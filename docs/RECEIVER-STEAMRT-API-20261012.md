# Receiver Steam-runtime OpenVR API audit (2026-10-12)

The receiver's actual API is the Steam-runtime
`steamrtarm64/libopenvr_api.so`, rather than the compositor-side `/opt/steamvr`
copy used in the earlier API bootstrap audit. Their public names coincide, but
their file identities, implementation locations and native loader flags differ.
This report replaces the **API-specific** offsets and loader assumptions in
[IMPORT-CLIENT-BOOTSTRAP.md](IMPORT-CLIENT-BOOTSTRAP.md). It does not replace the
unchanged receiver/client constructor proof or authorize deployment.

This was a bounded offline review of Root's already acquired read-only private
file. The worker did not access the Frame, change files/services, execute native
API code, change the PC sender or activate a mask/shader. A path/file hash alone
still does not establish a future live mapping or resource association.

## Exact files and exports

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Actual receiver API | 333,320 | `4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4` |
| Unchanged receiver `vrlink` | 2,425,024 | `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153` |
| Unchanged `vrclient.so` | 6,511,792 | `19b2b9a2b512eb2cb25f0de9c60c20c6b82c78f29384e2fcec4487c507a4e8b0` |

The API is ELF64 little-endian AArch64 `ET_DYN`, with seven program headers and
build ID `17862ca7f5ecf5565385b881527773fa80361f1f`. Its executable file-backed
load covers RVA `[0, 0x3df34)`. All locations below are ELF RVAs, requiring the
independently verified current process load bias.

| Export | RVA | Bytes | Actual ABI |
| --- | --- | ---: | --- |
| `VR_InitInternal2` | `0x5768` | 236 | `uint32_t(error_pointer, application_u32, startup_string_pointer)` |
| `VR_ShutdownInternal` | `0x5330` | 192 | `void()` |
| `VR_GetInitToken` | `0x5320` | 12 | `uint32_t()` |

Init preserves the error-output pointer from x0, application type from w1 and
startup string from x2. It forwards the latter two to the returned core's slot
0, conditionally writes the actual error to the non-null output pointer, and
returns a 32-bit API token in w0. The unchanged receiver calls it at `0x2ae90`
(return `0x2ae94`), with application type 1. Shutdown has no promised result;
its incidental register contents must not be interpreted as a token or success
status. The two-argument legacy `VR_InitInternal` only supplies a null x2 and
forwards through the public Init2 slot.

The existing 2032-byte role-2 descriptor serialization independently yields:

- Full descriptor fingerprint:
  `9ef296865d2583cd8136303727db150f6e4a349ffe7b175e3e0f651adfb81dcf`.
- Executable aggregate digest:
  `9d029872ff7b6c352906bfb0b9c3687e5b44e7f8aeae1954c5c752bc237227c6`.
- Mapped span 331,064 bytes; read-only file-backed bytes 253,748.

The implementation must pin this complete metadata/code descriptor, not accept
an arbitrary profile merely bearing the expected whole-file SHA label. The old
API's `0x5ab8`/`0x5680`, loader `0x5740` and helper `0x8fe8` are ineligible for
this receiver.

## Natural client initialization, failure and retirement

| API location | Source-proven action |
| --- | --- |
| `0x57a4` | Init calls `VR_LoadHmdSystemInternal` at `0x53f0`. |
| `0x5574` | Loader calls `SharedLib_Load` at `0x8a68` using the native runtime path. |
| `0x8a7c` | Helper calls public `dlopen`; return `0x8a80`, flags **`0x0a`**. |
| `0x5588` | Resolves `VRClientCoreFactory` through helper `0x8c00` and public `dlsym`. |
| `0x55a4` | Calls the factory for `IVRClientCore_003`; return `0x55a8`. |
| `0x57c8` | Calls the returned core's initialization slot 0; return `0x57cc`. |
| `0x5374` | Shutdown calls core cleanup slot 8. |
| `0x538c` | Shutdown unloads through `0x8c08`, which forwards to public `dlclose`. |

The native path combines the registered runtime with `bin/linuxarm64/vrclient.so`.
Client ELF initializers run inside this natural synchronous `dlopen`, before the
factory lookup and public Init2 return. The unchanged positive constructor chain
remains client `0xa924c` → `0x1b9438`, HMD `0x1b9578` → `0x244da8`, and pipe
`0x244db8` → `0x259c38`. The platform helper restores the original native frame
and tail-forwards `pthread_mutex_init` at `0x259c70`; its incoming return remains
`0x244dbc`. Discovering the client after `dlopen` returns misses this birth.

The API's own initializer array contains only CRT bookkeeping and the iostream
initializer. Its OpenVR static initialization functions return directly; its
native constructor path does not call Init2 or load the client. No observer-forced
load or preconstructor API invocation is needed. This is a bounded native-source
finding, not a guarantee about foreign profiler hooks, unsupported application
calls or previously loaded clients; those still require conservative refusal.

Successful Init increments the 32-bit API counter at `0x57dc`, stores it at
`0x57e0`, and returns it. Load or core-init failure returns zero, unloads the
handle and clears the native core/handle slots. The loader has explicit nonzero
errors 100, 101, 102, 104, 105 and 110 for its failed path/directory/library/factory
checks. Native mutex failure can throw; unwinding must retain original behavior
and abandon provisional evidence. Shutdown clears the core/handle and increments
the same counter at `0x53a0`/`0x53a4`, including the no-core path. This wrapping
API counter is not a resource, socket, video-frame or namespace generation.

## DEEPBIND is a real observation blocker

Flags `0x0a` mean `RTLD_NOW | RTLD_DEEPBIND`, not the earlier API's plain
`RTLD_NOW`. The client performs eager dependency binding with its native deep-bind
lookup order. Public-preload mutex aliases alone therefore do **not** establish
that the client calls the observer's birth veneer. The current preload-only birth
path must not be deployed on the assumption that it intercepts these calls.
Keep the native flags unchanged.

The next bounded correction is selective public `LD_AUDIT` binding observation:
only the actual client reference cookie, authenticated libc definition cookie,
exact native init/destroy symbol/RVA and prepared main-namespace mutex veneer
may redirect. Early, unsupported, `DLSYM`, alternative-value or foreign bindings
remain original/ineligible. The actual client mapping and live constructor stack
must still pass the independent module/stack verifier before scalar capture.
This adds no PC sender hooks, client private detours or extra resource proof.

## Evidence and limits

The private review passes **114** bounded exact-file identity, ELF/export,
instruction, independently decoded branch, import-version, initializer and
loader-string checks. It independently derives the new role-2 descriptor and
rechecks the unchanged caller chain. Private validation source/results are in
ignored `build/private-receiver-steamrt-api-review`; regenerable disassembly
scratch is removed after review. No native binaries, instruction listings or
shader data are tracked.

These checks prove the named static route only. Actual audit binding, preserved
native forwarding, runtime birth visibility and cross-endpoint/resource/source/
UV/GPU association remain separate gates. They do not establish working Steam
Link passthrough or approve another hardware run.
