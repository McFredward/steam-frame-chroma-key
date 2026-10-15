# First native pixel snapshot attempt, 2026-10-15

## Observed result

The maintainer explicitly approved the temporary user-directory package, one
SteamVR restart and acquisition of a received test image. The original PC
GloomhavenVR build676 source diagnostic remained the input. No native PC Steam
sender hook, shader replacement or passthrough activation was added.

The exact public Vulkan layer (`36b35acc3b5e1d0d5c6082f0a19b22debc88420c62f389dcf1a4a9a82ee745be`,
227,256 bytes) loaded on the actual Frame. All required imports were checked
against its currently mapped providers, and an isolated `RTLD_NOW` load using
the exact mapped C++/GCC providers succeeded. The approved native restart passed
the existing native UI health gate. The one-shot launch claim was consumed and
the background writer reported `prepared`.

The maintainer then confirmed that the streamed test pattern was visible.
Before arming, the writer had already reported `tracking_refused`. The arming
orchestrator refused to create `capture.arm`. No raw image or image metadata
file was produced. The layer's image-copy admission requires that arm, so this
attempt did not issue the diagnostic image copy. The image budget was allocated
cold and released with device teardown.

The first refusal occurred between the initial prepared/readiness observation
and the pre-arm check. The original status record has **no failure code, API
site, Core reason counters or timestamp**. The exact cause and trigger cannot
be recovered from it. The briefly open Steam menu is context, not an established
cause. Closing it cannot reset an intentionally sticky tracker refusal.

## Cold staging repair and safe restoration

Initial staging stopped before publishing config/manifest because normal
SteamOS process protection denies same-user maps for systemd, sd-pam, Gamescope
and the SSH session. The explicit, tested `--read-protected-maps` fallback now
reads only those denied numeric same-UID maps through a bounded `sudo -n head`.
Credentials remained transient in the orchestration process and were not placed
in helper files. With the approved read-only administrative check, the complete
scan passed and the package was staged unarmed. No protected process was skipped.

After the failed acquisition, the maintainer separately approved manual
restoration. Exact activation config/manifest were removed, SteamVR was manually
restarted, and the native UI health gate returned `ready`. The complete protected
map scan then found no owned or foreign diagnostic mapping; the owned run library
was deleted. Rollback reported complete with no errors. Raw and metadata image
files were absent. Small inactive package/evidence files remain in the recorded
private user directory; no automatic rollback, restart or expiry was introduced.
The maintainer was told that the Frame could be put into standby.

## Completed local follow-up

The new local package adds only bounded first-refusal telemetry. The first
winning failure enum and original API/method site are retained; the existing
atomic Core reason/table-capacity counters are sampled and the initialized
immutable report is published with release/acquire ordering. One bounded
background status record is emitted. Admission, lifetime, concurrency and refusal
rules remain unchanged. No guard is weakened based on an unobserved hypothesis.

Counter samples on a concurrent refusal are diagnostics, not a transactional Core
dump. An observed `selected=false` alone is not proof that GPU recording never
started; the no-copy claim above is specific to this attempt's never-created arm.
Real native failure data is still required before choosing a causal repair.

The revised tracker passes 1103 controls normally and under ASan/UBSan. Six
production formatter tests cover actual Core refusal, first-failure retention,
all categories, JSON safety and output boundaries. The final production layer
passes the 14 real Vulkan-loader cases after integration. Unchanged image-copy
GPU and original source-output evidence is inherited, not repeated. Independent
source review has no open blocker. The strict ARM64 build has three private
entry exports and no TLS; new SHA-256:
`59cd2f688e311b32b39dc6ec10d82bf81b0f7db57090c49daa5bfeb1298023e0`
(228,680 bytes). It has not been installed on the Frame.

Current local package and review evidence:
`build/private-refusal-prep-20261015/`. This is focused composed validation,
not another complete mod gate or a repair of the still-unidentified native cause.

## Retained private evidence

`build/private-pixel-run-20261015/` contains the read-only provider preflight,
protected map/auth checks, initial refusal, exact unarmed stage, independent
protected-reader review, native activation health, pre-arm refusal and complete
rollback/health results. Capture nonce: `f45ebb2060b6cfd6ea47476e9dc4536e`.
Protected-reader commit `529e2ff` passes the full fork CI run `38808501342`;
its 31 local session tests remain separate from the native UI/rollback evidence.

This establishes native layer startup and safe restoration, not decoded pixel
carriage or working streamed passthrough. A fresh device/restart agreement is
required for the revised diagnostic.
