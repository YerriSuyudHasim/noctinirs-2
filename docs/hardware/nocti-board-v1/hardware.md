# Nocti board v1 — hardware contract inputs

> **Evidence class R (documentation/reference).** Nothing here was measured. **Hardware status: NOT RUN.**
> This file feeds issue #19 but does not by itself satisfy it. It does not authorise flashing, hardware tests or real emitter/ADC adapters.

Fact labels: **[V]** verified from a file in this folder (schematic PNG, BOM CSV) or the repo; **[O]** Owner statement (2026-10-16); **[I]** inferred (reading or reasoning; needs confirmation); **[U]** unknown.

Schematic reading caveat: the schematics are low-resolution PNG exports (EasyEDA), not netlists. Net-label-to-pin assignments marked [V] were read at zoom. Where the label placement was ambiguous they are marked [I]. A native EasyEDA project or netlist export would turn these into [V].

## 1. Provenance

| Item | Value |
|---|---|
| Board name / revision | **Nocti board v1** — the only board that exists [O] |
| Same board as V2–V4 firmware targets ("Noctic_v1") | Yes, per Owner (2026-10-16) [O] |
| Supplied by | Owner (YerriSuyudHasim), chat attachments, 2026-10-16 [O] |
| Main board schematic | `schematic-main-board.png`, EasyEDA, "Sheet_1", REV 1.0, dated 2025-11-13 [V title block] |
| Optode/sensor board schematic | `schematic-optode-board.png`, EasyEDA, REV 1.0, dated 2025-12-22 [V title block] |
| PCB | `pcb-main-front-3d.png`: 3D render of the main board front only [O]. No Gerbers, no back side, no layout files [U] |
| BOMs | `bom-main-board.csv` and `bom-optode-board.csv`. Both are EasyEDA exports, **UTF-16LE, tab-separated**, committed byte-for-byte [V] |
| Checksums | `SHA256SUMS` in this folder |
| Datasheets in repo | `Datasheet/` on main: ADS131M04, PCA9956B, SMT735D_850D-1 (Marubeni LED), ESP32-S3 datasheet + TRM [V] |

## 2. Block summary

### Main board ([V] schematic + main BOM unless marked)
- **MCU:** U1 **ESP32-S3-WROOM-1U-N16R8**. The schematic symbol says "1U", which is the external-antenna variant [V schematic]. The Owner confirms N16R8 [O]. The physical module marking has not been photographed [U].
- **Power path:**
  - USB-C (TYPE-C-31-M-12) with SP0504 ESD.
  - **BQ24295** charger (U10), I2C, with CE# from the MCU. R30 pulls CE_BQ **up** (P20-H6) [V schematic, per Agent 04 review]. With GPIO8 undriven, CE# is therefore high, i.e. charging is disabled by default [I, from BQ24295 CE# active-low; needs datasheet confirmation].
  - Battery connector CN1: JST B2B-XH-2.
  - Cell protection: **DW01A** (U7) + **FS8205A** dual MOSFET (Q1).
  - Fuel gauge: **MAX17048** (U9).
  - Slide switch SW3 (SS-12D02) feeds net_SYS → SYS_Out [V schematic].
  - Battery: **LiPo 1-cell 3.7 V** [O]. The cell is not fixed: the Owner swaps between cells of different capacity (e.g. 1200 mAh and larger) [O, 2026-10-16]. The Owner reports normal operation with all of them because the power path feeds the buck-boost and the LDOs [O, unverified electrically]. Part number and protection thresholds are [U].
- **Rails:**
  - **TPS63070** buck-boost (U2) → **RAIL_5.3V**. FB divider R2 = 560 kΩ / R5 = 100 kΩ [V]. With the TPS63070's 0.8 V reference that gives ≈5.28 V [I]. The block is titled "Buck-Boost 5.3 V rail" [V].
  - **TPS7A2133** 3.3 V **LDO** (U3) → Rail_3.3V (digital) [V BOM]. This block is an LDO, not a buck converter (P20-H7). The schematic block title "Buck converter 3.3V digital rail" is a mislabel on the drawing [V].
  - **TPS7A2050** 5 V LDO (U4) → Rail_5V_Analog (for ADS1299) [V].
  - SW5 selects the module 3V3 feed between J_PSource and Rail_3.3V [V schematic]. Purpose [O, 2026-10-16]: it selects the ESP32 power input. The normal position is the main rail (Rail_3.3V); the other position takes power from a jumper (J_PSource) and is for emergency use only.
