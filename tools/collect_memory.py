#!/usr/bin/env python3
"""Build an exact clean checkout and archive ESP-IDF memory evidence (no hardware)."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

from memory_report import InvalidData, normalize, write

EXPECTED_IDF = "fff9895c82d744c7237be8847347bdd1b07c6643"
IMAGE = "espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def config_digest(text):
    values = {}
    for line in text.splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
        else:
            match = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
            if not match:
                continue
            key, value = match[1], "n"
        # IDF 6.1 emits identical legacy aliases twice in its deprecated section.
        # Conflicting duplicates are ambiguous and must still fail.
        if key in values and values[key] != value:
            raise InvalidData(f"conflicting duplicate sdkconfig value: {key}")
        values[key] = value
    if values.get("CONFIG_IDF_TARGET") != '"esp32s3"':
        raise InvalidData("generated sdkconfig target is not esp32s3")
    return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()


def partition_capacity(text):
    sizes = []
    for row in csv.reader(line for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")):
        if len(row) < 5:
            raise InvalidData("malformed generated partition CSV")
        if row[1].strip() not in ("app", "0", "0x00"):
            continue
        raw = row[4].strip()
        try:
            size = int(raw[:-1], 0) * {"K": 1024, "M": 1048576}[raw[-1].upper()] if raw[-1].upper() in "KM" else int(raw, 0)
        except (ValueError, IndexError, KeyError) as exc:
            raise InvalidData("invalid app partition size") from exc
        if size <= 0:
            raise InvalidData("nonpositive app partition")
        sizes.append(size)
    if not sizes:
        raise InvalidData("no app partition in generated table")
    return min(sizes)


def collect(project, out, expected_sha, reuse=False):
    project, out = Path(project).resolve(), Path(out).resolve()
    if out.is_relative_to(project):
        raise InvalidData("evidence output must be outside the source checkout")
    out.mkdir(parents=True, exist_ok=False)
    commands = []
    def run(args, log):
        completed = subprocess.run([str(x) for x in args], cwd=project, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        (out / log).write_text(completed.stdout, encoding="utf-8")
        commands.append({"argv": [str(x) for x in args], "cwd": str(project), "exit_code": completed.returncode, "log": log})
        write(out / "commands.json", commands)
        if completed.returncode:
            raise InvalidData(f"command failed ({completed.returncode}); see {log}")
        return completed.stdout.strip()
    try:
        source = run(["git", "rev-parse", "HEAD"], "source-sha.txt")
        if source != expected_sha or not re.fullmatch(r"[0-9a-f]{40}", source):
            raise InvalidData("checkout SHA differs from expected full source SHA")
        if run(["git", "status", "--porcelain", "--untracked-files=normal"], "source-status.txt"):
            raise InvalidData("source changes or untracked inputs present")
        idf = Path(os.environ["IDF_PATH"]).resolve()
        if run(["git", "-C", idf, "rev-parse", "HEAD"], "idf-commit.txt") != EXPECTED_IDF:
            raise InvalidData("ESP-IDF commit differs from v6.1 pin")
        if run(["git", "-C", idf, "status", "--porcelain", "--untracked-files=no"], "idf-status.txt"):
            raise InvalidData("modified tracked ESP-IDF sources")
        compiler = run(["xtensa-esp32s3-elf-gcc", "--version"], "compiler.txt").splitlines()[0]
        if "esp-15.2.0_20251204" not in compiler or "15.2.0" not in compiler:
            raise InvalidData("unexpected compiler")
        flags = {key: os.environ[key] for key in ("CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "SOURCE_DATE_EPOCH") if key in os.environ}
        if not reuse and ((project / "build").exists() or (project / "sdkconfig").exists()):
            raise InvalidData("clean build requires absent build/ and sdkconfig; use a fresh checkout")
        idf_py = [sys.executable, str(idf / "tools/idf.py")]
        if not reuse:
            run(idf_py + ["set-target", "esp32s3"], "set-target.log")
        run(idf_py + ["build"], "build.log")
        for tool, log in (([sys.executable, "--version"], "python.txt"),
                          (["cmake", "--version"], "cmake.txt"), (["ninja", "--version"], "ninja.txt"),
                          (idf_py + ["--version"], "idf-version.txt")):
            run(tool, log)
        for action in ("size", "size-components", "size-files"):
            run(idf_py + [action], action + ".txt")
        build = project / "build"
        desc = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
        if desc.get("target") != "esp32s3":
            raise InvalidData("project target mismatch")
        name = desc["project_name"]
        map_file = build / (name + ".map")
        for option, filename in (([], "size.json"), (["--archives"], "size-components.json"), (["--files"], "size-files.json")):
            run([sys.executable, "-m", "esp_idf_size", "--format", "json2", *option,
                 "--output-file", out / filename, map_file], filename + ".log")
        run([sys.executable, idf / "components/partition_table/gen_esp32part.py",
             build / "partition_table/partition-table.bin", out / "partitions.csv"], "partitions.log")
        for path in (project / "sdkconfig", map_file, build / (name + ".elf"), build / (name + ".bin"),
                     build / "project_description.json", build / "compile_commands.json",
                     build / "esp-idf/esp_system/ld/memory.ld", build / "esp-idf/esp_system/ld/sections.ld",
                     build / "partition_table/partition-table.bin"):
            shutil.copyfile(path, out / path.name)
        cfg = (out / "sdkconfig").read_text(encoding="utf-8")
        provenance = {"source_sha": source, "target": "esp32s3", "idf_commit": EXPECTED_IDF,
                      "compiler": compiler, "size_tool_version": version("esp-idf-size"),
                      "sdkconfig_values_sha256": config_digest(cfg), "sdkconfig_sha256": sha(out / "sdkconfig"),
                      "build_flags": json.dumps(flags, sort_keys=True),
                      "utc": datetime.now(timezone.utc).isoformat(),
                      "environment": platform.platform() + "; Python " + platform.python_version(),
                      "container_reference": os.environ.get("MEMORY_CONTAINER_IMAGE", "not used (local build)"),
                      "report_sha256": sha(out / "size.json"), "map_sha256": sha(out / map_file.name),
                      "ci_run_url": os.environ.get("MEMORY_RUN_URL", "local; not immutable CI evidence"),
                      "collection_mode": "existing-build-rebuilt" if reuse else "clean-build"}
        raw = json.loads((out / "size.json").read_text(encoding="utf-8"))
        result = normalize(raw, provenance, (out / (name + ".bin")).stat().st_size,
                           partition_capacity((out / "partitions.csv").read_text(encoding="utf-8")))
        write(out / "metrics.json", result)
        hashes = {p.name: sha(p) for p in sorted(out.iterdir()) if p.is_file()}
        write(out / "SHA256SUMS.json", hashes)
        print(json.dumps(result, indent=2))
    except Exception as exc:
        write(out / "failure.json", {"status": "INVALID", "error": str(exc), "commands": commands})
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--expected-sha", required=True)
    parser.add_argument("--reuse-build", action="store_true", help="Rebuild existing tree; provenance labels this deviation")
    args = parser.parse_args()
    collect(args.project, args.out, args.expected_sha, args.reuse_build)
