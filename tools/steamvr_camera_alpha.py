#!/usr/bin/env python3
"""Explicit, reversible camera alpha switch via the running native OpenVR API.

Not a layer installer, startup/restart tool, camera reader or image capture.
Only camera/forceSceneAlphaBlending may change. A receipt precedes the change;
SteamVR owns persistence. No JSON settings file is edited by this tool.
"""
import argparse
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys

import pixel_snapshot_session as files

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('owned_vrsettings', ROOT/'pc/vrsettings_win.py')
_vr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_vr)
SECTION, KEY = 'camera', 'forceSceneAlphaBlending'
SCHEMA = 'frame-camera-alpha-temporary-v1'
MAX_LIBRARY = 2*1024*1024


class NativeCamera(_vr.Session):
    """Same public IVRSettings_003 table as the existing PC helper, Linux ABI."""
    def __init__(self, library, sha256):
        self.library, self.sha256 = files.absolute(library), sha256
        self.fd = -1
        self.started = False

    def __enter__(self):
        if sys.platform != 'linux' or os.uname().machine not in ('aarch64', 'arm64'):
            raise files.SafetyError('native Frame AArch64 only; no PC SteamVR settings are changed')
        if not isinstance(self.sha256,str) or len(self.sha256)!=64 or any(c not in '0123456789abcdef' for c in self.sha256):
            raise files.SafetyError('explicit native OpenVR library SHA256 required')
        self.fd=os.open(self.library,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC|os.O_NONBLOCK)
        try:
            st=os.fstat(self.fd)
            import stat
            if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_uid not in (0,os.getuid()) or st.st_mode&0o022 or not 64<=st.st_size<=MAX_LIBRARY:
                raise files.SafetyError('bounded read-only native provider required')
            raw=os.read(self.fd,MAX_LIBRARY+1)
            if len(raw)!=st.st_size or files.digest(raw)!=self.sha256 or raw[:7]!=b'\x7fELF\x02\x01\x01' or struct.unpack_from('<H',raw,18)[0]!=183:
                raise files.SafetyError('actual native provider bytes or AArch64 identity differ')
            # Keep this verified inode open through the public API session.
            self.api=ctypes.CDLL('/proc/self/fd/'+str(self.fd))
            self.api.VR_InitInternal2.restype=ctypes.c_uint32
            self.api.VR_InitInternal2.argtypes=[_vr._ERR,ctypes.c_int,ctypes.c_char_p]
            self.api.VR_GetGenericInterface.restype=ctypes.c_void_p
            self.api.VR_GetGenericInterface.argtypes=[ctypes.c_char_p,_vr._ERR]
            self.api.VR_ShutdownInternal.restype=None
            self.api.VR_ShutdownInternal.argtypes=[]
            error=ctypes.c_int(0)
            self.api.VR_InitInternal2(ctypes.byref(error),3,None)  # Background never starts SteamVR.
            if error.value:raise files.SafetyError('background OpenVR initialization refused: '+str(error.value))
            self.started=True
            address=self.api.VR_GetGenericInterface(b'FnTable:IVRSettings_003',ctypes.byref(error))
            if not address or error.value:raise files.SafetyError('native IVRSettings_003 unavailable')
            self.fn=ctypes.cast(address,ctypes.POINTER(_vr._FnTable)).contents
            if not all(bool(getattr(self.fn,n)) for n in ('GetSettingsErrorNameFromEnum','GetBool','SetBool','RemoveKeyInSection')):
                raise files.SafetyError('incomplete native settings function table')
            return self
        except BaseException:
            self.__exit__(None,None,None)
            raise

    def __exit__(self,*unused):
        if self.started:
            self.api.VR_ShutdownInternal()
            self.started=False
        if self.fd>=0:
            os.close(self.fd)
            self.fd=-1

    def read(self):
        error=ctypes.c_int(0)
        value=self.fn.GetBool(SECTION.encode(),KEY.encode(),ctypes.byref(error))
        if error.value==5:return None  # Public UnsetSettingHasNoDefault, not IPC/parse/read failure.
        self._check(error,'read camera alpha')
        return bool(value)

    def write(self,value):
        if type(value) is not bool:raise files.SafetyError('exact boolean required')
        self.set_bool(SECTION,KEY,value)

    def remove_override(self):
        self.remove(SECTION,KEY)


def override(path):
    # Inspect explicit user override independently from effective runtime defaults.
    raw=files.read_file(files.absolute(path),MAX_LIBRARY)
    def pairs(items):
        result={}
        for k,v in items:
            if k in result:raise files.SafetyError('duplicate settings JSON key')
            result[k]=v
        return result
    data=json.loads(raw,object_pairs_hook=pairs,parse_constant=lambda _: (_ for _ in ()).throw(files.SafetyError('nonfinite settings JSON')))
    if type(data) is not dict or type(data.get(SECTION,{})) is not dict:
        raise files.SafetyError('ordinary native settings object required')
    camera=data.get(SECTION,{})
    value=camera.get(KEY)
    if KEY in camera and type(value) is not bool:raise files.SafetyError('nonboolean existing override preserved')
    return value


