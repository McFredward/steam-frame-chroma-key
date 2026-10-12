# Client-library bootstrap before native pipe birth

The current client creates its embedded resource-manager pipe **inside its ELF
initializers**, before the natural `dlopen` returns. Discovering its mapping on
a later worker, after `dlopen`, or at the first import cannot establish that
pipe's object birth. The registry must continue to refuse a missed birth; a
connected FD snapshot cannot repair it.

The minimum source-supported bootstrap is a prepared, first-initialization
`VR_InitInternal2` gate. Within that same thread's loader initialization, a
single candidate public mutex-init entry can discover the actual client module
through the prepared public `dladdr` provider, validate its mapping, and then
reserve its source-proven birth **before forwarding the original mutex init**.
This is bounded startup work, not a new lookup on graphics or import callbacks.
It needs implementation and runtime validation; this audit does neither.

## Exact inputs and early constructor

This audit uses the retained SteamOS 0.4.5 files in the private
`private-frame-update-20261012-084233` directory:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| `libopenvr_api.so` | 249,168 | `a07163c436b3d791a0aa2442029d2ef0ba40a772ebf1dc6006d610c3a7934ee2` |
| `vrclient.so` | 6,511,792 | `19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0` |
| `vrlink` | 2,425,024 | `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153` |
| libc | 1,722,992 | `be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b` |
| Loader | 204,048 | `2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e` |

The root agent acquired the actual OpenVR API library through an authorized
read-only copy during this audit; its adjacent provenance record remains
private. The worker performed no Frame access, native execution or installation.
Addresses below are ELF-relative for these exact files, not process addresses.

`vrlink` needs `libopenvr_api.so`, rather than `vrclient.so`. The client has 104
ELF initializer entries. One provides this direct, verified route:

| Client location | Action |
| --- | --- |
| `.init_array` slot `0x607b30` | Calls global initializer `0xa8e20`. |
| `0xa924c` | Constructs the HMD singleton through `0x1b9438`; return `0xa9250`. |
| `0x1b954c` | Installs the embedded manager's previously audited address point. |
| `0x1b9578` | Constructs its pipe through `0x244da8`; return `0x1b957c`. |
| `0x244db8` | Calls platform initialization `0x259c38`; return `0x244dbc`. |
| `0x259c70` | Tail-forwards `pthread_mutex_init` after restoring native FP, SP and saved registers. |

The constructor initializes request sequence 1 and peer PID 0 at `0x244df4`,
then FD -1 and related native state at `0x244df8`. These stores occur **after**
the public init call. Its before/after callback must not read those fields or
pretend the later stores are already observed. This is an object-birth route;
connection ownership still requires the later successful native connection
boundary and its independent socket lifetime.

The direct initializer call graph does not reach the known socket/connect
function. That is not a complete negative proof: indirect calls and external
callbacks remain. The positive constructor path above is sufficient to reject
the proposed post-`dlopen` birth registration without that broader assumption.

## Natural OpenVR initialization and ABI

The receiver's initialization method starts at `0x2acb8`, with a 4,448-byte
frame. At `0x2ae90` it calls `VR_InitInternal2`; incoming native return PC is
`0x2ae94`. The actual three arguments are error-output pointer in x0,
application type 1 in w1, and startup-string pointer in x2. The current API at
`0x5ab8` preserves those arguments, writes the native error, and returns a
32-bit initialization token. It is not a render, submit or import callback.
This call site alone does not prove process-first initialization: the observer
must enforce its prepared first-attempt state and refuse a prior/unknown load.

The API follows this route without any observer-forced loading:

| API location | Action |
| --- | --- |
| `0x5af4` | Calls native client-loading routine `0x5740`. |
| `0x58c4` | Calls loader helper `0x8fe8` with the native client pathname. |
| `0x8ffc` | Calls public `dlopen`; native return `0x9000`, flags 2 (`RTLD_NOW`). |
| After return, `0x58d8` | Resolves `VRClientCoreFactory` through `dlsym`. |
| `0x58f4` | Calls that factory for `IVRClientCore_003`. |
| `0x5b18` | Calls the returned core's initialization slot 0. |

The constructed HMD core's slot at byte offset 0 resolves to `0x1bebe8`; cleanup
at byte offset 8 resolves to `0x1bb320`. The factory's later public initialization therefore does not move
the earlier pipe constructor out of the loader.

## Minimum implementation contract

1. Prepare outside callbacks: pin the actual receiver, API, libc and loader
   mappings; verify the saved client file identity; resolve and verify the real
   `VR_InitInternal2`, `VR_ShutdownInternal` and `dladdr` providers once. Cache
   the initializer thread's actual stack bounds. No forced client load, new
   private function detour, guessed bias or hot symbol resolution is needed.
2. A true-entry `VR_InitInternal2` veneer retains the original arguments and
   receiver return PC. Its before callback admits only the pinned receiver
   initialization call and the unused first-load state. Establish a thread-local
   startup gate around the unchanged native provider call. Every other call
   forwards transparently without discovery. An already-loaded or unknown client
   cannot be adopted retrospectively.
