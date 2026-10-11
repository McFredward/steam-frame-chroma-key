"""Minimal PC SteamVR settings access through SteamVR's own openvr_api.dll.

Uses the documented flat C interface (FnTable:IVRSettings_003) via ctypes, so
it needs no third-party packages.  Writes go to the running vrserver, which
applies them live and saves steamvr.vrsettings itself.
"""
import ctypes
import os

IVRSETTINGS = b"FnTable:IVRSettings_003"
VRApplication_Background = 3


def _steamvr_dir():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
            steam = winreg.QueryValueEx(k, "SteamPath")[0]
    except OSError:
        steam = r"C:\Program Files (x86)\Steam"
    return os.path.join(steam, "steamapps", "common", "SteamVR")


_FN = ctypes.WINFUNCTYPE if os.name == "nt" else ctypes.CFUNCTYPE
_ERR = ctypes.POINTER(ctypes.c_int)


class _FnTable(ctypes.Structure):
    _fields_ = [
        ("GetSettingsErrorNameFromEnum", _FN(ctypes.c_char_p, ctypes.c_int)),
        ("SetBool", _FN(None, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool, _ERR)),
        ("SetInt32", _FN(None, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int32, _ERR)),
        ("SetFloat", _FN(None, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_float, _ERR)),
        ("SetString", _FN(None, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, _ERR)),
        ("GetBool", _FN(ctypes.c_bool, ctypes.c_char_p, ctypes.c_char_p, _ERR)),
        ("GetInt32", _FN(ctypes.c_int32, ctypes.c_char_p, ctypes.c_char_p, _ERR)),
        ("GetFloat", _FN(ctypes.c_float, ctypes.c_char_p, ctypes.c_char_p, _ERR)),
        ("GetString", _FN(None, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, _ERR)),
        ("RemoveSection", _FN(None, ctypes.c_char_p, _ERR)),
        ("RemoveKeyInSection", _FN(None, ctypes.c_char_p, ctypes.c_char_p, _ERR)),
    ]


class VRSettingsError(Exception):
    pass


class Session:
    """with Session() as s: s.set_string("steamvr", "background", "#0000FF")"""

    def __enter__(self):
        dll = os.path.join(_steamvr_dir(), "bin", "win64", "openvr_api.dll")
        self.api = ctypes.CDLL(dll)
        self.api.VR_InitInternal2.restype = ctypes.c_uint32
        self.api.VR_InitInternal2.argtypes = [_ERR, ctypes.c_int, ctypes.c_char_p]
        self.api.VR_GetGenericInterface.restype = ctypes.c_void_p
        self.api.VR_GetGenericInterface.argtypes = [ctypes.c_char_p, _ERR]
        err = ctypes.c_int(0)
        self.api.VR_InitInternal2(ctypes.byref(err), VRApplication_Background, None)
        if err.value:
            raise VRSettingsError("VR_Init failed (%d) - is SteamVR running?" % err.value)
        p = self.api.VR_GetGenericInterface(IVRSETTINGS, ctypes.byref(err))
        if not p or err.value:
            self.api.VR_ShutdownInternal()
            raise VRSettingsError("no IVRSettings interface (%d)" % err.value)
        self.fn = ctypes.cast(p, ctypes.POINTER(_FnTable)).contents
        return self

    def __exit__(self, *a):
        self.api.VR_ShutdownInternal()

    def _check(self, err, what):
        if err.value:
            name = self.fn.GetSettingsErrorNameFromEnum(err.value)
            raise VRSettingsError("%s: %s" % (what, name.decode() if name else err.value))

    def get_string(self, section, key):
        buf = ctypes.create_string_buffer(4096)
        err = ctypes.c_int(0)
        self.fn.GetString(section.encode(), key.encode(), buf, len(buf), ctypes.byref(err))
        if err.value:
            return None   # unset / no default
        return buf.value.decode()

    def get_bool(self, section, key):
        err = ctypes.c_int(0)
        v = self.fn.GetBool(section.encode(), key.encode(), ctypes.byref(err))
        return None if err.value else bool(v)

    def set_string(self, section, key, value):
        err = ctypes.c_int(0)
        self.fn.SetString(section.encode(), key.encode(), value.encode(), ctypes.byref(err))
        self._check(err, "set %s/%s" % (section, key))

    def set_bool(self, section, key, value):
        err = ctypes.c_int(0)
        self.fn.SetBool(section.encode(), key.encode(), bool(value), ctypes.byref(err))
        self._check(err, "set %s/%s" % (section, key))

    def remove(self, section, key):
        err = ctypes.c_int(0)
        self.fn.RemoveKeyInSection(section.encode(), key.encode(), ctypes.byref(err))
        self._check(err, "remove %s/%s" % (section, key))
