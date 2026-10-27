"""Host-only serial-control regression; never opens a physical COM port."""
import contextlib
import io
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
