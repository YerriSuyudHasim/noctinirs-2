# ADR-0004 owner build and bench guide

Date: 2026-10-27. Status: procedure only; target build and bench NOT RUN.
This is the current communication procedure. It supersedes STA provisioning,
CLAIM/RELEASE, injected fault commands and the old three-second silence timeout
in BOARD_COMMUNICATION_TEST.md and SETUP_COMMUNICATION.md.

## Scope and entry gate

Scope: ESP32-S3 / ESP-IDF 6.1 communication profile, SoftAP, ADR-0004 control
transactions and cached battery telemetry. No optical acquisition or FRAME
producer exists. No synthetic-data implementation is added by this procedure.
Accountable bench operator: the human Owner. Hardware/RTOS specialist review,
independent review and any required hardware authorization remain prerequisites;
this document does not grant them. No physical test is recorded as passed.

Before powering/flashing, record assembled board revision, USB interface/COM port,
power source, battery model/capacity/allowed charge current, protection and
temperature arrangements, and emitter isolation. Confirm actual assembly maps
GPIO18 to active-low LED reset, GPIO8 to charger CE, SDA15/SCL16 and appropriate
external pullups/voltage domains. Do not infer assembly from historical firmware.

Charging is automatically requested at boot: nominal 512 mA, nominal regulation
4176 mV for the Owner's single-cell 4.20 V LiPo limit. This is not a measured
voltage/current guarantee. USB-only operation or an absent cell must not be
assumed to satisfy the power driver's initial checks. Battery/charger operation
requires its approved procedure, cell compatibility and instrumented monitoring.
Do not connect a battery merely to bypass a startup failure.

Bootloader safe-off PR #60 is not integrated. The software reset hold begins in
app_main, not necessarily at power-on. Keep emitter energy physically unavailable
until reset behavior is measured through boot/reset. This includes automatic
reboot after flashing. Compilation cannot establish off behavior.

## Stage 1: build without operating the board

Use the ESP-IDF 6.1 terminal in the repository root. Preserve existing root
sdkconfig/build. Use a fresh isolated build/configuration; existing generated
sdkconfig values override defaults. Do not erase NVS or alter security/eFuses.

```powershell
idf.py --version
git rev-parse HEAD
git status --short
& ./tools/verify-reference.ps1
idf.py -B build/communication -D SDKCONFIG=build/communication/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication' -D IDF_TARGET=esp32s3 build 2>&1 | Tee-Object -FilePath communication-build.log
# Continue only if the build exited successfully.
idf.py -B build/communication size 2>&1 | Tee-Object -FilePath communication-size.log
```

Before flash, inspect generated build/communication/sdkconfig: ESP32-S3,
NN2_PROFILE_COMMUNICATION=y, main stack >=12288, PSRAM disabled, USB Serial/JTAG
console, brownout enabled. Confirm flash size/mode, partition layout and USB
interface against the assembled module. Retain the SDK git revision, compiler
version shown in the build and all warnings. Send build/size logs and generated
configuration for assessment if any setting or warning is unexplained.

HEAD alone is insufficient because the implementation has uncommitted and
untracked files. Retain a copy/hash inventory of active source and the generated
configuration for this run. Record ELF/binary SHA256 after a successful build.
Do not change source between build and flash; rebuild if anything changes.

## Stage 2: authorized flash and boot

Only after Stage 1 and the physical entry gate are satisfied, the Owner may use
the following commands, replacing COM3 with the confirmed interface. These
commands operate hardware; they are not part of Stage 1.

```powershell
idf.py -B build/communication -p COM3 flash monitor
```

Capture complete serial output from reset. Expect a battery-monitor startup
message, ADR0004/1 SIMULATED with RESET_LED held low, and SoftAP NoctiNIRS at
192.168.4.1 UDP3333. No startup/runtime stopped message, panic, watchdog or reset
loop is acceptable. A power-initialization failure prevents SoftAP startup; keep
the first error rather than assuming a Wi-Fi problem. Software off-request logs
are not physical off evidence. Exit the monitor with Ctrl+] before USB CLI use.

## Stage 3: basic transactions

Join NoctiNIRS (Owner-selected prototype password 123456789). Keep the association
when the computer reports no internet. Only one associated computer is supported.
Record assigned host IPv4 and Wi-Fi adapter. Run commands sequentially:

```powershell
python tools/nn2ctl.py info
python tools/nn2ctl.py status
python tools/nn2ctl.py get-config
python tools/nn2ctl.py get-time
python tools/nn2ctl.py set-fs --rate 5
python tools/nn2ctl.py get-config
python tools/nn2ctl.py run --seconds 60
python tools/nn2ctl.py status
python tools/nn2ctl.py safe-off
python tools/nn2ctl.py status
python tools/nn2ctl.py clear-fault
python tools/nn2ctl.py status
```

