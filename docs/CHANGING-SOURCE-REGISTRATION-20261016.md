# Changing application-source registration

This is developer evidence for the Steam Link explicit-mask investigation. It
does not enable streamed passthrough, replace a native shader or modify Steam's
PC sender. All native mapping and alpha-presentation permissions remain closed.

## Actual PC result

The maintainer confirms a visibly moving test pattern in GloomhavenVR Build677.
The supplied logs identify Direct3D11, OpenXR MultiPass, supported CPU graphics
fences, Gamma output and 4040x4040 eye targets. Session
`f23421f555a346c88615b1677e34c280` completes 757 CPU source pairs with 757 callbacks
per eye, no fallback callbacks and no skipped cohorts. Opening VR Options stops
the diagnostic and restores ordinary source output. The camera census finds no
other image effects on its owned head camera.

The first four pairs contain eight explicit source and actual-eye receipts.
Their stereo engine cohorts agree; eye projections are distinct and stable,
and the view matrices change. Regenerating all eight explicit tuples through
the production C# generator reproduces every logged manifest field, including
the source-plane and full-canvas digests. Independent log review finds no
SteamLink-specific failure. This is actual PC evidence; it does not prove the
native decoder received those pairs or establish physical-eye/ray mapping.

The separate stackless NullReferenceException occurs during application
shutdown. Its cause is unassigned. Early water-shader lookup failures are
followed by successful bundle resolution; neither is assigned to this source
diagnostic.

## Bounded header bootstrap

`tools/registration_header_discovery.py` accepts only an explicitly supplied,
already mapped 688x548 canvas with the existing `FITTED_DIAGNOSTIC_ONLY` contract.
The expected nonzero session and source-eye label must come independently from
the corresponding PC run. It does not infer them, fit a map or choose a latest
source record. All three complete complementary headers must agree byte for
byte and pass CRC. Format, convention, reserved fields and every geometry value
are pinned. The discovered positive uint64 pair ID is an `UNTRUSTED_CANDIDATE`.
CRC is an accidental-error check, not source authentication.

Regenerate exactly that session/pair/eye with the actual C# `--emit` fixture.
Compare the complete independently generated header, including both plane SHA
values, and run the unchanged `registration_source_recovery.measure_mapped`
validator on the original canonical artifacts. Never generate expected artwork
from received hashes. The source profile is fixed independently; its name is
not encoded in the generic version-one header.

Nine focused methods cover identity/format/layout changes, mixed valid copies,
CRC and full-cell damage, uint64 boundaries, untrusted digest claims, explicit
mapping, replicated G codes and bounded/exclusive file handling. Integration
uses eight actual C#-generated tuples. An additional C# pair129 has pair1's
repeated phase and plane bytes but its own header: discovery preserves ID129,
and comparison with the old pair1 reference refuses. The old static validator
is unchanged. None of these tests is a new native decoded acquisition.

Older pairs repeated by the decoder remain legitimate candidates within the
expected session. Different sessions refuse; different left/right pair IDs must
never be joined into a stereo pair. Header agreement alone cannot bless mixed
or damaged scene planes. Even perfect fitted pixel agreement leaves native
frame, physical-eye, application-ray and decoded-source integrity unproven.

## Next receiver observation

The prepared ordinary package has now completed its approved one-shot run,
capture `b5ff764e7af8867a94aff07a56c555db`. All three transferred byte counts and
SHA256 values match the device's completed immutable files. Independent review
accepts the recorded owned fence, exact reusable-generation retirement and
disjoint storage slices. No producer refusal occurs. The uniform companion and
live mask remain disabled. The corresponding fresh PC source log is still
needed before selecting an independent expected session and decoding a tuple;
the earlier Build677 log must not be substituted.

Manual rollback is complete after separate agreement. Activation files and the
owned runtime library are absent, certain scans find no diagnostic mappings,
native UI health is ready, and the maintainer confirms normal visible usable
UI. Standby is safe. No further Frame access is needed for current offline work.
Original artifacts and all activation/arm/transfer/rollback receipts are kept
under `build/private-registration-source-run-20261016-r1/`. Two pinned historical
fitted hypotheses are prepared without session discovery. No new compiler or
Unity cache was created for the acquisition; all child processes are reaped.

The next prepared package uses the ordinary pixel/native-sampler diagnostic
with `FRAME_PIXEL_UNIFORM_COMPANION=OFF`. It acquires at most one immutable image
as the three existing artifacts: `snapshot.p010`, `sampler-components.rgba8`
and `snapshot.json`. It does not read additional native uniforms, inspect host
writes, alter shaders or change the PC sender. The native sampling path is used
because the same earlier capture's raw stored Y representation failed header
recovery. All capture integrity, fence and recording-retirement checks remain.

For offline comparison, the two unchanged historical rational maps may be used
only as pinned fitted hypotheses. Changed extents, clipping or header failure
must refuse; do not silently recalibrate. A private adapter reproduces all three
headers of the earlier immutable static capture using the original left map.
That checks the adapter, not changing-source carriage or a native runtime map.

The 293424-byte AArch64 package has three private Vulkan-loader exports, no TLS
or Vulkan library dependency, and NOW/RELRO. All 14 actual host Vulkan-loader
cases pass. Cold activation checks the currently mapped native runtime providers
before loading. A complete pre-stream gate requires native UI health, the actual
compositor's owned mapping, certain/no foreign diagnostic mappings, ready native
sampling, disabled uniform companion and no refusal/failure events. Arming
resumes the health checkpoints and takes a fresh sample before creating the arm
token. Visible UI and stream readiness still require the maintainer's report.

No Frame access, installation, capture or restart follows from the PC log
review. Fresh permission and readiness agreement are required for the next
temporary package. Rollback remains explicit and manual: remove the exact
activation files, restart SteamVR once after agreement, then remove the library
after its mappings disappear and verify original native UI health. Keep the
original captures. There is no automatic rollback, restart or source-test timer.

Real per-eye game coverage and a verified live presenter remain separate work.
Copied post-draw uniform bytes are not independently proven shader-consumed
bytes. The app-canvas-to-decoded mapping is still unknown. A successful next
header observation would identify a changing diagnostic tuple, not grant any
of these missing authorities.

Private evidence: main `.planning/debug/steamlink677/hardware-20261016/` and fork
`build/private-registration-hardware-20261016/`,
`build/private-registration-discovery-20261016/`,
`build/private-registration-receiver-prep-20261016/`. Preserve original inputs,
canonical artifacts, manifests, current deliverables and review reports; remove
only completed owned compiler/CMake/fixture caches after their processes stop.
