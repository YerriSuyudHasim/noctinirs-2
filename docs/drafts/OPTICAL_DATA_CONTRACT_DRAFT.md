# NoctiNIRS 2 — Draft Kontrak Data Optik (v0.4, DRAFT)

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
| 1 | 1 | `quality` | u8 | 0 INVALID (UNDER_K atau gagal gerbang; lihat `reason`), 1 VALID_MINIMUM (`K_min` ≤ n < `K_target`), 2 VALID_TARGET (n ≥ `K_target`), 3 ABORTED_BY_STOP, 4 STARTUP_WARMUP. Nilai 0–4 sama dengan `nocti_v4_slot_quality_t` di `nocti_v4_optical.h` (koreksi v0.2: draft v0.1 menukar 3 dan 4) |
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
| 8 | 2 | `sd_on_q4` | u16 | simpangan baku **sampel** (pembagi n−1; butuh n ≥ 2) × 16, dibulatkan ke terdekat; `0xFFFE` = terpotong (sd ≥ 4095,875 count); `0xFFFF` = dikosongkan |
| 10 | 2 | `sd_off_q4` | u16 | idem |
| 12 | 1 | `det_flags` | u8 | bit0 SAT_ON, bit1 SAT_OFF, bit2 STEP_Z_PASS, bit3 STABLE_PASS, bit4 FLOOR_PASS, bit5 DIFF_NEGATIVE, bit6 GATE_OFF (1 hanya pada frame ENGINEERING_UNGATED), bit7 reserved (0) |

Ukuran: 24 + 20 × 64 + 80 (blok `stab_q`, §4.5) = **1384 B** payload; datagram 28 + 1384 + 4 = **1416 B** (sisa 56 B payload).
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

Aturan (v0.4.1): `reason != 0` ⇒ `quality ∈ {0, 3, 4}`: bit 6 `STOP_ABORT` ⇒ `quality = 3`; bit 9 `STARTUP_WARMUP` ⇒ `quality = 4`; selain itu `quality = 0`. Dalam ketiganya `verified_mask = 0` ⇒ semua `sum`/`sd` dikosongkan (kecuali frame ENGINEERING, di bawah). `quality` 1 dan 2 selalu `reason = 0`; `BELOW_TARGET` diwakili oleh `quality = 1` sendiri (ADR-0004 §3.17 menyebut bit `BELOW_TARGET`, tetapi kode firmware tidak memilikinya; ikuti kode). Pada `quality` 1 atau 2, `verified_mask` boleh himpunan bagian detektor mana pun (detektor yang gagal gerbang dikosongkan).

**Pengecualian ENGINEERING (v0.4).** Bila `frame_flags` bit `ENGINEERING_UNGATED` = 1: `verified_mask` = 0, `witness_ain` = `0xFF`, `reason` bit 10 dan 11 = 0, `det_flags` bit 2–5 = 0 (tidak dievaluasi) dan bit 6 `GATE_OFF` = 1. Nilai TIDAK dikosongkan menurut aturan di atas; ia terisi bila dapat dihitung dari sampel yang ada (`sum` bila n ≥ 1, selain itu `INT32_MIN`; `sd_*_q4` bila n ≥ 2, selain itu `0xFFFF`), termasuk slot `quality` 0, 3, atau 4. `SAT_ON`/`SAT_OFF` tetap dievaluasi dengan `sat_code` tetap 7.969.177; `stab_q` dihitung bila n ≥ 4. Frame ini bukan data sains dan tidak boleh diperlakukan sebagai sains oleh penerima. Tanpa bit itu, aturan di atas berlaku penuh dan encoder menolak masukan yang menaruh nilai pada detektor ber-`verified_mask` bit 0.

### 4.4 `frame_flags`

| Bit | Nama | Arti |
|---|---|---|
| 0 | `WARMUP` | frame pemanasan (ADR-0004) |
| 1 | `PAIR_SAME_SLOT_OFF_AFTER_ON` | seperti ADR-0004 |
| 2 | `ENGINEERING_UNGATED` | gerbang dimatikan: nilai TIDAK dikosongkan, seluruh frame **bukan data ilmiah** |
| 3 | `STARTING_GATE` | frame dibuat saat gerbang start (§6.5), bukan data ilmiah |
| 4–15 | reserved | 0 |

### 4.5 Blok `stab_q` — 80 B (diterima Owner 2026-10-31; definisi dipertegas v0.4)

