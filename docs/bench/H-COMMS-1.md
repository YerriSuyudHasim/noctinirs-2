# H-COMMS-1 bench procedure: board v1, de-energized flash, emitters inactive

> **Status: DRAFT, not approved.** Executing this procedure is an **A3** action. It requires a separate, specific Owner approval recorded on [#53](https://github.com/YerriSuyudHasim/noctinirs-2/issues/53), and the Owner's direct confirmation. Neither this document nor its merge authorizes a flash, power-on or any energized test.
>
> Author: Agent 04 (Hardware Specialist). Reviews required: Agent 05 (RTOS), Agent 06 (Verification, record format), Agent 07 (IR). Approval: Nori + Owner.
> Evidence class of this document: **Class S** (static: datasheets, netlist, repository records). **Class H: NOT RUN.**

Tags: **[V]** verified against the cited source; **[P]** proposed or inferred; **[U]** unknown, to be resolved before or during execution.

## 1. Purpose and scope

H-COMMS-1 is the Class H gate for milestone v0.1.0 (#53 scope 1-5; milestone acceptance item 5). On board v1, with the emitters kept inactive, it verifies:

1. The de-energized-board flash procedure from D6 (#33 [comment 6333972667](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333972667), rec. 4).
2. Emitter-safe boot from P7 (#49): IO18/`RESET_LED` is LOW from boot, safe-off runs, and the reset reason is logged.
3. Communications: USB provisioning, STA reconnect, every ADR-0004 command via `nn2ctl` with SIMULATED flags, and grace expiry → `STOPPED` with `end_reason=LINK_LOSS`.
4. Optical LED-off check, plus IO18 scope captures and the ROM-to-hook latency.

**Excluded:** emitter-on tests (H-LED-1, v0.1.1), soak (v0.1.2), any human-subject or on-skin use, eFuse/security changes, erase-all, and touching GPIO26.

## 2. Prerequisites (all must be recorded before Step 1)

| # | Prerequisite | Status at authoring |
|---|---|---|
| PR1 | P7 [#49](https://github.com/YerriSuyudHasim/noctinirs-2/issues/49) merged to `main`; merge SHA recorded | **BLOCKED**: no PR open |
| PR2 | Firmware package from the #45/#46 flow identified by full source SHA, profile, package ZIP SHA-256, and per-image SHA-256 (bootloader, partition table, app). Offline validation PASS per the guide in §6 | **BLOCKED** |
| PR3 | This procedure approved (AC1 reviews, then Nori + Owner) | **BLOCKED** |
| PR4 | Owner answer to "does USB VBUS feed `SYS_Out` with SW3 off?" recorded on #53 (AC2) | **BLOCKED**: open with the Owner |
| PR5 | Owner A3 grant naming this board, fixture, procedure revision (commit SHA of this file), time window and operator | **BLOCKED** |
| PR6 | Generated sdkconfig of the package shows the brownout detector enabled (P7 AC2) and SPIRAM off | **BLOCKED** (depends on PR1/PR2) |

The board v1 profile is the default (fake ADC, emitter disabled). The diagnostic profile is out of scope unless an H-ADS-PSRAM pre-check has been defined and approved. That check is referenced in the #46 guide but not defined yet [U].

## 3. Board and fixture identity

| Field | Value |
|---|---|
| Board | "Nocti board v1" (Owner naming) [V Owner, #33] |
| Schematic | `Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json` @ `f2d7c8cfecdc6570df155fac4d90625c16a7bf4b` [V] |
| Board revision | The schematic `REV:` field is empty, so the revision is **unknown** [U]. Record any PCB silkscreen marking |
| Board serial / ID | _to record_ |
| ESP32-S3 MAC (from ROM/boot log) | _to record_ |
| Module marking | _to record_ (guide names N16R8; verify) [U] |
| Fixture ID, emitter cover | _to record_ |
| Cable(s) part number | _to record_ |

## 4. Safe limits and safety conditions

- **No LEDs on skin, and no human subject**, during flash, first boot or any step. Fixture only [V Owner decision, #33 comment 6333953903].
- **Emitters must remain dark** throughout. This firmware has no LED drive (milestone exclusion). Any visible light is a stop.
- Firmware current cap: ≤ 50 mA/ch, IREF ≤ 222 (milestone Class U criterion). In this procedure IREF must stay 00h [V milestone #2].
- Hardware maximum per channel: PCA9956B REXT = R4 1 kΩ → **57.4 mA/ch**, with up to 20 connected channels [V D6 §1, datasheet p24].
- Board v1 has **no hardware emitter-off**: OE# is tied to GND, R46 is a pull-up on RESET#, and there is no switch on `RAIL_5.3V_LED` [V D6 §1]. The only safety barriers are power removal and firmware.
- Stable supply only. The ESP brownout detector must be enabled (PR6) [V Owner decision].
- Operator eye protection / emitter cover over LED0-9 and LED14-23 is recommended. Use an opaque cover with an inspection method [P].

## 5. Emergency stop and power removal

**E-STOP:** (1) unplug the USB cable at the host end; (2) switch SW3 off or remove the battery; (3) disconnect any bench supply on `J_PSource`. Keep all three within the operator's reach before Step 1. Whether (1)+(2) removes all energy depends on PR4 [U]. Record the operator's named E-STOP method in the record.

## 6. Install and recovery guide reference (not copied)

Package selection, offline validation, flash command and recovery follow `docs/BOARD_V1_PACKAGE_GUIDE.md` at the #46 head `f0b49bbe8392b517b2756e9dfcc15f9bce6d0249` (blob `bc8e3db2b49db8a097fb17d549712753f6b2749b`), or its merged successor; record whichever SHA is used. This procedure adds only the power and safety sequence around that guide. Where they conflict, stop.

## 7. D6 de-energize-before-download

Why it is needed: in ROM download mode the P7 hook never runs, and a warm reset does not reset the PCA9956B. Only a POR with VDD below 1 V for more than 20 µs guarantees IREF = PWM = 00h (D6 H2; datasheet §7.5, Table 26) [V].

| D-step | Action | Expected | Stop if |
|---|---|---|---|
| D1 | Unplug USB. Switch SW3 off or remove the battery. Remove `J_PSource` | No source connected | — |
| D2 | Wait **≥ 5 s** (D6 criterion: well over 20 µs below 1 V) [P margin] | — | — |
| D3 | Measure `Rail_3.3V` (and `SYS_Out`, `RAIL_5.3V_LED`) with a DMM | `Rail_3.3V` < 1 V; others ≈ 0 V | `Rail_3.3V` ≥ 1 V |
| **D4 HARD STOP** | **Until PR4 is answered and recorded on #53, do not proceed past D3.** If USB VBUS feeds `SYS_Out` with SW3 off, this method is **invalid** for a USB-attached download. The procedure returns to Hardware for revision | — | PR4 open, or answer = "yes" |
| D5 | Connect only the programming source. Enter download mode from cold (hold BOOT/IO0, then apply power) | ROM download banner. LEDs dark | Any light; unexpected banner |
| D6 | Flash per §6 | Guide-defined PASS | Guide stop conditions |

The BOOT/IO0 and SW1/EN contact markings need Hardware confirmation on the physical board [U, #46 guide].

## 8. Instruments

| Instrument | Model / serial | Calibration status / due date |
|---|---|---|
| DMM | | |
| Oscilloscope (≥ 2 ch, ≥ 10 MS/s) for IO18/`RESET_LED`, `Rail_3.3V` | | |
| USB power meter (V/A) | | |
| Lux meter or camera for optical check (optional) [P] | | |
| Host PC: OS, Python, `nn2ctl` SHA, ESP-IDF commit | | |

## 9. Test steps

Stop rule for every step: on any LED light, unexpected reset reason or reset loop, brownout, supply anomaly, heat, burning smell, or readback `FAULTED`: **E-STOP (§5)**, record FAIL/BLOCKED, and do not retry without a new review.

| Step | Action | Expected outcome | Additional stop condition |
|---|---|---|---|
| S0 | Prerequisites PR1-PR6 recorded | All met | Any unmet → BLOCKED |
| S1 | D1-D3 de-energize verification | `Rail_3.3V` < 1 V | ≥ 1 V |
| S2 | D4 gate | PR4 answered "no" (or a revised method approved) | Otherwise stop here |
| S3 | D5-D6 flash, scope on IO18 | Flash PASS. LEDs dark (visual) | Light seen |
| S4 | Cold first boot: repeat D1-D3, then apply power without BOOT, with the scope armed on IO18 + `Rail_3.3V` | IO18 LOW from the hook onward; ROM-to-hook latency captured; reset reason POWERON logged; safe-off readback OK | IO18 HIGH after hook; `FAULTED` |
| S5 | Visual LED-dark check by the operator | All emitters dark | Any light |
| S6 | USB provisioning (credentials from the secret store, never logged) | NVS stored; STA connects | — |
| S7 | Power-cycle; STA reconnect | Reconnects without reprovisioning | — |
| S8 | `nn2ctl` runs every ADR-0004 command and error code | Replies per ADR-0004; HW-dependent replies flagged SIMULATED; `safe_off` reports its evidence level | Missing SIMULATED flag |
| S9 | Stop heartbeats, wait > 3 s grace | `STOPPED`, `end_reason=LINK_LOSS` | — |
| S10 | Warm resets, each scoped on IO18: `esp_restart`, WDT, panic, SW1/EN, USB-JTAG reset | Reset reason matches the trigger; IO18 LOW at the hook; latency recorded; LEDs dark | Unexpected reason; light |
| S11 | Final visual LED-dark check | Dark | Any light |

## 10. Evidence record (template; AC3)

One record per execution, append-only; a rerun is a new record. Agent 06 owns the final format.

```yaml
procedure: H-COMMS-1
procedure_sha: "<full SHA of this file's commit>"
owner_a3_approval: "<#53 comment URL>"
vbus_sw3_answer: "<#53 comment URL + answer>"
board: {name: "Nocti board v1", rev: "UNKNOWN (REV field empty)", serial: "", mac: "", module: ""}
fixture_id: ""
firmware: {source_sha: "", profile: "", esp_idf_commit: "", package_zip_sha256: "", images: {bootloader: "", partition_table: "", app: ""}}
equipment: [{instrument: "", model: "", serial: "", cal_due: ""}]
operator: ""
start: {utc: "YYYY-MM-DDTHH:MM:SSZ", kst: "YYYY-MM-DD HH:MM:SS KST"}
end: {utc: "", kst: ""}
steps:
  - {id: S1, result: "PASS|FAIL|BLOCKED|NOT RUN", measured: "", evidence: "", notes: ""}
  # S0..S11
logs: {boot_logs_with_reset_reason: "", nn2ctl_log: "", scope_captures: "", artifact_sha256: ""}
deviations: []
```

## 11. Cleanup

E-STOP sequence (§5) → wait ≥ 5 s → verify `Rail_3.3V` < 1 V → retain the raw logs and captures, unedited, with SHA-256 → remove Wi-Fi credentials from the host scratch. Erasing the board NVS is a separate decision → attach the record to #53. No automatic next test.

## 12. Owner approval point

Execution starts only after PR1-PR6 are met and the Owner posts on #53 an A3 approval naming: this procedure's commit SHA, the board serial, fixture, operator, time window, and the stop conditions in §9. Reusing approval for another board, package or revision is not permitted (hub AGENTS.md §7).

## 13. Residual risks (accepted for bench only)

Accepted by the Owner via Nori for board v1, **bench work only**: [#33 comment 6333953903](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333953903), analysis in [D6](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333972667).

- **H1:** a warm ESP reset during emission leaves the LEDs on until the hook runs. Mitigated by P7 and by no emission in this procedure; the window is measured in S4/S10.
- **H2:** in download mode, or with no valid hook, the LEDs may stay on indefinitely. Mitigated only by §7 de-energize-before-download, so PR4 is a hard gate.
- **H3:** a brownout at 1-3 V on `Rail_3.3V` neither resets the PCA nor keeps it in spec. Mitigated by a stable supply and the brownout detector; HW fix in the next revision.

Not accepted here and still [U]: H5 back-power while PCA VDD < V_POR. If D3 shows an unexplained voltage on any rail with all sources removed, stop.
