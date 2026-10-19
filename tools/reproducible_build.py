#!/usr/bin/env python3
"""Issue #9: collect raw build evidence and compare every binary, fail closed."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys

IMAGE = "espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c"
IDF_COMMIT = "fff9895c82d744c7237be8847347bdd1b07c6643"
COMPILER = "xtensa-esp-elf-gcc (crosstool-NG esp-15.2.0_20251204) 15.2.0"
REQUIRED = {"bootloader/bootloader.bin", "partition_table/partition-table.bin", "noctinirs2.bin"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def command(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT).strip()


def inventory(root):
    """Never follow symlinks, truncate, ignore an unreadable file, or filter bins."""
    result = {}
    if not root.is_dir() or root.is_symlink():
        raise ValueError(f"missing or symlinked binary directory: {root}")
    pending = [root]
    while pending:
        # scandir iteration and entry classification can both raise. Do not use
        # os.walk's default error suppression or accept a partial inventory.
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                path = Path(entry.path)
                mode = entry.stat(follow_symlinks=False).st_mode
                if stat.S_ISLNK(mode):
                    raise ValueError(f"symlink in binary tree: {path}")
                if stat.S_ISDIR(mode):
                    pending.append(path)
                elif path.suffix == ".bin":
                    if not stat.S_ISREG(mode):
                        raise ValueError(f"non-regular binary: {path}")
                    result[path.relative_to(root).as_posix()] = {
                        "size": path.stat().st_size, "sha256": digest(path)
                    }
    return dict(sorted(result.items()))


def warning_count(path):
    """Require readable, nonempty regular UTF-8 text; never infer zero on error."""
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f"warning log is not a regular file: {path}")
    # Nonblocking open prevents a replaced FIFO from hanging validation.
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(fd, "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError(f"warning log is not a regular file: {path}")
        text = stream.read().decode("utf-8")
    if not text or "\0" in text:
        raise ValueError(f"warning log is empty or contains NUL bytes: {path}")
    return sum(bool(re.search(r"warning:", line, re.IGNORECASE)) for line in text.splitlines())


def verify_warnings(log, output):
    # Remove stale counts before any operation that may fail.
    output.unlink(missing_ok=True)
    try:
        count = warning_count(log)
    except (OSError, ValueError) as error:
        print(f"cannot validate warning log: {error}", file=sys.stderr)
        return False
    output.write_text(f"{count}\n", encoding="utf-8")
    print(f"Compiler warning lines: {count}")
    return count == 0


def config_values(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def snapshot(workspace, output, sha, build_exit):
    output.mkdir(parents=True, exist_ok=True)
    errors = []
    metadata = {
        "source_sha": sha, "workspace": str(workspace.resolve()),
        "build_exit": build_exit, "image": os.environ.get("IDF_CONTAINER_IMAGE"),
        "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "platform": platform.platform(), "machine": platform.machine(),
        "python": sys.version, "run_url": os.environ.get("CI_RUN_URL"),
        "event": os.environ.get("GITHUB_EVENT_NAME"),
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
        "environment": {key: os.environ.get(key) for key in (
            "TZ", "LC_ALL", "SOURCE_DATE_EPOCH", "CCACHE_DISABLE", "IDF_CCACHE_ENABLE",
            "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "IDF_PATH", "IDF_TOOLS_PATH")},
    }
    checks = {
        "actual_sha": ("git", "-C", str(workspace), "rev-parse", "HEAD"),
        "source_status": ("git", "-C", str(workspace), "status", "--porcelain", "--untracked-files=no"),
        "idf_commit": ("git", "-C", os.environ.get("IDF_PATH", "/missing-idf"), "rev-parse", "HEAD"),
        "idf_version": ("idf.py", "--version"),
        "compiler": ("xtensa-esp32s3-elf-gcc", "--version"),
        "cmake": ("cmake", "--version"), "ninja": ("ninja", "--version"),
    }
    for key, args in checks.items():
        try:
            metadata[key] = command(*args)
        except (OSError, subprocess.CalledProcessError) as error:
            errors.append(f"cannot record {key}: {error}")
    if not re.fullmatch(r"[0-9a-f]{40}", sha) or metadata.get("actual_sha") != sha:
        errors.append("source SHA is invalid or checkout differs from requested SHA")
    if metadata.get("source_status"):
        errors.append("tracked source changed during build")
    if metadata.get("idf_commit") != IDF_COMMIT:
        errors.append("ESP-IDF commit does not match pinned v6.1")
    if metadata.get("compiler", "").splitlines()[:1] != [COMPILER]:
        errors.append("compiler version does not match pinned v6.1 toolchain")
    if metadata["image"] != IMAGE:
        errors.append("container image does not match immutable pin")
    if build_exit != 0:
        errors.append(f"incomplete build: command exit {build_exit}")
    build = workspace / "build"
    try:
        artifacts = inventory(build)
        for name in artifacts:
            destination = output / "bins" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(build / name, destination)
    except (OSError, ValueError) as error:
        errors.append(str(error))
        artifacts = {}
    for name in sorted(REQUIRED - artifacts.keys()):
        errors.append(f"incomplete build: required binary missing: {name}")
    for name, entry in artifacts.items():
        if entry["size"] == 0:
            errors.append(f"incomplete build: empty binary: {name}")
    for name in ("sdkconfig", "sdkconfig.defaults", "build/flasher_args.json",
                 "build/project_description.json", "build/compile_commands.json",
                 "build/bootloader/compile_commands.json", "build/gdbinit/prefix_map"):
        path = workspace / name
        if not path.is_file():
            errors.append(f"incomplete build: missing metadata: {name}")
        else:
            dest = output / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
    try:
        config = config_values(workspace / "sdkconfig")
        metadata["config"] = {k: config.get(k, "n") for k in (
            "CONFIG_APP_REPRODUCIBLE_BUILD", "CONFIG_APP_COMPILE_TIME_DATE",
            "CONFIG_BOOTLOADER_COMPILE_TIME_DATE", "CONFIG_COMPILER_HIDE_PATHS_MACROS", "CONFIG_IDF_TARGET")}
        if metadata["config"] != {
            "CONFIG_APP_REPRODUCIBLE_BUILD": "y", "CONFIG_APP_COMPILE_TIME_DATE": "n",
            "CONFIG_BOOTLOADER_COMPILE_TIME_DATE": "n", "CONFIG_COMPILER_HIDE_PATHS_MACROS": "y",
            "CONFIG_IDF_TARGET": '"esp32s3"',
        }:
            errors.append("generated sdkconfig does not enable the required deterministic ESP32-S3 configuration")
        metadata["sdkconfig_sha256"] = digest(workspace / "sdkconfig")
        flash = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))["flash_files"]
        if not isinstance(flash, dict) or not flash:
            raise ValueError("empty or invalid flash_files")
        for name in flash.values():
            if name not in artifacts:
                errors.append(f"incomplete build: flasher input missing from binary inventory: {name}")
    except (OSError, ValueError, KeyError, TypeError) as error:
        errors.append(f"cannot validate build configuration/flash metadata: {error}")
    try:
        count = warning_count(output / "build.log")
        metadata["warning_count"] = count
        if count:
            errors.append(f"compiler/build warnings: {count} (see build.log)")
    except (OSError, ValueError) as error:
        errors.append(f"cannot validate warning log: {error}")
    manifest = {"schema": 1, "complete": not errors, "errors": errors,
                "metadata": metadata, "artifacts": artifacts}
    write_json(output / "manifest.json", manifest)
    return not errors


def load_build(root):
    errors = []
    manifest = {}
    artifacts = {}
    try:
        parsed = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
        if not isinstance(parsed, dict):
            raise ValueError("manifest must be a JSON object")
        manifest = parsed
        if manifest.get("schema") != 1 or manifest.get("complete") is not True:
            errors.append("incomplete/invalid build manifest")
        errors.extend(manifest.get("errors", []))
        artifacts = inventory(root / "bins")
        if manifest.get("artifacts") != artifacts:
            errors.append("recorded inventory differs from actual binaries (evidence changed/missing)")
        for name in sorted(REQUIRED - artifacts.keys()):
            errors.append(f"required binary missing: {name}")
        for name, entry in artifacts.items():
            if entry["size"] <= 0:
                errors.append(f"empty binary: {name}")
        meta = manifest["metadata"]
        if not re.fullmatch(r"[0-9a-f]{40}", meta["source_sha"]):
            errors.append("invalid source SHA")
        if meta["build_exit"] != 0:
            errors.append("build did not complete successfully")
        if meta["image"] != IMAGE or meta["idf_commit"] != IDF_COMMIT:
            errors.append("toolchain pin mismatch")
        if meta["compiler"].splitlines()[0] != COMPILER:
            errors.append("compiler pin mismatch")
        count = warning_count(root / "build.log")
        if type(meta["warning_count"]) is not int or meta["warning_count"] != count or count != 0:
            errors.append("nonzero or inconsistent warning count")
    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError) as error:
        errors.append(f"cannot validate build evidence: {error}")
    return manifest, artifacts, errors


def compare(left, right, output):
    output.mkdir(parents=True, exist_ok=True)
    a, aa, ae = load_build(left)
    b, ba, be = load_build(right)
    errors = [f"A: {e}" for e in ae] + [f"B: {e}" for e in be]
    am = a.get("metadata") if isinstance(a.get("metadata"), dict) else {}
    bm = b.get("metadata") if isinstance(b.get("metadata"), dict) else {}
    for key in ("source_sha", "image", "idf_commit", "compiler", "idf_version", "cmake", "ninja", "python", "sdkconfig_sha256"):
        if not am.get(key) or am.get(key) != bm.get(key):
            errors.append(f"environment mismatch or missing value: {key}: A={am.get(key)!r}, B={bm.get(key)!r}")
    if not am.get("workspace") or am.get("workspace") == bm.get("workspace"):
        errors.append("builds must use distinct nonempty workspace paths")
    rows = []
    for name in sorted(aa.keys() | ba.keys()):
        av, bv = aa.get(name), ba.get(name)
        status = "IDENTICAL" if av is not None and av == bv else "DIFFERENT"
        if av is None:
            status = "UNEXPECTED IN B / MISSING IN A"
        elif bv is None:
            status = "MISSING IN B"
        row = {"name": name, "A": av, "B": bv, "status": status}
        if status != "IDENTICAL":
            errors.append(f"{name}: {status}")
            if av is not None and bv is not None:
                ab, bb = (left / "bins" / name).read_bytes(), (right / "bins" / name).read_bytes()
                offsets = [i for i, (x, y) in enumerate(zip(ab, bb)) if x != y]
                row["different_bytes"] = len(offsets) + abs(len(ab) - len(bb))
                row["first_different_offsets"] = offsets[:32]
        rows.append(row)
    report = {"schema": 1, "result": "FAIL" if errors else "PASS", "errors": errors,
              "build_A": am, "build_B": bm, "artifacts": rows}
    write_json(output / "comparison.json", report)
    lines = [f"# Reproducible build: {report['result']}", "", "Class B only; no hardware validation.", "",
             "| Binary | A bytes | A SHA-256 | B bytes | B SHA-256 | Result |",
             "|---|---:|---|---:|---|---|"]
    for row in rows:
        av, bv = row["A"] or {}, row["B"] or {}
        lines.append(f"| {row['name']} | {av.get('size', '-')} | {av.get('sha256', '-')} | {bv.get('size', '-')} | {bv.get('sha256', '-')} | {row['status']} |")
    lines.extend(["", "## Diagnostics", ""] + ([f"- {e}" for e in errors] or ["No differences found."]))
    for row in rows:
        if "first_different_offsets" in row:
            lines.append(f"- {row['name']}: {row['different_bytes']} differing bytes; first offsets: {row['first_different_offsets']}")
    lines.extend(["", "## Environment A", "", "```json", json.dumps(am, indent=2), "```",
                  "", "## Environment B", "", "```json", json.dumps(bm, indent=2), "```", ""])
    (output / "comparison.md").write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))
    return not errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    snap = sub.add_parser("snapshot")
    snap.add_argument("--workspace", type=Path, required=True)
    snap.add_argument("--output", type=Path, required=True)
    snap.add_argument("--sha", required=True)
    snap.add_argument("--build-exit", type=int, required=True)
    diff = sub.add_parser("compare")
    diff.add_argument("left", type=Path)
    diff.add_argument("right", type=Path)
    diff.add_argument("--output", type=Path, required=True)
    warnings = sub.add_parser("warnings")
    warnings.add_argument("log", type=Path)
    warnings.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.mode == "snapshot":
        success = snapshot(args.workspace, args.output, args.sha, args.build_exit)
    elif args.mode == "warnings":
        success = verify_warnings(args.log, args.output)
    else:
        success = compare(args.left, args.right, args.output)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
