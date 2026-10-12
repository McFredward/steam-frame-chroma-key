# Compositor import diagnostic preparation

The successful [protected receiver capture](FRAME-RECEIVER-CAPTURE-20261012.md)
has 32 selected key/content/token observations. The next diagnostic is being
prepared locally; no compositor/IPC observer has been installed, no native
pixels changed and no mask shader activated. This checkpoint is deliberately
not advertised as working Steam Link passthrough.

## Implemented offline components

* `import/import_observer_core.*` records at most32 attempts in a fixed32-slot
  queue. A native image creation is provisional. Completion requires exactly
  one image/binding/view, the matching complete21-byte native resource reply,
  positive contiguous writes and the genuine native final-send outcome.
  Unsupported, incomplete, nested, changed or lost observations are refused.
* `import/native_import_route.*` decodes only the current pinned active-frame
  layouts. It computes each outer frame address before checking its saved
  FP/LR; it never chases an arbitrary frame pointer or scans a heap. The caller
  must independently establish true native entry and live-window ownership.
  Bounds checks alone cannot authorize reading an arbitrary address.
* `import/native_import_entry.S` captures the original AArch64 registers/SP
  before a compiler callback and retains explicit unwind rules. Its symbols
  remain hidden, it is not installed or exported as a public interposer, and
  its real-provider dispatchers are not implemented. Object-model tests do
  not establish real provider forwarding or cancellation behavior.
* The existing asynchronous Vulkan journal now accepts opt-in invocation tags
  at image/view creation and publishes a typed, immutable resource receipt.
  The worker resolves its actual image/allocation/view generations, checks
  matching creation tags and refuses destroyed/recreated, ambiguous, missing
  or lost resources. Ordinary untagged calls retain their existing behavior.
  Neither the invocation tag nor replay cursor is a video-frame identity.
* `tools/inspect_unix_connection.py` queries only two specified UNIX stream
  endpoints, repeats each query with its real kernel generation cookie, and
  checks process start identities, FD/socket inodes, UID and network namespace
  before/after. Four exact queries have a two-second deadline and bounded
  responses. There is no packet/image read, render-thread I/O, process restart,
  file installation or automatic rollback.

The socket helper uses the Linux
[UNIX diagnostic ABI](https://github.com/torvalds/linux/blob/v6.18/include/uapi/linux/unix_diag.h).
The kernel reports the actual peer's socket inode and validates the requested
socket cookie in its
[exact-query implementation](https://github.com/torvalds/linux/blob/v6.18/net/unix/diag.c).
This is a momentary kernel peer relation, **not continuous descriptor lifetime**.
The helper explicitly leaves socket-epoch continuity, native namespace, source
frame, accepted submission and GPU coherence false. Native lifecycle observation
must connect such a snapshot to an actual uninterrupted attempt; before/after
FD equality alone cannot detect every intervening close/rebind.

## Native timeline corrections

The independent source audit caught two incorrect early test assumptions:

1. The actual typed client ID does not exist at initial import lock admission.
   It is copied from the exact saved register during retained nested creation.
2. The compositor's header reader does not query peer credentials. For the first
   request, the native `SO_PEERCRED` query occurs during the later payload read.
   Initial admission can therefore be provisional, with absent peer facts;
   subsequent events must carry the genuinely acquired, owned credential copy.

The revised fixture models both timelines. Initial native unlock and successful
payload read are mandatory before creation. A cached peer can be admitted only
with its actual continuous socket lifetime. Client ownership is attached once;
after native release, later observations carry the immutable scalar copy rather
than rereading its pointer. Native return values remain authoritative and the
core never changes them.

Another independent machine-code review found missing unwind restores after
the entry veneer epilog. The original CFA offsets would have referenced memory
below the restored SP at `ret`; explicit `.cfi_restore` rules now remove those
stale offsets. This is an object-level correction, not an actual cancellation
experiment. A future cancellable `write` dispatcher must propagate forced
unwind and must not use a swallowing catch-all or `noexcept` boundary.

The receiver's real response parser can parse a wrong-sequence payload before
rejecting its header. The [current carrier audit](FRAME-RESOURCE-CARRIER-20261012.md)
therefore requires independent matching wire bytes and final token publication,
not just a nonzero parsed private scalar. Equal PIDs, FDs, counters or tokens
are still insufficient as a cross-process association.

## Read-only native reference refresh

Authorized read-only access copied the current `vrclient.so`, libc and loader
into private ignored storage and verified their exact hashes. The actual libc
has the distinct `__write@GLIBC_2.17` forwarding export at the same address/size
as `write`; `__read` and `__close` aliases were also checked. Some other socket
APIs lack distinct aliases and need a separately reviewed forwarding route.
No receiver was running during this read, so an on-disk library match does not
claim an actual receiver mapping. SSH was closed without changing any Frame
file, service, startup setting or process.

## Required next integration

Bind exact entry observations to the existing asynchronous Vulkan resource
journal's real generations; no rendering callback may look up mutable object
tables or manufacture a generation from its call number. Complete production
forwarding, callback cancellation, observed socket creation/retirement and
receiver-side carrier/publication recording remain required before deployment.
The exact current executable/library/provider maps and every active read scope
must be independently checked in the resulting artifact.

The existing receiver permission covers its six protected scalars. It does not
authorize this new compositor/client recording. Prepare the complete bounded
artifact, fixtures and recoverable manifest before requesting the additional
specific scope. Until all source/session/content/eye/UV/GPU joins are proved,
the explicit-mask path remains opaque and inactive.

Private current references and validation receipts remain under ignored
`build/private-frame-update-20261012-084233/` and `build/private-import-*/`.
Completed regenerable binaries/caches are removed while retaining source,
validation reports, native references and the original hardware capture.
