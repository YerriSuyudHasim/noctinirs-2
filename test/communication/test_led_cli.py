"""Host CLI capability checks; no sockets or serial ports are opened."""
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import nn2ctl

class ClientModel:
    advisory = False
    limit_s = 0
    real = True
    calls = []
    closed = False
    def __init__(self, *_):
        self.session_id = 0
    def close(self):
        type(self).closed = True
    def request(self, name, *_):
        self.calls.append(name)
        reply = dict(error='OK', state='STOPPED')
        if name == 'info': reply['info'] = ('led=REAL_BRINGUP' if self.real else 'SIMULATED') + (f';short_policy=BENCH_WARN;duration=HOST;fs_min=2;fs_max=10;limit_s={self.limit_s};single_run=0' if self.advisory else '')
        if name == 'start':
            self.session_id = 7
            reply['state'] = 'STARTING'
        if name == 'heartbeat': reply['state'] = 'STREAMING'
        if name == 'stop': reply['state'] = 'STOPPING'
        if name == 'status' and self.session_id:
            self.session_id = 0
            reply['error'] = 'INVALID_STATE' # asynchronous teardown completed
        return reply

class LedCliTests(unittest.TestCase):
    def test_optical_fault_context_is_retained_in_error(self):
        with self.assertRaisesRegex(RuntimeError, 'state=FAULTED, fault=267'):
            nn2ctl.require_ok(dict(error='INVALID_STATE', state='FAULTED', fault=267))
    def run_cli(self, argv, real, clock=None):
        ClientModel.real, ClientModel.calls, ClientModel.closed = real, [], False
        with patch.object(nn2ctl, 'Client', ClientModel), \
                patch.object(sys, 'argv', ['nn2ctl', *argv]), \
                patch.object(nn2ctl.time, 'sleep'), \
                patch.object(nn2ctl.time, 'monotonic', side_effect=clock or [0,0,.1,.2,.3,.4]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = nn2ctl.main()
        self.assertTrue(ClientModel.closed)
        return result
    def test_advisory_requires_ack(self):
        ClientModel.advisory = True
        try:
            for args in (['start', '--led'],):
                self.assertEqual(self.run_cli(args, True), 1)
                self.assertEqual(ClientModel.calls, ['info'])
            self.assertEqual(self.run_cli(['start', '--led', '--ack-short-warning'], True), 0)
            self.assertEqual(ClientModel.calls, ['info', 'start'])
        finally:
            ClientModel.advisory = False
    def test_all_rates_2_through_10_are_accepted_before_start(self):
        ClientModel.advisory = True
        try:
            for rate in range(2, 11):
                with self.subTest(rate=rate):
                    self.assertEqual(self.run_cli(['start', '--led', '--ack-short-warning',
                                                  '--rate', str(rate)], True), 0)
                    self.assertEqual(ClientModel.calls, ['info', 'set-fs', 'start'])
        finally:
            ClientModel.advisory = False

    def test_outside_rate_range_is_rejected_before_network(self):
        for rate in ('-1', '0', '1', '11', '255', '256'):
            with self.subTest(rate=rate), patch.object(nn2ctl, 'Client') as client, \
                    patch.object(sys, 'argv', ['nn2ctl', 'set-fs', '--rate', rate]), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(nn2ctl.main(), 1)
                client.assert_not_called()

    def test_host_duration_accepts_15s_15min_and_more_than_one_day(self):
        ClientModel.advisory = True
        try:
            for seconds in (15, 900, 90000):
                with self.subTest(seconds=seconds):
                    clock = [0, 0] + [seconds + 1] * 4
                    self.assertEqual(self.run_cli(['run', '--led', '--ack-short-warning',
                        '--rate', '5', '--seconds', str(seconds)], True, clock), 0)
                    self.assertEqual(ClientModel.calls,
                        ['info', 'set-fs', 'start', 'heartbeat', 'stop', 'status', 'status'])
        finally:
            ClientModel.advisory = False

    def test_legacy_running_image_cap_is_reported_before_start(self):
        ClientModel.advisory, ClientModel.limit_s = True, 2
        try:
            self.assertEqual(self.run_cli(['run', '--led', '--ack-short-warning', '--seconds', '15'], True), 1)
            self.assertEqual(ClientModel.calls, ['info'])
        finally:
            ClientModel.advisory, ClientModel.limit_s = False, 0

    def test_invalid_duration_is_rejected_before_network(self):
        for seconds in ('0', '-1', 'nan', 'inf'):
            with self.subTest(seconds=seconds), patch.object(nn2ctl, 'Client') as client, \
                    patch.object(sys, 'argv', ['nn2ctl', 'run', '--led', '--seconds', seconds]), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(nn2ctl.main(), 1)
                client.assert_not_called()

    def test_start_prints_single_info_preflight_before_start(self):
        ClientModel.real, ClientModel.advisory, ClientModel.calls = True, False, []
        with patch.object(nn2ctl, 'Client', ClientModel), \
                patch.object(sys, 'argv', ['nn2ctl', 'start', '--led']), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(nn2ctl.main(), 0)
        import json
        lines = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(len(lines), 2)
        self.assertIn('led=REAL_BRINGUP', lines[0]['info'])
        self.assertEqual(lines[1]['state'], 'STARTING')
        self.assertEqual(ClientModel.calls, ['info', 'start'])
    def test_plain_start_rejects_real_profile_before_start(self):
        self.assertEqual(self.run_cli(['start'], True), 1)
        self.assertEqual(ClientModel.calls, ['info'])
    def test_led_option_rejects_simulation(self):
        self.assertEqual(self.run_cli(['start', '--led'], False), 1)
        self.assertEqual(ClientModel.calls, ['info'])
    def test_led_run_waits_for_asynchronous_stop(self):
        self.assertEqual(self.run_cli(['run', '--led', '--seconds', '.1'], True), 0)
        self.assertEqual(ClientModel.calls, ['info', 'start', 'heartbeat', 'stop', 'status', 'status'])
    def test_explicit_run_rate_is_configured_before_start(self):
        self.assertEqual(self.run_cli(['run', '--led', '--seconds', '.1', '--rate', '10'], True), 0)
        self.assertEqual(ClientModel.calls[:3], ['info', 'set-fs', 'start'])
    def test_rate_above_ten_is_rejected_before_network(self):
        # No client is constructed on this validation failure.
        with patch.object(nn2ctl, 'Client') as client, patch.object(sys, 'argv',
                ['nn2ctl', 'run', '--led', '--rate', '11']), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(nn2ctl.main(), 1)
            client.assert_not_called()

if __name__ == '__main__': unittest.main()
