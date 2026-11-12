#!/usr/bin/env python3
"""Explicit offline compatibility check in the existing C3M app; never a runtime service.

Run with that app's existing Python environment and fprime-dp on PATH.
The input must be a file already downloaded from Yamcs.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import importlib.util
import json
import math
import shutil
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def check(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def fingerprint(root: Path) -> dict[str, str]:
    paths = [root / "ground-station/lepton-dp-viewer/lepton_dp_viewer.py",
             root / "ground-station/boson-viewer/boson_viewer.py",
             root / "ArtemisRpiTeensy_N2/tools/payload_receiver.py"]
    paths += [p for p in (root / "ground-station/c3m-payload-receiver-ui").rglob("*")
              if p.is_file() and p.suffix in (".py", ".js", ".html", ".css")]
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def verify(args: argparse.Namespace) -> dict:
    root, source, dictionary, output = (p.resolve() for p in
                                      (args.app_root, args.fdp, args.dictionary, args.output))
    check(not output.exists(), "Use a new output directory for each compatibility check")
    blob = source.read_bytes()
    check(struct.unpack(">HI", blob[:6]) == (5, 0x10027000), "Unexpected FDP descriptor/container identity")
    model = json.loads(dictionary.read_text())
    for category, suffix in (("containers", ".ThermalImageContainer"), ("records", ".ThermalImageRecord")):
        entries = [v for v in model[category] if v['name'].endswith(suffix)]
        check(len(entries) == 1 and int(entries[0]['id']) == 0x10027000, f"Unexpected {category} identity")
    before = fingerprint(root)
    decoder_cli = shutil.which('fprime-dp')
    check(decoder_cli is not None, 'Existing app requires fprime-dp on PATH')
    path = root / "ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py"
    spec = importlib.util.spec_from_file_location("unchanged_c3m_app", path)
    check(spec is not None and spec.loader is not None, "Cannot load existing C3M app")
    app = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = app
    spec.loader.exec_module(app)
    controller = app.ReceiverController(output, dictionary=dictionary)
    try:
        check(controller._classify_product(source, None) == "lepton", "Existing classifier rejected file")
        # No decode_fn override: run the actual app receiver, classifier, decoder and History.
        controller.connect_replay(blob, delay_s=0)
        controller.thread.join(timeout=60)
        check(not controller.thread.is_alive(), "Existing app replay timed out")
        history = controller.history()
        # The existing app finalizes/decodes on a separate thread after reception.
        deadline = time.monotonic() + 60
        while not history and time.monotonic() < deadline:
            if controller.snapshot()['current']['status'] == 'failed':
                raise RuntimeError(str(controller.snapshot()['current'].get('failure_reason')))
            time.sleep(0.1)
            history = controller.history()
        check(len(history) == 1, "Expected one fresh History entry")
        item = history[0]
        check(item['result'] == 'complete', f"Existing app failed: {item.get('failure_reason')}")
        check(item['product_kind'] == 'lepton' and item['crc_ok'], "Product classification/CRC failed")
        check(item['sha256'] == hashlib.sha256(blob).hexdigest(), "App archived different FDP bytes")
        decoded = item['decode']
        check((decoded['width'], decoded['height'], decoded['pixels']) == (160, 120, 19200), "Wrong image shape")
        directory = output / item['run_id']
        with (directory / item['outputs']['csv']).open() as stream:
            rows = list(csv.reader(line for line in stream if not line.startswith('#')))
        check(len(rows) == 120 and all(len(row) == 160 for row in rows), "Wrong CSV shape")
        fixture = args.fixture.resolve()
        with fixture.open() as stream:
            expected = [float(v) for row in csv.reader(line for line in stream if not line.startswith('#')) for v in row]
        actual = [float(v) for row in rows for v in row]
        check(len(expected) == 19200 and all(math.isclose(a, b, rel_tol=0, abs_tol=0.0051)
                                            for a, b in zip(actual, expected)), "Temperatures differ from capture fixture")
        png = directory / item['outputs']['png']
        check(png.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), "Existing app did not produce PNG")
        check((directory / item['outputs']['fdp']).read_bytes() == blob, "Archived FDP bytes differ")
        check(before == fingerprint(root), "Existing app source changed during verification")
        result = {'result': 'PASS', 'file': str(source), 'sha256': item['sha256'],
                  'container_id': '0x10027000', 'record_id': '0x10027000',
                  'dictionary_sha256': hashlib.sha256(dictionary.read_bytes()).hexdigest(),
                  'app_root': str(root), 'app_sources_unchanged': True, 'app_source_sha256': before,
                  'fprime_gds_version': importlib.metadata.version('fprime-gds'),
                  'python_executable': sys.executable, 'fprime_dp_executable': decoder_cli,
                  'fprime_dp_launcher_sha256': hashlib.sha256(Path(decoder_cli).read_bytes()).hexdigest(),
                  'history_result': item['result'], 'product_kind': item['product_kind'],
                  'width': 160, 'height': 120, 'samples': 19200, 'fixture_temperatures_equal': True,
                  'existing_app_png': str(png), 'existing_app_history': str(directory / 'run.json')}
        (output / 'compatibility.json').write_text(json.dumps(result, indent=2) + '\n')
        return result
    finally:
        controller.stop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-root', required=True, type=Path)
    parser.add_argument('--fdp', required=True, type=Path)
    parser.add_argument('--dictionary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--fixture', type=Path, default=ROOT / 'ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv')
    print(json.dumps(verify(parser.parse_args()), indent=2))
