# Manual receiver observer session

`tools/receiver_observer_session.py` prepares a reversible user-service session
for the [protected scalar observer](RECEIVER-OBSERVER-IMPLEMENTATION.md).
The helper is tested offline and has performed a manually restored Steam-client
attempt; **this document does not authorize installation**.
New sessions require permission for private scalar copies, a **Steam client
service** preload and restart readiness. The previous concrete approval targeted
the SteamVR drop-in; it did not authorize this different unit. The maintainer
separately approved the Steam-client scope for
[the corrected attempt](FRAME-RECEIVER-STEAM-RUN-20261012.md). Restarting the
Steam client interrupts the Frame interface and stream. No mask or shader is
activated.

## Corrected launch boundary

The 2026-10-12 hardware attempt produced no capture or private scalar observations.
The pinned `vrlink` receiver was started through `run_vrlink.sh` by the Steam
client in `steam.service`, while the schema-v1 preload reached `vrcompositor` in
`steamvr.service`. Receiver hashes and service activity could not prove that the
receiver inherited the observer. Its actual mapped libc/loader matched the native
provider review despite a `Steam/steamrtarm64` library search path. The subsequent
approved Steam-client attempt loaded the receiver observer, then refused arming
on a Btrfs mapping/stat device mismatch. The capture contained no protected
records, and its activation was fully removed after manual restart readiness.

Schema v2 therefore supports **only `receiver_service:steam.service`**. Never
overwrite a schema-v1 helper in an active session: use that session's original
self-contained copied script and its original readiness flag to roll back the
SteamVR drop-in first. New start refuses any lingering old
`steamvr.service.d/91-frame-receiver-observer.conf` and any pre-existing session.
Archive old evidence and remove only reviewed owned files after processes stop.

## Reviewed inputs

First use `tools/prepare_receiver_observer.py` with the actual native receiver,
libc, dynamic loader and reviewed production observer. Its
`frame-receiver-observer-proposal-v2` output remains `runtime_authorized:false`;
`offline_files_supported:true` is only offline file compatibility. A cross-sysroot
proposal cannot substitute for the Frame's actual native provider namespace.

The current production observer and preparation/session helpers pin the
[re-audited SteamOS 0.4.5 receiver](FRAME-RECEIVER-UPDATE-20261012.md) and its four
exact caller returns. A fresh hardware check stopped the previous observer
before deployment when the receiver changed. Stale receiver pins or proposal
call sites are refused. Keep an active session's original self-contained
rollback helper; do not replace it during a port.

The current helper [completed a native 32-record capture](FRAME-RECEIVER-CAPTURE-20261012.md)
and exact manual restoration after that port. Its receiver/Btrfs guards passed
on the actual capture writer; the remaining frame/GPU gates were not inferred.

The session helper takes these four files explicitly, rereads their bytes and
checks hashes, sizes and ELF64 little-endian AArch64 ET_DYN headers against the
reviewed proposal. It requires the exact supported `vrlink` hash and basename.
Use resolved, non-symlink native input paths; activation paths never follow
symlinks. Review the proposal's own SHA-256 before acknowledging its scope.

Review every EnvironmentFile declared by the actual native Steam client unit,
including `/usr/share/deckard/mesavars.sh` when listed. `start` requires
`--reviewed-environment-file PATH=SHA256` for every file
listed by `systemctl show` (at most four). It accepts bounded ordinary assignment
lines, comments and simple quoted values, never executes or sources the file,
and rejects unsupported syntax, changed fingerprints and foreign preload/trace
values. Unknown EnvironmentFiles syntax fails closed. Native files are read-only.

## Exact mutation scope

The sole activation file is:

```text
$XDG_RUNTIME_DIR/systemd/user/steam.service.d/91-frame-receiver-observer.conf
```

