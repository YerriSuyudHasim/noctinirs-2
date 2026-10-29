"""Version-2 V4 optical time words. Not an ACQ1 claim and not a device test."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import nn2ctl


def timing_reply(text):
    payload = struct.pack("<IHH", 7, 0x31, 0) + bytes([1, 0, 0, 0]) + struct.pack("<QQ", 9, 11)
    payload += text.encode("ascii")
    return nn2ctl.decode(nn2ctl.encode_envelope(payload, msg_type=nn2ctl.REPLY))


class V4SampleTime(unittest.TestCase):
    def test_measured_anchor_is_little_endian(self):
        payload = bytearray(1184)
        payload[0] = 2
        struct.pack_into("<Q", payload, 16, 0x0807060504030201)
        struct.pack_into("<II", payload, 1176, 0xFFFFFFF0, 8000)
        parsed = nn2ctl.parse_v4_optical_time(payload)
        self.assertEqual(parsed["anchor_us"], 0x0807060504030201)
        self.assertEqual(parsed["first_sample_sequence"], 0xFFFFFFF0)
        self.assertEqual(parsed["adc_fs_hz"], 8000)
        self.assertEqual(payload[16], 0x01)
        self.assertNotEqual(payload[16], 0x08)

    def test_version_one_and_wrong_rate_are_rejected(self):
        payload = bytearray(1184)
        payload[0] = 1
        with self.assertRaises(ValueError):
            nn2ctl.parse_v4_optical_time(payload)
        payload[0] = 2
        struct.pack_into("<II", payload, 1176, 1, 4000)
        with self.assertRaises(ValueError):
            nn2ctl.parse_v4_optical_time(payload)

    def test_timing_report_expands_planned_and_actual_edges(self):
        on = ",".join(["0"] * 19 + ["2"])
        off = ",".join(["x"] + ["0"] * 19)
        text = (
            "v4timing=1 boot82_bound=0 adc_missed=1 adc_backlog_catchup=3 "
            "adc_missed_backlog=3 adc_sequence_gap=0 spi_min_us=32 spi_max_us=45 "
            "spi_avg_us=33 spi_lt40=8 spi_ge100=0 service_min_us=50 service_max_us=70 "
            "service_avg_us=60 service_over_budget=0 lag_max_us=40 lag_ge250=0 "
            "event_depth_max=4 ads_hwm_words=200 led_hwm_words=180 sched_hwm_words=300 "
            "cpu=not_measured fs=8000 anchor=1000 frame_delta=800 slot_samples=40 "
            "off_samples=18 missed=1 catchup=3 gap=0 edge_miss=0 on_d=%s off_d=%s"
        ) % (on, off)
        parsed = nn2ctl.describe_reply(timing_reply(text), 7, 0x31)
        sync = parsed["sync"]
        self.assertEqual(sync["fs"], 8000)
        self.assertEqual(sync["anchor"], 1000)
        self.assertEqual(sync["frame_delta"], 800)
        self.assertEqual(sync["missed"], 1)
        self.assertEqual(sync["catchup"], 3)
        self.assertEqual(sync["gap"], 0)
        self.assertEqual(sync["slots"][0]["on_planned"], 1000)
        self.assertEqual(sync["slots"][0]["on_actual"], 1000)
        self.assertEqual(sync["slots"][19]["on_planned"], 1000 + 19 * 40)
        self.assertEqual(sync["slots"][19]["on_actual"], 1000 + 19 * 40 + 2)
        self.assertFalse(sync["slots"][0]["off_known"])
        self.assertEqual(sync["slots"][1]["off_planned"], 1000 + 40 + 18)
        self.assertEqual(sync["slots"][1]["off_actual"], 1000 + 40 + 18)

    def test_health_still_refuses_an_acq1_label(self):
        text = (
            "v4health=1 boot_stage=ready boot82_bound=0 adc_missed=0 "
            "adc_backlog_catchup=0 adc_missed_backlog=0 adc_ok=1 adc_sequence_gap=0 "
            "crc_bad=0 spi_errors=0 event_depth_max=1 task_wdt_subscribe_errors=0 "
            "task_wdt_kick_errors=0 frames=ACQ1"
        )
        with self.assertRaises(ValueError):
            nn2ctl.parse_diag(text)


if __name__ == "__main__":
    unittest.main()
