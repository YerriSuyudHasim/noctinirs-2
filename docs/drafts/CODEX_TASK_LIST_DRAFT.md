# Daftar Tugas Codex — NoctiNIRS 2 (DRAFT, 2026-10-30)

Acuan: `docs/drafts/OPTICAL_DATA_CONTRACT_DRAFT.md` (kontrak), `docs/drafts/optical_geometry_v1.draft.json` (geometri).
Nomor baris di bawah diambil dari pohon kerja pada saat pembacaan; cari dengan nama fungsi bila baris bergeser.

## Aturan kerja (dari Owner)

1. Kerjakan di **folder lokal Owner**. **Jangan** `git commit`, `git push`, buat PR, atau ubah remote. Owner yang build dan tes;
   **hanya bila lolos tanpa error, Owner yang commit dan push.**
2. Satu batch = satu paket perubahan. Setelah tiap batch **berhenti dan lapor**; tunggu hasil build/bench Owner sebelum batch berikutnya.
3. **Jangan ubah**: konstanta timing (`lead`, `settle`, `guard`), prioritas dan core task, kecepatan I2C, pin, TWDT/safety, partisi, isi `reference/` (read-only).
4. Boleh dijalankan Codex: tes host (`python tools/run_host_tests.py`, tes di `tests/architecture/`). **Jangan flash.** Build ESP-IDF dan bench dilakukan Owner.
5. Laporan serah-terima tiap batch: daftar file berubah + alasan, perintah build/tes, hasil tes host, risiko, dan label **NOT RUN** untuk yang tidak dijalankan.
   Jangan mengklaim lolos hardware.
6. Temuan di luar daftar dicatat sebagai temuan; jangan diperbaiki diam-diam.

Perintah Owner (referensi):
```
python tools/run_host_tests.py
idf.py -C firmware\v4-idf61 build
idf.py -C firmware\v4-idf61 -p COM11 flash monitor
python tools/nn2ctl.py info
python tools/nn2ctl.py run --rate 10 --seconds 600 --led
python tools/nn2ctl.py timing
python tools/nn2ctl.py health
```

Acuan bench Owner (10 Hz, 60 s, lolos) untuk uji regresi: `fault=0`, `edge_miss=0`, `adc_missed=0`, `gap=0`, `crc_bad=0`, `spi_errors=0`,
`spi_max=51 µs`, `service_max=67 µs`, `lag_max=31 µs`, `sched_deferred_max=245 µs`, I2C ON ≈ 213 µs / OFF ≈ 147 µs, `on_d`/`off_d` = 1 semua slot.
Batas regresi yang diterima: `spi_max ≤ ~60`, `service_max ≤ ~100`, `sched_deferred_max` tidak naik berarti.

---

## Batch 1 — Bug dan visibilitas (kecil, tidak menyentuh jalur timing)

### T1 · Validasi rate dan pesan error  — S
- **Bukti:** `run --rate 6` lolos pemeriksaan host lalu ditolak firmware dengan `INVALID_ARG` tanpa petunjuk. Firmware hanya menerima {2, 4, 5, 8, 10}
  (`control_start` di `firmware/v4-idf61/main/v4_nn2_control.inc` ~l.77 dan `nocti_v4_scheduler_set_fs` di `nocti_v4_scheduler.c` ~l.1309).
  Host hanya memeriksa rentang 2..10 (`tools/nn2ctl.py` ~l.416, ~l.435–438). String `info` (`v4_nn2_control.inc` ~l.295) beriklan `fs_min=2;fs_max=10`.
- **Kerjakan:** (a) host menolak rate di luar {2,4,5,8,10} dengan pesan yang menyebut daftar itu; (b) tambahkan `fs_allowed=2,4,5,8,10` ke string `info`
  (pertahankan `fs_min/fs_max` demi kompatibilitas) dan baca `fs_allowed` di host bila ada; (c) perbarui `docs/V4_IDF61_SINGLE_CLOCK.md` dan tes `tests/architecture/test_nn2ctl_diag.py`.
- **Terima bila:** `run --rate 6` gagal di host sebelum mengirim apa pun dengan pesan jelas; `--rate 5` dan `--rate 10` tetap jalan; tes host lolos.

