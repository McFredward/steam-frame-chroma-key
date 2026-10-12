# Selective OpenVR startup forwarding

The viable user-directory route is a small **`LD_AUDIT` selector plus a main-
namespace observer**, configured in the existing Steam user-service environment.
The selector redirects only already resolved native OpenVR bindings and,
when explicitly prepared, source-client mutex bindings to authenticated libc. Neither
library exports `VR_InitInternal2` or `VR_ShutdownInternal`, depends on
`libopenvr_api.so`, or forces the client to load. Missing APIs therefore remain
missing in unrelated Steam children. Native launch scripts and binaries stay
untouched. This is implemented and exercised offline; nothing is deployed and
no additional Frame write/restart is authorized by these results.

## Actual provider and initialization order

The retained SteamOS 0.4.5 receiver is SHA-256
`31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153`.
Its `DT_NEEDED` names `libopenvr_api.so`, but not `vrclient.so`; both startup
functions have real AArch64 `JUMP_SLOT` relocations. It has `PIE`, but no
`BIND_NOW`/`NOW`. The read-only live preflight located its actual API in
`steamrtarm64/libopenvr_api.so`, SHA-256
`4a8723b7134c53ec6c9291d9d2f55ea86349ffd0d227d10ae49a4951ae5395d4`.
The earlier `/opt/steamvr` API is a different compositor-side provider and is
ineligible for receiver startup preparation:

| Public function | API-relative address | Bytes | Native ABI |
| --- | ---: | ---: | --- |
| `VR_InitInternal2` | `0x5768` | 236 | u32 token; error-output pointer in x0, 32-bit application type in w1, startup string in x2 |
| `VR_ShutdownInternal` | `0x5330` | 192 | no arguments, void result |

These unversioned public providers are naturally mapped as receiver dependencies
before its normal initialization call. Their availability is not inferred from
our wrapper. The [actual receiver API audit](RECEIVER-STEAMRT-API-20261012.md)
rechecks native startup, failure and retirement with 114 static source checks;
the unchanged client constructor proof comes from the
[existing constructor audit](IMPORT-CLIENT-BOOTSTRAP.md). Original init loads
`vrclient.so` with **`RTLD_NOW | RTLD_DEEPBIND`**. Its pipe is born inside client
ELF initializers before that `dlopen` returns. Public preload aliases alone can
miss this binding. Selective source-client-to-libc audit forwarding retains the
native flags and caller entry. Late mapping adoption cannot repair a missed birth.

The main observer prepares once, outside audit/render/import callbacks: actual
receiver/API/libc/loader/own-observer mappings, owned file identities, stack
preparation, native aliases and both real OpenVR function pointers. Constructor-
time queries must never force API/client loading. `dlsym` is a preparation step,
not a lookup in every redirected call. A prepared actual main-namespace loader
reader must do mapped verification in that namespace; executing it in the audit
namespace would enumerate the wrong modules.

## Implemented selector and copied handshake

`import/import_startup_audit.{h,cpp}` supplies only public loader-audit exports.
A hidden weak `preparation()` returns null by default, making the DSO inactive.
A separate own-source preparation TU can supply immutable module loader names,
source-prepared RX intervals and function RVAs. Names/RVAs alone are candidate
filters: the main observer must validate its already prepared actual mapped
identities before acknowledging the handshake.

1. `la_objopen` accepts the initial namespace only and records bounded,
   atomic module slots: candidate main executable, API, and owned observer,
   plus explicitly prepared client and libc slots for the mutex route. The
   main executable's loader name is normally empty; that is **not executable
   identity**. Fresh executable and API checks belong to main preparation.
   The public loader bias plus prepared relative intervals yields copied views;
   no native heap, private loader structure or guessed Valve pointer is read.
2. Actual `la_preinit` calls the known hidden main-observer bridge, using its
   source-prepared own RVA and RX bounds. This occurs after initial relocations
   and constructors. `Stage::Prepare` carries the views and missed-binding flag.
   The callback only checks cached proofs and attaches positive observed-open
   generations; it does not resolve providers, read files or reenter the loader.
3. An eligible `la_symbind64` must have the exact main-reference and API-definition
   cookies, the supported symbol name, and the actual `sym->st_value` equal to
   the prepared API bias plus function RVA inside its RX interval. `Stage::Bind`
   passes that **already resolved original address** to main code once per
   function. The main callback compares it to its cached real provider and
   returns the corresponding known hidden entry. The selector rejects an entry
   outside the owned observer RX interval and caches accepted results.
