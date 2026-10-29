# V4 IDF 6.1 board init origin

Static record of where each boot stage came from, before and after the
feat35 stage machine was brought into `firmware/v4-idf61`. This image was
not flashed. A host test does not show that RESET stayed low on the pin.

## Stable feat35 reference

The stable init reference is `31fb2a9101fbb12a59e52506464d476e184bbdd9`,
the tip of `origin/feat/35-ads131-acquisition`
(`docs: record ADS131 acquisition build and host verification`).

`main/app_main.c` and `components/nn2_board/nn2_board.c` at that tip are
byte-identical to the same paths at:

- `df796ba6a8d0c5d6d8bc4f30fde2aa2ffea3198d` (checkpoint on the V4 restart
  branch, not an ancestor of the published feat/35 tip)
- `edb70411d2d7cc3da529c92af375bbd21f7f6016` (ancestor of `df796ba`, on the
  restart branch only; its subject is the ADS priority and 16 MiB layout)

`edb7041` is the wrong citation for init structure. It is not on
`origin/feat/35-ads131-acquisition`. The boot text matches, so it
corroborates the stage list, and the published tip is the reference this
port follows.

## Before (head `ae4ddb2`)

| Stage | Origin | Evidence |
|---|---|---|
| Boot sequence | Legacy V4 linear `app_main`. Later stages ran only while the previous `esp_err_t` stayed `ESP_OK`. | `firmware/v4-idf61/main/app_main.c:249-320` at `ae4ddb2` |
| Emitter-safe order | V4. `gpio_config` first, then PCA RESET low, ADS reset low, CS high. No pulldown. | `nocti_v4_board.c:14-33` at `ae4ddb2` |
| GPIO set | V4 pins: PCA RESET, ADS reset, CS, DRDY input. | same function, DRDY at lines 35-42 |
| ADS bring-up | V4 `nocti_v4_ads_init`: reset, 8 kSPS program, 1024-sample qualification, CRC, `transfer_frame`. | `nocti_v4_ads.c` `hardware_reset`, `configure_8ksps`, `validate_rate`, `transfer_frame` |
| Wi-Fi / NVS / netif | feat35 `nn2_config_init`, `nn2_wifi_init`, `nn2_net_init`, `nn2_wifi_start`, but only after the scheduler succeeded. | `v4_nn2_control.inc` `control_init` at `ae4ddb2` |
| Config defaults | V4 `sdkconfig.defaults` (16 MB, 240 MHz, NN2 UDP, TWDT panic). Not feat35 `NN2_PROFILE_*`. | `firmware/v4-idf61/sdkconfig.defaults` |
| Init fault handling | Per-stage `printf`, then skip the rest. No single fail label. PCA RESET was not re-asserted after a later owner failed. UDP did not start, so with the USB command console removed the host could not see the fault. | `app_main.c:289-313` at `ae4ddb2` |

## After

| Stage | Origin | What the image does |
|---|---|---|
| Boot sequence | feat35 stage list from `31fb2a9:main/app_main.c:101-225` | `nocti_v4_boot.h` flags. Runtime only if GPIO, core, I2C, PCA, and ADS are 1. Network if GPIO is 1. |
| Emitter-safe order | feat35 `nn2_board_init_safe` (`31fb2a9:components/nn2_board/nn2_board.c:24-46`): level, then `gpio_config`, then level, with pulldown | V4 still drives ADS reset low and CS high in that same order. `nocti_v4_board_early_safe_init` |
| GPIO set | V4 pin set kept | PCA RESET, ADS SYNC/RESET, CS, DRDY. Feat35 safe-init owns RESET only. |
| ADS bring-up | V4 kept | `nocti_v4_ads_init` and `spi_device_polling_transmit` in `transfer_frame`. Feat35 `nn2_acquisition_init` (`31fb2a9:main/adapters/nn2_acquisition_esp.c:193`) and `nn2_ads131_transport.inc` polling start/end were not copied. |
| Wi-Fi / NVS / netif | feat35 order kept, and it now runs after a later hardware failure | `control_init`: NVS, boot id, app comm, Wi-Fi init, UDP 3333, Wi-Fi start. USB provision (`nn2_provision_init` at feat35 `app_main.c` stage `USB diagnostics`) is not called. |
| Config defaults | V4 `sdkconfig.defaults` kept, with brownout pinned. Runtime cap and Fs stay in the V4 control binding (`30` mA, Fs 2/4/5/8/10). | No feat35 profile Kconfig. This project has one defaults file. |
| Init fault handling | feat35 `fail:` label (`31fb2a9:main/app_main.c:225-248`): name the stage, hold emitters off, do not continue into acquisition | First zero flag is the stage. `health` carries `boot_stage`. A hardware failure re-asserts PCA RESET and latches `NN2_OPTICAL_FAULT` before the UDP task starts. `CLEAR` cannot clear it. |

