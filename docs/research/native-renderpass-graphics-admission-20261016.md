# Native render-pass and graphics admission repair

This is an offline receiver diagnostic repair, not working Steam Link
passthrough. The last native attempt (r3) refused before manual arm. The
maintainer confirmed the original Frame interface visible and usable after
manual rollback; the headset is not needed during this work.

## Evidence and cause

The actual r3 journal retains105 qualifying layouts within256 slots, one shared
Set0 fact and no Capacity refusal. The earlier startup retention bottleneck is
therefore removed. Original RP2 creation shapes include two/three subpasses and
one to three attachments. The old one-subpass decoder cannot represent those
whole passes. One pinned target gets past that gate and rejects dynamic state
1000455017 (`SAMPLE_LOCATIONS_ENABLE_EXT`). Other native dynamic values and
subordinate payloads were not captured; host fixtures must not be described as
a replay of those unknown native inputs.

Grouped creation shapes previously emitted only their first observation. Their
later last-handle fields could change without another report. Exact successful
distortion-entry target observations now have a separate finite eight-record
bank. Entry names select diagnostic metadata only; shader pins and original
creation generations still govern clone admission.

## Representation and original-call contracts

- Retain the full bounded pass: up to8 subpasses, attachments, inputs, resolves,
  depth/stencil references, preserve lists and dependencies. Select graphics
  scope using the actual pipeline's original subpass index.
- Reconstruct RP2 through RP2 with its input aspects intact. Do not infer
  maintenance2 or an application API version from physical-device capability.
- Keep the exact original core/KHR command variant. A returned promoted core
  function pointer does not, by itself, establish that calling it is allowed.
  See [GetDeviceProcAddr](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceProcAddr.html).
  An absent exact factory entry refuses observation rather than trying another
  name. The original application's calls must each forward exactly once.
- Own the complete supported graphics definition, up to64 dynamic declarations
  and bounded typed child extensions. Rebuild all pointers after copying.
  Preserve native dynamic state; do not replay guessed values or add setters to
  the native stream.
- Respect ignored arrays and optional null state. Unknown extensions, unsupported
  flags, invalid bounds and unrepresented routes still refuse observation.
  Three blend dynamics alone do not prove advanced blend state irrelevant;
  that path additionally needs the fully checked device whitelist's absence
  proof, or the fourth dynamic declaration. See
  [color blend state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineColorBlendStateCreateInfo.html).

The opaque target ticket binds the immutable prepared recipe's selected subpass
to the actual original pipeline creation and current command-buffer subpass.
Only inline transitions are supported. Observer completion/source-copy work
occurs after the original whole render pass ends, never between subpasses.
Later draws, secondary recording, unknown routes or retirement still invalidate
publication while original application calls continue untouched.

## Bounded diagnostics

Each exact target preserves its pipeline/layout/pass handles, selected subpass,
up to64 dynamic enums and12 state-header chains of up to32 public structure
types each. Unknown render-pass scope never permits depth/blend pointer reads.
Eight same-shaped target creates stay separate; overflow is explicit, with no
eviction. This is copied public metadata, not a native memory/lifetime lease.

Cold JSON formatting permits16KiB per target record. The maximum-width fixture
currently emits7,489B. Prior conservative progress245,416B plus eight16KiB lines
fits the separate512KiB reader; ordinary configuration/audit JSON remains16KiB,
and the event count remains256. Reports are emitted once, not per frame or
repeated unsuccessful poll. A reporting failure prevents operator admission.

## Validation scope

The new private GPU fixture exercises legacy/RP2 two/three-subpass cohorts with
selected index1, multisample resolve, input attachments, depth and preserves.
Both Intel and Lavapipe run original/observer/immediate-original-restore indexed
draws with bit-identical color and complete128-byte records. The extended case
queries and enables actual EDS1 support and supplies the real culling setter;
the same owned graphics representation reconstructs the declaration.
Each driver passes866 checks across eight cases with active Vulkan/synchronization
validation and zero errors. A separate focused real-driver cold-factory proof
passes154 checks per driver for queried/enabled EDS3 state: Intel includes
`SAMPLE_LOCATIONS_ENABLE_EXT`, the enum actually refused in r3. That latter proof
creates pipelines and retires original inputs; it records no draws or pixels.

These are actual host-driver proofs with owned fixture vertex code. They do not
prove the Frame's full native creation payload, a physical-eye/video-frame
association or passthrough presentation. Separate fake-dispatch checks exercise
the production Snapshot exports, generation tickets, exact original forwarding,
full-pass End ordering and refusal paths. Sanitizer/ARM compilation and scoped
integration receipts are retained privately, with failed fixture and build
attempts preserved. Already passing unrelated areas are inherited rather than
repeated after a bounded repair.

Private evidence: `build/private-native-r4-integration-20261016/`,
`build/private-native-r4-report-20261016/`,
`build/private-native-subpass-worker-20261016/` and the pass/graphics worker
receipts. Temporary compilation outputs and completed worker checkouts are
removed once reaped and no longer borrowed. Original native captures and current
deliverables remain protected. No new Frame action follows from this document.

## Final integration and next action

Runtime source b897155 has exact hosted CI38972790160 green:127 CTests in96.61s,
both optional diagnostic builds, Windows sender and8,482 protocol sanitizer
checks. Local focused integration reuses unchanged passing scopes after two
obsolete test expectations were repaired; it is not reported as another full
local gate. Final exact-name Snapshot public checks total333;200 new exact-name
and910 complete-subpass controls pass normally and sanitized.

ARM package510,752B SHA
`dc9fa04aa9d7da4e559b00c614e73378c4243dac43e457ff7367db91ab06c494`
retains three exports and ELF hardening. Its81 imports add two GLIBCXX3.4 locale
functions; the existing actual-provider and isolated-load preflight must still
check all imports before activation. Exact ARM-file local staging, byte/inode
idempotence, unarmed state and rollback pass; three separate real host-loader
cases pass. These do not execute the ARM file on the headset.

The unique r4 plan is630,333B transferred and1,141,085B including the staged
library copy. One paired source capture stays at most64MiB combined plus128B
SSBO; progress is at most512KiB. There is no automatic expiry/restart, sender
modification or live passthrough. Installation and the single loading restart
remain pending fresh maintainer permission/readiness; later removal needs a
manually agreed restart. No Frame contact occurred in this preparation round.

All three own worker worktrees and reaped build/VVL caches are removed after
source comparison. The final root compile-cache cleanup removed77,737,048B;
private original inputs, failed attempts, logs, source pins and current package
remain. The fork's remaining private build directory contains evidence/current
deliverables, with no CMakeFiles or Python cache directories.
