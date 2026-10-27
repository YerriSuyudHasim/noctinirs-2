# Communication Foundation verification matrix

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Source: base `5958a0eafec6b03c2b158c4026f3f10b8a3366d1` plus local working tree.
Exact implementation fingerprint/inventory: `evidence/communication-source.json`.
Documentation/editor state/generated build inputs are explicitly excluded from
that implementation fingerprint. No uncommitted code is attributed to base HEAD.

Latest Owner scope: no C/ESP-IDF compilation or board operation by this agent.
Host Python and static checks are separate evidence classes, not build success.

| ID | Implementation / procedure | Verification state |
|---|---|---|
| C01 | Integrated app_main, eight components, isolated v6.1/ESP32-S3 defaults | Source inspected; target build/warnings/size NOT RUN |
| C02 | Hex USB parser, validation, one NVS blob, no echo/erase | Python encoding tests PASS; C storage-error test source supplied NOT RUN; actual NVS/reboot persistence NOT RUN |
| C03 | STA lifecycle, capped backoff, link epoch, UDP reopening | Source inspection only; reconnect/RF tests NOT RUN |
| C04 | NN2-CF1 specification, registry, golden vectors | Five Python tests PASS including independent vector comparison; C codec execution NOT RUN |
| C05 | Info/status/time/config/claim/heartbeat/start/stop/safe-off/fault/release | Source and C tests supplied; actual C/board command execution NOT RUN |
| C06 | Length/CRC/opcode/argument checks, ID cache/conflict/order, socket counters | Python negative codec cases PASS; native state/malformed cases NOT RUN; socket faults/SDK buffer pressure NOT RUN |
| C07 | 3-second lease, duplicates do not renew, known loss revokes immediately | Fake-clock C test source supplied NOT RUN; no timer callbacks used |
| C08 | GPIO18 reset held low; no activation API/LED bus | Schematic/source inspection; physical boot/reset/off tests NOT RUN |
| C09 | Independent Python codec/CLI and real production-C UDP loopback source | CLI help/Python checks PASS; two native-loopback tests explicitly SKIPPED because executable not built |
| C10 | Persistent boot counter, monotonic microseconds, synthetic sequence accounting | Python wrap/loss/duplicate/out-of-order test PASS; C clock/wrap tests NOT RUN; no real sample timing claim |
| C11 | 30-minute soak, 10 link interruptions, 10 restarts with limits | Procedure in BOARD_COMMUNICATION_TEST.md; all board execution NOT RUN |
| C12 | Setup, API map, roadmap, protocol, decisions, checkpoints and handoff | Present locally; source fingerprint retained |

## Executed checks and limitations

- `python test/communication/test_host.py -v`: five PASS; two native-loopback
  cases explicitly skipped. No packet was sent to a board.
- `python tools/nn2ctl.py --help`: PASS; no service/serial access.
- `python tools/class-s-gate.py`: PASS after explicitly classifying the new
  platform components and forbidding their headers in pure policy.
- `tests/architecture/test_platform_boundary.py`: five tests, including negative
  core -> platform import rejection; see current local execution log.
- Existing `test_manifest.py`: 14 PASS; one shell-integration test **BLOCKED by
  environment** (unittest reports FAIL). Git Bash cannot create its MSYS named
  object: access denied, exit 3221225794. No bypass attempted and no test disabled.
- Initial temporary-directory runs were blocked by the default sandbox temp path.
  Reran using a dedicated writable folder under `build/communication-checks-tmp`.
  This resolved the Python fixture access errors, not the separate Git Bash block.
- `git diff --check`: PASS; Git emits informational CRLF-to-LF normalization
  messages, not compiler diagnostics.
- Reference integrity: 319 files PASS before work; final result recorded in HANDOFF.

Local logs: `build/communication-platform-checks.log` and
`build/communication-manifest-checks.log`. The latter records the real environment
failure. The C loopback/policy executables and integrated firmware have not been
built in this session, so there are no usable compiler warnings or size figures.

The earlier exploratory host compiler attempt in the prior turn failed to find a
Visual Studio environment. It did not produce a verified executable. After the
Owner reserved builds for themselves, no compiler/build command was run.

## CP6 evidence and acceptance update

Owner reports the CP5 firmware build succeeded. Full logs, warnings, size and
artifact/configuration hashes are not archived; earlier NOT RUN entries describe
assistant execution, not a denial of the Owner report. Hardware remains NOT RUN.
SoftAP changes are documentation only: C02/C03 must additionally pass candidate
validation/commit failure, AP+STA handover and 60-second fallback cases defined in
NETWORK_ONBOARDING.md. These additional cases have NOT RUN.
