# Persistent user setup: software validation, 2026-10-17

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
- The final setup suite passed 29 real temporary-filesystem tests. It uses a fake
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

The tests exercise real files and native compilation, but do not claim a live
systemd installation or a new headset picture from the persistent installer.
That installation and an actual future SteamVR update remain hardware checks.
Neither the version label nor these passing checks certifies another runtime,
arbitrary games or every future update. See [compatibility and recovery](../COMPATIBILITY.md).
