# First compositor raw candidate: source and capture review

The latest diagnostic consumed its single candidate during Frame startup,
without an operator-confirmed PC-VR stream. It correctly refused that candidate
before observing an image or reply. This is not evidence that the compositor
exited, nor that the raw refusal caused the blue Frame interface. The separate
`vrwebhelper` loader failure reports insufficient static TLS for `libcef.so`.
Those two failures require separate repairs.

This review uses the retained `compositor-raw.jsonl`, `compositor-journal.jsonl`
and native service logs from the failed-start run, plus source at `2700aa6`.
A subsequent bounded evidence-format repair is described below. Neither the
review nor that repair performs Frame access, additional native acquisition,
shader activation or sender modification. Full native records and instruction
listings remain in ignored private evidence.

## What the raw result establishes

The raw header and eight ordered records belong to the reviewed current
compositor process. They show prepared/attached instrumentation, authenticated
lock/unlock/write forwarding providers, one staged Begin and successful thread
admission. The source-specific Begin is the pinned DMA-BUF import handler; this
is stronger than merely seeing the first generic IPC poll. It is still not a
selected Steam Link video image.

The final raw record establishes:

- Successful Begin lock, successful early unlock, and the native payload-read
  result true for the retained invocation and sequence.
- Exactly **24 scalar bytes** acquired: 12 at Begin and 12 at early unlock.
  The after-unlock event reuses its before-unlock copy. Together with the single
  **16-byte caller-link copy**, the actual private acquisition was **40 bytes**.
- No successful image/binding/view observation, no reply write, no reply token,
  and no local resource-journal join. The zero/default image, client and FD
  fields are unavailable results, not observed native values. In particular,
  client zero cannot be interpreted as an absent or invalid native client.
- `reason:128` is exactly `frame_compositor_raw::InvalidEntry` (`1 << 7`).
  `decoder_refusal:0` does **not** identify a successful subsequent decode or a
  decoder defect. The refusing event failed the earlier entry-route predicate.
- Footer `refusal:8` is the runtime's distinct `Refusal::Raw`. It is a different
  enum/domain from raw reason 128. `raw_terminal:true` means the one diagnostic
  scope is settled by refusal; `incomplete:false` is not a successful import.
  All eight queued records were drained and callback quiescence was confirmed.

`Observer::observe()` checks `entry_route()` before its state-specific decoded
metadata checks. `can_acquire()` performs the same source/entry checks before
any further private copy. Consequently the already successful early-unlock
transition can be followed by an entry mismatch without acquiring the next
80-byte image window or any wire bytes. That is the observed safe cutoff.

The serialized record omits `RawRecord.last_boundary`, `last_phase` and native
result fields, although the state machine retains them. Its outer `entry_lr`
and `entry_sp` are zero because `outcome()` fills the raw record without copying
the refusing event's entry into that outer field. Therefore this capture cannot
identify the exact rejected boundary/register predicate. Plausible alternatives
include an unsupported native response shape, or a Vulkan call whose actual
LR/SP/FP does not match the reviewed creation route. A forwarding loader/native
Vulkan layer may affect that entry route. **Neither alternative is proven.**
It would be premature to conclude that operator arming alone fixes the refusal,
or that the pinned Vulkan route is unsupported everywhere.

## Diagnostic termination is not compositor termination

The refusal path marks the diagnostic terminal, clears its cold-ready flag,
retires its gate and stops further observation. `import_forwarding` still calls
the real native provider and preserves its result and `errno`. Raw retirement
contains no service restart, exit, signal, native cancellation or graphics
mutation. The raw writer writes a finite footer and closes its own file.

The same compositor's native log continues from its startup at 21:58:45 until
the coordinated original-service rollback at approximately 22:00:54. It then
logs ordinary render/listener shutdown. The separate passive journal also
continues after raw retirement: 13,050 public lifecycle observations were
published and replayed without journal loss. Thus `end:"retired"` must not be
reported as a compositor crash. This does not excuse the independently observed
UI loader regression: it only separates the causal evidence.

