# Cold preparation of the native resource thread

## Result and recommended boundary

The authentic resource census copied all six Begin/Pending/Reference records,
but reported `startup_thread_stack_owned=false` for each one. Preserve that
hardware fact: the initialization thread's prepared stack cannot authorize
this resource caller's private stack acquisition.

The smallest source-supported next adapter is a **one-time cold preparation at
the decoder worker's first native event wait**, before that worker handles an
event or creates imported resources. Reuse the existing audit bridge and entry
snapshot; add only a hidden receiver-to-libc `epoll_wait` route. This avoids
changing native `pthread_create` arguments, substituting a C++ thread state or
depending on an unretained Frame `libstdc++` implementation.

An independent read-only audit passed **110 native hash, instruction, relocation,
call-order and provider-export checks** for the retained receiver and libc.
Private receipts remain under ignored `build/source-thread-plan/`. No Frame
access, network request, native execution, live-memory read or rendering change
was performed. This is a concrete implementation plan, not an installed adapter
or a new hardware proof.

## Exact native route

Retained inputs are receiver `vrlink`, SHA-256
`31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153`,
and actual Frame libc, SHA-256
`be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b`.
All offsets below require those complete native identities and current MAIN
audit-open/module lifetimes.

| Source boundary | Established route |
| --- | --- |
| Decoder worker creation | Receiver call `0x33734` invokes `std::thread::_M_start_thread`, returning at `0x33738`. A native 16-byte state has vtable address point `0x248cc0` and captured decoder at +8. |
| Decoder `_M_run` | The relocation at `0x248cd0` points to receiver `0x32e30`; this thunk loads the captured decoder and tail-enters `0x32878`. |
| Worker entry | `0x32878` allocates 336 bytes, sets FP = SP and retains the decoder in x19. There is no preceding BL in this source function before its event wait. |
| First event wait | `0x328cc` calls receiver `epoll_wait@plt`, return **`0x328d0`**. x1 = SP +120, x23 = SP +120, w2 = 1, w3 = -1. Native loads the actual epoll FD into w0; no observer descriptor/heap read is required. |
| Resource event work | The later event branch calls `0x32328` at `0x3299c`; it calls `0x34740` at `0x324fc`. This invokes the native import frame `0x342d0` through `0x34844` or `0x34938`, then receiver's ImportDmabuf at `0x34604`. |

The worker loop returns to the **same wait site repeatedly**. The call site alone
does not distinguish cold entry from a later hot iteration. The first-boundary
admission described below is mandatory; a mutable ready flag, first matching
counter after arming or plausible current SP is insufficient.

The receiver also has five direct calls to `pthread_create@plt` at `0x384dc`,
`0x6b5f8`, `0x6b658`, `0x6e764` and `0x6e824`. They are other source routes;
the decoder path demonstrated above uses the C++ thread starter instead. Its
third argument includes a dependency pointer loaded from receiver GOT
`0x24da00`, relocated to `pthread_create`. That pointer does **not** prove that
the native C++ library invokes it. No exact Frame `libstdc++.so.6` is retained;
a host/sysroot copy cannot close that gap.

## Original provider, ABI and cancellation

| Actual Frame libc symbol | RVA | Bytes |
| --- | ---: | ---: |
| `epoll_wait@@GLIBC_2.17` | `0xe9e10` | 20 |
| `epoll_pwait@@GLIBC_2.17` | `0xe9c20` | 248 |
| `pthread_create@GLIBC_2.17` / default `GLIBC_2.34` | `0x84140` | 3,592 |
| `pthread_getattr_np` | `0x85140` | 976 |
| `pthread_self` | `0x8b090` | 12 |
| `pthread_attr_getstack` | `0x816d0` | 40 |
| `pthread_attr_destroy` | `0x81420` | 56 |

Neither `__epoll_wait` nor `__pthread_create` is an exported distinct alias in
this actual provider. Do not invent a symver alias or replace the original
cancellation-aware function with a raw syscall.

Cold MAIN preparation can establish an immutable function pointer to the
**original public `epoll_wait` provider PC**, validate its exact RVA/current
mapped libc and retain it before activation. The audit binding must independently
present that same original PC, exact receiver reference and exact libc definition.
Forward through the cached native pointer; export no public `epoll_wait` symbol.
Other receiver epoll calls and all unrelated Steam/UI bindings retain native
behavior without resource preparation.

