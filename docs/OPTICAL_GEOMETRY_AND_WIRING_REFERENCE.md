# NoctiNIRS optical geometry and wiring reference

Prepared 2026-10-30. Inspected repository baseline: `cbec2dab8e23595ae4e374e181c0d7c70f25edec`.

This reference separates physical detector names, historical LiveView indices,
ADC inputs, source positions, PCA outputs, and optical channel names. It records
existing evidence; it does not change firmware configuration or qualify assembled
hardware. The geometry is a historical NoctiNIRS arrangement, not an automatic
requirement for a new board revision.

## 1. Evidence and confidence

Confidence applies to the stated evidence class, not to all physical hardware.

| Label | Meaning | Limit |
|---|---|---|
| **S / High** | Directly readable schematic connection or symbol pin; static design evidence | Does not establish PCB routing, assembled wiring, or measured operation |
| **F / High** | Explicit repository source constant or documented software convention; static evidence | Does not establish which physical emitter actually lights |
| **H / Medium** | Historical conversation table, internally consistent with other evidence | Original layout/metrology and complete LiveView implementation were not recovered |
| **O / Owner statement** | Owner's explanation in the referenced conversation | Records intent; does not independently prove connectivity |
| **D / Conditional** | Derived by composing mappings with named assumptions | Invalid if any contributing mapping is changed |
| **U / Unknown** | Evidence is missing or insufficient | No mapping should be invented |

### Source register

- **S1 — attached ADC screenshot:** `image(20261030-120759).png`, SHA-256
  `386d426470d4898a67a0124803a94d190419a1ffde2cf763ba597c0bca388684`.
  Shows U8 ADS131M04IRUKR and AIN pin labels.
- **S2 — attached input-filter screenshot:** `image(20261030-120748).png`, SHA-256
  `de30071fb09f2b654daa2eb19d24e48189ccc68521609fa24c05d906a8b27ed8`.
  Shows OUTPD1..4, positive-input series resistors, ground-referenced negative
  inputs and differential capacitors. Both attachments were visually inspected;
  the original chat attachments are not copied into this repository.
- **S3 — full PD-board schematic:** [PNG](../Schematic_and_hardwareInfo/Schematic_PD-board_2026-10-16.png)
  and [EasyEDA JSON](../Schematic_and_hardwareInfo/SCH_PD_board_2026-10-16.json).
  Includes detector/TIA blocks, filters, ADC and ten LED symbols. Corroborates S1/S2.
- **S4 — main-board schematic:** [PNG](../Schematic_and_hardwareInfo/Schematic_Thesis-main_2026-10-16.png)
  and [EasyEDA JSON](../Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json).
  Main-board U8 is the PCA9956B; PD-board U8 is the ADS131M04. Reference designators
  are local to each schematic and must not be confused.
- **F1 — active PCA slot table:** [`channels[20]` and `nn2_board_pca_channel`](../components/nn2_board/nn2_board_pca.c).
- **F2 — historical V2 PCA table:** [`k_default_slot_to_pca`, slot-map resolution and wavelength indexing](../reference/v2/components/nocti_led_driver/nocti_led_driver.c),
  plus [`NOCTI_SLOT_INDEX`](../reference/v2/components/nocti_led_driver/include/nocti_led_driver.h).
  V2 supports custom maps, so its default table alone cannot prove every past runtime configuration.
