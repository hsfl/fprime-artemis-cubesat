#!/usr/bin/env python3
"""Explicit no-hardware HIL ground acceptance: real Yamcs + two local PTYs.

Uses real HIL manifests/dictionaries and a previously captured FDP fixture.
The PTY peer is test equipment: this does not execute flight code or RF.
"""
from __future__ import annotations
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import time
from urllib import error, parse, request

from common import crc16

ROOT = Path(__file__).resolve().parents[2]


def fetch(url, body=None):
    req = request.Request(url, None if body is None else json.dumps(body).encode(),
                          {'Content-Type': 'application/json'}, method='GET' if body is None else 'POST')
    with request.urlopen(req, timeout=5) as response:
        return response.read()


async def wait_for(description, predicate, timeout=90):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            result = await asyncio.to_thread(predicate)
            if result: return result
        except (OSError, error.URLError, ValueError) as exc:
            last = str(exc)
        await asyncio.sleep(0.1)
    raise RuntimeError(f'Timed out waiting for {description}: {last}')


async def pty_write(fd, data, timeout=30):
    offset = 0
    deadline = time.monotonic() + timeout
    while offset < len(data):
        if time.monotonic() >= deadline:
            raise RuntimeError('Timed out writing local PTY stream')
        try:
            offset += os.write(fd, data[offset:offset + 67])
        except BlockingIOError:
            pass
        await asyncio.sleep(0.001)


async def pty_read(fd, minimum=1, timeout=10):
    data = bytearray();deadline = time.monotonic()+timeout
    while len(data) < minimum and time.monotonic() < deadline:
        try: data.extend(os.read(fd, 4096))
        except BlockingIOError: pass
        await asyncio.sleep(0.005)
    if len(data) < minimum: raise RuntimeError('PTY returned no expected bytes')
    return bytes(data)


def science_packets(blob):
    chunks = [blob[n:n+35] for n in range(0, len(blob), 35)]
    header = b'N2\x01\x29' + struct.pack('<IIHBH', 901, len(blob), len(chunks), 35, crc16(blob))
    packets = []
    for index, chunk in enumerate(chunks):
        data = b'N2\x02\x29' + struct.pack('<H', index) + bytes((len(chunk),)) + chunk
        packets.append(data + struct.pack('<H', crc16(data)))
    end = b'N2\x03\x29' + struct.pack('<HH', len(chunks), crc16(blob))
    return header, packets, end


