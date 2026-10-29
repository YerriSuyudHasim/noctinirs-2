# V4 acquisition on IDF 6.1 with NN2 communication

LOCAL UNBUILT CANDIDATE. Scheduler, SPI, I2C, optical and frame processing remain
V4 sources. The default control path now reuses the actual feat35 components
nn2_core/ports/app/wifi/net/config through EXTRA_COMPONENT_DIRS. It does not
link feat35's ADC/LED adapters, replacement scheduler, pairing or USB provision
parser. The original V4 network is compiled but not initialized in this mode.

## Select the correct project

Build/flash/monitor from this folder, not the repository root. The author has
not built or flashed this change. Building the root still produces feat35.
An old device reply `ERR unsupported; SoftAP-only; use STATUS` comes from the
root nn2_provision parser, which is not part of this migrated image.

The new image identifies itself by `CONTROL_MODE baseline=V4_IDF61_NN2_CTRL`
and `V4_NN2_CONTROL ... ip=192.168.4.1 port=3333`. USB VERSION and STATUS work;
while scanning, only emergency SCAN_STOP / SAFE_OFF are accepted. Other V4
mutation commands are disabled when NN2 control owns acquisition. Inspect USB
SCAN_SESSION_REPORT, SCAN_HEALTH and ADC_DEBUG after stopping.

## Host control

Use the same public prototype SoftAP setup as feat35 (SSID NoctiNIRS), from the
shared nn2_wifi implementation. The old V4 password menu option applies only
when the NN2 communication option is disabled. Session/retry/heartbeat/lease,
boot identity, UDP3333 and stopped-only Fs configuration are the NN2 protocol.
Build in this folder; run the host tool from the repository root:

```powershell
python tools/nn2ctl.py info
python tools/nn2ctl.py run --rate 5 --seconds 10 --led
```

INFO must report `baseline=V4_IDF61_NN2_CTRL`. These are bench instructions for
use only after an authorized build/deployment and procedure; the author sends
no commands to hardware. NN2 run duration/STOP/lease are available again. V4 I2C
qualification and current/Fs setup are performed by a separate control worker
before ADC START, so no manual I2C_FMP_QUALIFY or ASYNC_ENTER is required.

Do not pass --acquisition: native V4 frame values lack the complete timestamp
and sequence metadata required by ACQ1. INFO deliberately says frames=unsupported;
no V4 data is relabelled as ACQ1 and no sample quality policy is replaced.
The V4 frame consumer still processes data and retains its diagnostics; UDP
measurement export needs a separately specified binding.

## Ownership and stop behavior

The single NN2 App/UDP owner is Core0/priority4, requested stack16384 bytes.
A new Core1/priority1 worker (8192 bytes) serializes blocking V4 resource/setup
and cleanup. V4 LED22 > scheduler21 > ADC20 remains intact. Main console stack
is12288 bytes. Heap/stack/Wi-Fi coexistence margins have not been measured.

Nonblocking STOP/cancel immediately closes a shared reset-release gate and holds
PCA RESET low. The same gate prevents a late startup release after cancellation.
Normal async submissions are disabled; V4 owner cleanup still drains and restores
all-OFF. Configuration-only reset release is allowed after scheduler STOP and
ADC/optical stop; final cleanup holds reset again. USB emergency off is handed
to the NN2 App owner to revoke the host session. Faults remain sticky; cleanup
failure or uncertain in-flight driver ownership is reported as fault, never
fabricated STOPPED success. Cancellation during I2C needs target qualification.

## Preserved settings and evidence

Active board pins, dedicated SPI2/mode1/8MHz/18bytes, CPU240MHz, flash16MiB,
I2C runtime800kHz/setup100kHz, current ceiling30mA, tick1000Hz and async queue4.
Fs2/4/5/8/10 is set only while stopped. No automatic NVS erase. The shared NN2
Wi-Fi code uses the existing owner-selected public prototype credentials; no
historical V4 password is restored. Never use prototype credentials in production.

Run `python tools/verify-v4-idf61.py` from repository root for source/provenance,
component graph and geometry checks. Historical integrity covers319 files.
These are static checks, not compile/link, host execution or board evidence.
Source commits record this restart; no author build or flash has been performed. Specialist and independent
review remain required before acceptance/merge. The old local working tree and
standalone checkpoint remain preserved.
