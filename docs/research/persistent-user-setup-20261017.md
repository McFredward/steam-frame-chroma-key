# Persistent user setup: software and device validation, 2026-10-17

The public `setup.sh` now prepares and installs the scene receiver locally on
the Frame. This is distinct from the earlier temporary receiver used for the
[visible GloomhavenVR result](real-game-visible-run-20261017.md).

Read-only device inspection confirmed Frame-side **SteamVR 2.18.2,
build 1791331621, Linux ARM64** and the ten runtime-file identities in
[`packaging/compatibility.json`](../../packaging/compatibility.json). No install,
camera change or service restart was performed for this inspection.

## Checked scope

- The existing host CTest configuration passed all 144 tests. Its configuration
  preceded registration of the new preparation test, which was run separately.
- The final package-preparation suite passed 22 tests, including exclusive
  publication, changed runtime/source rejection and retained failure evidence.
- The original setup suite passed 29 real temporary-filesystem tests. It uses a fake
  runtime/service boundary: update refusal, fresh one-shot launches, abnormal-stop
  and reboot latches, all three camera baselines, foreign settings, partial
  upgrades, live cleanup, legacy collisions and cross-process emergency revocation.
- The release-only receiver compiled for both host and AArch64 with `-Werror`.
  Local transformation of the privately retained original shader reproduced the
  pinned 19,224-byte candidate and passed `spirv-val`. Those Valve-derived bytes
  are not part of the repository or a public download.
- Shell syntax, user-entry help, documentation links and current source-setting
  guidance were checked. Completed owned compiler and CMake caches were removed;
  bounded logs, source hashes and results were retained privately.

Subsequent bounded installer corrections passed all **42** focused Python setup
tests. These cover the Steam-created mode-0666 settings data, early preparation
before native `ExecStartPre`, and OpenVR's ordered config-path history and
environment priority. The final source checkpoint for the device installation
is `7ea427f92d51535c5577331fdf5469e573f0d18b`. Native receiver code was unchanged;
the earlier native checks were not repeated as a new full-suite pass.

## Public installation on the Frame

The earlier temporary receiver was removed and its camera override restored.
The public `main` checkout then built the release receiver locally and installed
it under `~/.local/share/SteamFrameNativePassthrough`. Repeating an unchanged
installation reused the checked package without another compilation.

Two SteamVR service starts loaded the public release package with distinct fresh
launch claims. Both reported a ready, pinned scene source with no definition
history failure; the second start also passed the bounded native UI startup
health gate. The first launch was recorded as a successful stop before the next
fresh launch. The installed package, enabled control, discovery link and service
drop-in remained present. No temporary debug activation or runtime service mask
remained.

The retained camera baseline was **no explicit override**, not a value borrowed
from the temporary installation. Only `camera/forceSceneAlphaBlending` was owned;
the second registered Steam config directory remained untouched.

Real-device inspection exposed three installer assumptions before this result:

- Steam creates writable settings data; it must not be mistaken for executable
  or private control input.
- Valve launches `vrstartup` from a native pre-command. An appended pre-command
  is too late, so the optional owned condition runs before native pre-commands.
- Multiple registered config paths are ordered history. Valve uses the first,
  unless `VR_CONFIG_PATH` overrides it; this is not an ambiguous target.

Steam also requested competing service starts during a manual maintenance stop.
The update therefore used an explicitly owned **runtime-only** mask while VR was
stopped, then removed it. One manual start job was canceled by that competition;
independent service, fresh claim and startup-health evidence established the
actual running state instead of treating the command's exit code as a verdict.
No automatic rollback timer was installed.

Private receipts are retained under `build/private-public-install-20261017/`,
including `public-active-first.json` and `public-normal-restart.json`. Compiler
caches and the completed worker checkout were removed; no capture was collected.

This establishes public-package installation and repeated **SteamVR service
starts**, not a full OS reboot, a new streamed-game picture, a future update or
arbitrary-game support. The previously confirmed visible game result remains
separate evidence. See [compatibility and recovery](../COMPATIBILITY.md).