The private session is `~/.local/share/frame-receiver-observer` (mode 0700).
Files are created exclusively with mode 0600. `owned.json` records the exact
activation bytes/hash, observer/proposal/helper hashes, original service and
manager environment, and original native environment-file bytes/hashes **before
the drop-in exists**. A bounded `session.jsonl` records acknowledgments and
restart requests. Existing sessions, foreign activation files, unsafe paths and
nonempty foreign `LD_PRELOAD`, `FRAME_RECEIVER_*` or `FRAME_MASK_TRACE*` values
are refused. Existing directories, native unit files and environment files are
never rewritten or chmodded.

The drop-in supplies only `LD_PRELOAD`, `FRAME_RECEIVER_TRACE_OUTPUT` and
`FRAME_RECEIVER_TRACE_ARM`. It leaves native ExecStart/ExecStop
commands, restart policy and environment intact. Inherited nonreceiver children
only forward mutex operations; only the pinned receiver claims the capture.
There are no timers, watchdogs, sudo commands, kernel probes or native patches.

## Explicit operations after authorization

Run on the native Linux/AArch64 Frame user session with
`XDG_RUNTIME_DIR=/run/user/<uid>`. Substitute reviewed resolved paths and hashes:

```bash
python3 tools/receiver_observer_session.py start \
  --reviewed-plan /private/review/receiver-proposal.json \
  --reviewed-plan-sha256 <reviewed-proposal-sha256> \
  --native-receiver /resolved/native/vrlink \
  --native-provider-libc /resolved/native/libc.so.6 \
  --native-loader /resolved/native/ld-linux-aarch64.so.1 \
  --observer /private/build/libframe_receiver_observer.so \
  --reviewed-environment-file /usr/share/deckard/mesavars.sh=<reviewed-sha256> \
  --ack-private-scalar-preload --ack-steam-client-restart-readiness
```

Include the environment-file argument only for each file actually declared by
`steam.service`; omit it when that unit has no EnvironmentFiles.

`start` reloads the user service and requests one explicit **Steam client restart**.
The acknowledgment is an operator confirmation of separately granted permission,
not permission inferred from the proposal. Service activity alone does not prove
that the receiver observer loaded. A failed explicit activation attempts exact
owned rollback and records any native restoration restart; it schedules nothing.

After reconnecting a **PC VR stream** and confirming the VR game is visible:

```bash
python3 ~/.local/share/frame-receiver-observer/receiver_observer_session.py arm \
  --ack-vr-stream-ready
```

`arm` checks the active service, exact activation/owned file hashes and unchanged
native environment-file snapshots. Its private capture must contain the exact
production supported header with `writer_liveness:exclusive_flock_v1` and be
held by an exclusive writer `flock`. The helper verifies that a shared lock
cannot be acquired before and after its bounded header read. An open descriptor,
header alone, expired writer, footer, changed inode, symlink or public file never
authorizes arming. It also identifies the one exclusive capture FLOCK owner in
bounded `/proc/locks` metadata. That user-owned PID must have the pinned
`/proc/PID/exe`, belong to `steam.service`, map the exact owned observer and
inherit its three expected environment values. Actual libc and loader are read
as on-disk files through `/proc/PID/root`, and their device/inode and fingerprints
must match both `/proc/PID/maps` and the reviewed native proposal. Direct
device/inode comparison stays the normal path. The observed top-level Btrfs
layout has a narrowly checked reconciliation for **libc and loader only**:
their exact reviewed paths and inode numbers must match, a unique deepest mount
must be `/` with `btrfs`, `subvolid=5,subvol=/` and the mapping's superblock
device, and the receiver root must have inode 256 and the same anonymous
file-stat device. Other mounts/subvolumes, ambiguous mounts, other filesystems
and observer-device mismatches remain refused. Equal hashes alone never authorize
this reconciliation. Its evidence is recorded in the arm event.
A search path, similar library basename or mapping path alone is insufficient.

PID starttime, cgroup, relevant mappings, executable inode and capture-lock owner
are rechecked before creating the empty mode-0600 arm marker. When Btrfs
reconciliation is needed, bounded mount metadata, root identity and native file
path identities are also rechecked. There is no process
enumeration: only the capture lock's identified PID is inspected. Bounds are
1 MiB/8192 rows for lock metadata, 512 KiB/4096 rows for maps, 64 KiB for filtered
environment metadata, 8 KiB for stat/cgroup, 512 KiB/4096 rows for mountinfo,
and the existing native file limits.
Unavailable, ambiguous or changing evidence refuses arming. This reads metadata
and on-disk ELF bytes only, never `/proc/PID/mem`, map_files, mapped uniform data
or images. It is an instantaneous receiver/writer verification, not proof of GPU
consumption or source-frame association.

