"""Batch 4a source contracts: observation-only diagnostics (D1/D2). Static text checks.

Runtime behaviour on ESP32-S3 is NOT tested here (NOT RUN: no ESP-IDF build in this environment).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NET_C = (ROOT / "components/nn2_net/nn2_net.c").read_text()
NET_H = (ROOT / "components/nn2_net/include/nn2_net.h").read_text()
CONTROL = (ROOT / "firmware/v4-idf61/main/v4_nn2_control.inc").read_text()


def frame_block(source):
    start = source.index("/* Bounded, nonblocking frame TX")
    end = source.index("return NN2_OK;", start)
    return source[start:end]


class NetCounters(unittest.TestCase):
    def test_struct_declares_frame_counters_and_old_fields_are_kept(self):
        for field in ("received", "replied", "invalid", "socket_errors", "send_errors",
                      "frame_send_errors", "frame_send_eagain", "frame_send_enobufs",
                      "frame_send_other", "last_errno", "frame_last_errno"):
            self.assertRegex(NET_H, r"\b" + field + r"\b")

    def test_frame_counters_increase_only_in_the_frame_send_path(self):
        frame = frame_block(NET_C)
        for name in ("frame_send_errors", "frame_send_eagain", "frame_send_enobufs",
                     "frame_send_other", "frame_last_errno"):
            self.assertNotIn(name, NET_C.replace(frame, ""), name + " outside frame path")
            self.assertIn(name, frame, name)

    def test_reply_path_does_not_touch_frame_counters(self):
        reply = NET_C[NET_C.index("for (unsigned budget = 0; s_socket >= 0 && budget < 4; ++budget)"):
                      NET_C.index("/* Bounded, nonblocking frame TX")]
        self.assertNotIn("frame_", reply)

    def test_frame_send_policy_is_unchanged(self):
        frame = frame_block(NET_C)
        self.assertIn("nn2_app_comm_frame_tx_result(s_app, sent==(int)count);", frame)
        self.assertIn("budget<4", frame)
        self.assertIn("MSG_DONTWAIT", frame)
        code = re.sub(r"/\*.*?\*/", "", frame, flags=re.S)
        self.assertNotRegex(code, r"\bretry\b|\bgoto\b|\bfor \(;;\)")

    def test_errno_is_captured_right_after_sendto(self):
        frame = frame_block(NET_C)
        sendto_at = frame.index("sendto(s_socket,output,count,MSG_DONTWAIT")
        capture_at = frame.index("const int frame_errno=sent<0?errno:EIO;")
        self.assertGreater(capture_at, sendto_at)
        self.assertLess(capture_at, frame.index("nn2_app_comm_frame_tx_result"))


class NetDiagKeys(unittest.TestCase):
    """Keys live in get_netdiag (kind 3). Health and timing keep their pre-Batch-4a text."""

    def netdiag_block(self):
        start = CONTROL.index("if (kind == 3u) {")
        return CONTROL[start:CONTROL.index("return netdiag > 0", start)]

    def test_heap_and_frame_keys_appear_only_in_netdiag(self):
        block = self.netdiag_block()
        for key in ("heap_free_now=", "heap_free_min=", "largest_free_block_now=",
                    "frame_send_errors=", "frame_send_eagain=", "frame_send_enobufs=",
                    "frame_send_other=", "frame_last_errno=", "net_received=", "net_replied=",
                    "net_invalid=", "net_send_errors=", "net_last_errno="):
            self.assertIn(key, block, key)
            self.assertEqual(CONTROL.count(key), 1, key + " must not appear in health or timing")

    def test_largest_free_block_walk_is_gated_to_idle_in_netdiag(self):
        block = self.netdiag_block()
        gate = block.index("control_state(NULL) == NN2_OPTICAL_IDLE")
        walk = block.index("heap_caps_get_largest_free_block(heap_mask)")
        self.assertLess(gate, walk)
        self.assertIn('snprintf(largest_now, sizeof(largest_now), "na");', block)
        self.assertEqual(len(re.findall(r"heap_caps_get_largest_free_block\(", CONTROL)), 2,
                         "only the boot snapshot and the gated netdiag read may call it")

    def test_net_status_is_read_once_in_netdiag_only(self):
        self.assertEqual(CONTROL.count("nn2_net_get_status()"), 1)
        self.assertIn("nn2_net_get_status()", self.netdiag_block())

    def test_fallback_and_prefix_are_netdiag(self):
        app = (ROOT / "components/nn2_app/src/nn2_app_comm.c").read_text()
        self.assertIn('"v4netdiag=0 reason=unavailable"', app)
        self.assertIn("op == NN2_CMD_GET_TIMING ? 1u : 3u", app)
        self.assertIn('"v4netdiag=1 heap_free_now=', CONTROL)

    def test_boot_snapshot_and_existing_health_keys_are_still_present(self):
        for key in ("free_heap_before_wifi=", "free_heap_after_wifi=", "largest_free_block=",
                    "raw_packets_sent=", "raw_dropped_samples=", "raw_buffer_highwater=",
                    "udp_frames_dropped=", "adc_missed="):
            self.assertIn(key, CONTROL, key)
        self.assertIn('"NN2_HEAP point=%s free=%" PRIu32 " largest=%" PRIu32', CONTROL)

if __name__ == "__main__":
    unittest.main()
