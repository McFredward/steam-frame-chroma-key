# Compatible Set0 mirror and exact Valve subpass semantics

Status: offline implementation and focused checks pass. Native streaming
passthrough is **not** implemented or demonstrated by this result. The new
private subpass adapter is a separate, unfinished integration.

## State restoration

The prior observer added Set1 and restored only the original pipeline. An empty
modeled higher set was not evidence that native Vulkan had never bound it: the
ordinary tracker clears more state on layout changes than Vulkan compatibility
requires. Refusing a later draw could prevent publication but could not undo a
descriptor disturbance already introduced by the observer.

The receiver now uses a separate, exactly checked mirror profile:

- Independently pin both original shader modules and their complete descriptor
  interfaces. Neither uses the original storage binding8.
- Keep one complete, identical Set0 layout and the original push ranges. Use
  binding8 only in the owned set, never write the native descriptor set.
- Populate the seven attested original bindings once before the first owned
  descriptor bind. Hold the buffer lease separately from that immutable-set seal.
- Require a fresh opaque target proof after copying. Restore the exact original
  pipeline, descriptor set and every admitted sorted dynamic offset immediately
  after the observed draw, including on a publication refusal.
- Preserve ordinary later draws and passes. Refuse uncopied dynamic descriptors
  before Vulkan creation rather than assume undefined unused bindings are safe.

The additional-Set1 profile remains a distinct offline control and cannot arm
the receiver. The ordinary tracker's clearing policy was not weakened.

## Validation scope

Fresh merged checks: eight affected CTests and strict host/ARM optional snapshot
builds. The earlier unchanged clone-contract pass is inherited. Focused private
normal and sanitizer controls cover the profile, owner, exact restore ticket,
transaction, bridge and actual Snapshot translation unit. Their distinct
assertion counts and inherited portions are retained in the private manifests;
this is not a new full main-repository/wire gate.

The new owned GPU oracle passes 36 cases and two legal causal mutations on both
Intel and the software ICD, 3981 assertions per device, with zero Khronos or
synchronization-validation errors. Control/probe/restored colors are bit-exact;
both higher sets, nonzero dynamic offsets and native storage survive. Omitting
the restore or using wrong offsets produces the exact expected wrong result,
followed by recovery on a later original-style rebind.

This GPU test uses an owned raster vertex and finite eight-binding layout, not
the full native29 census. It checks the call pattern, not production Clone or
Transaction execution. Its draws span separate compatible render-pass instances;
same-pass production callback order has separate fixture evidence. The original
array3-versus-count1 color proof is inherited. No frame, eye, UV or native
passthrough authority follows from any of these tests.

Private evidence: `build/private-native-r6-integration-20261017/`,
`build/private-native-set0-mirror-gpu-r6-20261017/`,
`build/private-set0-mirror-profile-r6-20261017/`, and
`build/private-native-subpass-path-r6-20261017/`. The first GPU fixture attempt
correctly failed validation because its pipeline used an incompatible earlier
render-pass definition; pipeline creation was moved after the final definition.
That failed receipt remains preserved. It is not a diagnosed native defect.

## Exact private header found offline

After the successful manual rollback and the maintainer's original-interface
confirmation, one read-only acquisition saved the actual mapped Vulkan ICD.
No remote file was written and no service or stream was started or stopped.
The maintainer was immediately told standby was safe.

The acquired 18,850,240-byte driver has SHA256
`dfed0652b592b263a400ba0a64fbdd421870a938abed4d447a9aafa6a4ad789a`
and GNU ID `b7e5995d0b202f06e81cd5a0f809ec1b744a0cb8`.
Its embedded Mesa commit resolves to Valve's exact downstream source
`abc426bb62351045a9dcf7fa0a113e6dd810c303`, recovered from Valve's official
SteamOS source package. The private source declaration, acquired executable
and original compositor writer agree:

- Type1000109099 is a Valve subpass-begin structure containing `sType`, `pNext`
  and a 32-bit sRGB-control mask.
- Begin2 and Next2 set or clear the per-subpass override; pass reset clears it.
- The only consumer substitutes that mask for the ordinary attachment-derived
  mask emitted to the RB/SP sRGB-control registers. The compositor supplies zero.

It is therefore not an ignored header. Stripping it can alter native color
conversion. Public upstream Mesa's different behavior was not evidence about
this Valve build, and a numeric/default-zero allowlist would have been wrong.

Still required: attest the **actual saved downstream PFNs** and the exact loaded
provider for the observed device. The ICD's static dispatch/alias mapping does
not establish whether a runtime call crosses an unproved loader/layer wrapper.
Unknown providers and shapes must continue to refuse before reading private
payload. The next adapter must preserve original entrypoints, pointers and
arguments, then combine this finite private contract with the native pass,
pipeline, descriptor, source and lifetime tickets. It grants no frame/eye proof.

The private module, instruction excerpts, source pins and acquisition receipts
are retained only in ignored storage. Generated test caches and the temporary
1.18GB source archive/repository have been removed after their processes exited.
