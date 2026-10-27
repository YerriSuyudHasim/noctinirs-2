# v0.1.0.1 implementation strategy

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Owner-authorized local revision: replace USB-first onboarding with protected
SoftAP -> AP+STA candidate validation -> STA handover. NN2-CF1 operational bytes
remain unchanged. No LED/ADC activation or streaming. No automatic builds/flash.

1. Version and provisioning contract; preserve current user source and references.
2. Extend nn2_wifi with AP resources, candidate deadline, persistence callback,
   recovery AP and explicit handover. One main task owns all mutations.
3. Add bounded setup UDP polling and restrict sockets by interface. Integrate main.
4. Extend host setup utility, static/host checks and handoff. Owner builds/bench tests.

Unique random AP password is stored in NVS. Development bootstrap uses an explicit
USB SETUP command to retrieve SSID/password once for a physical label; no automatic
password logging. This is not yet factory label/QR tooling. Password grants setup
access; do not expose this development service on an untrusted network.

Acceptance: no config opens AP; candidate failure preserves previous saved config;
only successful DHCP then NVS commit reports READY; STA confirmation closes AP;
host-only lease loss does not open AP; STA failure enables AP after 60 seconds;
all transitions keep emitters off and invalidate prior control leases. Reboot never
automatically resumes acquisition. Build/RF/board results remain separate.
