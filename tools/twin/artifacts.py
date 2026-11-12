"""Validate the local executable/dictionary pair before starting a role."""
import hashlib
import json
from pathlib import Path


def verify(role: str, base: Path | None = None) -> dict:
    base = base or Path("/artifacts") / role
    manifest = json.loads((base / "manifest.json").read_text())
    if manifest.get("role") != role:
        raise RuntimeError(f"Artifact role mismatch for {role}")
    binary = base / "bin" / ("flight-controller" if role == "fc" else "payload-computer")
    dictionary = base / "dict/topology.json"
    for path, field in ((binary, "binary_sha256"), (dictionary, "dictionary_sha256")):
        if hashlib.sha256(path.read_bytes()).hexdigest() != manifest.get(field):
            raise RuntimeError(f"Stale or modified {role} artifact: {path.name}")
    if manifest.get("framework_sha") in (None, "unknown"):
        raise RuntimeError(f"Missing framework identity for {role}")
    return manifest
