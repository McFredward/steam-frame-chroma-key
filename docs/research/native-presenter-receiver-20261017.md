# Opt-in synthetic presenter receiver

This optional `FRAME_PIXEL_SYNTHETIC_PRESENTER` build is a functional receiver
prototype, not a real-game coverage implementation or a native passthrough
claim. It requires the definition journal and excludes the SSBO/uniform modes.
The default build remains unchanged.

Successful loader negotiation is the sole config/artifact preparation boundary.
Earlier GPA/create access remains inert and cannot lazily perform those reads.
The private session directory must contain an owned regular single-link 0600
`synthetic-presenter.enable` with `GHVR_SYNTHETIC_PRESENTER/1\n` and the exact
18,416-byte pinned `synthetic-presenter.spv`. Preparation copies and seals the
candidate once. Deleting the marker does not rewrite existing pipelines: the
same-image GPU format guard supplies the ordinary-image fallback. Full library
removal/restart belongs to the separate operator, not this module.

Only cold shader/layout/pass/pipeline creation and destruction are intercepted.
The actual successful original pipeline callback supplies its current owned
recipe, original CI and dependencies. The completion primitive preserves every
original pipeline CI member except the fragment module. Candidate compilation
happens only during original native pipeline creation, never on a draw. A
successful never-exposed original is retired from the journal before downstream
destruction, then its replacement is returned with ordinary application-owned
lifetime. Replacement handles are intentionally not registered as original
Valve shader definitions. Any admission or creation refusal preserves the
original batch result/handle; failed/partial native batches are left untouched.

There are no draw/submit/descriptor/resource hooks, SSBO objects or readbacks in
this mode. Hot GPA requests return the exact next-layer PFN. Original core/KHR
cold aliases retain their exact native entry. The actual negotiated API must
admit the candidate's SPIR-V 1.1 version. Count1 B9 layouts use only the original
PS's separately checked constant-index attestation; that CPU-only transform
creates no Vulkan SSBO interface.

A single prepared background writer writes at most 32 atomic status replacements
per process, including device recreation, each below 1 KiB. Creation callbacks
update bounded diagnostic atomics without file I/O. Status includes source
readiness, attempts/replacements, API and output refusal detail; those independent
atomics explicitly do not claim a coherent source/frame association. No camera,
Steam setting, service, Frame file or PC sender is changed here.

Focused evidence lives privately in
`build/private-native-presenter-receiver-worker-20261017/`. Public controls use
owned fake dispatch and private-file adversaries. Private controls additionally
use read-only pinned Valve VS/PS and the canonical guarded fragment, without
redistributing their bodies. They exercise actual exported Snapshot GPA/create/
destroy hooks, all matched views, full 29-binding count1 promotion, exact old
retirement and immediate handle reuse, unadmitted replacements, native/optional
partial failures, nested retirement-scope refusal, unsupported output gates,
observer history loss, and exhausted process-budget recreation.
`--presenter-status-json` emits the actual status producer for the independent
Python operator reader. These are host software controls, not a headset image,
real camera blend or game-derived coverage proof.