- **LED driver:** **PCA9956BTW** (U8), I2C (see §5).
- **Biopotential front end:** **ADS1299IPAGR** (U5) with EOG/EMG/EEG RC filter networks. Present, and to be used in future [O]. **Out of 0.1.x scope.**
- **Connectors:**
  - FPC1: 16-pin 0.5 mm, to the optode board.
  - Electrode pads.
  - LED1–LED10 "PAD" footprints for emitter wiring (see §5).
- **User I/O:** SW1 = EN/reset, SW2 = IO0/BOOT (TS-1088 tact switches) [V]. LED_PWR and LED1_STAT (red 0603) [V BOM]. LED_PWR is on SYS_Out via R12 2.2 kΩ [V schematic]. LED1_STAT drive net [U].

### Optode / sensor board ([V] schematic + optode BOM unless marked)
- **Supply:** SYS_Out arrives over FPC2 (pins 14/15). **TPS7A2033** 3.3 V LDO (U3) → Rail_3.3A. Ferrites L1/L2 (600 Ω) give Rail_3.3D / Rail_3.3D2 [V].
- **ADC:** **ADS131M04IRUKR** (U8), 4-channel, 24-bit.
- **Clock:** X2 **Epson SG-8018CE 8.192 MHz** active oscillator [V BOM: "SG-8018CE 8.192000MHz TJHSA"]. OE is pulled up by R27 10 kΩ to Rail_3.3D, and the output goes through R26 33 Ω to net CLK8192 → ADS131M04 CLKIN [V schematic, label reading I]. Matches the V4 firmware's "8.192 MHz board-supplied" [V].
- **Photodetectors:** PD1–PD4 **Vishay VEMD8082** [V BOM].
- **TIAs:** U1, U4 **OPA2380AIDGKR** (dual, 4 channels) [V BOM].
- **Emitters:** LED1–LED10 **SMT735D** symbols (pins VCC / 850 / 735 / NC) are on the optode sheet. **No emitter part appears in either BOM** [V: absent], and the symbols show no wires on the sheet [I]. The Owner says the LEDs are Marubeni; the datasheet is `Datasheet/SMT735D_850D-1.pdf` [O/V].
  - [O, 2026-10-16] The LEDs are not in the BOM because the Owner already had them and did not buy them with the board. They are connected off-board by a **3-wire twisted cable: VCC, GND_735, GND_850**. Cable length and exact mounting are [U]; this is an Owner statement, not electrically verified.

## 3. NN2 pin map — schematic vs V4 firmware

V4 column = `reference/` V4 `board.h` (read-only; per the 2026-10-16 hardware study). "Match" means the schematic and the firmware agree.

