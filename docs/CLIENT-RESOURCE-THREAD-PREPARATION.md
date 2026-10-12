# Opt-in cold decoder-thread preparation

## Implemented boundary

The [actual resource census](CLIENT-RESOURCE-CAPTURE-20261012.md) established
that resource calls do not own the startup thread's cached stack. The opt-in
adapter prepares the actual decoder worker **once, before its first
native event wait**, following [110 retained native source checks](CLIENT-RESOURCE-THREAD-PLAN-20261012.md).
It does not call stack-discovery helpers from resource locks, import callbacks
or subsequent hot wait iterations.

An append-only, hidden receiver-to-libc audit route admits the original
`epoll_wait@@GLIBC_2.17`, native RVA `0xe9e10`. Actual `la_preinit` admission
arms the source gate. Wrong/eager/preinit/DLSYM/ALTVALUE binding, missing first
history, a skipped first preparation, changed source entry, another thread,
recursion or retirement permanently refuses acquisition. Native calls still
forward unchanged. No public all-process `epoll_wait` replacement is exported.

The true entry must match receiver return RVA `0x328d0`, its 336-byte frame,
original FP/SP, event-buffer registers and original signed integer arguments.
Only then do the already authenticated POSIX thread helpers obtain/destroy
current-thread attributes. The bounds must include the real current marker and
original worker frame. Bounds are not guessed from SP or queried through a
borrowed pthread handle on another thread.

This is explicitly cold work. The native attribute implementation can lock or
allocate; it is not advertised as universally free of those operations. The
observer adds no file I/O, packet/pixel reads or new event wait at this boundary.
The original event wait retains its arguments, event output, result, errno and
cancellation/forced unwind.

The process-lifetime gate publishes immutable bounds and uses **bit 8 of the
existing marker byte**, adding no TLS. Use requires that exact current thread,
fresh marker and owned stack; address reuse cannot inherit the registration.
Existing `startup_thread_stack_owned` observations remain unchanged. New
`resource_thread.stack_owned_current` records and historical footer
`resource_thread_stack_prepared` identify this separate capability. Historical
preparation is not permission for a background reader or another thread.

The six resource observations remain register-only. No resource transaction,
namespace, source video/eye, UV or GPU association is accepted. Native rendering,
the PC Steam sender and mask activation remain unchanged.

## Exact reviewed candidate

Enable `FRAME_MASK_CLIENT_RESOURCE_THREAD_PREPARATION` together with the
existing resource-census and connect-observation build options. The artifact
contains only the new hidden, read-only non-executable `FCSTAK01` scope marker,
schema **4**, envelope size **11,800**. Older modes cannot silently adopt it.

| Own artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Main ARM library | 354,064 | `1eb29432bfe3e78f594eb88e24013a6969bb94f0c2babd78f1e1202bb474afe7` |
| Dependency-free audit | 69,080 | `3ab7920a449011fec1f047be6a52d66adeecbd46591741d94466a2e7c02502d7` |
| Envelope | 11,800 | `7fd69f7c0b15acefe331e7c91845201be5744a0366221b04d188f64299253cd0` |

The offline plan hash is
`0a1ef852e5e60e528b709ecaaa9cb541f56757c54bef2e9b090435a04297eedb`.
The compiled audit preparation is 200 bytes, with five relative string fixups
and six pinned libc RVAs. Main retains **18-byte static TLS**, one own public
bridge and eight private entry veneers. Native profiles/providers and client
`RTLD_NOW|RTLD_DEEPBIND` flags remain unchanged.

The proposed eight own inputs total **555,372 bytes**, including both exact
session helpers and the corrected external kernel helper. Private native
reference files are not distributed or installed.

## Focused software evidence

- **425** first-entry/current-thread controls pass normally and under ASan/UBSan,
  including real pthread bounds, reused addresses with fresh markers,
  overlapping preparation, invalid geometry, failed preparations and late arming.
  Native-stack tests disable ASan's artificial return stack, without weakening
  production ownership guards. The new core object has no TLS or I/O/query
  imports; the root supplies the explicitly cold POSIX query.
- **16** actual audit-core ELF cases pass for receiver-only routing, first-load
  misses, wrong providers/history, native arguments/results/errno and unchanged
  unrelated callers. Two affected existing connection cases and three private
  source/scope refusal controls also pass.
- The exact final ARM ELF passes **39** native-provider/entry checks under QEMU,
  including real epoll event delivery and blocked cancellation with C++ RAII
  unwind across the own DSO/assembly veneer. Three unrelated-process inert-load
  cases pass **29 controls each** with the actual pinned Frame libc/loader.
- Six focused preparation and thirteen session controls pass. Two additional
  private-input preparation cases and actual session-envelope validation use
  the exact final artifact and real canonical references, without fixture pins.
- Two affected root readiness/inert CTest cases pass. Independent final review
  passes **212** artifact/bundle checks and **22** explicit source invariants.

A missing `<cerrno>` include was caught in the first ARM compile. Only that
bounded ARM build was repeated after the repair; already passing host checks
were inherited. CI then caught a direct-assembly checker restriction: the
shared entry source must remain free of C preprocessing. The selector now lives
in the separate connection wrapper, while the shared source uses assembler
conditionals. All four affected ABI checks passed after that repair. Only the
ARM artifact was rebuilt; its executable, unwind, TLS and data load bytes are
identical to the preceding candidate. The final new ELF again passed the 39
native epoll/forced-unwind controls; unchanged inert-load evidence is inherited
by that verified equivalence. The later CMake change only registers the new audit CTest and
does not alter artifact build flags/source. Debug stripping preserved program
headers and load/executable bytes, apart from ELF section-table bookkeeping.

Unchanged prior census/connect and helper evidence is retained. These checks are a
focused integration, not a new complete local gate. The subsequent
[actual Frame capture](CLIENT-RESOURCE-THREAD-CAPTURE-20261012.md) separately
confirmed current-thread ownership at all six observed resource boundaries,
followed by a terminal ownership refusal; it does not prove lifetime continuity. Generated fixture/compiler trees and
worker checkouts
were removed after their processes stopped; small receipts and exact current
deliverables remain private.

## Separate runtime scope and recovery

Preparation requires `--connection-observation --resource-census
--resource-thread-preparation`. The session helper additionally requires
`--ack-resource-thread-preparation-scope` alongside the three preceding scope
acknowledgments and coordinated restart readiness. A reviewed plan or passing
test supplies no Frame deployment permission.

The proposed recording remains **32 records, 64 KiB and 120 seconds**, plus at
most four external kernel metadata queries in two seconds with a bounded scan
of one exact compositor process. The deadline ends recording only; no automatic
rollback or Steam restart is scheduled. Genuine first-wait preparation has now
been tested in
[the separately authorized Frame run](CLIENT-RESOURCE-THREAD-CAPTURE-20261012.md).
The temporary diagnostic was fully removed. Fresh native/service checks remain
mandatory before any future deployment.

Only the own user-directory inventory and the matching runtime Steam user-service
override may change. With coordinated rollback readiness, the staged
`client_startup_session.py rollback --ack-steam-client-restart-readiness`
restores original Steam. Archive exact inventories/capture/recovery receipts,
verify native/environment/service hashes and affected process mappings, then
remove only the matching owned files. No game, native binary, system image,
shader or PC Steam sender modification is part of this proposal.

Hardware confirmed current ownership during the first captured resource trace;
the next separate scope can acquire the minimal source-checked sparse
transaction fields in
[the resource integration plan](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md).
Until exact connection/resource/video/eye/UV/GPU association is established,
the explicit-mask output remains opaque and streamed genuine-mask passthrough
is not available.
