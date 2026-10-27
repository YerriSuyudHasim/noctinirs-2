# PCA9956B fault 267 investigation: V2/V3 baseline

Date: 2026-10-27. Status: local diagnosis/implementation candidate; hardware cause
and physical LED operation remain unverified. Owner scope: use the previously
tested V2 and V3 as references; do not use untested V4 as the implementation
baseline. This supersedes the historical V4 comparison for this investigation.

The Owner reports a permanently assembled single board, previously working LED
operation with older firmware, and no observed light in the new test. Do not
assume a modular wiring issue. The observation method is not yet established.

Reported run, boot8: START accepted, then FAULTED267; USB led_edges39,
led_frames0, led_missed0, led_max_us1064, led_error264, led_mode2=45,
led_bus_free1 and off_requested1. These are Owner-reported bench data, not an
agent-operated hardware measurement. Source/image identity of that run is not
independently verified.

## Interpretation and V2/V3 comparison

267=0x010B is the application optical-adapter fault wrapper. 264=0x108 is
ESP_ERR_INVALID_RESPONSE, also used by the current adapter for a health failure;
it is not proof of an I2C ACK/transport failure. led_mode2 is printed in hex:
45 means 0x45, not decimal45. Bits2:0=101 are normal read-only reserved values.
Bit6 ERROR is set; bit7 OVERTEMP is clear. The code counts only39 edges because
the last OFF is followed by the MODE2 check, which fails before edge40/frame1
is counted. This proves software progress, not emitted optical energy.

