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
OWNED_PROCESS_MARKERS = (
    str(HERE / "run_hackrf_ground_station.py"),
    str(HERE / "live_rx_bridge.py"),
    str(FPRIME_VENV / "bin" / "fprime-gds"),
    str(PAYLOAD_UI),
)


def apply_student_rf_baseline(args: argparse.Namespace) -> argparse.Namespace:
    """Apply the operator-safe C3M identity and calibration bootstrap.

    RX-only remains fail-closed. Enabling TX starts from the known indoor value
    but automatic calibration replaces RX/TX gains with live link evidence
    before GDS starts. Operators cannot tune RF parameters through this
    launcher.
    """

    args.network = DEFAULT_PROFILE.name
    args.tx_mode = BASELINE_TX_MODE
    args.tx_gain = 0 if args.no_tx else BASELINE_TX_GAIN_DB
    args.rx_lna_gain = BASELINE_RX_LNA_GAIN_DB
    args.rx_vga_gain = BASELINE_RX_VGA_GAIN_DB
    args.rx_rf_amp_enabled = False
    args.tx_rf_amp_enabled = False
    args.rf_path_label = BASELINE_RF_PATH_LABEL
    args.tx_leading_ms = BASELINE_TX_LEADING_MS
    args.auto_calibrate = True
    # This permits the automatic search across HackRF's supported TX VGA. The
    # separate path confirmation still requires the named antenna to be attached.
    args.allow_elevated_tx_gain = not args.no_tx
    return args


@dataclass
class Child:
    name: str
    process: subprocess.Popen
    log_path: Path
    log_stream: object


def port_is_available(port: int) -> bool:
    # Reject a service actively accepting connections on the exact loopback
    # endpoint used by the operator UIs.  A reusable bind alone is insufficient
    # because two SO_REUSEADDR sockets can otherwise overlap on macOS.
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.1):
            return False
    except OSError:
        pass

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as stream:
        try:
            # The prior GDS/viewer may leave client connections in TIME_WAIT
            # after its listener exits.  SO_REUSEADDR permits that released
            # state while bind still rejects an active listener on the port.
            stream.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
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


def _process_table() -> dict[int, tuple[int, str]]:
    """Return pid -> (parent pid, command) for lifecycle ownership checks."""

    try:
        result = subprocess.run(
            ["ps", "-axo", "pid=,ppid=,command="],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.TimeoutExpired):
        return {}
    table: dict[int, tuple[int, str]] = {}
    for line in result.stdout.splitlines():
        fields = line.strip().split(maxsplit=2)
        if len(fields) < 2:
            continue
        try:
            pid, parent = int(fields[0]), int(fields[1])
        except ValueError:
            continue
        table[pid] = (parent, fields[2] if len(fields) == 3 else "")
    return table


