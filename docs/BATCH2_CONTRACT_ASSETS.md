# Batch 2 — optical contract assets (offline only)

## Scope and provenance

Prepared by Codex under the owner's explicit Batch 2 instruction, after PR #83
merged. Base: `2ccbbffb00ea735a1cd652d271e3a1bed463a25d` on
`cursor/n3-single-clock-72a4`; PR #78 remained unmerged when checked.

The reference is the owner-provided pending v0.4.1 contract plus the owner's
2026-10-31 chat corrections. The pending document is deliberately excluded from
this PR and has not been edited or removed. Its SHA-256 at inspection:
`9f4f5b68116b6824814a83250b010229d13e196bfa27e38f6d612101794fa4cf`.
It changed externally during preparation to v0.4.1; the final source hash is
`f38755c474b664ab77d28cedb977634c1eeed5f6c16ccd8c3e66fb4284b97ad7`.
Both revisions were read without writing the pending file.
Merged v0.3 documents and the original draft geometry are unchanged.

## Deliverables

- `config/optical_geometry_v1.json` and JSON Schema: original geometry retained,
  with status `draft_pending_caliper`; old status retained as
  `source_draft_status`. One offline `ain_to_pd()` lookup uses this file.
- Geometry tests check 20 unique channels, two per source, detector/source sides,
  the exact PCA sequence, wavelengths and both C source tables. Triangle
  inequalities imply detector separation bounds 11.5–13.0 mm on each side;
  this is not a caliper measurement.
- `config/optical_gate_configs.json` and Schema: ID 0 is disabled ENGINEERING;
  ID 1 contains the requested provisional/unmeasured constants. No runtime reads.
- Pure C11 OPT2/RAW codec in `components/nn2_core`, byte-wise LE, no ESP/RTOS,
  packed structs, allocation, CRC, network sender or production caller.
- `test/golden/optical_frame_vectors.json`: 35 shared codec fixtures with full
  structured inputs and frozen payload hex. C compares encode byte-for-byte and
  decode field-by-field, including rejection and warning/counter paths.
- `test/golden/optical_gate_vectors.json`: 26 separate gate fixtures with
  binary64 diff/threshold bit patterns. This is test data, not a production gate.
- `test/golden/README.md`: stable interchange format and owner's arithmetic text,
  with the later STABLE correction applied.

## v0.4 / v0.4.1 differences applied

ENGINEERING carries mask 0, witness 0xFF, reason bits 10/11 clear, GATE_OFF bit 6
set and detector bits 2–5 clear. For each phase, n=0 requires empty sum/sd; n=1
retains sum and empty sd; n>=2 retains both. Numeric statistics are permitted at
slot quality 0, 3 and 4. Stability can be carried at n>=4. No gate function was
implemented. v0.4.1 also requires masked gate-on stability entries to be 0xFF
and nonzero reason to select quality 3 for STOP_ABORT, 4 for STARTUP_WARMUP,
otherwise 0; quality 1/2 requires reason zero.

RAW uses two's-complement int24 LE; OUTSIDE requires slot 0xFF. Unknown layouts,
including layout 1, are rejected and counted, as are unknown ADC sources.

The encoder rejects noncanonical/unsafe value masking. The decoder follows
v0.4 section 15.8: unknown science quality/reason/flag bits invalidate and empty
only the affected slot(s), count warnings, and preserve other slots. Unknown
frame flags affect every slot. Engineering diagnostics retain values and count
warnings. INTERNAL reason records receiver invalidation.

STABLE_PASS uses `stab_q < 0xFE` and integer
`10 * stab_q <= 16 * stable_k_x10`. Both FE and FF always fail.
Fixtures cover FE at k_x10=159/200, FD at 159, equality, adjacent sd_q4 LSBs,
SE floor, clipped sd, missing phases and unequal counts. The fixed floating-point
operation order, binary64 and no-FMA requirements are documented for future
T11 and host use; the codec performs no floating-point calculation.

Metadata encoding and RAW first_affected analysis are outside this batch.

## Validation and baseline comparison

Functional source tested: `e963e34c96df565e9ec0a28b11c5ab5e262f470b`.
The subsequent evidence commit changes documentation/evidence only.

Environment: Windows, Python 3.13.5, MinGW/TDM GCC 5.1.0,
CMake 3.30.2 and Ninja 1.12.1. Native GCC/CMake/Ninja paths were explicitly
added to PATH; NN2_TEST_CC was set to the same GCC for both comparisons.

| Check | Base 2ccbbff | Batch 2 |
|---|---|---|
| Full architecture suite | 104 tests; 14 failures, 2 errors | 114 tests; same 14 failures, 2 errors |
| Newly failing test identities | — | None |
| New optical architecture tests | Absent | 10 PASS |
| Existing Class U host suite | 137 tests, 0 failures, 0 ignored | 137 tests, 0 failures, 0 ignored |
| CTest | Existing target | 2/2 targets PASS, including shared C codec fixtures |
| verify-v4-idf61.py | PASS | PASS |
| verify-reference.ps1 | PASS, 319 files | PASS, 319 files |
| JSON Schema validation | Absent | PASS, both Draft 2020-12 metaschemas and assets |
| git diff --check | — | PASS |

Architecture failures are retained, not bypassed: old GCC lacks C17 flags,
some tests only search native cc/clang rather than the available gcc,
Windows workflow fixtures fail, and two existing ADR reads raise cp1252
UnicodeDecodeError. The exact failing identities and log hashes are in
`docs/evidence/batch2-contract-assets.json`. No existing tests were weakened
or changed to hide them. JSON Schema metaschema checks additionally used
jsonschema 4.25.1 from a temporary external directory; repository tests use a
small fail-closed validator for the vocabulary of these two schemas, without
introducing a runtime dependency.

The migration manifest changed only the existing shared-communication hash for
`components/nn2_core/CMakeLists.txt`, because that tracked file now registers
the codec source. No historical hashes or other firmware settings changed.

## Limits, review questions and recovery

There is no production call site for the new codec. Geometry/gate JSON is not
loaded by firmware. No changes to nn2_app_comm.c, scheduler, optical, timing,
priorities, I2C, pins, watchdog or reference/. Bench is not required for this
batch because no new code is called by the running firmware. Build/flash,
binary-size measurements, simulator, dark-box, HIL and human tests: NOT RUN.
These are author checks, not independent verification or review.

Before a future runtime binding, the owner should confirm whether zero-count RAW
packets should be allowed (this codec currently rejects them) and whether using
INTERNAL reason for host-side invalidation is the desired shared diagnostic.
No pending decision permits wiring this codec into runtime in this batch.
Calibration constants remain provisional; geometry remains pending caliper.
Independent contract review is required before any merge. Recovery for these
unbound assets is ordinary revert commits; no device migration is involved.

No force-push, amend, reset --hard, stash, --no-verify or merge was used.

