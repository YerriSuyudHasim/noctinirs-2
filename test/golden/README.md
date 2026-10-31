# Shared optical contract fixtures

These are offline draft-contract v0.4.3 assets. Nothing binds the codec, geometry or gate
configuration to running firmware. No gate calculation is implemented by the C
codec. The owner clarification below governs future T11 and host implementations.

## Stable JSON interchange

`optical_frame_vectors.json` has `schema = noctinirs.optical_frame_vectors/1`,
`contract`, and an ordered `vectors` array. Each vector has a unique `name`, numeric
`layout`, complete structured `input`, `expected_result` (`OK`, `INVALID`,
`TOO_LARGE`, `UNKNOWN_LAYOUT`), `expected_hex` (lowercase payload-only hexadecimal,
or null when no wire representation applies), and `decode_result` (or null when
decode does not apply). An optional `encode: false` marks decode-only
tests. Optional `decoded_input` gives the field-by-field expected receiver output
when the v0.4 unknown-bit policy invalidates science slots. Integers are JSON
integers, not floating-point values. Input object keys
follow public C field order; `generate_c_vectors.py` translates them to typed C
fixtures and never calculates expected wire bytes. Do not reorder field keys
without updating the generator. Arrays contain all 20 slots/80 stability entries
or all RAW samples: there are no implicit defaults. A rejected count of 101 is
represented with 101 structured samples; the C test supplies the first 100 in
fixed storage and verifies rejection before access.

The C host test checks encode against frozen hex and decode against every
structured field, including signed int24 boundaries. The same JSON is intended
for the Python parser tests in `noctinirs-2-host`. No CRC is included: CRC-32 is
owned by the existing NN2 envelope. Generate/build via `test/host/CMakeLists.txt`
and run `ctest --test-dir <build> --output-on-failure`; the architecture suite
also compiles and runs the C fixtures using `NN2_TEST_CC` or native cc/gcc.

`optical_gate_vectors.json` is separate: `schema = noctinirs.optical_gate_vectors/1`
and `vectors`. Each vector contains `wire` inputs, integer-scaled `parameters`,
and `expected` booleans plus `diff_f64_hex` and `thr_f64_hex`. Hex is the canonical
16-digit IEEE-754 binary64 bit pattern (most significant digit first), independent
of host byte order. Null doubles indicate the mandatory early exit with no
calculation. These are frozen expected results, not a production gate evaluator.

## Codec decisions and limits

- Byte-wise little-endian serialization; public structs are value types and never
  wire overlays. Rejection leaves output and length unchanged. Stats are required,
  caller-owned and saturate at UINT32_MAX; oversized payloads are never fragmented.
- Gate-on quality 0/3/4 or nonzero reason requires mask zero and empty sum/sd.
  Every other unverified detector also requires empty sum/sd. Invalid input is
  rejected, never silently repaired. Quality 3/4 and their reason are preserved.
- ENGINEERING_UNGATED is the explicit exception: mask zero, witness 0xFF,
  reason bits 10/11 clear, detector bits 2–5 clear, GATE_OFF bit 6 set, real
  sum/sd retained when computable (sum at n>=1, sd at n>=2). Bit 7 remains reserved. No gate is evaluated by this codec.
- RAW requires ENGINEERING, count 1..100, signed int24 and phase 0..8. OUTSIDE
  requires slot 0xFF; the other phases require slot 0..19. Zero-count RAW is
  rejected, as explicitly required by contract v0.4.2.
- Encoders reject reserved bits, and decoders reject reserved bytes/noncanonical
  lengths. Following v0.4 section 15.8, unknown science quality/reason/flag bits
  cause warning counters and INVALID/empty affected slots, while engineering
  values are retained as diagnostics with warnings. Unknown frame flags affect
  all 20 slots. INTERNAL reason records receiver invalidation. Unknown sources
  and layouts are rejected and counted by decoder dispatch. `stab_q` 0xFE and
  0xFF are preserved, including
  engineering diagnostic entries. Gate-on masked detectors require stab_q=0xFF. Clipped sd 0xFFFE is preserved.
- For gate-on numeric ON/OFF statistics, used counts must be at least two; used
  cannot exceed available. ENGINEERING permits n=0 with empty sum/sd, n=1 with
  numeric sum and empty sd, and n>=2 with numeric sum/sd, even at quality 0/3/4. Gate evaluation and acquisition policy remain future
  work. The engineering exception and RAW slot rules follow the owner's explicit
  2026-10-31 clarification, without editing the merged draft document.