def _lsof_pids(arguments: list[str]) -> set[int]:
    try:
        result = subprocess.run(
            ["lsof", "-t", *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.TimeoutExpired):
        return set()
    return {
        int(line)
        for line in result.stdout.splitlines()
        if line.strip().isdigit()
    }


def _manifest_pids(runtime_dir: Path) -> set[int]:
    pids: set[int] = set()
    for path in runtime_dir.glob("runs/*/run-manifest.json"):
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        supervisor_pid = manifest.get("supervisor_pid")
        if isinstance(supervisor_pid, int):
            pids.add(supervisor_pid)
        children = manifest.get("children")
        if isinstance(children, dict):
            for child in children.values():
                if isinstance(child, dict) and isinstance(child.get("pid"), int):
                    pids.add(child["pid"])
    return pids


def owned_runtime_pids(runtime_dir: Path, ports: list[int]) -> set[int]:
    """Find only processes attributable to this repository's C3M SDR stack."""

    table = _process_table()
    manifest_pids = _manifest_pids(runtime_dir)
    resource_pids: set[int] = set()
    for port in ports:
        # Only the process listening on an operator port owns that resource.
        # Browser/client connections must never become cleanup targets.
        resource_pids.update(_lsof_pids([f"-iTCP:{port}", "-sTCP:LISTEN"]))
    for symlink in (runtime_dir / "gds-port", runtime_dir / "payload-port"):
        if symlink.exists() or symlink.is_symlink():
            resource_pids.update(_lsof_pids([str(symlink)]))

    roots = {
        pid
        for pid in manifest_pids | resource_pids
        if pid != os.getpid()
        and (
            any(marker in table.get(pid, (0, ""))[1] for marker in OWNED_PROCESS_MARKERS)
            or pid in resource_pids
        )
    }
    owned = set(roots)
    changed = True
    while changed:
        changed = False
        for pid, (parent, _command) in table.items():
            if pid != os.getpid() and parent in owned and pid not in owned:
                owned.add(pid)
                changed = True
    return owned


def reap_owned_runtime(runtime_dir: Path, ports: list[int]) -> list[int]:
    """Stop prior repo-owned C3M SDR processes before starting a replacement."""

    pids = owned_runtime_pids(runtime_dir, ports)
    if not pids:
        return []
    print(
        "STALE_RUNTIME_CLEANUP stopping=" + ",".join(map(str, sorted(pids))),
        flush=True,
    )
    for pid in sorted(pids, reverse=True):
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 5.0
    remaining = set(pids)
    while remaining and time.monotonic() < deadline:
        remaining = {
            pid
            for pid in remaining
            if pid in _process_table()
        }
        if remaining:
            time.sleep(0.1)
    for pid in sorted(remaining, reverse=True):
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    if remaining:
        print(
            "STALE_RUNTIME_CLEANUP forced="
            + ",".join(map(str, sorted(remaining))),
            flush=True,
        )
    return sorted(pids)


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
        self._last_manifest_sync = 0.0
        self._adaptive_link_status: dict[str, object] = {}
        self._bridge_status: dict[str, object] = {}
        self._last_diagnostic_at = 0.0
        self._last_diagnostic_signature: tuple[object, ...] | None = None

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
                calibration = status.get("calibration", {})
                selected = calibration.get("selected") if isinstance(calibration, dict) else None
                if self.args.auto_calibrate:
                    if not isinstance(selected, dict):
                        raise RuntimeError("bridge became ready without automatic gain results")
                    self.args.rx_lna_gain = int(selected["rx_lna_gain_db"])
                    self.args.rx_vga_gain = int(selected["rx_vga_gain_db"])
                    self.args.tx_gain = int(selected["tx_gain_db"])
                    self.args.rx_rf_amp_enabled = bool(
                        selected["rx_rf_amp_enabled"]
                    )
                    self.args.tx_rf_amp_enabled = bool(
                        selected["tx_rf_amp_enabled"]
                    )
                return
            time.sleep(0.1)
        raise RuntimeError(
            f"bridge did not become ready (state={last_state})\n{tail(child.log_path)}"
        )

    def _wait_bridge_runtime(self, child: Child) -> None:
        """Wait only for the PTYs needed to start the operator surfaces."""

        deadline = time.monotonic() + self.args.start_timeout
        while time.monotonic() < deadline and not self.stopping:
            if child.process.poll() is not None:
                raise RuntimeError(
                    f"bridge exited {child.process.returncode}\n{tail(child.log_path)}"
                )
            if (
                self.bridge_metrics.is_file()
                and self.gds_symlink.is_symlink()
                and self.payload_symlink.is_symlink()
            ):
                return
            time.sleep(0.05)
        raise RuntimeError(
            "bridge did not create its runtime PTYs\n" + tail(child.log_path)
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

    def _wait_http_surfaces(
        self, surfaces: list[tuple[Child, int, str]]
    ) -> None:
        """Poll concurrently started HTTP surfaces until all are available."""

        deadline = time.monotonic() + self.args.start_timeout
        pending = {child.name: (child, port, label) for child, port, label in surfaces}
        while pending and time.monotonic() < deadline and not self.stopping:
            for name, (child, port, label) in list(pending.items()):
                if child.process.poll() is not None:
                    raise RuntimeError(
                        f"{name} exited {child.process.returncode}\n"
                        f"{tail(child.log_path)}"
                    )
                if not http_ready(port):
                    continue
                url = f"http://127.0.0.1:{port}"
                print(f"{label}_STARTED {url}", flush=True)
                if not self.args.no_open:
                    webbrowser.open(url)
                del pending[name]
            if pending:
                time.sleep(0.2)
        if pending:
            names = ", ".join(sorted(pending))
            details = "\n".join(
                f"{name}: {tail(child.log_path)}"
                for name, (child, _port, _label) in pending.items()
            )
            raise RuntimeError(f"HTTP surfaces did not start: {names}\n{details}")

    def _sync_bridge_rf_state(self) -> bool:
        try:
            status = json.loads(self.bridge_metrics.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return False
        self._bridge_status = status
        adaptive = status.get("adaptive_link")
        if isinstance(adaptive, dict):
            self._adaptive_link_status = adaptive
        before = (
            self.args.rx_lna_gain,
            self.args.rx_vga_gain,
            self.args.rx_rf_amp_enabled,
            self.args.tx_gain,
            self.args.tx_rf_amp_enabled,
        )
        self.args.rx_lna_gain = int(status.get("rx_lna_gain", before[0]))
        self.args.rx_vga_gain = int(status.get("rx_vga_gain", before[1]))
        self.args.rx_rf_amp_enabled = bool(
            status.get("rx_rf_amp_enabled", before[2])
        )
        self.args.tx_gain = int(status.get("tx_gain", before[3]))
        self.args.tx_rf_amp_enabled = bool(
            status.get("tx_rf_amp_enabled", before[4])
        )
        after = (
            self.args.rx_lna_gain,
            self.args.rx_vga_gain,
            self.args.rx_rf_amp_enabled,
            self.args.tx_gain,
            self.args.tx_rf_amp_enabled,
        )
        return after != before

    def _print_sdr_status(self, *, force: bool = False) -> None:
        """Print a compact operator view of the live RF state."""

        status = self._bridge_status
        adaptive = self._adaptive_link_status
        link_state = str(adaptive.get("state", "starting"))
        reason = str(adaptive.get("last_adjustment_reason", "none")).replace(
            "_", " "
        )
        frame_age = adaptive.get("last_valid_frame_age_s")
        frame_age_text = (
            f"{float(frame_age):.1f}s ago"
            if isinstance(frame_age, (int, float))
            else "not seen"
        )
        signature = (
            link_state,
            self.args.rx_lna_gain,
            self.args.rx_vga_gain,
            self.args.rx_rf_amp_enabled,
            self.args.tx_gain,
            self.args.tx_rf_amp_enabled,
            reason,
        )
        now = time.monotonic()
        if (
            not force
            and signature == self._last_diagnostic_signature
            and now - self._last_diagnostic_at < 10.0
        ):
            return
        self._last_diagnostic_signature = signature
        self._last_diagnostic_at = now
        print(
            "SDR STATUS | "
            f"link {link_state} | "
            f"RX LNA {self.args.rx_lna_gain} dB, "
            f"VGA {self.args.rx_vga_gain} dB, "
            f"amp {'on' if self.args.rx_rf_amp_enabled else 'off'} | "
            f"TX gain {self.args.tx_gain} dB, "
            f"amp {'on' if self.args.tx_rf_amp_enabled else 'off'} | "
            f"frames {int(status.get('rf22_frames', 0))}, "
            f"last frame {frame_age_text} | reason {reason}",
            flush=True,
        )

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
                "supervisor_pid": os.getpid(),
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
                "gain_control": "automatic" if self.args.auto_calibrate else "fixed",
                "rf_path_label": self.args.rf_path_label,
                "tx_safety_confirmed": self.args.tx_safety_confirmed,
                "elevated_tx_gain_confirmed": self.args.allow_elevated_tx_gain,
                "rf_amp_enabled": (
                    self.args.rx_rf_amp_enabled or self.args.tx_rf_amp_enabled
                ),
                "rx_rf_amp_enabled": self.args.rx_rf_amp_enabled,
                "tx_rf_amp_enabled": self.args.tx_rf_amp_enabled,
                "antenna_power_enabled": False,
                "adaptive_link": self._adaptive_link_status,
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
            "--auto-calibrate",
        ]
        if not self.args.no_tx:
            bridge_command.extend(("--enable-tx", "--tx-safety-confirmed"))
            if self.args.allow_elevated_tx_gain:
                bridge_command.append("--allow-elevated-tx-gain")
        try:
            bridge = self._spawn("bridge", bridge_command, "bridge.log")
            # The bridge publishes both PTYs before opening/calibrating the
            # HackRF.  Start both operator surfaces as soon as those PTYs
            # exist so their cold starts overlap the RF search.
            self._wait_bridge_runtime(bridge)

            gds: Child | None = None
            payload: Child | None = None

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

            # Both processes are now booting concurrently.  Publish/open each
            # surface as soon as its server answers; calibration continues in
            # the independent bridge process throughout these waits.
            surfaces: list[tuple[Child, int, str]] = []
            if gds is not None:
                surfaces.append((gds, self.args.gui_port, "GDS"))
            if payload is not None:
                surfaces.append(
                    (payload, self.args.payload_web_port, "PAYLOAD")
                )
            self._wait_http_surfaces(surfaces)

            self._wait_bridge(bridge)
            self._sync_bridge_rf_state()

            self._write_manifest("ready")
            print(f"GROUND_STATION_READY run={self.run_dir}", flush=True)
            print(
                "RF_AUTO_SELECTED "
                f"rx_lna={self.args.rx_lna_gain} rx_vga={self.args.rx_vga_gain} "
                f"rx_amp={'on' if self.args.rx_rf_amp_enabled else 'off'} "
                f"tx={self.args.tx_gain} "
                f"tx_amp={'on' if self.args.tx_rf_amp_enabled else 'off'}",
                flush=True,
            )
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
            self._print_sdr_status(force=True)
            while not self.stopping:
                for child in self.children:
                    returncode = child.process.poll()
                    if returncode is not None:
                        raise RuntimeError(
                            f"{child.name} exited unexpectedly with {returncode}\n"
                            f"{tail(child.log_path)}"
                        )
                now = time.monotonic()
                if now - self._last_manifest_sync >= 2.0:
                    self._last_manifest_sync = now
                    self._sync_bridge_rf_state()
                    self._write_manifest("ready")
                    self._print_sdr_status()
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
            "confirm the qualified POBADY 433 MHz magnetic-base antenna and "
            "3 m RG174 cable are attached at the tested lab geometry"
        ),
    )
    parser.add_argument("--gui-port", type=int, default=5057)
    parser.add_argument("--tts-port", type=int, default=50057)
    parser.add_argument("--payload-web-port", type=int, default=8064)
    parser.add_argument("--no-gds", action="store_true")
    parser.add_argument("--no-payload-ui", action="store_true")
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument("--start-timeout", type=float, default=60.0)
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
    args.runtime_dir = args.runtime_dir.expanduser().resolve()
    args.data_dir = args.data_dir.expanduser().resolve()
    if args.dictionary is not None:
        args.dictionary = args.dictionary.expanduser().resolve()
    reap_owned_runtime(args.runtime_dir, ports)
    occupied = [port for port in ports if not port_is_available(port)]
    if occupied:
        parser.error(
            "ports still owned by unrelated processes after C3M cleanup: "
            f"{occupied}"
        )

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
