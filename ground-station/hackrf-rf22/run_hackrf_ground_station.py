#!/usr/bin/env python3
"""Launch and supervise the HackRF bridge, F Prime GDS, and C3M payload UI."""

from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.request
import webbrowser
from dataclasses import dataclass
from pathlib import Path

from bridge_core import atomic_write_json
from rf22_protocol import DEFAULT_PROFILE
from rf_safety import (
    BASELINE_RF_PATH_LABEL,
    BASELINE_RX_LNA_GAIN_DB,
    BASELINE_RX_VGA_GAIN_DB,
    BASELINE_TX_GAIN_DB,
    BASELINE_TX_LEADING_MS,
    BASELINE_TX_MODE,
    TxSafetyError,
    validate_tx_request,
)


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
FPRIME_ROOT = REPO_ROOT / "ArtemisRpiTeensy_N2"
FPRIME_VENV = FPRIME_ROOT / "fprime-venv"
DEFAULT_SERIAL = "0000000000000000675c62dc301090cf"
PAYLOAD_UI = (
    REPO_ROOT
    / "ground-station"
    / "c3m-payload-receiver-ui"
    / "c3m_payload_receiver_ui.py"
)


def apply_student_rf_baseline(args: argparse.Namespace) -> argparse.Namespace:
    """Apply the single supported student C3M RF configuration.

    RX-only remains fail-closed.  Enabling TX selects the already-qualified
    gain and ACK mode; students cannot tune RF parameters through this
    launcher.  Leads can use the lower-level bridge during a future,
    explicitly documented requalification.
    """

    args.network = DEFAULT_PROFILE.name
    args.tx_mode = BASELINE_TX_MODE
    args.tx_gain = 0 if args.no_tx else BASELINE_TX_GAIN_DB
    args.rx_lna_gain = BASELINE_RX_LNA_GAIN_DB
    args.rx_vga_gain = BASELINE_RX_VGA_GAIN_DB
    args.rf_path_label = BASELINE_RF_PATH_LABEL
    args.tx_leading_ms = BASELINE_TX_LEADING_MS
    # The elevated-gain acknowledgement here represents the checked-in,
    # qualified preset.  The operator must still separately confirm that the
    # exact antenna/path is assembled before TX can start.
    args.allow_elevated_tx_gain = not args.no_tx
    return args


@dataclass
class Child:
    name: str
    process: subprocess.Popen
    log_path: Path
    log_stream: object


def port_is_available(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as stream:
        try:
            # Bind every interface without SO_REUSEADDR.  This is deliberately
            # stricter than the child servers: a recently released or
            # interface-specific listener must not produce a false READY race.
            stream.bind(("", port))
        except OSError:
            return False
    return True


def selected_ports(args: argparse.Namespace) -> list[int]:
    ports: list[int] = []
    if not args.no_gds:
        ports.extend((args.gui_port, args.tts_port))
    if not args.no_payload_ui:
        ports.append(args.payload_web_port)
    return ports


def validate_ports(ports: list[int]) -> None:
    invalid = sorted({port for port in ports if not 1 <= port <= 65_535})
    if invalid:
        raise ValueError(f"ports must be between 1 and 65535: {invalid}")
    duplicates = sorted({port for port in ports if ports.count(port) > 1})
    if duplicates:
        raise ValueError(f"ports must be unique: {duplicates}")


def runtime_requirements(args: argparse.Namespace) -> list[Path]:
    required = [HERE / ".venv" / "bin" / "python"]
    if not args.no_gds:
        required.append(FPRIME_VENV / "bin" / "fprime-gds")
    if not args.no_payload_ui:
        required.extend((FPRIME_VENV / "bin" / "python", PAYLOAD_UI))
    return required


def http_ready(port: int, timeout_s: float = 0.5) -> bool:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=timeout_s) as response:
            return 200 <= response.status < 500
    except Exception:
        return False