## v0.4.2/v0.4.3 negative receiver fixtures

Run `python test/golden/generate_negative_vectors.py` to regenerate the 24
`v042_` vectors and two `v043_` vectors (61 frame vectors total). The generator mutates existing frozen
positive payload bytes and structured inputs, never calls the codec, and keeps
the original 35 vectors unchanged. Re-running it produces identical JSON bytes.
Every added vector expects `NN2_OC_INVALID` (`INVALID`) from the decoder:

- Nonzero OPT2 reserved prefix word at offset 22 and RAW word at offset 14.
  These two are decode-only (`encode: false`): reserved words are not public C
  struct fields and the encoder always emits zero. Their structured inputs
  describe the positive source fields, while `expected_hex` carries the bad word.
- RAW ENGINEERING bit 0 clear, and each reserved flag bit 2 through 15 set
  separately (15 vectors).
- RAW sample_count zero with an exact 16-byte prefix-only payload. Existing
  vectors already cover counts 1 and 100 (`OK`) and 101 (`TOO_LARGE`).
- Both directions of ENGINEERING_UNGATED versus gate_config_id=0 disagreement.
- Verified detectors with ON count 0 or 1 and OFF count 0 or 1. Their stability
  entries are 0xFF so the independent n<4 stability check does not mask n<2.

Except for the two reserved-word decode-only cases, every new structured input
also expects `NN2_OC_INVALID` from the encoder. `generate_c_vectors.py` checks
these exact result codes against the unchanged production codec. A mismatch is
a test failure, not permission to change codec behavior. Contract v0.4.2 makes
receiver rejection explicit for reserved prefix words, RAW flags/counts,
ENGINEERING/gate-ID disagreement and verified-detector counts below two.

Contract v0.4.3 adds `n_on_used <= n_on_available` and
`n_off_used <= n_off_available`, with packet rejection on violation. Two new
vectors expect exactly `NN2_OC_INVALID` from both encoder and decoder:

- `v043_n_on_used_exceeds_available`: slot 0 ON used=10, available=9.
- `v043_n_off_used_exceeds_available`: slot 0 OFF used=10, available=9.

Only the respective available count changes from the frozen verified positive
vector; used counts, detector statistics and stability remain unchanged. The
previous 59 frame vectors are unchanged; regeneration remains byte-identical.

## Aritmetika gerbang

ARITMETIKA GERBANG (klarifikasi v0.4, salin persis ke test/golden/README.md bagian "Aritmetika gerbang")

Prinsip: gerbang memutuskan dari NILAI YANG DIKIRIM di paket, bukan dari nilai eksak internal. Firmware dan host harus menghasilkan hasil identik sampai bit terakhir. Tidak ada toleransi; selisih antara host dan firmware = bug.

Masukan per (slot, detektor d), semuanya dari kawat:
  n_on = n_on_used, n_off = n_off_used  (u8)
  sum_on, sum_off (i32; INT32_MIN = dikosongkan)
  sd_on_q4, sd_off_q4 (u16; 0xFFFF = dikosongkan; 0xFFFE = terpotong, dipakai apa adanya sebagai 4095,875)
  stab_q (u8; 0xFF = tidak dihitung)
Parameter dari gate_config_id: z_x10 (z_witness_x10 atau z_channel_x10), stable_k_x10, floor_counts, se_floor_x100.

Aturan awal: bila n_on = 0, n_off = 0, sum_on atau sum_off = INT32_MIN, atau sd_on_q4 atau sd_off_q4 = 0xFFFF, maka STEP_Z_PASS = FLOOR_PASS = STABLE_PASS = 0 dan tidak ada perhitungan lain.

