# PCA9956B SHORT policy review: all connected channels

Date: 2026-10-27. Status: analysis and concrete proposal, NOT an enabled firmware
policy or an approved electrical conclusion. Tested historical references remain
V2 and V3 only. No driver, current, timing or protection setting is changed here.

## Owner-reported evidence

Boot9, Fs5, configured30mA: the Owner observes light briefly, followed by fault267.
Cached STATUS: MODE2=0x45, led_error264, frames0, edges39, missed0,
work maximum1599us, valid mask0xFFC3FF, SHORT mask0xFFC3FF, OPEN0, invalid0.
Merged EFLAG bytes are55 55 05 50 55 55. All20 connected channels were sampled;
the four unused channels are not sampled. Stack: main3172, LED4664; bus released
and reset requested after fault. These are human-reported bench values, not an
agent measurement or complete waveform. Image/source hashes were not captured
with this run. No reset occurred within the shown boot identity.

The software trigger is now established: end-of-first-frame MODE2 ERROR causes
the new health check to reset and latch the optical fault. It is not host lease
expiry or a recorded deadline miss. It does not establish twenty physical shorts.

## Primary sources and nominal-voltage explanation

- [NXP PCA9956B Rev1.2 sections7.3.14.2 and DC characteristics](https://www.nxp.com/docs/en/data-sheet/PCA9956B.pdf):
  SHORT is inferred from the sink output voltage. The stated trip characteristic
  at Rext1k is2.7V minimum/2.85V typical; no maximum is specified in that table.
- [Ushio SMT735D/850D,2024.07](https://www.ushio.co.jp/jp/products/product_file/file/SMT735D_850D.pdf):
  at50mA/Tc25C, typical VF is2.0V for735nm and1.5V for850nm, maximum2.4/2.0V.
  These are not measured VF at the current30mA bench setting. The local LED PDF
  revision/part and actual assembly still need to be checked for qualification.
- Current EasyEDA SCH_Thesis_main_2026-10-16.json: nominal RAIL_5.3V_LED labels
  gge38632 and gge47264/gge47328/etc feed the optical source supply. This is a
  schematic value, not a measured rail for the reported run.

For a constant-current low-side sink, ignoring wiring drops, VO=V_LED-VF.
Illustrative nominal values are5.3-2.0=3.3V and5.3-1.5=3.8V, both above2.85V.
This makes detector incompatibility with a high LED supply/low-VF IR emitter a
strong hypothesis for the uniform SHORT flags and observed light. It is NOT
proof of a false indication, a safe current/temperature, or correct assembly.
Simultaneous uniform flags alone cannot rule out a shared electrical fault.

V2/V3 use the writable MODE2 configuration mask0x28, separate from live status.
The added current health policy treats ERROR as fatal. Successful historical
operation is consistent with the different policy; it does not prove the
historical hardware never asserted SHORT or qualify thermal/optical exposure.

## Options for a human decision and specialist review

### Output voltage and dissipation assessment

Exceeding the SHORT comparator threshold is not, by itself, an output-voltage
damage limit or a command to increase LED current. Within the driver's operating
and thermal limits, IREF and REXT still set the sink current; the higher sink
voltage increases dissipation. NXP section7.3.14.2 recommends turning outputs
off after SHORT/OPEN detection. Choosing a different software response therefore
needs a documented board-specific justification, rather than interpreting the
flag as an electrical continuity measurement or erasing ERROR globally.

For the illustrative nominal VO values above and requested30mA:

| Quantity | VO=3.3V | VO=3.8V |
| --- | ---: | ---: |
| Instantaneous sink power, VO x0.030A | 99mW | 114mW |
| Individual channel average at1/40 duty | 2.475mW | 2.850mW |
| Aggregate sink average if every slot had this VO, at1/2 duty | 49.5mW | 57mW |

The current command is nominally29.925mA after DAC quantization, but30mA is used
here for readability; it is not a tolerance bound. The active core schedules
40equal phases per frame:20one-hot ON phases and20OFF phases. At Fs5, a frame
is200ms and each phase5ms. Each physical output is commanded ON for5ms per
200ms, while some output is commanded ON for half the frame. Increasing Fs
shortens the pulses without changing these ideal duty ratios. These are command
timing calculations, not oscilloscope evidence: I2C execution latency changes
actual edges and on-time. Do not multiply the one-hot average by20 again.

An illustrative equal mix of the two nominal voltages gives sink average
53.25mW. This excludes VDD/internal logic consumption, I2C losses, supply/current
tolerances, transients and fault behavior. The junction estimate is
Tj=Tamb+RthetaJA*(P_internal+P_sink); NXP's33.9C/W example is for its specified
multilayer reference PCB, not an established thermal resistance for this board
or enclosure. Low nominal sink dissipation makes excess headroom a credible
explanation; it does not qualify board temperature, optical exposure or a
protection bypass. Turning all20outputs ON simultaneously would instead produce
roughly2.13W of sink dissipation for the same nominal mix, before internal power,
so the one-hot invariant is material to this assessment.

Disposition: a persistent SHORT-only advisory policy is a plausible design
candidate for the documented high-headroom operating point. Its activation is
not established safe by this static calculation or historical successful light
output. No runtime policy was changed for this assessment. Existing electrical
or thermal records and Hardware review may supply the missing evidence without
opening the assembled board. ADR-0006 is a design proposal only; neither a2s
limit nor successful host tests would independently establish a safe exposure.

1. Retain strict fault behavior and qualify/adjust the electrical operating point
   so a healthy LED's sink voltage stays below the applicable trip threshold
   while maintaining driver compliance and LED current. This can require hardware
   changes on this fixed board. No rail/current value is selected by this note.
2. Propose an explicitly gated board-compatibility bench policy after the voltage
   mechanism is checked. Expected SHORT indications would remain reported as a
   persistent warning instead of ending a bounded bench scan. This relaxes one
   automatic shutdown response and can mask a genuine short; it is not just a
   register-decoding correction. No such code/flag is enabled by this document.

For option2 the reviewable acceptance criteria would be:

- Default production/ordinary profile stays strict. A separately named bench
  setting and visible INFO/USB warning identify the changed policy; no silent
  change to the0xE8 mask and no clearing ERROR to conceal it.
- Only the documented connected-channel SHORT case is eligible. OPEN, invalid
  EFLAG code, missing/stale diagnostic evidence or an unexplained aggregate
  ERROR remain faults. OVERTEMP, configuration/readback mismatch, I2C failure,
  deadline miss, explicit off and host expiry remain hard stops.
- Keep Fs5/default30mA, one-hot20-slot scan and a bounded run; no increase in
  power or relaxed timing. The policy cannot distinguish a real short from
  the hypothesized normal high sink voltage without independent measurements.
- Hardware reviewer assesses supply and sink voltage/current/thermal limits;
  RTOS reviewer assesses the diagnostics/timing; independent review and specific
  Owner authorization cover the procedure before energization. A nominal-data
  calculation is not a substitute for this evidence.

Required evidence is a reviewed measurement of the actual LED rail and ON-state
sink/current behavior for both wavelengths, with suitable pulse instrumentation
and a defined safe fixture/energy-isolation method. A multimeter's averaged value
is not the ON-state voltage of5ms slots. CATH pins are constant-current sinks,
not ground. Do not short them to ground or request ad-hoc live probing.

Current firmware remains fail-closed while this decision is unresolved. Existing
cached evidence can be read without starting a new scan. No repeated clear/start,
new flash, energized operation, external write, commit or merge was performed by
the agent. Accountable activity here is static diagnosis; Hardware/RTOS and
independent review are pending. No code tests apply to this documentation-only
addition; reference integrity is checked separately.

## Software implementation update (2026-10-27)

The Owner's [recorded handoff](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6439754275)
authorizes implementation/testing now, without new physical measurements or
board disassembly. No historical measurement records are available. Earlier
statements in this note that runtime policy has not been implemented describe
the prior assessment. The optional candidate now exists, disabled by default;
see [ADR-0006](adr/0006-bounded-short-warning-bench.md) and the
[implementation handoff](SHORT_ADVISORY_HANDOFF.md). Existing strict/root config
is not silently changed. The high-headroom premise remains unmeasured and a
matching real short remains a residual risk. Physical activation is not approved.

NXP's PWM<8 diagnostic exclusion is addressed by PWMALL255/readback during
prepare, while LEDOUT FULL_ON and IREF current stay unchanged. EFLAG sampling
moves to the end of the existing ON interval before OFF; frame scratch cannot
borrow prior-frame evidence. OPEN/code3 immediately requests reset on observation.
The updated classifier and cap are software mechanisms, not electrical findings.
