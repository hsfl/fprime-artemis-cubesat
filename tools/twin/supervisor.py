#!/usr/bin/env python3
"""Run one real application behind per-container virtual UARTs."""
from __future__ import annotations

import argparse
import asyncio
import json
import os
from pathlib import Path
import signal
import time

from common import PtyBridge, log_event
from artifacts import verify

ROOT = Path(__file__).resolve().parents[2]
DATA = Path("/data")


class Supervisor:
    def __init__(self, role: str) -> None:
        self.role = role
        self.stop = asyncio.Event()
        self.process: asyncio.subprocess.Process | None = None
        self.power = False
        self.exit_code = 0
        host = os.environ.get("TWIN_DEVICES_HOST", "devices")
        ports = ({"pc": 6100, "gps": 6102, "pdu": 6103, "radio": 6104, "power": 6105, "ground": None}
                 if role == "fc" else {"fc": 6101})
        self.bridges = [PtyBridge(Path(f"/run/twin/{name}.tty"), host, port, DATA) for name, port in ports.items()]
        self.host = host

    async def start_application(self) -> None:
        if self.process is not None and self.process.returncode is None:
            return
        binary = Path("/artifacts") / self.role / "bin" / ("flight-controller" if self.role == "fc" else "payload-computer")
        if not binary.is_file():
            raise RuntimeError(f"Missing {binary}; run python3 tools/twin/twin.py build first")
        manifest = verify(self.role)
        (DATA / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        env = os.environ.copy()
        if self.role == "fc":
            env["ARTEMIS_TWIN_EPOCH_NS"] = str(time.time_ns())
            command = [str(binary), "--rt"]
            cwd = DATA
        else:
            fixture = ROOT / "ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv"
            if not fixture.is_file():
                raise RuntimeError(f"Recorded Lepton fixture missing: {fixture}")
            env.update(LEPTON_CAMERA_BACKEND="sample", C3M_LEPTON_SAMPLE_CSV=str(fixture))
            command = [str(binary), "-d", "/run/twin/fc.tty", "-b", "115200"]
            cwd = Path("/payload")
        self.process = await asyncio.create_subprocess_exec(*command, cwd=cwd, env=env,
                                                          stdout=asyncio.subprocess.PIPE,
                                                          stderr=asyncio.subprocess.STDOUT)
        asyncio.create_task(self.capture_log(self.process))
        log_event(DATA, "application_started", role=self.role, pid=self.process.pid)

    async def capture_log(self, process: asyncio.subprocess.Process) -> None:
        assert process.stdout is not None
        with (DATA / "application.log").open("ab") as stream:
            while line := await process.stdout.readline():
                stream.write(line)
                stream.flush()
                print(line.decode(errors="replace"), end="", flush=True)

    async def stop_application(self) -> None:
        if self.process is not None and self.process.returncode is None:
            self.process.terminate()
            try:
                await asyncio.wait_for(self.process.wait(), 5)
            except asyncio.TimeoutError:
                self.process.kill()
                await self.process.wait()
            log_event(DATA, "application_stopped", role=self.role, returncode=self.process.returncode)
        self.process = None

    async def power_control(self) -> None:
        while not self.stop.is_set():
            try:
                reader, writer = await asyncio.open_connection(self.host, 6106)
                while line := await reader.readline():
                    if len(line) > 64:
                        raise RuntimeError("oversized virtual power message")
                    if line.strip() not in (b"POWER ON", b"POWER OFF"):
                        continue
                    self.power = line.strip() == b"POWER ON"
                    log_event(DATA, "payload_power", enabled=self.power)
                    if self.power:
                        await self.start_application()
                    else:
                        await self.stop_application()
                writer.close()
                await writer.wait_closed()
            except (OSError, ConnectionError):
                await asyncio.sleep(0.25)

    async def monitor(self) -> None:
        while not self.stop.is_set():
            if self.process is not None and self.process.returncode is not None:
                code = self.process.returncode
                log_event(DATA, "application_exited", role=self.role, returncode=code)
                self.process = None
                if self.role == "fc":
                    self.exit_code = code or 0
                    self.stop.set()
                    return
                if self.power:
                    await asyncio.sleep(0.5)
                    await self.start_application()
            await asyncio.sleep(0.1)

    async def run(self) -> int:
        DATA.mkdir(parents=True, exist_ok=True)
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, self.stop.set)
        tasks = [asyncio.create_task(bridge.run()) for bridge in self.bridges]
        tasks.append(asyncio.create_task(self.monitor()))
        if self.role == "fc":
            await self.start_application()
        else:
            tasks.append(asyncio.create_task(self.power_control()))
        try:
            await self.stop.wait()
        finally:
            await self.stop_application()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            for bridge in self.bridges:
                bridge.close()
        return self.exit_code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("role", choices=("fc", "pi"))
    args = parser.parse_args()
    raise SystemExit(asyncio.run(Supervisor(args.role).run()))