def tail(path: Path, lines: int = 30) -> str:
    try:
        return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])
    except OSError:
        return ""


def stop_child(child: Child, timeout_s: float = 5.0) -> None:
    process = child.process
    try:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=timeout_s)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=timeout_s)
    finally:
        child.log_stream.close()


class Supervisor:
    def __init__(self, args: argparse.Namespace) -> None:
        apply_student_rf_baseline(args)
        args.rf_path_label = validate_tx_request(
            enable_tx=not args.no_tx,
            tx_gain=args.tx_gain,
            tx_safety_confirmed=args.tx_safety_confirmed,
            allow_elevated_tx_gain=args.allow_elevated_tx_gain,
            rf_path_label=args.rf_path_label,
        )
        if args.no_tx:
            args.tx_gain = 0
            args.tx_safety_confirmed = False
            args.allow_elevated_tx_gain = False
        self.args = args
        self.stopping = False
        self.children: list[Child] = []
        stamp = time.strftime("%Y%m%d_%H%M%S")
        self.run_dir = args.runtime_dir / "runs" / stamp
        suffix = 1
        while self.run_dir.exists():
            self.run_dir = args.runtime_dir / "runs" / f"{stamp}_{suffix}"
            suffix += 1
        self.gds_symlink = args.runtime_dir / "gds-port"
        self.payload_symlink = args.runtime_dir / "payload-port"
        self.bridge_metrics = self.run_dir / "bridge-status.json"
        self.manifest_path = self.run_dir / "run-manifest.json"
        self.started_at = time.time()

    def request_stop(self, _signum: int, _frame: object) -> None:
        self.stopping = True

    def _spawn(self, name: str, command: list[str], log_name: str) -> Child:
        log_path = self.run_dir / log_name
        log_stream = log_path.open("w", encoding="utf-8", buffering=1)
        child_env = os.environ.copy()
        # The payload UI invokes ``fprime-dp`` by name after a transfer.  It is
        # launched with the F Prime venv's Python, but subprocess PATH does not
        # change merely because that interpreter is selected.
        venv_bin = str(FPRIME_VENV / "bin")
        child_env["PATH"] = venv_bin + os.pathsep + child_env.get("PATH", "")
        try:
            process = subprocess.Popen(
                command,
                cwd=REPO_ROOT,
                env=child_env,
                stdin=subprocess.DEVNULL,
                stdout=log_stream,
                stderr=subprocess.STDOUT,
                start_new_session=True,
                text=True,
            )
        except BaseException:
            log_stream.close()
            raise
        child = Child(name, process, log_path, log_stream)
        self.children.append(child)
        print(f"STARTED name={name} pid={process.pid} log={log_path}", flush=True)
        return child

    def _wait_bridge(self, child: Child) -> None:
        deadline = time.monotonic() + self.args.start_timeout
        last_state = "missing"
        while time.monotonic() < deadline and not self.stopping:
            if child.process.poll() is not None:
                raise RuntimeError(
                    f"bridge exited {child.process.returncode}\n{tail(child.log_path)}"
                )
            try:
                status = json.loads(self.bridge_metrics.read_text(encoding="utf-8"))
                last_state = str(status.get("radio_state", "unknown"))
            except (OSError, json.JSONDecodeError):
                status = {}
            if (
                last_state == "receiving"
                and int(status.get("rx_blocks", 0)) > 0
                and self.gds_symlink.is_symlink()
                and self.payload_symlink.is_symlink()
            ):
                return
            time.sleep(0.1)
        raise RuntimeError(
            f"bridge did not become ready (state={last_state})\n{tail(child.log_path)}"
        )

    def _wait_http(self, child: Child, port: int) -> None:
        deadline = time.monotonic() + self.args.start_timeout
        while time.monotonic() < deadline and not self.stopping:
            if child.process.poll() is not None:
                raise RuntimeError(
                    f"{child.name} exited {child.process.returncode}\n{tail(child.log_path)}"
                )
            if http_ready(port):
                return
            time.sleep(0.2)
        raise RuntimeError(f"{child.name} did not open HTTP port {port}\n{tail(child.log_path)}")

    def _update_latest(self) -> None:
        latest = self.args.runtime_dir / "latest"
        if latest.exists() or latest.is_symlink():
            if not latest.is_symlink():
                raise FileExistsError(f"refusing to replace non-symlink {latest}")
            latest.unlink()
        latest.symlink_to(self.run_dir)

    def _write_manifest(self, state: str) -> None:
        try:
            branch = subprocess.run(
                ["git", "branch", "--show-current"],
                cwd=REPO_ROOT,
                check=False,
                capture_output=True,
                text=True,
                timeout=2,
            ).stdout.strip()
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=REPO_ROOT,
                check=False,
                capture_output=True,
                text=True,
                timeout=2,
            ).stdout.strip()
        except (OSError, subprocess.TimeoutExpired):
            branch, commit = "", ""
        atomic_write_json(
            self.manifest_path,
            {
                "schema": 1,
                "state": state,
                "started_at_s": self.started_at,
                "updated_at_s": time.time(),
                "repo": str(REPO_ROOT),
                "git_branch": branch,
                "git_commit": commit,
                "network": self.args.network,
                "hackrf_serial": self.args.serial,
                "tx_enabled": not self.args.no_tx,
                "tx_mode": self.args.tx_mode if not self.args.no_tx else "disabled",
                "tx_gain": self.args.tx_gain,
                "tx_leading_ms": self.args.tx_leading_ms,
                "rx_lna_gain": self.args.rx_lna_gain,
                "rx_vga_gain": self.args.rx_vga_gain,
                "gain_control": "fixed",
                "rf_path_label": self.args.rf_path_label,
                "tx_safety_confirmed": self.args.tx_safety_confirmed,
                "elevated_tx_gain_confirmed": self.args.allow_elevated_tx_gain,
                "rf_amp_enabled": False,
                "antenna_power_enabled": False,
                "gds_url": None if self.args.no_gds else f"http://127.0.0.1:{self.args.gui_port}",
                "payload_url": (
                    None
                    if self.args.no_payload_ui
                    else f"http://127.0.0.1:{self.args.payload_web_port}"
                ),
                "gds_pty": str(self.gds_symlink),
                "payload_pty": str(self.payload_symlink),
                "dictionary": (
                    None if self.args.dictionary is None else str(self.args.dictionary)
                ),
                "children": {
                    child.name: {
                        "pid": child.process.pid,
                        "returncode": child.process.poll(),
                        "log": str(child.log_path),
                    }
                    for child in self.children
                },
            },
        )

    def _cleanup(self, *, run_failed: bool = False) -> list[str]:
        errors: list[str] = []
        try:
            self._write_manifest("stopping")
        except Exception as exc:
            errors.append(f"failed to write stopping manifest: {exc!r}")

        # Payload receiver first, then GDS, then the RF adapter. Attempt every
        # child even when an earlier process cannot be stopped cleanly.
        for child in reversed(self.children):
            try:
                stop_child(child)
            except Exception as exc:
                errors.append(f"failed to stop {child.name}: {exc!r}")

        try:
            final_state = (
                "cleanup_failed"
                if errors
                else ("failed" if run_failed else "stopped")
            )
            self._write_manifest(final_state)
        except Exception as exc:
            errors.append(f"failed to write final manifest: {exc!r}")
        return errors

    def run(self) -> int:
        self.run_dir.mkdir(parents=True)
        self._update_latest()
        self._write_manifest("starting")
        bridge_python = HERE / ".venv" / "bin" / "python"
        bridge_command = [
            str(bridge_python),
            str(HERE / "live_rx_bridge.py"),
            "--gds-symlink",
            str(self.gds_symlink),
            "--payload-symlink",
            str(self.payload_symlink),
            "--metrics-file",
            str(self.bridge_metrics),
            "--uplink-capture-dir",
            str(self.run_dir / "uplink"),
            "--serial",
            self.args.serial,
            "--network",
            self.args.network,
            "--tx-gain",
            str(self.args.tx_gain),
            "--rx-lna-gain",
            str(self.args.rx_lna_gain),
            "--rx-vga-gain",
            str(self.args.rx_vga_gain),
            "--rf-path-label",
            self.args.rf_path_label,
            "--tx-leading-ms",
            str(self.args.tx_leading_ms),
        ]
        if not self.args.no_tx:
            bridge_command.extend(("--enable-tx", "--tx-safety-confirmed"))
            if self.args.allow_elevated_tx_gain:
                bridge_command.append("--allow-elevated-tx-gain")
        try:
            bridge = self._spawn("bridge", bridge_command, "bridge.log")
            self._wait_bridge(bridge)

            if not self.args.no_gds:
                gds_command = [
                    str(FPRIME_VENV / "bin" / "fprime-gds"),
                    "-n",
                    "--dictionary",
                    str(self.args.dictionary),
                    "--communication-selection",
                    "uart",
                    "--uart-device",
                    str(self.gds_symlink),
                    "--uart-skip-port-check",
                    "--uart-baud",
                    "115200",
                    "--gui-port",
                    str(self.args.gui_port),
                    "--tts-port",
                    str(self.args.tts_port),
                    "--no-zmq",
                    "--logs",
                    str(self.run_dir / "gds"),
                    "--log-directly",
                    "--log-to-stdout",
                    "--framing-selection",
                    "space-packet-space-data-link",
                ]
                gds = self._spawn("gds", gds_command, "gds-console.log")
                self._wait_http(gds, self.args.gui_port)

            if not self.args.no_payload_ui:
                payload_command = [
                    str(FPRIME_VENV / "bin" / "python"),
                    str(PAYLOAD_UI),
                    "--port",
                    str(self.payload_symlink),
                    "--web-port",
                    str(self.args.payload_web_port),
                    "--data-dir",
                    str(self.args.data_dir),
                    "--dictionary",
                    str(self.args.dictionary),
                    "--no-open",
                ]
                payload = self._spawn(
                    "payload-ui", payload_command, "payload-ui.log"
                )
                self._wait_http(payload, self.args.payload_web_port)

            self._write_manifest("ready")
            print(f"GROUND_STATION_READY run={self.run_dir}", flush=True)
            if self.args.no_tx:
                print(
                    "TX_DISABLED receive-only-default=true gain=0 "
                    f"rf_path={self.args.rf_path_label}",
                    flush=True,
                )
            else:
                print(
                    "TX_ENABLED safety_confirmed=true "
                    f"gain={self.args.tx_gain} rf_path={self.args.rf_path_label}",
                    flush=True,
                )
            if not self.args.no_gds:
                print(f"GDS_URL http://127.0.0.1:{self.args.gui_port}", flush=True)
            if not self.args.no_payload_ui:
                print(
                    f"PAYLOAD_URL http://127.0.0.1:{self.args.payload_web_port}",
                    flush=True,
                )
            if not self.args.no_open:
                if not self.args.no_gds:
                    webbrowser.open(f"http://127.0.0.1:{self.args.gui_port}")
                if not self.args.no_payload_ui:
                    webbrowser.open(
                        f"http://127.0.0.1:{self.args.payload_web_port}"
                    )

            while not self.stopping:
                for child in self.children:
                    returncode = child.process.poll()
                    if returncode is not None:
                        raise RuntimeError(
                            f"{child.name} exited unexpectedly with {returncode}\n"
                            f"{tail(child.log_path)}"
                        )
                time.sleep(0.25)
            return 0
        finally:
            active_error = sys.exc_info()[1]
            cleanup_errors = self._cleanup(run_failed=active_error is not None)
            if cleanup_errors:
                message = "; ".join(cleanup_errors)
                if active_error is None:
                    raise RuntimeError(message)
                print(
                    f"GROUND_STATION_CLEANUP_FAILED error={message}",
                    file=sys.stderr,
                    flush=True,
                )


