"""Check the actual ESP32-S3 ELF's sample-clock runtime placement (Class B)."""
import argparse
from pathlib import Path


REQUIRED = (
    "ads_task", "async_led_task", "scheduler_task",
    "nocti_v4_async_led_on_burst_scheduled",
    "nocti_v4_async_led_off_single_scheduled",
    "nocti_v4_async_led_get_state", "nocti_v4_sample_edge_observed",
    "i2c_master_transmit", "i2c_master_multi_buffer_transmit",
    "gptimer_get_raw_count",
)


def validate_sections(sections):
    for name in REQUIRED:
        matches = [section for symbol, section in sections.items()
                   if symbol == name or symbol.startswith(name + ".")]
        if not matches:
            raise ValueError("missing runtime symbol: " + name)
        if any(section != ".iram0.text" for section in matches):
            raise ValueError("runtime symbol depends on flash: " + name)


def inspect_elf(path):
    # ESP-IDF's existing Python environment supplies pyelftools.
    from elftools.elf.elffile import ELFFile
    with path.open("rb") as source:
        elf = ELFFile(source)
        symbols = elf.get_section_by_name(".symtab")
        if symbols is None:
            raise ValueError("ELF has no symbol table")
        sections = {}
        for symbol in symbols.iter_symbols():
            index = symbol["st_shndx"]
            if isinstance(index, int) and symbol["st_info"]["type"] == "STT_FUNC":
                sections[symbol.name] = elf.get_section(index).name
        validate_sections(sections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    try:
        inspect_elf(args.elf)
    except (ValueError, OSError, ImportError) as error:
        parser.exit(1, "FAIL: " + str(error) + "\n")
    print("PASS: all required sample-clock tasks, handoffs and I2C submit symbols are in IRAM.")
    print("BUILD ONLY: placement is not an on-target latency measurement.")


if __name__ == "__main__":
    main()
