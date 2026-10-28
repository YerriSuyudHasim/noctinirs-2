> SUPERSEDED WIRE CONTRACT (2026-10-27): active source now uses ADR-0004/1.
> See [the current local implementation binding](adr/0004-local-implementation.md).
> CF1 packets, CLAIM/RELEASE and the examples below are historical and are not
> accepted by the current firmware/client. SoftAP-only remains the network mode.

# Local v0.1.0 setup and usage

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

> Current implementation: USB provisioning + STA. The approved SoftAP/STA revision
> is specified in NETWORK_ONBOARDING.md and is not implemented yet. Existing
> NN2-CF1 operational packets remain unchanged.

Status: source candidate. The Owner performs compilation, flash and board tests.
No firmware build or board-success result is claimed for this candidate.

## 1. Build yourself in the ESP-IDF v6.1 terminal

Open the local repository in VS Code. Use the ESP-IDF terminal for **v6.1** and
ESP32-S3. Do not reuse an old ESP32 build cache or replace the root sdkconfig wholesale.

For the existing VS Code root build (`build/`, root `sdkconfig`), use the normal
Build command. The root configuration's main task stack was corrected from 3584
to 12288 bytes after the Owner's configure failure. `sdkconfig.defaults` now also
sets 12288 for fresh configurations. Existing sdkconfig values override defaults;
the CMake guard deliberately rejects a smaller communication stack.

For a separate configuration with the explicit communication console settings:

```powershell
idf.py --version
idf.py -B build/communication -D SDKCONFIG=build/communication/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication' -D IDF_TARGET=esp32s3 build
idf.py -B build/communication size
```

The command creates an isolated build/configuration. Keep those same `-B` and `-D`
arguments for reconfiguration/menuconfig. After the first configure, defaults do
not override values in its generated sdkconfig. Select **Communication Foundation**
in NoctiNIRS profile, main stack >=12288, PSRAM disabled, USB Serial/JTAG console.
The root build rejects SDKs outside v6.1. No partition/flash/security layout is
claimed to be board-qualified; inspect generated settings before deployment.

If it fails, retain the first compiler/configuration error and the generated
sdkconfig. Do not erase NVS, alter reference files, or switch to v5.5 to get past it.
The implementation has no third-party firmware dependencies or component downloads.

## 2. Board preparation and provisioning (Owner-operated only)

Before loading this candidate, follow BOARD_COMMUNICATION_TEST.md. GPIO18 maps to
RESET_LED in the supplied schematic. Confirm the actual assembly matches; keep
emitter energy unavailable until reset/off behavior is physically verified.
No flash command is run by the implementation workflow.

Close any other serial monitor before using the CLI. In the IDF Python environment
(which supplies pyserial), replace COM3 with the actual USB Serial/JTAG device:

```powershell
python tools/nn2ctl.py provision --serial-port COM3 --ssid "Your network name"
python tools/nn2ctl.py usb-status --serial-port COM3
```

The password is entered at a hidden prompt; never put it in source or a command
line. Device input is not echoed. Credentials are stored in one NVS blob; no
automatic broad NVS erase. This development profile does not enable NVS encryption.
Use a controlled WPA2-PSK network (8..63 printable ASCII password characters).
SSID is 1..32 bytes. First provisioning starts STA. Updating credentials after
STA has started stores them for the next reboot, even if currently disconnected.

The `STATUS` response reports the IPv4 address. An IP of 0.0.0.0 means no usable
IPv4 yet. The device is a station; it does not create the legacy NOCTINIRS AP.
Use the same LAN and permit UDP port 3333 between host and board.

## 3. Query the device

Replace the example address with `STATUS ip`:

```powershell
python tools/nn2ctl.py info --host 192.168.1.50
python tools/nn2ctl.py status --host 192.168.1.50
python tools/nn2ctl.py get-time --host 192.168.1.50
python tools/nn2ctl.py get-config --host 192.168.1.50
python tools/nn2ctl.py set-config --host 192.168.1.50 --rate 10 --slots 20
python tools/nn2ctl.py run --host 192.168.1.50 --seconds 10
python tools/nn2ctl.py safe-off --host 192.168.1.50
```

`run` claims one endpoint, enters SIMULATED_RUNNING, sends fresh heartbeats, then
stops and releases on exit. No LEDs or ADC are activated. A standalone `start`
command exits without heartbeats, so the device stops simulation after 3 seconds.
A different CLI process has a different UDP port; it cannot take another client's
lease. Wait for expiry or use public `safe-off`; it stops simulation without
stealing ownership. Setting simulation parameters does not change Wi-Fi settings.

Fault exercise: `fault --fault-code 7`, `get-fault`, `clear-fault`. Real off-request
failure is fault 255; it cannot be replaced by a synthetic fault. See PROTOCOL_LOCAL.md
for complete payloads, duplicate/cache behavior and error codes.

## 4. Host checks, optional native build performed by Owner

Python-only codec/provisioning/synthetic sequence checks:

```powershell
python test/communication/test_host.py -v
```

It explicitly skips the two real-C tests unless supplied an already-built loopback.
For a native GCC/Clang development terminal (including clang in a Windows C/C++
developer environment), the Owner can build and run the actual C implementation:

```powershell
cmake -S test/communication -B build/communication-host
cmake --build build/communication-host
ctest --test-dir build/communication-host --output-on-failure
python test/communication/test_host.py --loopback build/communication-host/nn2_loopback.exe -v
```

For a multi-configuration generator the executable may be under `Debug/`; on
Linux/macOS omit `.exe`. This loopback binds only 127.0.0.1 and calls the production
C codec/application, with an explicitly fake off port and test boot ID. It has no
board adapter and proves no hardware behavior. Compilation is not performed by
the Python test script. No package is installed automatically.

## Recovery and continuation

Check `docs/CHECKPOINTS_LOCAL.md` first when resuming. Preserve `.vscode`, `.clangd`,
root sdkconfig and prior build output. Rebuild only after recording the exact
working-source identity. The older fake and ADC diagnostic profiles remain
selectable; their implementation is preserved in `main/nn2_legacy_profiles.c`.
No production release, tag, push, or firmware installation is part of this work.
