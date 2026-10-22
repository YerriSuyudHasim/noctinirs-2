# NoctiNIRS 2 Roadmap (v0.1.x)

Status: approved by the Owner on 22 Oct 2026 (D1). This file describes the plan. Dates are targets, not commitments. The GitHub milestones hold the live state.

NoctiNIRS 2 is a non-clinical research prototype. No release makes any physiological or clinical claim.

## Release sequence

```
v0.1.0 Communication Foundation ──► v0.1.1 Deterministic LED Control ──┐
          │                                                             ├──► v0.1.3 Synchronized LED-ADC Acquisition
          └──────────────► v0.1.2 Streaming Path (synthetic + raw ADC) ─┘
```

v0.1.1 and v0.1.2 both depend only on v0.1.0, so they can run in parallel. v0.1.3 needs both of them. The ADS1299 mini-PSG work is planned for v0.2.x.

| Release | Goal | Main scope | Excluded | Class H gate |
|---|---|---|---|---|
| [**v0.1.0** Communication Foundation](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/2) | A host controls the device over Wi-Fi with the full command set. Responses that depend on hardware are simulated. | ADR-0004 host protocol; USB provisioning to NVS and STA reconnect; UDP transport (`nn2_net`); transactions keyed by `req_id` with a reply cache; full command registry; state machine and error codes; heartbeat with 3 s grace; safe-off stubs; P7 IO18 RESET# boot hook; `CONFIG_SPIRAM=n` checked at build time and at run time; `nn2ctl` host tool; CI | Real LED drive, SlotPlan timing, optical frames, streaming, ADS1299, OTA, TCP/TLS | H-COMMS-1 (emitter inactive) |
| [**v0.1.1** Deterministic LED Control](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/3) | Bounded and verifiable LED drive | PCA9956B driver (IREF ≤ 222, readback); SlotPlan with 20 slots; GPTimer deadline; SafeOff C2 order; FAULTED/RECOVERING; jitter instrumentation | ADC sync, frames | H-LED-1 |
| [**v0.1.2** Streaming Path](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/4) | A loss-accounted FRAME stream | FRAME envelope and CRC; `nn2_frame` on Core 0; bounded queues and drop counters; SESSION_REPORT; host validator; SYNTHETIC and ADS131 raw sources (never mixed) | LED-synchronised capture | H-STREAM-1, H-ADS-PSRAM |
| [**v0.1.3** Synchronized LED-ADC Acquisition](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/5) | Real optical fNIRS frames | ON/OFF capture aligned to DRDY in each slot (mean + count, K = 10); `set_fs` values {1,2,4,5,8,10}; optical FRAME payload | ADS1299, dual-rate, host app | H-SYNC-1 (fixture only, no human subject) |
| **v0.2.x** Mini-PSG | ExG path | ADS1299 adapter; dual-rate timestamp alignment (#31); ExG opcode range | — | TBD |

## v0.1.0 critical path

ADR-0004 PR (accepted) → protocol core (Class U) → `nn2_net` UDP transport → `nn2ctl` conformance → Class H H-COMMS-1 → tag.

These run in parallel: the PR #34 Wi-Fi port, P7 safe boot (it must merge before any flash), `nn2ctl`, release engineering, and the bench procedure.

Target: **30 Oct 2026 (KST)**, with a buffer to 2 Nov 2026. The assumptions behind this date are listed in the v0.1.0 milestone.

## Rules that apply to every release

- Workflow: issue → branch → Draft PR → CI → independent review on the exact head SHA by someone other than the author → joint Nori + AGENT 01 approval → squash merge.
- Each PR is reviewed on its own, with a short report. Authors never review their own work.
- Flashing, Class H (A3), tags and releases need explicit approval from the Owner.
- `reference/v2|v3|v4` is read-only. Historical behaviour is not a requirement.
- Every claim names its evidence class (S/B/U/H) and the commit SHA it was checked against. A green CI run is not evidence of hardware behaviour.

## References

- ADRs: `docs/adr/0001` (architecture), `0002` (optical timing), `0003` (RTOS topology), `0004` (host protocol, pending)
- Issues: #33 (host contract), #30 (Wi-Fi provisioning), #19 (hardware preconditions), #35 (ADS131), #31 (dual-ADC), #45 (packaging)
- Baseline: main `f2d7c8cfecdc6570df155fac4d90625c16a7bf4b`
