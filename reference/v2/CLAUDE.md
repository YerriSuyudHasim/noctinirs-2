# NOCTINIRSv2 — Project Context

## Hardware
- MCU: ESP32-S3 (SPI2=ADS131M04, SPI3=ADS1299, I2C0=PCA9956B+BQ24295+MAX17048)
- fNIRS ADC: ADS131M04 (SPI2, GPIO: DRDY=35, SYNC/RESET=6, CS=7, SCLK=36, MISO=37, MOSI=38)
- Biopot ADC: ADS1299 (SPI3, GPIO: DRDY=10, START=14, RESET=21, PWDN=47, CS=13)
- LED driver: PCA9956B via I2C (addr=0x01, Rext=1kΩ), 10 sources × 2 wavelength (735/850nm)
- Photodetector: VEMD8082 × 4, dengan TIA OPA2380

## Firmware Stack
- ESP-IDF, FreeRTOS, NimBLE
- Project: NOCTINIRSv2 (Visual Studio Code + Espressif IDF extension)

## Channel Mapping ADS1299
- CH1-4: EEG unipolar, SRB2=true, GAIN_24, ref di mastoid
- CH5: EMG chin bipolar, GAIN_8
- CH6: EOG Vertical bipolar, GAIN_4
- CH7: EOG Horizontal bipolar, GAIN_4
- CH8: ECG Lead I bipolar, GAIN_4
- Sample rate: dr_bits=0x04 (1000 SPS)
- BIASOUT electrode: mastoid (A1/A2)

## Bug Fixes Applied (dari sesi ini)
1. ADS131M04 reg_clock: 0x0F12 (OSR=2048, 2000 SPS at 8.192 MHz CLKIN)
2. fnirs_collect_and_average: guard underflow `if (i >= wr) break`
3. SYNC pulse sebelum LED ON di SLOT_BEGIN
4. ADS1299 spi_xfer: ganti ke transfer_contig (polling) untuk frame 27B
5. scl_wait_us: 15000→1000 di nocti_hal_i2c.h
6. PCA_I2C_TIMEOUT_MS: restored to 50 ms in nocti_led_driver.c
7. ADS1299 reset: tambah SDATAC + busywait 100µs setelah CMD_RESET
8. SRB2=true untuk CH1-4 EEG
9. R29 (1kΩ) BIASOUT→BIASIN: pertimbangkan ganti ke 10kΩ-100kΩ
10. app_main: dr_bits 0x06→0x04 (250→1000 SPS)
11. app_main: konfigurasi penuh CH1-8 (EEG GAIN_24, EMG GAIN_8, EOG GAIN_4, ECG GAIN_4)
12. app_main: BIAS_SENSP=0xFF, BIAS_SENSN=0xF0 (auto dari channel config, hapus override manual)
13. nocti_biopot: VLEAD_OFF_EN (bit4 LOFF reg) diset untuk mode AC lead-off (7.8Hz / 31.2Hz)

## fNIRS Scheduler
- tick=5000µs, frame=100ms, 20 slot (10 LED × 2 WL)
- candidate window starts at 200µs; window=3200µs; SLOT_END/LED OFF=3400µs; guaranteed OFF gap=1600µs
- k_fixed=3, FNIRS_ADC_SEARCH_BACK=128
- ESP32-S3 I2C=400kHz; PCA9956B OFFSET(0x3A)=0x00

## Stream / Transport
- nocti_stream: 3 FreeRTOS ringbuf (ctrl=8KB, fnirs=48KB, biopot=64KB)
- Transport: WiFi SoftAP UDP port 3333(ctrl) / 3334(stream)
- BLE: NimBLE, MTU=247, fragmentation header 8B