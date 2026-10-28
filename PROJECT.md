# NoctiNIRS2 real firmware

> Owner scope update: v0.1.0 SoftAP-only, SSID NoctiNIRS, public prototype password
> 123456789. docs/SOFTAP_CURRENT.md supersedes STA/provisioning requirements below.
> C02 now covers fixed AP configuration and DHCP; C03 covers client reconnection.
> Router-board integration is deferred. Do not implement STA onboarding this release.

Project charter and implementation instructions — 2026-10-22, Asia/Seoul.

## 1. Objective

Build working ESP32-S3 firmware, release by release, using ESP-IDF v6.1. Each release delivers an integrated main application and the reusable components it needs. Accuracy, explicit timing semantics, observable failures, and repeatable operation matter more than the number of features or abstractions.

NoctiNIRS V3 and V4 under the local `reference/` directory are the primary historical engineering references. Adapt their useful behavior to the current board and ESP-IDF v6.1 APIs. Preserve those snapshots unchanged. Study them; do not assume either is a complete, correct specification or copy an entire old firmware into the active application.

## 2. Owner-directed local workflow

The Owner has selected local files as the ground truth for this development track. The working folder is also connected to GitHub, but GitHub publication and multi-agent review happen later.

- Work directly on the selected local project in VS Code/Codex.
- Inspect the actual repository, branch, dirty files, SDK, and reference paths before editing. Preserve existing work and use a separate local branch when needed.
- Read applicable local instructions. This Owner-directed workflow replaces the old requirement to obtain an issue, remote assignment, PR, CI, or agent approval before ordinary reversible local implementation and tests. Record that scoped workflow decision locally; do not silently erase policy files or assume it authorizes remote actions.
- Do not use remote `main`, issue comments, or unfinished PRs to overwrite local work. They can be consulted when requested or when a specific missing engineering input requires them.
- Keep progress, decisions, test results, and handoffs in local Markdown. Record source identity using the commit plus a diff/working-tree fingerprint when changes are uncommitted.
- No automatic push, PR, merge, tag, release, or messages to other agents. Publication and review will be requested separately.
- Prepare buildable firmware and host checks autonomously. Flashing, RF/on-board testing, and emitter operation require the Owner's applicable hardware authorization and a concrete test procedure. Do not treat a request to write firmware as permission to operate hardware.
- Communicate with the Owner in Indonesian. Use English for source, APIs, tests, and engineering records.

This file is a portable project charter. It does not establish the location of the future firmware checkout. At startup, resolve the selected working folder and its real `reference/` paths. Never fabricate a board revision, pin map, installed SDK, test result, or measurement.

## 3. Release goals

The four functional milestones match the existing NoctiNIRS 2 roadmap. Its GitHub scheduling and publication process are replaced by the local workflow above. No inherited calendar deadline is imposed here.

| Version | Goal | Integrated deliverable | Boundary |
|---|---|---|---|
| **0.1.0 — Communication Foundation** | A PC can configure and communicate with the ESP32-S3 reliably over Wi-Fi. | SoftAP onboarding and recovery with optional USB service access, verified-credential NVS persistence, STA connection/reconnection, UDP request/reply protocol, validation/CRC, transactions, command/state handling, heartbeat/link-loss handling, a small host CLI and automated conformance tests. Hardware-dependent command results are explicitly simulated; emitter-off protection is real. | No active LED drive, optical acquisition, or continuous sample streaming. |
| **0.1.1 — Deterministic LED Control** | Drive the intended LED channels with bounded, testable behavior. | PCA9956B library, verified channel map and current limits, readback, 20-slot plan where supported by the board contract, GPTimer-based scheduling, safe-off, fault/recovery handling, timing instrumentation, and communication integration. | No claim of ADC synchronization or optical-frame correctness yet. |
| **0.1.2 — Streaming Path** | Send a continuous stream with detectable and accounted loss. | FRAME codec, bounded buffers/queues, sequence/timestamp metadata, drop/overrun counters, session report, host recording/validation, synthetic and ADS131 raw sources kept distinct. | No claim that ADC samples are aligned with LED ON/OFF windows. |
| **0.1.3 — Synchronized LED–ADC Acquisition** | Produce real optical fNIRS frames with a defined timing relationship. | DRDY-based capture, LED ON/OFF windows, settle/guard rules, per-slot mean and sample count, synchronized optical payloads, measured latency/jitter and loss accounting. Evaluate K=10 and fs={1,2,4,5,8,10} from the prior roadmap against measured feasibility; reject unsupported configurations. | ADS1299/dual-rate ExG and a full host GUI remain later work. |

