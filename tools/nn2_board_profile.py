#!/usr/bin/env python3
"""Select an explicit local board policy or inspect generated config. Never builds or accesses a device."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CMAKE_ARGS = ["-G", "Ninja", "-DPYTHON_DEPS_CHECKED=1", "-DESP_PLATFORM=1"]


def load_profile(root, name):
    manifest = json.loads((root / "config/board-profiles.json").read_text(encoding="utf-8"))
    if manifest["schema_version"] != 1:
        raise ValueError("unsupported board-profile schema")
    try:
        return manifest["profiles"][name]
    except KeyError as error:
        raise ValueError("unknown board profile: " + name) from error


def patch_sdkconfig(text, profile):
    """Patch only declared profile keys; preserve all other bytes/line endings."""
    newline = "\r\n" if "\r\n" in text else "\n"
    target = re.findall(r'^CONFIG_IDF_TARGET="([^"\r\n]+)"', text, re.M)
    if target and target != [profile["target"]]:
        raise ValueError("existing target differs from the board profile; no files changed")
    if not target:
        if text and not text.endswith("\n"):
            text += newline
        text += 'CONFIG_IDF_TARGET="' + profile["target"] + '"' + newline
    for key, value in profile["sdkconfig_values"].items():
        pattern = re.compile(r"^(?:CONFIG_" + re.escape(key) + r"=[^\r\n]*|# CONFIG_" +
                             re.escape(key) + r" is not set)(?=\r?$)", re.M)
        matches = list(pattern.finditer(text))
        if len(matches) > 1:
            raise ValueError("duplicate sdkconfig key: " + key)
        replacement = "CONFIG_" + key + "=" + value if value != "n" else "# CONFIG_" + key + " is not set"
        if matches:
            text = pattern.sub(lambda _: replacement, text)
        else:
            if text and not text.endswith("\n"):
                text += newline
            text += replacement + newline
    return text


def workspace_settings(existing, profile):
    result = dict(existing)
    args = existing.get("idf.cmakeCompilerArgs", DEFAULT_CMAKE_ARGS)
    if not isinstance(args, list) or not all(isinstance(item, str) for item in args):
        raise ValueError("idf.cmakeCompilerArgs must be a string array")
    # Own only this expectation argument; retain all unrelated compiler arguments.
    result["idf.cmakeCompilerArgs"] = [a for a in args if not re.match(
        r"^-DNN2_EXPECT_LED_(?:POLICY|I2C_HZ)(?::[^=]+)?=", a)] + ["-DNN2_EXPECT_LED_POLICY=" + profile["policy"],
        "-DNN2_EXPECT_LED_I2C_HZ=" + str(profile["runtime_i2c_hz"])]
    result["idf.buildPath"] = "${workspaceFolder}/" + profile["build_path"]
    result["idf.sdkconfigFilePath"] = "${workspaceFolder}/" + profile["sdkconfig"]
    result["idf.sdkconfigDefaults"] = profile["defaults"]
    return result


def select_profile(root, name):
    profile = load_profile(root, name)
    config = root / profile["sdkconfig"]
    settings = root / ".vscode/settings.json"
    old_config = config.read_bytes() if config.exists() else b""
    old_settings = settings.read_bytes() if settings.exists() else b"{}"
    # Validate both inputs before writing either one. JSONC is rejected, not stripped.
    new_config = patch_sdkconfig(old_config.decode("utf-8"), profile).encode("utf-8")
    parsed = json.loads(old_settings.decode("utf-8"))
    new_settings = workspace_settings(parsed, profile)
    settings_changed = parsed != new_settings
    backup = None
    if new_config != old_config or settings_changed:
        parent = root / "build/profile-backups"
        parent.mkdir(parents=True, exist_ok=True)
        backup = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-"), dir=parent))
        (backup / "sdkconfig.before").write_bytes(old_config)
        (backup / "vscode-settings.before.json").write_bytes(old_settings)
        if new_config != old_config:
            config.write_bytes(new_config)
        if settings_changed:
            settings.parent.mkdir(parents=True, exist_ok=True)
            settings.write_text(json.dumps(new_settings, indent=4) + "\n", encoding="utf-8", newline="\n")
    return {"profile": name, "policy": profile["policy"], "limit_s": profile["limit_s"],
            "fs_min": profile["fs_min"], "fs_max": profile["fs_max"],
            "runtime_i2c_hz": profile["runtime_i2c_hz"],
            "single_run": profile["single_run"], "duration_control": profile["duration_control"], "sdkconfig": str(config),
            "build_path": profile["build_path"], "backup": str(backup) if backup else None,
            "sdkconfig_sha256": hashlib.sha256(new_config).hexdigest(),
            "firmware_build": "NOT RUN", "next": "Owner build required"}


def check_generated(root, name, build_dir=None):
    profile = load_profile(root, name)
    build = Path(build_dir) if build_dir else root / profile["build_path"]
    generated = json.loads((build / "config/sdkconfig.json").read_text(encoding="utf-8"))
    actual = "BENCH_WARN" if generated.get("NN2_LED_SHORT_BENCH") else "STRICT"
    if (actual != profile["policy"] or not generated.get("NN2_LED_BRINGUP") or
            not generated.get("NN2_PROFILE_COMMUNICATION")):
        raise ValueError("generated policy is " + actual + "; expected " + profile["policy"] +
                         ". Existing artifacts are stale or use another profile. Owner build required.")
    clock = generated.get("NN2_LED_RUNTIME_I2C_HZ")
    if clock != profile["runtime_i2c_hz"] or not generated.get("I2C_ISR_IRAM_SAFE"):
        raise ValueError("generated async clock/IRAM configuration differs from selected profile. Owner build required.")
    return {"profile": name, "generated_policy": actual, "runtime_i2c_hz": clock, "build_path": str(build),
            "device_identity": "UNVERIFIED: compare existing INFO/USB STATUS, never infer from local files"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["select", "check"])
    parser.add_argument("profile", choices=["main-board-short-advisory", "main-board-strict"])
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        function = select_profile if args.action == "select" else check_generated
        print(json.dumps(function(args.root.resolve(), args.profile), indent=2))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print("ERROR: " + str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
