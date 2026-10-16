# ADR-0002: fNIRS Optical Acquisition Timing for NoctiNIRS 2

| Field | Value |
|---|---|
| ID | ADR-0002 |
| Title | fNIRS optical acquisition timing (configurable frame rate, slot geometry, LED ON/OFF, ADC sampling) |
| Status | **Proposed** (not approved; not implemented) |
| Date | 2026-10-16 (rev 6) |
| Issue | [#21](https://github.com/YerriSuyudHasim/noctinirs-2/issues/21) |
| Author role | Architect / Orchestrator (draft) |
| Independent reviewer | TBD (must not be the author) |
| Approver / merge | Project Owner `YerriSuyudHasim` only |
| Input commit | `noctinirs-2` `main` @ `96e68e1e052a5fce1ed006805132269cf34f1c1e` |
| Evidence class of this document | Static (design review only; Class H / hardware timing NOT RUN) |

> This ADR describes a *target* acquisition contract for later firmware bring-up. Nothing in it is implemented on the active 0.1.x path at the input commit. Historical V2/V3/V4 constants and call patterns are **evidence only**, not automatic NN2 requirements or ESP-IDF API templates. The project targets **ESP-IDF 6.1**. Proposed parameters are marked **[Proposed]** until the Project Owner accepts them after independent review. This ADR does **not** authorize flashing, hardware tests, merges, releases, or tags.

## 1. Context

### 1.1 Problem

NoctiNIRS 2 must eventually acquire multi-source, dual-wavelength (735 nm / 850 nm) optical intensity with local ambient subtraction. The Owner requires a configurable **optical frame rate** \(f_s\); the **implemented allowed set** is Owner-locked to exact **\{1,2,4,5,8,10\} Hz** (t34u Q1). No accepted document yet defines:

- frame / slot geometry for variable \(f_s\);
- LED ON vs LED OFF (dark) phase timing and settling;
- how ADS131M04 continuous sampling relates to optical \(f_s\);
- averaging count \(K\) and validity rules;
- when configuration may change relative to SCAN sessions.

Without that contract, later bring-up would either freeze an undocumented 10 Hz historical layout or invent incompatible timings per module.

### 1.2 Current product state (verified at `96e68e1`)

- Active firmware remains a thin scaffold / composition-root direction; 0.1.x still excludes active ADC/emitter paths (fake/disabled adapters). Hardware timing is **NOT RUN**.
- `docs/adr/0001-clean-architecture-boundaries.md` exists (Proposed). This ADR is the next free number: **0002**.
- `docs/hardware/**` is **not** on `main` at `96e68e1` (hardware board doc still Draft on PR #20). Board facts used below that are not in `main` are labeled **[O]** Owner statement or **[U]** unverified / draft PR evidence.

### 1.3 Historical references (evidence only)

`reference/v2`, `reference/v3`, and `reference/v4` are read-only historical snapshots (`agent_edits: forbidden`, `active_build_inputs: false`). Static evidence at `96e68e1`:

| Source | Historical values (not NN2 requirements) |
|---|---|
| `reference/v3/.../nocti_v3_config.h` | `FRAME_RATE` 10 Hz; `SLOT_COUNT` 20; `FRAME_PERIOD` 100000 µs; `SLOT_PERIOD` 5000 µs; `LED_OFF_OFFSET` 2500 µs; settle ON/OFF 500 µs; end guard 400 µs; ADS 8000 Hz / 125 µs; K target 10 / min 8; LED currents 15 mA; LED current max 50 mA; I2C commit allowance 250 µs; ADS CLKIN 8.192 MHz, OSR 512 |
| `reference/v3/docs/V3_FINAL_TIMING_CALCULATION.md` | Same geometry; absolute deadlines from frame origin; stable-window math after commit+settle(+guard) |
| `reference/v4/.../nocti_v4_scheduler_config.h` | Slot geometry 20×5000 µs / 100000 µs frame; OFF phase default 2200 µs (qualification 2500 µs); settle 500 µs; slot end guard 100 µs; K 10/8; OFF phase selected only while STOPPED, snapshotted for SCAN |
| `reference/v4/.../nocti_v4_ads_config.h` | ADS131M04 @ 8000 SPS, CLKIN 8.192 MHz, OSR 512, period 125 µs |
| `reference/v2/PAIRED_LED_OFF_V2.md` | Local LED-OFF and LED-ON per slot; \(I_{\mathrm{corrected}} = I_{\mathrm{LED\_ON}} - I_{\mathrm{LED\_OFF}}\); no reuse of stale dark profile; under-K / CRC / unequal counts invalidate the slot |

Board / Owner context for NN2 (not all on `main` yet):

- fNIRS path: PCA9956B emitters + ADS131M04 on optode (8.192 MHz oscillator) + Marubeni 735/850 LEDs; typical **10 sources × 2 wavelengths → 20 slots**. **[O]/[U]** (board doc Draft / Owner)
- Owner-approved LED current **cap 50 mA**; first bring-up/debug set-point **15–30 mA** (Owner-locked t34u); later operational/default set-point **50 mA** pending Agent 04 PCA9956B + Marubeni datasheet cross-check. Continuous thermal at sustained duty **[U]**. **[O]**
- ADS1299 present for future biopotential; **out of 0.1.x / this ADR scope**. **[O]/[U]**
- Known hardware deviation: emitter OE# tied to GND (no hardware emitter disable) — tracked separately; not a waiver of software fail-safe obligations. **[O]**

### 1.4 Owner binding requirements

- Optical frame rate \(f_s\) **MUST** be selectable only from the Owner-locked exact set **\{1,2,4,5,8,10\} Hz**. Values \{3,6,7,9\} and any other rate are **rejected** at configure-time (no floor-to-20 µs skew). **[Owner-locked t34u ~15:16 Asia/Shanghai]**
- Firmware for NoctiNIRS 2 **MUST** prefer helpful **ESP-IDF 6.1** features where they fit the job (e.g. modern timer / GPTimer usage, current driver APIs, `eventfd` and related I/O notification facilities, and other IDF 6.1 primitives appropriate to acquisition timing). Older ESP-IDF 5.x patterns from `reference/v2`–`v4` are **not** a mandate to copy APIs; they are timing-intent evidence only. Any retention of a 5.x-style approach needs an explicit, reviewed justification. **[Owner-required]** (project already targets ESP-IDF 6.1 / ESP32-S3)

## 2. Requirements and constraints

| ID | Requirement | Source |
|---|---|---|
| R1 | Optical \(f_s\) selectable only from the exact set **\{1,2,4,5,8,10\} Hz**. Firmware SHALL reject \{3,6,7,9\} and any other rate at configure-time (STOPPED). No floor-to-20 µs skew. | Owner (t31u range intent; t34u Q1 lock ~15:16 Asia/Shanghai) |
| R2 | Acquisition uses time-multiplexed emitters and continuous ADS131M04 sampling with per-slot ambient subtraction. | Product intent + historical evidence |
| R3 | Historical V2/V3/V4 timings are evidence, not automatic requirements. | Governance / `reference/**` policy |
| R4 | No clinical claims; no fabricated hardware timing evidence. | Standing Owner rules |
| R5 | Configuration of optical \(f_s\) (and related session-immutable timing) only while acquisition is STOPPED; immutable for a SCAN session. | **[Proposed]** (mirrors V4 OFF-phase session snapshot) |
| R6 | Software MUST enforce the Owner-approved LED current cap of 50 mA. | Owner |
| R7 | 0.1.x does not activate ADC/emitter; this ADR is a design contract for later bring-up issues. | Milestone scope |
| R8 | Dependency and layering remain subject to ADR-0001. | ADR-0001 |
| R9 | Acquisition timing implementation SHALL prefer ESP-IDF 6.1 APIs appropriate to the job; historical V2–V4 code is timing-intent evidence only, not an API portability mandate. Deviations to older IDF patterns require written justification in the implementing PR. **IDF 6.1 preference is not proven timing** — Class H on-target measurement remains NOT RUN until Owner-authorized. | Owner (binding, 2026-10-16) |
| R10 | LED current has **two distinct phases**: (1) **Bring-up/debug** first energisation **15–30 mA** (Owner-locked) — this is the only first-bring-up set-point; (2) **Later operational/default** **50 mA** only after Agent 04 PCA9956B + Marubeni cross-check (not verified **[V]** until that check). Hard software **cap 50 mA**. Continuous thermal **[U]** until Class H. Do **not** present 50 mA as the bring-up set-point. | Owner (t34u Q3); Agent 07 B2 |
| R11 | \(K\) is the **averaging sample count** (ADC samples), **not** an ON:OFF time or sample ratio. Fixed \(K_{\mathrm{target}}=10\) and \(K_{\min}=8\) **per ON phase and per OFF phase** (historical V3/V4) for all allowed \(f_s\) until an ADR amendment. Q2’s 50/50 split gives equal time budgets so both phases can aim for the same \(K\), which is consistent with matched ON/OFF windows for \(I_{\mathrm{ON}}-I_{\mathrm{OFF}}\). | Owner (t34u Q4); Agent 07 B1 |
| R12 | Settle/guard (and related commit allowance) re-tune on board v1 is approved **in principle** when it improves the system; Class H energization still needs separate Owner authorization. | Owner (t34u Q5) |
| R13 | Default per-slot LED ON/OFF phase split is **50/50** of `slot_period_us`. Optional V4-style OFF-phase offsets remain a later STOPPED-only configuration (snapshotted for SCAN), not the default. | Owner (t34u Q2 lock) |
| R14 | Implemented optical \(f_s\) allowed set is exactly **\{1,2,4,5,8,10\} Hz**. Reject all other rates at configure-time. | Owner (t34u Q1 lock ~15:16 Asia/Shanghai) |

## 3. Decision

### 3.1 Optical frame rate (Owner-locked exact set)

- The **optical frame rate** \(f_s\) is the rate at which one complete multiplex cycle over all optical slots is produced (one “frame”).
- **Allowed set — Status: Owner-locked (t34u ~15:16 Asia/Shanghai):** exact integers **\{1,2,4,5,8,10\} Hz only**.
- Firmware SHALL **reject** \{3,6,7,9\} and any other requested rate at configure-time (STOPPED). **No** floor-to-20 µs skew policy.
- **Rationale (Owner accepted specialist consensus + external research rec A):** Agents 02 / 03 / 05 (Architect Agent 02 note 2026-10-16) and external research recommendation A favored the exact set for cleanest/most accurate time series and ambient fidelity:
  - For \(f_s \in \{1,2,4,5,8,10\}\): `FRAME_PERIOD_US = 1e6/fs` and `SLOT_PERIOD_US = FRAME/20` are exact integers (matches historical v3/v4 asserts at 10 Hz).
  - Same set: `SLOT_PERIOD_US` is an exact multiple of the historical ADS131M04 sample period **125 µs** (8 kSPS) ⇒ integer ADC count per ON/OFF window after settle — best fidelity for \(K\) averaging and \(I_{\mathrm{ON}}-I_{\mathrm{OFF}}\) without fractional-sample ambiguity.
  - Floor-to-20 µs on \{3,6,7,9\} would accept systematic rate error (~40–120 ppm) and break exact ADC-grid alignment — rejected.
- §3.4 Table A is the **implemented** period table. Table B records rejected rates (historical discussion only; not implemented).
- If a future protocol requires 3/6/7/9 Hz, reopen with Class H evidence + an explicit period/ADC-grid contract (ADR amendment + Owner approval).
- Nominal frame period: \(\mathrm{frame\_period\_us} = 10^{6} / f_s\) microseconds (exact for every allowed \(f_s\)).

### 3.2 ESP-IDF 6.1 implementation preference (Owner-required)

NoctiNIRS 2 targets **ESP-IDF 6.1** on **ESP32-S3**. When this acquisition contract is later implemented (outside 0.1.x fake/disabled adapters):

- Prefer IDF 6.1 facilities that improve correctness, observability, or maintainability for the timing path — for example: GPTimer / timer group APIs current in 6.1 for absolute slot deadlines; current I2C and SPI driver APIs for PCA9956B commits and ADS131M04 frames; `eventfd` (or equivalent IDF 6.1 notification primitives) where task wakeups or cross-subsystem signalling benefit from them; other 6.1 helpers as justified in design notes.
- Treat `reference/v2`–`v4` as **evidence of optical timing intent** (frame/slot math, ON/OFF protocol, \(K\), settle budgets). Do **not** treat their ESP-IDF 5.x call shapes, driver wrappers, or task patterns as required templates.
- If an implementer proposes retaining a 5.x-era pattern, the PR MUST document why a 6.1 alternative is unsuitable (measurement, missing feature, or ADR-0001 constraint) — silence is not justification.

Concrete API choices remain with Firmware/RTOS specialists at bring-up; this ADR only binds the **preference and justification rule**. Preferring IDF 6.1 **does not** constitute proven optical or electrical timing — that requires Class H measurement (NOT RUN).

### 3.3 Slot geometry **[Proposed]**

- `SLOT_COUNT = 20` (10 sources × wavelengths {735, 850}).
- Equal slot duration: \(\mathrm{slot\_period\_us} = \mathrm{frame\_period\_us} / 20\).
- **Period derivation (Owner-locked path):** for allowed \(f_s \in \{1,2,4,5,8,10\}\):

```text
frame_period_us = 1_000_000 / fs          // exact integer for the locked set
slot_period_us  = frame_period_us / 20    // exact integer; also multiple of 125 µs @ 8 kSPS
```

- **Floor-to-20 µs alignment** for \{3,6,7,9\} is **rejected / not implemented** (see §3.1 Owner lock). Reopen only via ADR amendment + Class H + explicit period/ADC-grid contract.
- **Feasibility assert [Proposed]:** at configure-time (STOPPED), both ON and OFF phase budgets MUST fit `settle + K_min × T_adc (+ guards)` or the configuration is rejected.
- Deadlines remain **absolute from frame origin** (historical V3/V4 pattern). A late event does not shift subsequent slot deadlines; it is counted as a timing fault/diagnostic. **[Proposed]**

### 3.4 Frame / slot period table

**Table A — Implemented / Owner-locked allowed set** (exact integers; `SLOT % 125 == 0` at 8 kSPS). **[Owner-locked t34u Q1]**

| \(f_s\) (Hz) | `frame_period_us` | `frame_period_ms` | `slot_period_us` | Actual \(f_s\) (Hz) | Notes |
|---:|---:|---:|---:|---:|---|
| 1 | 1 000 000 | 1000.000 | 50 000 | 1.000000 | exact; 400×125 µs/slot |
| 2 | 500 000 | 500.000 | 25 000 | 2.000000 | exact |
| 4 | 250 000 | 250.000 | 12 500 | 4.000000 | exact |
| 5 | 200 000 | 200.000 | 10 000 | 5.000000 | exact |
| 8 | 125 000 | 125.000 | 6 250 | 8.000000 | exact |
| 10 | 100 000 | 100.000 | 5 000 | 10.000000 | exact (historical V3/V4 geometry) |

**Table B — Rejected / not implemented** (\{3,6,7,9\}): historical discussion of floor-to-20 µs skew only. **Owner-locked reject** (t34u Q1). Do not expose these rates in configuration.

| Requested \(f_s\) (Hz) | Would-be floor-aligned `frame_period_us` | `slot_period_us` | Would-be actual \(f_s\) (Hz) | Why rejected |
|---:|---:|---:|---:|---|
| 3 | 333 320 | 16 666 | ≈3.000120 | ~40 ppm high; slot not multiple of 125 µs |
| 6 | 166 660 | 8 333 | ≈6.000240 | ~40 ppm high; same |
| 7 | 142 840 | 7 142 | ≈7.000840 | ~120 ppm high; same |
| 9 | 111 100 | 5 555 | ≈9.000900 | ~100 ppm high; same |

Reopen 3/6/7/9 only with Class H evidence + explicit period/ADC-grid contract + ADR amendment.

### 3.5 Per-slot LED ON / LED OFF protocol **[Proposed]**

For each slot \(s \in [0, SLOT_COUNT)\):

1. **LED ON phase** for the scheduled (source, wavelength) pair.
2. **LED OFF (dark) phase** for ambient / electronic offset.
3. Emit intensity only when both phases are valid:

\[
I_{\mathrm{corrected}}[frame, s, PD] = I_{\mathrm{LED\_ON}}[frame, s, PD] - I_{\mathrm{LED\_OFF}}[frame, s, PD]
\]

4. If either phase is missing, under-\(K\), CRC-invalid, non-contiguous, or unequal sample count → **slot invalid** (do not invent ambient from another slot/frame). Evidence: V2 paired LED-OFF protocol.
5. **Per-slot \(I_{\mathrm{OFF}}\) / \(I_{\mathrm{dark}}\):** ambient for slot \(s\) MUST come from that slot’s own LED-OFF window in the same frame — never a prior frame’s dark, never another slot’s OFF samples.

**Default phase split — Status: Owner-locked (t34u Q2).** Default is **50/50** of `slot_period_us` (ON budget = OFF budget = `slot_period_us / 2`), matching V3’s 2500/2500 µs at 10 Hz and giving matched windows for fixed \(K\) 10/8 on both phases. Aligns with Agent 02 architecture recommendation 2026-10-16 (specialist consensus) and is now an Owner lock, not merely a recommendation.

**Optional OFF-phase offsets (V4-style) — later / not default [Proposed surface]:** may be exposed later as a STOPPED-only configuration, snapshotted for the whole SCAN (immutable mid-session). **Not** the default. Any offset MUST still pass the §3.3 feasibility assert (both phases ≥ settle + \(K_{\min}\times T_{\mathrm{adc}}\) + guards) or be rejected at configure-time. Class U can prove accept/reject logic; Class H (NOT RUN; needs Owner auth) is required before claiming a non-50/50 offset improves ambient fidelity on hardware.

### 3.6 Settling, commit allowance, and end guard **[Proposed start; re-tune approved in principle — Owner t34u Q5]**

Starting points from historical evidence. **Owner approved in principle** that settle/guard (and related commit allowance) **MAY be re-tuned on board v1** when measurement shows an improvement. Class H measurement that energizes emitters or drives the acquisition path still requires **separate, explicit Owner authorization** and remains **NOT RUN** in this ADR:

| Parameter | Proposed start | Historical basis |
|---|---|---|
| `LED_ON_SETTLE_US` | 500 | V3/V4 |
| `LED_OFF_SETTLE_US` | 500 | V3/V4 |
| `I2C_COMMIT_ALLOWANCE_US` | 250 | V3 design allowance (PCA9956B LEDOUT burst); measure p99 on hardware |
| `SLOT_END_GUARD_US` | 100 | V4 (V3 used 400 µs — prefer tighter V4 start; re-tune) |

Samples are accepted only inside the **stable window** after commit boundary + settle (**settle blanking** of post-edge samples), and before end guard (OFF phase). Historical settle start values and blanking adequacy on board v1 remain **[U]** until Class H measures them. Classification windows scale with `slot_period_us` at lower \(f_s\). Re-tuned values that change this ADR’s start numbers require an ADR amendment after measured evidence.

**P20-H2 / software-only emitter OFF:** board OE# is tied to GND (no hardware output-disable). LED-OFF / dark and fail-safe OFF are **software-only** obligations on the PCA9956B path; this ADR does not waive them.

### 3.7 ADC sampling (ADS131M04) **[Proposed]**

- Optical path ADC: **ADS131M04** (not ADS1299).
- Continuous conversion; optical \(f_s\) and ADC output data rate (ODR) are **independent**.
- Default ODR baseline: **8000 SPS** (period 125 µs), with CLKIN 8.192 MHz and OSR 512 as in V3/V4 — **[Proposed]** historical baseline, not locked until measured.
- ODR may be chosen differently provided each stable ON/OFF window still admits at least \(K_{\min}\) contiguous valid samples for the selected \(f_s\).
- ADS1299 biopotential path: **non-goal** of this ADR.

### 3.8 Averaging count \(K\) **[Owner decision 2026-10-16 t34u; Agent 07 B1]**

**Definition:** \(K\) is the **averaging sample count** — the number of contiguous valid ADS131M04 samples averaged inside one LED phase window. It is **not** an ON:OFF time ratio and **not** an ON:OFF sample-count ratio.

| Parameter | Value | Scope |
|---|---|---|
| \(K_{\mathrm{target}}\) | 10 | **Per ON phase** and **per OFF phase** (same target both sides) |
| \(K_{\min}\) | 8 | **Per ON phase** and **per OFF phase** (minimum-valid both sides) |

Historical basis: V3/V4 (`nocti_v3_config.h`, V4 scheduler config).

**Locked (t34u Q4):** both values stay fixed for all allowed \(f_s \in \{1,2,4,5,8,10\}\) until an ADR amendment. No automatic scale-with-\(f_s\).

**Consistency with Q2 50/50:** the Owner-locked 50/50 phase split provides **equal time budgets** for ON and OFF; both phases therefore aim for the **same** \(K_{\mathrm{target}}\)/\(K_{\min}\). That matched-window design is what makes local \(I_{\mathrm{ON}}-I_{\mathrm{OFF}}\) (per-slot) a fair ambient subtraction — unequal \(K\) targets between ON and OFF are out of scope for this revision.

At **10 Hz**, \(K_{\mathrm{target}}=10\) must still fit after I2C commit + settle blanking + guard inside each phase budget (historical V3 static asserts at 5 ms slots). Feasibility assert in §3.3 enforces this at configure-time.

### 3.9 LED current **[Owner decision 2026-10-16 t34u; Agent 07 B2]**

Two phases are labeled distinctly. **Do not** present 50 mA as the only (or first) bring-up set-point.

| Phase | Set-point | Status |
|---|---|---|
| **Bring-up / debug** (first energisation) | **15–30 mA** | **Owner-locked** — mandatory first band |
| **Later operational / default** | **50 mA** | Intended later set-point **only after** Agent 04 PCA9956B + Marubeni cross-check; **not [V]** until that check |
| **Hard software cap** | **50 mA** | Always enforced (tighter of Owner cap / PCA / datasheet once known) |
| Continuous thermal at sustained duty | — | **[U]** until Class H (Owner auth required) |

- Enforcement is mandatory in the LED port/adapter (R6 / R10).
- Software **MUST** enforce the tighter of: (a) Owner hard cap 50 mA, (b) PCA9956B capability, (c) Marubeni LED datasheet limits once Agent 04 reports them. If that cross-check requires a lower operational default or lower hard limit, this ADR **MUST** be amended before any Class H optical energization beyond the debug band.
- Historical 15 mA (V3) aligns with the lower end of the bring-up band; it is evidence, not a clinical exposure claim.

### 3.10 Configuration API and session immutability **[Proposed]**

Mirror V4 session discipline:

- Optical \(f_s\), slot geometry derived from it, phase-split parameters, \(K\) policy, and ADC ODR used for a SCAN are selected only while acquisition is **STOPPED**.
- On START/SCAN, the active timing block is **snapshotted** and immutable until STOP.
- Attempts to change \(f_s\) during SCAN are rejected (error), not deferred mid-frame.

### 3.11 Non-goals

- Physiology / haemodynamic processing, classification beyond corrected intensity validity.
- Wireless streaming contracts, OTA, storage formats.
- ADS1299 biopotential acquisition.
- Clinical claims, patient safety certification, or exposure limits beyond the Owner current cap.
- Implementing or flashing firmware in this ADR.
- Changing `reference/**` or treating historical constants as normative NN2 requirements.
- Mandating a verbatim port of ESP-IDF 5.x APIs from historical trees (see 3.2).

## 4. Timing budget sketch (informative)

At requested **10 Hz** (tightest optical schedule; historical geometry):

```text
frame 100000 µs = 20 × slot 5000 µs
per slot (50/50): ON 2500 µs | OFF 2500 µs
ON stable ≈ 2500 - commit(250) - settle(500) = 1750 µs → ≤14×125 µs periods @ 8 kSPS
OFF stable ≈ 2500 - commit(250) - settle(500) - guard(100) = 1650 µs (order-of-magnitude; V3 used 400 µs guard → 1350 µs)
K_target 10 × 125 µs = 1250 µs must fit inside stable windows
```

At **1 Hz**, each slot is 50 000 µs: budgets are loose for \(K\), but LED duty, thermal, and raw-buffer sizing dominate (see Risks).

These sketches are **[I]** inferred from historical math; board v1 must measure commit latency, DRDY phase, and worst-case \(K\).

## 5. Alternatives considered

| Alternative | Description | Pros | Cons | Outcome |
|---|---|---|---|---|
| A. Fixed 10 Hz only (V3/V4) | Keep historical frame only. | Proven geometry in reference trees. | Violates Owner R1 (1–10 Hz). | Rejected. |
| B. Variable SLOT_COUNT with \(f_s\) | Fewer slots at higher rates. | Relaxes 10 Hz I2C pressure. | Breaks 10×2 wavelength coverage; complex configs. | Rejected for v1 contract. |
| C. Restrict \(f_s\) to exact divisors | Only {1,2,4,5,8,10}; reject {3,6,7,9} (no floor skew). | Exact FRAME/SLOT; SLOT multiple of 125 µs @ 8 kSPS; cleanest \(K\) / ambient math (Agent 02, 2026-10-16; external research rec A). | Loses 3/6/7/9 until ADR reopen. | **Owner-locked** (t34u Q1 ~15:16 Asia/Shanghai). |
| D. Nanosecond / GPTIMER sub-µs periods | Exact 1e9/\(f_s\) without 20 µs quantisation. | Near-exact rate. | More complex timebase; still need equal slots. | Deferred. |
| E. Shared stale DARK profile | One dark map reused across frames. | Less LED toggling. | Failed historically (V2); ambient drift. | Rejected. |
| F. Couple ADC ODR to optical \(f_s\) | Change OSR whenever \(f_s\) changes. | Fewer samples at low \(f_s\). | Couples unrelated knobs; hurts SNR control. | Rejected as default. |
| G. Port V3/V4 IDF 5.x APIs verbatim | Copy historical driver/timer/task call shapes into NN2. | Faster first compile from reference trees. | Fights project IDF 6.1 target; freezes obsolete patterns; Owner forbids without justification. | Rejected as default (see 3.2). |

## 6. Consequences

**Positive**

- Clear Owner-facing knob: optical \(f_s\) from the locked exact set \{1,2,4,5,8,10\} Hz (Table A); Table B documents rejected rates only.
- Ambient subtraction contract is unambiguous (\(I_{\mathrm{ON}}-I_{\mathrm{OFF}}\), local per slot).
- Separates optical frame rate from ADC ODR; enables later SNR tuning.
- Session immutability reduces mid-SCAN races (aligned with ADR-0001 / V4 lessons).
- IDF 6.1 preference keeps timing code on the supported toolchain and encourages modern timers/drivers/`eventfd` rather than fossilised 5.x copies.

**Negative**

- At 10 Hz, I2C LEDOUT commits remain timing-critical; needs measured p99.
- At 1 Hz, long ON duty and larger per-slot raw buffers increase thermal and RAM pressure.
- {3,6,7,9} and floor-skew are Owner-rejected; configuration MUST fail closed if requested.
- Implementers must invest in learning/mapping IDF 6.1 equivalents instead of pasting reference firmware; justified 5.x carry-overs need extra review text.

**Neutral**

- Bring-up issues after related firmware PRs will turn this contract into acceptance tests (host timing math first; Class H only with Owner authorization).
- **P20-H2:** OE# tied to GND remains a hardware deviation: LED-OFF / fail-safe are **software-only**; current limits stay mandatory; not solved by this ADR.

## 7. Affected components and interfaces (future)

Proposed touchpoints only (names illustrative; subject to ADR-0001 paths):

- Application: SCAN/STOP lifecycle; reject live \(f_s\) changes.
- Ports: LED current/commit, ADS131 sample stream, timebase deadlines.
- Adapters: PCA9956B, ADS131M04, board pins.
- Host tests: pure functions for `frame_period_us` / slot budgets / window capacity vs \(K\).
- No change to `reference/**` in the ADR merge itself.

## 8. Verification strategy

| Class | What | When |
|---|---|---|
| S / U | Static asserts or host unit tests: Table A periods exact + `SLOT % 125 == 0` (pinned 8 kSPS); **reject** \(f_s \notin \{1,2,4,5,8,10\}\) at configure-time; ON+OFF=slot; 50/50 and optional OFF-phase feasibility assert; windows ≥ \(K_{\min} \times t_{\mathrm{ADS}}\); cap 50 mA + bring-up band 15–30 mA encoded; mid-SCAN config immutability | Before enabling real adapters |
| B | Build with acquisition types present but inactive as required by milestone | CI on PR |
| H | On-target DRDY rate, commit p99, achieved \(K\), thermal at 1 Hz and 10 Hz | **Only with explicit Owner authorization**; NOT RUN in this ADR |
| R | Reviewer ≠ author; Owner merge approval of PR SHA | Process |

## 9. Open questions and risks

| # | Question / risk | Status (2026-10-16 t34u) | Owner of answer |
|---|---|---|---|
| Q-1 | Restrict implemented \(f_s\) to exact \{1,2,4,5,8,10\} (reject floor-skew on 3/6/7/9), or keep all ten with skew / custom periods? | **Owner-locked (t34u ~15:16 Asia/Shanghai):** exact set **\{1,2,4,5,8,10\} Hz only**; reject 3/6/7/9. Aligns with specialist consensus (Agents 02/03/05; Agent 02 2026-10-16) and external research rec A. | Owner (locked) |
| Q-2 | Default phase split: fixed 50/50 vs V4-style selectable OFF-phase as default? | **Owner-locked (t34u):** default **50/50**. Optional V4-style OFF-phase offsets remain a later STOPPED-only config (not default) + feasibility assert. | Owner (locked) |
| Q-3 | LED currents under 50 mA cap? | **Locked (t34u; Agent 07 B2 labels):** **Bring-up/debug** first energisation **15–30 mA** only; **later operational/default** **50 mA** after Agent 04 PCA/Marubeni cross-check (**not [V]** until check). Cap 50 mA. Continuous thermal **[U]**. 50 mA is **not** the bring-up set-point. | Owner (locked bands); Agent 04 cross-check pending |
| Q-4 | \(K\) policy: fixed 10/8 vs scale with slot length? | **Locked (t34u; Agent 07 B1):** \(K\) = **averaging sample count** (not ON:OFF ratio). Fixed \(K_{\mathrm{target}}=10\) / \(K_{\min}=8\) **per ON and per OFF** for all allowed \(f_s\). Consistent with Q2 50/50 equal time budgets so both phases aim for the same \(K\) for matched \(I_{\mathrm{ON}}-I_{\mathrm{OFF}}\). | Owner (locked) |
| Q-5 | Settle/guard / settle-blanking re-tune on board v1? | **Approved in principle** when it improves the system. Settle blanking start values remain **[U]** until Class H. Class H still requires separate Owner authorization (NOT RUN). | Hardware + Firmware (Class H when authorized) |

**Risks (summary):** 10 Hz timing tightness (I2C); 1 Hz thermal/duty and buffer sizes; ADS1299 vs ADS131 naming confusion; treating reference constants as requirements by mistake; treating 50 mA as bring-up current or as thermally verified; confusing \(K\) with an ON:OFF ratio; settle blanking still **[U]**; P20-H2 software-only OFF; IDF 6.1 preference ≠ proven timing; future protocol pressure to reopen 3/6/7/9 without Class H + ADC-grid contract.

## 10. References

- Owner messages: t30u (timing concept), t31u (\(f_s\) configurable 1–10 Hz); Owner binding to prefer ESP-IDF 6.1 features for firmware (timers, modern drivers, `eventfd`, etc.); t34u (cleanest/most accurate; Q1 lock exact \{1,2,4,5,8,10\} ~15:16 Asia/Shanghai; Q2 lock default ON/OFF 50/50; bring-up LED 15–30 mA; later operational default 50 mA + Agent 04 cross-check; Q4 fixed \(K\) 10/8; Q5 settle re-tune approved in principle).
- Agent 02 architecture recommendation 2026-10-16 (Q1 exact set \{1,2,4,5,8,10\}; Q2 50/50 + optional STOPPED OFF-phase) — specialist consensus with Agents 03/05; external research recommendation A aligned on exact set. **Q1 and Q2 Owner-locked** (t34u).
- Agent 07 independent review (CHANGES REQUESTED): B1 \(K\) = averaging sample count per ON and per OFF; B2 two-phase LED current labels; non-blocking: settle blanking [U], P20-H2 software-only OFF, per-slot \(I_{\mathrm{OFF}}\), IDF 6.1 ≠ proven timing.
- Toolchain target: ESP-IDF 6.1 / ESP32-S3 (project baseline; exact pin may be tracked under reproducible-build issues).
- ADR-0001: `docs/adr/0001-clean-architecture-boundaries.md` @ `96e68e1`.
- Evidence only @ `96e68e1`:
  - `reference/v3/components/nocti_v3_common/include/nocti_v3_config.h`
  - `reference/v3/docs/V3_FINAL_TIMING_CALCULATION.md`
  - `reference/v3/docs/V3_P3_TIMING_ANALYSIS.md` (related)
  - `reference/v4/components/nocti_v4_scheduler/include/nocti_v4_scheduler_config.h`
  - `reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h`
  - `reference/v2/PAIRED_LED_OFF_V2.md`
- Board / Owner (Draft / not on `main` at input commit): PR #20 hardware doc path `docs/hardware/nocti-board-v1/hardware.md` when merged; Owner statements on 50 mA cap and OE# deviation.
- Standing rules: no merge/release/flash without specific Owner approval; `reference/**` read-only.

## 11. Revision history

| Rev | Date | Change | Status |
|---|---|---|---|
| 0 | 2026-10-16 | Initial draft for Owner chat review (Orchestrator). Configurable \(f_s\) 1–10 Hz; proposed 20-slot geometry, ON/OFF protocol, settle/K/ADC baselines from V2–V4 evidence. Not committed. | **Proposed** |
| 1 | 2026-10-16 | Owner binding: prefer ESP-IDF 6.1 APIs for acquisition timing (Decision 3.2, R9, Alternative G, Consequences). Historical V2–V4 remain timing-intent evidence only. Still not committed. | **Proposed** |
| 2 | 2026-10-16 | Owner t34u (early fold): Q3/Q4/Q5 direction; Q1/Q2 open pending specialist rec. Still not committed. | **Proposed** |
| 3 | 2026-10-16 | Interim: specialist rec recorded for exact \{1,2,4,5,8,10\} (Table A); 3/6/7/9 then still undecided by Owner. Q-2 then still Proposed specialist rec. R10 / §3.9: bring-up LED **15–30 mA** locked; later operational **50 mA** + Agent 04 cross-check; continuous thermal **[U]**. Q-4 locked 10/8; Q-5 retune OK in principle (Class H needs auth). Local draft only — not committed. | **Proposed** |
| 4 | 2026-10-16 | Owner lock Q-2: default ON/OFF **50/50** (R13, §3.5). Optional V4 OFF-phase remains later STOPPED-only (not default). Status still **Proposed**. Local draft only — not committed. | **Proposed** |
| 5 | 2026-10-16 | Owner lock Q-1 (~15:16 Asia/Shanghai): implemented \(f_s\) = exact **\{1,2,4,5,8,10\} Hz only**; reject 3/6/7/9 (R14, §3.1, Table A implemented / Table B rejected). Cites specialist consensus (Agents 02/03/05; Agent 02 2026-10-16) + external research rec A. Q2/Q3/Q4/Q5 locks unchanged. Status still **Proposed**. Local draft only — not committed. No Class H evidence. | **Proposed** |
| 6 | 2026-10-16 | Agent 07 independent review (CHANGES REQUESTED): **B1** define \(K\) as averaging sample count \(K_{\mathrm{target}}=10\)/\(K_{\min}=8\) per ON and per OFF (not ON:OFF ratio); align with Q2 50/50 matched windows for \(I_{\mathrm{ON}}-I_{\mathrm{OFF}}\) (R11, §3.8, Q-4). **B2** distinct LED phases — bring-up/debug 15–30 mA vs later operational 50 mA after Agent 04 (not [V] until check); cap 50 mA; thermal [U] (R10, §3.9, Q-3). Non-blocking: settle blanking [U]; P20-H2 software-only OFF; per-slot \(I_{\mathrm{OFF}}\); IDF 6.1 ≠ proven timing. Status still **Proposed**. Local draft only — not committed. | **Proposed** |
