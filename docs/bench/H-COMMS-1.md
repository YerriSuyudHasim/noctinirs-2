# H-COMMS-1 bench procedure: board v1, de-energized flash, emitters inactive

> **Status: DRAFT, not approved.** Executing this procedure is an **A3** action. It requires a separate, specific Owner approval recorded on [#53](https://github.com/YerriSuyudHasim/noctinirs-2/issues/53), and the Owner's direct confirmation. Neither this document nor its merge authorizes a flash, power-on, RF use or any energized test.
>
> Author: Agent 04 (Hardware Specialist). Reviews required: Agent 05 (RTOS/timing), Agent 06 (Verification, record format), Agent 07 (IR). The author does not self-review. Approval: Nori + Owner.
> Evidence class of this document: **Class S** (static: datasheets, netlist, repository records). **Class H: NOT RUN. Flash: NOT authorized.**

Tags: **[V]** verified against the cited source; **[P]** proposed, inferred, or relayed second-hand; **[U]** unknown, to be resolved before or during execution.

**Relayed Owner answers used in this revision (all [P], none [V]):**

- **VBUS/SW3 (PR4):** relayed by Agent 01 from its 1:1 with the Owner, 2026-10-22 15:17 KST; awaiting Owner's direct confirmation on #53: *"When SW3 is off, USB only feeds the BQ charger IC, and the BQ output does not reach the board system. So the buck-boost, the LDOs and the rails will be 0."* The D4 measurement (§7) verifies this on the bench.
- **Board ID:** Owner answer relayed by Nori (via Agent 01); awaiting Owner's direct confirmation. There is one unit, "Nocti board v1", it is the bench unit, and it has no separate serial number.
- **Buttons:** Owner answer relayed by Nori (via Agent 01); awaiting Owner's direct confirmation. The Owner thinks the labels are "BOOT" and "RESET" but is not sure. They are **UNCONFIRMED [U]** until PR10.

## 1. Purpose and scope

H-COMMS-1 is the Class H gate for milestone v0.1.0 (#53 scope 1-5; milestone acceptance item 5). On board v1, with the emitters kept inactive, it verifies:

1. The de-energized-board flash procedure from D6 (#33 [comment 6333972667](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333972667), rec. 4).
2. Emitter-safe boot from P7 (#49): IO18/`RESET_LED` is LOW from the hook onward, safe-off runs, and the reset reason is logged.
3. Communications: USB provisioning, STA reconnect, every ADR-0004 command via `nn2ctl` with SIMULATED flags, and grace expiry → `STOPPED` with `end_reason=LINK_LOSS`.
4. Instrumented optical emitter-dark check, plus IO18 scope captures and the ROM-to-hook latency (record-only).

**Excluded:** emitter-on tests (H-LED-1, v0.1.1), soak (v0.1.2), any human-subject or on-skin use, eFuse/security changes, erase-all, and touching GPIO26.

## 2. Prerequisites (all must be recorded before Step 1)

| # | Prerequisite | Status at this revision |
|---|---|---|
| PR1 | P7 [#49](https://github.com/YerriSuyudHasim/noctinirs-2/issues/49) merged to `main`; merge SHA recorded | **BLOCKED**: no PR open |
| PR2 | Firmware package from the #45/#46 flow identified by full source SHA, profile, package ZIP SHA-256, and per-image SHA-256 (bootloader, partition table, app), each with its artifact link. Offline validation PASS per the guide in §6. The package must contain PR1, PR8 and the USB provisioning / STA reconnect code (currently draft PR #34, not on `main`) | **BLOCKED** |
| PR3 | This procedure approved (Agent 05/06 review, Agent 07 IR on the exact head, then Nori + Owner) | **BLOCKED** |
| PR4 | Owner answer to "does USB VBUS feed `SYS_Out` with SW3 off?" recorded **by the Owner directly** on #53 | **OPEN**: answer "no" relayed by Agent 01 (1:1, 2026-10-22 15:17 KST) [P]; Owner's direct confirmation on #53 pending. D4 measures it regardless |
| PR5 | Owner A3 grant with every field in §12 (board, fixture, procedure revision, power configuration, RF use, time window, operator, stop conditions, H1-H3 re-confirmation) | **BLOCKED** |
| PR6 | Generated sdkconfig of the package shows the brownout detector enabled (P7 AC2) and SPIRAM off | **BLOCKED** (depends on PR1/PR2) |
| PR7 | [#50](https://github.com/YerriSuyudHasim/noctinirs-2/issues/50) merged: `nn2ctl` (and `nn2proto`) full SHA recorded, with its conformance result (Class U run URL and outcome) against ADR-0004 | **BLOCKED**: `nn2ctl` does not exist yet |
| PR8 | [#48](https://github.com/YerriSuyudHasim/noctinirs-2/issues/48) merged: loopback/protocol core (envelope, registry, SIMULATED rule, grace policy) with merge SHA and Class U result recorded, and included in the PR2 package | **BLOCKED**: not implemented |
| PR9 | Energized power configuration defined by the Owner/Hardware and recorded on #53: source, SW3/SW5 positions, battery fitted or not, supply voltage and current limit, USB-meter stop threshold (see §4.1). No value is invented here | **BLOCKED**: values [U] |
| PR10 | Button silkscreen check: before the run, the Owner checks the silkscreen labels of SW1/SW2 (BOOT / RESET-EN) or sends a photo; the result is recorded (photo link if any). If the labels do not match what this procedure says, **STOP, do not run** | **BLOCKED**: labels UNCONFIRMED [U] |
| PR11 | A revision of `docs/BOARD_V1_PACKAGE_GUIDE.md` (#46) reviewed by someone other than its author, with that revision's SHA recorded. At `f0b49bbe` the guide is still marked "FOR REVIEW" | **BLOCKED** |
| PR12 | Every instrument in §8 present, identified, and within calibration on the execution date; the optical instrument has passed its functional check (§8) | **BLOCKED** (execution-time) |

S8 and S9 are **BLOCKED** until PR7 and PR8 are met. They are never run with an ad-hoc client.

The board v1 profile is the default (fake ADC, emitter disabled). The diagnostic profile is out of scope unless an H-ADS-PSRAM pre-check has been defined and approved. That check is referenced in the #46 guide but not defined yet [U].

## 3. Board and fixture identity

| Field | Value |
|---|---|
| Board ID | **"Nocti board v1 (single unit)"** [P]: Owner answer relayed by Nori (via Agent 01); awaiting Owner's direct confirmation. Single unit, which is the bench unit |
| Board serial | None (no separate serial number) [P]: Owner answer relayed by Nori (via Agent 01); awaiting Owner's direct confirmation |
| Schematic | `Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json` @ `f2d7c8cfecdc6570df155fac4d90625c16a7bf4b` (blob `bce01e07f6a21397c146b09f4645c9ca46b4e308`) [V] |
| Board revision | The schematic `REV:` field is empty, so the revision is **unknown** [U]. Record any PCB silkscreen marking |
| ESP32-S3 MAC (from ROM/boot log) | _to record_ |
| Module marking | _to record_ (schematic U1: ESP32-S3-WROOM-1U-N16R8 [V schematic]; physical marking [U]) |
| Firmware currently on the board | **[U]**: unknown image. Treat as able to drive the emitters until D3/D4 and §4 show otherwise |
| Buttons SW1/SW2 (TS-1088R) | Labels believed "BOOT" and "RESET" — **UNCONFIRMED [U]** (relayed, see header). Which switch is IO0 and which is EN: [U]. PR10 resolves this |
| Emitter part fitted on LED1-LED10 pads | Schematic symbols are `PAD` with no part number, and the main BOM has no emitter line [V schematic, BOM]. Believed Marubeni SMT735D/850D [P] (ADR-0002 §1.3 board context, tagged [O]/[U] there; datasheet in repo). Record the actual part marking or photo |
| Fixture ID, emitter cover | _to record_ |
| Cable(s) part number | _to record_ |

## 4. Safe limits and safety conditions

Source of the safety rules (no skin, brownout, H1-H3): D6 analysis, [#33 comment 6333972667](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333972667). The Owner's acceptance relayed by Nori, [#33 comment 6333953903](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333953903), is cited for the record only.

- **No LEDs on skin, and no human subject**, during flash, first boot or any step. Fixture only [V D6, #33 comment 6333972667].
- **Emitters must remain dark** throughout. This firmware has no LED drive (milestone exclusion). Any detected emission is a stop.
- **Emitter wavelengths (V1):** board v1 has 10 sources × 2 wavelengths = 20 emitter channels, nets `L1_735`…`L10_735` and `L1_850`…`L10_850` [V schematic net labels @ `f2d7c8cf`]; ADR-0002 states 735 nm / 850 nm, `SLOT_COUNT = 20` [V ADR-0002 @ `f2d7c8cf`, blob `821ab735`]. For the believed part SMT735D/850D the datasheet gives peak 725-745 nm (735 typ., half width 21 nm) and 840-865 nm (850 typ., half width 27 nm) at IF = 50 mA [V `Datasheet/SMT735D_850D-1.pdf` @ `f2d7c8cf`, blob `cfad8ca0`]. That this part is fitted is [P] (see §3).
- **Every emitter channel is NIR or near-NIR, so a visual check is not valid evidence for any channel.** 850 nm is effectively invisible to the eye, and 735 nm is at the far-red edge of vision, where a lit LED can look dark [P, photometry]. The dark check is **instrumented** for all 20 channels (§8): a camera without an IR-cut filter, an IR viewer card covering 700-900 nm, or a photodiode / optical power meter whose stated band covers 725-865 nm. The method used is recorded per step (`optical_check` in §10).
- Indicator LEDs `LED_PWR` and `LED1_STAT` (E6C0603URAC1UDA, red indicators [V BOM]) are not emitters and are excluded from the dark check. Their wavelength is not documented in the BOM [U] and does not matter here. Record their observed state only.
- **Opaque emitter cover is mandatory** over LED1-LED10 for D5 and S3-S4, and is recommended for every other energized step. The optical instrument's sensor or view port sits inside the cover, so the dark check is made without removing it [P].
- Firmware current cap: ≤ 50 mA/ch, IREF ≤ 222 (milestone Class U criterion). In this procedure IREF must stay 00h [V milestone #2].
- Hardware maximum per channel: PCA9956B REXT = R4 1 kΩ → **57.4 mA/ch**, with up to 20 connected channels (PCA outputs LED0-9 and LED14-23) [V D6 §1, PCA9956B datasheet p24].
- Board v1 has **no hardware emitter-off**: OE# is tied to GND, R46 is a pull-up on RESET#, and there is no switch on `RAIL_5.3V_LED` [V D6 §1; #46 guide @ `f0b49bbe`]. The only safety barriers are power removal and firmware. Because the firmware already on the board is [U] (§3), the first energization (D5) is treated as possibly emitting until the instrumented check shows otherwise.
- Stable supply only. The ESP brownout detector must be enabled (PR6) [V D6, #33 comment 6333972667].

### 4.1 Energized power configuration (D5, S4 and later steps)

| Item | Value | Tag / source |
|---|---|---|
| Source | USB-C (`USBC1`) as the programming and power source, with SW3 switching the system on; `J_PSource` removed | [P]: follows the relayed VBUS/SW3 answer; final choice is PR9 |
| SW3 position | OFF for D1-D4; ON to energize at D5 and S4 | [P] (PR9) |
| SW5 position | **[U]**: function and required position not documented in the v1 records reviewed here | PR9 |
| Battery (CN1) | Fitted or removed: **[U]** | PR9 |
| Supply voltage | USB 5 V nominal from the host port; actual value **[U]** (to be measured on the USB meter) | PR9 |
| Supply current limit | **[U]**: not in the v1 schematic/BOM or the #46 guide (the guide says Hardware must define it) | PR9 |
| USB-meter current stop threshold | **[U]**: Owner/Hardware to define | PR9 |

None of these values exist in the v1 schematic, BOM or the #46 guide @ `f0b49bbe` (guide step 2 leaves them to Hardware). Until PR9 is recorded, no energized step may run.

## 5. Emergency stop and power removal

**E-STOP:** (1) unplug the USB cable at the host end; (2) switch SW3 off and remove the battery if fitted; (3) disconnect any bench supply on `J_PSource`. Keep all three within the operator's reach before Step 1. That (1)+(2) removes all energy rests on the relayed PR4 answer [P] and is verified by the D3/D4 measurements. Record the operator's named E-STOP method in the record.

## 6. Install and recovery guide reference (not copied)

Package selection, offline validation, flash command and recovery follow `docs/BOARD_V1_PACKAGE_GUIDE.md` at the non-author-reviewed revision required by PR11. The current #46 head is `f0b49bbe8392b517b2756e9dfcc15f9bce6d0249` (blob `bc8e3db2b49db8a097fb17d549712753f6b2749b`), marked "FOR REVIEW", so it does not satisfy PR11 yet. Record whichever SHA is used. This procedure adds only the power and safety sequence around that guide. Where they conflict, stop.

**Console capture:** for the default (fake) profile the primary console is **UART0 at 115200** (GPIO43 TX / GPIO44 RX), with USB Serial/JTAG secondary [V #46 guide @ `f0b49bbe`, generated config]. Capture UART0 at 115200 for every boot in this procedure. The board-side UART access point (schematic header J4 "Tx-Rx" [V schematic]) and its wiring to GPIO43/44 are [U]; confirm by continuity with power removed. A missing console is a stop.

## 7. D6 de-energize-before-download

Why it is needed: in ROM download mode the P7 hook never runs, and a warm reset does not reset the PCA9956B. Only a POR with VDD below 1 V for more than 20 µs guarantees IREF = PWM = 00h (D6 H2; PCA9956B datasheet §7.5, Table 26) [V].

| D-step | Action | Expected | Stop if |
|---|---|---|---|
| D1 | Unplug USB. Switch SW3 off and remove the battery if fitted. Remove `J_PSource` | No source connected | — |
| D2 | Wait **≥ 5 s** (D6 criterion: well over 20 µs below 1 V) [P margin] | — | — |
| D3 | Measure `Rail_3.3V`, `SYS_Out` and `RAIL_5.3V_LED` with the DMM; record all three values | Each **< 0.1 V** [P threshold] | Any ≥ 0.1 V |
| **D4 gated check** | Connect only the USB cable (host end last), keep **SW3 OFF**, battery and `J_PSource` as in D1. Measure `SYS_Out`, `Rail_3.3V` and `RAIL_5.3V_LED` with the DMM; record all three values | Each **< 0.1 V** [P threshold]. This verifies the relayed claim "USB VBUS does not feed `SYS_Out` with SW3 off" [P] | Any ≥ 0.1 V → **STOP**: the method is invalid; E-STOP and return the procedure to Hardware for a new revision |
| D5 | With the emitter cover on and the optical instrument armed: hold BOOT/IO0 (label per PR10), then switch SW3 ON per §4.1 | ROM download banner on the console; emitters dark (instrumented); USB-meter current below the PR9 threshold | **No ROM download banner → E-STOP immediately.** Any emission; current above threshold; unexpected banner |
| D6 | Flash per §6 | Guide-defined PASS | Guide stop conditions |

The BOOT/IO0 and SW1/EN contact markings are UNCONFIRMED [U]; PR10 resolves them before the run. If they don't match, stop.

## 8. Instruments

All instruments must be within calibration **on the execution date**; record model, serial and calibration due date. An instrument without a current calibration is a BLOCKED prerequisite (PR12).

| Instrument | Requirement | Model / serial | Calibration due date (must be after the execution date) |
|---|---|---|---|
| DMM | DC volts, resolution ≤ 10 mV (for the < 0.1 V checks) | | |
| Oscilloscope | ≥ 2 ch, ≥ 10 MS/s, for IO18/`RESET_LED` and `Rail_3.3V` | | |
| USB power meter | V and A, inline on the USB cable | | |
| **Optical emitter-dark instrument (mandatory)** | One of: camera with no IR-cut filter; IR viewer card covering 700-900 nm; photodiode or optical power meter with a stated band covering 725-865 nm. State the band. Before Step 1, run a functional check against a known NIR source in that band and record it | | |
| Console adapter | UART0 115200 capture (§6) | | |
| Host PC | Recorded in the `host` block (§10): OS, Python, `nn2ctl` SHA, `esptool` version, ESP-IDF version/commit | | |

## 9. Test steps

Stop rule for every step: on any detected emission, unexpected reset reason or reset loop, brownout, supply anomaly, current above the PR9 threshold, heat, burning smell, missing console, or readback `FAULTED`: **E-STOP (§5)**, record FAIL or BLOCKED with `stop_triggered` and `aborted: true`, and do not retry without a new review.

| Step | Action | Expected outcome | Additional stop condition |
|---|---|---|---|
| S0 | Prerequisites PR1-PR12 recorded | All met | Any unmet → BLOCKED |
| S1 | D1-D3 de-energize verification; record the three voltages | Each rail < 0.1 V | Any ≥ 0.1 V |
| S2 | D4 gated check; record the three voltages | Each rail < 0.1 V with USB connected and SW3 OFF | Any ≥ 0.1 V → method invalid, stop |
| S3 | D5-D6 flash, scope on IO18, cover on | Flash PASS; emitters dark by the instrumented check (method recorded) | No ROM download banner; emission detected |
| S4 | Cold first boot: repeat D1-D3, then apply power per §4.1 without BOOT, cover on, scope armed on IO18 + `Rail_3.3V` | IO18 LOW from the hook onward; reset reason POWERON logged; safe-off readback OK; emitters dark (instrumented). ROM-to-hook latency (power-good to IO18 LOW) is **recorded only: no PASS/FAIL until a limit is defined** | IO18 HIGH after the hook; `FAULTED`; emission |
| S5 | Instrumented emitter-dark check of all 20 channels | All dark; method recorded | Any emission |
| S6 | USB provisioning (credentials from the secret store, never logged). **RF on: Wi-Fi STA** (also S7-S9). Then run the no-credentials check: `grep -c -F -f <secret-file> <all captured logs>` (the secret file is read by grep and never printed) and `grep -c -i -E 'pass(word)?|psk|passphrase' <logs>`; attach both commands and counts | NVS stored; STA connects; both grep counts = 0 | Any credential found in a log → stop, quarantine the log |
| S7 | Power-cycle; STA reconnect | Reconnects without reprovisioning | — |
| S8 | `nn2ctl` (PR7 SHA) runs every ADR-0004 command and error code | Replies per ADR-0004; HW-dependent replies flagged SIMULATED; `safe_off` reports its evidence level | Missing SIMULATED flag. **BLOCKED until PR7 and PR8** |
| S9 | Stop host heartbeats with `nn2ctl`; record the time of the last heartbeat, the grace start (`LINK_GRACE_STARTED` event, if present) and `STOPPED` | `STOPPED`, `end_reason=LINK_LOSS`. Grace value: `NN2_HOST_GRACE_MS = 3000` ms per ADR-0004 rev 3.1 §3.11 (draft PR [#55](https://github.com/YerriSuyudHasim/noctinirs-2/pull/55) @ `702a8c4a88e9c139e45de49bd12dad102430a479`, status **Proposed**, not on `main`), from decision D1, [#33 comment 6227092516](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6227092516) [P until ADR-0004 is accepted]. `NN2_HOST_SILENCE_MS` and the heartbeat period are OPEN [U]. The measured times are **record-only**, because no tolerance is defined | **BLOCKED until PR7 and PR8** |
| S10a | Warm reset by `esp_restart` (trigger: `nn2ctl`/console command provided by #49 [U]) | Reset reason `ESP_RST_SW` [P]; IO18 LOW at the hook; emitters dark | Unexpected reason; emission |
| S10b | Watchdog reset (trigger: a test hook in the #49 build that starves the task WDT [U]; no hardware method) | Reset reason `ESP_RST_TASK_WDT` [P]; IO18 LOW at the hook; emitters dark | Unexpected reason; emission |
| S10c | Panic reset (trigger: a test hook in the #49 build that calls `abort()` [U]) | Reset reason `ESP_RST_PANIC` [P]; IO18 LOW at the hook; emitters dark | Unexpected reason; emission |
| S10d | SW1/EN button press (label per PR10) | Reset reason as logged by ROM/IDF (expected `ESP_RST_POWERON` for a CHIP_PU reset [P]); IO18 LOW at the hook; emitters dark | Unexpected reason; emission |
| S10e | USB-Serial/JTAG reset (`esptool --after hard_reset` via USB, version per `host`) | Reset reason `ESP_RST_USB` [P]; IO18 LOW at the hook; emitters dark | Unexpected reason; emission |
| S11 | Final instrumented emitter-dark check | Dark; method recorded | Any emission |

For each S10 sub-step record its own result, reset reason and IO18 trigger-to-LOW latency. The latency is **record-only: no PASS/FAIL until a limit is defined**. If the #49 package has no trigger for S10a-S10c, that sub-step is NOT RUN, not FAIL.

## 10. Evidence record (template)

One record per execution, append-only; a rerun is a new record. Agent 06 owns the final format.

```yaml
procedure: H-COMMS-1
repository: YerriSuyudHasim/noctinirs-2
procedure_sha: "<full commit SHA of this file>"
evidence_class: H
owner_a3_approval: "<#53 comment URL>"
owner_confirmations:            # Owner's own comments on #53, not relays
  vbus_sw3: "<#53 comment URL + answer>"
  board_id: "<#53 comment URL>"
  buttons: "<#53 comment URL>"
board: {id: "Nocti board v1 (single unit)", id_source: "P: relayed by Nori via Agent 01, pending Owner confirmation", serial: "none", rev: "UNKNOWN (REV field empty)", schematic_sha: f2d7c8cfecdc6570df155fac4d90625c16a7bf4b, mac: "", module_marking: "", emitter_part_marking: ""}
button_label_check: {result: "MATCH|MISMATCH", method: "silkscreen|photo", photo_url: ""}   # PR10; MISMATCH = stop
power_config: {source: "", sw3: "", sw5: "", battery: "fitted|removed", supply_v: "", current_limit_a: "", usb_meter_stop_a: "", source_record: "<#53 URL, PR9>"}
fixture_id: ""
emitter_cover: ""
operator: ""
rf: {wifi_sta: true, steps: [S6, S7, S8, S9], ap_ssid_recorded: false}
firmware:
  source_sha: ""
  profile: ""
  esp_idf_version: ""
  esp_idf_commit: ""
  guide_sha: ""                  # PR11 reviewed revision
  package_zip: {sha256: "", artifact_url: ""}
  images:
    bootloader: {sha256: "", artifact_url: ""}
    partition_table: {sha256: "", artifact_url: ""}
    app: {sha256: "", artifact_url: ""}
dependencies: {p7_merge_sha: "", protocol_core_sha_48: "", nn2ctl_sha_50: "", nn2ctl_conformance: {result: "", run_url: ""}}
host: {os: "", python: "", nn2ctl_sha: "", esptool_version: "", console: "UART0 115200"}
equipment: [{instrument: "", model: "", serial: "", band_or_range: "", cal_due: "", functional_check: ""}]
start: {utc: "YYYY-MM-DDTHH:MM:SSZ", kst: "YYYY-MM-DD HH:MM:SS KST"}
end: {utc: "", kst: ""}
aborted: false
stop_triggered: {step: null, condition: null}
steps:
  - id: S1
    utc: ""
    command_or_procedure: "D1-D3"
    result: "PASS|FAIL|BLOCKED|NOT RUN"
    measured: {rail_3v3_v: "", sys_out_v: "", rail_5v3_led_v: ""}
    evidence: [{file: "", sha256: "", artifact_url: ""}]
    notes: ""
  - id: S2
    utc: ""
    command_or_procedure: "D4, USB connected, SW3 OFF"
    result: ""
    measured: {rail_3v3_v: "", sys_out_v: "", rail_5v3_led_v: ""}
    evidence: []
  - id: S3          # same shape; also S4, S5, S11
    utc: ""
    command_or_procedure: ""
    result: ""
    optical_check: {method: "camera_no_ir_cut|ir_viewer_card|photodiode|power_meter", instrument: "", band_nm: "", result: "dark|emission"}
    evidence: []
  - id: S4
    utc: ""
    result: ""
    reset_reason: ""
    rom_to_hook_latency_us: {value: "", status: "record-only"}
    optical_check: {method: "", instrument: "", band_nm: "", result: ""}
  - id: S6
    utc: ""
    result: ""
    credential_grep: {commands: ["grep -c -F -f <secret-file> <logs>", "grep -c -i -E 'pass(word)?|psk|passphrase' <logs>"], counts: [null, null], logs_scanned: []}
  - id: S9
    utc: ""
    result: ""
    grace_ms_reference: 3000     # ADR-0004 rev 3.1 (PR #55, Proposed)
    t_last_heartbeat_utc: ""
    t_grace_start_utc: ""
    t_stopped_utc: ""
    end_reason: ""
    timing_status: "record-only"
  - {id: S10a, trigger: "esp_restart", utc: "", result: "", reset_reason: "", io18_latency_us: "", latency_status: "record-only"}
  - {id: S10b, trigger: "task WDT test hook", utc: "", result: "", reset_reason: "", io18_latency_us: "", latency_status: "record-only"}
  - {id: S10c, trigger: "panic test hook", utc: "", result: "", reset_reason: "", io18_latency_us: "", latency_status: "record-only"}
  - {id: S10d, trigger: "SW1/EN button", utc: "", result: "", reset_reason: "", io18_latency_us: "", latency_status: "record-only"}
  - {id: S10e, trigger: "USB-JTAG hard_reset", utc: "", result: "", reset_reason: "", io18_latency_us: "", latency_status: "record-only"}
  # S0..S11: every step has id, utc, command_or_procedure, result, evidence
logs: {uart0_boot_logs: {sha256: "", artifact_url: ""}, nn2ctl_log: {sha256: "", artifact_url: ""}, scope_captures: [{file: "", sha256: "", artifact_url: ""}]}
limitations: []                  # e.g. latency record-only, relayed Owner answers, emitter part [P]
not_run: []                      # every step or sub-step NOT RUN, with reason
deviations: []
```

## 11. Cleanup

E-STOP sequence (§5) → wait ≥ 5 s → verify `Rail_3.3V`, `SYS_Out` and `RAIL_5.3V_LED` < 0.1 V → re-run the S6 credential grep over every retained log → retain the raw logs and captures, unedited, with SHA-256 and artifact link → remove Wi-Fi credentials from the host scratch. Erasing the board NVS is a separate decision → attach the record to #53. No automatic next test.

## 12. Owner approval point (A3)

Execution starts only after PR1-PR12 are met and the Owner posts on #53 an A3 approval naming:

- this procedure's full commit SHA and the package identity (PR2);
- board ID "Nocti board v1 (single unit)", fixture, emitter cover, operator and time window;
- the energized power configuration of §4.1 (PR9 values);
- **RF use: Wi-Fi STA (2.4 GHz) in S6-S9**, the test network, and the location;
- the stop conditions of §9, plus these RF stop conditions: stop if the device joins any network other than the named test AP, if RF use falls outside the granted window or location, or if any emission is detected while RF is on;
- an explicit re-confirmation that the residual risks H1-H3 (§13) are accepted for this run.

Reusing approval for another board, package or revision is not permitted (hub AGENTS.md §7). A changed method requires a new revision of this file, re-review and a new A3 grant.

## 13. Residual risks (accepted for bench only)

Analysis in D6, [#33 comment 6333972667](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333972667). For the record, the Owner's acceptance for board v1, **bench work only**, was relayed by Nori in [#33 comment 6333953903](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333953903). The A3 grant must re-confirm H1-H3 (§12).

- **H1:** a warm ESP reset during emission leaves the LEDs on until the hook runs. Mitigated by P7 and by no emission in this procedure; the window is recorded in S4/S10.
- **H2:** in download mode, or with no valid hook, the LEDs may stay on indefinitely. Mitigated only by §7 de-energize-before-download and the D4 gated check.
- **H3:** a brownout at 1-3 V on `Rail_3.3V` neither resets the PCA nor keeps it in spec. Mitigated by a stable supply and the brownout detector; HW fix in the next revision.

Not accepted here and still [U]: H5 back-power while PCA VDD < V_POR (including through USB data or UART lines). If D3 or D4 shows a voltage ≥ 0.1 V on any rail, stop.

## 14. Sources and revisions

| Source | Revision |
|---|---|
| Main schematic `SCH_Thesis_main_2026-10-16.json` | `f2d7c8cf`, blob `bce01e07f6a21397c146b09f4645c9ca46b4e308` |
| Main BOM `BOM_Thesis_main_2026-10-16.csv` | `f2d7c8cf`, blob `e561abfb3983c3a7d8285dcb7f8cdd99797848b9` |
| ADR-0002 | `f2d7c8cf`, blob `821ab7355bb54eeb3afc911a9020e954b17950b3` (Accepted) |
| Marubeni SMT735D/850D datasheet (2024.12) | `Datasheet/SMT735D_850D-1.pdf` @ `f2d7c8cf`, blob `cfad8ca0ed09c9f4a5d523d5f0a8843a160c3b75` |
| ADR-0004 rev 3.1 | PR #55 @ `702a8c4a88e9c139e45de49bd12dad102430a479` (Proposed, not on `main`) |
| Board v1 package guide | PR #46 @ `f0b49bbe8392b517b2756e9dfcc15f9bce6d0249` (FOR REVIEW) |
| D6 analysis / Owner acceptance | #33 comments 6333972667 / 6333953903 |

`reference/**` was not used.

| Rev | Commit | Change |
|---|---|---|
| 1 | `9ef551d1eb5051392128fca2bb6572eed9f063e7` | Initial draft |
| 2 | this commit | Agent 06 V1-V2, R1-R6; Agent 07 B1-B4, S1-S8; D4 gated measured check; relayed Owner answers (VBUS/SW3, board ID, buttons) |
