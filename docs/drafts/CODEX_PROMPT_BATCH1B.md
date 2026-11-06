# Prompt Codex — Commit/push Batch 1 + Batch 1b (arus LED) — NoctiNIRS 2

Salin isi blok di bawah ke Codex (folder `C:\Users\doeki\Documents\GitHub\noctinirs-2`).
Kondisi saat ini: Batch 1 sudah lolos bench 600 s (Owner), masih berupa perubahan lokal di cabang `cursor/n3-single-clock-72a4`, HEAD `93129ef`.

```text
Kamu bekerja di repo NoctiNIRS 2. Ada dua fase. Kerjakan berurutan dan berhenti di tempat yang ditentukan. Laporan dalam bahasa Indonesia sederhana.

KEADAAN AWAL
Perubahan Batch 1 (T1–T6) ada di working tree, belum di-commit, dan SUDAH LOLOS bench Owner di perangkat (600 s, 10 Hz): fault=0, edge_miss=0, adc_missed=0, gap=0, crc_bad=0, spi_errors=0, spi_max=52 us, service_max=70 us, lag_max=32 us, sched_deferred_max=248 us, async latency 66–232 us (rata-rata 175), late_1+late_2+late_3p = sample_clock_late_commits = 239862, late_max=2, semua async_*_errors = 0. Batch 1 TIDAK boleh diubah isinya di fase ini.

ATURAN GIT (berlaku untuk seluruh pekerjaan)
- Jangan force-push, jangan --force/--force-with-lease, jangan --no-verify, jangan rewrite/amend commit yang sudah di-push, jangan reset --hard, jangan stash, jangan merge PR, jangan hapus branch.
- Jangan menyentuh folder reference/.
- PR dibuat berurutan dan hanya mengandalkan CI. Pemilik (Yerri) yang menggabungkan (merge).
- Jangan pernah memasukkan file di luar daftar yang disebut di tiap commit. Periksa "git status" dan "git diff --cached --stat" sebelum tiap commit.
- Kalau ada langkah git yang gagal atau hasilnya tidak sesuai harapan: BERHENTI dan laporkan apa adanya. Jangan mencoba jalan memutar.

=== FASE A — Commit dan push Batch 1 (yang sudah lolos bench) ===
A1. Jalankan: git fetch origin; git status; git log --oneline -5; git branch --show-current.
    Periksa apakah HEAD (93129ef) sudah ada di origin/main ("git merge-base --is-ancestor HEAD origin/main").
    - Jika sudah di origin/main: buat branch baru dari HEAD bernama batch1-observability.
    - Jika belum (HEAD masih bagian dari PR #78 yang belum digabung): JANGAN menebak. Buat branch batch1-observability dari HEAD, dorong, lalu di laporan tulis jelas bahwa PR ini menumpuk di atas cabang cursor/n3-single-clock-72a4 dan base PR-nya harus cabang itu, bukan main.
A2. Dua commit terpisah:
    Commit 1 — hanya file firmware/host/test/docs Batch 1:
      components/nn2_app/src/nn2_app_comm.c
      docs/V4_IDF61_SINGLE_CLOCK.md
      firmware/v4-idf61/components/nocti_v4_health/include/nocti_v4_health.h
      firmware/v4-idf61/components/nocti_v4_optical/nocti_v4_optical.c
      firmware/v4-idf61/components/nocti_v4_scheduler/nocti_v4_scheduler.c
      firmware/v4-idf61/main/v4_nn2_control.inc
      test/host/test_v4_sample_clock.c
      tests/architecture/test_nn2ctl_diag.py
      tests/architecture/test_v4_batch1.py
      tools/nn2ctl.py
      Pesan: "feat(v4): rate validation, late-commit histogram and diagnostic counters (Batch 1)"
      Isi pesan (ringkas, bahasa Inggris): T1 rate validation + fs_allowed; T2 late_1/late_2/late_3p/late_max + early_commits export; T3 export of async_* and udp_* counters; T4 static assert on tail capacity; bench evidence (angka di atas).
    Commit 2 — hanya docs/drafts/ (draft kontrak data, daftar tugas, geometri, prompt-prompt). Pesan: "docs(drafts): optical data contract, Codex task list and geometry draft (not yet adopted)".
    Jangan mengubah isi file draft.
A3. Sebelum push, jalankan ulang tes host yang bisa jalan (tests/architecture/test_nn2ctl_diag.py dan test_v4_batch1.py) dan laporkan hasilnya. Jangan memperbaiki tes lama yang gagal karena lingkungan Windows (CRLF/cp1252/bash); catat saja.
A4. git push -u origin batch1-observability (tanpa force). Lalu buka PR (gunakan gh bila tersedia dan sudah login; kalau tidak, laporkan URL pembuatan PR dan berhenti di titik itu).
    Isi PR (bahasa Inggris, singkat): ringkasan T1–T6, tabel bukti bench di atas, bagian "Not changed" (timing, prioritas, I2C, pin, watchdog, reference/), bagian "Known: scheduler_task_core reports -1 (fixed in follow-up PR)", dan "Owner merges after CI".
    Jangan menggabungkan PR.
A5. Laporkan: nama branch, hash kedua commit, URL PR, status CI bila sudah terlihat.

=== FASE B — Batch 1b (perubahan kecil, sudah diputuskan Owner) ===
Mulai dari branch baru batch1b-current-limit yang dibuat DARI ujung batch1-observability (bertumpuk di atas Batch 1; PR 1b nanti memakai batch1-observability sebagai base bila Batch 1 belum digabung).
Keputusan Owner (D3): arus LED default 30 mA, maksimum 50 mA (antisipasi cahaya terang atau kulit gelap). Default TIDAK berubah: tetap 30 mA.

B1 — Satukan batas arus menjadi satu sumber kebenaran.
  Temuan audit T5 (lokasi bisa bergeser; cari dengan grep nama simbol):
    firmware/v4-idf61/main/v4_nn2_control.inc: control_start menolak current_ma>30; control_init current_limit_ma=30
    firmware/v4-idf61/components/nocti_v4_led/nocti_v4_led.c: default 15, maksimum 30 (~l.35-36, ~l.470); IREF limiter 255 (~l.79)
    components/nn2_core/include/nn2_core_optical.h: NN2_OPTICAL_BRINGUP_MA=30
    components/nn2_app/src/nn2_app_comm.c: default 30, batas port, batas umum 50 (~l.137-152, ~l.273)
    components/nn2_board/.../nn2_board_pca.c: pembatas memakai konstanta bring-up; konversi IREF dibulatkan ke bawah (~l.60, ~l.97)
    components/.../nn2_led_bringup.c: maksimum 30; mode advisory mensyaratkan tepat 30 mA (~l.677-680, ~l.800)
    tools/nn2ctl.py: host menerima 0–50 mA
  Kerjakan:
    - Definisikan dua konstanta di satu tempat bersama (usulan: nn2_core_optical.h): NN2_OPTICAL_DEFAULT_MA=30 dan NN2_OPTICAL_MAX_MA=50, dengan _Static_assert(DEFAULT<=MAX). Pertahankan NN2_OPTICAL_BRINGUP_MA bila masih dipakai, tetapi jangan biarkan dua nilai kebenaran yang bertentangan; jelaskan pilihanmu di laporan.
    - Semua pemeriksaan batas di jalur V4 (control_start, control_init current_limit_ma, nocti_v4_led maksimum, nn2_board_pca, nn2_app_comm) memakai MAX=50. Default 30 tetap.
    - nn2_led_bringup (mode advisory "tepat 30 mA"): JANGAN diubah perilakunya; catat saja di laporan.
    - Pastikan konversi mA -> register IREF PCA9956B tetap benar dan tidak melebihi rentang register pada 50 mA. Tambahkan tes host untuk 0, 15, 30, 50, 51 mA (51 ditolak). Jangan ubah rumus konversi yang sudah ada; hanya verifikasi dan uji.
    - INFO string menambahkan current_default_ma=30;current_max_ma=50 (jangan hapus kunci lama). nn2ctl.py membaca bila ada dan menolak nilai di luar batas sebelum mengirim.
    - nn2ctl.py: bila --current-ma > 30 (atau arus per kanal > 30), cetak PERINGATAN jelas di stderr ("arus di atas default 30 mA; pastikan sudah dihitung irradiansi dan aman untuk kulit"). Jangan menambah flag atau fitur lain.
    - Perbarui docs/V4_IDF61_SINGLE_CLOCK.md (bagian arus) agar menyebut default 30 / maksimum 50 dan satu sumber konstanta.
B2 — scheduler_task_core: bernilai -1 karena tidak pernah diisi. Isi dengan core tempat task scheduler berjalan, di tempat yang sama dan dengan cara yang sama seperti async_led_task_core diisi, saat task mulai. Tidak boleh ada perubahan pada timing atau prioritas.
B3 — Metadata jaringan V4 yang menuliskan arus 15 mA: ubah agar mengikuti nilai arus yang benar-benar dipakai sesi (bukan 15 tetap). Bila ternyata butuh perubahan struktur yang lebih besar, jangan lakukan; laporkan saja.

BATAS FASE B
- Jangan ubah timing (lead, settle, guard), prioritas/core task, kecepatan I2C, pin, watchdog/safety, partisi.
- Jangan menambah fitur safety atau gerbang baru. Jangan menyentuh Batch 2+.
- Tidak ada pekerjaan baru di jalur ISR / task ADS.
- Jalankan tes host yang bisa jalan; label NOT RUN untuk build ESP-IDF, flash, bench.

KOMIT FASE B (lokal saja) dan PUSH DITAHAN
- Satu commit: "fix(v4): single source for LED current limit (default 30 mA, max 50 mA), scheduler_task_core, session current metadata".
- JANGAN push dan JANGAN buka PR untuk Fase B. Push dilakukan hanya setelah Owner menulis kepada kamu: "bench 1b lolos, push". Alasannya: perubahan arus menyentuh jalur LED dan harus lolos bench Owner dulu.
- Setelah commit lokal, BERHENTI dan laporkan.

LAPORAN AKHIR
1. Fase A: branch, hash commit, URL PR, status CI, dan hal git yang tidak biasa.
2. Fase B: tabel (file, fungsi, nilai lama -> nilai baru) untuk tiap batas arus; hasil tes host; daftar file berubah; label NOT RUN.
3. Risiko untuk bench Owner dan langkah tesnya (di bawah).
4. Temuan di luar daftar (tidak diperbaiki).

LANGKAH BENCH OWNER (tulis ulang persis di laporan)
  idf.py -C firmware\v4-idf61 build
  idf.py -C firmware\v4-idf61 -p COM11 flash monitor
  python tools/nn2ctl.py info                       (harus memuat current_default_ma=30;current_max_ma=50)
  python tools/nn2ctl.py run --rate 10 --seconds 600 --led    (arus default 30 mA; angka regresi sama seperti Batch 1)
  python tools/nn2ctl.py timing
  python tools/nn2ctl.py health                     (scheduler_task_core harus 1, bukan -1)
  Uji batas arus tanpa menempel di kulit: --current-ma 50 diterima dan menampilkan peringatan; --current-ma 51 ditolak di komputer.
```

## Setelah Codex melapor
1. Fase A: cek PR di GitHub; tunggu CI hijau, lalu kamu yang merge.
2. Fase B: flash dan jalankan langkah bench di atas. Kalau lolos tanpa error, tulis ke Codex: `bench 1b lolos, push`.
3. Tempel hasil `info`, `timing`, `health` ke sini sebelum Batch 2.