4. During natural client `dlopen`, eligible mutex bindings require the exact
   client reference cookie, libc definition cookie, public symbol and resolved
   native export RVA (`0x86a00` init; `0x86980` destroy). The copied views must
   match cached authenticated main-namespace mappings and open generations.
   MAIN returns its known hidden mutex entry; unsupported bindings remain native.
   The actual client code and live constructor stack are verified separately by
   the source Gate, not inferred from these scalar audit cookies.
5. The original API call runs through a hidden main-namespace true-entry veneer
   and the actual main-namespace provider, preserving arguments, token, error
   output, errno, unwind and caller continuation. The init scope prepares the
   actual calling thread before admitting source-supported constructor birth.
   All failure/unwind paths close the startup gate. A callback that is refused
   leaves the loader's original bound address untouched; no fake native failure
   or substitute token is returned.

`BridgeRequest`, `BoundProvider`, `ModuleView` and `BridgeResponse` are copied
POD values, never retained pointers into the audit namespace. Open generations
label actual `la_objopen` lifetimes, not file-hash trust, source video frames or
GPU ownership. Bridge callbacks must preserve **main-namespace errno** themselves;
using audit-local errno/TLS would preserve the wrong state. The audited module
has no dynamic provider imports and uses inline lock-free atomics, not an
additional audit-local libc or C++ runtime.

The selector sets `LA_SYMB_NOPLTENTER|LA_SYMB_NOPLTEXIT` on accepted bindings.
Own-source real ELF fixtures confirm the exact incoming caller return address
on both x86-64 and AArch64, including the supplied actual Frame loader under
QEMU. This matters: a successfully returned alternate symbol alone would not
prove that an audit PLT trampoline preserves the receiver's original LR.

## Refusal and lifetime boundaries

- Constructor/eager bindings before `la_preinit` retain the original address and
  latch missed proof. A `NOW` receiver or inherited `LD_BIND_NOW` cannot use this
  startup route. Do not silently change native binding mode or patch its GOT.
- `LA_SYMB_DLSYM` queries always retain the original address. This includes the
  main observer's early preparation queries; they are not missed init calls.
  Other references/definitions, namespaces and symbols are not redirected.
- Wrong executables cannot become ready merely because their name or dependency
  resembles the receiver. Missing APIs create no binding to intercept, and no
  library exports those names to make a missing `dlsym` probe succeed.
- Duplicate module lifetime, conflicting/busy binding, stale provider or bad
  bounds lose proof without blocking native continuation. The selector never
  calls the original provider in the audit namespace and never interposes
  `dlopen`, `execve`, loader search, or native script execution.
- A first sticky audit loss after MAIN has admitted preparation sends an immediate
  copied `Stage::Loss` notification. MAIN retires provisional evidence even if
  an earlier eager mutex slot was already redirected. That slot is not unbound;
  its existing veneer continues forwarding the real provider and original errno.
  This matters when destroy binds successfully before a later init binding fails.
- An optional client slot is an explicitly prepared client loader name/RX
  profile. Its real `la_objclose` sends `Stage::Retire` with a copied closing view
  only after the main bridge was admitted. Main code compares that view to its
  known client/module lifetime and atomically retires it before unmap. Unknown
  unrelated modules do not poison the trace. If the actual client name/geometry
  does not match the explicit profile, production capture must remain unavailable;
  the optional field does not imply all unload paths are already observed.
- `la_objclose` occurs **after finalizers**, before unmap; the fixtures check that
  order. It does not establish pre-finalizer retirement. Known public shutdown
  therefore still retires gate evidence **before** original cleanup/unload.
  Finalizer callbacks on an otherwise unsupported close route need their own
  source/lifecycle validation. Reloads are not silently adopted.

The client-to-libc scope is all-or-none. It does not alter the receiver's lazy
API imports, the client's eager deep binding or other modules' lookup order.
The main observer's distinct versioned libc aliases are authenticated before
activation; no audit-namespace libc dispatch or replacement native token is used.

## Focused evidence and remaining integration