Sources read:
- reference/v2/components/nocti_led_driver/nocti_led_driver.c and its header.
- reference/v3/components/nocti_v3_led/nocti_v3_led.c, board and config headers.
- Local Datasheet/PCA9956B.pdf Rev1.2 and [NXP sections7.3.2/7.3.3/7.3.14](https://www.nxp.com/docs/en/data-sheet/PCA9956B.pdf).

V2 explicitly separates the MODE2 writable configuration mask0x28 from live
status and normal reserved bits0x05. V3 uses the same0x28 initialization mask.
The new driver's0xE8 mask intentionally includes ERROR/OVERTEMP in health policy;
this extra runtime gate explains the software stop. It must not be described as
proof that the newer I2C command encoding is wrong or the assembled board is bad.

The ON path matches V3 intent: MODE1=0x40, MODE2 writable config0, IREF per
channel, and a seven-byte one-hot LEDOUT burst with control byte0x82. For slot0
at30mA/Rext1k, IREF0=0x85 and ON bytes82 01 00 00 00 00 00. FULL_ON01 bypasses
PWM dimming, so PWMALL0 alone does not explain missing light. Unused channels
10..13 keep IREF/LEDOUT zero. All20 connected channels are mapped identically.

V3 sends a complete seven-byte zero LEDOUT burst for OFF as well. The current
short OFF command clears the sole active LEDOUT byte; it is mathematically
consistent with its one-hot cache, but this investigation restores the full
V3-style OFF image to remove that difference from the tested reference path.
This does not, by itself, establish the reason for the observed MODE2 ERROR.

## Scoped corrective work and acceptance

Accountable activity: local firmware implementation. Hardware/RTOS and nonauthor
review remain required. No flash, energized test, GitHub write or merge by agent.
No reference snapshot edits, new dependencies, current/Fs/clock increases,
protection bypass, automatic fault clear or claim of a fixed light output.

1. Use a full six-register zero image for normal OFF, matching V3 transaction
   intent and preserving OCH=0 STOP-commit semantics.
2. Preserve per-channel EFLAG samples while that output is ON, before reset can
   destroy them. One single-register read per nonzero ON slot, no burst across
   EFLAG addresses (they are outside the auto-increment range). Merge only the
   active channel's two bits and expose a valid-channel mask: six register reads
   at different slots are not one atomic24-channel snapshot. New START clears
   the cache; cleanup and fault reset retain it. No additional diagnostic read
   of the device after reset is presented as pre-fault evidence.
3. Keep the existing ERROR/OVERTEMP/configuration/I2C/deadline fault response.
   Additional work must fit the same phase deadline; a missed phase still resets
   the chip. No deadline relaxation or policy suppression is introduced.
4. Expose raw EFLAG data and decoded OPEN/SHORT/reserved-code masks in cached
   USB STATUS. Cached snapshots alone do not establish a physical short/open.

The NXP short detector compares sink voltage against about2.85V. Thus a SHORT
indication must be assessed against the LED supply and forward voltage, not
automatically equated with damaged wiring. With a5.3V supply, low-forward-voltage
IR emitters may put the sink above that threshold; this is an inference, not a
diagnosis of this run. Actual EFLAG and voltage evidence are required before
changing the fault policy. Existing V2/V3 successful operation is useful evidence
but does not establish the meaning of these new health checks.

Acceptance: host model reproduces the0x45/39-edge stop; normal0x05 does not fault;
full OFF images zero all24 outputs; raw/decoded active-channel errors survive
reset; a read failure or overrun still resets/faults; zero-current and unused
channels are not mislabeled as observed. Tests/evidence are recorded separately.

Additional reads increase owner work at100kHz. Fs5/maximum10/current30 remain
unchanged; on-target deadline/stack margin is NOT established by host tests.
Keep emitters off pending investigation. If physical measurements are necessary,
use a specifically approved procedure; do not repeatedly clear/restart to bypass
the fault. Rollback is restoring the matched prior source/config/client; neither
rollback nor source editing establishes a physical off state.

## Local verification record

Completed2026-10-27 about09:41 UTC (18:41 KST). Base commit
d53a0b345575f12d94e290628269bff006dc07e0, feat/33-board-bringup plus local edits.
[Affected input hashes](evidence/pca-eflag-2026-10-27.json):
94e5fa8d0c8c55abde7378b6a0cb84c484ee5cd3fb91f1f4cedfeee32e2cae7f.
This covers nine affected source/test files, not the whole firmware identity.
Existing evidence files continue to describe their original revisions.

- Host/unit: CMake test/led -> build/led-eflag-host, MSVC19.50.35721.0 x64
  C11 /W4 /WX:5169 checks,0 failures. Fake peripherals only; no physical port.
  The normal MODE2 model now preserves actual read-only0x05 bits. It reproduces
  the Owner's0x45,39-edge,0-frame stop while retaining injected ch0 SHORT and
  ch23 OPEN across reset. All20 sampled channels and four unused ones are
  distinguished. Tests also cover zero-current skip, reserved error code3,
  failed EFLAG read, full V3 OFF images and existing deadline/cancel/failure paths.
- Host/unit: Python3.13.5, test_usb_status.py:8 PASS; test_led_cli.py:6 PASS.
  Cached masks decode to hardware channel indices, unobserved masks are rejected,
  raw STATUS stays available and generic command errors retain fault267 context.
- Static target frontend: exact current generated compile commands, ESP-IDF6.1
  commit fff9895c82d744c7237be8847347bdd1b07c6643, Xtensa GCC15.2.0,
  -fsyntax-only -Werror: nn2_board_pca.c, nn2_led_bringup.c and app_main.c PASS.
  Commands/results are in ignored build/pca-eflag-syntax.json. No image was emitted.
- Class-S include/dependency gate PASS; platform boundary tests5 PASS;
  git diff --check PASS with CRLF normalization warnings; reference integrity
  before/after PASS319 files. Conservatively formatted maximum STATUS825 bytes
  fits the existing1024-byte buffer and reader limit.
- NOT RUN: full target build/link/size for this changed candidate; physical I2C,
  voltage/current/light/phase/stack measurements; flash by agent; fresh native
  UDP loopback and power suites (their implementations were not changed here).
  Hardware/RTOS/independent reviews remain pending; no new timing guarantee.

No commit, push, merge, protection change or energized hardware operation was
performed. The failure's software trigger is explained; its electrical/optical
cause is not established. A repeat requires a specifically approved diagnostic
bench procedure and preserved pre-clear evidence, not unconditional fault retries.
