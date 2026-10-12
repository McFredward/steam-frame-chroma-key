# Steam-client receiver observer attempt, 2026-10-12

The maintainer explicitly approved the corrected **Steam-client-service**
preload and restart. It reached the native receiver, resolving the launch-route
failure in [the preceding attempt](FRAME-RECEIVER-LAUNCH-20261012.md).
The safety gate then refused arming because it incorrectly assumed that
`/proc/PID/maps` and file `stat` always report identical device numbers.
**No protected selection records were captured, and no mask or shader was enabled.**

## Fresh native evidence

The exact reviewed production observer remained unchanged: 74,896 bytes,
SHA-256 `97acec88ae307ade7130c8424d3b47dbcdee44cf14a8da8c2be6235afa31cccc`.
The Frame's current receiver, libc, loader and native environment file matched
their previously recorded fingerprints. Fresh proposal v2 had SHA-256
`63b1510f569a45cd8a72776953b22fd7e24bdfd965a1489ad3b505b708818c4f`.
The proposal still says `runtime_authorized:false`: permission came from the
maintainer, not from the artifact check.

After the explicitly acknowledged Steam-client restart:

- Native ARM64 Steam client PID 850860 mapped the exact observer and inherited
  its three expected environment values in `steam.service`.
- Once the maintainer connected a PC VR stream, native `vrlink` PID 853971 held
  the capture's exclusive writer lock in the same service. Its header declared
  the exact supported receiver hash and production bounds.
- The owned observer mapping matched the actual file device, inode, bytes and
  hash. Actual libc and loader bytes and inode numbers also matched their
  reviewed files, but their device numbers differed between the two kernel APIs.

| File | Mapping device | File-stat device | Inode |
| --- | --- | --- | --- |
| libc | `0:29` | `0:30` | 50309 |
| dynamic loader | `0:29` | `0:30` | 48411 |

Receiver and inspection process were in the same mount namespace. The receiver's
root mount record identified Btrfs on device `0:29`, mounted at `/`, with
`subvolid=5,subvol=/`.

This difference is consistent with the Linux implementation:
[Btrfs file attributes use the root's anonymous device](https://github.com/torvalds/linux/blob/v6.16/fs/btrfs/inode.c#L7488),
while [process mappings use the inode superblock's device](https://github.com/torvalds/linux/blob/v6.16/fs/proc/task_mmu.c#L304).
It is not evidence that the Frame substituted an unsupported libc. It also does
not justify ignoring device identity or accepting equal hashes alone. The
correction must reconcile the observed filesystem identities explicitly and
preserve all other process, mapping, inode, fingerprint and liveness checks.

## Recording outcome

The unmodified arm helper refused `Mapped native file identity mismatch` and
created no arm marker. The unchanged writer expired normally after its 120-second
deadline. Its 790-byte capture contained only the production header and footer,
SHA-256 `4e7929b2be9ca82aeb900f10000d6b302aacacb3c4cfa3189cea4f7286e994f3`.
The footer recorded `armed:false`, zero completed/serialized records,
`closed_quiescent:true` and no queue, pointer, mutex or I/O failures. This is a
successful receiver-loading observation followed by a pre-arm refusal, **not**
a successful scalar capture.

Private proposal, manifests, metadata, capture and rollback evidence remain in
ignored `build/private-receiver-steam-run-20261012/`. No native executable,
shader, disassembly or camera/scene image is redistributed.

## Verified manual restoration and cleanup

After separate restart-readiness confirmation, the original copied v2 helper
removed its exact owned Steam-service drop-in and restarted native Steam.
Restoration matched the original service, manager environment and native
environment file **exactly**; no volatile-manager exception was needed.

Independent checks confirmed the old receiver and instrumented Steam client had
exited, native files retained their hashes, both diagnostic drop-ins were absent,
and native Steam was active. All 11 inspected Steam-service processes lacked
diagnostic mappings and environment values. Four unrelated processes denied
mapping access; their groups were outside the affected Steam service, and the
record explicitly does not claim inspection of every process on the headset.

The final manifest and checks were archived before removing 16 exact owned
files, including the generated staging Python cache, and both private diagnostic
directories. SSH was closed; the maintainer was told the Frame could enter
standby. No timer restarted the headset, no native configuration was overwritten,
and the active self-contained rollback helper was never patched in place.

The remaining capture still needs a checked Btrfs identity reconciliation and
another explicitly coordinated restart. Source-video association, resource
import identity and coherent GPU eye/UV consumption remain separate unresolved
gates. This attempt does not enable genuine Steam Link passthrough.
