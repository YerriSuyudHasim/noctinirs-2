# USB STATUS host reset investigation

Date: 2026-10-27. Local reversible host-tool fix; independent review pending.
Accountable activity: host-tool implementation. No firmware, electrical settings,
SDK, dependencies, tasks or physical device operations are changed by the agent.

## Owner-reported bench observations

The Owner reports consecutive `usb-status --serial-port COM11` calls without
touching the instrument. STATUS advances boot=2 -> 3 -> 4, each with reset=11,
cleared UDP counters and zero associated clients. These are human-reported
on-target observations, not agent-operated measurements. Installed ESP-IDF 6.1
esp_system.h maps enum 11 to ESP_RST_USB. Exact flashed source identity, board
revision, USB driver and installed pySerial version are not verified here.

Earlier Owner reports also show successful simulated control transactions,
SAFE_OFF/clear-fault, a continuous heartbeat run and silence expiration. Those
reports do not establish physical emitter-off behavior or charging correctness.

## Likely cause and change

The old host client constructed Serial(COM11, ...), opening immediately with
pySerial's default asserted DTR/RTS. USB Serial/JTAG interprets control-line
transitions as reset/download commands. This is a plausible host-triggered cause
consistent with ESP_RST_USB, not an instrumented trace proving the exact edge.

The client now constructs a closed serial object, disables software and hardware
flow control, sets DTR=False and RTS=False, assigns the port and opens explicitly.
It makes no deliberate line changes while open and always closes on completion
or error. The STATUS command, USB firmware and UDP protocol remain unchanged.

Primary references:

- [pySerial API: open and RTS/DTR](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)
  describes immediate constructor opening, defaults and pre-open line settings.
- [pySerial Windows backend](https://github.com/pyserial/pyserial/blob/master/serial/serialwin32.py)
  shows application of the configured control-line states during opening.
- [Espressif ESP32-S3 TRM, USB Serial/JTAG controller](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf)
  documents reset actions from control-line transitions.

Driver/OS open/close glitches remain possible, as documented by pySerial. This
change removes the client's default asserted-line request; it cannot guarantee
electrical behavior across every driver. If resets persist, retain evidence and
investigate the USB driver/control transfers; do not disable firmware protections.

## Acceptance and verification

Host acceptance: no requested active DTR/RTS at open; correct STATUS output;
cleanup and errors preserved. Physical acceptance: the same boot ID before and
after repeated status calls, no unintended Wi-Fi disconnection, advancing uptime
and no reset of counters solely due to opening/closing the client. No reflash is
needed for this host-only change. Run only within the existing approved fixture
and bench procedure. Do not use rebooting samples for heap or battery-cadence
comparisons.

Agent verification at 2026-10-27 07:25 UTC, Windows / Python 3.13.5:

- `python -m unittest discover -s test/communication -p test_usb_status.py -v`:
  5 PASS. A serial model detects a requested active-line open transition; it is
  not a USB driver or electrical simulation. The repeated-open regression failed
  against the old client and passes with the fix. Error/timeout/open-failure and
  write-failure paths release the port.
- `python test/communication/test_host.py -v`: 6 codec/helper tests PASS,
  7 native loopback tests SKIPPED. An attempted run with the prior native path
  could not start because build/adr004-host/nn2_loopback.exe is no longer present.
  No native-loopback rerun or firmware build is claimed for this change.
- Reference integrity: 319 files PASS before and after the code edit.
  `git diff --check` passes for tracked changes (existing CRLF normalization
  warnings); touched Python inputs are untracked local candidate files.
- Physical COM-port test, same-boot behavior and Windows driver qualification:
  NOT RUN by the agent. No real serial port is opened by the regression tests.

Source identity: base HEAD 5958a0eafec6b03c2b158c4026f3f10b8a3366d1 plus local
uncommitted/untracked changes. SHA256 of the affected inputs:

```text
7a47f6913ea8260290bc71361cc9b6de2045d0a29dee19baeef0824f59f0b7b2  tools/nn2ctl.py
50bdc1acb4990db18a0e71186a4293f37657c1455b551224619504c059af7307  test/communication/test_usb_status.py
```

Older evidence fingerprints refer to their earlier source snapshots. Do not
represent them as this host revision's identity. No commit, push or merge was
performed. Rollback affects only the host script and reintroduces the suspected
reset behavior; preserve this candidate for comparison and avoid repeated COM
opens with the old client during stability testing.
