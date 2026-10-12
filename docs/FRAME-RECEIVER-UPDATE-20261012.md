# Receiver audit after SteamOS 0.4.5 update

The next manually coordinated capture was stopped **before deployment** by the
fresh native-file check. The maintainer confirmed updating the Frame since the
previous attempt. Its installed receiver and compositor fingerprints had changed.
The existing observer was not loaded, no Steam service was restarted, and no
file was created on the headset. SSH was closed after read-only collection.

## Current native inputs

The Frame reports SteamOS 0.4.5, build `20261007.6125817`, kernel
`6.18.0-ge66bc2ca6f8c`.

| Native file | Bytes | SHA-256 |
| --- | --- | --- |
| `vrlink` | 2,425,024 | `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153` |
| `vrcompositor` | 6,026,096 | `0bb148eda6b5e54e2fbed09d475a43671f5ff6fcb6c2b4f48345b1a9fe8d6f96` |

The receiver build ID is `bfe671fed2382cad717d2994fa3a62e5e99fb859`.
The launcher, libc, dynamic loader and native service environment-file hashes
remain the ones reviewed in [the Steam-client attempt](FRAME-RECEIVER-STEAM-RUN-20261012.md).
The root mount still matches the narrowly supported Btrfs layout; its boot slot
now names `/dev/sda4`. File inode numbers changed, so the fresh session must
record actual identities rather than carry old inode values forward.

A verified read-only copy of the current receiver and small validation records
remain in ignored `build/private-frame-update-20261012-084233/`. Transfer chunks
were removed after hash verification. No Valve binary or disassembly is tracked.

## Revalidated observation boundary

Do not replace only the expected hash or assume a uniform address shift.
Independent audits located and checked the exact public mutex call sites and
the protected dataflow in the new pinned binary:

| Observer boundary | Previous direct-caller return | Current direct-caller return |
| --- | --- | --- |
| Selected-record lock | `0x34a50` | `0x355f8` |
| Selected-record unlock | `0x34d74` | `0x3591c` |
| Metadata lock | `0x5200c` | `0x538dc` |
| Metadata unlock | `0x5203c` | `0x5390c` |

The selected and metadata functions moved by different amounts. Both still use
undefined public `pthread_mutex_lock/unlock@GLIBC_2.17` imports with dynamic
`R_AARCH64_JUMP_SLOT` entries. Their PLT stubs preserve the caller's return
address. The root audit passed 32 exact instruction checks and bounded dataflow
checks on the current file.

The native mutex-relative tuple and metadata field offsets remain unchanged.
The selected key reaches its metadata lookup without another call or branch.
The helper's audited 140-instruction window is unchanged apart from its address;
it reads the selected record and replaces its argument with the resource token
without storing through or escaping the record pointer. Failure branches still
share the unlock sites, so an observation remains a **protected selection
attempt**, never proof of accepted presentation.

The production port must pin this new receiver and these verified sites, retain
all forwarding, liveness, bounds and conservative evidence labels, and refuse
unsupported builds. The scope remains the same six scalar fields at imported
public mutex boundaries, at most 32 records, 1 MiB and 120 seconds. No private
method probe, binary patch, image/uniform read or shader activation is introduced.

The integrated port passed 10 preparation and 67 session tests, 175 core
assertions normally and under ASan/UBSan, and 17 real host preload cases.
The independent worker audit also checked 47 exact instructions and 17 decoded
branch targets. The new AArch64 production observer is 75,048 bytes, SHA-256
`393e39c3bb9970032cac54a9fd3e3547389d62f78bb5056b014139c677e4011a`.
Its artifact check found only the two public mutex exports, 80-byte initial-exec
TLS, unchanged required import versions, and no dynamic TLS, `dlsym`, test
bypass or older receiver pin. These are software/static checks, not runtime
capture evidence.

## Limits of inherited evidence

The Btrfs arm correction and verified manual rollback from the preceding session
remain relevant. This static update audit is not a new native capture or headset
validation. In particular, previous compositor import addresses, descriptor
snapshots and UV findings belong to their exact older binary. They do not prove
the new compositor's live resource/eye binding. At this audit checkpoint, the
updated receiver still required a coordinated runtime capture; the subsequent
run below supplies that separate evidence.

The [subsequent native capture](FRAME-RECEIVER-CAPTURE-20261012.md) supplied 32
valid protected selections and was fully restored. It is separate hardware
evidence; the source-frame, compositor namespace and GPU gates remain open.
