#!/usr/bin/env python3
WLENGTH_MASK = 0x0300
WLENGTH_24 = 0x0100
LOCK = 0x8000
F_RESYNC = 0x4000
REG_MAP = 0x2000
CRC_ERR = 0x1000
CRC_TYPE = 0x0800
RESET = 0x0400
HARD = LOCK | REG_MAP | CRC_ERR | RESET

def classify(status: int):
    resync = bool(status & F_RESYNC)
    base_valid = ((status & WLENGTH_MASK) == WLENGTH_24 and
                  (status & CRC_TYPE) == 0 and
                  (status & HARD) == 0)
    return base_valid, resync

cases = {
    0x010F: (True, False),
    0x410F: (True, True),
    0x450F: (False, True),
    0x210F: (False, False),
    0x110F: (False, False),
    0x810F: (False, False),
    0x090F: (False, False),
}
for status, expected in cases.items():
    got = classify(status)
    assert got == expected, f"0x{status:04X}: got={got} expected={expected}"

budget = 1
for idx, status in enumerate((0x410F, 0x010F, 0x010F)):
    base_valid, resync = classify(status)
    effective = base_valid
    if effective and resync:
        if budget:
            budget -= 1
        else:
            effective = False
    assert effective, f"discard frame {idx} rejected unexpectedly"
assert budget == 0

base_valid, resync = classify(0x410F)
effective = base_valid and not resync
assert not effective, "steady-state repeated F_RESYNC must be rejected"
print("PASS ADS status policy: bounded post-SYNC F_RESYNC, hard faults retained")
