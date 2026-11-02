#!/usr/bin/env python3
"""Launch the persistent Neutron 2 ground Yamcs instance."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parents[2]
TM_INJECT_PORT_FALLBACK = 50002
DICTIONARY_MARKER = "N2CdrTcp.ENABLED"


LOGGING_PROPERTIES = """\
handlers = java.util.logging.ConsoleHandler, java.util.logging.FileHandler
java.util.logging.ConsoleHandler.level = INFO
java.util.logging.ConsoleHandler.formatter = org.yamcs.logging.JournalFormatter
java.util.logging.ConsoleHandler.filter = org.yamcs.logging.GlobalFilter
java.util.logging.FileHandler.level = ALL
java.util.logging.FileHandler.pattern = {log_path}
java.util.logging.FileHandler.limit = 20000000
java.util.logging.FileHandler.count = 5
java.util.logging.FileHandler.formatter = org.yamcs.logging.CompactFormatter
org.yamcs.level = INFO
"""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=REPO,
                        help="neutron_2 checkout (default: this script's checkout)")
    parser.add_argument("--station", type=Path, default=Path.home() / "yamcs-station",
                        help="persistent ground-station directory")
    parser.add_argument("--address", default="n2-obc.local",
                        help="OBC TCP peer used by the separate ground bridge")
    return parser


def validate_dictionary(dictionary: Path) -> None:
    try:
        payload = json.loads(dictionary.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read F' dictionary {dictionary}: {exc}") from exc
    constants = {item.get("qualifiedName"): item.get("value")
                 for item in payload.get("constants", [])}
    if constants.get(DICTIONARY_MARKER) is not True:
        raise ValueError(f"dictionary must contain {DICTIONARY_MARKER}=true")


def write_default_logging_config(config_dir: Path, logs_dir: Path) -> None:
    """Add file and journal logging defaults once, preserving operator config."""
    path = config_dir / "etc" / "logging.properties"
    if path.exists():
        return
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    with os.fdopen(fd, "w") as output:
        output.write(LOGGING_PROPERTIES.format(log_path=logs_dir / "yamcs-server.log"))


def generate_xtce(config_dir: Path, dictionary: Path, env: dict[str, str], venv: Path) -> str:
    """Regenerate the config's F' XTCE MDB without replacing operator settings."""
    sys.path.insert(0, str(venv / "lib" / f"python{sys.version_info.major}.{sys.version_info.minor}" / "site-packages"))
    from fprime_yamcs.__main__ import (
        anchor_relative_mdb_paths,
        xtce_mdb_location,
        yamcs_instances,
    )
    import yaml

    instances = yamcs_instances(config_dir)
    for instance_file in sorted((config_dir / "etc").glob("yamcs.*.yaml")):
        instance_config = yaml.safe_load(instance_file.read_text()) or {}
        if anchor_relative_mdb_paths(instance_config, config_dir):
            instance_file.write_text(yaml.safe_dump(instance_config, sort_keys=False))

    xtce_path, instance = xtce_mdb_location(config_dir, instances)
    xtce_path = xtce_path.resolve()
    if not xtce_path.is_relative_to(config_dir.resolve()):
        raise ValueError(f"XTCE MDB must stay inside station config: {xtce_path}")
    if not xtce_path.name.endswith("fprime.xtce.xml"):
        raise ValueError(f"unexpected XTCE MDB target: {xtce_path}")
    xtce_path.parent.mkdir(parents=True, exist_ok=True)

    converter = venv / "bin" / "fprime-to-xtce"
    if not converter.is_file():
        raise ValueError(f"missing {converter}; run tools/yamcs/bootstrap.sh first")
    fd, temp_name = tempfile.mkstemp(prefix=".fprime-xtce-", suffix=".xml", dir=xtce_path.parent)
    os.close(fd)
    temporary = Path(temp_name)
    try:
        subprocess.run([str(converter), "-o", str(temporary), str(dictionary)],
                       check=True, env=env)
        if temporary.stat().st_size == 0:
            raise ValueError("fprime-to-xtce produced an empty MDB")
        temporary.replace(xtce_path)
    finally:
        temporary.unlink(missing_ok=True)
    return instance


def split_tm_inject_port(config_dir: Path, instance: str) -> int:
    import yaml

    instance_file = config_dir / "etc" / f"yamcs.{instance}.yaml"
    if not instance_file.is_file():
        raise ValueError(f"missing Yamcs instance config {instance_file}")
    config = yaml.safe_load(instance_file.read_text()) or {}
    links = [link for link in config.get("dataLinks", [])
             if link.get("name") == "UDP_TM_SPLIT_IN"]
    if len(links) != 1:
        raise ValueError(f"expected one UDP_TM_SPLIT_IN link in {instance_file}")
    port = int(links[0].get("port", TM_INJECT_PORT_FALLBACK))
    if not 1 <= port <= 65535:
        raise ValueError(f"invalid UDP_TM_SPLIT_IN port: {port}")
    return port


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repo = args.repo.expanduser().resolve(strict=True)
    station = args.station.expanduser().resolve()
    config_dir = station / "config"
    dictionary = station / "dictionary.json"
    data_dir = station / "data"
    received_dir = station / "received-files"
    logs_dir = station / "logs"
    venv = repo / "tools" / "yamcs" / ".venv"

    if not (venv / "bin" / "python").is_file():
        raise SystemExit(f"missing Python environment at {venv}; run tools/yamcs/bootstrap.sh")
    if not config_dir.is_dir() or not (config_dir / "etc" / "yamcs.yaml").is_file():
        raise SystemExit(f"missing persistent Yamcs config at {config_dir}")
    if not dictionary.is_file():
        raise SystemExit(f"missing matching F' dictionary at {dictionary}")

    validate_dictionary(dictionary)
    data_dir.mkdir(parents=True, exist_ok=True)
    received_dir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    write_default_logging_config(config_dir, logs_dir)

    env = os.environ.copy()
    env["PATH"] = str(venv / "bin") + os.pathsep + env.get("PATH", "")
    env["FPRIME_DICTIONARY"] = str(dictionary)
    env["FPRIME_YAMCS_TM_INJECT_PORT"] = str(TM_INJECT_PORT_FALLBACK)
    env["FPRIME_DOWNLINK_DIR"] = str(received_dir)
    try:
        instance = generate_xtce(config_dir, dictionary, env, venv)
        env["FPRIME_YAMCS_INSTANCE"] = instance
        env["FPRIME_YAMCS_TM_INJECT_PORT"] = str(split_tm_inject_port(config_dir, instance))

        # Resolve classpath and Java from the same pinned fprime-yamcs runtime
        # used by ProcessRunner helpers, then exec Java so systemd owns its PID.
        sys.path.insert(0, str(venv / "lib" / f"python{sys.version_info.major}.{sys.version_info.minor}" / "site-packages"))
        from fprime_yamcs.java import build_classpath, find_java, yamcs_launch_command

        command = yamcs_launch_command(
            find_java(), build_classpath([]), config_dir / "etc", data_dir,
            jvm_args=["-Xmx1024m"],
        )
    except Exception as exc:
        raise SystemExit(f"cannot prepare Yamcs: {exc}") from exc

    print(f"Starting Yamcs instance {instance}; OBC TCP peer {args.address} is handled by n2-ground-bridge.service",
          flush=True)
    os.chdir(repo)
    os.execvpe(command[0], command, env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