| GPIO | Module pin | Schematic net | V4 firmware | Match |
|---|---|---|---|---|
| 0 | 27 | IO0: SW2 to GND, C21 0.1 µF, R13 10 kΩ pull-up to Rail_3.3V (BOOT) [V] | not used | n/a (strapping) |
| 3 | 15 | not connected [V] | not used | ✓ |
| 45 | 26 | not connected [V] | not used | ✓ |
| 46 | 16 | not connected [V] | not used | ✓ |
| 6 | 6 | fNIRS_RESET (ADS131M04 SYNC/RESET#) [V] | ADS131M04 SYNC/RESET# | ✓ |
| 7 | 7 | fNIRS_CS via R47 33 Ω [I: label placement] | ADS131M04 CS# | ✓ [I] |
| 15 | 8 | net_SDA [I: label placement] | I2C SDA | ✓ [I] |
| 16 | 9 | net_SCL [I: label placement] | I2C SCL | ✓ [I] |
| 17 | 10 | no net (stub) [I] | not used | ✓ |
| 18 | 11 | RESET_LED (PCA9956B RESET#) [V] | PCA9956B RESET# | ✓ |
| 8 | 12 | CE_BQ (BQ24295 CE#) [V] | **not defined/driven in V4** (V2 drives it) | ⚠ firmware gap |
| 19 / 20 | 13 / 14 | USB D− / D+ [V] | USB-Serial-JTAG console | ✓ |
| 35 | 28 | fNIRS_DRDY [V] | ADS131M04 DRDY# | ✓ — **PSRAM conflict, §4** |
| 36 | 29 | fNIRS_SCLK via R48 33 Ω [V] | SPI3 SCLK | ✓ — **PSRAM conflict** |
| 37 | 30 | fNIRS_MISO [V] | SPI3 MISO | ✓ — **PSRAM conflict** |
| 38 | 31 | fNIRS_MOSI via R49 33 Ω [V] | SPI3 MOSI | ✓ |
| 10 | 18 | DRDY#_ADS (ADS1299) [V] | not in V4 (V2: ADS1299 DRDY#) | ✓ vs V2 |
| 11 | 19 | ADS_MISO [V] | – (V2 ✓) | ✓ vs V2 |
| 12 | 20 | ADS_SCLK via R10 33 Ω [V] | – (V2 ✓) | ✓ vs V2 |
| 13 | 21 | ADS_CS [V] | – (V2 ✓) | ✓ vs V2 |
| 14 | 22 | ADS_START [V] | – (V2 ✓) | ✓ vs V2 |
| 21 | 23 | ADS_RESET [V] | – (V2 ✓) | ✓ vs V2 |
| 47 | 24 | ADS_PWDN [V] | – (V2 ✓) | ✓ vs V2 |
| 48 | 25 | ADS_MOSI [V] | – (V2 ✓) | ✓ vs V2 |
| 43 / 44 | 37 / 36 | TXD0 / RXD0 → net_TX / net_RX [V] | not used (console is USB) | n/a |
| 1, 2, 4, 5, 9, 39–42 | – | not connected [V/I] | not used | ✓ |

**Result:** no pin conflicts found between the schematic and the V4/V2 firmware maps.
- Three rows (GPIO7/15/16) are [I] because of label placement in the PNG.
- The only gap is that the V4 firmware does not drive **GPIO8 CE_BQ**. CE# is pulled up by R30 (P20-H6).

**FPC (main FPC1 ↔ optode FPC2), 16-pin** [V optode schematic]:
- Even pins 12/10/8/6/4/2 carry fNIRS_RESET / CS / DRDY / SCLK / MISO / MOSI.
- Pins 14/15 carry SYS_Out.
- Odd pins 1–13 and 16/17/18 (shell) are GND.

## 4. PSRAM finding (N16R8 vs GPIO35–37)

- [V] The module is ESP32-S3-WROOM-1**U-N16R8**, and GPIO35/36/37 are wired to the ADS131M04 DRDY/SCLK/MISO lines.
- [I, Espressif module datasheet; not yet quoted from `Datasheet/`] On R8 (octal PSRAM) modules, GPIO35–37 are connected internally to the PSRAM. Espressif lists them as unavailable for other uses.
- [O] The Owner confirms that the pins are used, so **PSRAM cannot be used**.
- **Contract consequences:**
  - (a) `CONFIG_SPIRAM` must stay disabled. This should be enforced, for example by a build check.
  - (b) GPIO33/34 must stay unused.
  - (c) The PSRAM die is still physically on those lines. Correct SPI behaviour therefore depends on the PSRAM staying deselected/high-Z. This is **unverified** [U]. No V4 logs exist that show the SPI link working [O: "belum"].
- The Agent 04 board-revision decision should record this as an accepted deviation, or require a module change (N16 / N8, no octal PSRAM) on a future revision.

## 5. Emitter path (PCA9956B)

- **OE#: tied directly to GND** (pin 5, together with AD0–AD2) [V schematic]. The outputs are therefore **always enabled in hardware**. There is no MCU-controlled or hardware output-disable.
- **RESET# (P20-H1, corrected):** R46 is a **10 kΩ pull-up** from RESET_LED to Rail_3.3V, not a pull-down [V schematic, per Agent 04 review]. With GPIO18 undriven (boot ROM, reset, crash before GPIO init), the PCA9956B is therefore **out of reset and active**.
- **Hardware "off" therefore rests only on** the PCA9956B's power-on defaults (PWMx = 0 / LEDOUTx = 0, outputs off) until firmware writes the registers. A crash after firmware has enabled outputs leaves the last register state in place. There is no hardware path that forces the emitters off.
- **Owner position [O, 2026-10-16]:** Nocti board v1 has no hardware emitter disable. This is recorded as **known deviation P20-H2**, to be fixed in **Nocti board v2**, including considering replacing the LED driver IC.
- **Gap against issue #19 (still open):** #19 requires the emitter to be off by default in hardware through reset, boot and crash. Recording P20-H2 as a known deviation is **not** an accepted safety waiver. The #19 hardware-off criterion remains open and unmet on this board.
- **I2C address:** AD0–AD2 to GND gives 0x01, matching the firmware [V].
- **REXT = R4 1 kΩ** to GND [V schematic + main BOM: R4 1 kΩ RT0603BRD071KL, 0.1 %]. This matches V4 `board.h` [V].
  - Full scale ≈ 57 mA per channel [I, computed from the firmware formula].
  - The firmware cap of **50 mA** is Owner-approved [O]. Whether it is within the SMT735D datasheet ratings has not been extracted [U].
- **LED supply:**
  - LED PADs: VCC/VCC2 go to **RAIL_5.3V_LED**, cathodes go to PCA9956B channels, and SG goes to GND [V schematic].
  - RAIL_5.3V_LED comes from the TPS63070 RAIL_5.3V through a ferrite filter [I: low-res block].
  - There is no separate LED-rail enable [I]. The LED anodes are powered whenever the buck-boost runs.
- **Channel map:**
  - PCA LED0…LED9 → L1_735, L1_850 … L5_735, L5_850 [V/I label placement].
  - LED10, LED11 and the right-hand bank (LED12–23): wires go to sources L6–L10 [V wires], but individual labels are unreadable in the PNG [U].
  - V4 firmware uses channels 0–9 and 14–23 and leaves 10–13 unused. LED10/11 at least look unconnected [I], which is consistent.
  - Full confirmation needs a netlist.

## 6. Detection path (optode board)

> P20-H5: the PD→AIN mapping below was read from the PNG. The Owner has now supplied native EasyEDA sources (§9 Q1; not yet committed); the mapping must be re-checked against them by Agent 04. Status: **open**.

| PD | TIA | Feedback R / C | Output RC | Net | ADS131M04 input (1 kΩ/1 kΩ + 10 nF differential filter) |
|---|---|---|---|---|---|
| PD1 | U1.1 | R7 100 kΩ / C17 0.5 pF | U7 100 Ω, C18 100 pF | OUTPD1 | **AIN2P** (AIN2N → GND) |
| PD2 | U1.2 | R8 100 kΩ / C41 0.5 pF | U11 100 Ω, C42 100 pF | OUTPD2 | **AIN3P** (AIN3N → GND) |
| PD3 | U4.1 | R1 100 kΩ / C5 0.5 pF | U5 100 Ω, C6 100 pF | OUTPD3 | **AIN0P** (AIN0N → GND) |
| PD4 | U4.2 | R2 100 kΩ / C7 0.5 pF | U6 100 Ω, C8 100 pF | OUTPD4 | **AIN1P** (AIN1N → GND) |

- [V] BOM: 100 kΩ ×4 (R1, R2, R7, R8), 0.5 pF ×4, 100 pF ×4, 100 Ω ×4, 1 kΩ ×8, 10 nF ×4. OUTPDx → AINx routing [V schematic].
- [I] The exact PD→TIA→ref-des pairing is read from the low-res PNG.
- **All 4 ADS131M04 channels carry detectors.** Note that the physical PD numbering does not equal the ADC channel number (PD1 = CH2, PD3 = CH0).
- The TIA non-inverting bias reference has not been resolved from the PNG [U].
- The ADS131M04 digital lines on the optode side have pull resistors: R50 and R10 are 10 kΩ pull-ups to Rail_3.3D2 on fNIRS_RESET and fNIRS_CS; R11 and R12 are 47 kΩ pull-downs to GND on fNIRS_SCLK and fNIRS_MOSI [I].

## 7. Strapping, buttons, indicators

- **GPIO0:** 10 kΩ pull-up, BOOT button SW2 to GND, 0.1 µF [V].
- **GPIO3, GPIO45, GPIO46:** not connected, so module defaults apply [V/I].
- **EN:** R6 10 kΩ + C15 1 µF RC, and SW1 reset button [V].
- **LED_PWR:** on whenever SYS_Out is present [V]. LED1_STAT driver net [U].

## 8. Future-scope parts (present, not 0.1.x)

ADS1299 (SPI2 on GPIO10–14/21/47/48), BQ24295 and MAX17048 are all populated, and the Owner plans to use them later [O]. No 0.1.x work.

## 9. Open questions

1. ~~Native EasyEDA project~~: the Owner supplied two EasyEDA JSON sources on 2026-10-16 ("Thesis_main" and "PD_board"). They are **not yet committed** to `easyeda/` (transfer pending). Once committed, the [I] readings (GPIO7/15/16, PCA channel map, PD pairing, P20-H5) can be re-checked.
2. Physical module marking photo (N16R8) and back-side PCB render/photo.
3. ~~OE# = GND / RESET_LED pull~~: answered. R46 is a 10 kΩ pull-up (P20-H1), so RESET#-low is **not** a hardware off. Owner: no hardware emitter disable on v1; fix in Nocti board v2 (P20-H2, known deviation, not a waiver; #19 stays open).
4. Is RAIL_5.3V_LED switchable? Is there any way to cut LED anode power?
5. ~~SMT735D missing from BOMs~~: answered [O]. Owner already had the LEDs; connected off-board by a 3-wire twisted cable (VCC, GND_735, GND_850). Cable length/mounting still [U].
6. SMT735D absolute maximum ratings (DC/pulse current, duty) to set beside the approved 50 mA cap.
7. ~~BQ24295 CE# default~~: R30 pulls CE_BQ up (P20-H6). Datasheet confirmation of the resulting charge-disable default is still needed.
8. TIA bias/reference node and expected photocurrent range (gain = 100 kΩ).
9. Battery: 1S LiPo 3.7 V, capacity varies (1200 mAh and larger) [O]. Part number and protection thresholds still [U].
10. ~~SW5 purpose~~: answered [O]. Main rail normally; jumper input for emergency only.
11. No measurement logs exist for V4 "HARDWARE_VERIFIED" [O]. That claim stays unverified.
12. Agent 04: does the module's octal PSRAM stay high-impedance on GPIO35–37 with `CONFIG_SPIRAM=n`? Needs the module datasheet, Espressif guidance or the Owner. A passing text check does not settle it.

## 10. Review finding IDs (PR #20)

| ID | Topic | Status in this file |
|---|---|---|
| P20-H1 | R46 is a 10 kΩ pull-up on RESET_LED; PCA9956B active with GPIO18 undriven | Fixed (§5, §9 Q3) |
| P20-H2 | No hardware emitter disable (OE# = GND) | Known deviation, fix in Nocti board v2; **not a waiver**, #19 open |
| P20-H3 | Agent 04 finding as recorded in the PR #20 review | See PR #20 review |
| P20-H4 | Agent 04 finding as recorded in the PR #20 review | See PR #20 review |
| P20-H5 | PD→AIN channel map from low-res PNG | Open; re-check against EasyEDA sources |
| P20-H6 | R30 pulls CE_BQ up | Fixed (§2) |
| P20-H7 | 3.3 V digital block is TPS7A2133 LDO, not a buck | Fixed (§2) |

Owner answers recorded on 2026-10-16 are Owner statements [O] and have not been verified electrically. Hardware status: NOT RUN.