## The second capture window also expired too early

The passive journal footer reports a reached output byte cap after about
**5.70 seconds**, 1,608 emitted events, no dropped events, no lost binding state,
and all 13,050 publications replayed. The output file is below its fixed 1-MiB
ceiling with footer reserve. The nominal 120-second deadline therefore did not
keep that journal open for a later PC-VR stream.

The current session enables draw/binding capture immediately: the journal
header has `binding_arm_required:false` and `binding_ready_at_header:true`.
Both the raw one-candidate selection and the passive draw-output budget can
therefore be spent on native startup before the useful interaction. Correcting
only one of these windows is insufficient for a later local CPU resource join.
A closed/lost journal must continue to refuse currentness; never reopen it,
reset its counters, or substitute a new journal to make an old receipt current.

## Bounded next steps

1. **Expose the existing refusal facts.** The bounded source repair now
   serializes the already retained last boundary/phase/native result and its
   availability flag. It value-copies the refusing or matched event's already
   supplied public entry into the existing queued record and emits LR/SP/FP.
   This requires no additional native acquisition or new transaction. The exact
   next rejected boundary and entry shape will be available for diagnosis;
   unchanged older captures do not retroactively acquire those fields. Actual
   artifact/forwarding review and a new capture remain required before any
   hardware conclusion.
2. **Explicitly arm candidate selection after healthy startup.** Keep providers,
   callbacks, ownership mechanisms and the journal prepared before native MAIN.
   While unarmed, skip candidate claim, thread-stack preparation and every
   private read. An owned operator signal, consumed by an existing background
   worker, can publish one lock-free permission flag. The next eligible Begin
   may then consume the same single claim and existing 16/672/21-byte limits.
   Graphics/IPC callbacks must not open/read a marker or wait for the operator.
   Keep an absolute finite recording deadline; if it expires, refuse rather
   than silently extend/reset the approved 120-second window.
3. **Preserve lifecycle replay while reserving passive output.** The existing
   binding-arm mechanism suppresses draw/submit recording before its owned
   signal while retaining public lifecycle observations. Coordinate it with
   candidate arming, or use a reviewed raw-import-only output mode which omits
   unrelated draw/submit serialization throughout this local-resource capture.
   Keep the same owning journal, capacities, 1-MiB ceiling and deadline. Retain
   lifetime metadata necessary to validate the eventual receipt.
4. **Use zero-read filtering only where source proves its meaning.** Genuine
   public-entry metadata can narrow candidate admission before the irreversible
   claim. A request size, counter, approximate time, first matching opcode or
   operator signal is merely selection policy, not proof of video/eye identity.
   The current Begin entry does not establish an authenticated receiver endpoint
   or PC session. Do not introduce a guessed peer/PID/resource pointer predicate.

Step 1 is implemented and software-tested only. Steps 2–4 remain proposals,
not implemented or hardware-validated behavior.
Even an operator-armed capture can select an unrelated post-arm import or a
native failure. There must be no automatic second attempt after private bytes
have been acquired. A terminal scope, budget, TLS claim or journal cannot be
reset to obtain the first *successful* transaction under a one-transaction
permission.

Any additional received header/payload, heap/client/peer lookup, native caller
link, connection inspection, second transaction or new constructor/renderer
scope needs a separately reviewed copy/acquisition budget and explicit hardware
permission. Extending recording duration or new activation/control-file scope
also requires review before deployment. None is silently authorized by the
unused portion of the previous byte ceiling.

A future successful raw reply plus current owning-journal receipt establishes
only a local token-to-public-resource association. Continuous native registry
lifetime, contemporaneous endpoint/namespace association, selected PC source
session/frame/eye, exact reprojection/foveation UV mapping, and GPU lifetime
remain separate obligations before enabling explicit-mask passthrough.

## Bounded formatter repair validation