The writer deadline is 120 seconds from observer startup, with at most 32
selection attempts and 1 MiB output. If it expires before the stream is ready,
arming is refused. The deadline stops recording only; it never restarts Steam
or automatically removes activation. Roll back manually and arrange a newly
reviewed session if another run is needed.

## Manual recovery

Keep the Frame awake until restoration has been checked. Recovery remains usable
if the stream or original terminal exits:

```bash
python3 ~/.local/share/frame-receiver-observer/receiver_observer_session.py rollback \
  --ack-steam-client-restart-readiness
```

Rollback removes **only** an exact matching owned drop-in, reloads the user
manager, requests one native **Steam client restart**, and compares service/manager/environment
files against the original snapshots. The service environment, drop-ins and native
EnvironmentFiles must match exactly. Manager values also match exactly except
that `GAMESCOPE_LIMITER_FILE` and `MANGOHUD_CONFIGFILE` may retain existing path
values that change during a native session restart. Both old/new values must be
normalized absolute direct children of the user's runtime directory, with the
respective `gamescope-limiter-` and `gamescope-mangoapp-` filename prefixes.
Their contents are never read or changed.
Missing/added keys, relative values and changes to any other manager key still
refuse completion. The helper never sets or overwrites the manager environment
to force equality, and preload/observer/trace refusals remain in place.

This exception follows the 2026-10-12 rollback evidence: native configuration was
restored and the observer was absent, while those two native session paths had
changed. `rollback-complete.json` and the ledger record `exact_manager`,
`manager_matches_except_session_paths`, changed key names and
`permitted_path_changes`. A permitted
path change has `exact_manager:false`; successful restoration does not claim a
byte-identical manager environment. A completed restoration is idempotent.
Changed or foreign activation bytes are preserved and reported for manual review;
they are never blindly deleted. Missing activation after an interrupted rollback
can be recovered using the saved manifest. Ledger ENOSPC/bound errors cannot
prevent removal and the native restart. If restoration bookkeeping cannot be
written, the operation reports failure; do not infer completion or promise that
a retry cannot restart again. The retained capture/manifest are bounded private
evidence, not active instrumentation. Archive them before removing a completed
session directory for reuse.

`rollback-complete.json` confirms configuration restoration and active service;
it explicitly does not assert that a particular native receiver process was
inspected. Independently verify normal headset/stream operation before standby.
No helper result establishes accepted submission, source-frame association,
coherent eye/UV binding or working Steam Link passthrough.

## Focused offline evidence

The mocked session suite covers successful start/arm/idempotent Steam rollback, fresh
artifact and plan fingerprints, ABI/path/env guards, native EnvironmentFile
snapshots, activation-before-restart recovery, foreign-file preservation, writer
lock/header refusal, wrong launch service, absent receiver/observer, actual mapped
provider hashes, missing inherited environment, inode mismatch, changed PID/lock,
metadata bounds, old-unit refusal, interrupted/failed activation, and recovery
despite full or bounded ledgers. It performs no Frame access, network calls or real systemctl
operations. Receiver C++/preflight evidence is documented separately; these
Python tests do not replace native provider checks or a headset test. One focused
Linux host case verifies the real `/proc/locks` FLOCK-owner format and lock release
using a temporary local fixture; receiver process/mapping cases remain mocked.
The Btrfs cases reproduce the captured `0:29` mapping / `0:30` stat identities
while retaining real fixture-file reads and hashes. Negative fixtures cover
inode/content/path changes, other filesystems, wrong superblock/anonymous root,
bind and nested mounts, colliding subvolumes, stacked ambiguity, metadata bounds
and changed mounts/root/file identities. They do not establish native arming.
