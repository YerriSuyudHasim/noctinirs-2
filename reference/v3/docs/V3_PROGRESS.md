# NoctiNIRSv3 Progress — Final Source Delivery

```text
Firmware: NoctiNIRSv3 3.4.3
Phase:    P5_LOGGING_PARITY
Input:    NoctiNIRSv3_P4_Optical_Processing.zip
```

## Completed source work

- Prompt 1 bootstrap/contract/migration.
- Prompt 2 common timebase, minimal DRDY ISR, ADS task, rate validation, raw ring.
- Prompt 3 nonblocking PCA path, completion callback, absolute scheduler,
  persistent payloads, fault recovery.
- Prompt 4 half-open timestamp classification, representative values, quality,
  20-slot frame assembly, wavelength pairing, completed-frame ownership.
- Prompt 5 decoupled UDP stream task, network drop/backpressure policy,
  compatible stream-v1 encoding, final command set, separate online states,
  task/core/priority and sdkconfig cleanup, health logger, validation tooling.
- Bounded network-task cleanup on partial initialization.

## Verified in the generation environment

```text
Host static review: 11/11 translation units pass
Flags: -std=gnu11 -Wall -Wextra -Werror -Wformat=2 -fsyntax-only
Stream compatibility test: pass
Python tool compilation: pass
Archive integrity: generated at final packaging step
```

## Not verified

- ESP-IDF 5.5.1 target compile/link/map.
- Flash/boot on NoctiBoard.
- GPIO35–37 physical compatibility.
- Actual 8 kSPS, CRC/status, task/callback core, stack and WCET.
- K=10/10, frame validity, scheduler timing, Wi-Fi stress, I2C recovery,
  100-cycle lifecycle, or long soak.

## Final status taxonomy

```text
Source implementation complete: YES
Static review verified:          YES
Build verified:                  NO
Hardware partially verified:     NO
Acceptance verified:             NO
```


## Maintenance patch 3.4.1 — serial diagnostics and SPI acquire

Status: source patch complete; host static checks pass; ESP-IDF hardware build pending.

- [x] Remove unsupported finite wait from `spi_device_acquire_bus()`.
- [x] Preserve dedicated ADC task ownership and release-on-STOP policy.
- [x] Restore explicit NoctiNIRSv3 INFO-tag visibility.
- [x] Add AP/station/network/peer/stream fields to periodic health output.
- [x] Add DHCP-assigned Wi-Fi event log.
- [x] Match image header to 16 MB flash.
- [ ] Run `idf.py fullclean build` with ESP-IDF 5.5.1.
- [ ] Flash hardware and verify the SPI error is absent.
- [ ] Verify ADS startup response/rate and Wi-Fi station logs.


## Maintenance patch 3.4.2 — serial quiet forced OFF

Status: source patch complete; host checks pending/recorded in `V3_3_4_2_HOST_CHECK.txt`; target hardware validation pending.

- [x] Remove the unconditional `printf()` console probe.
- [x] Define `NOCTI_V3_RUNTIME_SERIAL_QUIET=0`.
- [x] Call `runtime_serial_set_quiet(false)` before normal application logs.
- [x] Reset the wildcard runtime filter to INFO instead of only setting selected project tags.
- [x] Preserve no-log rules for ISR/callback/hot paths.
- [ ] Clean-build and flash with ESP-IDF 5.5.1.
- [ ] Verify `serial_quiet_policy=0 serial_quiet_active=0`.
- [ ] Capture the first ADC and PCA startup error after INFO logs are restored.


## Maintenance patch 3.4.3 — V2/V3 logging parity

Status: source patch complete; host checks pass; target hardware verification pending.

- [x] Audit V2(16) and V3.4.2 compile-time, runtime, console, Wi-Fi, and UDP logging paths.
- [x] Prove V2 quiet mode begins only at START and is not the cause of absent boot/Wi-Fi logs.
- [x] Remove V3 quiet helper/state rather than forcing a boolean false.
- [x] Require INFO compile ceiling, dynamic/master log control, UART0, 115200, and no secondary console.
- [x] Restore master/default/named tags to INFO at `app_main()` entry.
- [x] Add file-local INFO ceilings for project diagnostic translation units.
- [x] Add staged `INIT[1/8]..INIT[8/8]` and detailed Wi-Fi/UDP task-context logs.
- [x] Add UDP `LOGSTATUS` and config checker.
- [x] Pass host static review 11/11 and stream-v1 compatibility test.
- [ ] Build from a fresh extraction with ESP-IDF 5.5.1.
- [ ] Verify UDP `VERSION` reports 3.4.3 / `P5_LOGGING_PARITY`.
- [ ] Verify UDP `LOGSTATUS` reports all runtime levels at 3 and UART0/115200.
- [ ] Capture startup serial from `nocti_v3_main` through Wi-Fi and first ADS/PCA failure/success.

## 3.4.6 hardware feedback

Resolved the false ADS startup failure caused by one expected post-SYNC `F_RESYNC` status indication. Added bounded resync acknowledgement and three-frame settling discard for startup validation and runtime START.
