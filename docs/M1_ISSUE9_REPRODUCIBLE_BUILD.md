# Issue #9 — M1.3 reproducible build evidence

Accountable: Group 2, Verifier Lead and Integration Engineer. Scope: build
determinism, comparison tooling, CI and review evidence. Base: `main` at
`4a21171353b72ceff62774ba114705e0c8be2c31`. PRs #38 and #39 are separate work;
neither is included or modified. No reference snapshot is a build input.

## Original failure and measured cause

M0 built `cd4ed7f88905a1c430e0b942732ddc0613081aef` twice:

| Image | Push SHA-256 | PR SHA-256 | Bytes each |
|---|---|---|---:|
| Application | `86ca19229b1ba9604f70c3a5e6812b1c8fe7c025597f14266fdba6bf2ba70384` | `49f58488907e721e57f49d127cc4499a44de487e52c3d8ac81f45bbe3621cf6a` | 170048 |
| Bootloader | `e4006072d8c2f89f5ae96dfedd9cef9e92f77198702398dee46520f828a46fa1` | `95c36fb89e19ef2101f2be2cbed8de71076be7217afc964336bbac88f2488c0d` | 21120 |
| Partition table | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` | 3072 |

Source artifacts were downloaded and measured on 2026-10-17:
[push run 38847664721](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38847664721),
artifact **12024271866**; [PR run 38847669878](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38847669878),
artifact **12023717650**. See [raw measurement record](evidence/issue9-m0-measurements.json).

The application descriptor contains `03:56:07` versus `03:57:09`, both dated
`Oct 16 2026`. The two ELF files differ in exactly **two bytes**, both in
`.flash.appdesc`; every other ELF section is identical. Their complete ELF
SHA-256 values match the different ELF hashes embedded at application binary
offsets 176..207. The application binary has 67 different bytes: two timestamp
characters, 32 ELF-hash bytes, one image checksum byte and 32 appended image-hash
bytes. This is measured timestamp nondeterminism and its derived hash/checksum
propagation, not an inference from different final hashes alone. Bootloaders
differ in 35 bytes (timestamp region and derived trailer); partition tables and
generated sdkconfig are identical. Both configurations enable application and
bootloader timestamps and disable reproducible builds.

Absolute project and IDF paths are present in the original ELF debug data.
They did **not** differ between these two M0 jobs, so they are a measured exposure,
not the proven cause of that historical mismatch. Different-path CI below tests
their removal by the SDK's reproducible build mode. Ordering, compiler randomness,
locale, environment and generated metadata are additional possible sources;
none is claimed as a measured historical cause without supporting differences.

Reproduce the read-only historical measurement:

```text
python tools/analyze_m0_reproducibility.py m0-push.zip m0-pr.zip measurements.json
```

## Configuration proposal (independent review required)

Exactly one sdkconfig.defaults value is added: `CONFIG_APP_REPRODUCIBLE_BUILD=y`.
The existing optimization and logging defaults are retained. Under the pinned
SDK this disables application and bootloader date/time metadata, selects macro
path hiding and enables debug prefix maps for project, build, component, SDK and
toolchain paths. Generated config and actual compiler commands are retained from
both builds so the effect can be checked independently. Debuggers need the SDK's
generated `build/gdbinit/prefix_map` substitutions to locate local sources.

Measured effective-config comparison against the downloaded M0 configuration
shows exactly three value changes: APP_REPRODUCIBLE_BUILD `n -> y`,
APP_COMPILE_TIME_DATE `y -> n`, and BOOTLOADER_COMPILE_TIME_DATE `y -> n`.
COMPILER_HIDE_PATHS_MACROS was already `y` and stays `y`. The Class B ELF at
`eda7adc112b010aef5d2fde3b5bbc404df5e1104` contains neither the actual project
checkout path nor `/opt/esp/idf`; its app descriptor's date/time fields are 32 zero
bytes. These are observed configuration/metadata effects, independently of the
different-path build pair recorded below. Source: [Class B PR run 39017047331](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017047331),
[artifact 12095745639](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017047331/artifacts/12095745639),
zero warnings. App is 169952 bytes (-96 versus M0), bootloader 21072 (-48),
partition table 3072 (unchanged); DIRAM remains 48242 bytes and IRAM 16384 bytes.

No additional Kconfig, task, scheduling, peripheral, network, pin, partition,
security or hardware setting is proposed. This requested build configuration is
within Issue #9's authorization; independent review and Owner merge approval
remain required. No additional Owner-only configuration exception is identified.

Alternatives considered: manually disabling only timestamps leaves debug-path
dependence; fixing SOURCE_DATE_EPOCH masks time-dependent source use; post-build
binary normalization changes the artifact being tested. None is used. SDK-native
compiler prefix maps act during compilation; comparison hashes the exact produced
bytes and never rewrites, strips, patches or excludes a binary.

## Immutable toolchain

The original Class B job already used the correct immutable pin; it is preserved:

- OCI index: `espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
- linux/amd64 child: `sha256:f81b6e077e6af93c312181cd7d4e7936cfbafc5c7f77cbacba8fdd34994f218a`.
- ESP-IDF v6.1: `fff9895c82d744c7237be8847347bdd1b07c6643`.
- Compiler: `xtensa-esp-elf-gcc (crosstool-NG esp-15.2.0_20251204) 15.2.0`.