### T2 · Histogram keterlambatan commit  — S/M
- **Bukti:** `sample_clock_late_commits` hanya menghitung "telat ≥ 1" (hari ini 100 % edge terlambat tepat 1 sampel). Besarnya keterlambatan tidak terlihat
  kecuali dari satu frame terakhir (`on_d`/`off_d`). `sample_clock_early_commits` ada di kode tetapi tidak diekspor.
- **Kerjakan:** di `note_sample_commit` (`nocti_v4_scheduler.c` ~l.549) tambah penghitung kumulatif `late_1`, `late_2`, `late_3p`, `late_max`
  (terpisah untuk ON dan OFF bila murah) dan ekspor `sample_clock_early_commits`; keluarkan di laporan `timing` dan `health`; baca di `nn2ctl.py`.
  Gunakan `plan.on_late_samples` / `off_late_samples` yang sudah dihitung `nocti_v4_sample_note_commit`.
- **Terima bila:** tes host di `test_v4_sample_clock.c` mencakup 0/1/2/3+ sampel; laporan menampilkan kunci baru; tidak ada alokasi atau panggilan berat di ISR.

### T3 · Ekspor penghitung yang belum tampil  — S
- **Bukti:** kunci-kunci ini ada di kode tetapi **tidak muncul** di `v4_nn2_control.inc` (0 kemunculan): `async_completion_errors`, `async_submit_errors`,
  `async_stale_completion`, `async_callback_core`, `async_led_task_core`, `scheduler_task_core`, `udp_frames_sent`, `udp_frames_dropped`.
  Akibatnya "tidak ada error I2C" hanya bisa diduga, tidak bisa dibuktikan dari laporan; dan core tempat ISR I2C berjalan tidak terlihat.
- **Kerjakan:** tambahkan ke keluaran `health`/`timing` dan ke `tests/architecture/test_nn2ctl_diag.py`.
- **Terima bila:** `nn2ctl health` menampilkan semuanya; nilai 0 pada bench normal.

### T4 · Bug laten: ukuran tail OFF memakai konstanta ON  — S
- **Bukti:** `nocti_v4_optical.c` — `on_tail[NOCTI_V4_K_ON_TARGET]` dan `off_tail[NOCTI_V4_K_OFF_TARGET]` (~l.95–96), tetapi `tail_add()` (~l.300) dan
  `latest_contiguous_mean()` (~l.312) memakai `NOCTI_V4_K_ON_TARGET` untuk indeks/modulo kedua buffer, dan dipanggil juga untuk `off_tail` (~l.614, ~l.850).
  Aman hanya selama `K_ON_TARGET == K_OFF_TARGET` (10/10 di `nocti_v4_scheduler_config.h` ~l.22–23). Jika berbeda, terjadi tulis/baca di luar batas.
- **Kerjakan:** parameterkan ukuran (atau tambah `_Static_assert(NOCTI_V4_K_ON_TARGET == NOCTI_V4_K_OFF_TARGET)` dengan komentar) dan tambah tes host untuk K berbeda bila diparameterkan.
- **Terima bila:** build lolos, hasil rata-rata identik dengan sebelumnya pada K = 10/10.

### T5 · Rekonsiliasi batas arus LED (investigasi saja)  — S
- **Bukti:** `control_start` menolak `current_ma > 30` (`v4_nn2_control.inc` ~l.78). Owner menyebut default 30 mA, maksimum 50 mA.
- **Kerjakan:** **jangan ubah nilai.** Cari semua tempat yang membatasi arus (grep di `nocti_v4_led`, `nocti_v4_async_led`, `nn2_board_pca`, `config/`, docs, `short_policy=STRICT`),
  buat tabel (file, nilai, alasan) di laporan, dan usulkan satu konstanta sumber tunggal. Menunggu keputusan Owner (D3).
- **Terima bila:** laporan tabel lengkap, tanpa perubahan perilaku.

### T6 · Dua sumber kebenaran untuk "stabil" (audit saja)  — S
- **Bukti:** `process_led_completion` (`nocti_v4_scheduler.c` ~l.652–670) menetapkan keadaan `COMMITTED → SETTLING → STABLE` berturut-turut di dalam satu critical section
  dan menghitung `led_on_stable_start_ts` dari konstanta mikrodetik (`NOCTI_V4_LED_ON_SETTLE_US`), sejajar dengan klasifikasi berbasis indeks sampel yang menjadi jam utama.
