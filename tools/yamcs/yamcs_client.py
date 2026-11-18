"""Shared authenticated loopback client for the stock Yamcs HTTP API."""
from __future__ import annotations
import argparse
import ipaddress
import json
from pathlib import Path
import socket
import time
from urllib import error, parse, request

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

    def login(self, timeout: float = 10.0) -> None:
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
        with self.opener.open(req, timeout=timeout) as response:
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