3. During that gate only, a candidate `pthread_mutex_init` before callback may
   make one bounded public `dladdr` discovery using its **original** incoming
   native return PC. Register/argument shape can filter candidates but never
   proves a mapping. Require successful lookup, the actual validated client
   base, matching prepared file/build identity and critical executable schema.
   The pinned client has its first `PT_LOAD` at VA 0; verify that geometry before
   interpreting `dli_fbase` as this ELF's load bias.
   Pathname or `PC - expected offset` alone is insufficient. No private stack
   read occurs before module ownership and current-thread bounds are established.
4. Decode the exact global HMD constructor route below while those original
   frames are live. Reserve a `GenericClient` birth, forward unchanged native
   init, then commit only its actual successful result. This birth has no
   accepted FD and makes no claim that FD/PID initialization has completed.
   Accepted-pipe constructors remain a different route.
5. Disable startup discovery after it succeeds and on **every** exit from the
   public initialization wrapper, including failure or unwind. Recursion,
   missing after events, mapping failure, unmatched constructor or exhausted
   bounds lose the evidence; they never manufacture an epoch. Preserve native
   arguments, result, error-output behavior, errno and unwind behavior.
6. Native connection observation can subsequently activate the known object.
   All ordinary lifecycle/import callbacks use the immutable registered profile;
   they perform no `dladdr`, `dlsym`, `/proc` read or file hashing.

For the public init entry, `FP = SP`, x0 and x19 are the pipe, and x1 is pipe +48.
The constructor frame's saved FP at +0 is `FP + 32`; its saved LR at +8
is client base + `0x1b957c`. The HMD frame at `FP + 32` has saved FP `FP + 416`
and saved LR client base + `0xa9250`. These two fixed links identify this
global-initializer route without a generic backtrace. Only the five scalar
slots in the first 48 bytes need copying. Current-thread bounds should contain
all three known frames, through `FP + 544`; the global initializer allocates
128 bytes. Its loader return PC is unspecified and must not be guessed.

The constructor's saved inherited x19 at `FP + 16` is the HMD singleton. For
this exact global route its source address is client base + `0x647d78`; the
pipe is HMD + `0x2278`, hence client base + `0x649ff0`. These are additional
numeric consistency checks **after** actual mapping validation, not a license
to read that native global object. Do not accept merely a nonzero parent LR.

## Provider safety and module retirement

The actual libc exports `dladdr@GLIBC_2.17` and `dladdr@@GLIBC_2.34` at
`0x7f320`, size 56; both `dlopen` versions are at `0x7fc70`, size 192.
There is no distinct `__dlopen` forwarding export. The actual `dladdr` path
locks `_rtld_global + 0xa88`; the retained loader initializes that mutex with
recursive kind 1. This supports same-thread startup discovery while the loader
is initializing a mapped module. It does not make discovery lock-free or
approve it on rendering threads. Verify the actual provider and startup
recursion behavior in a host fixture and the scoped native run.

A separate ordinary C `dlopen` interposer would change the incoming LR which
libc retains as the loader's caller address. Namespace and origin/path behavior
would need additional proof. The public initialization gate avoids that extra
forwarding problem while preserving native loading and dependency handling.
`LD_AUDIT` is another early mapping boundary, but an audit library uses a
separate linker namespace: coherently sharing observer state and forwarding
through the actual target libc's TLS/cancellation domain require additional
work. Neither a second audit-local libc nor a guessed main-namespace address is
a shortcut for the prepared public gate.

`VR_ShutdownInternal` at API `0x5680` calls native core cleanup at `0x56c4`,
then unloads the client handle through `0x56dc` -> `0x9188` -> public `dlclose`.
Invalidate the client module/profile epoch and pending bindings **before**
forwarding this shutdown. Unrelated/unsupported unload paths must likewise
invalidate conservatively, or the observer remains unavailable. The API's
load/factory failure paths also call `dlclose`; a failed initialization cannot
retain its provisional profile. Never reuse a stale client bias after unload
or silently adopt a reload. Any future supported reload needs a newly observed
mapping and constructor/connection lifetime. No private handles are read to
guess which module survives.

## Evidence and remaining gates

Private `build/private-client-bootstrap/validate.py` passes **71 exact
instruction, branch, relocation, ELF-identity and actual-provider checks**.
The bounded startup plan now has concrete current API/caller/constructor inputs;
the initially missing API-library input was acquired and resolved. The actual
wrapper, first-load/recursion/error controls, prepared current-thread ownership,
fresh loaded-module verification and shutdown invalidation must still be
implemented and tested before deployment. No hardware bootstrap is claimed.

This bootstrap supplies local observed object/module lifetime, not endpoint
pairing, token namespace, source-frame identity, UV mapping or GPU ownership.
Those independent [carrier gates](FRAME-RESOURCE-CARRIER-20261012.md) remain
unchanged; pixels remain opaque until all required proofs exist.
