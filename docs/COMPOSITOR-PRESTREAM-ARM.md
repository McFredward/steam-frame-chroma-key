# Pre-stream operator arming

This is a passive import diagnostic, not working Steam Link passthrough. The
operator may arm the existing collector after the diagnostic SteamVR invocation
and system UI have passed their health checks, immediately before connecting the
PC-VR stream. The visible-stream path remains available.

## Why the selection phase matters

The captured callbacks describe resource imports, not every video frame. In the
latest visible-stream attempt, the collector armed successfully but did not claim
an import candidate. An import that happened while connecting the stream cannot
be recovered by arming after its image is already visible. Arming before stream
connection allows that subsequent import to be observed without changing the
private-read or recording budgets.

This timing change does not establish that a claimed import belongs to the PC-VR
stream. A system UI resource can also be imported after arming. The eventual
capture still needs independently established source session, video frame, eye,
extent and native UV/reprojection association before a mask can be used.

## Reviewed contract

New plans use `frame-compositor-operator-arm-v2` and explicitly contain
`allowed_stream_selection_phases: ["visible", "start-intent"]`. The helper
accepts exactly one of these acknowledgments for `arm`:

```sh
python3 compositor_raw_session.py arm --ack-pc-vr-stream-start-intent
python3 compositor_raw_session.py arm --ack-pc-vr-stream-visible
```

Use the first command only after the operator has confirmed readiness to connect
the PC-VR stream immediately after successful arming. It does not mean a stream
is connected or visible. Use the second only when the operator has confirmed an
already visible PC-VR stream. These commands do not grant permission to install,
activate or restart anything on the Frame.

Both modes revalidate the passed invocation, current compositor/UI health,
launch isolation, frozen launch identity, exact native and diagnostic mappings,
protected live raw/journal writers, unclaimed capture, reserved journal budget
and remaining absolute deadline. Neither mode restarts SteamVR, extends or resets
the 120-second deadline measured from capture start, or reopens a retired writer.
There is one candidate and at most 757 private metadata bytes. Marker contents,
ownership requirements and output limits are unchanged. The marker is single
use, including after receipt-write failure.

## Receipt and recovery

A v2 receipt records the actual `stream_selection_phase`. In `start-intent`,
`stream_start_intent_acknowledged` is true and `stream_ready_acknowledged` is
false; the latter continues to mean the operator confirmed a visible stream.
In `visible`, those booleans are reversed. `source_frame_associated`,
`accepted_submit`, `gpu_consumed` and `mask_enabled` remain false in either
receipt. Neither receipt is proof of displayed pixels or a particular source
frame.

The helper still accepts exact v1 descriptors and their original visible-stream
receipts. It refuses `start-intent` against a v1 plan: the newer selection phase
must appear in a separately reviewed v2 descriptor. Recovery validates the
corresponding receipt version and acknowledgment consistency only after restoring
the native service. A damaged dynamic receipt cannot prevent removal of the
owned activation and the coordinated original-service restart; evidence is
preserved and its refusal reported afterward.

## Reserved raw trace sampling

A v2 plan also requires the observer report and actual protected journal header
to contain the same exact typed `raw_trace_sampling` metadata:

```json
{
  "intentional": true,
  "draw_call_limit": 32,
  "execute_child_limit": 16,
  "submit_command_limit": 16,
  "binding_snapshots_require_import_request": true,
  "lifecycle_survives_trace_cap": true
}
```

Missing fields, foreign caps, coerced number/boolean types and disabled lifecycle
preservation are refused before publishing the arm marker. Stage and acquisition
readmission check the observer report; arming additionally checks the live
journal header. Both the artifact preparer and session helper authenticate this
collector's owned sampling marker in the actual ELF before staging or acquisition
readmission. The exact 32-byte `FCMSMP01` object must occupy one read-only,
non-executing, file-backed load range and have the reviewed LOCAL or HIDDEN
OBJECT symbol, absent from the dynamic symbol table. A JSON report cannot relabel
an older artifact as this collector. These bounded trace samples do not prove
complete GPU coverage, video-frame association or absence of unsampled calls.
Legacy v1 visible-only plans keep their original behavior. Sampling checks never
prevent the authorized original-service recovery before dynamic evidence is
validated.

## Software validation and remaining hardware work

Focused session tests exercise mutually exclusive boolean acknowledgments, CLI
selection, unchanged health/deadline/single-use gates, v1 compatibility, v2 intent
receipts, write-fault retry refusal and recovery before rejecting an inconsistent
receipt. These are synthetic session controls with real file locks; they do not
prove a native stream import or GPU/frame association. A new authorized headset
attempt is required to establish whether pre-stream arming observes an import.

## Current-image acquisition scope

The following hardware attempt captured a later UI import, despite correct
pre-connect arming. New `frame-compositor-raw-offline-bundle-v5` plans therefore
require the actual `FCMRAW04` acquisition object and envelope version 4. The
envelope length and all private, record, output and time ceilings remain
unchanged. Legacy v4/FCMRAW03 plans retain their original acquisition and recovery
contract; neither a changed JSON report nor a new helper may relabel that ELF.

The production observer retains paired, actual public Begin and EarlyUnlock
notifications in at most 16 preallocated worker slots. It prepares stack
ownership once per worker at the authenticated native Begin, outside graphics
callbacks. Only a small existing-owner slot index lives in initial-exec TLS;
the 128-byte artifact TLS ceiling is unchanged. No private parent, dispatch or
reply bytes are copied for excluded images.

The one irreversible claim now waits for a source-validated public Image Before
with NV12 or P010 format, even nonzero extent, depth 1 and one array layer. These
are stream-compatible resource inputs, not proof of a selected stream. Native
YUV UI surfaces also exist, so eligibility never sets source session/frame, eye,
UV, GPU or mask admissions. Missing or invalid association stays opaque.

After selection, the observer validates the live parent and current native
creation chain. It checks the pipe against the retained public lock arguments
and the creation method's saved dispatch register, and establishes the sequence
from the current creation frame. It never rereads the old lock-time saved-pipe
slot: the pinned dispatcher overwrites that slot before calling the image
creation method. Raw results explicitly distinguish actual retained public
lock outcomes from private acquisition beginning at Image. All later memory,
view, send and reply checks retain the complete invocation comparison.

The separately reviewed selection descriptor must match the actual protected
capture header. Public counters report staged calls, excluded/eligible images
and preparation/refusal counts without adding a private retry or extending the
deadline. Stage and acquisition readmission validate the actual scope marker;
arming additionally validates the current capture. Original-service recovery
still takes priority over changed or missing diagnostic evidence.
