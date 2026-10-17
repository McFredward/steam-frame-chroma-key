# Reversible native camera alpha setting for the synthetic trial

`tools/steamvr_camera_alpha.py` controls only
`camera/forceSceneAlphaBlending` through the running **native Frame** OpenVR
settings service. It does not install/enable a Vulkan layer, start/stop a service,
open a camera, acquire camera images or enable the PC source. Its existence is
not permission to run it on the maintainer's Frame. The visible trial still needs
fresh concrete approval for this temporary setting change and the native output
replacement, after the actual package and rollback are checked.

The helper reuses the existing PC settings helper's public `IVRSettings_003`
function table, with an explicit verified native library instead of a Windows
SteamVR path. Production loading requires Linux AArch64, a caller-selected exact
library SHA256, ordinary read-only bounded ELF bytes and machine183. The same
verified inode stays open across loading. The public Background application type3
is used; it must not start SteamVR or take scene focus. There is no shader/image
or private Steam memory interface involved. Primary interface definitions:
[OpenVR public C ABI](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr_capi.h)
and [settings/background contract](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h).
The reviewed public header SHA256 is
`1e6ed57199896cc1f7c5484e50fa18955e97be15be690beb28d998c877ead7fd`.
A new runtime must still supply the named versioned interface; physical-device
or SDK-version guesses are not admission.

Before the first write, `enable` creates an exclusive owned0600 receipt recording
the explicit previous user override and effective running value. The user JSON
file is read independently to distinguish an absent override from a default;
it is never rewritten. Only the exact public unset/no-default error5 may mean
an unknown prior default. IPC, parse, read, write or access failures are errors,
not false/absent values. The setter is acknowledged with a new public getter.
An existing owned receipt can resume an interrupted change without losing its
original baseline. Foreign/mismatched providers, receipts, setting types or
incompatible current values refuse rather than overwrite the changed state.

`restore` removes the one override if it was absent originally, or writes its
previous exact boolean. The effective value must match the recorded baseline. The helper then rereads
the explicit JSON override: asynchronous persistence is reported separately as
`persistence_pending`, and `restored` stays false until both observations match.
A mismatch retains the receipt and reports incomplete restoration. Already
restored values require no new change. All other user settings remain under
SteamVR's persistence. The operator must also disable/remove the temporary
receiver layer, perform the separately agreed manual SteamVR restart and verify
normal startup/UI. Restoring this one setting alone does not unload a Vulkan
pipeline or prove the interface is visible. There is no automatic timer/restart.

Example **operator commands, not a player installation flow**:

```sh
python3 -B tools/steamvr_camera_alpha.py read \
  --library /actual/native/libopenvr_api.so --library-sha256 ACTUAL_SHA256
python3 -B tools/steamvr_camera_alpha.py enable \
  --library /actual/native/libopenvr_api.so --library-sha256 ACTUAL_SHA256 \
  --settings-json /actual/native/steamvr.vrsettings --receipt /private/run/camera-before.json
python3 -B tools/steamvr_camera_alpha.py restore \
  --library /actual/native/libopenvr_api.so --library-sha256 ACTUAL_SHA256 \
  --receipt /private/run/camera-before.json
```

The package must include the unchanged `pc/vrsettings_win.py` dependency at its
expected relative path. The helper is independently useful only in a running
native settings session; a PC OpenVR handle would modify the wrong runtime and
is deliberately refused by production loading.

Ten initial focused sandbox methods passed. A subsequent independent review
found that an absent current file override was incorrectly exempted from foreign-
change checks, and that effective API acknowledgment did not prove explicit-file
restoration. Both are corrected. Four affected/new methods pass after that bounded
repair; unchanged initial methods retain their earlier evidence. An effectiveTrue
baseline requires no write and never authorizes adoption of a later explicitTrue
override. One additional new method passes both file/runtime changes between
receipt creation and the first API write; neither changed baseline is adopted.
The composed scope is13 methods, not a fresh complete run. Coverage includes: true/false/absent baselines, unknown defaults,
write-before-receipt refusal, acknowledgement failure/recovery, repeat-enable,
foreign receipts/current changes, duplicate/wrong-type settings, symlink
preservation and actual ctypes scalar callback dispatch for the public table.
Only exact unset5 is treated specially. The x86 host refuses native loading
before dlopen. These are **not** an actual Frame/OpenVR session or camera-picture
proof. Private source pins/results are retained under
`build/private-native-camera-alpha-20261017/`; no device action occurred.

The persistence observation is not a durability guarantee across a restart. The
operator must check the recorded effective/explicit baseline again after the
separately approved original-runtime restart before claiming device restoration.
No file polling deadline, automatic retry or full-settings rewrite is introduced.