The actual 20-byte `epoll_wait` wrapper supplies a null signal mask and
tail-forwards to `epoll_pwait`. The latter contains native cancellation-entry
and restoration calls in its multithreaded route and preserves normal error
return -1/errno handling. Forward all original arguments exactly, including
the timeout's signed 32-bit representation, and return the original result and
errno. The own dispatcher must **not** be `noexcept`; forced unwind must cross
its correct CFI. A missing After due to cancellation cannot fabricate completion.
Native output event bytes remain untouched and unread.

## Genuine first-boundary admission

Implement this as a separate opt-in preparation mode, with these obligations:

1. Use only a fresh receiver process started with the reviewed preload/audit
   configuration. Retire/refuse inherited processes, late injection, an existing
   receiver at activation, audit slot reopen, changed native hash or lost callbacks.
2. Require actual `la_preinit` admission of the sealed MAIN preparation. This
   loader event establishes completed observer relocation/constructors before
   entering native executable code; constructor priority is not a substitute.
   The receiver's source creation `0x33734` is an ordinary runtime method, whose
   worker starts the `0x32e30 -> 0x32878` body. Do not claim that source review
   alone rules out every indirect initializer/thread-creation route.
3. Extend audit tracking with an append-only receiver-to-libc epoll binding and
   immutable original provider. A binding observed before `la_preinit`, with
   DLSYM/ALTVALUE, an already adopted/rebound slot, missing first-load history or
   wrong MAIN reference/definition must not supply cold admission. Keep the
   existing `missed_before_preinit`/loss refusal. Eager or unsupported binding
   may safely make this diagnostic unavailable; do not reinterpret it as late
   permission to inspect a running thread.
4. Source-specific first-wait eligibility must be coupled to that admitted
   binding generation and the fresh per-thread invocation marker. Once the
   reviewed native slot routes through the own veneer, **every** call still
   forwards natively, and its exact source wait is accounted for even if
   connection/resource readiness has not yet arrived. An ignored first wait
   must set a permanent missed/unsupported state for that thread. A later loop
   iteration cannot become its substitute.
5. At that first genuine source entry, require LR = receiver bias +`0x328d0`,
   true-entry SP = FP with the source 336-byte frame geometry, x1 = x23 = SP +120,
   maxevents 1, timeout -1 and unchanged receiver/module/audit ownership. These
   are scalar checks only. No generic SP-derived stack extent or frame walk is
   allowed. Refuse any changed/nested eligible route or unexplained missing
   Before/After history; record the reason and continue original behavior.
6. A uniquely claimed process-lifetime registration slot may admit only this
   thread's first source attempt. Source-attempt state is historical evidence
   derived from actual audited entry delivery, never a caller-set `first=true`
   or `ready=true` parameter. After preparation has failed or was unavailable,
   later waits remain native and inert. The background reader must distinguish
   never observed, first wait missed, attempted failure and actual prepared state.

The source and audit timeline make a valid first event wait useful, but the
implementation must supply negative controls for each missing-history condition.
If this first-load/binding history cannot be established in the native run,
the cold path remains refused; obtain a bounded creation/binding census or the
missing native C++ library evidence instead. Never compensate by calling
`pthread_getattr_np` from the first later ImportDmabuf lock callback.

## Cold current-thread stack preparation

At the admitted first wait, before forwarding the native event wait, call the
already source-verified original `pthread_getattr_np(pthread_self(), ...)`,
`pthread_attr_getstack` and `pthread_attr_destroy` **once**. Validate these cached
original provider addresses during cold MAIN preparation, not with hot lookup.
This is an explicit cold decoder-entry exception, prior to event handling; no
lookup, attribute query, allocation, observer I/O or new wait is introduced in
an import/lock/render callback or subsequent wait iteration.

Do not describe the libc attribute query itself as universally lock-free,
allocation-free or I/O-free. Its actual implementation acquires native thread
metadata protection, can allocate affinity metadata and has an alternate
main-stack discovery path. Those operations belong only to the explicitly
approved cold preparation. The ordinary source-supported child worker has its
glibc-allocated thread stack; unsupported caller/lifecycle routes remain refused.
The observer adds no file I/O or socket wait of its own at this boundary.

Use the returned usable stack interval, overflow/size/alignment checks and the
existing `OwnedThreadStack::prepare_current`, whose actual local marker must lie
within it. Require the original source entry frame to lie within this independently
obtained region as a check, never as the basis for inventing bounds.

