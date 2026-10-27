# Owner boot 13: STRICT image rejected the first SHORT frame

Acting as Agent 03 (Firmware Developer), model Codex/OpenAI, session
agent03-short-20261027-profile. Continuation of the recorded issue 33 assignment
and the Owner's configuration-first direction. Base HEAD:
`23eb04217463772887cd5b755d5a0b3920a679a5`, plus the configuration/host-tool/doc
changes in this working tree. PR packaging is deferred.

## Confirmed diagnosis

The normal local VS Code build used `build/`, root `sdkconfig`, and only
`sdkconfig.defaults` as its configured defaults. Before this correction, root
config and `build/config/sdkconfig.json` both enabled real LED bring-up and
selected STRICT (`NN2_LED_SHORT_BENCH` false). The generated header did not define
the advisory option. Merely having `sdkconfig.led-short-bench` in the repository
had not selected it. Existing sdkconfig values override defaults; cleaning the
build directory does not turn an existing unset root option into y.

The Owner's cached USB STATUS boot 13 confirms the exact flashed image:

| Identity | Owner's build / flashed image | Previously tested advisory candidate |
| --- | --- | --- |
| Source SHA | `23eb04217463772887cd5b755d5a0b3920a679a5` | Same source SHA |
| Build directory | `build/` | `build/short-idf/` |
| Generated sdkconfig | Root `sdkconfig` | `build/short-idf/sdkconfig` |
| Defaults | `sdkconfig.defaults` | Defaults + communication + LED bring-up + SHORT overlay |
| Policy | STRICT | BENCH_WARN |
| ELF SHA256 (USB `image`) | `c39444a276e6b937b2e9e22abdccc2b148cb7e774540835523eab0280025a421` | `b80bab81c1cb13ca82f348121ac4a2e2fba6e52a838f5aa27e1f17bfb19f11b9` |
| Application binary SHA256 | `8009b32f1c88420836ad8c12c30d61b64a278c052ba6849e280ba551e725408d` | `84ae3f0587e1845e224bbb437700a80a24837b446bb989285f5f768b368b13ba` |

This comparison uses existing files, generated metadata, the existing binary's
app descriptor and Owner-supplied cached status. No compiler, linker, map/size
check, device access or build command was run by the agent in this diagnosis.
The advisory hashes are preserved results from the preceding implementation,
not new build evidence. The same source SHA alone does not identify a profile
or binary. The previous isolated advisory build is not what the Owner flashed.

The decisive cached fields are:

```text
boot=13 fault=267 short_policy=STRICT limit_s=0 single_run=0
led_frames=0 led_edges=39 led_missed=0 led_error=264 led_stop=5 led_cap=0
led_mode2=45 led_eflags=555505505555 led_ef_valid=FFC3FF
led_short=FFC3FF led_open=000000 led_ef_invalid=000000
led_epoch=1 led_complete=0 led_fault_epoch=1 led_warn=0
led_dispatch_us=72 led_completion_us=2649 led_stack=4648 led_bus_free=1
```

MODE2 is hexadecimal: 0x45 has ERROR set, OVERTEMP clear and expected writable
configuration bits. All twenty connected channels were sampled in frame 1 and
reported SHORT; there were no OPEN or invalid codes. With STRICT, the existing
health classifier deliberately rejects ERROR. Error 264 is ESP_ERR_INVALID_RESPONSE
from diagnostic rejection; 267 is its application optical fault wrapper. It is
not evidence of a failed I2C ACK. The last OFF precedes health classification,
so edge40/frame 1 is not counted when health fails. No deadline miss was reported.
This is the exact signature for which the optional advisory software was prepared.
It does not establish whether the SHORT indication is a real electrical short.

The START and fault replies were 530,196 us apart. A nominal Fs5 frame is 200 ms;
the next heartbeat observes the already-latched failure. This one-frame rejection
is separate from the advisory timer ceiling. The flashed STRICT image had no
advisory cap enabled, and `led_cap=0` confirms it did not hit that cap. No new RUN
is needed for diagnosis. A separate INFO output is no longer needed: USB STATUS
already establishes policy, source and image. If any other image instead reports
BENCH_WARN, retain its own MODE2/EFLAG/epoch/first-stop data and investigate that
rejection; do not apply the boot 13 STRICT diagnosis to a different image.

## Concrete configuration correction

The intended local workflow now explicitly selects `main-board-short-advisory`
from [board-profiles.json](../config/board-profiles.json), using
[the selector](../tools/nn2_board_profile.py). Selection never builds or accesses a
device. In the Owner's existing root sdkconfig, it changed only:

```diff
-# CONFIG_NN2_LED_SHORT_BENCH is not set
+CONFIG_NN2_LED_SHORT_BENCH=y
```

Every unrelated root byte is preserved. Before/after hashes and the backup path
are in [the evidence record](evidence/owner-strict-profile-2026-10-27.json).
The existing binary/header/generated config remain STRICT until the Owner builds;
selection does not relabel or overwrite the old image.

VS Code now explicitly uses `build/`, root sdkconfig, the four advisory defaults,
and `-DNN2_EXPECT_LED_POLICY=BENCH_WARN`. Existing tool paths, serial-port setting,
OpenOCD config and custom variables are retained. The installed extension
normalized an equivalent separator in clangd's existing build-directory path.
No flash command or combined build/flash task is added.

