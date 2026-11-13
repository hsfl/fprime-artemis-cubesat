#!/usr/bin/env python3
"""Report stalled stock Yamcs downloads without canceling or retrying them.

The stock F Prime file service does not support cancellation. This observer
publishes TransferStalled warnings only; it never issues spacecraft commands.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import ipaddress
import json
import logging
import math
import os
from pathlib import Path
import signal
import socket
import threading
import time
from urllib import error, parse, request


LOG = logging.getLogger("n2-ground-transfer-watchdog")


def positive_seconds(value: str) -> float:
    try:
        seconds = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a positive number of seconds") from exc
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number of seconds")
    return seconds


def loopback_url(value: str) -> str:
    """Credentials are sent only to a local HTTP(S) listener, never redirects."""
    try:
        url = parse.urlsplit(value)
        if (url.scheme not in ("http", "https") or not url.hostname
                or url.username is not None or url.password is not None
                or url.path not in ("", "/") or url.query or url.fragment):
            raise ValueError
        port = url.port or (443 if url.scheme == "https" else 80)
        addresses = socket.getaddrinfo(url.hostname, port, type=socket.SOCK_STREAM)
        if not addresses or not all(ipaddress.ip_address(item[4][0]).is_loopback for item in addresses):
            raise ValueError
    except (ValueError, OSError) as exc:
        raise argparse.ArgumentTypeError("--url must name a loopback HTTP(S) server") from exc
    return value.rstrip("/")


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never follow an authentication or API redirect with a bearer token.
        return None


class LocalYamcs:
    def __init__(self, url: str, credentials: Path):
        self.url = loopback_url(url)
        self.credentials = credentials
        self.token: str | None = None
        self.token_expires = 0.0
        # Avoid environment proxy routing for requests containing local secrets.
        self.opener = request.build_opener(request.ProxyHandler({}), NoRedirect())

    def login(self) -> None:
        password = None
        for line in self.credentials.read_text().splitlines():
            label, separator, value = line.partition(":")
            if separator and label.strip().lower() == "admin":
                password = value.strip()
                break
        if not password:
            raise RuntimeError("station access file has no admin credential")
        body = parse.urlencode({"grant_type": "password", "username": "admin", "password": password})
        req = request.Request(self.url + "/auth/token", body.encode(),
                              {"Content-Type": "application/x-www-form-urlencoded"}, method="POST")
        with self.opener.open(req, timeout=10) as response:
            result = json.load(response)
        token = result.get("access_token")
        if not isinstance(token, str) or not token:
            raise RuntimeError("local authentication returned no access token")
        self.token = token
        lifetime = float(result.get("expires_in", 600))
        self.token_expires = time.monotonic() + max(1.0, lifetime - min(30.0, lifetime / 2))

    def _request(self, path: str, body: dict | None = None, timeout: float = 10.0) -> bytes:
        if not path.startswith("/api/"):
            raise ValueError("local Yamcs requests must use an /api/ path")
        for attempt in range(2):
            if self.token is None or time.monotonic() >= self.token_expires:
                self.login()
            headers = {"Authorization": "Bearer " + self.token}
            data = None
            if body is not None:
                data = json.dumps(body).encode()
                headers["Content-Type"] = "application/json"
            req = request.Request(self.url + path, data=data, headers=headers,
                                  method="GET" if body is None else "POST")
            try:
                with self.opener.open(req, timeout=timeout) as response:
                    return response.read()
            except error.HTTPError as exc:
                # A 401 proves rejection before execution. Never retry a
                # potentially submitted POST after a transport error.
                if exc.code == 401 and attempt == 0:
                    self.token = None
                    continue
                raise
        raise RuntimeError("local authentication failed")

    def api(self, path: str, body: dict | None = None, timeout: float = 10.0):
        content = self._request(path, body, timeout)
        return json.loads(content) if content else {}

    def api_bytes(self, path: str, timeout: float = 10.0) -> bytes:
        """Read bucket bytes using the same credentials and redirect protections."""
        return self._request(path, timeout=timeout)


@dataclass
class Progress:
    started: float
    last_progress: float
    size: int
    alerted: set[str] = field(default_factory=set)


def printable(value: object, maximum: int = 180) -> str:
    return "".join(char if char.isprintable() else " " for char in str(value))[:maximum]


def parent_alive(pid: int | None) -> bool:
    if pid is None:
        return True
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def poll(api: LocalYamcs, args, tracked: dict[str, Progress]) -> None:
    path = ("/api/filetransfer/" + parse.quote(args.instance, safe="") + "/"
            + parse.quote(args.service, safe="") + "/transfers?"
            + parse.urlencode({"state": "RUNNING", "direction": "DOWNLOAD", "limit": 1000}))
    result = api.api(path)
    active = set()
    now = time.monotonic()
    for transfer in result.get("transfers", []):
        if transfer.get("state") != "RUNNING" or transfer.get("direction") != "DOWNLOAD":
            continue
        identifier = str(transfer["id"])
        active.add(identifier)
        size = int(transfer.get("sizeTransferred", 0))
        total = int(transfer.get("totalSize", 0))
        previous = tracked.get(identifier)
        if previous is None:
            previous = Progress(now, now, size)
            tracked[identifier] = previous
        elif size != previous.size:
            previous.size = size
            previous.last_progress = now
            previous.alerted.discard("idle")
        idle = now - previous.last_progress
        age = now - previous.started
        reasons = set()
        if idle >= args.idle_timeout:
            reasons.add("idle")
        if age >= args.overall_timeout:
            reasons.add("overall")
        unreported = reasons - previous.alerted
        if not unreported:
            continue
        message = (
            f"Download stalled: id={printable(identifier, 40)} "
            f"object={printable(transfer.get('objectName') or transfer.get('remotePath') or '?')} "
            f"received={size}/{total} bytes idle={idle:.1f}s age={age:.1f}s "
            f"reason={','.join(sorted(unreported))}; stock receiver may remain RUNNING. "
            "No automatic cancel or retry."
        )
        api.api("/api/archive/" + parse.quote(args.instance, safe="") + "/events", {
            "source": "n2-ground-transfer-watchdog", "type": "TransferStalled",
            "severity": "WARNING", "message": message,
        })
        LOG.warning(message)
        previous.alerted.update(unreported)
    for identifier in set(tracked) - active:
        del tracked[identifier]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--station", required=True, type=Path)
    parser.add_argument("--url", type=loopback_url, default="http://127.0.0.1:8090")
    parser.add_argument("--instance", default="fprime-project")
    parser.add_argument("--service", default="FprimeFilePacketService")
    parser.add_argument("--parent-pid", type=int)
    parser.add_argument("--idle-timeout", type=positive_seconds, default=30.0)
    parser.add_argument("--overall-timeout", type=positive_seconds, default=300.0)
    parser.add_argument("--poll-interval", type=positive_seconds, default=2.0)
    args = parser.parse_args(argv)
    if args.parent_pid is not None and args.parent_pid <= 1:
        parser.error("--parent-pid must be greater than one")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    stopped = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda _sig, _frame: stopped.set())
    api = LocalYamcs(args.url, args.station.expanduser().resolve() / "Yamcs-access.txt")
    tracked: dict[str, Progress] = {}
    errors = 0
    last_error_log = float("-inf")
    LOG.info("Observing stock downloads: idle=%.1fs overall=%.1fs; alerts only",
             args.idle_timeout, args.overall_timeout)
    while not stopped.is_set() and parent_alive(args.parent_pid):
        delay = args.poll_interval
        try:
            poll(api, args, tracked)
            errors = 0
        except Exception as exc:
            errors += 1
            now = time.monotonic()
            if now - last_error_log >= 30.0:
                # Do not log exception text, response bodies, tokens, or credentials.
                label = f"HTTP {exc.code}" if isinstance(exc, error.HTTPError) else type(exc).__name__
                LOG.warning("Local transfer API unavailable (%s); continuing with backoff", label)
                last_error_log = now
            delay = min(30.0, args.poll_interval * (2 ** min(errors - 1, 5)))
        stopped.wait(delay)
    LOG.info("Transfer observer stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
