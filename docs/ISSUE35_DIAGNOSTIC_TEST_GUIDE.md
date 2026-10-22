# ADS131 diagnostic and recovery guide (draft; no physical run)

This is source/build guidance for a later reviewed fixture procedure. No board
has been flashed, connected or operated by this task. All physical observations
below are expected, not observed. No USB command parser or LED-on operation exists.

Build from the exact reviewed source SHA using the pinned IDF/container in CI.
Default `sdkconfig.defaults` selects fake ADC and disabled emitter. For the
diagnostic use clean build/sdkconfig and defaults
`sdkconfig.defaults;sdkconfig.ads131-diagnostic`. Keep generated sdkconfig,
compile_commands.json/full dependencies, maps, size reports, warning log and
generated `flasher_args.json`/flash argument files with every image. Source/profile
identities and SHA256 files are retained by the existing CI artifact system.
No manual flash offsets are prescribed here. P1 must assemble/validate the
relocatable package against the generated metadata; this candidate is not a release.

Before any later physical operation, Orchestrator needs a specific Owner grant
and reviewed Hardware procedure: named board/fixture, exact FPC connector contact
map (BOM16-pin versus earlier illustrative18-position reversal remains unresolved),
unpowered continuity, cable part number, supplies/back-feed limits, PSRAM residual
H-ADS-PSRAM checks, and emitter energy physically unavailable. #19 is not waived.
Do not treat ADC reset as emitter control. No eFuse action is specified.

Expected USB Serial/JTAG console, diagnostic profile (values are placeholders):

```text
profile=ADS131_DIAGNOSTIC source=ADC time_kind=read_complete_us emitter_control=ABSENT fixture=REQUIRED source_sha=<full SHA> config=MODE0110_CLOCK0F0A_GAIN0000 version=0.1.0 elf_sha256=<image identifier>
diagnostic result=OK state=0 fault=0 session=1 produced=<n> consumed=<n> lost=<n> overflow=<n> crc_bad=0 spi_errors=0 drdy_timeouts=0 missed_min=<lower bound> unknown_loss=<events> occupancy=<n> peak=<n> record_bytes=<sizeof>
source=ADC sequence=<observed sequence> read_complete_us=<software time> status=010f quality=<flags> ch0=<signed> ch1=<signed> ch2=<signed> ch3=<signed>
```

The source executes128 bounded consumer attempts, stops, reports a raw record if
any was obtained, then explicitly reinitializes for a second session only after
success. Fault ends the run with reset held, with no automatic recovery. No valid
record returns EMPTY, not a false successful detector-data claim. A30ms DRDY stall,
CRC/SPI failure or ambiguous/coalesced service gap faults the adapter. Strict gap
detection may stop8kSPS diagnostics under ordinary scheduling; feasibility is NOT RUN.
Counters distinguish known queue loss and unknown hardware/cache-off loss.

Default expected output still says `src=FAKE raw=0`; those are synthetic values.
No waveform, optical response, timing, stack margin or physical safety conclusion
can be drawn from host tests or either build.

Later recovery tests must separately cover ID/readback mismatch, disconnected or
stuck DRDY, integrity errors, consumer stall/overflow, stop during startup/SPI,
repeat start-stop and reinit. A timed-out SPI result poisons static transaction
storage until reboot; do not free/reuse it, and do not interpret command timeout
as proof that the device stopped. Reboot/BOOT-button installation instructions
need the actual board's reviewed power/USB/reset procedure from P1/Hardware.
Rollback is a separately reviewed revert or the fake profile; no installation
or rollback operation was performed here.