- **F3 — historical V3:** [PCA table](../reference/v3/components/nocti_v3_led/nocti_v3_led.c)
  and [wavelength pairing](../reference/v3/docs/V3_P4_VALIDITY_POLICY.md#wavelength-pairing).
- **F4 — current bring-up limitations:** [LED bring-up](LED_BRINGUP.md), including
  the requirement to verify physical source/wavelength mapping and unused outputs.
- **H1/O1 — historical discussion:** “Mengingat Geometri NoctiNIRS”, conversation
  ID `6ae486a3-8f34-83e8-9dfc-5db9244ce1ff`. The retrieved assistant messages contain
  the PD permutation, source-detector distances and channel tables reproduced
  below. Their references to July discussions were not independently recovered.
  The owner explicitly explains that physical versus LiveView numbering differs
  because exchanging the order made wiring easier.
- **F5 — partial LiveView trace:** [V2 patch](../reference/v2/NoctiNIRS_paired_ledoff_v2.patch)
  references `software/public/lib/fnirs-geometry.js` and detector-indexed
  `slot.averages[detectorIndex]`. The full geometry file is absent from the inspected
  checkout; this patch does not independently verify the H1 channel/distance tables.

Repository links above refer to the inspected paths. To reproduce this inspection,
read them at the baseline SHA rather than assuming future revisions are identical.
Filename dates are repository labels, not proof of a manufactured hardware revision.

## 2. Naming rules

| Name | Range / example | Meaning |
|---|---|---|
| Physical PD | PD1..PD4 | Detector reference on the PD-board schematic / historical physical naming |
| LiveView PD | PD0..PD3 | Historical software detector label/index |
| ADC channel | AIN0..AIN3 | ADS131M04 differential input pair AINnP/AINnN |
| Physical source | L1..L10 | Ten dual-wavelength source positions |
| Software source | source 0..9 | Historical zero-based source ID; nominally L(source+1) |
| Optical slot | 0..19 | One source at one wavelength |
| PCA output | LED0..LED23 | PCA9956B sink-output number, not the package pin number |
| Optical channel | CH1..CH16 or SC1L..SC2R | A selected source-detector pair, with two wavelength measurements |

Always qualify “PD” with **physical** or **LiveView**. For example, physical PD1
and LiveView PD1 are different detectors. Likewise, main-board connector/module
label LED1, source L1, and PCA output LED1 are different namespaces.

## 3. Physical PD versus LiveView and ADS131M04

The historical mapping is a deliberate permutation, not simply subtracting one
from physical PD numbering. The owner attributes it to easier wiring (**O1**).

| Physical detector | Historical side | Distance from near source | OUT net | ADC pair | ADS symbol pins P / N | Historical LiveView label |
|---|---|---|---|---|---|---|
| PD1 | Left | L1: 5 mm | OUTPD1 | AIN2P / AIN2N | 5 / 6 | PD2 |
| PD2 | Left | L1: 8 mm | OUTPD2 | AIN3P / AIN3N | 7 / 8 | PD3 |
| PD3 | Right | L6: 8 mm | OUTPD3 | AIN0P / AIN0N | 1 / 2 | PD0 |
| PD4 | Right | L6: 5 mm | OUTPD4 | AIN1P / AIN1N | 3 / 4 | PD1 |

**Evidence:** physical PD→OUTPD→AIN and symbol pins are **S / High**, S1–S3.
The side, distance and LiveView assignment are **H / Medium**, H1. LiveView index
equals AIN channel number in this reconstruction, but an end-to-end software/
packet check on the actual LiveView version remains required.

Inverse lookup, useful when interpreting a raw detector vector:

| AIN channel / historical LiveView index | Physical detector | Side / near separation |
|---|---|---|
| 0 | PD3 | Right / 8 mm |
| 1 | PD4 | Right / 5 mm |
| 2 | PD1 | Left / 5 mm |
| 3 | PD2 | Left / 8 mm |

Conditional vector conversion (**D**, assuming a raw vector is ordered AIN0..3):

```text
physical order [PD1, PD2, PD3, PD4] = [raw[2], raw[3], raw[0], raw[1]]
```

Do not apply this conversion again if a decoder has already reordered detectors.
Do not infer ADC packet order merely from the displayed label.

### Input-filter trace from the screenshots

| Signal path to AINnP | Ground path to AINnN | Capacitor between P and N |
|---|---|---|
| OUTPD1 → R9 (1 kΩ) → AIN2P | GND → R29 (1 kΩ) → AIN2N | C43, 10 nF |
| OUTPD2 → R21 (1 kΩ) → AIN3P | GND → R28 (1 kΩ) → AIN3N | C22, 10 nF |
| OUTPD3 → R3 (1 kΩ) → AIN0P | GND → R6 (1 kΩ) → AIN0N | C2, 10 nF |
| OUTPD4 → R4 (1 kΩ) → AIN1P | GND → R5 (1 kΩ) → AIN1N | C1, 10 nF |

This is **S / High** for the drawn circuit. “Negative input grounded” means through
the shown resistor; it is not a claim that the ADC package pin is directly shorted
to ground. These ADC symbol pin numbers are not FPC/connector pin assignments.

## 4. Ten dual-wavelength source positions and geometry

There are ten nominal source positions, each with 735 nm and 850 nm: twenty
individually driven wavelength slots. Ten positions do not mean twenty independent
spatial source positions. S3 shows ten dual-wavelength LED symbols, S4 shows
source-specific wavelength nets, and F2/F3 encode wavelength pairs.

Historical layout grouping (**H / Medium**): L1–L5 left; L6–L10 right. L1/L6
provide the short-separation pairs; L2–L5 and L7–L10 provide the channels
historically called “long”. Left/right is the historical convention: wearer-view
versus observer-view, board face, orientation and Cartesian origin are **U**.
Schematic page placement is not physical PCB placement.

| Left source | To physical PD2 / LiveView PD3 | To physical PD1 / LiveView PD2 | Mirrored right source | To physical PD3 / LiveView PD0 | To physical PD4 / LiveView PD1 |
|---|---|---|---|---|---|
| L1 | 8 mm | 5 mm | L6 | 8 mm | 5 mm |
| L2 | 30 mm | 28.2 mm | L7 | 30 mm | 28.2 mm |
| L3 | 30 mm | 22.5 mm | L8 | 30 mm | 22.5 mm |
| L4 | 30 mm | 18.5 mm | L9 | 30 mm | 18.5 mm |
| L5 | 30 mm | 20.3 mm | L10 | 30 mm | 20.3 mm |

All distances and mirrored-right distances are **H / Medium**, H1; they are
historical specified separations, not measurements taken during this inspection.
The source labels are corroborated by the schematic; their locations are not.
No complete `(x, y, z)` coordinates, inter-source distances, angular orientation,
or cross-hemisphere separations can be recovered from these tables alone.

## 5. PCA9956B output mapping

S4 explicitly labels `L1_735`, `L1_850`, …, `L10_850` at the PCA outputs.
The JSON symbol provides the package pin numbers, and wire segments connect
those symbol endpoints to the net labels. F1–F3 use the same ordered output set.
Thus the mapping below has **S / High** confidence for the schematic and
**F / High** confidence for the default software ordering. Assembled module,
cable and emitted wavelength validation remains **U**.

| Source | Historical side | Software source | Slots 735 / 850 | PCA outputs 735 / 850 | PCA symbol package pins 735 / 850 | Schematic nets |
|---|---|---|---|---|---|---|
| L1 | Left | 0 | 0 / 1 | LED0 / LED1 | 6 / 7 | L1_735 / L1_850 |
| L2 | Left | 1 | 2 / 3 | LED2 / LED3 | 8 / 9 | L2_735 / L2_850 |
| L3 | Left | 2 | 4 / 5 | LED4 / LED5 | 10 / 11 | L3_735 / L3_850 |
| L4 | Left | 3 | 6 / 7 | LED6 / LED7 | 12 / 13 | L4_735 / L4_850 |
| L5 | Left | 4 | 8 / 9 | LED8 / LED9 | 15 / 16 | L5_735 / L5_850 |
| L6 | Right | 5 | 10 / 11 | LED14 / LED15 | 23 / 24 | L6_735 / L6_850 |
| L7 | Right | 6 | 12 / 13 | LED16 / LED17 | 26 / 27 | L7_735 / L7_850 |
| L8 | Right | 7 | 14 / 15 | LED18 / LED19 | 28 / 29 | L8_735 / L8_850 |
| L9 | Right | 8 | 16 / 17 | LED20 / LED21 | 30 / 31 | L9_735 / L9_850 |
| L10 | Right | 9 | 18 / 19 | LED22 / LED23 | 32 / 33 | L10_735 / L10_850 |

Side is H1; source/slot interpretation is F2/F3 plus correspondence to S4.
Active F1 independently proves slot→PCA output; it should not by itself be read
as an optical wavelength measurement.

```text
historical slot = 2 * source_id + wavelength_index
wavelength_index: 0 = 735 nm; 1 = 850 nm
slot_to_pca = [0,1,2,3,4,5,6,7,8,9,14,15,16,17,18,19,20,21,22,23]
```

PCA LED10, LED11, LED12 and LED13 (symbol pins 17, 19, 20 and 22) have no
source-wavelength net connection drawn in S4 and are omitted from the software
slot map. They are unused in this design/scan; this does not establish that they
are available on the assembled board. Package VSS pins interrupt numerical pin
sequences: output number is never a substitute for package pin number.

The earlier chat proposal “PCA0=L1_735 through PCA19=L10_850” was only a proposal
and is inconsistent with S4/F1–F3 after L5. Do not implement it. The recovered
schematic resolves the earlier conversation's lack of a design-level mapping;
it does not remove the physical verification requirement in LED_BRINGUP.md.
`CATH_735`/`CATH_850` are current-sink connections, not board ground (F4).

## 6. Optical channel reference

These channel names and distances are **H / Medium**, H1. Physical PD columns
are **D / Conditional**, obtained using section 3. Each row is one spatial
source-detector pair measured at both 735 and 850 nm.

| Channel | Source | Historical LiveView detector | Physical detector | Side | Separation |
|---|---|---|---|---|---|
| CH1 | L2 | PD3 | PD2 | Left | 30 mm |
| CH2 | L2 | PD2 | PD1 | Left | 28.2 mm |
| CH3 | L3 | PD3 | PD2 | Left | 30 mm |
| CH4 | L3 | PD2 | PD1 | Left | 22.5 mm |
| CH5 | L4 | PD3 | PD2 | Left | 30 mm |
| CH6 | L4 | PD2 | PD1 | Left | 18.5 mm |
| CH7 | L5 | PD3 | PD2 | Left | 30 mm |
| CH8 | L5 | PD2 | PD1 | Left | 20.3 mm |
| CH9 | L7 | PD0 | PD3 | Right | 30 mm |
| CH10 | L7 | PD1 | PD4 | Right | 28.2 mm |
| CH11 | L8 | PD0 | PD3 | Right | 30 mm |
| CH12 | L8 | PD1 | PD4 | Right | 22.5 mm |
| CH13 | L9 | PD0 | PD3 | Right | 30 mm |
| CH14 | L9 | PD1 | PD4 | Right | 18.5 mm |
| CH15 | L10 | PD0 | PD3 | Right | 30 mm |
| CH16 | L10 | PD1 | PD4 | Right | 20.3 mm |
| SC1L | L1 | PD2 | PD1 | Left | 5 mm |
| SC2L | L1 | PD3 | PD2 | Left | 8 mm |
| SC1R | L6 | PD1 | PD4 | Right | 5 mm |
| SC2R | L6 | PD0 | PD3 | Right | 8 mm |

“Long” here is the historical CH1–CH16 category, including separations below
30 mm; it is not a claim that all sixteen are 30 mm or clinically qualified.
There are 16 long + 4 short = **20 selected spatial channels**, hence **40 selected
wavelength-specific intensities**. This differs from 20 illumination slots.
If all four detectors are recorded for every slot, the raw frame contains
20 × 4 = 80 detector-slot values, including pairs outside the selected geometry.
F3 describes four simultaneous ADS channels per wavelength pair; raw acquisition
does not automatically assign the CH/SC labels or validate the geometry.

Example (**D**, conditional on unchanged slot and detector ordering):
`CH1_I735` selects slot 2 (L2, 735 nm), detector index 3 (physical PD2);
`CH1_I850` selects slot 3, detector index 3. `SC1R` selects slots 10/11
(L6), detector index 1 (physical PD4). These are lookup examples, not proof that
the current LED-only profile emits ADC intensity frames; F4 explicitly says it does not.

## 7. Remaining unknowns and how to close them

| Unknown / limitation | Evidence needed |
|---|---|
| Actual assembled hardware revision and match to the exported schematics | Board identifiers, assembly record and matching PCB/netlist revision |
| Physical L1..L10/PD1..PD4 coordinates and view convention | Annotated PCB layout with origin, units, face and wearer/observer orientation |
| Historical distance accuracy and right-side symmetry | Original geometry file/design drawing and measured separation tolerances |
| Complete LiveView mapping and any decoder permutation | Exact LiveView revision, full fnirs-geometry.js, packet decoder and detector-order test evidence |
| Cable/module mapping and correct 735/850 emitter at each position | End-to-end connection record, unpowered continuity checks and authorized wavelength-resolved bench evidence |
| FPC and external LED-module pin assignments as assembled | Matching connector drawings, cable orientation and continuity record; ADC/PCA symbol pins above are insufficient |
| Whether historical custom PCA maps were selected | Actual historical configuration and runtime map capture |
| Optical cross-talk, gains, settling, dark subtraction and signal polarity | Applicable acquisition configuration and measured electrical/optical evidence |
| Reuse in a future board | Explicit design decision and revision-specific acceptance criteria |

No PCB-coordinate, cable-pin or unobserved LED mapping is supplied by this document.
Before promoting a mapping to assembled-hardware verified status, preserve the board,
source/configuration and instrument identities plus the observations. Any energized
test needs the repository's approved procedure and owner authorization; this reference
is not such a procedure.

## 8. Scope, validation and review status

- Scope: one documentation reference covering the two screenshots, repository
  schematics, static software maps and historical optical geometry.
- Exclusions: firmware changes, reference snapshot edits, build/flash, hardware
  tests, new pin assignments, new optical limits and clinical claims.
- Acceptance criteria: distinguish all numbering domains; trace every asserted
  electrical map to S1–S4/F1–F3; label historical geometry; list unresolved evidence;
  preserve historical snapshot integrity.
- Preparation: Codex documentation author, requested by the repository owner.
  Formal project role assignment and independent review remain pending; this is
  not a Hardware Specialist or independent Verification Agent attestation.
- Static checks: both screenshots and both full schematic PNGs inspected;
  main-board EasyEDA symbol pins/net labels/wires compared with the PCA tables;
  all twenty PCA source/wavelength nets also checked against direct JSON wire
  endpoints and symbol package pins; local documentation links checked (11, none
  missing); reference integrity verified before and after the documentation edit
  (319 files). Publication/PR records capture the final commit and check results.
- Not run: firmware build, host acquisition tests, on-target bench, HIL, optical
  metrology and physical continuity tests. They are not established by this document.
- Required next review: hardware reviewer checks schematic interpretation and
  revision applicability; independent reviewer checks provenance and label scope.
  Existing merge checks and human approval remain applicable.
- Main risk: historical names/distances or design-level mappings being mistaken
  for measured assembled-board facts. Retain the confidence labels when quoting tables.
- Recovery: revert the documentation commit through the normal review workflow;
  no device or runtime setting is changed.