Mengikuti 20 slot record. 20 slot × 4 detektor, urutan `slot_index` lalu AIN (indeks = `slot_index × 4 + AIN`), 1 B per entri. Dihitung firmware dari data eksak internal lalu dikuantisasi; komputer tidak dapat menghitung ulang `stab_q` (butuh rata-rata separuh jendela), hanya memeriksa ambangnya (§6.1).

- `n_half = floor(n / 2)`. Separuh tua = `n_half` sampel pertama jendela, separuh baru = `n_half` sampel terakhir. Bila n ganjil, sampel tengah dikeluarkan dari keduanya.
- `sd` = simpangan baku sampel SELURUH jendela (n sampel, pembagi n−1), bukan per separuh.
- `r = |mean_baru − mean_tua| / max( sd · sqrt(2 / n_half), floor_se )`, dengan `floor_se = se_floor_x100 / 100` (sama dengan §6.1). Dihitung untuk jendela ON dan OFF.
- `stab_q = round-to-nearest(16 × max(r_on, r_off))`, terpotong di `0xFE`.
- `0xFE` = terpotong (nilai sebenarnya ≥ 254/16); SELALU gagal STABLE. `0xFF` = tidak dihitung (n < 4 pada salah satu jendela, atau nilai dikosongkan); juga gagal STABLE.
- Pada frame bergerbang, detektor yang dikosongkan (`verified_mask` bit 0) SELALU `stab_q = 0xFF`; diagnosisnya lewat `det_flags` (STEP_Z, STABLE, FLOOR, SAT tetap dikirim). Karena itu "nilai dikosongkan ⇒ `stab_q = 0xFF`" berlaku di frame bergerbang. Pada frame ENGINEERING `stab_q` dihitung bila n ≥ 4.

## 5. FRAME layout 3 (RAW) — hanya ENGINEERING

Tujuan: validasi independen di komputer (cari tepi langkah LED di data, ukur settle sebenarnya, ukur noise). Tidak dipakai untuk sains.

Prefix 16 B: `adc_source:u8`, `frame_layout:u8 (=3)`, `slot_plan_epoch:u16`, `first_sample_sequence:u32`, `sample_count:u16`,
`raw_flags:u16` (bit0 `ENGINEERING` selalu 1; bit1 `GAP_BEFORE` bila ada data terbuang sebelum paket ini),
`geometry_map_id:u16`, reserved `u16`.

Sampel 14 B, berurutan (`indeks DRDY = first_sample_sequence + i`):
`ain0..ain3` masing-masing int24 LE (urutan AIN, 12 B), `phase_class:u8`, `slot_index:u8`.

`phase_class` = `nocti_v4_sample_class_t`: 0 OUTSIDE, 1 PRECOMMIT, 2 COMMIT_LATE, 3 ON_SETTLE, 4 ON_STABLE,
5 OFF_TRANSITION, 6 OFF_SETTLE, 7 OFF_STABLE, 8 END_GUARD.

Definisi (v0.4): `sample_count` = jumlah sampel dalam paket ini (≤ 100). `int24` = dua-komplemen little-endian, di-sign-extend ke i32.
Sampel dengan `phase_class = OUTSIDE (0)` wajib `slot_index = 0xFF`; kombinasi lain ditolak encoder. `first_affected` tidak punya field sendiri; turunkan dari `phase_class` pada slot yang sama:
`first_affected_ON` = sampel pertama berkelas `ON_SETTLE` (atau `ON_STABLE` bila settle 0); `first_affected_OFF` = sampel pertama berkelas `OFF_SETTLE` (atau `OFF_STABLE`).
Sampel sebelumnya dalam slot berkelas `COMMIT_LATE` (komit terlambat) atau `PRECOMMIT`; sisi OFF berkelas `OFF_TRANSITION`.
Kekecualian: komit EARLY (`first_affected` < tepi rencana) tidak terbaca dari kelas karena kelas tidak menarik fase baru lebih awal dari tepi nominal; bila `sample_clock_early_commits` > 0 pada sesi RAW, alat analisis menandai sesi itu "butuh catatan tepi eksplisit".

N sampel per paket ≤ 100 (payload 16 + 100×14 = 1416 B; datagram 1448 B; ±80 paket/s ≈ 116 kB/s ≈ 0,93 Mbit/s).
Tag fase baru tersedia setelah slot difinalisasi, jadi paket mentah tertunda ≤ 1 slot. Itu dapat diterima untuk analisis offline.
Kehilangan paket terdeteksi dari diskontinuitas `first_sample_sequence`.