`tests/test_import_startup_audit.py` builds only controlled own-source ELF
fixtures and cleans source/cache/binaries after completed or failed runs. It
passes ten real processes on the host and ten under AArch64 QEMU with the pinned
Frame libc/loader: both bindings, natural client constructor during the init
scope, caller LR, original arguments/error/token/errno, provider exception
cleanup, finalizer/retire order, wrong process, absent APIs, eager `NOW`,
`LD_BIND_NOW`, constructor binding, inactive preparation, weak inactive default,
unchanged `dlsym` pointers and two semantic negative controls. Host child exec
also retains missing API availability. ARM child exec is not claimed: QEMU is
not registered as a host binfmt handler.

The new `--deepbind` subset separately passes eight host processes and eight
AArch64 processes with the retained actual Frame libc/loader. These include the
preload-only miss, positive eager client/libc redirect, wrong client/provider,
partial preparation, early client, invalid owned entry and unchanged `dlsym`.
ARM executes the shared production mutex assembler and real versioned libc
forwarder. A platform-specific relocation-order negative control confirms that
an already redirected earlier mutex still forwards native behavior while the
later rejection immediately retires evidence. Only that bounded repaired case
and previously unrun cases were rerun; one focused ARM legacy case confirms the
original API-only preparation. No full legacy matrix is claimed for this update.

The audit fixture uses its own C++ main wrapper and API/client DSOs. A separate
`tools/run_import_startup_native.py` fixture now executes the two production
true-entry veneers with the actual startup forwarding core, under AArch64 QEMU
and the pinned Frame libc/loader. Its 83 checks reuse the existing executed
register probe: original general registers/SP, preserved SIMD registers,
32-bit token and error output, errno, native exception and pthread forced-unwind
cleanup. It uses controlled API providers, not native OpenVR initialization.
Neither fixture has yet executed the **combined production audit/veneer/runtime
path**. Explicit QEMU/provider paths opt this test into CTest; private inputs
are neither downloaded by the runner nor published by CI.

The real constructor/runtime/writer path is now integrated as the opt-in
`FRAME_MASK_BUILD_CLIENT_STARTUP` ARM64 target. Its sealed cold preparation,
actual-thread startup Gate, original mutex veneers, selective audit handshake,
bounded records and manual-session proposal remain uninstalled. The source
contains no public OpenVR export or forced API dependency. The two public mutex
aliases share the exact original entry PCs, while their actual-provider
`__pthread_*@GLIBC_2.17` imports stay distinct. Missing setup is transparent:
those dispatchers forward unconditionally and their registration atomics are
constant-initialized, not constructor-dependent.

`tools/run_client_startup_preload.py` executes the final ARM64 observer ELF under
QEMU with the pinned native libc/loader: three unrelated-process cases (missing,
invalid and FIFO input), 20 checks each, real public/compatibility-version
provider mapping, mutex results/errno, inactive bridge and unchanged missing
OpenVR presence. This is actual final-library forwarding evidence, **not a
production Ready/native OpenVR or Frame-kernel test**. The complete positive
audit/veneer/runtime path still requires that native initialization.

The current runtime consumes init notifications for the source constructor
birth only. Authenticating/forwarding the destroy binding does not establish
observed pipe destruction or a continuous socket/import lifetime. Never promote
the recorded local birth token into later cross-process or GPU evidence without
the corresponding source/lifecycle adapter.

The first actual Frame capture now confirms cold preparation, audit attachment,
the receiver's real Init entry, both deep-bound native mutex providers and the
authenticated client mapping. It did **not** confirm the target birth: an earlier
legitimate embedded pipe uses the same mutex return site and consumed the old
candidate filter's discovery attempt. The bounded register-only rejection and
independent constructor-order proof are described in
[CLIENT-STARTUP-NATIVE-INSTANCE-20261012.md](CLIENT-STARTUP-NATIVE-INSTANCE-20261012.md).
The subsequent [coordinated native capture](CLIENT-STARTUP-CAPTURE-20261012.md)
confirms the intended pipe's successful mutex initialization and the original
public OpenVR init result. Both diagnostics were manually removed; the original
service configuration and affected process mappings were verified after each
coordinated restart. This closes the local startup-instance question, without
establishing continuous connection, namespace, video or GPU ownership. No PC
sender hook or scene-pixel change was introduced.

`tools/prepare_client_startup_observer.py` now produces only an offline reviewed
candidate bundle. Loader names are explicitly expected inputs, not observed
link-map lifetimes. Root must verify the actual current launch environment and
obtain permission before any changed runtime instrumentation. Native
constructor discovery, binding absence/presence, startup thread ownership and
close behavior still require a scoped Frame run. No headset/kernel/GPU proof,
source-frame association or working native-mask passthrough is claimed.
