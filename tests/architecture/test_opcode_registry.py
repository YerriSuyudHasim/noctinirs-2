"""Opcode registry conformance for health and timing. No device."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import nn2ctl


class OpcodeRegistry(unittest.TestCase):
    def test_health_and_timing_are_registered(self):
        header = (ROOT / "components/nn2_core/include/nn2_core_protocol.h").read_text()
        source = (ROOT / "components/nn2_core/src/nn2_core_protocol.c").read_text()
        adr = (ROOT / "docs/adr/0004-host-protocol.md").read_text()
        local = (ROOT / "docs/adr/0004-local-implementation.md").read_text()
        dispatch = (ROOT / "components/nn2_app/src/nn2_app_comm.c").read_text()

        self.assertRegex(header, r"NN2_CMD_GET_HEALTH\s*=\s*0x0030")
        self.assertRegex(header, r"NN2_CMD_GET_TIMING\s*=\s*0x0031")
        self.assertIn("{NN2_CMD_GET_HEALTH, 0, false}", source)
        self.assertIn("{NN2_CMD_GET_TIMING, 0, false}", source)
        self.assertEqual(nn2ctl.COMMANDS["health"], 0x30)
        self.assertEqual(nn2ctl.COMMANDS["timing"], 0x31)

        health = _registry_row(adr, "0x0030")
        timing = _registry_row(adr, "0x0031")
        self.assertIn("`get_health`", health)
        self.assertIn("`get_timing`", timing)
        self.assertIn("UDP only", health)
        self.assertIn("UDP only", timing)
        self.assertIn("bypass", health)
        self.assertIn("bypass", timing)
        self.assertIn("| 0030 | health | none | bypass |", local)
        self.assertIn("| 0031 | timing | none | bypass |", local)
        self.assertIn("2026-10-29", adr)
        self.assertIn("nn2ctl", adr)
        self.assertIn("NN2_CMD_GET_HEALTH", dispatch)
        self.assertIn("NN2_CMD_GET_TIMING", dispatch)
        self.assertNotIn("frames=ACQ1", health)
        self.assertNotIn("frames=ACQ1", timing)

    def test_netdiag_is_registered_read_only_udp_only(self):
        header = (ROOT / "components/nn2_core/include/nn2_core_protocol.h").read_text()
        source = (ROOT / "components/nn2_core/src/nn2_core_protocol.c").read_text()
        adr = (ROOT / "docs/adr/0004-host-protocol.md").read_text()
        local = (ROOT / "docs/adr/0004-local-implementation.md").read_text()
        dispatch = (ROOT / "components/nn2_app/src/nn2_app_comm.c").read_text()
        self.assertRegex(header, r"NN2_CMD_GET_NETDIAG\s*=\s*0x0032")
        self.assertIn("{NN2_CMD_GET_NETDIAG, 0, false}", source)
        self.assertEqual(nn2ctl.COMMANDS["netdiag"], 0x32)
        row = _registry_row(adr, "0x0032")
        self.assertIn("`get_netdiag`", row)
        self.assertIn("UDP only", row)
        self.assertIn("bypass", row)
        self.assertIn("| 0032 | netdiag | none | bypass |", local)
        self.assertIn("NN2_CMD_GET_NETDIAG", dispatch)
        self.assertIn("v4netdiag=0 reason=unavailable", dispatch)
        self.assertIn("file rev 5", adr)


def _registry_row(text, opcode):
    for line in text.splitlines():
        if line.startswith("| " + opcode + " |"):
            return line
    raise AssertionError("missing registry row " + opcode)


if __name__ == "__main__":
    unittest.main()
