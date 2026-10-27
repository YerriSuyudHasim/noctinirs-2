# Local firmware roadmap

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

| Release | Goal | Current local state |
|---|---|---|
| 0.1.0 Communication Foundation | USB/NVS/STA/UDP, transactions/state/heartbeat, CLI, real reset-off request | USB/STA source implemented; Owner reports build success (logs/size pending); SoftAP revision planned; board validation NOT RUN |
| 0.1.1 LED control | Verified LED library/map/limits, deterministic timing, fault recovery | Not implemented by this track |
| 0.1.2 Streaming | Bounded continuous transport with explicit drop/sequence/timestamp accounting | Not implemented; synthetic sequence tests are not streaming |
| 0.1.3 Synchronized LED-ADC | Measured DRDY/window alignment and real optical frames | Not implemented |

No inherited calendar deadline. No full GUI, ExG, OTA, clinical claim or active
emitter operation belongs to 0.1.0. The existing diagnostic profile is retained
for its original scope and is not silently called by the communication profile.

Next action: implement CP7 from NETWORK_ONBOARDING.md, then CP8 main/provisioning integration. The Owner reports the prior source built successfully; archive logs/size when available. No board is available at the lab.
Checkpoint resume file: CHECKPOINTS_LOCAL.md.
