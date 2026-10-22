#!/usr/bin/env python3
"""Fail closed unless every recorded firmware version is the same string.

ESP-IDF v6.1 (tools/cmake/project.cmake) copies PROJECT_VER into the build
when it is set before project(). components/esp_app_format then writes that
value to esp_app_desc_t.version (32 bytes, truncated to 31 characters plus
NUL). This repository sets it in the root CMakeLists.txt. version.txt and
git describe are used only when PROJECT_VER is unset; those fallbacks are
rejected here. CONFIG_APP_PROJECT_VER_FROM_CONFIG and
CONFIG_APP_EXCLUDE_PROJECT_VER_VAR would stop PROJECT_VER from being the
embedded version, so enabling either fails the check.

Compared fields, all required:
  VERSION
  root CMakeLists.txt PROJECT_VER
  project.yaml development_version
  components/nn2_core/include/nn2_core_types.h NN2_VERSION_STRING
  git tag name, only when --tag is set or GITHUB_REF_TYPE=tag

A missing field, a second definition, or a disagreement exits 1.
The tag is compared exactly: a leading "v" is not removed.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

MAX_LEN = 31
MAGIC = b"\x32\x54\xcd\xab"
OVERRIDE_LINES = (
    "CONFIG_APP_EXCLUDE_PROJECT_VER_VAR=y",
    "CONFIG_APP_PROJECT_VER_FROM_CONFIG=y",
)
CORE_HEADER = Path("components/nn2_core/include/nn2_core_types.h")


class VersionError(Exception):
    """The tree does not have one unambiguous version."""


def _one_line(path: Path, label: str) -> str:
    if not path.is_file():
        raise VersionError(f"missing {label}: {path}")
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise VersionError(f"unreadable {label}: {path}: {exc}") from exc
    if data.startswith(b"\xef\xbb\xbf"):
        raise VersionError(f"{label} has a UTF-8 BOM: {path}")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise VersionError(f"{label} is not UTF-8: {path}") from exc
    if text.endswith("\r\n"):
        text = text[:-2]
    elif text.endswith("\n"):
        text = text[:-1]
    if text != text.strip() or "\n" in text or "\r" in text or not text:
        raise VersionError(f"{label} must be one non-empty line without surrounding space: {path}")
    _validate_token(text, label)
    return text


def _validate_token(value: str, label: str) -> None:
    if not value or len(value) > MAX_LEN or any(ch.isspace() or not ch.isascii() or not ch.isprintable() for ch in value):
        raise VersionError(f"{label} must be 1..{MAX_LEN} printable ASCII characters with no whitespace")


def _exact_lines(path: Path, label: str, kind: str) -> list[str]:
    if not path.is_file():
        raise VersionError(f"missing {label}: {path}")
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as exc:
        raise VersionError(f"unreadable {label}: {path}: {exc}") from exc
    found = []
    for line in lines:
        matched = _match_pattern(line.strip(), kind)
        if matched is None:
            continue
        _validate_token(matched, label)
        found.append(matched)
    if not found:
        raise VersionError(f"missing {label} in {path}")
    if len(found) != 1:
        raise VersionError(f"multiple {label} entries in {path}")
    return found


def _unquote(rest: str, label: str, line: str) -> str:
    if len(rest) < 2 or rest[0] not in "\"'" or rest[-1] != rest[0] or rest[0] in rest[1:-1]:
        raise VersionError(f"malformed {label}: {line}")
    return rest[1:-1]


def _match_pattern(stripped: str, kind: str) -> str | None:
    if kind == "project_ver":
        if not stripped.startswith("set(PROJECT_VER"):
            return None
        if stripped.startswith("set(PROJECT_VER_") or stripped.startswith("set(PROJECT_VER)"):
            return None
        if not (stripped.startswith("set(PROJECT_VER ") and stripped.endswith(")")):
            raise VersionError(f"malformed PROJECT_VER: {stripped}")
        return _unquote(stripped[len("set(PROJECT_VER "):-1].strip(), "PROJECT_VER", stripped)
    if kind == "core":
        prefix = "#define NN2_VERSION_STRING"
        if not stripped.startswith(prefix):
            return None
        if stripped.startswith(prefix + "_"):
            return None
        if not stripped.startswith(prefix + " "):
            raise VersionError(f"malformed NN2_VERSION_STRING: {stripped}")
        return _unquote(stripped[len(prefix):].strip(), "NN2_VERSION_STRING", stripped)
    if kind == "yaml":
        prefix = "development_version:"
        if not stripped.startswith(prefix):
            return None
        return _unquote(stripped[len(prefix):].strip(), "development_version", stripped)
    raise VersionError(f"unknown pattern {kind}")


def _version_txt(root: Path, expected: str) -> None:
    path = root / "version.txt"
    if not path.exists():
        return
    value = _one_line(path, "version.txt")
    if value != expected:
        raise VersionError(f"version.txt {value!r} != PROJECT_VER {expected!r}; IDF would ignore it only while PROJECT_VER is set")


def _sdkconfig_overrides(root: Path) -> None:
    paths = sorted(path for path in root.glob("sdkconfig*") if path.is_file())
    for path in paths:
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError) as exc:
            raise VersionError(f"unreadable sdkconfig {path}: {exc}") from exc
        for line in lines:
            stripped = line.strip()
            if stripped in OVERRIDE_LINES:
                raise VersionError(f"{path.name} enables {stripped}, so esp_app_desc.version would not be PROJECT_VER")


def _app_desc_version(path: Path) -> str:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise VersionError(f"unreadable app binary {path}: {exc}") from exc
    found = []
    start = 0
    while True:
        index = data.find(MAGIC, start)
        if index < 0:
            break
        start = index + 1
        field = data[index + 16:index + 48]
        if len(field) != 32 or b"\x00" not in field:
            raise VersionError(f"{path} esp_app_desc version field is missing or not NUL-terminated")
        raw, padding = field.split(b"\x00", 1)
        if any(byte != 0 for byte in padding):
            raise VersionError(f"{path} esp_app_desc version padding is not NUL")
        try:
            text = raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise VersionError(f"{path} esp_app_desc version is not ASCII") from exc
        found.append(text)
    if len(found) != 1:
        raise VersionError(f"{path} has {len(found)} esp_app_desc magic words; expected 1")
    return found[0]


def resolve_tag(explicit: str | None, env: dict[str, str] | None = None) -> str | None:
    """Return the tag that must match, or None when this run is not a tag."""
    environ = os.environ if env is None else env
    ref_type = environ.get("GITHUB_REF_TYPE", "")
    ref_name = environ.get("GITHUB_REF_NAME", "")
    if ref_type == "tag":
        if not ref_name:
            raise VersionError("GITHUB_REF_TYPE=tag but GITHUB_REF_NAME is missing")
        if explicit is not None and explicit != ref_name:
            raise VersionError(f"--tag {explicit!r} != GITHUB_REF_NAME {ref_name!r}")
        _validate_token(ref_name, "git tag")
        return ref_name
    if explicit is None:
        return None
    _validate_token(explicit, "git tag")
    return explicit


def check_tree(root: Path, tag: str | None = None, app_bins: tuple[Path, ...] = ()) -> str:
    root = root.resolve()
    version_file = _one_line(root / "VERSION", "VERSION")
    project_ver = _exact_lines(root / "CMakeLists.txt", "PROJECT_VER", "project_ver")[0]
    core_version = _exact_lines(root / CORE_HEADER, "NN2_VERSION_STRING", "core")[0]
    yaml_version = _exact_lines(root / "project.yaml", "development_version", "yaml")[0]
    values = {
        "VERSION": version_file,
        "PROJECT_VER": project_ver,
        "NN2_VERSION_STRING": core_version,
        "development_version": yaml_version,
    }
    unique = set(values.values())
    if len(unique) != 1:
        detail = ", ".join(f"{name}={value}" for name, value in values.items())
        raise VersionError(f"version mismatch: {detail}")
    expected = version_file
    _version_txt(root, expected)
    _sdkconfig_overrides(root)
    if tag is not None and tag != expected:
        raise VersionError(f"git tag {tag!r} != version {expected!r}")
    embedded = []
    for binary in app_bins:
        found = _app_desc_version(binary)
        embedded.append(found)
        if found != expected:
            raise VersionError(f"{binary} esp_app_desc.version {found!r} != {expected!r}")
    tag_text = tag if tag is not None else "not-checked"
    bins = ",".join(embedded) if embedded else "not-checked"
    return f"PASS version={expected} tag={tag_text} app_desc={bins}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Fail closed on firmware version drift.")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--tag", default=None)
    parser.add_argument("--app-bin", type=Path, action="append", default=[])
    args = parser.parse_args(argv)
    try:
        tag = resolve_tag(args.tag)
        print(check_tree(args.root, tag=tag, app_bins=tuple(args.app_bin)))
    except VersionError as exc:
        print(f"FAIL: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
