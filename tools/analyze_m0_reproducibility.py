#!/usr/bin/env python3
"""Read the two retained M0 ZIP artifacts; measure differences without rewriting bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sections(data):
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("expected a little-endian ELF32 image")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    start, width, count, string_index = header[6], header[11], header[12], header[13]
    entries = [struct.unpack_from("<IIIIIIIIII", data, start + i * width) for i in range(count)]
    names_header = entries[string_index]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    return {names[s[0]:].split(b"\0", 1)[0].decode():
            (data[s[4]:s[4] + s[5]] if s[1] != 8 else b"") for s in entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("push_zip", type=Path)
    parser.add_argument("pr_zip", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = {"source_sha": "cd4ed7f88905a1c430e0b942732ddc0613081aef",
              "push_run": 38847664721, "push_artifact_id": 12024271866,
              "pr_run": 38847669878, "pr_artifact_id": 12023717650,
              "method": "Raw ZIP member SHA-256 and byte/ELF-section comparison; no normalization",
              "files": {}}
    with zipfile.ZipFile(args.push_zip) as a, zipfile.ZipFile(args.pr_zip) as b:
        for side, archive in (("push", a), ("pr", b)):
            toolchain = archive.read("evidence/toolchain.txt").decode()
            if "SOURCE_SHA=" + report["source_sha"] not in toolchain:
                raise ValueError("wrong source SHA in M0 artifact")
            app = archive.read("build/noctinirs2.bin")
            elf = archive.read("build/noctinirs2.elf")
            boot = archive.read("build/bootloader/bootloader.bin")
            report[side] = {
                "toolchain": toolchain,
                "app_compile_time": app[112:128].split(b"\0")[0].decode(),
                "app_compile_date": app[128:144].split(b"\0")[0].decode(),
                "embedded_elf_sha256": app[176:208].hex(), "actual_elf_sha256": sha(elf),
                "bootloader_timestamp_region_hex": boot[80:112].hex(),
                "config": [line for line in archive.read("evidence/sdkconfig").decode().splitlines()
                           if any(key in line for key in ("REPRODUCIBLE", "COMPILE_TIME_DATE", "HIDE_PATHS"))],
                "absolute_project_path_in_elf": b"/__w/noctinirs-2/noctinirs-2" in elf,
                "absolute_idf_path_in_elf": b"/opt/esp/idf" in elf,
            }
        for name in sorted(n for n in a.namelist() if n.endswith((".bin", ".elf")) or n == "evidence/sdkconfig"):
            av, bv = a.read(name), b.read(name)
            offsets = [i for i, (x, y) in enumerate(zip(av, bv)) if x != y]
            entry = {"push_size": len(av), "pr_size": len(bv), "push_sha256": sha(av), "pr_sha256": sha(bv),
                     "different_bytes": len(offsets) + abs(len(av) - len(bv)),
                     "first_different_offsets": offsets[:100]}
            if name.endswith(".elf"):
                sa, sb = sections(av), sections(bv)
                entry["different_sections"] = [k for k in sa.keys() | sb.keys() if sa.get(k) != sb.get(k)]
            report["files"][name] = entry
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