0.1.1 and 0.1.2 may be developed independently after 0.1.0; 0.1.3 integrates both. Prefer finishing one coherent milestone at a time on the working branch.

A milestone has separate implementation, host/build verification, and hardware verification states. A local candidate may be build/host verified while board tests remain NOT RUN. Do not call it operationally validated on a real board until its applicable board tests pass. A version number or GitHub tag is not proof of working hardware.

## 4. Sources and legacy adoption

Use the following inputs with explicit boundaries:

1. Owner goals and decisions recorded locally for this track.
2. Local active firmware, configuration, board schematic/netlist, datasheets, and measured evidence. Resolve conflicts between these rather than choosing a convenient source.
3. Local V3/V4 implementation as historical evidence of behavior and known problems.
4. ESP-IDF v6.1 official documentation, the exact installed SDK source, and version-matched examples for API use.
5. Earlier NoctiNIRS 2 ADRs and implementation, reused where suitable. Identify Proposed versus Accepted text; do not silently treat a draft wire format as frozen.

Create `docs/LEGACY_ADOPTION.md` with: V3/V4 path and symbol; observed behavior; keep/change/reject decision; reason; new implementation location; corresponding test. Especially examine communication framing, credential handling, reconnect, LED ordering, ADC acquisition, timestamps, buffering, and failure behavior. A legacy constant is not sufficient proof of a pin or electrical limit.

Scan historical inputs without printing secrets. Do not bring credentials, binaries, build caches, or old SDK copies into active source. Verify the existing reference manifest before/after work when available; otherwise create and compare an external checksum inventory without modifying the snapshots.

## 5. ESP-IDF v6.1 engineering standard

- Target ESP32-S3 and pin the actual ESP-IDF v6.1 revision used; record toolchain and extension/environment details. Do not silently substitute another IDF version.
- Follow the official IDF style guide: component-prefixed public symbols, file-local `static` implementation details, and consistent formatting. Preserve provenance and licenses for adapted code.
- Use the supported component/CMake model with explicit dependencies, public/private headers, and Kconfig configuration. Use the SDK's own drivers and facilities where appropriate.
- Interpret “maximize v6.1” as choosing useful supported facilities with an engineering reason, not enabling every feature. Record API choice, v6.1 documentation/example, benefit, limitations, and verification in `docs/IDF61_DECISIONS.md`.
- Check migration guidance before carrying an old API forward. Avoid deprecated interfaces, undocumented peripheral register manipulation, or new third-party dependencies when the supported IDF facility meets the goal.
- Keep the domain codec/state logic host-testable. ESP-IDF/FreeRTOS, sockets, flash storage, and peripheral access belong in platform components.
- Bound queues, buffers, retries, waits, and callback work. Define task ownership and synchronization. Use ISR-safe APIs only in ISR paths; never block in callbacks that must keep servicing communication.
- Use explicit byte encoding and length checks, not C struct packing as a wire protocol. Define signedness, endianness, ranges, CRC, errors, and compatibility behavior.
- `esp_timer_get_time()` is a candidate for device monotonic timestamps. Timer resolution is not a measurement of end-to-end acquisition precision. For later LED deadlines, assess GPTimer and measure actual behavior.
- Report flash/RAM usage, configuration, and warnings. Preserve relevant existing protections. No blanket warning suppression or pretending a skipped check passed.

## 6. Main application and components

Keep `main/app_main.c` focused on startup order, dependency wiring, and application lifecycle. Put reusable behavior behind documented component APIs. Do not create one task per component by default.

Logical responsibilities for 0.1.0:

| Responsibility | Required behavior |
|---|---|
| Board/safe boot | Establish verified emitter-inactive state before normal application activity, preserve required reset/off sequence, report startup failures and reset reason. Fail closed on missing critical board inputs. |
| Configuration/credentials | Validate and store network configuration in NVS after successful candidate connection; SoftAP provisioning and optional USB service access; mask credentials and support explicit replacement. Never automatically erase all NVS to recover an unrelated error. |
| Wi-Fi connection | SoftAP/AP+STA onboarding, STA lifecycle, reconnect/backoff, 60-second recovery AP fallback, link events, no connection-state busy loops. See docs/NETWORK_ONBOARDING.md. |
| Protocol core | Codec, CRC, sizes/flags, request IDs/cache, command registry, errors and hardware-independent state transitions. |
| Communication service | UDP socket lifecycle, peer ownership, request/reply, heartbeat and link-loss behavior, responsive safe-off, counters. |
| Host tools | Independent Python codec/CLI and tests that send valid/invalid packets, exercise retries and validate responses. |

