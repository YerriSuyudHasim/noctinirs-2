# Board-v1 firmware package guide — FOR REVIEW

Status: author proposal for Firmware/Verifier and Hardware static review. Not a
release or an approved physical procedure. Flashing, reset, power-on, serial
probing, RF, optical operation and all timing/electrical observations: **NOT RUN**.
This document supplies an offline package and a future procedure to review.

## Select and verify offline

Use Python 3.12 or newer, standard library only. No SDK installation is required.
Keep the package ZIP SHA256 from DOWNLOAD_INDEX with the original CI run, attempt,
event, artifact ID and expiry. GitHub private artifacts require authorized login;
they are neither public nor permanent. The package and its build-evidence ZIP are
different artifacts. Compare the ZIP digest before running its included validator.
Checksums are integrity evidence, not signatures or proof of origin. If an attacker
replaces the package and the externally trusted digest, offline validation cannot
establish authenticity.

Extract to a new directory, including paths with spaces. From its parent run:

```text
python "<package>/validate_package.py" validate "<package>" --expect-sha <firmware_source_sha> --expect-profile <fake_or_diagnostic> --expect-config <sdkconfig_sha256> --expect-tool <packaging_tool_sha>
```

The validator accepts the ZIP itself as the package argument too. It reads metadata
and ordinary files only and returns 0 for PASS, 1 for invalid input. It never
connects to a device, runs metadata as commands, installs software, resets, erases
or flashes. The generator uses git only to retain source/defaults identity.
SHA256SUMS covers images, original metadata, defaults, tool, manifest and this guide,
excluding SHA256SUMS itself. The ZIP digest is external to avoid a circular hash.
Original generated argument files remain byte-for-byte; relocated `images/` paths
are separately mapped in manifest.json. `IDF_PATH` is an environment locator,
not a payload reference. Required bootloader/partition/application roles and every
additional flash_files entry are checked again independently of the manifest list.

FAKE / LED DISABLED is synthetic `src=FAKE raw=0`; it is not detector data. Its
configured flash is **2MB**, even on the named N16R8 module, with primary UART0
115200 and secondary USB Serial/JTAG selected by the retained generated config.
Do not promise USB diagnostics as its default console. REAL ADC DIAGNOSTIC /
EMITTER INACTIVE uses **16MB**, primary USB Serial/JTAG, no UART console. It is raw
acquisition diagnostics, with physical acquisition untested. Do not interchange
these manifests/images/settings. Both use generated DIO/80m. PSRAM, secure boot and
flash encryption are disabled in these builds; that does not establish the
physical board/eFuse state or disconnect the bonded PSRAM die. GPIO26 is untouched.

## Source basis and unresolved board routing

Authoritative retained sources at the firmware SHA are:
`Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json`,
`SCH_PD_board_2026-10-16.json`, the corresponding BOMs, `main/nn2_board_v1.*`,
`docs/ISSUE35_DIAGNOSTIC_CONTRACT.md`, `docs/ISSUE35_DIAGNOSTIC_TEST_GUIDE.md`,
and the generated sdkconfig in this package. The schematic U1 names IO0, EN,
IO19/module pin13, IO20/pin14, RXD0/pin36 and TXD0/pin37. ESP32-S3 default UART0
uses GPIO43 TX / GPIO44 RX; USB uses GPIO19 D- / GPIO20 D+. These silicon mappings
are not proof that an accessible board connector/cable is wired correctly.
The schematic contains switches; their physical accessibility, markings, contact
mapping and the actual BOOT/reset sequence require Hardware confirmation. Do not
assume a generic devkit BOOT/EN pair, USB-UART bridge, DTR/RTS auto-reset or a working
UART cable. USB console baud is not a UART signaling baud. Generated flash metadata
requests `default-reset` before / `hard-reset` after; this records tool intent only.

Existing Hardware correction: Issue35 comment6227859080 (plus the later diagnostic
guide) distinguishes FPC contact orientation, 16-pin BOM vs old illustrative
18-position reversal, R50 case-sensitive net-label ambiguity and SYS_Out-derived PD
rail/back-feed. Preserve the actual contact numbering; do not copy reversal math.
PCA OE# grounded and RESET18 pulled up mean absence of emitter code does **not**
prove physical emitter-off. No SafeOff/PCA/IO18 implementation is delivered here.

