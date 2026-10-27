# SoftAP onboarding and STA recovery

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Status: Owner-approved behavior; implementation pending. This document supersedes
USB-only onboarding as the target UX, not as a claim about current firmware.
Current source still uses USB provisioning and STA only. UDP NN2-CF1 is unchanged.

## User flow

1. With no saved network, start a password-protected NoctiNIRS-XXXX SoftAP.
2. The computer joins that network. The desktop host contacts the provisioning
   service and submits the target SSID/password. Manual SSID entry is sufficient
   for the first increment; network scan is a later convenience.
3. Keep SoftAP available while trying the candidate using AP+STA mode. This uses
   one radio: STA channel selection can interrupt AP clients; the host must retry.
4. After obtaining a usable STA address, commit the candidate to NVS. A failed
   attempt must not overwrite the last accepted credentials. On commit failure,
   report failure and retain provisioning access; do not claim durable success.
5. Host reads the result/address over AP, switches the computer to the target LAN,
   reconnects to the device and confirms handover. Close AP only after handover
   confirmation; DHCP success alone does not prove host reachability.
6. Normal commands use the existing UDP protocol on STA. A fresh control lease is
   required after transition; never resume acquisition automatically.

The first host version may guide manual Windows network switching and use the
reported IP. Automatic network switching/discovery is not a prerequisite.
USB remains an optional service/recovery interface, not the normal user flow.

## Recovery policy

| Trigger | Required action |
|---|---|
| Host heartbeat expires while STA is healthy | Revoke lease, request off; keep STA, do not open AP solely for this |
| STA loses association or usable IP | Revoke lease immediately, request off, retry using bounded backoff |
| STA remains unavailable for 60 seconds | Enable recovery AP while continuing bounded STA retries |
| Candidate cannot connect within 60 seconds | Reject candidate, wipe temporary secrets, retain AP and restore prior accepted configuration if available |
| STA recovers with AP active | Keep AP until host confirms STA handover; require a new lease |
| Storage error | Explicit failure, no broad NVS erase, no false success |
| Reboot | Request emitter off first; load accepted config, otherwise enter AP setup |

The 60-second interval is an implementation default chosen for the approved flow,
not a measured performance guarantee. Use monotonic deadlines, not blocking waits.
Provisioning must not grant measurement ownership. No hardware activation is added.

## Component changes, in implementation order

| Component | Planned responsibility/API direction |
|---|---|
| nn2_wifi | STA/AP netifs; explicit lifecycle state; start with/without stored credentials; begin candidate; poll retries/deadlines; expose AP/STA addresses and transition epoch; confirm handover |
| nn2_config | Keep accepted credentials; persist only verified candidate; maintain separate device AP identity/secret storage |
| nn2_provision | Retain USB compatibility; add a bounded network provisioning adapter or a separately documented adapter component, using shared credential validation |
| nn2_app / nn2_ports | Host-testable onboarding policy and injected persistence; no SDK dependency in policy |
| nn2_net | Serve operational UDP on STA only; do not let a wildcard socket expose operation/control on the setup AP; revoke leases on transitions |
| app_main | Wire storage, onboarding and off protection; start setup when unconfigured; poll services; propagate failures |
| Host | Provisioning session, result polling, manual network-switch guidance, STA confirmation, then normal UDP session |

These are API directions, not declarations of existing symbols. Keep one owner for
Wi-Fi mutations; SDK callbacks publish bounded events/snapshots only. Do not add
one task per component. Polling operations must not wait for network completion.

## Contract and security work before network provisioning is enabled

- Define the provisioning endpoint, version, bounded request/response sizes,
  attempt ID, duplicate behavior, status/error codes and STA handover confirmation.
  Keep it separate from the frozen NN2-CF1 operational commands; do not silently
  repurpose an opcode or send credentials through the command transaction log.
- Use a unique random AP password, persisted per device. No common password and
  no password derived only from public MAC/SSID. It must be provided to the user
  through a device label/QR or explicit initial setup. The delivery mechanism is
  an unresolved product input; never claim USB-free first setup until resolved.
- Restrict provisioning to the AP interface; never expose credential replacement
  on the normal LAN by accident. Validate source/interface in the adapter.
- Never echo/log passwords; bound one candidate attempt and wipe temporary copies.
- The current USB path stores before connection validation. Reconcile its semantics
  explicitly with the new candidate workflow; do not keep contradictory save rules.

## Checkpoints and verification

CP6: record approved behavior, current-source gap and build evidence (this change).
CP7: implement host-testable onboarding state/deadline/storage policy and Wi-Fi
AP+STA adapter using the installed IDF v6.1 headers/examples; define provisioning
wire contract and password delivery before exposing that endpoint.
CP8: integrate provisioning, main and interface-specific UDP; update API/setup docs.
CP9: add desktop host onboarding and simulator support. Packaging targets a Windows
installer/application; UI framework is not selected by this decision.

Required cases: no config, correct/incorrect credentials, missing router, DHCP
failure, commit failure, repeated submission, interrupted AP client, STA loss,
60-second fallback, successful handover, host-only heartbeat loss, restart with
accepted credentials and no automatic resume. Add fake-clock policy tests first.
Board procedures must cover AP channel migration, both computer network switches,
10 connection interruptions and 10 restarts. Preserve existing soak/off limits.
Owner performs builds/flash/board operation; source/host simulation are separate
from RF, electrical and on-board evidence.