def find_dictionary() -> Path | None:
    matches = sorted(
        FPRIME_ROOT.glob(
            "build-artifacts/*/ArtemisRpiTeensyDeployment/dict/"
            "ArtemisRpiTeensyDeploymentTopologyDictionary.json"
        )
    )
    armv6 = [path for path in matches if "pi-zero-w-armv6hf" in path.parts]
    return (armv6 or matches or [None])[0]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default=DEFAULT_SERIAL)
    tx_group = parser.add_mutually_exclusive_group()
    tx_group.add_argument(
        "--enable-tx",
        dest="no_tx",
        action="store_false",
        help="enable TX after satisfying the explicit RF-path interlocks",
    )
    tx_group.add_argument(
        "--no-tx",
        dest="no_tx",
        action="store_true",
        help="force receive-only operation (the default)",
    )
    parser.set_defaults(no_tx=True)
    parser.add_argument(
        "--tx-safety-confirmed",
        action="store_true",
        help=(
            "confirm the qualified 9-10 inch monopole/no-attenuator path is "
            "attached at the tested lab geometry"
        ),
    )
    parser.add_argument("--gui-port", type=int, default=5057)
    parser.add_argument("--tts-port", type=int, default=50057)
    parser.add_argument("--payload-web-port", type=int, default=8064)
    parser.add_argument("--no-gds", action="store_true")
    parser.add_argument("--no-payload-ui", action="store_true")
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument("--start-timeout", type=float, default=12.0)
    parser.add_argument("--runtime-dir", type=Path, default=Path("/tmp/c3m-sdr"))
    parser.add_argument("--data-dir", type=Path, default=REPO_ROOT / "data")
    parser.add_argument("--dictionary", type=Path, default=find_dictionary())
    return parser


