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

## Software validation and remaining hardware work

Focused session tests exercise mutually exclusive boolean acknowledgments, CLI
selection, unchanged health/deadline/single-use gates, v1 compatibility, v2 intent
receipts, write-fault retry refusal and recovery before rejecting an inconsistent
receipt. These are synthetic session controls with real file locks; they do not
prove a native stream import or GPU/frame association. A new authorized headset
attempt is required to establish whether pre-stream arming observes an import.
