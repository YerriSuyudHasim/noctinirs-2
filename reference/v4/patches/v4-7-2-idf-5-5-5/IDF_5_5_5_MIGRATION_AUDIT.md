# ESP-IDF 5.5.1 to 5.5.5 migration audit

## Decision

Use ESP-IDF 5.5.5 as a controlled patch-level uplift. Do not combine this
candidate with an ESP-IDF 6.x migration. The active engineering gate remains
V4-7.2 Wi-Fi-idle hardware validation.

Compared official Espressif tags:

- v5.5.1 commit: `fcae32885b0296b32044cb99ecbdc50d98dddb83`;
- v5.5.5 commit: `b774170ff46c393eeb5e495ea37936038d3f4f4f`.

ESP-IDF 5.5.5 is identified by Espressif as a bug-fix release in the 5.5
series. Patch-level compatibility reduces migration scope, but does not imply
identical timing or binary behavior.

Official references:

- https://github.com/espressif/esp-idf/releases/tag/v5.5.5
- https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/migration-guides/release-6.x/6.0/index.html

## Relevant framework findings

### GPTimer

The public `driver/gptimer.h` and `src/gptimer.c` files are byte-identical
between the inspected tags. The V4 registration-before-enable sequence and
absolute alarm API require no source change.

### FreeRTOS SMP

The inspected SMP `tasks.c` and `queue.c` are byte-identical. The Xtensa port
does contain internal startup and thread-local-storage changes. V4's static
queue, notification, and pinned-task APIs retain their signatures, but the
real-time hardware gate must still detect any timing effect.

### I2C master

The public API used by V4 remains available. The visible header change makes a
multi-buffer transmit pointer `const`; V4 does not use that API. Internal
master-driver changes are material to this firmware and include:

- corrected reset/bus-clear ordering;
- bounded recovery when a NACK leaves the bus busy;
- SCL timeout range clamping;
- fixes in read-command/FIFO state handling;
- preservation of the ISR yield path when no device record is returned;
- capability-aware allocation for async queues and device records;
- corrected propagation of transaction errors.

V4 uses the new master driver, asynchronous transmit callbacks, queue depth 4,
and `CONFIG_I2C_ISR_IRAM_SAFE`. No source API adaptation was found necessary,
but callback placement, completion identity, latency, and strong-OFF behavior
must be rechecked after the 5.5.5 build.

### SPI master / ADS

The SPI public functions used by V4 retain their signatures. New header flags
concern DMA/PSRAM behavior; V4 keeps ADS DMA disabled. Internal SPI code changed,
so the existing 8-kSPS rate, 18-byte frame integrity, latency, and zero-loss
counters remain hardware requirements rather than assumptions.

### Wi-Fi and network stack

The inspected ESP32-S3 Wi-Fi adapter source is byte-identical. Common Wi-Fi
initialization and esp-netif internals changed, while the SoftAP APIs used by
V4 remain available. Precompiled Wi-Fi/PHY libraries may also differ across
patch releases. Therefore associated-but-not-streaming Wi-Fi-idle Test 1 must
be repeated before active UDP is permitted.

### Configuration symbols

The required 5.5.5 Kconfig symbols were found in the official tag:

- `I2C_ISR_IRAM_SAFE`;
- `GPTIMER_CTRL_FUNC_IN_IRAM`;
- `GPTIMER_ISR_CACHE_SAFE`;
- `GPTIMER_OBJ_CACHE_SAFE`;
- `SPI_MASTER_ISR_IN_IRAM`;
- `ESP_MAIN_TASK_AFFINITY_CPU1`;
- `ESP_WIFI_TASK_PINNED_TO_CORE_0`;
- `LWIP_TCPIP_TASK_AFFINITY_CPU0`;
- `ESP_CONSOLE_USB_SERIAL_JTAG`.

No locked V4 configuration is intentionally weakened or renamed by this
patch candidate.

## Locked firmware behavior

This candidate intentionally preserves:

- ADS SPI3, mode 1, 8 MHz, 18-byte frames, 8 kSPS, DMA disabled;
- 20 x 5-ms slots, 100-ms frames, 10 Hz;
- K target 10/10 and minimum 8/8;
- synchronous PCA diagnostics at 400 kbit/s, queue depth 0;
- qualified asynchronous SCAN at 800 kbit/s, queue depth 4;
- seven-byte one-hot ON, two-byte invariant-gated normal OFF;
- seven-byte strong ALL_OFF plus RESET# for stop/fault/recovery;
- 2,200-us OFF phase, 500/500-us settling, 100-us guard;
- LED owner Core 1/p22, scheduler Core 1/p21, ADS Core 1/p20;
- Wi-Fi/lwIP and downstream processing on Core 0;
- immutable completed-frame networking and existing UDP ABI;
- absence of MBLL, SSR, bandpass, and a new host UI.

## Required build evidence

The user-run build is not accepted until it records:

1. exact `idf.py --version` output showing v5.5.5;
2. full clean configure/compile/link/image result and exit status;
3. all compiler warnings and errors;
4. generated `sdkconfig` SHA-256 and diff from
   `sdkconfig.v5.5.1.reference`;
5. BIN, ELF, bootloader, and partition-table sizes and SHA-256 hashes;
6. size/map comparison with the recorded 5.5.1 build;
7. callback/time/ring helpers still located in `.iram0.text`;
8. dedicated static-IRAM and combined D/IRAM usage;
9. confirmation that the compile-time core, priority, and IDF-version guards
   pass.

Do not add a build-manifest entry until a real build has completed.

## Required hardware evidence

After a successful build, the existing V4-7.2 Wi-Fi-idle Test 1 must be run
unchanged for at least 120 seconds with streaming disabled. It must prove zero
ADS/integrity loss, zero scheduler misses, zero async/completion-ring faults,
zero steady-state K below 8, at least 98% target frames, the complete Core-1
PCA ownership contract, zero network frames enqueued/sent, and verified
synchronous plus physical OFF after STOP.

Until that passes, active UDP Test 2 remains blocked and this candidate is not
hardware verified.
