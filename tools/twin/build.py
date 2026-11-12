#!/usr/bin/env python3
"""Build the two real applications in container-owned build directories."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import importlib.util

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "fprime-artemis-core"


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def source_sha(path: Path) -> str:
    result = subprocess.run(["git", "-c", f"safe.directory={path}", "-C", str(path),
                             "rev-parse", "HEAD"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def git_state(path: Path) -> dict:
    prefix = ["git", "-c", f"safe.directory={path}", "-C", str(path)]
    result = subprocess.run([*prefix, "status", "--porcelain", "--untracked-files=normal"], capture_output=True, text=True)
    diff = subprocess.run([*prefix, "diff", "--binary", "HEAD"], capture_output=True)
    return {"sha": source_sha(path), "dirty": bool(result.stdout.strip()),
            "dirty_paths": result.stdout.splitlines(),
            "tracked_diff_sha256": hashlib.sha256(diff.stdout).hexdigest()}


def working_source_digest(root: Path = ROOT) -> str:
    """Hash compiled source, templates and native build configuration, including new files."""
    digest = hashlib.sha256()
    extensions = {".cpp", ".c", ".hpp", ".h", ".fpp", ".fppi", ".cmake", ".conf", ".overlay",
                  ".dts", ".ld", ".ini", ".yml", ".yaml", ".json", ".in"}
    paths = set()
    for tree in (root / "fprime-artemis-core", root / "tools/cross", root / "config"):
        for directory, names, files in os.walk(tree):
            names[:] = sorted(name for name in names if name not in ("lib", "fprime-venv", ".venv", "__pycache__", ".git")
                              and not name.startswith("build"))
            for name in files:
                path = Path(directory) / name
                if path.suffix in extensions or name == "CMakeLists.txt":
                    paths.add(path)
    paths.update(root / name for name in ("twin/native-linux.cmake", "tools/twin/build.py", "tools/twin/pdu_host.c"))
    for path in sorted(paths):
        if path.is_file():
            digest.update(str(path.relative_to(root)).encode() + b"\0")
            digest.update(path.read_bytes())
            digest.update(b"\0")
    return digest.hexdigest()


def select_dictionary(build: Path, role: str) -> Path:
    marker = "missionApp" if role == "fc" else "payloadManager"
    candidates: list[tuple[int, Path]] = []
    for path in build.rglob("*Dictionary.json"):
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        commands = data.get("commands", [])
        if any(marker in str(command.get("name", "")) for command in commands):
            candidates.append((len(commands), path))
    if not candidates:
        raise RuntimeError(f"No complete {role} topology dictionary under {build}")
    return max(candidates, key=lambda item: item[0])[1]


def build(role: str, jobs: int) -> None:
    directory = Path("/build") / role
    artifacts = Path("/artifacts") / role
    directory.mkdir(parents=True, exist_ok=True)
    libraries = str(PROJECT / "lib/fprime-zephyr") + ";" + str(ROOT / "tools/cross") if role == "fc" else str(ROOT / "tools/cross")
    common = ["cmake", "-S", str(PROJECT), "-B", str(directory), "-G", "Ninja",
              f"-DFPRIME_PROJECT_ROOT={PROJECT}",
              f"-DFPRIME_FRAMEWORK_PATH={PROJECT / 'lib/fprime'}",
              f"-DFPRIME_LIBRARY_LOCATIONS={libraries}",
              "-DFPRIME_ENABLE_FRAMEWORK_UTS=OFF", "-DFPRIME_ENABLE_AUTOCODER_UTS=OFF",
              f"-DFPRIME_INSTALL_DEST={artifacts}", f"-DUSER_CACHE_DIR={directory / '.cache'}"]
    if role == "fc":
        common += [f"-DCMAKE_TOOLCHAIN_FILE={PROJECT / 'lib/fprime-zephyr/cmake/toolchain/zephyr.cmake'}",
                   "-DBOARD=native_sim/native/64", "-DZEPHYR_TOOLCHAIN_VARIANT=host",
                   "-DCONF_FILE=prj_native.conf", "-DDTC_OVERLAY_FILE=boards/native_sim.overlay",
                   "-DARTEMIS_TWIN=ON", "-DARTEMIS_GROUND_LINK=RFM23"]
    else:
        common += [f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'twin/native-linux.cmake'}", "-DARTEMIS_TWIN=ON"]
    run(common)
    # RadioHead's standalone target needs generated offsets before compilation.
    if role == "fc":
        run(["cmake", "--build", str(directory), "--target", "offsets_h"])
    deployment = "FlightControllerDeployment" if role == "fc" else "PayloadComputerDeployment"
    run(["cmake", "--build", str(directory), "--target", f"FprimeArtemisCore_Deployments_{deployment}",
         "--parallel", str(jobs)])
    if role == "fc":
        binary = directory / "zephyr/zephyr.exe"
    else:
        candidates = [p for p in directory.rglob("*PayloadComputerDeployment")
                      if p.name in ("PayloadComputerDeployment", "FprimeArtemisCore_Deployments_PayloadComputerDeployment")
                      and p.is_file() and os.access(p, os.X_OK)]
        if not candidates:
            raise RuntimeError("PayloadComputerDeployment executable was not built")
        binary = candidates[0]
    if not binary.is_file():
        raise RuntimeError(f"Application executable missing: {binary}")
    dictionary = select_dictionary(directory, role)
    (artifacts / "bin").mkdir(parents=True, exist_ok=True)
    (artifacts / "dict").mkdir(parents=True, exist_ok=True)
    binary_out = artifacts / "bin" / ("flight-controller" if role == "fc" else "payload-computer")
    dictionary_out = artifacts / "dict/topology.json"
    # Atomic replacement also avoids ETXTBSY if a prior run has this inode open.
    for source, destination in ((binary, binary_out), (dictionary, dictionary_out)):
        temporary = destination.with_name(destination.name + ".new")
        shutil.copy2(source, temporary)
        temporary.replace(destination)
    project_state = git_state(ROOT)
    manifest = {"role": role, "source_sha": project_state["sha"], "source_dirty": project_state["dirty"],
                "source_dirty_paths": project_state["dirty_paths"], "working_source_sha256": working_source_digest(),
                "framework_sha": source_sha(PROJECT / "lib/fprime"),
                "zephyr_sha": source_sha(PROJECT / "lib/zephyr-workspace/zephyr"),
                "dependency_states": {name: git_state(PROJECT / "lib" / name)
                                      for name in ("fprime", "fprime-zephyr", "zephyr-workspace/zephyr")},
                "pdu_dependency": {**git_state(ROOT / "external/artemis-pdu"),
                                   "protocol_sha256": hashlib.sha256((ROOT / "external/artemis-pdu/src/pdu_protocol_v2.h").read_bytes()).hexdigest()},
                "binary_sha256": hashlib.sha256(binary_out.read_bytes()).hexdigest(),
                "dictionary_sha256": hashlib.sha256(dictionary_out.read_bytes()).hexdigest(),
                "binary": str(binary_out), "dictionary": str(dictionary_out)}
    (artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2), flush=True)


def build_pdu() -> None:
    """Compile actual application/parser C with the firmware's existing host shims."""
    source = ROOT / "external/artemis-pdu"
    directory = Path("/build/pdu")
    copied = directory / "src"
    copied.mkdir(parents=True, exist_ok=True)
    for name in ("app.c", "app.h", "pdu_packet.c", "pdu_packet.h", "pdu_protocol_v2.h", "version.h"):
        shutil.copy2(source / "src" / name, copied / name)
    helper = source / "ArtemisPDU.X/tools/update_build_info.py"
    spec = importlib.util.spec_from_file_location("pdu_build_info", helper)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    (copied / "pdu_build_info.h").write_text(module.render_header(source))
    run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-D_POSIX_C_SOURCE=200809L",
         "-I" + str(source / "tests/host/include"), "-I" + str(copied),
         str(ROOT / "tools/twin/pdu_host.c"), str(copied / "app.c"), str(copied / "pdu_packet.c"),
         "-o", str(directory / "pdu-host")])
    (directory / "manifest.json").write_text(json.dumps({"source": git_state(source),
                                                        "implementation": "actual app.c + pdu_packet.c; mocked GPIO/UART/ticks"}, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("role", choices=("fc", "pi", "pdu"))
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")
    build_pdu() if args.role == "pdu" else build(args.role, args.jobs)
