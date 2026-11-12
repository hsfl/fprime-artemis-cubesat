#!/usr/bin/env python3
"""Local Artemis twin launcher. Docker only; never connects to hardware."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / ".twin-runs/current.json"


def environment(new: bool = False) -> dict[str, str]:
    env = os.environ.copy()
    if new or not STATE.exists():
        run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(3)
        run = ROOT / ".twin-runs" / run_id
        for role in ("flight-controller", "payload-computer", "devices", "ground"):
            (run / role).mkdir(parents=True)
        metadata = {"run_id": run_id, "run_dir": str(run), "created_at": datetime.now(timezone.utc).isoformat()}
        STATE.write_text(json.dumps(metadata, indent=2) + "\n")
    else:
        metadata = json.loads(STATE.read_text())
    env.update(TWIN_RUN_ID=metadata["run_id"], TWIN_RUN_DIR=metadata["run_dir"])
    return env


def compose(arguments: list[str], env: dict[str, str]) -> int:
    return subprocess.run(["docker", "compose", "-f", str(ROOT / "twin/compose.yaml"), *arguments],
                          cwd=ROOT, env=env).returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build image and both real applications")
    build.add_argument("--image-only", action="store_true")
    build.add_argument("--role", choices=("fc", "pi", "pdu", "all"), default="all")
    commands.add_parser("up", help="start isolated services and create a run log directory")
    commands.add_parser("down", help="stop services; retain onboard/Yamcs data")
    commands.add_parser("status")
    logs = commands.add_parser("logs")
    logs.add_argument("service", nargs="?")
    commands.add_parser("test", help="run bounded local protocol/runtime unit tests")
    demo = commands.add_parser("demo", help="run the complete flow through Yamcs and verify archived products")
    demo.add_argument("--captures", type=int, default=3)
    demo.add_argument("--pi-off-repair", action="store_true")
    commands.add_parser("faults", help="exercise actual UART/RF/Yamcs failure and recovery")
    shell = commands.add_parser("shell", help="open the toolchain container")
    shell.add_argument("role", choices=("fc", "pi"), default="fc", nargs="?")
    args = parser.parse_args(argv)
    if args.command == "test":
        return subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tools/twin/tests"),
                               "-v"], cwd=ROOT).returncode
    if args.command == "demo":
        command = [sys.executable, str(ROOT / "tools/twin/demo.py"), "--captures", str(args.captures)]
        if args.pi_off_repair:
            command.append("--pi-off-repair")
        return subprocess.run(command, cwd=ROOT).returncode
    if args.command == "faults":
        return subprocess.run([sys.executable, str(ROOT / "tools/twin/faults.py")], cwd=ROOT).returncode
    env = environment(new=args.command == "up")
    if args.command == "build":
        rc = compose(["build"], env)
        if rc or args.image_only:
            return rc
        for role in (("pdu", "fc", "pi") if args.role == "all" else (args.role,)):
            service = "payload-computer" if role == "pi" else "flight-controller"
            rc = compose(["run", "--rm", "--no-deps", service, "python", "tools/twin/build.py", role], env)
            if rc:
                return rc
        return 0
    if args.command == "up":
        rc = compose(["up", "-d", "--no-build"], env)
        if rc == 0:
            print("Yamcs: http://127.0.0.1:8090")
            print("Run logs:", env["TWIN_RUN_DIR"])
        return rc
    if args.command == "down":
        return compose(["down", "--remove-orphans"], env)
    if args.command == "status":
        return compose(["ps"], env)
    if args.command == "logs":
        return compose(["logs", "--tail", "100", *([args.service] if args.service else [])], env)
    if args.command == "shell":
        service = "flight-controller" if args.role == "fc" else "payload-computer"
        return compose(["run", "--rm", "--no-deps", service, "bash"], env)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
