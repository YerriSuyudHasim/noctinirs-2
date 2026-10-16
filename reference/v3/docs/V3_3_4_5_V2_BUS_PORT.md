# NoctiNIRSv3 3.4.5 — V2 I2C/SPI Bus Port

## Source of truth

The validated NoctiNIRSv2(16) I2C and SPI implementations were used as the behavioral reference.

## I2C correction

ESP-IDF selects asynchronous master behavior from `trans_queue_depth`. Removing a device callback does not make a qdepth=4 bus synchronous. Version 3.4.4 created qdepth=4 first and then ran stack-backed init/readback operations, causing `ops list is full`.

Version 3.4.5 uses the V2 phase sequence:

1. create/recreate I2C bus with qdepth=0;
2. reset and synchronously configure/read back PCA9956B;
3. destroy the synchronous device/bus;
4. create qdepth=4 bus/device on the LED task core;
5. register the runtime transaction-done callback;
6. use only persistent prebuilt LEDOUT payloads during runtime.

## SPI correction

The ADS131 transport now matches V2:

- SPI3_HOST;
- mode 1;
- 8 MHz;
- DMA disabled;
- queue size 1;
- max transfer 256 bytes;
- CS pre/post guard 1/1;
- one contiguous 18-byte polling transaction.

GPIO35-38 are retained. `CONFIG_SPIRAM` remains disabled.

## Expected startup evidence

```text
nocti_v3_ads131: SPI ready ... cs_guard=1/1 V2_port=1
nocti_v3_board: I2C phase=SYNC qdepth=0 ...
nocti_v3_led: PCA init verified ...
nocti_v3_board: I2C bus recreated qdepth=0->4 mode=ASYNC
nocti_v3_led: PCA runtime path ready phase=ASYNC qdepth=4 ...
```

There should be no `ops list is full` error.