Feat35 `fail:` also disables the charger and releases the power-management
I2C bus (`nn2_board_power_set_charge_enabled`, `nn2_board_power_release_i2c`).
Those calls were not ported. This image has no charger owner.

## Bus transactions that stay V4

- PCA9956B writes and reads: `i2c_master_transmit` and
  `i2c_master_transmit_receive` in
  `firmware/v4-idf61/components/nocti_v4_i2c/nocti_v4_i2c.c`.
- Async scan payloads: `i2c_master_transmit` in
  `nocti_v4_async_led.c` (`submit_payload`).
- ADS131 frames: `spi_device_polling_transmit` in
  `nocti_v4_ads.c` `transfer_frame`.

Feat35 LED bring-up (`31fb2a9:main/adapters/nn2_led_bringup.c`) uses the same
IDF I2C calls with a different completion wait. That adapter was not
substituted for the V4 driver. PCA RESET is still released only inside
`program_sync_golden_path` after the I2C bus exists
(`nocti_v4_led.c`, low time then recovery).

## D6 register order

Owner decision, 2026-10-29 23:22 KST: follow D6 and pin brownout on.
D6 is ADR-0004 §3.8.1, from [#33 comment 6333972667](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333972667):
IREFALL=00h, then PWMALL=00h, then LEDOUT0–5=00h, then readback.
Register addresses in that section are IREFALL `40h` and PWMALL `3Fh`.

`write_d6_emitter_off` performs those three writes in that order, through
the existing `i2c_master_transmit` path. It then reads IREF0–23 and
LEDOUT0–5, including channels 10–13, and expects zero. Only after that
readback does init write MODE1/MODE2/GRPPWM/GRPFREQ and the existing
15 mA default IREF. LEDOUT stays the verified zero image, so the emitters
stay off while that default current is stored. The 30 mA cap is unchanged.
OE# is still not a GPIO. `nocti_v4_board_early_safe_init` leaves
`oe_state` unverified, and the D6 writes do not drive it.

A failed init still re-asserts PCA RESET. A successful init leaves RESET
released. ADR-0004 B4 also holds RESET# LOW after the zero sequence, as
the protocol STOPPED off-state. This image does not add that final hold
on the success path: the later LED owner writes LEDOUT only while RESET
is released, and no other function releases it. Early boot still holds
RESET until this golden path runs. That final-hold difference is static
only. It is not an electrical measurement.

## Brownout

`firmware/v4-idf61/sdkconfig.defaults` sets `CONFIG_ESP_BROWNOUT_DET=y`
and `CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7=y` (about 2.44 V). That choice is
the ESP-IDF v6.1 default for ESP32-S3
(`components/esp_hw_support/power_supply/port/esp32s3/Kconfig.power`,
default `ESP_BROWNOUT_DET_LVL_SEL_7`). The schematic text "LDO 5V"
(`Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json`) and
`docs/BOARD_POWER.md` do not state the module's 3V3 rail or a brownout
threshold. ADR-0004 §3.8.1 item 3 leaves the threshold TBD and says not
to invent one, so the IDF default is pinned instead of a new level.
This project does not use the repository-root CI profile overlays.
Those overlays build the root firmware, not this image.

## What still needs a board

- RESET and ADS reset stay low across `gpio_config`, including the pulldown.
- CS (active low, no pulldown) does not glitch low while its output is
  enabled.
- After a forced I2C or ADS init failure, `nn2ctl health` shows that
  `boot_stage` and START stays rejected until reboot.
- Charger behavior is unchanged because the charger path was not added.