These are responsibilities, not mandatory new component names. Reuse or extend suitable local `nn2_core`, `nn2_ports`, `nn2_app`, adapters, and other existing code. Make a brief dependency map before implementing. Preserve supported architecture boundaries; justify a material change locally rather than building a duplicate architecture alongside the existing one.

## 7. v0.1.0 acceptance criteria

| ID | Required result |
|---|---|
| C01 | The selected local project builds from documented inputs with ESP-IDF v6.1 for ESP32-S3. Main application links and invokes the required components. No new unexplained warnings. |
| C02 | SoftAP provisioning accepts valid credentials, rejects invalid lengths/formats, masks secrets, and persists settings only after successful candidate connection; optional USB service behavior must follow the documented policy. Host tests cover storage errors; reboot persistence is verified on the authorized board. |
| C03 | STA connects, disconnects and reconnects with bounded backoff; UDP service responds again after recovery. No reset, unbounded retry loop, or credential disclosure. |
| C04 | A local protocol specification and shared golden vectors define the exact wire format, CRC, sizes, flags, error codes, opcodes, states, and peer/transaction behavior. Pending ADR conflicts are resolved or explicitly isolated. |
| C05 | Host can exercise info/status/configuration, heartbeat, start/stop, safe-off and fault commands defined by that contract. Hardware-dependent outcomes are explicitly SIMULATED and never claim physical LED/ADC execution. |
| C06 | Tests cover malformed/short/oversize packets, bad CRC, unsupported versions/flags/opcodes, invalid arguments/states, duplicate IDs, cache eviction, peer conflicts, socket errors and queue exhaustion. No crash or unintended side effect. |
| C07 | Heartbeat and link loss have a defined anchor, cancellation/race rules and bounded grace (the existing roadmap uses 3 s). Fake-clock tests cover expiry, cancellation and stale notifications. Network service remains responsive during stop/recovery waits. |
| C08 | Emitter-inactive behavior is implemented from verified board inputs. Communication commands cannot energize LEDs. Boot/reset/link loss/safe-off behavior has a documented board test procedure. Simulated safe-off replies never substitute for the real off protection. |
| C09 | Python CLI can configure/access the device and run deterministic loopback conformance on a real host-built protocol implementation. No canned simulator that simply returns the expected answers. Tests fail with nonzero exit on a detected defect. |
| C10 | Device time is defined as monotonic microseconds with explicit reboot identity/reset semantics. get_time/time mapping and synthetic sequence tests follow the selected contract; device sampling accuracy is not claimed in this release. |
| C11 | Board soak procedure covers 30 minutes of request/reply traffic, 10 connection interruptions, and 10 board restarts. Record latency distribution, retry/timeouts, recovery time, reset reason, heap trend and task/stack health. Choose justified numeric acceptance limits before execution; unexplained resets, secret leakage, incorrect execution, or continued emitter operation fail. |
| C12 | README, setup/build/run instructions, API/dependency map, test matrix, known limitations and a continuation handoff exist locally. Every result identifies the source/configuration/environment; unrun board tests remain NOT RUN. |

C11 is a proposed local minimum verification procedure, not evidence that testing already happened. If hardware is unavailable, finish all independent implementation/build/host work and deliver the exact remaining board procedure. Do not report full v0.1.0 operational acceptance.

## 8. Local records and iteration

Maintain only useful records:

- `README.md`: setup, build, host CLI, configuration and operating steps.
- `docs/ROADMAP_LOCAL.md`: the four milestone goals and current status.
- `docs/DECISIONS.md`: local Owner decisions, protocol freeze, board facts and open decisions.
- `docs/LEGACY_ADOPTION.md` and `docs/IDF61_DECISIONS.md`: implementation provenance and SDK choices.
- `docs/TEST_MATRIX.md`: acceptance ID, procedure, source identity, result, log location, limitation.
- `docs/HANDOFF.md`: what works, what fails, current source/configuration and one next action.

Prefer small complete increments: protocol loopback → Wi-Fi transport → integrated main application → fault/recovery tests → authorized board validation. Keep local recovery points. Never stop at scaffolding or mock success when the real implementation can be completed with the available inputs.

## 9. Reference links

- [Original four-release roadmap, pinned snapshot](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/docs/ROADMAP.md). Functional scope reference; its old process and deadlines do not govern this local track.
- [ESP-IDF v6.1 ESP32-S3 programming guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/index.html).
- [Component/build model](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/build-system.html).
- [Official coding style](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/contribute/style-guide.html).
- [ESP Timer and timing limitations](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/esp_timer.html).
