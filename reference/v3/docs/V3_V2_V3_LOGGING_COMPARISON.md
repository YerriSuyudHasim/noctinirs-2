# NoctiNIRSv2(16) vs NoctiNIRSv3 logging audit

## Scope

Compared the actual source trees of:

- `NOCTINIRSv2(16)`;
- NoctiNIRSv3 3.4.2;
- maintenance patch NoctiNIRSv3 3.4.3.

The comparison covers compile-time log settings, runtime log filtering, UART
console routing, Wi-Fi/UDP event coverage, and forbidden real-time paths.

## Findings

### 1. The V2 wrapper is not a separate logger

Under ESP-IDF, `NOCTI_LOGI/W/E` in `nocti_log.h` maps directly to
`ESP_LOGI/W/E`. Therefore the visible V2 output is not caused by a special
serial backend. It is primarily caused by having more log call sites and by
building INFO messages into the binary.

### 2. V2 quiet mode starts only with acquisition

V2 defines `FNIRS_RUNTIME_SERIAL_QUIET=1`, but
`runtime_serial_set_quiet(true)` is called immediately before a successful
`START` path enables timing-critical acquisition. On START failure and after
STOP, the saved levels are restored. The helper only lowers selected tags to
ERROR.

Therefore V2 quiet mode cannot explain missing V3 boot, peripheral-init, or
Wi-Fi startup logs while the device is still STOPPED.

### 3. V2 and V3.4.2 source sdkconfig both request INFO on UART0

Both source trees contain:

```text
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAXIMUM_LEVEL=3
CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_UART_NUM=0
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
```

The source configuration is therefore not materially different at those
settings. A generated/stale `sdkconfig`, a different build directory, a binary
that was not the intended build, or a compile-time local log ceiling can still
make the flashed image behave differently.

### 4. V3.4.2 had much less diagnostic coverage

Counting actual log invocations in `.c` sources:

| Tree | All source log calls | Wi-Fi component log calls |
|---|---:|---:|
| V2(16) | 164 | 21 |
| V3.4.2 | 36 | 7 |
| V3.4.3 | 84 | 39 |

V2 reports SSID, password/open mode, channel, AP IP/GW/netmask, AP start,
station connection/disconnection reason, assigned station IP, UDP target,
socket/bind failures, listener readiness, task creation, task stack margin,
and final readiness.

V3.4.2 reported only a subset. That explains why V3 was less useful once INFO
logging worked, but it does not explain why *all* application INFO lines were
absent.

### 5. Missing `main_task` and project INFO is stronger evidence than missing Wi-Fi logs

The observed serial output includes bootloader INFO and an application-time SPI
ERROR, but no normal `main_task` or NoctiNIRSv3 INFO lines. This pattern means
the problem is broader than the Wi-Fi component. The likely classes are:

- INFO statements were compiled out by the actual generated configuration;
- a master/default/tag runtime level is below INFO;
- the application console differs from the monitored console;
- a stale/different binary was flashed.

The source alone cannot distinguish those cases. V3.4.3 therefore makes each
one observable or build-fatal.

## V3.4.3 policy

- There is no `runtime_serial_set_quiet()` function and no quiet state variable.
- `CONFIG_LOG_MASTER_LEVEL=y` is required.
- Build fails when default/maximum level is below INFO, dynamic control is off,
  UART0 is not the application console, baud is not 115200, or a secondary
  console is enabled.
- `LOG_LOCAL_LEVEL ESP_LOG_INFO` is set before `esp_log.h` in each project file
  that owns INFO diagnostics.
- At `app_main()` entry, master level, wildcard/default level, and named project,
  Wi-Fi, PHY, and netif tags are restored to INFO.
- `LOGSTATUS` exposes the compiled and runtime log configuration over UDP.
- Startup is logged as explicit `INIT[1/8]` through `INIT[8/8]` stages.
- Wi-Fi startup is logged stage-by-stage, with errors naming the exact failed
  API stage.
- No logging is added to DRDY ISR, GPTimer callback, async I2C callback,
  per-sample acquisition, or normal per-slot processing.

## Required verification

After a clean build in a new extraction directory:

```text
VERSION
LOGSTATUS
```

must return version `3.4.3`, phase `P5_LOGGING_PARITY`, default/master/maximum
level `3`, UART0, 115200, and `runtime_quiet=DISABLED`.
