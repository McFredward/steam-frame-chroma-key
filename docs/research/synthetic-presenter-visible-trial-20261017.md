# Prepared visible synthetic-alpha trial

This is a concrete next experiment, not demonstrated Steam Link passthrough or
real-game alpha. The Frame is currently restored; no library, camera setting,
service action or experiment is active. A fresh explicit authorization is needed
before the temporary native output replacement and camera-setting change below.

## Frozen software scope

The current GloomhavenVR dev source (Build681 or later) emits the named v2 pattern
through the existing VR setting. It is already integrated; normal gameplay and
the PC Steam sender are unchanged. The separately opted-in native receiver uses
only the pinned original shader pair and its same synchronous native pipeline
creation inputs. All original vertex/projection/descriptors/reprojection and
ordinary native commands stay under SteamVR. The default receiver build is off.

Each fragment reads the format guard from its current source image. A matching
v2 source produces premultiplied color plus the independently encoded numeric
coverage; other/invalid images preserve the exact original output. There is no
key-color decision, latest-mask queue join, timer or per-draw pipeline switch.
The twelve guard cells recognize format, not body integrity or liveness. A frozen
valid image cannot establish a fresh app pair. Real scene coverage is not exported.

The saved successful r7 native sampled image additionally preserves six known
neutral lower-strip codes0/64/128/191/255/192 exactly at54 independently selected
cell-interior samples. This supports the numeric source path without claiming
that the new twelve-cell guard has been observed on hardware. Independent owned
GPU tests cover the current-image OFF fallback and partial-alpha composition;
their measured additional cost is real and is not a Frame timing prediction.

## Device procedure after fresh approval

1. Require a current PC build, keep the existing synthetic source switch off,
   and verify the Frame's original runtime, imports and visible startup. Stage
   only the exact private ARM library, guarded module and bounded helpers in the
   user directory. Start SteamVR once to load that explicitly opted-in receiver.
   No stream is requested until current startup checks and the user's interface
   confirmation pass.
2. Save the native settings service's effective value and explicit user override
   for `camera/forceSceneAlphaBlending`, then enable that one key through public
   OpenVR003. No whole-settings rewrite, camera image read, Steam sender change or
   unrelated Steam/theater setting is involved. Start normal Steam Link PCVR and
   enable the GloomhavenVR source switch. Inspect both eyes for actual camera
   visibility behind zero coverage and intermediate blending behind partial
   coverage. Fully covered areas must stay opaque. Opening the mod menu must
   restore the ordinary game image; enabling again should repeat the result.
3. At the agreed end, disable the PC source, restore the exact prior native
   effective/explicit camera state and disable the owned receiver activation.
   The helper distinguishes runtime acknowledgment from pending persistence.
   Perform the separately agreed manual SteamVR restart, verify the original
   setting again, no remaining helper mappings/activation and normal visible UI,
   then remove only owned inactive library files. Evidence and rollback receipts
   remain. No timed rollback or automatic test-end restart exists.

The original shader's output alpha is1, so ordinary invalid-source rendering
remains opaque even while the temporary native camera-alpha key is enabled.
Removing an enable marker or settings key does not unload an existing pipeline;
manual restart and mapping checks are part of full restoration. Failed startup or
unsupported native format must be reported directly; technical health alone
never proves a visible interface or working transparency.

## Outcome boundaries

Success would prove visible synthetic native alpha through the actual unmodified
Steam Link transport, including partial transparency and manual menu recovery.
Only after that proof should real Gloomhaven scene coverage, its transparent
materials and ordinary settings/installation integration be connected. A camera
blend that never appears, bad eye mapping, a source-format refusal or unstable
startup is a failed visible experiment, even if a status file says a pipeline
was created. No additional metadata-only diagnosis is the planned user trial.