def receipt(path):
    value=json.loads(files.read_file(files.absolute(path),files.MAX_JSON,mode=0o600))
    if type(value) is not dict or set(value)!={'schema','library','library_sha256','settings_json','previous_override','previous_effective','section','key'} or value['schema']!=SCHEMA or value['section']!=SECTION or value['key']!=KEY:
        raise files.SafetyError('exact camera alpha receipt required')
    for field in ('previous_override','previous_effective'):
        if value[field] is not None and type(value[field]) is not bool:raise files.SafetyError('invalid previous camera state')
    files.absolute(value['library']);files.absolute(value['settings_json'])
    return value


def apply(camera, path, library, library_sha256, settings_json):
    path=files.absolute(path)
    fresh=not files.exists(path)
    if not fresh:
        previous=receipt(path)
        if previous['library']!=str(library) or previous['library_sha256']!=library_sha256 or previous['settings_json']!=str(settings_json):
            raise files.SafetyError('foreign camera receipt preserved')
    else:
        before, effective=override(settings_json),camera.read()
        if before is not None and effective is not before:raise files.SafetyError('native file/runtime state disagree before change')
        previous={'schema':SCHEMA,'library':str(library),'library_sha256':library_sha256,'settings_json':str(settings_json),
            'previous_override':before,'previous_effective':effective,'section':SECTION,'key':KEY}
        files.write_new(path,files.encoded(previous))  # Must exist BEFORE any settings write.
    current_override=override(settings_json)
    if current_override is not previous['previous_override'] and (previous['previous_effective'] is True or current_override is not True):
        raise files.SafetyError('foreign file override preserved')
    current=camera.read()
    if fresh and (current_override is not previous['previous_override'] or current is not previous['previous_effective']):
        raise files.SafetyError('baseline changed before first camera write; receipt retained')
    if current is not True:
        if current is not previous['previous_effective']:raise files.SafetyError('changed runtime setting preserved')
        camera.write(True)
    if camera.read() is not True:raise files.SafetyError('camera alpha write not acknowledged; receipt retained for restoration')
    return {'enabled':True,'automatic_restart':False,'receipt':str(path),'camera_images_read':False}


def restore(camera,path):
    previous=receipt(path)
    current_override=override(previous['settings_json'])
    if current_override is not previous['previous_override'] and (previous['previous_effective'] is True or current_override is not True):
        raise files.SafetyError('foreign file override preserved')
    current=camera.read()
    wanted=previous['previous_effective']
    if current is not True and current is not wanted:raise files.SafetyError('foreign runtime setting preserved')
    if current is wanted and current_override is previous['previous_override']:
        pass  # Already restored, or the original effectiveTrue required no write.
    elif previous['previous_override'] is None:
        # Remove only our one key; never rewrite SteamVR's whole settings file.
        camera.remove_override()
    else:
        camera.write(previous['previous_override'])
    after=camera.read()
    if after is not wanted:raise files.SafetyError('camera restoration not acknowledged; receipt retained')
    observed=override(previous['settings_json'])
    pending=observed is not previous['previous_override']
    # The public API acknowledges runtime state before SteamVR necessarily
    # persists it. Never describe an unchanged/stale JSON override as restored.
    return {'restored':not pending,'runtime_restored':True,'effective':after,
        'explicit_override_observed':observed,'expected_explicit_override':previous['previous_override'],
        'persistence_pending':pending,'automatic_restart':False,'camera_images_read':False}


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('read','enable','restore'))
    p.add_argument('--library',type=Path,required=True)
    p.add_argument('--library-sha256',required=True)
    p.add_argument('--settings-json',type=Path)
    p.add_argument('--receipt',type=Path)
    a=p.parse_args(argv)
    try:
        if a.command=='enable' and (a.settings_json is None or a.receipt is None):raise files.SafetyError('enable requires explicit settings path and fresh/owned receipt')
        if a.command=='restore':
            if a.receipt is None:raise files.SafetyError('restore requires original receipt')
            old=receipt(a.receipt)
            if old['library']!=str(a.library) or old['library_sha256']!=a.library_sha256:raise files.SafetyError('restore provider differs from receipt')
        with NativeCamera(a.library,a.library_sha256) as camera:
            if a.command=='read':result={'effective':camera.read(),'camera_images_read':False}
            elif a.command=='enable':result=apply(camera,a.receipt,a.library,a.library_sha256,a.settings_json)
            else:result=restore(camera,a.receipt)
        print(json.dumps(result,sort_keys=True));return 0
    except (OSError,ValueError,TypeError,KeyError,AttributeError,_vr.VRSettingsError) as error:
        print('native camera alpha: '+str(error),file=sys.stderr);return 2


if __name__=='__main__':
    raise SystemExit(main())