## 6. Gerbang bukti (firmware)

### 6.1 Besaran per (slot, detektor d) — aritmetika tetap (v0.4)

Gerbang memutuskan dari NILAI YANG DIKIRIM di paket, bukan dari nilai eksak internal. Firmware dan komputer harus menghasilkan keputusan identik sampai bit terakhir; tidak ada toleransi, selisih = bug.

Masukan dari kawat: `n_on` = `n_on_used`, `n_off` = `n_off_used`, `sum_on`, `sum_off`, `sd_on_q4`, `sd_off_q4`, `stab_q`. Parameter dari `gate_config_id`: `z_x10` (`z_witness_x10` untuk saksi, `z_channel_x10` untuk nilai kanal), `stable_k_x10`, `floor_counts`, `se_floor_x100`.

Aturan awal: bila `n_on = 0`, `n_off = 0`, `sum_*` = `INT32_MIN`, atau `sd_*_q4` = `0xFFFF`, maka `STEP_Z_PASS` = `FLOOR_PASS` = `STABLE_PASS` = 0 dan tidak ada perhitungan lain. `0xFFFE` dipakai apa adanya sebagai 4095,875.

Aritmetika: binary64 (`double`), urutan operasi tetap, tanpa fused multiply-add dan tanpa fast-math (C: `-ffp-contract=off`, tidak ada `-ffast-math`). Operasi + − × ÷ dan `sqrt` dibulatkan tepat menurut IEEE-754, sehingga C dan Python `float` identik. Tiap langkah ditulis sebagai pernyataan terpisah:

```
a = (double)sum_on / (double)n_on        b = (double)sum_off / (double)n_off
diff = a - b
s_on = (double)sd_on_q4 / 16.0           s_off = (double)sd_off_q4 / 16.0
v_on = s_on * s_on / (double)n_on        v_off = s_off * s_off / (double)n_off
se = sqrt(v_on + v_off)
floor_se = (double)se_floor_x100 / 100.0
se_eff = (se > floor_se) ? se : floor_se
z = (double)z_x10 / 10.0
thr = z * se_eff
STEP_Z_PASS = (diff >= thr)
FLOOR_PASS  = (diff >= (double)floor_counts)
DIFF_NEGATIVE = (diff < 0.0)
STABLE_PASS = (stab_q < 0xFE) && (10 * (int)stab_q <= 16 * (int)stable_k_x10)   /* integer murni */
```

Contoh `stable_k` = 2,3 (`stable_k_x10` = 23): `10·stab_q ≤ 368`, jadi `stab_q ≤ 36`. `stab_q` = `0xFE` dan `0xFF` selalu gagal.
`SAT_*`: ada sampel dengan |kode| ≥ `sat_code` pada jendela terkait (dievaluasi firmware dari sampel; komputer mempercayai `det_flags` dan mengujinya lewat RAW).

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
Jika gagal, kembali STOPPED dengan `end_reason = NO_OPTICAL_EVIDENCE` dan EVENT yang memuat slot gagal. Frame pada fase ini berflag `STARTING_GATE`. Aturan gerbang berlaku normal pada frame ini (`verified_mask` boleh 1 dan nilai terisi bila lulus), tetapi frame ini BUKAN data sains: penerima tidak memasukkannya ke himpunan sains.

### 6.6 Mode ENGINEERING dan kalibrasi

`set_mode` (hanya saat STOP) mengaktifkan ENGINEERING: gerbang mati (`gate_config_id = 0`), nilai tidak dikosongkan,
semua frame berflag `ENGINEERING_UNGATED`; RAW opsional. Mode ini untuk menentukan ambang dan kalibrasi arus.
Kalibrasi arus per LED dikerjakan **oleh host**: jalankan sesi ENGINEERING pada arus uji (`config`, saat STOP),
baca `sum`/`sd`/saturasi, hitung arus yang menjaga detektor terdekat ≤ ~70 % skala penuh dengan saksi z ≥ target,
lalu kirim `config` hasilnya. Firmware tidak punya state kalibrasi.

Rincian bidang pada frame ENGINEERING: lihat "Pengecualian ENGINEERING" di §4.3.

## 7. SESSION_REPORT / EVENT (Proposed; encoding final menunggu ADR)