def main() -> int:
    parser = build_parser()
    args = apply_student_rf_baseline(parser.parse_args())
    try:
        args.rf_path_label = validate_tx_request(
            enable_tx=not args.no_tx,
            tx_gain=args.tx_gain,
            tx_safety_confirmed=args.tx_safety_confirmed,
            allow_elevated_tx_gain=args.allow_elevated_tx_gain,
            rf_path_label=args.rf_path_label,
        )
    except TxSafetyError as exc:
        parser.error(str(exc))
    if args.no_tx:
        # Fail closed even when a stale command line carries an old gain.
        args.tx_gain = 0
        args.tx_safety_confirmed = False
        args.allow_elevated_tx_gain = False
    missing = [str(path) for path in runtime_requirements(args) if not path.is_file()]
    if missing:
        parser.error(f"missing required runtime: {', '.join(missing)}")
    dictionary_required = not args.no_gds or not args.no_payload_ui
    if dictionary_required and (args.dictionary is None or not args.dictionary.is_file()):
        parser.error(f"dictionary not found: {args.dictionary}")
    ports = selected_ports(args)
    try:
        validate_ports(ports)
    except ValueError as exc:
        parser.error(str(exc))
    occupied = [port for port in ports if not port_is_available(port)]
    if occupied:
        parser.error(f"ports already in use; existing processes were preserved: {occupied}")
    args.runtime_dir = args.runtime_dir.expanduser().resolve()
    args.data_dir = args.data_dir.expanduser().resolve()
    if args.dictionary is not None:
        args.dictionary = args.dictionary.expanduser().resolve()

    supervisor = Supervisor(args)
    signal.signal(signal.SIGINT, supervisor.request_stop)
    signal.signal(signal.SIGTERM, supervisor.request_stop)
    try:
        return supervisor.run()
    except Exception as exc:
        print(f"GROUND_STATION_FAILED error={exc!r}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
