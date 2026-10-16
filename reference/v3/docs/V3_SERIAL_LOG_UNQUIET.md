# NoctiNIRSv3 3.4.3 — V2/V3 logging parity and hard diagnostics

## Root conclusion

The V2 acquisition-only quiet policy was not responsible for missing V3 boot
or Wi-Fi logs. V3.4.2 also contained far fewer Wi-Fi diagnostics. More
importantly, the reported monitor output lacked every application INFO line,
not only Wi-Fi, so this patch treats the active build configuration, runtime
master/default/tag levels, console route, and stale-binary risk as separate
failure classes.

## Implementation

- Removed the quiet helper and quiet state completely.
- No unconditional `printf()` is present.
- Requires INFO to be compiled and UART0/115200 to be the application console.
- Enables ESP-IDF master log level and restores master/default/named tags to
  INFO at the first line of `app_main()`.
- Sets file-local INFO ceilings for all project files that own INFO logs.
- Adds detailed task-context startup and Wi-Fi/UDP logs.
- Adds UDP command `LOGSTATUS`.
- Keeps all ISR/callback/sample/slot hot paths free of serial output.

## Expected first application lines

```text
I (...) nocti_v3_main: ============================================================
I (...) nocti_v3_main: NoctiNIRSv3 firmware=3.4.3 phase=P5_LOGGING_PARITY ...
I (...) nocti_v3_main: serial logging enabled configured_default=3 runtime_default=3 maximum=3 master=3 ... console=UART0/115200 ... runtime_quiet=DISABLED
I (...) nocti_v3_main: INIT[1/8] validating centralized timing configuration
```

## Independent UDP verification

```text
NOCTI VERSION firmware=NoctiNIRSv3 version=3.4.3 phase=P5_LOGGING_PARITY ...
NOCTI LOGSTATUS default=3 maximum=3 master=3 wildcard=3 main=3 wifi=3 ... console=UART0 baud=115200 secondary=NONE runtime_quiet=DISABLED
```

If UDP still reports an older version, the monitored firmware is not this
build. If UDP reports 3.4.3 and LOGSTATUS is all INFO but UART remains silent,
then the next target is the actual UART/USB console path and host monitor port,
not the Wi-Fi logger.