The CMake policy assertion compares the selected policy with generated Kconfig.
A stale STRICT result fails configuration with an actionable mismatch error
instead of silently building an unexpected policy. Defaults alone are not treated
as proof of selected policy. Generic Kconfig remains strict by default; the
workspace's advisory selection is explicit. `main-board-strict` remains available
with an explicit unset overlay and its own STRICT expectation for regressions.

The CLI now prints its existing INFO reply once before START. This adds no
network request and no recurring streaming log. A future run log will show which
policy was actually reported before any START request.

## Owner build required

**Owner build required,** using the installed ESP-IDF 6.1 terminal from this
repository root. These commands are instructions for the Owner, not commands
executed by the agent:

```powershell
python tools/nn2_board_profile.py select main-board-short-advisory
idf.py -B build -D SDKCONFIG=sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-short-bench' -D NN2_EXPECT_LED_POLICY=BENCH_WARN -D IDF_TARGET=esp32s3 build
python tools/nn2_board_profile.py check main-board-short-advisory
```

Alternatively, after selection, use Command Palette -> **ESP-IDF: Build your
project** in VS Code. It uses the same explicitly configured root/build/defaults
and expectation. No full clean is required by this correction. Do not select a
combined build/flash command as part of these instructions.

Expected build message:

```text
NN2 board policy=BENCH_WARN; Fs2..10Hz; 30mA; duration=HOST; limit_s=0; single_run=0; physical qualification pending
```

Expected generated settings: `CONFIG_NN2_LED_BRINGUP=y` and
`CONFIG_NN2_LED_SHORT_BENCH=y`. The post-build check must report
`generated_policy=BENCH_WARN`. A subsequent separately authorized device image
should identify BENCH_WARN, duration=HOST, fs_min=2, fs_max=10, limit_s=0, single_run=0 and its new ELF identity;
its exact hash cannot be predicted before the Owner build. These are build-only
instructions, not flash or energization approval.

For an explicit strict regression selection, use:

```powershell
python tools/nn2_board_profile.py select main-board-strict
```

Its VS Code expectation and final overlay become STRICT. Selecting it changes
the intended next build; it is not the currently selected workspace profile.
The previous native strict/advisory tests are retained unchanged. Native LED
binaries were not rebuilt or linked in this task.

## Superseded fifteen-second duration proposal (historical)

The requested Fs5/15s run is unsupported by the current advisory candidate.
Fixing profile selection does not fix that separate duration mismatch. Its
firmware ceiling remains 2,000,000 us and one accepted START per boot; clear-fault,
STOP or heartbeats cannot renew it. The CLI refuses an acknowledged advisory 15 s
request before START, and still requires explicit SHORT acknowledgement.

Proposed scope for Agent 01/Nori to record as a separate duration-policy revision:

- Add a bounded engineering-duration configuration, default 2 s, with a separately
  named 15 s board profile selecting 15 s. Retain the 2 s profile and generic strict
  default. Do not make host-requested duration or heartbeat control the cap.
- Derive ISR/worker expiry, INFO/STATUS `limit_s` and CLI validation from the same
  selected ceiling. Preserve absolute timer expiry, equality priority, earlier
  alarm scheduling, boot-lifetime single attempt and generation cancellation.
- Keep Fs5, nominal 30 mA, one-hot/full OFF, unused outputs zero, fresh frame
  classification, ERROR consistency, OVERTEMP, OPEN/code3, I2C/readback, deadlines,
  STOP/safe-off and lease timeout protections.
- Test both ceilings, full multi-frame progression, exact/late expiry, each phase
  boundary, cancellation, replay and unchanged warning silence. Owner performs
  any required firmware build manually; agent checks remain lightweight unless
  the Owner changes that constraint.

No 15 s option/profile or duration change has been implemented or selected here.
A 15 s engineering bound would not qualify physical exposure. The matching-real-short
risk and any electrical/thermal/optical duration qualification remain separate
specialist/Owner gates before authorized energized use. Additional measurements
are not a prerequisite for finishing this software/configuration diagnosis.

## Validation and handoff

Firmware builds, clean/rebuild, target compilation/link, size/map checks, flash
and agent device access: **NOT RUN** for this task. Only existing artifact
identity was read. Runtime PCA/ISR source and the 2 s cap are unchanged.

Lightweight checks: 40 communication/Python cases, 33 PASS and 7 optional C loopback
skips; these include 8 profile-selection tests and 8 CLI tests. CMake policy-guard
fixtures use `cmake -P` only, with no project/compiler/linker. Both strict/advisory
expectations, stale generated config, preservation/idempotence, explicit strict
switch,15s refusal and one INFO preflight are covered. Static dependency gate,
platform-boundary 5 tests, manifest 15 tests, whitespace and 319-file reference
integrity PASS. These checks do not qualify hardware or replace non-author review.

Remaining action: Owner builds the explicitly selected profile manually. The
current host-duration/rate revision is described in LED_HOST_CONTROL.md.
Hardware/RTOS delta review, non-author verification and eligible independent
review remain open; PR/CI packaging remains deferred behind the scan diagnosis.

## Subsequent async transport revision

The Owner's boot16..20 image already selected BENCH_WARN and HOST duration.
The new [V4-inspired ESP-IDF6.1 runtime](LED_ASYNC_RUNTIME.md) changes transport:
advisory800kHz async, strict400kHz async, synchronous100kHz boot/preparation.
The selector now owns these clock/IRAM keys and matching CMake expectations too.
Historical image hashes above are not hashes of this new unbuilt candidate.
