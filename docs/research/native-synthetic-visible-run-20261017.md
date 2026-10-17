# First visible native synthetic passthrough — 2026-10-17

The maintainer confirms native camera visibility through the synthetic test
image in the actual GloomhavenVR PCVR Steam Link stream on Steam Frame. The
follow-up explicitly confirms intermediate transparency (test image and room
visible simultaneously) and immediate menu recovery in both directions. Both
eyes were requested in the visual check. No camera or game image was captured.
This is a user-observed end-to-end synthetic result, not real game-scene coverage
or optical calibration, and not an independently recorded native frame/eye ID.

## Exact scope

The current PC dev was confirmed by the maintainer as at least Build681, which
emits the named v2 pattern through the existing mod setting. A specific new PC
log/build banner was not collected in this round. Steam Link's PC sender remains
unchanged. Forkdev3762194 and exact-head CI39017279688 passed141 hosted checks,
existing optional builds, the new default-off functional build, Windows sender
and protocol sanitizers. Private focused evidence and source pins are retained.

The explicitly approved temporary native package totals390,636 bytes. Its ARM
receiver is226296 bytes, SHA256
`d3ef6e3c27bf89570884a1d7475e20e11008de1cc7a2f87c66f586c43fab6cc9`.
The private guarded fragment is18416 bytes, SHA256
`0aaf356d3789473cd32ff0d60cf5971bc1473813241657877db5c147bb1d4364`.
Capture/session nonce is248f41b81019661d1f9e3a4f42562dd5. The actual negotiated
Vulkan API is1.1. All73 runtime imports resolve; isolated loading uses the exact
mapped providers. The native public OpenVR API provider matches the reviewed
SHA256`a07163c436b3d791a0aa2442029d2ef0ba40a772ebf1dc6006d610c3a7934ee2`.

The cold status reports six original pipeline attempts and six replacements,
no definition-history failure and no refusal. The native camera setting is
acknowledgedTrue by both the public service and explicit user override. These
are bounded independent diagnostic atomics; they are not coherent source-frame
receipts. The visible result is the maintainer's report, not a status-file claim.
The guard recognizes current-image source format only, including its stated
copied-body/frozen-image limitations. No key-color replacement is involved.

## Result and remaining work

- Native streamed camera blending: visibly confirmed with the synthetic source.
- Intermediate transparency and immediate menu OFF/resume: visibly confirmed.
- Motion quality: not accepted. The maintainer reports lag while turning the head.
- Real Gloomhaven color/coverage and transparent materials: not connected yet.
- Final settings, installation and supported-version behavior: still development.

The new fragment evaluates twelve source-format samples plus four body samples
on the valid path, in addition to its unchanged original prefix. Owned Intel
measurements previously showed a significant extra cost (about3.27ms median at
5,308,416 pixels), so receiver GPU cost is a plausible contributor. That is not
a measurement of this Frame run or a proven cause. Native camera composition,
PC source generation/upload, transport/reprojection and actual native sampling
must remain separate hypotheses. No new diagnostic headset round is required
merely to acknowledge the reported lag; inspect and improve the known work first.

## Full manual restoration

At the maintainer's explicit request, the temporary camera key is removed and
SteamVR manually restarted once. Before restart, the public service acknowledges
the original unset/no-default state while file persistence is still pending;
this is correctly reported as incomplete. After restart both effective state
and explicit override are absent again, exactly matching the saved baseline.
The owned activation/config/enable are absent, run library removed, protected
mapping scan complete with no owned/foreign helper mapping, and normal native
startup passes. The maintainer explicitly confirms the visible interface is
normal again. Frame is safe for standby; no timer, active experiment or automatic
restart remains. Inactive package evidence and rollback receipts are preserved.

Private evidence is under
`build/private-native-presenter-runtime-20261017/`: source/CI/package pins,
actual-byte sandbox controls, first-visible result, activation/readonly inspection/
rollback records, independent reviews and cleanup. All owned worker trees and
regenerable test/build binaries/caches are removed. Current deliverables, captures
and small evidence are retained. The next device change requires a new concrete
approval after the next package is fully prepared.