Publish success only after all native attribute calls have succeeded and their
owned attributes are destroyed. Save/restore incoming errno around preparation
before calling the unchanged epoll provider. On refusal, forward immediately
without suppressing native decoding or replacing its success/failure result.

Reuse the existing 18-byte TLS footprint: add claim **bit 8** to the existing
marker byte, leaving bits 1/2/4 for connection/resource/startup unchanged. Tie
bit 8 to this real cold preparation and the atomically claimed permanent slot;
do not set it merely because a register matches. The stack capability is used
only on that still-current thread with the fresh cookie and actual prepared
object. A newly created thread reusing the same TLS/stack address starts with
cookie zero and must not inherit it. Refuse replacement rather than recycling
a prepared slot into another lifetime.

This owns use of the cached bounds **during that executing pthread's callbacks**;
it is not a global pthread join/detach lease. No background reader may use those
addresses as memory permissions. Global finalization/retirement touches atomics,
never another thread's mutable acquisition scratch. Actual thread cancellation,
exit, missing completion and fork must leave private acquisition unavailable.

Keep `startup_thread_stack_owned=false` in the existing census when appropriate.
Add a distinct `resource_thread_stack_prepared` fact and matching local owner
label; do not rename the old false observation or present the new cookie as a
socket/resource/video generation. This enables the later sparse stack acquisition
described in the [resource integration plan](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md),
with every exact route/window/ownership check still required.

## Alternatives and why they are not the next implementation

* **Creator-after-`pthread_create` query:** the returned handle/default joinable
  attributes do not own a live thread until `pthread_getattr_np` finishes.
  Concurrent detach/join/exit and descriptor reuse invalidate that assumption;
  the child can also import before the parent returns. No complete lifetime
  adapter or exclusive native join owner has been established. Do not ship it.
* **Native child-start wrapper:** it could prepare before the original routine,
  but deliberately substitutes `pthread_create`'s start-function and argument.
  The original routine may still receive its original argument later, which is
  distinct from claiming the forwarded native arguments were unchanged.
  Exact creation caller, actual original provider, state ownership, child/parent
  race, cancellation/unwind and reclamation would need independent review and
  separate scope. The decoder's C++ starter currently lacks actual Frame
  `libstdc++` provider evidence, so its ABI must not be assumed.
* **Native TCB/private stack-field copy or current-SP guess:** neither is the
  current approved acquisition route. Do not replace a missing POSIX stack
  capability with guessed private offsets or apparent readable mappings.

## Concrete implementation and validation

Root can implement the bounded cold-bootstrap core and registration in
`receiver/client_startup_runtime.*`, with a small independently tested helper
for first-entry/thread-cookie admission. Extend the existing true-entry macro
and forwarding descriptor for an appended hidden `epoll_wait` entry; preserve
all legacy modes/CFI. Extend `import/import_startup_audit.*` for the receiver-only
binding and its immutable original provider, and keep all client-to-libc
connection/resource callbacks unchanged.

The preparer/session helper need a **new explicit marker/schema and scope
acknowledgement**, retaining the exact helper/native/artifact pins, hidden-only
exports, bounded diagnostics, manual recovery and no timed Steam restart. A
future combined artifact may include sparse resource acquisition, but list
that private read budget separately; cold stack preparation itself does not
authorize wire/stack transaction bytes.

Focused controls must exercise genuine before-main loader preparation, lazy
first binding, preinit/eager binding refusal, missing first source entry,
early/not-ready first wait followed by a later valid-looking wait, unrelated
UI/receiver epoll calls, wrong provider/version/module lifetime, negative native
results, errno propagation, failed attribute calls, actual thread/TLS address
reuse, overlapping registration and native cancellation with no After. Confirm
the **actual final ARM ELF** retains 18-byte PT_TLS and only the own bridge export;
all unselected public providers must still resolve to original libc. Verify
epoll forced unwind and cancellation against the actual pinned providers.

The existing completed resource census and focused checks can be inherited for
unchanged areas. The new first-wait/stack capability and actual provider/entry
must get their own evidence; a test-created marker is not a hardware thread
birth. Cleanup generated native disassembly/build caches after processes stop,
preserving small receipts and exact deliverables.

Remaining proof flags stay false: uninterrupted FD ownership, kernel endpoint
lifetime, cross-endpoint resource namespace, selected video frame/session,
eye/extent, native UV/reprojection/foveation association, accepted GPU route and
coherent consumption. This step prepares safe native acquisition; it does not
activate mask compositing or establish working Steam Link passthrough.
