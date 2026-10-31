# Prompt Codex — Batch 1 (T1–T6) — NoctiNIRS 2

Salin semua teks di dalam blok di bawah ini ke Codex (jalankan Codex di folder `C:\Users\doeki\Documents\GitHub\noctinirs-2`).

Sebelum itu, pastikan dua file draft sudah ada di `docs\drafts\` (sudah disalin) dan cabang kerja bersih (`git status`).

```text
Kamu bekerja di repo NoctiNIRS 2 (firmware ESP32-S3, ESP-IDF 6.1) pada folder lokal ini. Kerjakan HANYA Batch 1 (T1–T6) dari docs/drafts/CODEX_TASK_LIST_DRAFT.md. Baca dulu file itu sampai habis, lalu docs/drafts/OPTICAL_DATA_CONTRACT_DRAFT.md bagian 1–3 (hanya untuk konteks; jangan implementasikan isinya di batch ini).

ATURAN KERJA (wajib, tidak boleh dilanggar)
1. Jangan git commit, git push, buat branch/PR, ubah remote, stash, reset, atau checkout. Biarkan semua perubahan sebagai perubahan lokal yang belum di-commit. Pemilik (Yerri) yang build, flash, tes hardware, lalu commit dan push kalau lolos.
2. Jangan flash dan jangan jalankan idf.py flash/monitor. Build ESP-IDF boleh dicoba hanya bila lingkungan tersedia; kalau tidak, tulis NOT RUN.
3. Jangan ubah: konstanta timing (lead, settle, guard), prioritas/core task, kecepatan I2C, pin, TWDT/watchdog/safety, partisi, dan apa pun di folder reference/ (read-only).
4. Jangan menambah fitur di luar T1–T6. Temuan lain: catat di laporan, jangan diperbaiki.
5. Jangan menambah pekerjaan berat di jalur ISR / task ADS / task LED. Penghitung baru harus murni penambahan integer, tanpa alokasi, tanpa log, tanpa pembagian di ISR.
6. Pertahankan kompatibilitas: kunci laporan lama (fs_min, fs_max, dll.) tetap ada; hanya tambah kunci baru.
7. Perubahan sekecil mungkin dan bisa direview. Satu topik per T. Jangan format ulang file, jangan ganti nama.
8. Jangan mengklaim sesuatu lolos hardware. Beri label NOT RUN untuk semua yang tidak benar-benar kamu jalankan.

TUGAS
T1 — Validasi rate: host (tools/nn2ctl.py) menolak rate di luar {2,4,5,8,10} sebelum mengirim, dengan pesan yang menyebut daftar itu. Tambahkan fs_allowed=2,4,5,8,10 ke string info di firmware/v4-idf61/main/v4_nn2_control.inc (pertahankan fs_min/fs_max), baca fs_allowed di host bila ada. Perbarui docs/V4_IDF61_SINGLE_CLOCK.md dan tests/architecture/test_nn2ctl_diag.py. Terima: "run --rate 6" gagal di host dengan pesan jelas; --rate 5 dan --rate 10 tetap jalan.

T2 — Histogram keterlambatan commit: di note_sample_commit (nocti_v4_scheduler.c) tambah penghitung kumulatif late_1, late_2, late_3p, late_max (pisahkan ON/OFF bila murah) memakai plan.on_late_samples / off_late_samples yang sudah ada; ekspor sample_clock_early_commits; tampilkan di timing dan health; baca di nn2ctl.py. Tambah tes host di test_v4_sample_clock.c untuk 0/1/2/3+ sampel.

T3 — Ekspor penghitung yang belum tampil: async_completion_errors, async_submit_errors, async_stale_completion, async_callback_core, async_led_task_core, scheduler_task_core, udp_frames_sent, udp_frames_dropped. Tampilkan di health/timing dan di test_nn2ctl_diag.py.

T4 — Bug laten di nocti_v4_optical.c: off_tail dipakai dengan NOCTI_V4_K_ON_TARGET di tail_add() dan latest_contiguous_mean(). Perbaiki dengan memparameterkan ukuran, ATAU tambah _Static_assert(NOCTI_V4_K_ON_TARGET == NOCTI_V4_K_OFF_TARGET) dengan komentar. Pilih yang paling kecil risikonya dan jelaskan alasannya. Hasil rata-rata pada K=10/10 harus identik dengan sebelumnya (buktikan dengan tes host).

T5 — Investigasi saja, JANGAN ubah nilai: cari semua tempat yang membatasi arus LED (control_start di v4_nn2_control.inc mengecek current_ma>30; juga nocti_v4_led, nocti_v4_async_led, nn2_board_pca, config/, docs, short_policy). Buat tabel (file, baris, nilai, alasan) di laporan dan usulkan satu konstanta sumber tunggal. Keputusan 30 vs 50 mA ada di tangan pemilik.

T6 — Audit saja, JANGAN hapus: daftar semua pemakai field/konstanta "stable" berbasis waktu (process_led_completion, NOCTI_V4_LED_ON_SETTLE_US, led_on_stable_start_ts, keadaan COMMITTED/SETTLING/STABLE) dan nyatakan mana yang masih memengaruhi klasifikasi atau kualitas data. Usulkan hapus atau tandai "diagnostik saja".

VERIFIKASI YANG BOLEH KAMU JALANKAN
- python tools/run_host_tests.py (dan tes di tests/architecture/). Laporkan keluaran apa adanya, termasuk yang gagal.
- Jangan edit tes agar lolos kalau tes lama gagal; laporkan.

FORMAT LAPORAN AKHIR (bahasa Indonesia sederhana, tanpa istilah yang tidak perlu)
1. Ringkasan per T: selesai / sebagian / tidak, 1–2 kalimat.
2. Daftar file yang berubah, dan alasan tiap perubahan (nama fungsi).
3. Hasil tes host: perintah yang dijalankan dan hasilnya. Label NOT RUN untuk build ESP-IDF, flash, bench, hardware.
4. Tabel T5 dan daftar pemakai T6.
5. Risiko / hal yang perlu dilihat pemilik saat bench.
6. Temuan di luar daftar (tidak diperbaiki).
7. Langkah tes untuk pemilik, persis perintahnya:
   idf.py -C firmware\v4-idf61 build
   idf.py -C firmware\v4-idf61 -p COM11 flash monitor
   python tools/nn2ctl.py info
   python tools/nn2ctl.py run --rate 10 --seconds 600 --led
   python tools/nn2ctl.py timing
   python tools/nn2ctl.py health
   serta nilai acuan yang harus tetap terpenuhi: fault=0, edge_miss=0, adc_missed=0, gap=0, crc_bad=0, spi_errors=0, spi_max<=~60 us, service_max<=~100 us, sched_deferred_max tidak naik berarti.

Setelah laporan, BERHENTI. Jangan lanjut ke Batch 2 dan jangan commit.
```

## Setelah Codex selesai (untuk kamu)
1. Baca laporan; cek `git status` dan `git diff` (atau minta aku review diff-nya).
2. Build, flash, lalu jalankan `info`, `run --rate 10 --seconds 600 --led`, `timing`, `health`.
3. Bandingkan dengan acuan regresi. Kalau semua lolos tanpa error, baru kamu commit dan push sendiri.
4. Tempelkan hasil `timing` dan `health` ke sini; aku bantu bacakan sebelum Batch 2.