Aritmetika: binary64 (double), urutan operasi tetap persis seperti di bawah, tanpa fused multiply-add dan tanpa fast-math. Di C pakai -ffp-contract=off dan tidak mengaktifkan -ffast-math. Operasi + - * / dan sqrt dibulatkan tepat menurut IEEE-754, jadi hasil C dan Python float identik. Tulis tiap langkah sebagai pernyataan terpisah (jangan digabung dalam satu ekspresi yang bisa diatur ulang compiler).
  a      = (double)sum_on  / (double)n_on
  b      = (double)sum_off / (double)n_off
  diff   = a - b
  s_on   = (double)sd_on_q4  / 16.0
  s_off  = (double)sd_off_q4 / 16.0
  v_on   = s_on  * s_on  / (double)n_on
  v_off  = s_off * s_off / (double)n_off
  se     = sqrt(v_on + v_off)
  floor_se = (double)se_floor_x100 / 100.0
  se_eff = (se > floor_se) ? se : floor_se
  z      = (double)z_x10 / 10.0
  thr    = z * se_eff
  STEP_Z_PASS = (diff >= thr)
  FLOOR_PASS  = (diff >= (double)floor_counts)
  STABLE_PASS = (stab_q < 0xFE) && (10 * (int)stab_q <= 16 * (int)stable_k_x10)     (integer murni, tanpa pecahan)
Contoh stable_k = 2,3 (stable_k_x10 = 23): 10*stab_q <= 368, jadi stab_q <= 36.

DEFINISI stab_q (dihitung firmware dari data eksak internal, lalu dikuantisasi; host tidak dapat menghitung ulang, hanya memeriksa ambangnya):
  n_half = floor(n / 2). Separuh tua = n_half sampel pertama jendela; separuh baru = n_half sampel terakhir. Bila n ganjil, sampel tengah dikeluarkan dari keduanya.
  sd = simpangan baku sampel SELURUH jendela (n sampel, pembagi n-1), bukan per separuh.
  r = | mean_separuh_baru - mean_separuh_tua | / max( sd * sqrt(2 / n_half), floor_se )     (floor_se sama dengan di atas)
  Dihitung untuk jendela ON dan jendela OFF; stab_q = round-to-nearest(16 * max(r_on, r_off)), terpotong di 0xFE.
  stab_q = 0xFF bila n < 4 pada salah satu jendela, atau nilai dikosongkan.

Vektor emas bagian "gerbang": masukan dari kawat + parameter -> keluaran (STEP_Z_PASS, FLOOR_PASS, STABLE_PASS), dengan kasus:
  - jauh di atas ambang; jauh di bawah ambang;
  - tepat di ambang (diff == thr, harus lulus karena >=) dan satu LSB sd_q4 di bawah/atas ambang yang membalik hasil;
  - se jatuh ke floor_se (sd_q4 = 0, 0 < sd_q4 kecil);
  - sd terpotong 0xFFFE;
  - stab_q = 0xFF, stab_q tepat di batas 16*stable_k, satu di atasnya;
  - kosong (sentinel) di salah satu fase;
  - n_on != n_off.
Setiap vektor memuat nilai double yang diharapkan untuk diff dan thr (hex IEEE-754 64-bit) agar kedua sisi dapat dibandingkan bit-per-bit.

### Subsequent owner corrections (2026-10-31)

The STABLE_PASS line above incorporates the owner's later correction: 0xFE and
0xFF always fail, even when stable_k_x10 is 159 or 200. See the dedicated FE/FD
boundary fixtures. The pending v0.4 source is read-only and excluded from this
PR: docs/drafts/OPTICAL_DATA_CONTRACT_DRAFT_v0.4_PENDING.md, SHA-256
9f4f5b68116b6824814a83250b010229d13e196bfa27e38f6d612101794fa4cf.
Compared with v0.3, it clarifies GATE_OFF/ENGINEERING partial statistics, stability
clipping, fixed arithmetic, and OUTSIDE=0xFF. RAW first_affected is derived by
future analysis from phase tags; early commits require explicit edge notes.
Session metadata and runtime binding are not implemented here.

The pending document was subsequently updated externally to v0.4.1, SHA-256
f38755c474b664ab77d28cedb977634c1eeed5f6c16ccd8c3e66fb4284b97ad7.
This later source remains excluded and unmodified. Gate-on masked detectors
now always carry stab_q=0xFF. Nonzero producer reason requires quality 3 for
STOP_ABORT, 4 for STARTUP_WARMUP, otherwise 0; quality 1/2 requires reason zero.
RAW non-OUTSIDE phases require slot 0..19. STARTING_GATE can carry normally
gated values but is not scientific data. One-way host second-check logic is
outside this payload codec and is not a new firmware gate implementation.