Tambahan SESSION_REPORT: `adc_fs_measured_ppm:i32` (DRDY terhadap GPTimer selama sesi), `geometry_map_id`, `gate_config_id`,
hitungan slot VERIFIED / UNVERIFIED per slot, hitungan `verified_mask` per (slot, AIN), `frames_dropped`, `end_reason`.
EVENT: `STARTING_GATE_FAILED` (memuat daftar slot gagal), `NO_OPTICAL_EVIDENCE`.

### 7.1 Metadata sesi (USULAN; encoding ditugaskan ADR-0004 ke sisi host/Agent 03, belum diputuskan)

ADR-0004 mewajibkan `get_config` dan setiap awal sesi (balasan `start_stream`) membawa parameter yang tidak boleh ditebak host. Usulan isi (struktur tetap, little-endian, versi di byte pertama): `meta_version:u8`, `k_min:u8`, `k_target:u8`, `warmup_frames:u8`, `settle_on_samples:u8`, `settle_off_samples:u8`, `guard_samples:u8`, `lead_samples:u8`,
`slot_samples:u16`, `off_samples:u16`, `fs_frame_hz:u8`, `mode_flags:u8` (bit0 ENGINEERING, bit1 RAW), `adc_fs_hz:u32`, `sat_code:i32`, `geometry_map_id:u16`, `gate_config_id:u16`, `z_witness_x10:u16`, `z_channel_x10:u16`, `stable_k_x10:u16`, `floor_counts:u16`, `se_floor_x100:u16`, `no_witness_stop_ms:u16`.
Definisi (v0.4.1, sesuai kode `nocti_v4_sample_clock.h`): `slot_samples` = panjang slot dalam sampel ADC (= `adc_fs_hz / (fs_frame_hz × 20)`; 40 di 10 Hz). `off_samples` = jumlah sampel dari tepi ON sampai tepi OFF dalam slot (LED menyala 18 sampel di 10 Hz). `lead_samples` = perintah I2C ke LED dikirim sekian sampel sebelum tepi yang direncanakan (`on_submit = on_edge − lead`; 2). `guard_samples` = sampel di akhir slot yang tidak dipakai (`END_GUARD`; 1). `settle_*_samples` = sampel pertama setelah `first_affected` yang tidak dipakai (kode saat ini satu nilai untuk ON dan OFF: 4). Saturasi: `|kode| ≥ sat_code` berlaku simetris untuk sisi atas dan bawah.
Penghitung yang hanya diketahui di akhir sesi (`sample_clock_early_commits`, `late_1/2/3p`, `frames_dropped`, `adc_fs_measured_ppm`) masuk SESSION_REPORT (§7), bukan metadata awal.
Sampai encoding diputuskan, host memakai berkas konfigurasi sesi buatan host yang diberi tanda "sementara, bukan format perangkat".

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

8. **Pemeriksa kedua hanya satu arah pada frame bergerbang (v0.4.1).** Komputer dapat menangkap *lulus-palsu* (nilai terkirim yang seharusnya gagal gerbang), karena nilainya ada. Komputer TIDAK dapat melihat *penolakan-palsu* (nilai yang dikosongkan firmware padahal seharusnya lulus) karena nilainya tidak dikirim. Penolakan-palsu hanya dapat diukur di mode ENGINEERING (nilai tidak dikosongkan) dan lewat analisis RAW offline (tes kotak gelap). Laporan penerima tidak boleh menyiratkan jaminan dua arah.

## 11. Anggaran bandwidth

| Aliran | Ukuran datagram | Laju | Throughput |
|---|---|---|---|
| OPT2 (layout 2) | 1416 B | 2–10 frame/s | 2,8–14,2 kB/s |
| RAW (layout 3) | 1448 B | ±80 paket/s | ±116 kB/s (≈0,93 Mbit/s) |

RAW untuk sesi pendek saja; ukur noise ADC dengan WiFi diam versus mengalir.

## 12. Keputusan terbuka

