# NoctiNIRS 2 — Draft Kontrak Data Optik (v0.1, DRAFT)

Status: **DRAFT untuk review Owner. Belum normatif.** Belum ada kode yang mengikuti dokumen ini.
Disusun 2026-10-30 berdasarkan: laporan bench Owner (10 Hz, 60 s, lolos), `docs/adr/0004-host-protocol.md`
(§3.3 envelope, §3.16 waktu, §3.17.5 FRAME), `docs/V4_IDF61_SINGLE_CLOCK.md`, dan
`docs/OPTICAL_GEOMETRY_AND_WIRING_REFERENCE.md` (PR #80).

Perubahan format FRAME adalah perubahan kontrak kabel. ADR-0004 menyatakan perubahan itu hanya lewat ADR,
jadi dokumen ini harus dijadikan ADR baru (usulan: ADR-0009) atau amandemen §3.17.5 sebelum kode ditulis (lihat §12, D4).

---

## 0. Ringkasan satu halaman

1. **Satu jam.** Nomor sampel DRDY ADS131M04 adalah satu-satunya jam. Tidak ada jam kedua yang menentukan label ON/OFF.
2. **Firmware mencatat fakta, komputer memberi makna.** Firmware tahu *kapan* (indeks sampel) dan *apa yang terukur* (jumlah, sebaran).
   Komputer tahu *apa artinya* (nama saluran, jarak, OD, MBLL).
3. **Tidak ada angka praduga.** Sebuah nilai intensitas hanya dikirim sebagai angka bila bukti optik (tingkat 3, §2) lolos.
   Selain itu nilainya dikosongkan (sentinel) dan diberi alasan. Pengecualian hanya mode ENGINEERING yang bertanda jelas.
4. **Dua penilai independen.** Firmware menegakkan gerbang di sumber. Komputer mengulang pemeriksaan dari data yang diterima.
5. **Frame ringkas selalu; data mentah hanya untuk validasi.** Keduanya lewat envelope NN2 yang sudah ada (UDP 3333).

## 1. Pembagian tanggung jawab

| Tugas | Firmware | Komputer (host) |
|---|---|---|
| Klasifikasi sampel ON/OFF/settle per nomor DRDY | ✔ | verifikasi independen dari data mentah |
| Jumlah sampel (`n_on`, `n_off`), jumlah (`sum`), sebaran (`sd`), saturasi | ✔ | — |
| Gerbang bukti tingkat 3 dan pengosongan nilai | ✔ (penegak) | ✔ (pemeriksa kedua) |
| Nama saluran (PD, CH, SC), jarak, sisi, panjang gelombang | hanya indeks (slot, AIN) | ✔ dari `optical_geometry_v1.json` |
| Kontrol negatif (detektor kontralateral) | — | ✔ |
| Tingkat 4 (denyut jantung, SCI, uji angkat probe) | — | ✔ |
| Baseline I0, OD, MBLL, HbO/HbR | — | ✔ |
| Ambang gerbang (nilai) | menerima lewat perintah (hanya saat STOP) | ✔ menentukan dari kalibrasi bench |
| Kalibrasi arus per LED | menerapkan `config` | ✔ memutuskan nilai (lihat §6.6) |

## 2. Tangga bukti

| Tingkat | Bukti | Status data |
|---|---|---|
| 0 | Perintah LED dikirim | praduga |
| 1 | I2C selesai pada nomor sampel yang direncanakan | praduga (kondisi firmware saat ini) |
| 2 | Register PCA9956B dibaca ulang sesuai | bukti konfigurasi, bukan cahaya |
| 3 | **Sinyal optik**: ON−OFF signifikan secara statistik, jendela stabil, tidak saturasi | **VERIFIED** (satu-satunya yang boleh dikirim sebagai angka) |
| 4 | Fisiologis: pulsasi jantung di 735 dan 850 nm, SCI, uji angkat/tutup probe | di host, bukan gerbang firmware |

Catatan penting: tingkat 3 membuktikan "LED menyala dan cahaya sampai ke detektor", **bukan** "cahaya melewati jaringan"
(kebocoran langsung juga lolos tingkat 3). Itu sebabnya ada kontrol negatif (§10) dan tingkat 4.
Tingkat 3 juga tidak membuktikan identitas panjang gelombang 735 vs 850 nm (fotodioda silikon lebar-pita);
itu butuh uji terpisah (OPEN, §12 D7).

## 3. Transport

- Envelope NN2 (ADR-0004 §3.3): header 28 B, little-endian, CRC-32/ISO-HDLC di akhir, `msg_type = FRAME (2)`,
  satu envelope per datagram, tidak pernah difragmentasi, payload ≤ 1440 B, datagram ≤ 1472 B.
- Header `timestamp_us` = latch terukur DRDY pertama pada/ setelah batas slot-0 (ADR-0004 §3.16.c), bukan waktu kirim.
- Flag header bit 0 `SIMULATED` dan bit 1 `TS_ESTIMATED` tetap sesuai ADR-0004. Bit 2–15 tetap reserved (fail-closed).
- Payload FRAME diawali `adc_source:u8` (ADS131 = 1) lalu `frame_layout:u8`.
  - `frame_layout = 1`: layout lama ADR-0004 §3.17.5 (556 B, satu `channel` per slot). **Tidak cukup**: tidak membawa 4 detektor dan sebaran.
  - `frame_layout = 2`: **OPT2**, ringkasan terverifikasi per slot × 4 detektor (§4).
  - `frame_layout = 3`: **RAW**, sampel mentah bertag fase, hanya mode ENGINEERING (§5).
- Producer FRAME di Core 1 tidak pernah memblokir: antrean penuh tetap menetapkan `frame_sequence`, menaikkan `frames_dropped`, lalu membuang (ADR-0004 C6/D3).
- Proses pengirim berprioritas rendah (frame 5, net 4 sesuai `nocti_v4_rtos.h`); tidak menyentuh jalur timing core 1.

## 4. FRAME layout 2 (OPT2)

Semua angka little-endian, serialisasi per byte (tanpa packed struct, tanpa overlay), seperti `nn2_core`.

### 4.1 Prefix — 24 B

| Offset | Ukuran | Field | Tipe | Isi |
|---|---|---|---|---|
| 0 | 1 | `adc_source` | u8 | 1 = ADS131 |
| 1 | 1 | `frame_layout` | u8 | 2 |
| 2 | 2 | `slot_plan_epoch` | u16 | seperti ADR-0004 |
| 4 | 4 | `frame_sequence` | u32 | naik 1 per frame, wrap-safe |
| 8 | 4 | `first_sample_sequence` | u32 | indeks DRDY dari latch di header |
| 12 | 2 | `sample_count` | u16 | `slot_samples × 20` (800 di 10 Hz) |
| 14 | 2 | `frame_flags` | u16 | lihat 4.4 |
| 16 | 1 | `slot_count` | u8 | 20 |
| 17 | 1 | `det_count` | u8 | 4 |
| 18 | 2 | `geometry_map_id` | u16 | ID peta geometri yang dipakai host (firmware hanya membawa, tidak menafsirkan) |
| 20 | 2 | `gate_config_id` | u16 | versi set ambang yang aktif; 0 = gerbang nonaktif (ENGINEERING) |
| 22 | 2 | reserved | u16 | 0 |

### 4.2 Slot record — 64 B (20 buah, indeks = `slot_index`)

| Offset | Ukuran | Field | Tipe | Isi |
|---|---|---|---|---|
| 0 | 1 | `slot_index` | u8 | 0..19 |
| 1 | 1 | `quality` | u8 | 0 UNVERIFIED, 1 VERIFIED_MIN, 2 VERIFIED_TARGET, 3 WARMUP, 4 ABORTED_BY_STOP |
| 2 | 2 | `reason` | u16 | bit di bawah |
| 4 | 1 | `n_on_used` | u8 | |
| 5 | 1 | `n_off_used` | u8 | |
| 6 | 1 | `n_on_available` | u8 | |
| 7 | 1 | `n_off_available` | u8 | |
| 8 | 1 | `on_commit_late` | u8 | keterlambatan edge ON dalam sampel (0 = tepat waktu; saturasi di 255) |
| 9 | 1 | `off_commit_late` | u8 | idem untuk OFF |
| 10 | 1 | `witness_ain` | u8 | AIN dengan z tertinggi yang lolos; `0xFF` = tidak ada saksi |
| 11 | 1 | `verified_mask` | u8 | bit d = nilai AIN d terverifikasi dan dikirim |
| 12 | 13 | `det[0]` | — | AIN0 |
| 25 | 13 | `det[1]` | — | AIN1 |
| 38 | 13 | `det[2]` | — | AIN2 |
| 51 | 13 | `det[3]` | — | AIN3 |

Detektor record — 13 B:

| Offset | Ukuran | Field | Tipe | Isi |
|---|---|---|---|---|
| 0 | 4 | `sum_on` | i32 | jumlah tepat `n_on_used` kode ADC (mean = sum / n); `INT32_MIN` bila dikosongkan |
| 4 | 4 | `sum_off` | i32 | idem; `INT32_MIN` bila dikosongkan |
| 8 | 2 | `sd_on_q4` | u16 | simpangan baku sampel × 16, saturasi; `0xFFFF` bila dikosongkan |
| 10 | 2 | `sd_off_q4` | u16 | idem |
| 12 | 1 | `det_flags` | u8 | bit0 SAT_ON, bit1 SAT_OFF, bit2 STEP_Z_PASS, bit3 STABLE_PASS, bit4 FLOOR_PASS, bit5 DIFF_NEGATIVE, bit6–7 reserved |

Ukuran: 24 + 20 × 64 = **1304 B** payload; datagram 28 + 1304 + 4 = **1336 B** (sisa 136 B payload).
`sum` memakai i32 karena K ≤ 255 × 8.388.607 < 2³¹ (`_Static_assert(K_TARGET <= 255)`).
Mengirim `sum` + `n` (bukan mean) menghindari pembulatan: `latest_contiguous_mean()` saat ini memotong hasil bagi ke integer.

### 4.3 `reason` (u16)

Bit 0–9 sama dengan `nocti_v4_optical.h` (`UNDER_K_ON`, `UNDER_K_OFF`, `LED_ON_MISSING`, `LED_OFF_MISSING`,
`RUNTIME_TIMING`, `ADS_INTEGRITY`, `STOP_ABORT`, `STALE_EPOCH`, `INTERNAL`, `STARTUP_WARMUP`). Tambahan:

| Bit | Nama | Arti |
|---|---|---|
| 10 | `NO_WITNESS` | tidak ada detektor yang lolos sebagai saksi untuk slot ini |
| 11 | `WITNESS_UNSTABLE` | saksi ada tetapi jendelanya tidak stabil |
| 12–15 | reserved | 0 |

Aturan: `reason != 0` (kecuali WARMUP) ⇒ `quality = UNVERIFIED` ⇒ `verified_mask = 0` ⇒ semua `sum`/`sd` dikosongkan.

### 4.4 `frame_flags`

| Bit | Nama | Arti |
|---|---|---|
| 0 | `WARMUP` | frame pemanasan (ADR-0004) |
| 1 | `PAIR_SAME_SLOT_OFF_AFTER_ON` | seperti ADR-0004 |
| 2 | `ENGINEERING_UNGATED` | gerbang dimatikan: nilai TIDAK dikosongkan, seluruh frame **bukan data ilmiah** |
| 3 | `STARTING_GATE` | frame dibuat saat gerbang start (§6.5), bukan data ilmiah |
| 4–15 | reserved | 0 |

## 5. FRAME layout 3 (RAW) — hanya ENGINEERING

Tujuan: validasi independen di komputer (cari tepi langkah LED di data, ukur settle sebenarnya, ukur noise). Tidak dipakai untuk sains.

Prefix 16 B: `adc_source:u8`, `frame_layout:u8 (=3)`, `slot_plan_epoch:u16`, `first_sample_sequence:u32`, `sample_count:u16`,
`raw_flags:u16` (bit0 `ENGINEERING` selalu 1; bit1 `GAP_BEFORE` bila ada data terbuang sebelum paket ini),
`geometry_map_id:u16`, reserved `u16`.

Sampel 14 B, berurutan (`indeks DRDY = first_sample_sequence + i`):
`ain0..ain3` masing-masing int24 LE (urutan AIN, 12 B), `phase_class:u8`, `slot_index:u8`.

`phase_class` = `nocti_v4_sample_class_t`: 0 OUTSIDE, 1 PRECOMMIT, 2 COMMIT_LATE, 3 ON_SETTLE, 4 ON_STABLE,
5 OFF_TRANSITION, 6 OFF_SETTLE, 7 OFF_STABLE, 8 END_GUARD.

N sampel per paket ≤ 100 (payload 16 + 100×14 = 1416 B; datagram 1448 B; ±80 paket/s ≈ 116 kB/s ≈ 0,93 Mbit/s).
Tag fase baru tersedia setelah slot difinalisasi, jadi paket mentah tertunda ≤ 1 slot. Itu dapat diterima untuk analisis offline.
Kehilangan paket terdeteksi dari diskontinuitas `first_sample_sequence`.

## 6. Gerbang bukti (firmware)

### 6.1 Besaran per (slot, detektor d)

- `diff = mean_on − mean_off`, `SE = sqrt(sd_on²/n_on + sd_off²/n_off)`, `SE_eff = max(SE, se_floor)`.
- `STEP_Z_PASS = diff ≥ z · SE_eff` (z = `z_witness` untuk saksi, `z_channel` untuk nilai kanal).
- `STABLE_PASS`: selisih rata-rata separuh-tua dan separuh-baru jendela ON, dan jendela OFF, masing-masing ≤ `stable_k · sd · sqrt(2/n_half)`.
- `SAT_*`: ada sampel dengan |kode| ≥ `sat_code` pada jendela terkait.
- `FLOOR_PASS = diff ≥ floor_counts`.

### 6.2 Keputusan

- **Saksi** = AIN mana pun yang lolos `STEP_Z_PASS(z_witness) ∧ STABLE_PASS ∧ ¬SAT ∧ FLOOR_PASS`.
  Firmware tidak perlu tahu geometri: kebocoran cahaya pun membuktikan LED menyala.
- `slot VERIFIED` ⇔ ada saksi ∧ `reason` bit 0–9 bersih.
- `verified_mask[d]` ⇔ slot VERIFIED ∧ `STEP_Z_PASS(z_channel)` ∧ `STABLE_PASS` ∧ `¬SAT` ∧ `FLOOR_PASS` untuk d.
- Nilai d dengan `verified_mask[d] = 0` dikosongkan (sentinel); `det_flags` tetap dikirim untuk diagnosis.

### 6.3 Parameter (nilai awal semuanya **TBD — ditentukan dari bench kotak gelap, bukan ditebak**)

| Parameter | Nilai awal sementara | Catatan |
|---|---|---|
| `z_witness` | 5,0 | |
| `z_channel` | 5,0 | boleh lebih longgar dari `z_witness` setelah ada data |
| `stable_k` | 3,0 | |
| `sat_code` | 7.969.177 (95 % skala penuh) | |
| `floor_counts` | 4 | |
| `se_floor` | 0,3 count | mencegah SE ≈ 0 akibat kuantisasi |
| `no_witness_stop_ms` | 3000 | §6.4 |
| `gate_config_id` | 1 | naik tiap perubahan set ambang |

Parameter hanya boleh diubah saat STOP (perintah `set_gate`, §8).

### 6.4 Aturan hentikan

Jika tidak ada satu pun slot ber-saksi selama `no_witness_stop_ms` berturut-turut selama STREAMING,
firmware menghentikan scan (LED OFF terkonfirmasi) dengan `end_reason = NO_OPTICAL_EVIDENCE`. (Default Owner: mask + hentikan; lihat D1.)

### 6.5 Gerbang start

Tambahan pada fase STARTING (tanpa menambah state ke mesin keadaan ADR-0004): setelah frame pemanasan, firmware menilai M frame
(default M = 4) dan baru masuk STREAMING bila minimal `selfcheck_min_witness_slots` slot (default 20 = semuanya) ber-saksi di setiap frame.
Jika gagal, kembali STOPPED dengan `end_reason = NO_OPTICAL_EVIDENCE` dan EVENT yang memuat slot gagal. Frame pada fase ini berflag `STARTING_GATE`.

### 6.6 Mode ENGINEERING dan kalibrasi

`set_mode` (hanya saat STOP) mengaktifkan ENGINEERING: gerbang mati (`gate_config_id = 0`), nilai tidak dikosongkan,
semua frame berflag `ENGINEERING_UNGATED`; RAW opsional. Mode ini untuk menentukan ambang dan kalibrasi arus.
Kalibrasi arus per LED dikerjakan **oleh host**: jalankan sesi ENGINEERING pada arus uji (`config`, saat STOP),
baca `sum`/`sd`/saturasi, hitung arus yang menjaga detektor terdekat ≤ ~70 % skala penuh dengan saksi z ≥ target,
lalu kirim `config` hasilnya. Firmware tidak punya state kalibrasi.

## 7. SESSION_REPORT / EVENT (Proposed; encoding final menunggu ADR)

Tambahan SESSION_REPORT: `adc_fs_measured_ppm:i32` (DRDY terhadap GPTimer selama sesi), `geometry_map_id`, `gate_config_id`,
hitungan slot VERIFIED / UNVERIFIED per slot, hitungan `verified_mask` per (slot, AIN), `frames_dropped`, `end_reason`.
EVENT: `STARTING_GATE_FAILED` (memuat daftar slot gagal), `NO_OPTICAL_EVIDENCE`.

## 8. Perintah kontrol baru (Proposed; registry ADR-0004 perlu diperbarui)

| Perintah | Hanya STOP? | Argumen |
|---|---|---|
| `set_gate` | ya | `z_witness_x10:u16`, `z_channel_x10:u16`, `stable_k_x10:u16`, `sat_code:i32`, `floor_counts:u16`, `se_floor_x100:u16`, `no_witness_stop_ms:u16`, `gate_config_id:u16` |
| `set_mode` | ya | `engineering:u8`, `raw:u8`, `geometry_map_id:u16` |
| `config` (ada) | ya | arus per LED (batas arus: lihat D3) |

Nilai di luar rentang → `INVALID_ARG` (bukan `BAD_FRAME`), mengikuti ADR-0004 §3.2.

## 9. Waktu dan urutan

Mengikuti ADR-0004 §3.16: waktu host = `anchor / fs_terukur` + epoch sesi; group delay dilaporkan, tidak dikurangi.
Kehilangan frame = diskontinuitas `frame_sequence`, dicocokkan dengan Δ`frames_dropped` di SESSION_REPORT.
`fs_terukur` dari SESSION_REPORT dipakai untuk konversi waktu, bukan konstanta 8000.

## 10. Sisi komputer

1. **Geometri** dari `optical_geometry_v1.draft.json` (file di samping dokumen ini). Konversi AIN→PD/CH dilakukan **satu kali, di satu modul**;
   hash file dicatat di header setiap berkas rekaman. Jangan diterapkan dua kali (peringatan di dokumen PR #80 §3).
2. **Pemeriksa kedua:** hitung ulang z dari `sum`, `sd`, `n` yang diterima; bandingkan dengan `det_flags`. Selisih = bug.
3. **Kontrol negatif:** untuk slot bersumber di sisi X, AIN di sisi lawan harus `STEP_Z_PASS = 0`. Jika tidak, tandai `LEAK_SUSPECT` dan keluarkan slot dari himpunan sains sampai diselidiki.
4. **Cocokkan tepi (data RAW):** pada detektor saksi, cari sampel saat sinyal melewati 50 % antara baseline OFF dan plateau ON; harus jatuh di
   `[first_affected, first_affected + settle]`. Hasilnya adalah ukuran settle sebenarnya (menguji asumsi "+1" dan settle 4 sampel).
5. **Sidik jari posisi:** rasio intensitas PD1/PD2 (dan PD4/PD3) per sumber sesuai jarak geometri; LED yang tertukar mengubah pola.
6. **Tingkat 4:** pulsasi jantung pada kedua panjang gelombang, SCI, uji angkat/tutup probe.
7. **OD:** `OD = −ln(I_clean / I0)`, dengan `I_clean = (sum_on/n_on) − (sum_off/n_off)`; nilai tidak-terverifikasi tidak masuk perhitungan.

## 11. Anggaran bandwidth

| Aliran | Ukuran datagram | Laju | Throughput |
|---|---|---|---|
| OPT2 (layout 2) | 1336 B | 2–10 frame/s | 2,7–13,4 kB/s |
| RAW (layout 3) | 1448 B | ±80 paket/s | ±116 kB/s (≈0,93 Mbit/s) |

RAW untuk sesi pendek saja; ukur noise ADC dengan WiFi diam versus mengalir.

## 12. Keputusan terbuka

| ID | Keputusan | Usulan |
|---|---|---|
| D1 | Slot gagal bukti: kirim dengan mask atau buang frame | mask + aturan hentikan (§6.4); **Owner belum memutuskan** |
| D2 | `lead` 2 atau 3 sampel | ukur histogram keterlambatan dulu; hari ini semua edge terlambat tepat 1 sampel |
| D3 | Batas arus LED: 30 mA (`control_start` menolak `current_ma > 30`) atau 50 mA (pernyataan Owner) | Owner putuskan sebelum kalibrasi arus |
| D4 | Jalur dokumen: ADR baru atau amandemen ADR-0004 §3.17.5 | ADR-0009 baru |
| D5 | Nilai ambang gerbang | dari bench kotak gelap |
| D6 | Nasib stream V4 lama (port 3334, datagram 1184 B, CRC-16) | tidak diperluas; putuskan hapus/pertahankan terpisah |
| D7 | Verifikasi identitas panjang gelombang (735 vs 850) | uji filter/spektrometer di luar gerbang |

## 13. Kriteria penerimaan

- Vektor emas (host test): jendela LED-ON normal, tanpa langkah, jenuh, ramp tidak stabil, saluran jauh lemah, `n < K_min`; hasil gerbang dan pengosongan sesuai tabel yang dibekukan.
- Pembulat layout: `_Static_assert` ukuran 24 / 64 / 13 / 1304 dan 16 / 14 / 1416; enkode-dekode round-trip identik; CRC-32 dicek; paket uji simulator lewat parser host.
- Tidak ada regresi timing (acuan bench Owner, 10 Hz, 60 s): `fault=0`, `edge_miss=0`, `adc_missed=0`, `gap=0`, `crc_bad=0`, `spi_errors=0`,
  `spi_max ≤ ~60 µs`, `service_max ≤ ~100 µs`, `sched_deferred_max` tidak naik berarti, `on_d/off_d` tetap 1 atau lebih baik.
  Kemudian ulangi 10 menit di 10 Hz dan di 2/4/8 Hz.
- `frames=supported` di `info` hanya setelah pengirim FRAME bekerja dan lolos tes di atas.
- Bench kotak gelap menghasilkan ambang; kontrol negatif bersih; tepi ON/OFF di data RAW cocok dengan klaim firmware.

## Lampiran A — peta geometri

Lihat `optical_geometry_v1.draft.json`: 10 sumber (L1–L10, kiri/kanan), 4 detektor (PD1→AIN2, PD2→AIN3, PD3→AIN0, PD4→AIN1),
20 saluran terpilih (CH1–CH16, SC1L, SC2L, SC1R, SC2R). Uji konsistensi: jarak yang tercatat menyiratkan spasi PD1–PD2 11,5–13 mm (dan PD4–PD3 sama);
ukur dengan kaliper sebagai pengecekan tambahan.