async def run(args):
    if os.name != 'posix': raise RuntimeError('PTY probe requires macOS/Linux/WSL')
    output = args.output.resolve();output.mkdir(parents=True, exist_ok=False)
    blob = args.fdp.read_bytes();run_id='serial-pty-'+str(time.time_ns())
    descriptors = [os.openpty(), os.openpty()]
    for master, slave in descriptors: os.set_blocking(master, False)
    ports = [os.ttyname(slave) for master, slave in descriptors]
    env = os.environ.copy()
    command = [sys.executable, str(ROOT/'tools/twin/ground.py'), '--transport','serial',
               '--ccsds-port',ports[0],'--science-port',ports[1],
               '--fc-artifacts',str(args.fc_artifacts.resolve()),'--pi-artifacts',str(args.pi_artifacts.resolve()),
               '--data-dir',str(output/'ground'),'--yamcs-data',str(output/'yamcs-data'),
               '--yamcs-port',str(args.yamcs_port),'--tcp-port',str(args.tcp_port),'--run-id',run_id]
    result = {'result':'IN_PROGRESS','scope':'Real Yamcs and serial adapter; local PTY peer; no flight execution/hardware/RF',
              'run_id':run_id,'source_fdp_sha256':hashlib.sha256(blob).hexdigest()}
    process = None
    try:
        with (output/'ground-process.log').open('wb') as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                       stdin=subprocess.DEVNULL, start_new_session=True)
        url=f'http://127.0.0.1:{args.yamcs_port}'
        def running():
            if process.poll() is not None: raise RuntimeError(f'Ground exited {process.returncode}; see ground-process.log')
            data=json.loads(fetch(url+'/api/instances'))
            return any(v['name']=='fprime-project' and v['state']=='RUNNING' for v in data.get('instances',[]))
        await wait_for('real Yamcs RUNNING', running)
        fc=json.loads((args.fc_artifacts/'dict/topology.json').read_text())
        names=json.loads(fetch(url+'/api/mdb/fprime-project/commands?q=PING&limit=1000'))['commands']
        candidates=[v for v in names if v['qualifiedName'].replace('|','/').endswith('/missionApp/PING')]
        if len(candidates)!=1: raise RuntimeError('Real Yamcs does not expose one missionApp.PING')
        submission=json.loads(fetch(url+'/api/processors/fprime-project/realtime/commands/'+parse.quote(candidates[0]['qualifiedName'],safe='/'),
                                    {'args':{},'origin':'serial-pty-probe','sequenceNumber':1}))
        command_bytes=await pty_read(descriptors[0][0],minimum=10)
        opcode=next(v['opcode'] for v in fc['commands'] if v['name'].endswith('.missionApp.PING'))
        opcode=int(opcode,0) if isinstance(opcode,str) else opcode
        if struct.pack('>I',opcode) not in command_bytes: raise RuntimeError('Expected opcode absent from raw CCSDS PTY uplink')
        result['yamcs_command']={'submission':submission,'opcode':opcode,'raw_ccsds_bytes':len(command_bytes),
                                 'wire_hex':command_bytes.hex(),'flight_completion':'Not asserted: PTY peer is not flight software'}
        header,data,end=science_packets(blob)
        # Missing packet zero must yield a real application RETRY on raw science USB.
        await pty_write(descriptors[1][0], header+b''.join(data[1:])+end)
        retry=await pty_read(descriptors[1][0],minimum=8,timeout=10)
        start=retry.find(b'N2\x04\x29')
        if start<0 or not retry[start+7]&1: raise RuntimeError('Missing DATA0 did not generate expected RETRY bitmap')
        result['science_retry_hex']=retry.hex()
        await pty_write(descriptors[1][0],data[0])
        def receipt():
            for path in (output/'ground/archived').glob('*.receipt.json'):
                value=json.loads(path.read_text())
                if value.get('run_id')==run_id: return value
            return None
        item=await wait_for('real Yamcs verified science receipt',receipt,30)
        received=fetch(url+item['download_path'])
        if received!=blob or hashlib.sha256(received).hexdigest()!=item['sha256']: raise RuntimeError('Actual Yamcs GET differs from PTY fixture')
        received_receipt=json.loads(fetch(url+'/api/storage/buckets/artemis-science/objects/'+parse.quote(item['object']+'.receipt.json',safe='')))
        if received_receipt!=item: raise RuntimeError('Actual Yamcs receipt GET differs')
        (output/'downloaded.fdp').write_bytes(received)
        result.update(result='PASS',raw_file_bytes=len(received),yamcs_exact_get=True,receipt=item,
                      recovered_data_packet=0,decoded_or_analyzed=False)
    except Exception as exc:
        result.update(result='FAIL',error=str(exc));raise
    finally:
        if process is not None:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try: await asyncio.to_thread(process.wait,20)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid,signal.SIGKILL);await asyncio.to_thread(process.wait)
                    result.update(result='FAIL',cleanup_error='Ground required SIGKILL')
            result['ground_exit_code']=process.returncode
            if process.returncode!=0:result['result']='FAIL'
        for master,slave in descriptors:
            os.close(master);os.close(slave)
        result['finished_at']=datetime.now(timezone.utc).isoformat()
        (output/'acceptance.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))
    if result['result']!='PASS':raise RuntimeError('Serial probe cleanup failed')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fc-artifacts',required=True,type=Path)
    parser.add_argument('--pi-artifacts',required=True,type=Path)
    parser.add_argument('--fdp',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--yamcs-port',default=18090,type=int)
    parser.add_argument('--tcp-port',default=15100,type=int)
    asyncio.run(run(parser.parse_args()))
