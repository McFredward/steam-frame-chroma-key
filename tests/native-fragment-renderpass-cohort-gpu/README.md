# Owned render-pass cohort GPU proof

Explicit private original fragment bytes are required; no Valve code is embedded.
The fixture reuses existing read-only resource/validation helpers, then builds
independent two/three-subpass legacy and RP2 cohorts with multisample resolve,
input attachment, depth and a trailing preserve-only subpass. The target is
subpass1. Production capture and same-route owned views construct the compatible
observer pass. Indexed original/observer/immediate-original-restore draws must
produce bit-identical color and a128-byte record with intact canaries.

Run `python3 tests/native-fragment-renderpass-cohort-gpu/run.py --original <private-original.spv> --record <new-private-receipt.json>` with a current Vulkan validation layer available.
Both Intel and Lavapipe are required by default, with synchronization validation.
This is host offscreen evidence, not a replay of uncaptured native payloads,
full pinned native-vertex Clone factory, Frame execution, physical-eye/video-frame
association or passthrough authorization. Existing unrelated GPU cases are not run.
