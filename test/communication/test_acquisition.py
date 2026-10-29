import importlib.util
from pathlib import Path
import struct
import unittest
spec=importlib.util.spec_from_file_location("acq_nn2ctl",Path(__file__).resolve().parents[2]/"tools/nn2ctl.py")
import sys
ctl=importlib.util.module_from_spec(spec);sys.modules[spec.name]=ctl;spec.loader.exec_module(ctl)
class AcquisitionFrames(unittest.TestCase):
    def packet(self,quality=1,n_on=8,n_off=8):
        p=bytearray(1156);p[:4]=b"ACQ1"
        struct.pack_into("<IIQ",p,4,1,7,100000)
        p[20:24]=bytes([20,4,10,8])
        for s in range(20):
            q=36+s*56
            struct.pack_into("<BBBBI",p,q,s,n_on,n_off,quality,0)
            struct.pack_into("<4i4i",p,q+8,101,102,103,104,40,41,42,43)
        return ctl.decode(ctl.encode_envelope(p,msg_type=ctl.FRAME,flags=ctl.TS_ESTIMATED,session_id=9))
    def test_exact_fractional_dark_subtraction(self):
        f=ctl.describe_frame(self.packet());s=f['slots'][0]
        self.assertTrue(s['valid']);self.assertEqual(s['on_sum_counts'][0],101)
        self.assertEqual(s['corrected_counts'][0],61/8)
        self.assertEqual(s['on_counts'][0],101/8)
        self.assertTrue(f['timestamp_estimated']);self.assertFalse(f['simulated'])
    def test_short_unequal_pair_retains_raw_but_no_correction(self):
        s=ctl.describe_frame(self.packet(quality=2,n_off=7))['slots'][0]
        self.assertFalse(s['valid']);self.assertIsNone(s['corrected_counts'])
        self.assertEqual(s['off_sum_counts'][0],40)
    def test_warmup_and_unknown_layout(self):
        s=ctl.describe_frame(self.packet(quality=4))['slots'][0]
        self.assertFalse(s['valid'])
        packet=self.packet();bad=ctl.Envelope(packet.msg_type,1,2,9,1,0,b"OLD!"+packet.payload[4:])
        with self.assertRaises(ValueError):ctl.describe_frame(bad)