The evidence-format repair changes only raw outcome record publication and
background serialization. Candidate selection, callbacks, gate predicates,
scalar/wire reads, native forwarding and all limits remain unchanged.

The focused host checks passed both affected CTest targets: **27 callback
integration subprocess cases** and **84 writer/formatter controls**. Two new
cases independently produce the same 40-byte cutoff for a wrong Image return
PC and a wrong send payload shape, then verify that each distinct refusing
entry and boundary remains in the immutable queued record. Successful outcomes
likewise preserve their actual final entry and available original native result.
Formatter controls cover signed values, unavailable results, LR/SP/FP and
unchanged false proof flags within the existing output bounds.

ASan/UBSan writer checks passed. The runtime fixture's first sanitizer attempt
refused its synthetic native-frame array because ASan's fake-stack feature
moves it outside the real pthread stack interval. Only that affected target was
rerun with `detect_stack_use_after_return=0`, retaining ASan/UBSan and leak
checking while keeping its simulated native frame on the actual thread stack.
All 27 cases then passed. No production ownership check was weakened. These
internal adapter/transport fixtures establish neither native cold admission nor
a real headset result. No complete gate was repeated for this bounded change.

## Second source-specific cutoff (2026-10-14)

The cold-freeze v2 attempt produced the same successful Begin/early-unlock and
40-byte cutoff. Unlike the earlier capture, its terminal record now identifies
`Image/Before`, `InvalidEntry`, and the actual entry registers. The image entry's
frame pointer equals SP and the dispatch frame is 1,536 bytes above SP. The
previously supported Vulkan route requires a 96-byte FP offset and a 1,232-byte
dispatch offset. No image scalar read or wire copy followed that mismatch.

Offline review of the exact already-pinned native executable identifies two
renderer implementations. The observed image call belongs to the Facet
allocator, reached through the Facet renderer, not the supported Vulkan renderer.
Its additional allocator/renderer frame explains the observed layout exactly.
Thus this is a source-specific alternate route, not merely a relocated address
or evidence that native image creation failed. The capture did not acquire its
caller chain, so static source agreement still does not prove a complete runtime
transaction or video-resource association.

The source repair admits both exact renderer paths and remembers which path its
Image boundary selected. Bind/View must use that same route; mixed chains remain
refused. Facet uses the same genuine public image/bind/view APIs, but five caller
links require 96 scalar bytes per creation boundary instead of Vulkan's 80. The
new maximum is 720 scalar bytes, plus the unchanged 16-byte parent and 21-byte
reply limits: **757 private bytes**. Omitting a caller check to retain the old
709-byte authorization would be incorrect.

The source marker/envelope and offline/owned package schemas are versioned again.
Older 709-byte artifacts cannot silently become 757-byte packages, and the new
artifact needs separate runtime scope approval before deployment. One candidate,
eight writes, 32 raw records, 64 KiB and 120 seconds remain unchanged. No new
heap, socket, payload, image, shader or sender acquisition is proposed. Operator
arming and passive-output reservation remain independent pending work; a
successful startup import still cannot identify the requested PC-VR frame.

Local validation passed four affected source/sparse/raw/runtime CTest groups
normally and under ASan/UBSan, including 30 callback subprocess cases and exact
maximum-write/scalar-budget cases. The older lease/replay adapter explicitly
retains its Vulkan-only contract; its additional refusal control passed normally
and sanitized without rerunning the already passing raw groups.

Integration validation used the final ARM64 ELF for all 11 offline artifact
controls without skips, plus three inert ARM/native-libc forwarding/forced-unwind
cases (52 checks each). The changed writer's focused sanitizer target passed.
The 77 session controls passed after schema/envelope changes; three later scope
guards passed separately, checking actual read-only ELF marker bytes, relabelled
old artifacts and stored-plan activation admission without blocking recovery.
The stage helper rechecks the marker through bounded actual load-segment metadata;
it does not rely solely on the preparer's JSON report. These are software/source
results, not a completed Frame capture or live resource association.
