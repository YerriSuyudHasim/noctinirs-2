"""Source/provenance checks only. Does not invoke CMake, compiler or hardware."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "firmware/v4-idf61"
HISTORICAL = ROOT / "reference/v4"

def require(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    manifest = json.loads((PROJECT / "migration-manifest.json").read_text())
    for record in manifest["files"]:
        relative = Path(record["path"])
        require(not relative.is_absolute() and ".." not in relative.parts, "unsafe manifest path")
        require(sha(HISTORICAL / relative) == record["historical_sha256"], "historical hash: " + str(relative))
        require(sha(PROJECT / relative) == record["active_sha256"], "migration hash: " + str(relative))
    for record in manifest.get("generated_files", []):
        require(sha(PROJECT / record["path"]) == record["sha256"], "generated bridge hash")
    for record in manifest.get("shared_communication", []):
        require(sha(ROOT / record["path"]) == record["sha256"], "shared communication hash: " + record["path"])
    top = (PROJECT / "CMakeLists.txt").read_text()
    for name in ("nn2_core", "nn2_ports", "nn2_app", "nn2_wifi", "nn2_net", "nn2_config"):
        require("../../components/" + name in top, "missing shared component: " + name)
    require("../../components/nn2_provision" not in top, "old USB parser must not be imported")
    components = {p.name for p in (PROJECT / "components").iterdir() if p.is_dir()}
    require(len(components) == 11, "expected all eleven V4 components")
    for cmake in PROJECT.rglob("CMakeLists.txt"):
        source = cmake.read_text()
        for component in set(re.findall(r"\bnocti_v4_[a-z_]+\b", source)):
            if component.endswith(".c"):
                continue
            # Component C source stems coincide with component names.
            require(component in components, "unresolved component: " + component)
        for filename in re.findall(r'"([^"\n]+\.c)"', source):
            require((cmake.parent / filename).is_file(), "missing C source: " + filename)
    board = (PROJECT / "components/nocti_v4_board/include/nocti_v4_board.h").read_text()
    active_board = (ROOT / "components/nn2_board/include/nn2_board_wiring.h").read_text()
    for historical, active in (("I2C_SDA", "SDA"), ("I2C_SCL", "SCL"), ("PCA_RESET", "RESET_LED")):
        old = re.search(r"#define NOCTI_V4_" + historical + r"_GPIO\s+GPIO_NUM_(\d+)", board)
        current = re.search(r"#define NN2_BOARD_" + active + r"_GPIO\s+(\d+)", active_board)
        require(old and current and old[1] == current[1], "active pin mismatch: " + historical)
    require(re.search(r"SYNC_DIAGNOSTIC_HZ\s+100000u", board), "100 kHz configuration bus")
    require(re.search(r"SCAN_RUNTIME_HZ\s+800000u", board), "800 kHz runtime bus")
    require(re.search(r"ASYNC_TRANS_QUEUE_DEPTH\s+4u", board), "V4 async queue depth")
    ads = (PROJECT / "components/nocti_v4_common/include/nocti_v4_ads_config.h").read_text()
    require(re.search(r"ADS_SPI_HOST\s+SPI2_HOST", ads), "approved SPI2 controller")
    net_header = (PROJECT / "components/nocti_v4_network/include/nocti_v4_network.h").read_text()
    require(not re.search(r'^#define NOCTI_V4_WIFI_AP_PASSWORD\s+"[^"]+"', net_header, re.M), "embedded AP credential")
    network = (PROJECT / "components/nocti_v4_network/nocti_v4_network.c").read_text()
    require("nvs_flash_erase(" not in network, "automatic NVS erasure")
    defaults = (PROJECT / "sdkconfig.defaults").read_text()
    for setting in ("CONFIG_FREERTOS_HZ=1000", "CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y",
                    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y", "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240",
                    "CONFIG_ESP_BROWNOUT_DET=y", "CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7=y"):
        require(setting in defaults, "missing setting: " + setting)
    for other in ("SEL_1=y", "SEL_2=y", "SEL_3=y", "SEL_4=y", "SEL_5=y", "SEL_6=y"):
        require("CONFIG_ESP_BROWNOUT_DET_LVL_" + other not in defaults,
                "brownout level is not the IDF ESP32-S3 default")
    require((PROJECT / "partitions.csv").read_bytes() ==
            (ROOT / "config/partitions/acquisition-16mb.csv").read_bytes(), "partition overlay mismatch")
    optical = (PROJECT / "components/nocti_v4_optical/nocti_v4_optical.c").read_text()
    require(re.search(r"RAW_STAGE_CAPACITY\s+256u", optical), "Fs2 raw staging capacity")
    require(re.search(r"OPTICAL_BATCH_SAMPLES\s+32u", optical), "V4 ingest batch changed; recompute budget")
    for fs in (2, 4, 5, 8, 10):
        slot = 1000000 // (20 * fs)
        require(slot * 20 * fs == 1000000 and slot % 125 == 0, "nonintegral Fs grid")
        require(slot // 125 + 32 + 1 <= 256, "staging slot plus batch budget")
        for off in (slot * 44 // 100, slot // 2):
            require(500 + 10 * 125 <= off and 500 + 10 * 125 + 100 <= slot - off,
                    "V4 sample-window budget: Fs=" + str(fs))
    print("PASS: V4/shared control hashes, 11-component V4 graph, board/config overlays and Fs geometry.")
    print("STATIC ONLY: no compiler/linker, RTOS execution, waveform or board validation.")

if __name__ == "__main__":
    main()
