#!/usr/bin/env python3
"""Dependency-only Yamcs/XTCE startup probe using the real FC dictionary.

No telemetry or science is fabricated. This does not claim mission or split-
platform acceptance; the normal ground service still requires both artifacts.
"""
import asyncio
import hashlib
import json
from pathlib import Path
import shutil
import time
from urllib import request

from artifacts import verify
from ground import Ground, DATA


async def probe():
    metadata = verify("fc")
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / "dictionaries").mkdir(exist_ok=True)
    shutil.copy2(Path("/artifacts/fc/dict/topology.json"), DATA / "dictionaries/fc.json")
    # The real bridge process connects here; the dependency test sends no bytes.
    async def hold(reader, writer):
        try:
            await reader.read()
        finally:
            writer.close()
    server = await asyncio.start_server(hold, "127.0.0.1", 5100)
    context = object.__new__(Ground)
    context.stop = asyncio.Event()
    context.exit_code = 0
    context.yamcs = None
    result = {"result": "IN_PROGRESS", "scope": "Yamcs/XTCE dependency startup only; no FSW frames or science",
              "fc_dictionary_sha256": metadata["dictionary_sha256"]}
    try:
        await context.start_yamcs()
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            def state():
                with request.urlopen("http://127.0.0.1:8090/api/instances", timeout=2) as response:
                    return json.load(response)
            try:
                instances = await asyncio.to_thread(state)
                if any(item["name"] == "fprime-project" and item["state"] == "RUNNING"
                       for item in instances.get("instances", [])):
                    def definitions(kind, query):
                        with request.urlopen(f"http://127.0.0.1:8090/api/mdb/fprime-project/{kind}?q={query}&limit=1000", timeout=3) as response:
                            return json.load(response)
                    commands = await asyncio.to_thread(definitions, "commands", "ENTER_BASE_MODE")
                    parameters = await asyncio.to_thread(definitions, "parameters", "JobState")
                    if not any(item["qualifiedName"].replace("|", "/").endswith("/missionApp/ENTER_BASE_MODE")
                               for item in commands.get("commands", [])):
                        raise RuntimeError("Generated Yamcs MDB does not contain the real FC mission command")
                    result.update(result="PASS", instances=instances, mission_command_lookup=commands,
                                  science_parameter_lookup=parameters)
                    return result
            except OSError:
                pass
            if context.stop.is_set():
                raise RuntimeError("Yamcs launcher exited before RUNNING")
            await asyncio.sleep(0.5)
        raise RuntimeError("Yamcs did not reach RUNNING within 90 seconds")
    except Exception as exc:
        result.update(result="FAIL", error=str(exc))
        raise
    finally:
        context.stop.set()
        await context.stop_yamcs()
        server.close()
        await server.wait_closed()
        (DATA / "yamcs-dependency-probe.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)


if __name__ == "__main__":
    asyncio.run(probe())
