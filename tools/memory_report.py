#!/usr/bin/env python3
"""ESP32-S3 / esp-idf-size json2 v1.2 normalization and advisory comparison.

No third-party dependencies. Never interprets static headroom as runtime free heap.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys


class InvalidData(ValueError):
    pass


METRICS = ("iram_fixed", "iram_shared_text", "iram_placed", "dram_static",
           "diram", "flash_code", "flash_data", "flash_app")
IDENTITY = ("target", "idf_commit", "compiler", "size_tool_version",
            "sdkconfig_values_sha256", "build_flags")


def load(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise InvalidData(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=pairs,
                          parse_constant=lambda x: (_ for _ in ()).throw(InvalidData(x)))
    except (OSError, ValueError) as exc:
        raise InvalidData(f"{path}: {exc}") from exc


def integer(value, label, positive=False):
    if type(value) is not int or value < (1 if positive else 0):
        raise InvalidData(f"{label}: expected {'positive' if positive else 'nonnegative'} integer")
    return value


def metric(used, capacity=None):
    integer(used, "used")
    if capacity is not None:
        integer(capacity, "capacity", positive=True)
        if used > capacity:
            raise InvalidData("used exceeds capacity")
    return {"used_bytes": used, "capacity_bytes": capacity,
            "headroom_bytes": None if capacity is None else capacity - used,
            "used_percent": None if capacity is None else used * 100 / capacity}


def normalize(size, provenance, app_bytes, app_capacity):
    if not isinstance(size, dict) or size.get("version") != "1.2":
        raise InvalidData("expected esp-idf-size json2 schema version 1.2")
    integer(size.get("total_size"), "total_size", positive=True)
    layout = size.get("layout")
    if not isinstance(layout, list) or not layout:
        raise InvalidData("missing memory layout")
    regions = {}
    for row in layout:
        if not isinstance(row, dict) or not isinstance(row.get("name"), str):
            raise InvalidData("malformed memory region")
        name = row["name"]
        if name not in {"IRAM", "DIRAM", "Flash Code", "Flash Data", "RTC FAST", "RTC SLOW"}:
            raise InvalidData(f"unsupported memory region: {name}; extend accounting explicitly")
        if name in regions:
            raise InvalidData(f"duplicate region: {name}")
        used = integer(row.get("used"), name + ".used")
        total = integer(row.get("total"), name + ".total")
        free = integer(row.get("free"), name + ".free")
        parts = row.get("parts")
        if not isinstance(parts, dict) or not parts:
            raise InvalidData(f"{name}: missing sections")
        values = {k: integer(v.get("size") if isinstance(v, dict) else None, k)
                  for k, v in parts.items()}
        if sum(values.values()) != used:
            raise InvalidData(f"{name}: section sizes do not sum to used")
        if name.startswith("Flash "):
            # IDF uses zero as the sentinel for unknown flash window capacity.
            if total != 0 or free != 0:
                raise InvalidData("use default json2 output, without --use-linker-sizes")
        elif total <= 0 or used + free != total:
            raise InvalidData(f"{name}: invalid capacity/headroom")
        regions[name] = (used, total, values)
    for name in ("IRAM", "DIRAM", "Flash Code", "Flash Data"):
        if name not in regions:
            raise InvalidData(f"missing required region: {name}")
    iram, diram = regions["IRAM"], regions["DIRAM"]
    if set(iram[2]) - {".text", ".vectors"} or ".text" not in iram[2] or ".vectors" not in iram[2]:
        raise InvalidData("unsupported IRAM sections; review parser for new layout")
    if set(diram[2]) - {".text", ".data", ".bss", ".noinit"} or ".text" not in diram[2]:
        raise InvalidData("unsupported DIRAM sections; review parser for new layout")
    if not {".data", ".bss"} <= set(diram[2]):
        raise InvalidData("incomplete static DRAM sections")
    integer(app_bytes, "app binary bytes", positive=True)
    integer(app_capacity, "app partition bytes", positive=True)
    if size["total_size"] > app_bytes:
        raise InvalidData("reported image is larger than supplied app binary")
    result = {"schema_version": 1, "evidence_class": "B-static-build",
              "provenance": provenance, "metrics": {
                  "iram_fixed": metric(iram[0], iram[1]),
                  "iram_shared_text": metric(diram[2][".text"]),
                  "iram_placed": metric(iram[0] + diram[2][".text"]),
                  "dram_static": metric(diram[0] - diram[2][".text"]),
                  "diram": metric(diram[0], diram[1]),
                  "flash_code": metric(regions["Flash Code"][0]),
                  "flash_data": metric(regions["Flash Data"][0]),
                  "flash_app": metric(app_bytes, app_capacity)},
              "image_size_from_report_bytes": size["total_size"]}
    validate(result)
    return result


def validate(report):
    if not isinstance(report, dict) or report.get("schema_version") != 1:
        raise InvalidData("unsupported normalized schema")
    p = report.get("provenance")
    if not isinstance(p, dict):
        raise InvalidData("missing provenance")
    for key in (*IDENTITY, "source_sha", "utc", "environment", "report_sha256", "map_sha256"):
        if not isinstance(p.get(key), str) or not p[key].strip():
            raise InvalidData(f"missing provenance.{key}")
    if p["target"] != "esp32s3":
        raise InvalidData("parser supports ESP32-S3 only")
    for key, length in (("source_sha", 40), ("idf_commit", 40), ("report_sha256", 64),
                        ("map_sha256", 64), ("sdkconfig_values_sha256", 64)):
        if not re.fullmatch(r"[0-9a-f]{" + str(length) + "}", p[key]):
            raise InvalidData(f"invalid provenance.{key}")
    metrics = report.get("metrics")
    if not isinstance(metrics, dict) or set(metrics) != set(METRICS):
        raise InvalidData("incomplete metrics")
    for key, item in metrics.items():
        if not isinstance(item, dict):
            raise InvalidData(f"{key}: missing metric")
        expected = metric(item.get("used_bytes"), item.get("capacity_bytes"))
        if item != expected:
            raise InvalidData(f"{key}: inconsistent derived metric")
    for key in ("iram_fixed", "diram", "flash_app"):
        if metrics[key]["capacity_bytes"] is None:
            raise InvalidData(f"{key}: missing capacity")
    m = {k: v["used_bytes"] for k, v in metrics.items()}
    if m["iram_placed"] != m["iram_fixed"] + m["iram_shared_text"]:
        raise InvalidData("inconsistent IRAM total")
    if m["diram"] != m["iram_shared_text"] + m["dram_static"]:
        raise InvalidData("inconsistent shared RAM accounting")
    return report


def delta(current, baseline):
    if current is None or baseline is None:
        return {"bytes": None, "percent": None}
    difference = current - baseline
    return {"bytes": difference,
            "percent": difference * 100 / baseline if baseline else (0.0 if current == 0 else None)}


def validate_thresholds(thresholds):
    if not isinstance(thresholds, dict) or set(thresholds) - set(METRICS):
        raise InvalidData("unknown threshold metric")
    allowed = {"max_used_bytes", "max_used_percent", "min_headroom_bytes", "max_increase_bytes"}
    for name, bands in thresholds.items():
        if not isinstance(bands, dict) or set(bands) - {"warning", "critical"}:
            raise InvalidData(f"{name}: invalid threshold bands")
        for band, rules in bands.items():
            if not isinstance(rules, dict) or not rules or set(rules) - allowed:
                raise InvalidData(f"{name}.{band}: invalid threshold rules")
            for key, value in rules.items():
                if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                    raise InvalidData("threshold must be a finite nonnegative number")
                if key == "max_used_percent" and value > 100:
                    raise InvalidData("percentage threshold exceeds 100")
        for key in set(bands.get("warning", {})) & set(bands.get("critical", {})):
            warning, critical = bands["warning"][key], bands["critical"][key]
            if (key.startswith("min_") and critical > warning) or (not key.startswith("min_") and critical < warning):
                raise InvalidData("critical threshold must be at least as severe as warning")


def compare(current, baseline, thresholds):
    validate(current)
    validate_thresholds(thresholds)
    if baseline is not None:
        validate(baseline)
    mismatch = [key for key in IDENTITY if baseline and
                current["provenance"][key] != baseline["provenance"][key]]
    if baseline:
        mismatch += [key + ".capacity" for key in METRICS if
                     current["metrics"][key]["capacity_bytes"] != baseline["metrics"][key]["capacity_bytes"]]
    result = {"schema_version": 1, "advisory_only": True,
              "status": "NO_BASELINE" if baseline is None else "NOT_COMPARABLE" if mismatch else "OK",
              "identity_mismatches": mismatch, "current_provenance": current["provenance"],
              "baseline_provenance": baseline["provenance"] if baseline else None, "metrics": {}}
    for key in METRICS:
        item = current["metrics"][key]
        old = baseline["metrics"][key] if baseline else None
        row = {"current": item, "baseline": old,
               "used_delta": delta(item["used_bytes"], old["used_bytes"] if old else None),
               "headroom_delta": delta(item["headroom_bytes"], old["headroom_bytes"] if old else None),
               "alerts": []}
        for band in ("warning", "critical"):
            for rule, limit in thresholds.get(key, {}).get(band, {}).items():
                value = {"max_used_bytes": item["used_bytes"], "max_used_percent": item["used_percent"],
                         "min_headroom_bytes": item["headroom_bytes"],
                         "max_increase_bytes": row["used_delta"]["bytes"] if not mismatch else None}[rule]
                if value is None:
                    row["alerts"].append({"level": "UNAVAILABLE", "rule": rule, "limit": limit})
                elif (value < limit if rule.startswith("min_") else value > limit):
                    row["alerts"].append({"level": band.upper(), "rule": rule, "limit": limit, "value": value})
        result["metrics"][key] = row
    if result["status"] == "OK":
        levels = {a["level"] for m in result["metrics"].values() for a in m["alerts"]}
        result["status"] = next((v for v in ("CRITICAL", "WARNING", "UNAVAILABLE") if v in levels), "OK")
    return result


def markdown(result):
    def show(value):
        return "unavailable" if value is None else f"{value:.4f}" if isinstance(value, float) else str(value)
    lines = ["# Memory regression report", "", f"Status: **{result['status']}** (informational; no budget approval)", ""]
    if "error" in result:
        return "\n".join(lines + [result["error"], ""])
    lines += [f"Current SHA: `{result['current_provenance']['source_sha']}`",
              f"Baseline SHA: `{result['baseline_provenance']['source_sha'] if result['baseline_provenance'] else 'unavailable'}`",
              "", "| Metric | Current B | Capacity B | Headroom B | Delta B | Delta % | Headroom delta B |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    for key, row in result["metrics"].items():
        c = row["current"]
        values = (c["used_bytes"], c["capacity_bytes"], c["headroom_bytes"],
                  row["used_delta"]["bytes"], row["used_delta"]["percent"], row["headroom_delta"]["bytes"])
        lines.append("| " + key + " | " + " | ".join(show(v) for v in values) + " |")
    lines += ["", "IRAM placed includes fixed IRAM (vectors included) plus DIRAM text. Static DRAM is DIRAM data/BSS/noinit.",
              "These overlap the region totals: do not add every table row. Shared DIRAM headroom is counted once.",
              "Static build evidence does not establish peak runtime heap, stack high-water marks, fragmentation, or hardware timing.", ""]
    if result["identity_mismatches"]:
        lines.append("Environment/capacity mismatch (deltas are descriptive only): " + ", ".join(result["identity_mismatches"]))
    for key, row in result["metrics"].items():
        for alert in row["alerts"]:
            lines.append(f"- {key}: {alert['level']} {alert['rule']}={alert['limit']}")
    return "\n".join(lines) + "\n"


def write(path, data):
    Path(path).write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--thresholds", type=Path)
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = compare(load(args.current), load(args.baseline) if args.baseline else None,
                         load(args.thresholds) if args.thresholds else {})
    except (InvalidData, TypeError, KeyError) as exc:
        result = {"schema_version": 1, "advisory_only": True, "status": "INVALID", "error": str(exc)}
    write(args.json, result)
    args.markdown.write_text(markdown(result), encoding="utf-8")
    print(result["status"])
    return 2 if result["status"] == "INVALID" else 0


if __name__ == "__main__":
    sys.exit(main())