## Proposed installation checklist — pending Hardware review and Owner authority

1. Record the exact board revision, module marking, fixture, cable part number,
   operator, firmware/tool/config/ZIP hashes and the reviewed procedure revision.
   Obtain bounded non-author package review and separate exact-fixture Owner grant.
2. Hardware must specify unpowered contact continuity for every power/ground,
   USB/UART, IO0/EN and FPC contact, plus actual supply values/current limits,
   ramp/order, acceptable back-feed, instrumentation, and stop thresholds. Resolve
   the FPC BOM/contact discrepancy and bonded-PSRAM H-ADS-PSRAM residual. These
   values and measurements are pending, not invented by this package.
3. Hardware must identify and verify removal/isolation of **every** energy source
   capable of emitter operation: battery, USB, SYS_Out, external rails and charged
   storage. A software profile, ADC reset or USB disconnect alone is insufficient.
   Record the measured de-energization criterion, wait/discharge interval and named
   emergency disconnect accessible to the operator before any powered procedure.
4. With explicit authority, Hardware verifies actual board security/boot state,
   compatible flash capacity, safe IO0 strap/EN handling and the programming port.
   Do not burn eFuses, change security, bypass protections, erase-all or touch GPIO26.
   Unexpected security or wiring state is a stop, requiring a revised reviewed plan.
5. Validate the package offline with expected identities. Review the generated
   address preview below against manifest and original metadata. The preview is
   text only; no executable flash wrapper is provided. An existing reviewed IDF6.1
   environment supplies esptool; do not install/upgrade tools implicitly.
6. A later authorized operator may execute only the reviewed command with the
   verified port and power/reset sequence. Capture full log, boot version/profile,
   image identifiers and console routing. Do not infer physical success from PASS
   validation. Stop on unexpected rails, current, heating, emitter energy, missing
   console or mismatched identity; use the reviewed emergency disconnect.
7. Cleanup must use the reviewed all-source isolation/discharge procedure, confirm
   rails below its stated threshold and retain raw logs. No automatic next test.

## EXPECTED transcripts, not observations

Fake: `src=FAKE raw=0` (synthetic). Diagnostic, placeholders:

```text
profile=ADS131_DIAGNOSTIC source=ADC time_kind=read_complete_us emitter_control=ABSENT fixture=REQUIRED source_sha=<full firmware SHA> config=MODE0110_CLOCK0F0A_GAIN0000 version=0.1.0 elf_sha256=<identifier>
diagnostic result=<status> state=<state> fault=<fault> session=<n> produced=<n> consumed=<n> lost=<n> overflow=<n> crc_bad=<n> spi_errors=<n> drdy_timeouts=<n> missed_min=<lower-bound> unknown_loss=<n> occupancy=<n> peak=<n> record_bytes=<n>
source=ADC sequence=<n> read_complete_us=<software time> status=<hex> quality=<flags> ch0=<signed> ch1=<signed> ch2=<signed> ch3=<signed>
```

No record can be claimed until actually observed. Strict service-gap detection may
fault at 8kSPS under ordinary scheduling. Timing, WCET/jitter, watermark/heap, TWDT,
ADC/SPI/DRDY, electrical, optical and clinical behavior remain NOT RUN.

## Proposed recovery and limits

For invalid offline packages, stop and reacquire the original identified artifact;
never repair offsets or checksums by guesswork. For a later interrupted install,
first apply the approved energy-removal procedure. Hardware must establish the
actual ROM-download entry/release sequence using confirmed IO0/EN contacts and
security state. Revalidate a complete known package, then reprogram only under a
new or still-applicable explicit authority. No recovery success has been measured.
A fake image is a software fallback, not a physical safety control. A poisoned SPI
transaction requires reboot; port timeout is not cancellation or quiescence.

Tooling rollback is a reviewed revert. There is no backup/OTA recovery guarantee,
no new partition table/security configuration, no signing/release/tag and no board
modification by this task. #19/P20-H2 and H-ADS-PSRAM remain open. Non-author
Firmware/Verifier package review and Hardware static guide review are the next gate.