- **Kerjakan:** daftar semua pemakai field/konstanta berbasis waktu tersebut dan nyatakan mana yang masih memengaruhi klasifikasi atau kualitas. **Jangan hapus.** Usulkan penghapusan atau penandaan "diagnostik saja".
- **Terima bila:** laporan daftar pemakai dengan kesimpulan jelas.

**Batch 1 selesai bila:** tes host lolos; Owner build; bench 10 Hz 600 s memenuhi acuan; laporan `health`/`timing` menampilkan kunci baru.

---

## Batch 2 — Spesifikasi dan alat komputer (tanpa menyentuh firmware)

### T7 · Peta geometri dan validator  — S
- Pindahkan `optical_geometry_v1.draft.json` ke lokasi yang disepakati (usulan `tools/host/geometry/`), tambah JSON Schema dan modul Python tunggal `geometry.py`
  (`ain → pd`, `slot → (source, wavelength)`, daftar saluran, kontrol negatif per slot).
- **Terima bila:** tes membuktikan 20 saluran unik, tiap sumber tepat 2 saluran, `slot_to_pca = [0..9,14..23]`, tiap saluran satu sisi dengan sumbernya,
  dan hanya satu fungsi konversi AIN→PD yang ada di repo (uji grep otomatis).

### T8 · Codec FRAME layout 2 dan 3 (C murni) + vektor emas  — M
> Revisi 2026-10-31: parser dan simulator Python dikerjakan agent di repo `noctinirs-2-host`, bukan di sini.
- Di `nn2_core`: enkode/dekode byte-serialized sesuai kontrak §4–5; `_Static_assert` ukuran 24 / 64 / 13 / 1384 dan 16 / 14 / 1416; sentinel `INT32_MIN`/`0xFFFF`.
- Hasilkan berkas vektor emas (JSON: masukan terstruktur + byte hex yang diharapkan, termasuk slot gagal bukti, jenuh, paket terlalu besar) yang dipakai bersama oleh tes C dan tes parser Python di repo host.
- **Terima bila:** tes C memakai vektor emas, CRC-32 diperiksa, paket terlalu besar ditolak dan dihitung (tidak pernah difragmentasi), tes host lolos.

### T9 · (DIPINDAH) perekaman dan parse
- Revisi 2026-10-31: perekam (biner mentah + CSV berlabel) dan pemutar ulang dikerjakan agent di repo `noctinirs-2-host` (Langkah 3). `nn2ctl.py` tetap hanya untuk kontrol. Tidak ada pekerjaan Codex di T9.

---

## Batch 3 — Statistik dan gerbang di firmware (belum ada jaringan)

### T10 · Akumulator statistik per (slot, AIN)  — M
- Di `nocti_v4_optical.c` (`finalize_work`, `latest_contiguous_mean`): hitung `sum` (i32 tepat), `sd` (sampel, Welford atau jumlah kuadrat 64-bit), rata-rata separuh-tua/baru, jumlah sampel saturasi.
  Jalankan di task optik (prioritas 9); **bukan di ISR atau task ADS**.
- Tambah `_Static_assert(K_TARGET <= 255)`. Pertahankan `i_on`, `i_led_off`, `i_corrected` yang ada demi kompatibilitas.
- **Terima bila:** tes host dengan jendela sintetis menghasilkan `sum`/`sd` tepat; tidak ada pembagian di jalur ISR.

### T11 · Gerbang bukti (§6) dan pengosongan nilai  — M
- Implementasikan §6.1–6.2 sebagai fungsi murni (host-testable): `STEP_Z_PASS`, `STABLE_PASS`, `SAT`, `FLOOR_PASS`, saksi, `verified_mask`, bit `reason` baru (10, 11).
  Nilai tidak terverifikasi dikosongkan di **titik ekspor**, bukan dihapus dari struct internal.
- Parameter awal dari §6.3 sebagai konstanta sementara bertanda TBD; belum ada perintah `set_gate`.
- Tampilkan di `health`/`timing` agar Owner bisa memeriksa di bench sebelum ada jaringan: jumlah slot VERIFIED/UNVERIFIED, saksi per slot, `verified_mask` terakhir.
- **Terima bila:** vektor emas (LED-ON normal, tanpa langkah, jenuh, ramp, saluran jauh lemah, `n < K_min`) lolos; tidak ada regresi timing.

