# NoctiNIRSv3 3.4.4 — Native USB Serial/JTAG Console Fix

## Hardware basis

The custom board uses an ESP32-S3-WROOM module with the USB cable connected directly to:

- GPIO19: USB D-
- GPIO20: USB D+

There is no CH340, CP2102, or other USB-to-UART bridge. Therefore the firmware application console must use the ESP32-S3 **USB Serial/JTAG Controller**, not UART0 and not the USB-OTG CDC console.

## Root cause

The previous project selected `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` and also contained compile-time guards that forced UART0. Boot/ROM text could be visible during reset, but after the application startup switched standard output to UART0, `ESP_LOGI()` output was no longer available on the native USB connector.

## Correct configuration

```text
# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set
# CONFIG_ESP_CONSOLE_USB_CDC is not set
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y
CONFIG_ESP_CONSOLE_UART_NUM=-1
CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=4
```

## Mandatory rebuild

The console selection affects both the bootloader and application. Do not reuse the old build output. Run:

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Expected application banner includes:

```text
NoctiNIRSv3 firmware=3.4.4 phase=P5_USB_SERIAL_JTAG_FIX
serial logging enabled ... console=USB_SERIAL_JTAG gpio_dm=19 gpio_dp=20 ...
```
