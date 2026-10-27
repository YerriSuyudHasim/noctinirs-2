"""Synthetic profiles only; never queries Windows or real credentials."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
from nn2_windows_wifi import parse_profile, NetworkUnavailable

FIXTURE = """<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
<SSIDConfig><SSID><hex>4e4e322d74657374</hex></SSID></SSIDConfig>
<MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>test-only-password</keyMaterial></sharedKey>
</security></MSM></WLANProfile>"""
class Profiles(unittest.TestCase):
    def test_personal(self):
        config=parse_profile(FIXTURE)
        self.assertEqual(config.ssid,b"NN2-test")
        self.assertNotIn(config.password,repr(config))
    def test_rejections(self):
        for old,new in [("WPA2PSK","WPA2"),("AES","TKIP"),("<protected>false","<protected>true"),("<useOneX>false","<useOneX>true"),("test-only-password","short"),("4e4e322d74657374","00")]:
            with self.subTest(old=old), self.assertRaises(NetworkUnavailable) as caught:
                parse_profile(FIXTURE.replace(old,new))
            self.assertNotIn("test-only-password",str(caught.exception))
    def test_malformed(self):
        for xml in ["bad", "<!DOCTYPE foo>"+FIXTURE, "x"*65537]:
            with self.assertRaises(NetworkUnavailable): parse_profile(xml)
if __name__=="__main__": unittest.main()
