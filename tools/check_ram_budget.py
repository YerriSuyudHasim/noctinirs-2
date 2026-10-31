#!/usr/bin/env python3
"""Check static internal-RAM limits against a GNU ld linker map.

Usage (after an ESP-IDF build of the V4 project):
    python tools/check_ram_budget.py firmware\\v4-idf61\\build\\NoctiNIRSv4_IDF61.map
    python tools/check_ram_budget.py <map> --config config/ram_budget.json

Exit 0: every section total and every registered symbol is within its limit.
Exit 1: a limit is exceeded, or a registered symbol is missing or ambiguous in the map.
Exit 2: the map or config cannot be read or parsed.

Scope: static .dram0.data / .dram0.bss only. Heap is not in a linker map and is
not checked here (see docs/RAM_BUDGET.md). This is a static-source-of-truth check,
not a runtime measurement. Symbol sizes come from per-object input sections,
which need -fdata-sections (ESP-IDF default).
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "config" / "ram_budget.json"
OUTPUT_SECTIONS = (".dram0.data", ".dram0.bss")
HEX = r"0x[0-9a-fA-F]+"
OUTPUT_LINE = re.compile(r"^(\.dram0\.(?:data|bss))\s+(" + HEX + r")\s+(" + HEX + r")\s*$")
OUTPUT_NAME_ONLY = re.compile(r"^(\.dram0\.(?:data|bss))\s*$")
ADDR_SIZE = re.compile(r"^\s+(" + HEX + r")\s+(" + HEX + r")\s*$")
INPUT_LINE = re.compile(r"^\s(\.\S+)\s+(" + HEX + r")\s+(" + HEX + r")\s+(\S.*?)\s*$")
INPUT_NAME_ONLY = re.compile(r"^\s(\.\S+)\s*$")
INPUT_ADDR_LINE = re.compile(r"^\s+(" + HEX + r")\s+(" + HEX + r")\s+(\S.*?)\s*$")


class MapError(Exception):
    pass


def parse_map(text):
    """Return (output_totals, input_sections).

    output_totals: {".dram0.bss": size, ...} from the output-section header lines.
    input_sections: list of (output_section, input_name, size, object_path).
    """
    output_totals = {}
    inputs = []
    current_output = None
    pending_output = None
    pending_input = None
    for raw in text.splitlines():
        line = raw.rstrip()
        if pending_output is not None:
            match = ADDR_SIZE.match(line)
            if match:
                output_totals[pending_output] = int(match.group(2), 16)
                current_output = pending_output
            pending_output = None
            continue
        if pending_input is not None:
            match = INPUT_ADDR_LINE.match(line)
            if match:
                inputs.append((current_output, pending_input, int(match.group(2), 16), match.group(3)))
            pending_input = None
            continue
        match = OUTPUT_LINE.match(line)
        if match:
            output_totals[match.group(1)] = int(match.group(3), 16)
            current_output = match.group(1)
            continue
        match = OUTPUT_NAME_ONLY.match(line)
        if match:
            pending_output = match.group(1)
            continue
        if line and not line.startswith(" "):
            current_output = None
            continue
        match = INPUT_LINE.match(line)
        if match and current_output in OUTPUT_SECTIONS:
            inputs.append((current_output, match.group(1), int(match.group(3), 16), match.group(4)))
            continue
        match = INPUT_NAME_ONLY.match(line)
        if match and current_output in OUTPUT_SECTIONS:
            pending_input = match.group(1)
    missing = [name for name in OUTPUT_SECTIONS if name not in output_totals]
    if missing:
        raise MapError("output section(s) not found in map: " + ", ".join(missing))
    return output_totals, inputs


def symbol_size(entry, inputs):
    """Size of one registered static, from its own input section. Fail-closed on 0 or >1 match."""
    section = "." + entry["section"]
    pattern = re.compile(r"^\.(?:dram0\.)?" + re.escape(section[1:]) + r"\." + re.escape(entry["name"])
                         + r"(?:\.\d+)?$")
    hits = []
    for output, name, size, obj in inputs:
        if output not in OUTPUT_SECTIONS:
            continue
        if not pattern.match(name):
            continue
        if entry.get("object_contains") and entry["object_contains"] not in obj:
            continue
        hits.append((name, size, obj))
    if not hits:
        raise MapError("symbol not found: " + entry["label"] + " (needs -fdata-sections input sections)")
    objects = {obj for _, _, obj in hits}
    if len(objects) != 1:
        raise MapError("symbol ambiguous: " + entry["label"] + " matches " + "; ".join(sorted(objects)))
    return sum(size for _, size, _ in hits)


def check(map_text, config):
    output_totals, inputs = parse_map(map_text)
    rows, failures = [], []
    for name in OUTPUT_SECTIONS:
        limit = int(config["sections"][name]["limit_bytes"])
        used = output_totals[name]
        rows.append((name, used, limit))
        if used > limit:
            failures.append(f"{name} {used} B > limit {limit} B")
    for entry in config["symbols"]:
        try:
            used = symbol_size(entry, inputs)
        except MapError as error:
            failures.append(str(error))
            continue
        limit = int(entry["limit_bytes"])
        rows.append((entry["label"], used, limit))
        if used > limit:
            failures.append(f"{entry['label']} {used} B > limit {limit} B")
    return rows, failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("map", type=Path, help="GNU ld map, e.g. firmware/v4-idf61/build/NoctiNIRSv4_IDF61.map")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    args = parser.parse_args(argv)
    try:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        rows, failures = check(args.map.read_text(encoding="utf-8", errors="replace"), config)
    except (OSError, ValueError, KeyError, MapError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 2
    print(f"{'item':<36}{'used_B':>10}{'limit_B':>10}  status")
    for name, used, limit in rows:
        print(f"{name:<36}{used:>10}{limit:>10}  {'OK' if used <= limit else 'OVER'}")
    if failures:
        print("FAIL:")
        for failure in failures:
            print("  - " + failure)
        return 1
    print("PASS: static internal-RAM limits hold (heap is not checked here).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
