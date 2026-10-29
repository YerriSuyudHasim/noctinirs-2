"""Host parsing for V4 health/timing reports. No device and no ACQ1 claim."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import nn2ctl


HEALTH = (
    "v4health=1 boot82_bound=0 adc_missed=3 adc_backlog_catchup=3 "
    "adc_missed_backlog=3 adc_ok=10 adc_missed_sequence=0 adc_sequence_gap=0 "
    "adc_missed_other=0 drdy_event_overflow=0 adc_ring_overflow=0 "
    "crc_bad=0 spi_errors=0 ads_status_faults=0 event_depth_max=4 "
    "raw_depth_max=1 deadline_depth_max=1 led_queue_depth_max=1 "
    "frames=unsupported"
)
TIMING = (
    "v4timing=1 boot82_bound=0 adc_missed=3 adc_backlog_catchup=3 "
    "adc_missed_backlog=3 adc_sequence_gap=0 spi_min_us=32 spi_max_us=45 "
    "spi_avg_us=33 spi_lt40=8 spi_40_59=2 spi_60_79=0 spi_80_99=0 spi_ge100=0 "
    "service_min_us=50 service_max_us=70 service_avg_us=60 service_lt80=10 "
    "service_80_124=0 service_125_249=0 service_ge250=0 service_unknown=0 "
    "service_over_budget=0 lag_max_us=40 lag_lt40=9 lag_40_79=1 lag_80_124=0 "
    "lag_125_249=0 lag_ge250=0 sched_deferred_max_us=12 event_depth_max=4 "
    "ads_hwm_words=200 led_hwm_words=180 sched_hwm_words=300 "
    "optical_hwm_words=400 frame_hwm_words=250 dark_hwm_words=220 "
    "ctrl_hwm_words=500 net_hwm_words=800 cpu=not_measured"
)


def reply(opcode, text, session=0):
    payload = struct.pack("<IHH", 7, opcode, 0)
    body = bytes([1, 0, 0, 0]) + struct.pack("<QQ", 9, 11) + text.encode("ascii")
    return nn2ctl.decode(nn2ctl.encode_envelope(payload + body, msg_type=nn2ctl.REPLY,
                                                 session_id=session))


class DiagParse(unittest.TestCase):
    def test_health_and_timing_round_trip(self):
        health = nn2ctl.describe_reply(reply(0x30, HEALTH), 7, 0x30)
        timing = nn2ctl.describe_reply(reply(0x31, TIMING), 7, 0x31)
        self.assertEqual(health["diag"]["adc_missed_backlog"], "3")
        self.assertEqual(health["diag"]["event_depth_max"], "4")
        self.assertEqual(health["diag"]["frames"], "unsupported")
        self.assertEqual(timing["diag"]["spi_max_us"], "45")
        self.assertEqual(timing["diag"]["service_max_us"], "70")
        self.assertEqual(timing["diag"]["adc_backlog_catchup"], "3")
        self.assertEqual(timing["diag"]["boot82_bound"], "0")
        self.assertEqual(timing["diag"]["cpu"], "not_measured")
        self.assertEqual(timing["diag"]["ads_hwm_words"], "200")

    def test_status_layout_is_unchanged(self):
        extra = struct.pack("<QQQIIIIIB", 1, 2, 3, 4, 5, 6, 7, 8, 0)
        payload = struct.pack("<IHH", 1, 3, 0) + bytes(20) + extra
        packet = nn2ctl.decode(nn2ctl.encode_envelope(payload, msg_type=nn2ctl.REPLY))
        parsed = nn2ctl.describe_reply(packet, 1, 3)
        self.assertEqual(parsed["requests"], 4)
        self.assertFalse(parsed["grace_active"])

    def test_acq1_label_is_rejected(self):
        forged = HEALTH.replace("frames=unsupported", "frames=ACQ1")
        with self.assertRaises(ValueError):
            nn2ctl.parse_diag(forged)

    def test_missing_counter_is_rejected(self):
        with self.assertRaises(ValueError):
            nn2ctl.parse_diag("v4health=1 adc_ok=1 frames=unsupported")

    def test_info_text_is_still_plain_ascii(self):
        text = "baseline=V4_IDF61_NN2_CTRL;frames=unsupported"
        parsed = nn2ctl.describe_reply(reply(2, text), 7, 2)
        self.assertEqual(parsed["info"], text)
        self.assertNotIn("frames=ACQ1", parsed["info"])


if __name__ == "__main__":
    unittest.main()
