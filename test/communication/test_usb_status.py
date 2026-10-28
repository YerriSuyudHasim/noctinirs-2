"""Host-only serial-control regression; never opens a physical COM port."""
import contextlib
import io
import json
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import nn2ctl


class SerialModel:
    """Model the hazardous open transition, not USB electrical behavior."""
    def __init__(self, port=None, baudrate=9600, **settings):
        self.port, self.baudrate = port, baudrate
        self.settings = settings
        self.dtr = self.rts = True  # pySerial defaults
        self.is_open = False
        self.reset_events = self.close_calls = 0
        self.writes = []
        self.lines = [b'boot log\n', b'STATUS boot=4 battery_valid=1\n']
        self.open_error = None
        self.write_error = None
        if port is not None:
            self.open()

    def open(self):
        if self.dtr or self.rts:
            self.reset_events += 1
        if self.open_error:
            raise self.open_error
        self.is_open = True

    def close(self):
        self.close_calls += 1
        self.is_open = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def reset_input_buffer(self):
        pass

    def write(self, data):
        if self.write_error:
            raise self.write_error
        self.writes.append(data)

    def readline(self, _):
        return self.lines.pop(0) if self.lines else b''


class UsbStatusTests(unittest.TestCase):
    def test_cached_eflag_decode_uses_hardware_channels(self):
        line = ('STATUS led_ef_valid=FFC3FF led_open=800000 '
                'led_short=000001 led_ef_invalid=004000')
        result = json.loads(nn2ctl.describe_led_diagnostics(line).split(' ', 1)[1])
        self.assertEqual(result['open_pca_channels'], [23])
        self.assertEqual(result['short_pca_channels'], [0])
        self.assertEqual(result['invalid_code_pca_channels'], [14])
        self.assertNotIn(10, result['sampled_pca_channels'])
        self.assertEqual(len(result['sampled_pca_channels']), 20)

    def test_unknown_and_unobserved_eflag_data_is_not_reported_clean(self):
        self.assertIsNone(nn2ctl.describe_led_diagnostics('STATUS led_mode2=45'))
        line = 'STATUS led_ef_valid=000000 led_open=000001 led_short=0 led_ef_invalid=0'
        with self.assertRaises(ValueError): nn2ctl.describe_led_diagnostics(line)
        line = 'STATUS led_ef_valid=1000000 led_open=0 led_short=0 led_ef_invalid=0'
        with self.assertRaises(ValueError): nn2ctl.describe_led_diagnostics(line)

    def test_usb_prints_raw_status_and_decoded_snapshot(self):
        line = b'STATUS led_ef_valid=000001 led_open=0 led_short=1 led_ef_invalid=0\n'
        result = self.invoke(lambda port: setattr(port, 'lines', [line]))
        self.assertTrue(result.startswith(line.decode()))
        decoded = json.loads(result.splitlines()[1].split(' ', 1)[1])
        self.assertEqual(decoded['short_pca_channels'], [0])

    def invoke(self, configure=None, clock=None):
        ports = []

        def construct(*args, **kwargs):
            port = SerialModel(*args, **kwargs)
            ports.append(port)
            if configure:
                configure(port)
            return port

        self.ports = ports
        with patch.dict(sys.modules, {'serial': types.SimpleNamespace(Serial=construct)}), \
                patch.object(nn2ctl.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()) as output:
            if clock is None:
                nn2ctl.usb_status('COM11')
            else:
                with patch.object(nn2ctl.time, 'monotonic', side_effect=clock):
                    nn2ctl.usb_status('COM11')
        return output.getvalue()

    def test_repeated_status_does_not_request_reset_at_open(self):
        for _ in range(2):
            self.assertEqual(self.invoke(), 'STATUS boot=4 battery_valid=1\n')
            port = self.ports[0]
            self.assertEqual(port.reset_events, 0)
            self.assertEqual(port.port, 'COM11')
            self.assertEqual(port.baudrate, 115200)
            self.assertFalse(port.settings.get('rtscts', False))
            self.assertFalse(port.settings.get('dsrdtr', False))
            self.assertEqual(port.writes, [b'\n', b'STATUS\n'])
            self.assertEqual(port.close_calls, 1)
            self.assertFalse(port.is_open)

    def test_device_error_is_reported_and_port_closed(self):
        with self.assertRaisesRegex(RuntimeError, 'unsupported'):
            self.invoke(lambda port: setattr(port, 'lines', [b'ERR unsupported\n']))
        self.assertEqual(self.ports[0].close_calls, 1)

    def test_timeout_closes_port(self):
        with self.assertRaisesRegex(TimeoutError, 'no complete USB STATUS'):
            self.invoke(lambda port: setattr(port, 'lines', []), clock=[0, 0, 6])
        self.assertEqual(self.ports[0].close_calls, 1)

    def test_write_failure_closes_port(self):
        with self.assertRaisesRegex(OSError, 'write failed'):
            self.invoke(lambda port: setattr(port, 'write_error', OSError('write failed')))
        self.assertEqual(self.ports[0].close_calls, 1)

    def test_open_failure_closes_port(self):
        with self.assertRaisesRegex(OSError, 'open failed'):
            self.invoke(lambda port: setattr(port, 'open_error', OSError('open failed')))
        self.assertEqual(self.ports[0].close_calls, 1)


if __name__ == '__main__':
    unittest.main()