| ID | Keputusan | Usulan |
|---|---|---|
| D1 | Slot gagal bukti: kirim dengan mask atau buang frame | **Diputuskan Owner 2026-10-31:** nilai dibuang (tidak pernah keluar sebagai angka); penanda kosong + alasan tetap dikirim agar komputer bisa membedakan lubang dari paket hilang, menjaga garis waktu, dan menjalankan aturan hentikan (§6.4) |
| D2 | `lead` 2 atau 3 sampel (`NOCTI_V4_SAMPLE_LED_LEAD`: perintah ke LED dikirim sekian sampel sebelum tepi yang diinginkan) | **Diputuskan Owner 2026-10-31:** tetap 2; tinjau ulang setelah tes kotak gelap menunjukkan kapan cahaya benar-benar muncul |
| D3 | Batas arus LED | **Diputuskan Owner 2026-10-30:** default 30 mA, maksimum 50 mA (sudah diterapkan di Batch 1b; PR #82) |
| D4 | Jalur dokumen: ADR baru atau amandemen ADR-0004 §3.17.5 | **Diputuskan Owner 2026-10-31:** draft ini diresmikan sebagai ADR setelah tes kotak gelap dan simulator lolos; sampai saat itu berstatus DRAFT |
| D5 | Nilai ambang gerbang | **Diputuskan Owner 2026-10-31:** ditentukan dari data bench kotak gelap, tidak ditebak |
| D6 | Nasib stream V4 lama (port 3334, datagram 1184 B, CRC-16) | **Diputuskan Owner 2026-10-31:** tidak diperluas; dihapus dengan catatan setelah jalur baru (OPT2/RAW) berfungsi dan rekaman gelap selesai. Jangan dihapus lebih awal: kemungkinan jalur tercepat untuk rekaman gelap (isinya perlu diaudit dulu) |
| D7 | Verifikasi identitas panjang gelombang (735 vs 850) | **Diputuskan Owner 2026-10-31:** cek visual per slot (kamera ponsel tanpa filter IR), eksperimen dan timing; indikasi tertukar juga dari respons HbO/HbR (mis. HbR selalu naik); tes tahan napas setelah daftar keselamatan siap |

## 13. Kriteria penerimaan

- Vektor emas (host test): jendela LED-ON normal, tanpa langkah, jenuh, ramp tidak stabil, saluran jauh lemah, `n < K_min`; hasil gerbang dan pengosongan sesuai tabel yang dibekukan.
- Pembulat layout: `_Static_assert` ukuran 24 / 64 / 13 / 1384 dan 16 / 14 / 1416; enkode-dekode round-trip identik; CRC-32 dicek; paket uji simulator lewat parser host.
- Tidak ada regresi timing (acuan bench Owner, 10 Hz, 60 s): `fault=0`, `edge_miss=0`, `adc_missed=0`, `gap=0`, `crc_bad=0`, `spi_errors=0`,
  `spi_max ≤ ~60 µs`, `service_max ≤ ~100 µs`, `sched_deferred_max` tidak naik berarti, `on_d/off_d` tetap 1 atau lebih baik.
  Kemudian ulangi 10 menit di 10 Hz dan di 2/4/8 Hz.
- `frames=supported` di `info` hanya setelah pengirim FRAME bekerja dan lolos tes di atas.
- Bench kotak gelap menghasilkan ambang; kontrol negatif bersih; tepi ON/OFF di data RAW cocok dengan klaim firmware.

## Lampiran A — peta geometri

Lihat `optical_geometry_v1.draft.json`: 10 sumber (L1–L10, kiri/kanan), 4 detektor (PD1→AIN2, PD2→AIN3, PD3→AIN0, PD4→AIN1),
20 saluran terpilih (CH1–CH16, SC1L, SC2L, SC1R, SC2R). Uji konsistensi: jarak yang tercatat menyiratkan spasi PD1–PD2 11,5–13 mm (dan PD4–PD3 sama);
ukur dengan kaliper sebagai pengecekan tambahan.

## 14. Klarifikasi v0.2 (jawaban pertanyaan sisi host, 2026-10-31)

1. **Penerima FRAME live = lock owner (ADR-0004 §3.2 butir 4, §3.6).** Host app yang ingin menerima frame live harus menjadi pengirim `start_stream` dan mengirim `heartbeat` tiap `NN2_HOST_HB_PERIOD_MS` (stream berhenti setelah 3 s sunyi). Tidak ada subscriber baca-saja; menambahnya mengubah aturan lock yang menjadi dasar keselamatan, jadi bukan bagian rencana ini. Larangan "host tidak boleh mengirim perintah" di prompt awal hanya berlaku untuk tahap offline (simulator dan berkas). Tahap live memakai registry ADR-0004 apa adanya; `nn2ctl.py` tidak boleh dipakai bersamaan dengan host sebagai owner kedua.
2. **Parameter gerbang per `gate_config_id`.** Satu berkas sumber di repo firmware (usulan `config/optical_gate_configs.json`: `gate_config_id` → parameter §6.3). Firmware memuat set yang cocok; `get_config` dan SESSION_REPORT mengulang parameter sesi. Host membandingkan parameter yang dilaporkan dengan berkas itu; beda = sesi ditandai tidak sah.
3. **Pemeriksa kedua dari OPT2.** Host dapat menghitung ulang `STEP_Z` dan `FLOOR` dari `sum`, `sd`, `n`. `STABLE_PASS` dan `SAT` tidak dapat dihitung dari OPT2; pemeriksaan penuh hanya dari RAW (ENGINEERING). **Diputuskan Owner 2026-10-31: diterima** (lihat §4.5). Semula usulan: blok 80 B (`stab_q` u8 per detektor per slot; sisa payload layout 2 setelah blok ini = 56 B) berisi statistik stabilitas jendela supaya `STABLE_PASS` juga dapat diperiksa ulang; SAT tetap dipercaya dari `det_flags` dan diuji lewat RAW.
4. **Waktu live sebelum SESSION_REPORT.** Pakai fs nominal 8000 bertanda "estimasi", lalu fs berjalan dari selisih `first_sample_sequence` dibanding selisih `timestamp_us` header antar-frame. Hitung ulang waktu final dari `adc_fs_measured_ppm` di SESSION_REPORT setelah sesi selesai.
5. **Dua penghitung.** `msg_sequence` (header) menghitung semua pesan perangkat→host dalam sesi dan **bukan** penghitung kehilangan (ADR-0004 §3.4.4, §3.16.b). `frame_sequence` (payload) adalah satu-satunya lapisan deteksi frame hilang; dicocokkan dengan Δ`frames_dropped` di SESSION_REPORT. Selisih di atas itu = kehilangan di jaringan.

## 15. Klarifikasi v0.4 (jawaban kedua untuk sisi host, 2026-10-31)

1. Gerbang memakai nilai terkuantisasi yang dikirim; aritmetika tetap di §6.1; tidak ada toleransi.
2. `stab_q`: definisi di §4.5; `0xFE` dan `0xFF` selalu gagal STABLE.
3. Ambang `stable_k` dibandingkan sebagai integer (§6.1).
4. RAW: `sample_count`, int24, `slot_index` untuk `OUTSIDE` (§5).
5. `first_affected` dari `phase_class`; kekecualian komit early (§5).
6. ENGINEERING: bidang yang terisi dan yang dipaksa (§4.3).
7. `frame_layout = 1` (ADR-0004 §3.17.5): ditolak host dengan pesan "layout tidak didukung" (tidak membawa 4 detektor dan sebaran); layout tak dikenal diperlakukan sama.
8. Kebijakan penerima untuk bit tak dikenal (usulan Owner-approved bila tidak ada keberatan): `frame_layout` atau `adc_source` tak dikenal → tolak paket dan hitung. Untuk frame sains: `quality` > 4, bit `reason`/`det_flags`/`frame_flags` yang reserved atau tak dikenal → slot terkait diperlakukan INVALID (nilai dikosongkan di sisi komputer), diberi peringatan, dan dihitung; frame lain tetap diproses. Untuk frame ENGINEERING: peringatan dan hitung, nilai tidak pernah dianggap sains.
9. Metadata sesi: lihat §7.1 (usulan, belum diputuskan).
10. (v0.4.1) `reason != 0` ⇒ `quality ∈ {0, 3, 4}`; `quality` 1 dan 2 ⇒ `reason = 0` (§4.3).
11. (v0.4.1) Frame bergerbang: detektor dikosongkan ⇒ `stab_q = 0xFF` (§4.5).
12. (v0.4.1) Pemeriksa kedua satu arah pada frame bergerbang (§10 butir 8).
13. (v0.4.1) RAW: `phase_class` di luar 0..8, `slot_index = 0xFF` pada kelas selain `OUTSIDE`, `OUTSIDE` dengan `slot_index ≠ 0xFF`, atau `slot_index` ≥ 20 yang bukan `0xFF` ⇒ penerima menolak paket RAW dan menghitungnya.
14. (v0.4.1) Frame `STARTING_GATE`: gerbang normal, nilai boleh ada, bukan data sains (§6.5).
