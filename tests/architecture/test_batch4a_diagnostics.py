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


class HealthAndTimingKeys(unittest.TestCase):
    def test_heap_keys_are_health_only_and_read_without_isr_or_adc_task(self):
        block = CONTROL[CONTROL.index("heap_mask = MALLOC_CAP_INTERNAL"):]
        block = block[:block.index("if (wrote > 0 && (size_t)wrote < cap && kind == 1u)")]
        self.assertIn("if (wrote > 0 && (size_t)wrote < cap && kind == 0u) {\n        /* D2 observation",
                      CONTROL)
        self.assertIn("heap_caps_get_free_size(heap_mask)", block)
        self.assertIn("heap_caps_get_minimum_free_size(heap_mask)", block)
        self.assertIn('snprintf(largest_now, sizeof(largest_now), "na");', block)

    def test_largest_free_block_walk_is_gated_to_idle_only(self):
        block = CONTROL[CONTROL.index("heap_mask = MALLOC_CAP_INTERNAL"):]
        gate = block.index("control_state(NULL) == NN2_OPTICAL_IDLE")
        walk = block.index("heap_caps_get_largest_free_block(heap_mask)")
        self.assertLess(gate, walk)
        self.assertEqual(len(re.findall(r"heap_caps_get_largest_free_block\(", CONTROL)), 2,
                         "only the boot snapshot and the gated health read may call it")

    def test_frame_keys_are_timing_only_and_net_status_is_read_in_net_task_path(self):
        timing = CONTROL[CONTROL.index("kind == 1u) {\n        /* D1 observation"):]
        timing = timing[:timing.index("if (wrote < 0 || (size_t)wrote >= cap) return 0u;")]
        self.assertIn("frame_send_errors=", timing)
        self.assertIn("frame_last_errno=%d", timing)
        self.assertNotIn("frame_send_errors", CONTROL[:CONTROL.index("kind == 1u) {\n        /* D1 observation")])
        self.assertEqual(CONTROL.count("nn2_net_get_status()"), 1)

    def test_existing_health_keys_and_boot_snapshot_are_still_present(self):
        for key in ("free_heap_before_wifi=", "free_heap_after_wifi=", "largest_free_block=",
                    "raw_packets_sent=", "raw_dropped_samples=", "raw_buffer_highwater=",
                    "udp_frames_dropped=", "adc_missed="):
            self.assertIn(key, CONTROL, key)
        self.assertIn('"NN2_HEAP point=%s free=%" PRIu32 " largest=%" PRIu32', CONTROL)


if __name__ == "__main__":
    unittest.main()