Pass: successful replies report OK; configuration becomes 5 Hz; run keeps one
endpoint alive with heartbeat every 500 ms and stops normally; final normal
state is STOPPED (numeric 1). SAFE_OFF is numeric 7, and successful clear-fault
returns to STOPPED. Replies remain SIMULATED. No FRAME data is expected.
Do not use separate CLI processes to heartbeat/stop an active run: they have
different source ports. safe-off is explicitly allowed from another endpoint.

## Stage 4: silence and disconnect

1. From STOPPED, issue standalone start and allow that process to exit. Send no
   further commands for six seconds. Then query status: expect STOPPED, no active
   session and an increased grace-expiration count. Silence detection is 1.5 s
   plus a 3 s grace, approximately 4.5 s from last valid owner activity; this is
   not a measured scheduling guarantee. Six seconds is observation margin.
2. Start run for 60 seconds, disconnect the computer from the SoftAP for at least
   six seconds, then reconnect. The old run may report timeout/error; retain it.
   End that client and query status. Expect the old session to have expired;
   start a fresh run explicitly. Reassociation must not resume acquisition.
3. Repeat disconnect/reconnect three times initially; increase to ten only after
   initial success. Record disconnect, reconnect and first successful reply times.

Never interpret these tests as physical acquisition shutdown timing. Emitters
remain isolated and no real acquisition is enabled in this profile.

## Stage 5: telemetry and sustained control traffic

First verify that opening USB STATUS does not restart the board. The Owner has
reported repeatable ESP_RST_USB resets with the previous host script. Use the
updated tools/nn2ctl.py, repeat STATUS and compare boot IDs before accepting
same-boot telemetry trends. See [USB_STATUS_RESET_FIX.md](USB_STATUS_RESET_FIX.md).
The host-only change needs no firmware reflash; physical confirmation is pending.

Close other serial clients, then obtain USB STATUS:

```powershell
python tools/nn2ctl.py usb-status --serial-port COM3
```

Save STATUS before/after a 60-second run. Inspect battery_valid=1,
battery_error=0, battery_uv, battery_mperm, battery_age_ms, heap, min_heap, stack,
reset, socket/send errors. Units: voltage in microvolts; SOC in thousandths of a
percent (50000 = 50%). Compare voltage with an instrument under the approved
battery procedure; SOC is not a calibrated reference.

For cadence, observe idle for >120 s and a continuous run for >360 s, sampling
USB STATUS periodically with no competing serial monitor. Expected read interval:
idle 60 s, STREAMING simulation 300 s, measured from the last attempt. A mode
change is relative to that same last attempt, not a newly reset timer. Cached
age increases between successful reads and resets after an update. A read error
retains the old sample and records the error; do not mistake it for fresh data.

For an initial 30-minute stability run, use run --seconds 1800 and save console
output plus periodic USB STATUS. This exercises roughly two heartbeat
transactions/sec, NOT the older ten-transaction/sec load criterion. Pass this
stage only with no unresolved timeouts, panic/watchdog/unexplained resets,
unexpected session change or observed emitter activation. Report retries and
latency rather than claiming zero loss from final OK alone. Report stack
watermark and heap trend; investigate stack below 2048 bytes or sustained heap
decline >4 KiB between minutes 5 and 30. These are provisional bench criteria.
No stream throughput/frame-loss result can be obtained without a frame producer.

## Stop and preserve evidence

Stop the affected test on unexpected emitter activity, abnormal cell temperature,
voltage/current outside the approved battery procedure, reset loop, panic,
watchdog, electrical fault or unexplained power/identity/readback error. Isolate
energy using the approved fixture procedure and retain logs; do not repeatedly
reset, erase NVS or bypass checks to obtain a passing result. Record as on-target
bench evidence, separate from existing static/host results.

Rollback uses a known compatible previously reviewed image and documented board
recovery procedure. Reflashing alone is not a guarantee that charger registers or
physical states are restored. No production release, signing, security settings,
eFuses, patients or human optical tests are included.

## Report template

```text
Date/time and operator:
Board/module revision, USB interface and COM port:
Supply, battery model/capacity/max permitted charging current, fixture:
Emitter isolation/reset measurement and instruments:
Source HEAD + dirty source identity; changes since recorded candidate:
ESP-IDF revision/compiler; profile; generated sdkconfig:
Build result, warnings, size, image hashes:
Boot log/reset reason/boot ID:
Host OS, Wi-Fi adapter, IPv4, association/recovery observations:
Command outputs: info/status/config/time/run/safe-off/clear-fault:
Silence/disconnect test timelines and results:
USB STATUS snapshots and measured battery voltage/current/temperature:
Run duration, retries, unresolved timeouts, latency, heap/stack trend:
First failure (exact error + full surrounding log), if any:
Not-run items and reason:
```

Send text logs and generated configuration; screenshots supplement text. Exclude
unrelated credentials, tokens, network identifiers or personal information.
