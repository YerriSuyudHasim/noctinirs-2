"""Host parsing for V4 health/timing reports. No device and no ACQ1 claim."""
import contextlib
import io
from unittest import mock
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import nn2ctl


HEALTH = (
    "v4health=1 boot_stage=ready boot82_bound=0 adc_missed=3 adc_backlog_catchup=3 "
    "adc_missed_backlog=3 adc_ok=10 adc_missed_sequence=0 adc_sequence_gap=0 "
    "adc_missed_other=0 drdy_event_overflow=0 adc_ring_overflow=0 "
    "crc_bad=0 spi_errors=0 ads_status_faults=0 event_depth_max=4 "
    "raw_depth_max=1 deadline_depth_max=1 led_queue_depth_max=1 "
    "task_wdt_subscribe_errors=0 task_wdt_kick_errors=0 "
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

NETDIAG = (
    "v4netdiag=1 heap_free_now=60000 heap_free_min=41000 largest_free_block_now=na "
    "frame_send_errors=7 frame_send_eagain=3 frame_send_enobufs=2 frame_send_other=2 "
    "frame_last_errno=11"
)

def reply(opcode, text, session=0):
    payload = struct.pack("<IHH", 7, opcode, 0)
    body = bytes([1, 0, 0, 0]) + struct.pack("<QQ", 9, 11) + text.encode("ascii")
    return nn2ctl.decode(nn2ctl.encode_envelope(payload + body, msg_type=nn2ctl.REPLY,
                                                 session_id=session))


class DiagParse(unittest.TestCase):
    def test_fault_names_use_nn2_namespace_in_status_and_timing(self):
        for opcode, extra in ((3, struct.pack("<QQQIIIIIB", 1, 2, 3, 4, 5, 6, 7, 8, 0)),
                              (0x31, TIMING.encode("ascii"))):
            for code, name in ((0, "NONE"), (0x102, "FAULT_OFF"),
                               (0x10A, "FAULT_CLOCK"), (267, "FAULT_OPTICAL"),
                               (0xFFFF, "UNKNOWN_0xFFFF")):
                payload = struct.pack("<IHHBHBQQ", 7, opcode, 0, 5, code, 0, 9, 11) + extra
                packet = nn2ctl.decode(nn2ctl.encode_envelope(payload, msg_type=nn2ctl.REPLY))
                parsed = nn2ctl.describe_reply(packet, 7, opcode)
                self.assertEqual(parsed["fault"], code)
                self.assertEqual(parsed["fault_name"], name)

    def test_async_health_counters_are_preserved(self):
        fields = {"async_latency_min_us": 40, "async_latency_max_us": 180,
                  "async_latency_avg_us": 80, "async_on_pending_at_submit": 0,
                  "async_off_pending_at_submit": 0, "sample_clock_late_commits": 2,
                  "led_completion_timeout": 0}
        text = HEALTH + " " + " ".join(f"{key}={value}" for key, value in fields.items())
        parsed = nn2ctl.describe_reply(reply(0x30, text), 7, 0x30)
        for key, value in fields.items():
            self.assertEqual(parsed["diag"][key], str(value))

    def test_batch1_counters_in_health_and_timing(self):
        fields = {"sample_clock_early_commits": 2, "late_1": 10, "late_2": 3,
                  "late_3p": 4, "late_max": 9, "async_completion_errors": 0,
                  "async_submit_errors": 0, "async_stale_completion": 0,
                  "async_callback_core": -1, "async_led_task_core": 1,
                  "scheduler_task_core": 1, "udp_frames_sent": 6000,
                  "udp_frames_dropped": 0}
        for opcode, base in ((0x30, HEALTH), (0x31, TIMING)):
            text = base + " " + " ".join(f"{key}={value}" for key, value in fields.items())
            parsed = nn2ctl.describe_reply(reply(opcode, text), 7, opcode)
            for key, value in fields.items():
                self.assertEqual(parsed["diag"][key], str(value))

    def test_netdiag_keys_decode_and_na_is_not_a_number(self):
        parsed = nn2ctl.describe_reply(reply(0x32, NETDIAG), 7, 0x32)
        self.assertEqual(parsed["observation"],
                         {"heap_free_now": 60000, "heap_free_min": 41000,
                          "largest_free_block_now": None,
                          "frame_send_errors": 7, "frame_send_eagain": 3,
                          "frame_send_enobufs": 2, "frame_send_other": 2,
                          "frame_last_errno": 11, "frame_split_ok": True})

    def test_netdiag_split_mismatch_is_reported_not_hidden(self):
        text = NETDIAG.replace("frame_send_errors=7", "frame_send_errors=9").replace(
            "frame_last_errno=11", "frame_last_errno=-1")
        parsed = nn2ctl.describe_reply(reply(0x32, text), 7, 0x32)
        self.assertFalse(parsed["observation"]["frame_split_ok"])
        self.assertEqual(parsed["observation"]["frame_last_errno"], -1)

    def test_netdiag_fallback_and_malformed_keys_are_rejected(self):
        with self.assertRaises(ValueError):
            nn2ctl.parse_diag("v4netdiag=0 reason=unavailable")
        for bad in ("heap_free_now=12x", "largest_free_block_now=-1", "frame_last_errno=abc"):
            with self.assertRaises(ValueError):
                nn2ctl.parse_observation(nn2ctl.parse_diag(NETDIAG.replace(
                    "heap_free_now=60000", bad)))

    def test_health_and_timing_have_no_observation_keys(self):
        self.assertNotIn("observation", nn2ctl.describe_reply(reply(0x30, HEALTH), 7, 0x30))
        self.assertNotIn("observation", nn2ctl.describe_reply(reply(0x31, TIMING), 7, 0x31))

    def test_netdiag_command_is_registered(self):
        self.assertEqual(nn2ctl.COMMANDS["netdiag"], 0x32)

    def test_invalid_rate_is_rejected_before_client_creation(self):
        for rate in (0, 1, 3, 6, 7, 9, 11, 256):
            with self.subTest(rate=rate), mock.patch.object(sys, "argv",
                    ["nn2ctl", "run", "--rate", str(rate)]), mock.patch.object(nn2ctl, "Client") as client:
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    self.assertEqual(nn2ctl.main(), 1)
                self.assertIn("2,4,5,8,10", stderr.getvalue())
                client.assert_not_called()

    def test_allowed_rates_reach_set_fs_and_start_without_hardware(self):
        for rate in (2, 4, 5, 8, 10):
            with self.subTest(rate=rate), mock.patch.object(sys, "argv",
                    ["nn2ctl", "run", "--rate", str(rate), "--seconds", "0.001"]), mock.patch.object(nn2ctl, "Client") as factory:
                client = factory.return_value
                client.request.side_effect = lambda command, payload=None: dict(
                    error="OK", state="STOPPED" if command == "stop" else "STREAMING",
                    info="fs_allowed=2,4,5,8,10;frames=unsupported")
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(nn2ctl.main(), 0)
                client.request.assert_any_call("set-fs", bytes([rate]))
                client.request.assert_any_call("start")
                client.request.assert_any_call("stop")

    def test_firmware_allowlist_is_honored_and_legacy_info_works(self):
        for rate in (5, 10):
            nn2ctl.validate_rate(rate, "fs_min=2;fs_max=10")
        nn2ctl.validate_rate(5, "fs_allowed=2,5")
        with self.assertRaisesRegex(ValueError, "2,5"):
            nn2ctl.validate_rate(10, "fs_allowed=2,5")
        with mock.patch.object(sys, "argv", ["nn2ctl", "run", "--rate", "10"]), mock.patch.object(nn2ctl, "Client") as factory:
            factory.return_value.request.return_value = dict(error="OK", info="fs_allowed=2,5")
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(nn2ctl.main(), 1)
            self.assertEqual(factory.return_value.request.call_args_list, [mock.call("info")])

    def test_async_per_slot_durations_and_unknowns(self):
        text = HEALTH + " i2c_on_us=" + ",".join(["92"] * 20)
        text += " i2c_off_us=" + ",".join(["x"] + ["45"] * 19)
        parsed = nn2ctl.describe_reply(reply(0x30, text), 7, 0x30)
        self.assertEqual(len(parsed["async_i2c"]), 20)
        self.assertEqual(parsed["async_i2c"][19]["on_submit_complete_us"], 92)
        self.assertIsNone(parsed["async_i2c"][0]["off_submit_complete_us"])
        self.assertEqual(parsed["async_i2c"][1]["off_submit_complete_us"], 45)
        for invalid in ("1,2", ",".join(["-1"] * 20)):
            with self.assertRaises(ValueError):
                nn2ctl.parse_async_i2c({"i2c_on_us": invalid, "i2c_off_us": ",".join(["0"] * 20)})

    def test_health_and_timing_round_trip(self):
        health = nn2ctl.describe_reply(reply(0x30, HEALTH), 7, 0x30)
        timing = nn2ctl.describe_reply(reply(0x31, TIMING), 7, 0x31)
        self.assertEqual(health["diag"]["adc_missed_backlog"], "3")
        self.assertEqual(health["diag"]["event_depth_max"], "4")
        self.assertEqual(health["diag"]["frames"], "unsupported")
        self.assertEqual(health["diag"]["boot_stage"], "ready")
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
