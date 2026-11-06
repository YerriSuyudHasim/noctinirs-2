"""Static wiring checks for the immutable I2C-completion sample index."""
import unittest
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "firmware/v4-idf61/components"
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import verify_v4_runtime_placement as placement


def body(source, name):
    start = source.index("{", source.index(name + "("))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class CompletionClock(unittest.TestCase):
    def test_placement_guard_rejects_flash_and_missing_symbols(self):
        sections = {name: ".iram0.text" for name in placement.REQUIRED}
        placement.validate_sections(sections)
        sections["i2c_master_transmit"] = ".flash.text"
        with self.assertRaisesRegex(ValueError, "depends on flash"):
            placement.validate_sections(sections)
        del sections["i2c_master_transmit"]
        with self.assertRaisesRegex(ValueError, "missing runtime symbol"):
            placement.validate_sections(sections)

    def test_scheduler_edges_do_not_construct_a_diagnostic_status(self):
        source = (ROOT / "nocti_v4_scheduler/nocti_v4_scheduler.c").read_text()
        for function in ("process_deadline", "process_led_completion"):
            edge = body(source, function)
            self.assertNotIn("nocti_v4_async_led_get_status", edge)
            self.assertIn("nocti_v4_async_led_get_state", edge)

    def test_callback_freezes_clock_before_publishing(self):
        source = (ROOT / "nocti_v4_async_led/nocti_v4_async_led.c").read_text()
        callback = body(source, "i2c_done_callback")
        self.assertLess(callback.index("nocti_v4_sample_edge_observed"),
                        callback.index("completion_push_from_isr"))
        consume = body(source, "wait_for_completion")
        publish = body(source, "publish_scheduled_completion")
        for field in ("sample_sequence", "sample_generation", "sample_valid"):
            self.assertIn("runtime->last_" + field + " = completion." + field, consume)
            self.assertIn("completion." + field + " = runtime->last_" + field, publish)

    def test_scheduler_does_not_retimestamp_a_delayed_completion(self):
        source = (ROOT / "nocti_v4_scheduler/nocti_v4_scheduler.c").read_text()
        commit = body(source, "note_sample_commit")
        self.assertNotIn("nocti_v4_sample_edge_observed", commit)
        self.assertIn("completion->sample_sequence", commit)
        self.assertIn("completion->sample_generation != runtime->sample_generation", commit)

    def test_idf61_acquire_uses_supported_wait(self):
        source = (ROOT / "nocti_v4_ads/nocti_v4_ads.c").read_text()
        acquire = body(source, "transport_acquire")
        self.assertIn("transport->device, portMAX_DELAY", acquire)
        self.assertNotIn("Three seconds", acquire)


if __name__ == "__main__":
    unittest.main()
