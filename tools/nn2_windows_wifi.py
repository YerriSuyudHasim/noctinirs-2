"""Opt-in current-profile capture for the future host; never runs on import.
No shell, no profile enumeration/export, no elevation, and no credential logging.
Only the connected WPA2-PSK profile is eligible. Windows permissions are respected.
"""
from dataclasses import dataclass, field
import sys
import xml.etree.ElementTree as ET


class NetworkUnavailable(RuntimeError):
    pass


@dataclass(repr=False)
class PersonalNetwork:
    ssid: bytes
    password: str = field(repr=False)

    def __repr__(self):
        return "PersonalNetwork(<redacted>)"


def parse_profile(xml):
    """Parse only a Windows-returned profile; errors never contain profile content."""
    if len(xml) > 65536 or "<!DOCTYPE" in xml.upper() or "<!ENTITY" in xml.upper():
        raise NetworkUnavailable("Unsupported profile document")
    try:
        root = ET.fromstring(xml)
        ns = {"w": "http://www.microsoft.com/networking/WLAN/profile/v1"}
        def value(path):
            return root.findtext(path, namespaces=ns)
        security = "w:MSM/w:security/"
        if value(security + "w:authEncryption/w:authentication") != "WPA2PSK":
            raise ValueError()
        if value(security + "w:authEncryption/w:encryption") != "AES":
            raise ValueError()
        if value(security + "w:authEncryption/w:useOneX") not in (None, "false"):
            raise ValueError()
        if value(security + "w:sharedKey/w:protected") != "false":
            raise ValueError()
        if value(security + "w:sharedKey/w:keyType") != "passPhrase":
            raise ValueError()
        password = value(security + "w:sharedKey/w:keyMaterial")
        ssid_hex = value("w:SSIDConfig/w:SSID/w:hex")
        ssid = bytes.fromhex(ssid_hex) if ssid_hex else value("w:SSIDConfig/w:SSID/w:name").encode("utf-8")
        if not 1 <= len(ssid) <= 32 or b"\0" in ssid:
            raise ValueError()
        if not password or not 8 <= len(password) <= 63 or any(not 32 <= ord(c) <= 126 for c in password):
            raise ValueError()
        return PersonalNetwork(ssid, password)
    except (ET.ParseError, ValueError, AttributeError, TypeError):
        raise NetworkUnavailable("Current Wi-Fi profile is unsupported or its key is unavailable") from None


def capture_current_network():
    """Call only after user chooses Connect/pairing; never auto-run at app startup.

    Native API may deny access. No bypass or elevated retry is attempted. Returned
    Python strings cannot guarantee memory erasure: keep lifetime short and never
    serialize the returned object or include it in crash/error reports.
    """
    if sys.platform != "win32":
        raise NetworkUnavailable("Automatic profile capture requires Windows")
    import ctypes as c
    from ctypes import wintypes as w
    class GUID(c.Structure):
        _fields_ = [("data", c.c_ubyte * 16)]
    class Interface(c.Structure):
        _fields_ = [("guid", GUID), ("description", w.WCHAR * 256), ("state", w.DWORD)]
    class ConnectionPrefix(c.Structure):
        _fields_ = [("state", w.DWORD), ("mode", w.DWORD), ("profile", w.WCHAR * 256)]
    dll = c.WinDLL("wlanapi.dll")
    dll.WlanOpenHandle.argtypes = [w.DWORD, c.c_void_p, c.POINTER(w.DWORD), c.POINTER(w.HANDLE)]
    dll.WlanEnumInterfaces.argtypes = [w.HANDLE, c.c_void_p, c.POINTER(c.c_void_p)]
    dll.WlanQueryInterface.argtypes = [w.HANDLE,c.POINTER(GUID),w.DWORD,c.c_void_p,c.POINTER(w.DWORD),c.POINTER(c.c_void_p),c.POINTER(w.DWORD)]
    dll.WlanGetProfile.argtypes = [w.HANDLE,c.POINTER(GUID),w.LPCWSTR,c.c_void_p,c.POINTER(c.c_void_p),c.POINTER(w.DWORD),c.POINTER(w.DWORD)]
    dll.WlanFreeMemory.argtypes = [c.c_void_p]
    dll.WlanCloseHandle.argtypes = [w.HANDLE,c.c_void_p]
    for name in ("WlanOpenHandle","WlanEnumInterfaces","WlanQueryInterface","WlanGetProfile","WlanCloseHandle"):
        getattr(dll,name).restype = w.DWORD
    dll.WlanFreeMemory.restype = None
    handle=w.HANDLE(); version=w.DWORD(); interfaces=c.c_void_p(); connection=c.c_void_p(); profile=c.c_void_p()
    def checked(code):
        if code:
            raise NetworkUnavailable("Windows Wi-Fi access unavailable (code %d)" % code)
    checked(dll.WlanOpenHandle(2,None,c.byref(version),c.byref(handle)))
    try:
        checked(dll.WlanEnumInterfaces(handle,None,c.byref(interfaces)))
        count=c.cast(interfaces,c.POINTER(w.DWORD))[0]
        if count > 64:
            raise NetworkUnavailable("Unexpected Wi-Fi interface inventory")
        values=(Interface*count).from_address(interfaces.value+8)
        connected=[item for item in values if item.state==1]
        if len(connected)!=1:
            raise NetworkUnavailable("Exactly one connected Wi-Fi adapter is required")
        guid=connected[0].guid
        size=w.DWORD(); kind=w.DWORD()
        checked(dll.WlanQueryInterface(handle,c.byref(guid),7,None,c.byref(size),c.byref(connection),c.byref(kind)))
        if size.value<c.sizeof(ConnectionPrefix):
            raise NetworkUnavailable("Incomplete current connection")
        current=c.cast(connection,c.POINTER(ConnectionPrefix)).contents
        if current.state!=1 or not current.profile:
            raise NetworkUnavailable("Wi-Fi connection changed; retry")
        flags=w.DWORD(4); access=w.DWORD()
        checked(dll.WlanGetProfile(handle,c.byref(guid),current.profile,None,c.byref(profile),c.byref(flags),c.byref(access)))
        xml=c.wstring_at(profile)
        try:
            return parse_profile(xml)
        finally:
            c.memset(profile,0,(len(xml)+1)*c.sizeof(w.WCHAR))
            del xml
    finally:
        for pointer in (profile,connection,interfaces):
            if pointer: dll.WlanFreeMemory(pointer)
        dll.WlanCloseHandle(handle,None)
