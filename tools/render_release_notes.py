#!/usr/bin/env python3
"""Render release notes from one Keep a Changelog version section.

CHANGELOG.md is owned by issue #14 / PR #42. This script does not create
that file. A missing file, a missing '## [version]' heading, or an empty
section fails. '## Unreleased' is not treated as release notes.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def render(changelog: Path, version: str) -> str:
    if not version or any(ch.isspace() for ch in version):
        raise ValueError("version must be one token")
    if not changelog.is_file():
        raise ValueError(
            f"missing {changelog}. Release notes require the Keep a Changelog "
            "file from issue #14 / PR #42; this script does not create one.")
    try:
        lines = changelog.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as exc:
        raise ValueError(f"unreadable {changelog}: {exc}") from exc
    heading = f"## [{version}]"
    start = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped == heading or stripped.startswith(heading + " "):
            if start is not None:
                raise ValueError(f"multiple {heading} sections in {changelog}")
            start = index + 1
            continue
        if start is not None and stripped.startswith("## "):
            body = lines[start:index]
            break
    else:
        if start is None:
            raise ValueError(
                f"no '{heading}' section in {changelog}. "
                "'## Unreleased' is not a release section.")
        body = lines[start:]
    while body and not body[0].strip():
        body = body[1:]
    while body and not body[-1].strip():
        body = body[:-1]
    if not any(line.strip() for line in body):
        raise ValueError(f"empty '{heading}' section in {changelog}")
    text = "\n".join(body)
    return (
        f"# Release notes {version}\n\n"
        "Generated from CHANGELOG.md. Not a GitHub release by itself.\n\n"
        f"{text}\n"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Render release notes from CHANGELOG.md.")
    parser.add_argument("--changelog", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    try:
        sys.stdout.write(render(args.changelog, args.version))
    except ValueError as exc:
        print(f"FAIL: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