`tools/verify_idf_image.py` verifies SHA-256 of exact registry response bytes for
the pinned index and its linux/amd64 child. Local verification on 2026-10-17
08:19 UTC passed; each reproducibility job repeats it and retains raw manifests.
The runtime also enforces the SDK commit and compiler version in both snapshots.
New workflow actions are pinned to upstream tag-verified commit SHAs. Existing
firmware/reference workflow action tags are outside this change's pinning scope.

## Procedure and failure behavior

`Reproducible build` checks out the exact PR head (or pushed SHA). Host comparison
tests must pass before the Class B job. `tools/build_twice.sh` clones that SHA into
two new paths with different lengths/depths, without hardlinks, shared build
directories, sdkconfig or ccache. Builds run sequentially at real wall-clock times,
with no SOURCE_DATE_EPOCH. Clean-start records, full logs, generated sdkconfigs,
compile commands, flasher metadata, GDB prefix maps and every recursively found
`.bin` are retained separately. Build, target and size command failures are
preserved as failures even when partial binaries exist.

`tools/reproducible_build.py` requires the application, bootloader and partition
table, every flasher input, successful build status, nonempty binaries, pinned
toolchain, matching source SHA/config and zero warnings. It validates actual
retained files against their inventories, then compares the union of all binary
paths. Missing, unexpected or changed files fail. Both builds missing the same
required file also fail. Symlinked output is rejected. The report contains names,
sizes, both full hashes, first differing byte offsets, errors and environment
metadata. The comparison command's exit status is the CI verdict; `always()`
steps preserve diagnostics and raw artifacts even on failure. No continue-on-error
or artifact filtering is used to obtain PASS.

Local host tooling suite: **27 methods PASS** on the implementation tree. It tests
identical bytes, changed content with equal size, changed size, each required file
missing, unexpected nested files, optional produced bins, empty/missing outputs,
incomplete status with matching outputs, malformed/missing manifests, evidence
tampering, changed environment, identical workspaces, symlinks, CLI exit codes,
snapshot failure/config/warning/flash checks and workflow head/digest wiring.
An actual shell-runner fixture injects build exit 7 into both clean clones and
verifies that a successful size command cannot mask it; a spy records both exits.
The Linux CI suite is the final authoritative host result, tied to its source SHA.

## Current two-build and regression evidence

Implementation verification at `493b26abe4fb74917cd012696d16a2a4eb5891d2`:
[PR reproducibility run 39017211516](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017211516)
**PASS**, [artifact 12095307101](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017211516/artifacts/12095307101).
The uploaded raw binaries were downloaded and all 14 per-build size/hash entries
were independently recomputed by the author; every entry matches the CI report.
This is author-side evidence validation, not independent review approval.

- A: `/__w/_temp/nn2-issue9.qh6U9VdO/short-a`, clean start 08:26:05 UTC,
  snapshot 08:28:10 UTC on 2026-10-17.
- B: `/__w/_temp/nn2-issue9.qh6U9VdO/different-depth/longer-independent-workspace-b`,
  clean start 08:28:12 UTC, snapshot 08:30:11 UTC.
- Both: build exit 0, zero warnings, source SHA as above, empty tracked-source
  status, identical sdkconfig SHA-256
  `6d54d05c0e9e343175c423fed97f6df3249155e7838092a88c884ae11c00c725`.
- SDK/compiler as pinned above; CMake 4.0.3, Ninja 1.11.1, Python 3.12.3,
  Linux x86_64/glibc 2.39, UTC, C.UTF-8; ccache disabled, SOURCE_DATE_EPOCH
  and inherited C/C++/preprocessor/linker flags unset.
