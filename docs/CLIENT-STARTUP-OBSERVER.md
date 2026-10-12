# Passive client-startup observer

`receiver/client_startup_observer.cpp` supplies the main-namespace artifact
`libframe_client_startup.so`. It cold-prepares the already loaded receiver, API,
libc, loader and observer mappings, initializes the bounded startup runtime,
exports `frame_client_startup_bridge`, and drains copied records on one owned
background thread. It does not export `VR_Init*` or `VR_Shutdown*`, load OpenVR,
change shaders, alter scene pixels, install itself, or restart Steam.

The shared native entry assembler supplies public mutex aliases and hidden
Init/Shutdown and mutex entries. C wrappers must not replace those entries: the audited
original caller LR, SP, FP and arguments are part of the birth proof. The audit
DSO uses the bridge only after its own preinit boundary; preparation alone does
not authorize an early redirect or prove an object lifetime.

The actual receiver uses the Steam-runtime API described in
[RECEIVER-STEAMRT-API-20261012.md](RECEIVER-STEAMRT-API-20261012.md). Its native
`RTLD_NOW | RTLD_DEEPBIND` client load requires selective audit forwarding of
the real source-client-to-libc mutex binding; preload aliases alone are not
capture evidence. MAIN prepares distinct versioned libc providers and compares
the copied client/libc open generations and executable spans before accepting
that route. A later `Stage::Loss` immediately retires evidence while any already
bound entry keeps forwarding native calls. See
[IMPORT-STARTUP-FORWARDING.md](IMPORT-STARTUP-FORWARDING.md) for the full boundary.

## Explicit cold inputs

Only these two environment variables are consumed; other environment/configuration
files are neither read nor dumped:

- `GHVR_FRAME_CLIENT_STARTUP_ENVELOPE`: absolute path to a private prepared file.
- `GHVR_FRAME_CLIENT_STARTUP_OUTPUT`: absolute path to a new owned output file.

The constructor opens the envelope read-only, nonblocking, close-on-exec and
without following a final symlink. It requires a regular file, exactly 11,800
bytes, current effective-user ownership, one link and exact mode `0600` (including
no special mode bits). A bounded read must complete, and device, inode, mode,
ownership, link count, size, modification time and change time must remain stable
between the two `fstat` checks. Nonblocking open prevents a FIFO from hanging
startup before the file-type check. This guards the input acquisition; the
separate hard-pinned metadata/hash validators remain authoritative.

The envelope is explicitly little-endian and contains no redistributed native
machine code:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 8 | ASCII `FCSTART1` |
| 8 | 4 | Schema `1` |
| 12 | 4 | Exact envelope byte count `11800` |
| 16 | 32 | Independently reviewed observer descriptor fingerprint |
| 48 | 512 | Exact public client loader name, nonempty NUL terminator and zero tail |
| 560 | 8 | Client executable segment RVA begin |
| 568 | 8 | Client executable segment RVA end |
| 576 | 10160 | Five 2032-byte descriptors: receiver, API, libc, loader, observer |
| 10736 | 1064 | Canonical client descriptor |

The client span must equal the canonical executable file-backed segment containing
its audited caller RVA. Native descriptors use their separate built-in canonical
fingerprints; the observer uses the independently reviewed companion fingerprint.
The companion describes the final reviewed observer build, avoiding a circular
embedded self-binary hash. A deployment helper must additionally verify the final
artifact and envelope inventory before loading either DSO. File names, a copied
file SHA label or a producer's `ready` boolean cannot substitute for actual
loader-owned mapped verification.

A real sealed `main::Prepared` capability is mandatory. Wrong executables,
missing APIs, unavailable/incompatible providers, invalid descriptors, an already
loaded client, absent shared entries or failed runtime initialization leave the
bridge inert and do not create an output file. They do not attempt to load a
missing API/client or invoke Init/Shutdown.

## Recording lifecycle

After genuine initialization, exactly one writer creates the output through
`openat` with `O_EXCL`, `O_NOFOLLOW`, close-on-exec and mode `0600`. Its parent must
be a current-user-owned directory without group/other write permission. Existing
files are not overwritten. The writer serializes only owned copied scalars and a
bounded copied loader name; addresses are diagnostic scalar values, never deferred
native pointers to dereference.

The output is JSON Lines, bounded by 32 records, 64 KiB total and 120 seconds of
recording. The header names those caps and the reviewed observer fingerprint.
Records retain original entry registers, actual validated module geometry and
code identity, local object registry labels, observed audit-open generations,
copied client/libc binding views,
actual native outcomes where available, and explicit refusals. A footer records
the drained/queued counts and why recording stopped. Failed writes, capacity or
time limits disarm recording; they do not restart services, roll back files or
change the headset image. The only waiting, formatting and file I/O occurs on the
writer, outside native callbacks.

The finalizer first makes the runtime inert, then stops and joins its writer.
Late audit-close notifications cannot reactivate it. A bounded PID ownership
check refuses a real fork's inherited active state: the child bridge stays inert,
the child cannot create/write a capture and its finalizer never acknowledges the
copied prepared runtime or joins the parent's copied writer handle. A cold
`pthread_atfork` child callback is registered after genuine initialization and
before writer creation. It atomically finalizes the child's copied runtime and
clears writer ownership, including native notification paths that bypass the
bridge. Registration failure refuses activation. The cold function mappings are
checked against the already authenticated libc/observer; the actual glibc
`__register_atfork` provider must have the canonical libc export RVA `0xd35c0`.
The public wrapper's glibc DSO ownership handles callback lifetime. No fork/exec
interposer is introduced. The deadline performs the
same recording-only disarm and has no automatic restart or rollback behavior.

`process_generation` uses this process's PID only as single-process observer
bookkeeping; `local_verification_label` is a local verification label. Neither is
a native resource generation, audit-open generation, source/video frame or
cross-process identity. Every serialized record explicitly denies source-frame,
native cross-process namespace, accepted-submit and GPU proof. Local startup
records cannot establish streamed mask/resource association by themselves.

## Focused verification

`tests/test_client_startup_observer.py --observer <actual-built-library>` loads the
actual observer into an unrelated host probe. It checks that native mutex outcomes
and errno remain equal to the baseline, that the actual exported bridge stays
inert for all stages (including an actual fork child and its normal exit/finalizer),
and that no public OpenVR entry is exported or output file
is created. Missing/malformed inputs, wrong exact modes, symlinks, hard links,
FIFO and size/type failures are refusal controls. `--trace-io` additionally uses
actual syscall traces to distinguish input-read refusal from a later schema
refusal; the FIFO must use nonblocking open. `--envelope <private-envelope>` adds
an actual canonical-input wrong-executable control.

The test removes generated probes, inputs and syscall traces after child processes
have exited, retaining only its small JSON receipt. Sanitized host execution and
AArch64 compilation are separate focused checks. They prove bounded software
refusal/forwarding and compilation; the host fork control starts inert and does
not fabricate an inherited production Ready/writer state. They do **not** prove a
successful native startup capture,
correct headset picture or working Steam Link passthrough. Production activation
still requires Root's linked true-entry/audit artifacts, reviewed final inventory
and an explicitly authorized hardware run.
