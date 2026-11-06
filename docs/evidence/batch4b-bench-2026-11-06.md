# Batch 4b bench record (key fields only)

Source: terminal output pasted by the Owner, 2026-11-06. Firmware commit `c6c9e7bfe16599f545bc920abf2abd78fc599d60`,
`boot_id` 168, target ESP32-S3 (COM11), SoftAP, 10 Hz. Per-slot arrays (`async_i2c`, `sync.slots`) are omitted here.
Class: on-target bench (Owner). Not independently reproduced.

## Boot
`BOOT_V4_7_NETWORK_INIT OK`; `NN2_HEAP before_wifi_init free=79496 largest=31744`;
`NN2_HEAP after_wifi_start free=28228 largest=19456`; SoftAP up; one station joined (AID 1).
`check_ram_budget.py`: `.dram0.data` 25192, `.dram0.bss` 172376 (UNSET gate), all registered symbols OK.

## ACQ, 600 s (`run --rate 10 --seconds 600 --led`)
`adc_ok` 4797232, `adc_missed` 0, `adc_sequence_gap` 0, `crc_bad` 0, `spi_errors` 0, `fault` 0, `edge_miss` 0, `gap` 0,
`sched_error` 0, `spi_min/max/avg` 32/59/32 us, `service_max` 73, `lag_max` 31, `late_1` 239839, `late_2` 23, `late_3p` 0,
`late_max` 2, `sched_deferred_max` 254, `raw_depth_max` 23. Stack free (bytes): ads 1760, led 1708, sched 4704, optical 4812,
frame 588, dark 3272, ctrl 5724, net 4476. Netdiag after: `heap_free_now` 18576, `heap_free_min` 10076,
`largest_free_block_now` 7680, `frame_send_errors` 0.

## ENGINEERING
Before capture (`netdiag`): `heap_free_now` 13196, `heap_free_min` 9728, `largest_free_block_now` 7680, `frame_send_errors` 0.
`nn2_raw_capture.py --seconds 60 --rate 10`: `packets=11821 samples=472844 gap=0 breaks=53 lost_in_breaks=4400 invalid=0`.
After capture (`netdiag`): `heap_free_now` 13204, `heap_free_min` 6448, `largest_free_block_now` 7680,
`frame_send_errors` 110, `frame_send_other` 110, `frame_send_eagain` 0, `frame_send_enobufs` 0, `frame_last_errno` 12,
`net_send_errors` 110. `health`: `adc_ok` 477287, `adc_missed` 0, `spi_errors` 0, `crc_bad` 0, `sched_error` -1 (known 4a finding),
`late_max` 2, `raw_packets_sent` 11821, `raw_dropped_samples` 0, `raw_buffer_highwater` 80, `raw_depth_max` 19.
After `set-mode ACQ` (`netdiag`): `heap_free_now` 18584, `heap_free_min` 6448 (minimum never rises), `frame_send_errors` 110.

Derived: 110 failed sends = 4400 / 40; 110 / (11821 + 110) = 0.92 %; mean samples per received packet 472844 / 11821 = 40.0.
