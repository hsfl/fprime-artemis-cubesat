#!/usr/bin/env python3
"""Run the ground-only N2 CDR Yamcs client against a Zero W TCP server."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import time
from urllib import error, request

import yaml

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "yamcs"
VENV = TOOLS / ".venv"
PACED_BRIDGE = TOOLS / "cdr_comm_bridge.py"
DICTIONARY_MARKER = "N2CdrTcp.ENABLED"
UDP_HELPER_PORTS = (50000, 50001, 50002)
HTTP_PORT_DEFAULT = 8090
DEFAULT_TCP_PORT = 50000
DEFAULT_HEAP = "1024m"


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def validate_heap(value: str) -> str:
    if not re.fullmatch(r"[1-9][0-9]*(?:k|m|g)?", value, flags=re.IGNORECASE):
        raise argparse.ArgumentTypeError("heap must look like 1024m, 2g, or 512k")
    return value


def validate_frame_interval(value: str) -> float:
    try:
        interval = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("frame interval must be a number of milliseconds") from exc
    if not math.isfinite(interval) or interval <= 0:
        raise argparse.ArgumentTypeError("frame interval must be finite and greater than zero")
    return interval


def validate_marker(dictionary: Path) -> None:
    try:
        data = json.loads(dictionary.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read dictionary {dictionary}: {exc}") from exc
    marker = next((item for item in data.get("constants", [])
                   if item.get("qualifiedName") == DICTIONARY_MARKER), None)
    if marker is None or marker.get("value") is not True:
        raise ValueError(
            f"dictionary must contain {DICTIONARY_MARKER}=true; build the N2 CDR TCP profile first"
        )


def check_tcp_target(address: str, port: int) -> None:
    if not address.strip():
        raise ValueError("--address must name the Zero W TCP server")
    if address in {"0.0.0.0", "::"}:
        raise ValueError("--address must be a connectable Zero W address, not a wildcard")
    if not 1 <= port <= 65535:
        raise ValueError("--port must be between 1 and 65535")


def check_local_ports(http_bind: str, http_port: int) -> None:
    addresses = [http_bind]
    if http_bind in ("0.0.0.0", "::"):
        addresses = ["0.0.0.0" if ":" not in http_bind else "::"]
    with socket.socket(socket.AF_INET6 if ":" in addresses[0] else socket.AF_INET,
                       socket.SOCK_STREAM) as sock:
        try:
            sock.bind((addresses[0], http_port))
        except OSError as exc:
            raise ValueError(f"Yamcs HTTP port {http_bind}:{http_port} is unavailable: {exc}") from exc
    # Yamcs 5.12's pinned UDP TM links bind wildcard sockets. Probe loopback
    # first so we do not collide with another local stack; their wildcard bind
    # limitation is recorded in the run manifest and command help.
    for port in UDP_HELPER_PORTS:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.bind(("127.0.0.1", port))
            except OSError as exc:
                raise ValueError(f"Yamcs helper UDP port 127.0.0.1:{port} is unavailable: {exc}") from exc


def default_run_dir() -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return ROOT / "yamcs" / "run" / f"cdr-tcp-{stamp}-{secrets.token_hex(3)}"


def prepare_run_dir(requested: str | None) -> Path:
    run_dir = Path(requested).expanduser().resolve() if requested else default_run_dir()
    run_dir.mkdir(parents=True, exist_ok=True)
    reserved = ("manifest.json", "yamcs-config", "yamcs-data", "gds-logs", "logs",
                "java-wrapper", "signing-key", "received-files")
    occupied = [name for name in reserved if (run_dir / name).exists()]
    if occupied:
        raise ValueError(f"run directory already contains reserved paths: {', '.join(occupied)}")
    return run_dir


def copy_yamcs_config(run_dir: Path, http_bind: str, http_port: int) -> Path:
    source = ROOT / "yamcs" / "config"
    destination = run_dir / "yamcs-config"
    shutil.copytree(source, destination)

    global_path = destination / "etc" / "yamcs.yaml"
    global_config = yaml.safe_load(global_path.read_text())
    http_servers = [service for service in global_config.get("services", [])
                    if service.get("class", "").endswith("HttpServer")]
    if len(http_servers) != 1:
        raise ValueError(f"expected one Yamcs HttpServer service, found {len(http_servers)}")
    http_servers[0].setdefault("args", {})["address"] = http_bind
    http_servers[0]["args"]["port"] = http_port
    signing_key = secrets.token_hex(32)
    (run_dir / "signing-key").write_text(signing_key + "\n")
    (run_dir / "signing-key").chmod(0o600)
    global_config["secretKey"] = signing_key
    global_path.write_text(yaml.safe_dump(global_config, sort_keys=False))

    instance_path = destination / "etc" / "yamcs.fprime-project.yaml"
    instance_config = yaml.safe_load(instance_path.read_text())
    links = {entry.get("name"): entry for entry in instance_config.get("dataLinks", [])}
    # The bridge and TC destination use only loopback. The bundled Yamcs UDP
    # receive links do not expose a bind-address setting; see manifest/help.
    if "UDP_TC_OUT" in links:
        links["UDP_TC_OUT"]["host"] = "127.0.0.1"
    file_packet_services = [service for service in instance_config.get("services", [])
                            if service.get("class", "").endswith("FprimeFilePacketService")]
    if len(file_packet_services) != 1:
        raise ValueError(f"expected one FprimeFilePacketService, found {len(file_packet_services)}")
    file_packet_services[0].setdefault("args", {})["downlinkMirrorDir"] = str(
        run_dir / "received-files")
    instance_path.write_text(yaml.safe_dump(instance_config, sort_keys=False))
    return destination


def configure_heap_wrapper(run_dir: Path, heap: str, environment: dict[str, str]) -> Path:
    # fprime-yamcs 0.2.4 hard-codes -Xmx4g. Resolve its selected Java before
    # placing a small wrapper first in JAVA_HOME; the wrapper substitutes only
    # that exact argument and forwards every other argument unchanged.
    try:
        from fprime_yamcs.java import find_java
        real_java = str(find_java().resolve())
    except Exception as exc:
        raise ValueError(f"cannot resolve the Java runtime selected by fprime-yamcs: {exc}") from exc
    wrapper_root = run_dir / "java-wrapper"
    wrapper_bin = wrapper_root / "bin"
    wrapper_bin.mkdir(parents=True)
    wrapper = wrapper_bin / "java"
    wrapper.write_text(
        "#!/usr/bin/env python3\n"
        "import os, sys\n"
        "real = os.environ['N2_CDR_TCP_REAL_JAVA']\n"
        "heap = os.environ['N2_CDR_TCP_HEAP']\n"
        "args = ['-Xmx' + heap if arg == '-Xmx4g' else arg for arg in sys.argv[1:]]\n"
        "os.execv(real, [real, *args])\n"
    )
    wrapper.chmod(0o755)
    environment["JAVA_HOME"] = str(wrapper_root)
    environment["N2_CDR_TCP_REAL_JAVA"] = real_java
    environment["N2_CDR_TCP_HEAP"] = heap
    return wrapper


def http_ready(base_url: str, timeout: float, process: subprocess.Popen,
               bridge_process: subprocess.Popen | None = None,
               cancel_requested=None) -> bool:
    deadline = time.monotonic() + timeout
    endpoint = base_url.rstrip("/") + "/api/instances"
    while time.monotonic() < deadline:
        if cancel_requested is not None and cancel_requested():
            return False
        if process.poll() is not None:
            raise RuntimeError(f"fprime-yamcs exited during startup with code {process.returncode}")
        if bridge_process is not None and bridge_process.poll() is not None:
            raise RuntimeError(f"paced F' bridge exited during startup with code {bridge_process.returncode}")
        try:
            with request.urlopen(endpoint, timeout=2) as response:
                if response.status == 200:
                    payload = json.loads(response.read())
                    instances = payload.get("instances", []) if isinstance(payload, dict) else []
                    if any(instance.get("name") == "fprime-project"
                           and instance.get("state") == "RUNNING" for instance in instances):
                        return True
        except (error.URLError, TimeoutError, OSError, json.JSONDecodeError):
            pass
        if cancel_requested is not None and cancel_requested():
            return False
        time.sleep(0.25)
    return False


def stop_process_group(process: subprocess.Popen, grace_seconds: float = 8.0) -> None:
    if os.name == "posix":
        process_group = process.pid  # Popen is started with start_new_session=True.

        def group_exists() -> bool:
            try:
                os.killpg(process_group, 0)
                return True
            except ProcessLookupError:
                return False
            except PermissionError:
                return True

        def signal_and_wait(sig: int, timeout: float) -> bool:
            try:
                # Signal the owned process group even if Popen's leader already exited.
                os.killpg(process_group, sig)
            except ProcessLookupError:
                return True
            except OSError:
                pass
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                process.poll()  # reap the leader when it has exited
                if not group_exists():
                    return True
                time.sleep(0.05)
            process.poll()
            return not group_exists()

        for sig, timeout in ((signal.SIGINT, grace_seconds),
                             (signal.SIGTERM, 3.0),
                             (signal.SIGKILL, 3.0)):
            if signal_and_wait(sig, timeout):
                break
        if process.poll() is None:
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                pass
        return

    if process.poll() is not None:
        return
    try:
        process.send_signal(signal.CTRL_BREAK_EVENT)
        process.wait(timeout=grace_seconds)
        return
    except (OSError, subprocess.TimeoutExpired):
        pass
    try:
        process.terminate()
        process.wait(timeout=3)
        return
    except (OSError, subprocess.TimeoutExpired):
        pass
    try:
        process.kill()
    except OSError:
        pass
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        pass


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=("Run Pi 5 Yamcs and a local bridge using stock F' TCP/UDP adapters against the "
                     "Pi Zero W stock Drv.TcpServer. No spacecraft process or network mux is started."),
        epilog=("The independent physical Pi-to-Teensy UART mux channel 2 remains for PDU RPC. "
                "The bridge's UDP endpoints and TC target use loopback. Yamcs 5.12's built-in UDP "
                "receive links bind wildcard sockets and expose no bind-address option; host firewall "
                "policy controls access to those helper ports."),
    )
    parser.add_argument("--dictionary", required=True, help="matching F′ CDR TCP topology JSON dictionary")
    parser.add_argument("--address", required=True, help="connectable IPv4/host address of the Zero W")
    parser.add_argument("--port", type=int, default=DEFAULT_TCP_PORT, help=f"Zero W TCP server port (default {DEFAULT_TCP_PORT})")
    parser.add_argument("--http-bind", default="127.0.0.1", help="Yamcs HTTP bind address (default: loopback)")
    parser.add_argument("--http-port", type=int, default=HTTP_PORT_DEFAULT, help=f"Yamcs HTTP port (default {HTTP_PORT_DEFAULT})")
    parser.add_argument("--heap", type=validate_heap, default=DEFAULT_HEAP,
                        help=f"Yamcs JVM max heap; replaces fprime-yamcs's fixed 4g (default {DEFAULT_HEAP})")
    parser.add_argument("--frame-interval-ms", type=validate_frame_interval, default=20.0,
                        help="minimum gap between outgoing TM UDP frames (default/minimum: 20 ms)")
    parser.add_argument("--run-dir", help="new or existing empty-of-reserved-paths run directory; default is ignored yamcs/run/cdr-tcp-*")
    parser.add_argument("--startup-timeout", type=float, default=90.0, help="seconds to wait for Yamcs HTTP readiness")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        dictionary = Path(args.dictionary).expanduser().resolve(strict=True)
        if not dictionary.is_file():
            raise ValueError(f"dictionary is not a file: {dictionary}")
        validate_marker(dictionary)
        check_tcp_target(args.address, args.port)
        if not 1 <= args.http_port <= 65535:
            raise ValueError("--http-port must be between 1 and 65535")
        if not math.isfinite(args.startup_timeout) or args.startup_timeout <= 0:
            raise ValueError("--startup-timeout must be finite and positive")
        check_local_ports(args.http_bind, args.http_port)
        run_dir = prepare_run_dir(args.run_dir)
        config_dir = copy_yamcs_config(run_dir, args.http_bind, args.http_port)
        environment = os.environ.copy()
        configure_heap_wrapper(run_dir, args.heap, environment)
        environment["PATH"] = str(VENV / "bin") + os.pathsep + environment.get("PATH", "")
        environment["FPRIME_DICTIONARY"] = str(dictionary)
        environment["FPRIME_YAMCS_INSTANCE"] = "fprime-project"
        environment["FPRIME_YAMCS_TM_INJECT_PORT"] = "50002"
        environment["FPRIME_DOWNLINK_DIR"] = str(run_dir / "received-files")
        (run_dir / "received-files").mkdir()
    except (OSError, ValueError, yaml.YAMLError) as exc:
        parser.error(str(exc))

    fprime_yamcs = VENV / "bin" / "fprime-yamcs"
    if not fprime_yamcs.is_file():
        parser.error(f"missing {fprime_yamcs}; run tools/yamcs/bootstrap.sh first")
    if not PACED_BRIDGE.is_file():
        parser.error(f"missing local paced bridge {PACED_BRIDGE}")

    manifest_path = run_dir / "manifest.json"
    dictionary_hash = hashlib.sha256(dictionary.read_bytes()).hexdigest()
    manifest = {
        "status": "starting",
        "started_at": now_utc(),
        "pid": os.getpid(),
        "run_dir": str(run_dir),
        "dictionary": str(dictionary),
        "dictionary_sha256": dictionary_hash,
        "transport": "Pi Zero W Drv.TcpServer <-> stock F' TCP/UDP adapters with local paced TM egress <-> Yamcs; direct ComCcsds and stock FileDownlink",
        "paced_tm_egress": {"frame_interval_ms": max(args.frame_interval_ms, 20.0), "default": 20.0,
                             "catch_up_bursts": False},
        "network_mux": False,
        "physical_uart_mux": {"channel": 2, "purpose": "Teensy/PDU RPC", "independent_of_tcp": True},
        "tcp_peer": {"address": args.address, "port": args.port},
        "yamcs_http": {"bind": args.http_bind, "port": args.http_port},
        "heap": args.heap,
        "no_app": True,
        "helper_udp": {
            "bridge_bind": "127.0.0.1", "bridge_peer": "127.0.0.1",
            "ports": list(UDP_HELPER_PORTS),
            "yamcs_receive_binding": "wildcard in pinned Yamcs 5.12 UdpTmFrameLink/UdpTmDataLink; no supported bind-address option",
        },
        "config_dir": str(config_dir),
    }
    atomic_json(manifest_path, manifest)
    bridge_command = [
        str(VENV / "bin" / "python"), "-u", str(PACED_BRIDGE),
        "--dictionary", str(dictionary), "--address", args.address,
        "--port", str(args.port), "--frame-interval-ms", str(args.frame_interval_ms),
    ]
    command = [
        str(fprime_yamcs), "--no-app", "--dictionary", str(dictionary),
        "--communication-selection", "none",
        "--udp-tm-inject-port", "50002",
        "--yamcs-config-dir", str(config_dir),
        "--yamcs-data-dir", str(run_dir / "yamcs-data"),
        "--logs", str(run_dir / "gds-logs"), "--log-directly",
        "--log-to-stdout", "--skip-browser-open",
    ]
    manifest["paced_bridge_command"] = bridge_command
    manifest["command"] = command
    atomic_json(manifest_path, manifest)
    print(f"CDR run manifest: {manifest_path}", flush=True)
    print(f"Starting ground stack for {args.address}:{args.port} (no spacecraft app, paced stock bridge)", flush=True)

    process = None
    bridge_process = None
    interrupted = False

    def request_stop(_signum, _frame):
        nonlocal interrupted
        interrupted = True

    previous_handlers = {sig: signal.signal(sig, request_stop) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        bridge_process = subprocess.Popen(
            bridge_command, cwd=ROOT, env=environment,
            start_new_session=(os.name == "posix"),
        )
        manifest["paced_bridge_pid"] = bridge_process.pid
        manifest["paced_bridge_process_group_id"] = bridge_process.pid if os.name == "posix" else None
        atomic_json(manifest_path, manifest)
        process = subprocess.Popen(command, cwd=ROOT, env=environment, start_new_session=(os.name == "posix"))
        manifest["fprime_yamcs_pid"] = process.pid
        manifest["process_group_id"] = process.pid if os.name == "posix" else None
        atomic_json(manifest_path, manifest)
        print(f"PIDs: launcher={os.getpid()} paced-bridge={bridge_process.pid} fprime-yamcs={process.pid}", flush=True)
        http_host = "127.0.0.1" if args.http_bind in ("0.0.0.0", "::") else args.http_bind
        ready_url = f"http://{http_host}:{args.http_port}"
        ready_start = time.monotonic()
        ready = http_ready(ready_url, args.startup_timeout, process, bridge_process,
                           cancel_requested=lambda: interrupted)
        if interrupted:
            manifest["status"] = "stopped"
            manifest["stopped_at"] = now_utc()
            manifest["exit_code"] = 130
            return 130
        if not ready:
            raise TimeoutError(f"Yamcs did not become HTTP-ready within {args.startup_timeout:g}s")
        manifest["status"] = "ready"
        manifest["ready_at"] = now_utc()
        manifest["startup_seconds"] = round(time.monotonic() - ready_start, 3)
        atomic_json(manifest_path, manifest)
        print(f"Yamcs ready at {ready_url} after {manifest['startup_seconds']:.3f}s", flush=True)
        while process.poll() is None and bridge_process.poll() is None and not interrupted:
            time.sleep(0.25)
        if bridge_process.poll() is not None and not interrupted:
            result = 1
            manifest["status"] = "paced_bridge_exited"
            manifest["paced_bridge_exit_code"] = bridge_process.returncode
            manifest["stopped_at"] = now_utc()
            manifest["exit_code"] = result
            print(f"Paced F' bridge exited with code {bridge_process.returncode}", file=sys.stderr)
            return result
        result = process.returncode if process.returncode is not None else 130
        manifest["status"] = "stopped" if interrupted else "exited"
        manifest["stopped_at"] = now_utc()
        manifest["exit_code"] = result
        return result
    except (OSError, TimeoutError, RuntimeError) as exc:
        manifest["status"] = "startup_failed"
        manifest["error"] = str(exc)
        manifest["stopped_at"] = now_utc()
        print(f"CDR TCP launcher failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if process is not None:
            stop_process_group(process)
            if process.poll() is not None:
                manifest.setdefault("exit_code", process.returncode)
        if bridge_process is not None:
            stop_process_group(bridge_process)
            if bridge_process.poll() is not None:
                manifest["paced_bridge_exit_code"] = bridge_process.returncode
        atomic_json(manifest_path, manifest)
        for sig, handler in previous_handlers.items():
            signal.signal(sig, handler)


if __name__ == "__main__":
    raise SystemExit(main())