**Batch 3 selesai bila:** bench Owner dengan probe di kotak gelap menampilkan saksi per slot dan `verified_mask` yang masuk akal, dan acuan regresi terpenuhi.

---

## Batch 4 — Pengirim FRAME layout 2

### T12 · NN2 FRAME binder  — L
- Hubungkan frame optik lengkap ke `nn2_core` codec (T8) lalu ke socket NN2 (UDP 3333) sebagai `msg_type = FRAME`, sesuai ADR-0004 (producer tidak memblokir, `frames_dropped`, kelas TX terpisah dari kontrol).
- `info` baru berubah menjadi `frames=supported` setelah ini bekerja. Stream V4 lama (port 3334) tidak diubah (D6).
- **Terima bila:** `nn2ctl record` menerima 10 menit di 10 Hz tanpa diskontinuitas `frame_sequence` pada WiFi bersih; acuan regresi timing terpenuhi (jalur kirim di core 0, prioritas rendah).

### T13 · Tambahan SESSION_REPORT  — M
- `adc_fs_measured_ppm`, `geometry_map_id`, `gate_config_id`, hitungan VERIFIED/UNVERIFIED per slot, `frames_dropped`, `end_reason` (kontrak §7).

---

## Batch 5 — ENGINEERING, RAW, dan gerbang start

### T14 · `set_gate` dan `set_mode` (hanya saat STOP)  — M
### T15 · RAW layout 3 (ENGINEERING saja)  — M/L
### T16 · Gerbang start dan aturan hentikan `NO_OPTICAL_EVIDENCE`  — M
- Rinci sesuai kontrak §6.4–6.6 dan §8. Setiap perintah baru mengikuti aturan ADR-0004 (registry, `INVALID_ARG`, hanya STOP).
- **Terima bila:** mode ENGINEERING tidak pernah aktif tanpa perintah eksplisit; frame ENGINEERING selalu berflag; gerbang start menolak STREAMING ketika tidak ada saksi.

---

## Bukan tugas Codex (Owner / bench)

| Item | Catatan |
|---|---|
| Keputusan D1–D7 (kontrak §12) | terutama D1 (mask/buang), D3 (30 vs 50 mA), D4 (jalur ADR) |
| Kaliper PD1–PD2 dan PD4–PD3 | harus 11,5–13 mm menurut tabel jarak |
| Uji kotak gelap | σ tiap kanal, respons langkah LED, ambang (menentukan D5) |
| Cek I2C 800 kHz dengan osiloskop (rise time, bus bersama MAX17048/BQ24295) | |
| Uji LED macet menyala | hentikan task LED sengaja di bench dan lihat apakah LED tetap menyala |
| Checklist keselamatan sebelum menempel di dahi | arus rendah, iradiansi, pemutus cepat, aturan lab/IRB |

## Tambahan 2026-10-31 — T0 · Audit stream lama port 3334 (baca saja)  — S
- **Tujuan:** tahu apakah stream V4 lama (1184 B, CRC-16) bisa dipakai merekam intensitas untuk tes kotak gelap sebelum jalur OPT2/RAW siap. Keputusan D6: stream ini dihapus dengan catatan *setelah* jalur baru berfungsi; jangan hapus sekarang.
- **Kerjakan (tanpa mengubah kode):** jelaskan isi tiap datagram (field, satuan, apakah per sampel atau per slot, apakah memuat I_ON dan I_dark/OFF per detektor, penanda fase, nomor sampel), apakah masih diaktifkan di firmware V4-IDF61 saat ini, dan cara menerimanya dari komputer. Beri kesimpulan jelas: bisa dipakai merekam atau tidak, dan apa kekurangannya.
- **Terima bila:** laporan berupa tabel field dan kesimpulan; tidak ada perubahan berkas.

## Usulan urutan baru (menunggu persetujuan Owner)
Rekaman kotak gelap menentukan ambang gerbang (D5), tetapi butuh jalur data keluar dari perangkat. Usulan: T0 -> T7 -> T8 (codec C + vektor emas) -> pengirim FRAME dan mode ENGINEERING tanpa gerbang (bagian T12 + T14 + T15, semua frame berflag `ENGINEERING_UNGATED`, hanya lewat perintah eksplisit saat STOP, hanya untuk bench tanpa kulit) -> rekaman kotak gelap -> tentukan ambang -> T10/T11 (statistik dan gerbang) -> T13/T16.
