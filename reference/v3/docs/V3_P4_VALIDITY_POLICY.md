# Prompt 4 Representative and Validity Policy

## Representative method

The method is **arithmetic mean of the latest contiguous integrity-valid tail,
up to K target**. It is retained from v16 because v16 explicitly avoided
backfilling across CRC failure or a missing DRDY, thereby preserving one compact
optical time interval rather than mixing older optical states.

For each stable ON and stable `I_LED_OFF` window:

- search newest to oldest;
- include only ISR timestamps inside the half-open window;
- require matching session epoch;
- stop at the first SPI, CRC, ADS-status, or DRDY-sequence break;
- stop when K target=10 has been collected;
- average with 64-bit accumulators;
- do not duplicate or backfill samples.

A bad newest sample may therefore yield zero used samples even when older
samples exist in the same window. This is deliberate integrity behavior.

## Slot validity

| Quality | Exact condition | `I_corrected` usable? |
|---|---|---|
| `VALID_TARGET` | `n_on_used >= 10` and `n_i_led_off_used >= 10` | yes |
| `VALID_MINIMUM` | both counts >=8, with one/both below 10 | yes, marked minimum-valid |
| `PARTIAL` | timing valid and at least one usable sample exists, but one/both counts <8 | no |
| `INVALID` | invalid timing, staging overflow, or zero usable samples in both windows | no |

`underk_on/off` means below K minimum. `subtarget_on/off` means K minimum was met
but K target was not met.

## Frame validity

| Quality | Exact condition |
|---|---|
| `VALID` | all 20 slots are target/minimum valid and all ten source wavelength pairs are present |
| `PARTIAL` | no missing/invalid slot or pair, but at least one slot is partial |
| `INVALID` | any missing/invalid slot, missing pair, timing failure, duplicate slot, stop/recovery truncation, or internal ownership error |

A frame may contain `VALID_MINIMUM` slots and remain `VALID`; masks preserve the
difference between target and minimum quality for the host.

## Wavelength pairing

Slots are immutable pairs per source:

```text
source 0: slots 0/1   -> 735/850
source 1: slots 2/3   -> 735/850
...
source 9: slots 18/19 -> 735/850
```

Each pair stores corrected values for all four simultaneous ADS channels. Host
geometry mapping can expose these as wavelength-specific channels such as
`CH1_I735` and `CH1_I850`. Prompt 4 does not compute HbO/HbR.