- Actual app compilation has 150 SDK prefix-map flags per build, including both
  different project roots -> /IDF_PROJECT, build roots -> /IDF_BUILD,
  main -> /COMPONENT_MAIN_DIR, SDK -> /IDF, and toolchain -> /TOOLCHAIN.
  Exact commands and generated GDB substitutions are in each build's evidence.

Every produced .bin is included, including the four CMake compiler ABI probes.
These probes are not flash images; filtering them out would narrow the requested
all-binary comparison. Paths below are relative to each build directory.

| Binary | Bytes A = B | A SHA-256 | B SHA-256 | Result |
|---|---:|---|---|---|
| `CMakeFiles/4.0.3/CMakeDetermineCompilerABI_C.bin` | 9832 | `2e17a82b1b3d62c0d63d2a940e35310d0f99983f44a2e91f48368edae07f15e9` | `2e17a82b1b3d62c0d63d2a940e35310d0f99983f44a2e91f48368edae07f15e9` | PASS |
| `CMakeFiles/4.0.3/CMakeDetermineCompilerABI_CXX.bin` | 9856 | `1f2415b09fad4f709656624f0d5e7f9926ce931e2e0e0f7e3f9ba2591373ecef` | `1f2415b09fad4f709656624f0d5e7f9926ce931e2e0e0f7e3f9ba2591373ecef` | PASS |
| `bootloader/CMakeFiles/4.0.3/CMakeDetermineCompilerABI_C.bin` | 9832 | `2e17a82b1b3d62c0d63d2a940e35310d0f99983f44a2e91f48368edae07f15e9` | `2e17a82b1b3d62c0d63d2a940e35310d0f99983f44a2e91f48368edae07f15e9` | PASS |
| `bootloader/CMakeFiles/4.0.3/CMakeDetermineCompilerABI_CXX.bin` | 9856 | `1f2415b09fad4f709656624f0d5e7f9926ce931e2e0e0f7e3f9ba2591373ecef` | `1f2415b09fad4f709656624f0d5e7f9926ce931e2e0e0f7e3f9ba2591373ecef` | PASS |
| `bootloader/bootloader.bin` | 21072 | `da59a63758341ef54ff0083f178ea1f29ed7bc9cf89a8c7f64e9b0d2d9a9184b` | `da59a63758341ef54ff0083f178ea1f29ed7bc9cf89a8c7f64e9b0d2d9a9184b` | PASS |
| `noctinirs2.bin` | 169952 | `6741624ccd5f6cbbba74b285ad35af7987baae117b648dd702d973899fb0d041` | `6741624ccd5f6cbbba74b285ad35af7987baae117b648dd702d973899fb0d041` | PASS |
| `partition_table/partition-table.bin` | 3072 | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` | PASS |

Existing [Class B PR run 39017211513](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017211513)
and [push run 39017207927](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017207927)
both pass with zero warning lines; their three firmware images also match this
matrix. [Reference PR run 39017211512](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017211512)
and [push run 39017207923](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017207923)
pass on the same SHA. Linux host tooling: 27/27 methods pass; Class S legacy
fixtures/invariant pass, while the production dependency gate is explicitly
SKIPPED / NOT APPLICABLE because product layers are absent.

The documentation/upload-completeness follow-up must receive fresh CI on its own
head. The final exact head, run URLs, artifact IDs and complete matrix are recorded
in [Draft PR #40's append-only independent-review handoff](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40).
Each CI artifact identifies its own source SHA. This avoids a self-referential
commit hash and does not treat earlier-head success as evidence for a later head.

Initial CI at `eda7adc112b010aef5d2fde3b5bbc404df5e1104` stopped before any
build: Git rejected the container checkout ownership on the first repository
operation. [Failed run with preserved diagnostics](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39017024779).
The runner now supplies a scoped safe.directory for that first operation, then
registers only the selected checkout path; no global wildcard or failed check is
ignored. This is an orchestration failure, not a binary nondeterminism finding.

## Acceptance traceability and limits

| Requirement | Result at measured implementation SHA / evidence |
|---|---|
| All `.bin` bytes identical across clean paths | PASS, all 7 binaries / 14 hashes; full matrix and retained artifacts above |
| Immutable IDF v6.1 toolchain | PASS, registry index/child verification; IDF/compiler enforcement |
| Mismatch, artifact-set and incomplete failures | PASS, 27 host tooling methods including CLI and shell failure paths |
| Actionable diagnostics | PASS, fixture assertions for JSON/Markdown names/sizes/hashes/offsets/environment; always-upload CI |
| Review sdkconfig and keep Class B zero-warning | Build PASS, zero warnings; single default documented; independent configuration review PENDING |
| Reference integrity | PASS, exact-head CI and local 319-file verification |
| Independent handoff | Draft PR #40 checklist and final append-only hub handoff; independent approval PENDING |

Residual risks: this verifies the current scaffold, target and linux/amd64 image,
not arbitrary future components, hosts or SDK revisions. Future uses of date/time
macros, absolute paths or generated/random data can regress determinism; the
binary gate must then fail. Mutable hosted runner infrastructure, CPU/kernel,
network availability and the existing workflows' action tags remain outside the
container pin. Registry outage fails closed. Artifact retention is 90 days; copy
evidence to approved controlled storage before expiry if needed. Generated
metadata/logs can legitimately contain workspace paths and times; their raw
content is retained for diagnosis and is not firmware binary equality evidence.
IRAM baseline concerns belong to Issue #12. Combined compatibility with unmerged
PRs #37/#38/#39 needs fresh integration CI; their green runs are not this PR's
evidence. Independent configuration review and Owner merge approval are pending.

No on-target, HIL, field, flash, signing, electrical or clinical test was run.
Board revision: not applicable to this host-only ESP32-S3 scaffold build. Rollback:
reviewed revert of this PR, with no device state or partition changes. No automatic
Reviewer Agent workflow exists in product or hub; use the hub's independent PR
review/handoff contract. The author cannot provide that independent approval.

Primary SDK references at the exact pinned commit:
[reproducible build implementation guide](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/docs/en/api-guides/reproducible-builds.rst),
[root Kconfig](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/Kconfig),
[app descriptor Kconfig](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_app_format/Kconfig.projbuild).


## PR #40 F1/F2 remediation and current-main integration

The preceding measurements and acceptance table are historical evidence for their
named commits. Independent review at `f97767035e0ab4f801e655ec751141314668c407`
[requested changes](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40#issuecomment-6237139654):
the old inventory could suppress nested enumeration errors (F1), and the Class B
warning guard accepted missing or invalid logs (F2). Its broad completeness and
warning-enforcement claims were therefore not established by those normal runs.

F1 now uses explicit scandir traversal with no suppressed enumeration, iteration,
entry-stat or binary-read errors. An error discards the inventory, marks snapshot
capture incomplete, or makes retained-evidence comparison FAIL with diagnostics.
No partial matching inventory establishes completeness. Symlinks and non-regular
binary entries fail. Optional nested binaries remain part of the inventory.

F2 replaces the workflow's grep/error suppression with the same strict warning-log
validator used by snapshot capture and retained-evidence loading. Logs must be
readable, nonempty regular files, contain valid UTF-8 without NUL bytes, and have
zero case-insensitive `warning:` lines. Non-regular logs (including symlinks and
FIFOs), read errors and invalid text fail. A stale warning-count file is removed
before validation; only a successful read can produce a numeric count. The count
is retained even when it is nonzero. Comparison also checks the recorded count
against the actual retained log rather than trusting a manifest's zero.

New host fixtures inject nested PermissionError/FileNotFoundError/OSError during
comparison and snapshot capture with a real optional binary, inject binary/log
read errors, and execute the entire committed Class B Hash outputs block on
ordinary, missing, directory, empty, binary and invalid-UTF-8 logs. Linux adds
FIFO, symlink and real non-root permission-denial fixtures. The regression-first
runner uses the exact reviewed helper/workflow blobs in a temporary directory;
it requires both F1 paths and the F2 workflow case to fail on that baseline.
These are tooling regressions, not product firmware Class U or hardware evidence.

Current main `d92796d` is integrated, including merged #38/#39/#43. The sole workflow
conflict preserves main's evidence checksums alongside strict zero-warning
enforcement. Canonical layer detection, parser regressions, fail-closed invariant,
C11 host/target/header/IDF probes and evidence retention remain in place. The
reference workflow retains #40's explicit PR-head checkout. No runtime source,
SDK/image pin, sdkconfig default, ADR decision, historical snapshot, hardware or
repository setting changes are part of this remediation.

The append-only remediation handoff on PR #40 records the resulting full SHA,
fresh exact-head CI, two new isolated builds, raw artifact checksums and IR
re-review request. Earlier-head runs are not substituted for that new evidence.
The PR stays draft, Issue #9 remains open, and author-side verification does not
satisfy independent review or human merge approval. Recovery is a reviewed revert;
no device state changed. On-target/HIL/field tests, flashing and signing: NOT RUN.
