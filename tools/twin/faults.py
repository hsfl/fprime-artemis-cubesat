#!/usr/bin/env python3
"""Real split-platform failure/recovery acceptance; no fabricated packets/products."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time
from urllib import error, parse

from demo import Probe, ROOT
from common import crc16
from twin import compose, environment


def run():
    current = json.loads((ROOT / '.twin-runs/current.json').read_text())
    directory = Path(current['run_dir'])
    probe = Probe('http://127.0.0.1:8090', 'http://127.0.0.1:8092')
    evidence = {'result': 'IN_PROGRESS', 'run_id': current['run_id'], 'cases': []}
    paused = False

    def receipts(product):
        return [item for item in probe.receipt_list() if item['product_id'] == product and item['run_id'] == current['run_id']]

    def capture():
        record = {}
        record['base'] = probe.issue('missionApp.ENTER_BASE_MODE')
        probe.wait_value('missionApp.CurrentMode', {'BASE', 2}, 30)
        probe.wait_value('pcLinkManager.PayloadState', {'READY', 2}, 60)
        record['configure'] = probe.issue('scienceCoordinator.CONFIGURE_CAPTURE_DURATION', {'durationSeconds': 1})
        record['schedule'] = probe.issue('missionApp.SCHEDULE_COLLECTION', {'delaySeconds': 2})
        probe.wait_value('scienceCoordinator.JobState', {'SCHEDULED', 'CAPTURING', 'STAGING', 1, 2, 3}, 30)
        return record

    def cached(record):
        record['cache_ready'] = probe.wait_value('scienceCoordinator.JobState', {'CACHE_READY', 4})
        record['product_id'] = int(probe.value(probe.parameter('scienceCoordinator.ProductId')))
        record['bytes'] = int(probe.value(probe.parameter('scienceCoordinator.ProductBytes')))
        probe.wait_value('scienceCoordinator.CacheValid', {True, 'true', 1}, 10)

    def verified(record):
        receipt = probe.wait('verified actual archived product', lambda: next(iter(receipts(record['product_id'])), None))
        blob = probe.api('/api/storage/buckets/artemis-science/objects/' + parse.quote(receipt['object'], safe=''), raw=True)
        if len(blob) != record['bytes'] or hashlib.sha256(blob).hexdigest() != receipt['sha256'] or crc16(blob) != receipt['crc16']:
            raise RuntimeError('Independent Yamcs GET mismatch')
        record['receipt'] = receipt

    try:
        case = {'case': 'invalid_command_argument_rejected'}
        evidence['cases'].append(case)
        print('Running:', case['case'], flush=True)
        try:
            probe.issue('scienceCoordinator.CONFIGURE_CAPTURE_DURATION', {'durationSeconds': 'invalid-U32'})
            raise RuntimeError('Yamcs accepted a nonnumeric U32 command argument')
        except error.HTTPError as exc:
            if exc.code != 400:
                raise
            case.update(result='PASS', http_status=exc.code, rejection=exc.read().decode())

        # Cancel a real long capture, then prove the local completion barrier
        # permits a clean new capture rather than cancelling the replacement.
        case = {'case': 'active_capture_cancel_recovery'}
        evidence['cases'].append(case)
        print('Running:', case['case'], flush=True)
        case['base'] = probe.issue('missionApp.ENTER_BASE_MODE')
        probe.wait_value('missionApp.CurrentMode', {'BASE', 2}, 30)
        probe.wait_value('pcLinkManager.PayloadState', {'READY', 2}, 60)
        case['duration'] = probe.issue('scienceCoordinator.CONFIGURE_CAPTURE_DURATION', {'durationSeconds': 30})
        case['schedule'] = probe.issue('missionApp.SCHEDULE_COLLECTION', {'delaySeconds': 2})
        case['capturing'] = probe.wait_value('scienceCoordinator.JobState', {'CAPTURING', 2}, 30)
        case['payload_busy'] = probe.wait_value('pcLinkManager.PayloadState', {'BUSY', 3}, 30)
        case['cancel'] = probe.issue('missionApp.CANCEL_COLLECTION')
        case['cancelled'] = probe.wait_value('scienceCoordinator.JobState', {'CANCELLED', 8}, 15)
        case['fresh_ready'] = probe.wait_value('pcLinkManager.PayloadState', {'READY', 2}, 15)
        case['recovery'] = capture()
        cached(case['recovery'])
        case['recovery']['downlink'] = probe.issue('missionApp.REQUEST_SCIENCE_DOWNLINK')
        verified(case['recovery'])
        case['result'] = 'PASS'
        print('PASS:', case['case'], flush=True)

        # Break an in-progress UART stage. A partial cache cannot become READY.
        case = {'case': 'pc_link_mid_stage', **capture()}
        evidence['cases'].append(case)
        print('Running:', case['case'], flush=True)
        probe.wait_value('scienceCoordinator.JobState', {'STAGING', 3}, 60)
        probe.wait('partial stage bytes', lambda: 0 < int(probe.value(probe.parameter('scienceCoordinator.CacheReceivedBytes'))) < int(probe.value(probe.parameter('scienceCoordinator.ProductBytes'))), 10)
        case['fault'] = probe.fault(pc_connected=False)
        case['failed'] = probe.wait_value('scienceCoordinator.JobState', {'FAILED', 7}, 25)
        case['invalid_cache'] = probe.wait_value('scienceCoordinator.CacheValid', {False, 'false', 0}, 10)
        probe.fault(pc_connected=True)
        case['power_cycle'] = probe.issue('missionApp.ENTER_STANDBY_MODE')
        probe.wait_value('missionApp.CurrentMode', {'STANDBY', 0}, 30)
        case['recovery'] = capture()
        cached(case['recovery'])
        case['recovery']['downlink'] = probe.issue('missionApp.REQUEST_SCIENCE_DOWNLINK')
        verified(case['recovery'])
        case['result'] = 'PASS'
        print('PASS:', case['case'], flush=True)

        # Science RF has no link ACK; local SENT does not prove ground receipt.
        case = {'case': 'radio_off_retains_cache', **capture()}
        evidence['cases'].append(case)
        print('Running:', case['case'], flush=True)
        cached(case)
        case['first_downlink'] = probe.issue('missionApp.REQUEST_SCIENCE_DOWNLINK')
        case['fault'] = probe.fault(radio_connected=False)
        # Give the real sender its bounded packet drain and receiver timeout.
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            if receipts(case['product_id']):
                raise RuntimeError('Radio-off product unexpectedly obtained verified receipt')
            time.sleep(0.5)
        probe.fault(radio_connected=True)
        case['retained'] = probe.wait_value('scienceCoordinator.CacheValid', {True, 'true', 1}, 10)
        case['resend'] = probe.issue('missionApp.REQUEST_SCIENCE_DOWNLINK')
        verified(case)
        case['result'] = 'PASS'
        print('PASS:', case['case'], flush=True)

        # Stop the actual Java server while RF and the ground outbox continue.
        case = {'case': 'yamcs_unavailable_outbox_recovery', **capture()}
        evidence['cases'].append(case)
        print('Running:', case['case'], flush=True)
        cached(case)
        case['downlink'] = probe.issue('missionApp.REQUEST_SCIENCE_DOWNLINK')
        if compose(['exec', '-T', 'ground', 'python', 'tools/twin/yamcs_fault.py', 'pause'], environment()):
            raise RuntimeError('Could not pause actual Yamcs')
        paused = True
        def pending():
            for path in (directory / 'ground/outbox').glob('*.json'):
                item = json.loads(path.read_text())
                if item.get('product_id') == case['product_id'] and item.get('state') == 'pending':
                    return item
            return None
        case['pending_outbox'] = probe.wait('actual received product pending while Yamcs paused', pending, 90)
        if compose(['exec', '-T', 'ground', 'python', 'tools/twin/yamcs_fault.py', 'resume'], environment()):
            raise RuntimeError('Could not resume actual Yamcs')
        paused = False
        verified(case)
        case['result'] = 'PASS'
        print('PASS:', case['case'], flush=True)
        evidence['result'] = 'PASS'
    except Exception as exc:
        evidence.update(result='FAIL', error=str(exc))
        raise
    finally:
        cleanup_errors = []
        if paused:
            if compose(['exec', '-T', 'ground', 'python', 'tools/twin/yamcs_fault.py', 'resume'], environment()):
                cleanup_errors.append('Yamcs resume failed')
        try:
            probe.fault(pc_connected=True, radio_connected=True, drop_science_packet_once=-1, drop_science_packet_always=-1)
        except Exception as exc:
            cleanup_errors.append(str(exc))
        if cleanup_errors:
            evidence.update(result='FAIL', cleanup_errors=cleanup_errors)
        evidence['completed_at'] = datetime.now(timezone.utc).isoformat()
        path = directory / 'acceptance-faults.json'
        path.write_text(json.dumps(evidence, indent=2) + '\n')
        print(json.dumps(evidence, indent=2), flush=True)
        print('Evidence:', path)
    if evidence['result'] != 'PASS':
        raise RuntimeError('Fault acceptance cleanup failed: ' + str(evidence.get('cleanup_errors')))


if __name__ == '__main__':
    run()
