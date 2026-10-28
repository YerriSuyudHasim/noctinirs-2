# Automatic STA onboarding: security requirements and actual status

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Owner requested no manual SSID/password entry and secure automatic STA setup.
Default UX: a one-time unique device QR pairing, then Connect. No automatic pairing
with arbitrary devices bearing the NoctiNIRS SSID. QR enrollment is a proposed
product assumption, not a tested or already manufactured provisioning artifact.

## What can be automated

The Windows host may read ONLY the currently selected connected WLAN profile using
Native Wi-Fi APIs and Windows-granted permissions. It must not enumerate/export all
saved passwords, invoke netsh key=clear, elevate silently, write credentials to disk,
or log profile XML. Capture the authorized target before joining the device AP.
Support personal WPA2-PSK initially. Reject enterprise/802.1X, captive portal,
unsupported bands/security and protected profile keys. Report unsupported setup
instead of guessing, weakening Wi-Fi security or promising universal zero-input.

A device cannot discover a router password or establish trusted first pairing from
an SSID alone. Required trust material must come from device enrollment (QR/label or
an explicit initial local service step). Windows may still require an OS permission.
After enrollment, keep the pairing secret in Windows-protected storage, not a JSON
file. No app/device password is accessed on the lab computer by this source task.

## Secure transport decision

Use the installed ESP-IDF protocomm Security 2 (SRP6a + AES-GCM) as the intended
provisioning security mechanism; do not invent cryptography. Headers inspected:
components/protocomm/include/security/protocomm_security2.h and
components/protocomm/include/common/protocomm.h under C:/esp/v6.1/esp-idf.
No SDK/dependency was installed. Secure endpoint and desktop client are NOT YET
implemented. The draft plaintext N2P1 UDP setup implementation was removed from
active behavior; nn2_setup_poll is explicitly a no-op until secure transport exists.
This is a visible implementation gap, not simulated onboarding success.

Remaining implementation: manufacture/enroll SRP verifier and QR identity; bounded
secure transport sessions and replay handling; main-owner candidate handoff;
authenticated STA handover; Windows WLAN capture/switch/restore; protected pairing
storage; host UI and installer. Preserve one Wi-Fi mutation owner even if HTTP
transport introduces an SDK task. Define and test a bounded command mailbox.

Existing NN2-CF1 operational UDP remains unauthenticated, even with secure
provisioning. Do not call the whole application secure until authenticated control
and replay protection are designed and independently tested. NVS encryption and
physical secret protection are also not enabled; eFuse changes are not authorized.

## Current source candidate

v0.1.0.1 contains AP+STA resources, random per-device WPA2 AP password persisted in
NVS, 60-second recovery/candidate deadlines, injected persistence after DHCP,
STA-interface binding for operational UDP, and main integration. Local USB WIFI
now submits a candidate rather than immediately saving. USB SETUP explicitly
reveals setup credentials for development enrollment, never normal STATUS/logging.
No network credential endpoint is enabled. These C changes are uncompiled and
on-target behavior is NOT RUN. The previous Owner build applies to v0.1.0 only.

## Acceptance additions

Wrong QR/identity, tampered encrypted messages, replay and unpaired hosts must
fail without changing credentials. Denied Windows permissions must produce a
sanitized explanation without credential export. After interrupted setup, restore
the computer's previous network and retain the device's accepted credentials.
Test DHCP, persistence failure, 60-second AP fallback, AP channel migration and
host-only heartbeat expiry separately. No automatic measurement resume.

Sources: https://learn.microsoft.com/windows/win32/api/wlanapi/nf-wlanapi-wlangetprofile
and the installed IDF v6.1 headers above. Product behavior is not proof of security.
