"""Configure stock Yamcs limits for the bounded local file publisher."""
from pathlib import Path
import json
import os
import stat
import tempfile

import yaml

MAX_FILE_BYTES = 128 * 1024 * 1024
BUCKET_BYTES = 2 * 1024 * 1024 * 1024


def atomic_text(path: Path, content: str, mode: int) -> None:
    """Publish one complete configuration; never truncate the active file."""
    descriptor, name = tempfile.mkstemp(prefix="." + path.name + ".", suffix=".tmp", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            if hasattr(os, "fchmod"):
                os.fchmod(output.fileno(), mode)
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        if os.name != "nt":
            directory = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        temporary.unlink(missing_ok=True)


def configure_file_delivery(config_dir: Path, dictionary: Path | None = None) -> None:
    """Validate the full migration, preserve a backup, then atomically replace."""
    path = config_dir / "etc" / "yamcs.yaml"
    original = path.read_text()
    config = yaml.safe_load(original) or {}
    servers = [service for service in config.get("services", [])
               if service.get("class", "").endswith("HttpServer")]
    if len(servers) != 1:
        raise ValueError("expected one Yamcs HTTP server for local file publication")
    args = servers[0].setdefault("args", {})
    args["maxContentLength"] = max(int(args.get("maxContentLength", 65536)), MAX_FILE_BYTES)
    buckets = config.setdefault("buckets", [])
    matches = [bucket for bucket in buckets if bucket.get("name") == "fprimeFilesIn"]
    if len(matches) > 1:
        raise ValueError("duplicate fprimeFilesIn bucket configuration")
    if matches:
        bucket = matches[0]
    else:
        bucket = {"name": "fprimeFilesIn"}
        buckets.append(bucket)
    bucket["maxSize"] = max(int(bucket.get("maxSize", 100_000_000)), BUCKET_BYTES)
    bucket["maxObjects"] = max(int(bucket.get("maxObjects", 1000)), 4096)
    changes = [(path, original, yaml.safe_dump(config, sort_keys=False))]
    constants = {}
    if dictionary is not None:
        constants = {item.get("qualifiedName"): item.get("value")
                     for item in json.loads(dictionary.read_text()).get("constants", [])}
        if not {"ComCfg.TmFrameFixedSize", "ComCfg.SpacecraftId"} <= constants.keys():
            raise ValueError("dictionary omits CCSDS frame size or spacecraft ID")
        if not 8 <= int(constants["ComCfg.TmFrameFixedSize"]) <= 65535:
            raise ValueError("invalid CCSDS frame size")
        if not 0 <= int(constants["ComCfg.SpacecraftId"]) <= 1023:
            raise ValueError("invalid CCSDS spacecraft ID")
    for instance_path in sorted((config_dir / "etc").glob("yamcs.*.yaml")):
        original = instance_path.read_text()
        instance = yaml.safe_load(original) or {}
        changed = False
        for link in instance.get("dataLinks", []):
            if link.get("name") == "UDP_TC_OUT" and constants:
                link["maxFrameLength"] = int(constants["ComCfg.TmFrameFixedSize"])
                link["spacecraftId"] = int(constants["ComCfg.SpacecraftId"])
                changed = True
            if link.get("name") == "UDP_TM_SPLIT_IN":
                link["maxLength"] = 65535
                changed = True
            if link.get("name") != "UDP_TM_IN":
                continue
            channel = next((item for item in link.get("virtualChannels", [])
                            if item.get("vcId") == 1), {})
            for key in ("frameType", "spacecraftId", "frameLength", "errorDetection", "virtualChannels"):
                link.pop(key, None)
            link.update({"class": "org.yamcs.tctm.UdpTmDataLink", "stream": "tm_realtime",
                         "maxLength": 65535,
                         "packetPreprocessorClassName": channel.get("packetPreprocessorClassName",
                             link.get("packetPreprocessorClassName", "com.example.myproject.FprimePacketPreprocessor"))})
            if "packetPreprocessorClassArgs" in channel:
                link["packetPreprocessorClassArgs"] = channel["packetPreprocessorClassArgs"]
            changed = True
        if changed:
            changes.append((instance_path, original, yaml.safe_dump(instance, sort_keys=False)))
    # Parsing/validation above has no disk mutations. Each replacement below
    # leaves either the old or new valid YAML; restart can finish a partial run.
    for target, original, replacement in changes:
        if replacement == original:
            continue
        mode = stat.S_IMODE(target.stat().st_mode)
        backup = target.with_name(target.name + ".before-local-files")
        if not backup.exists():
            atomic_text(backup, original, mode)
        atomic_text(target, replacement, mode)
