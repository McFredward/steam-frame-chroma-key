# First compositor raw candidate: source and capture review

The latest diagnostic consumed its single candidate during Frame startup,
without an operator-confirmed PC-VR stream. It correctly refused that candidate
before observing an image or reply. This is not evidence that the compositor
exited, nor that the raw refusal caused the blue Frame interface. The separate
`vrwebhelper` loader failure reports insufficient static TLS for `libcef.so`.
Those two failures require separate repairs.

This review uses the retained `compositor-raw.jsonl`, `compositor-journal.jsonl`
and native service logs from the failed-start run, plus current source at
`2700aa6`. It performs no Frame access, runtime/source change, added native
copy, shader activation or sender modification. Full native records and
instruction listings remain in ignored private evidence.

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

1. **Expose the existing refusal facts.** Serialize the already retained last
   boundary/phase/native result, and value-copy the refusing event's already
   captured public entry registers into its diagnostic record. This requires no
   additional native memory acquisition or new transaction. Review actual
   artifact/record sizes and forwarding behavior before deployment. It lets the
   next refusal distinguish a response-branch mismatch from a Vulkan entry ABI
   or layer-order mismatch instead of guessing.
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

Steps 1–3 remain proposals, not implemented or hardware-validated behavior.
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
