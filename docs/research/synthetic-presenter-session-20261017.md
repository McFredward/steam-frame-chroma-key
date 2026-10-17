# Reversible synthetic presenter staging

`tools/synthetic_presenter_session.py` composes the unchanged
`pixel_snapshot_session.Session`. This is explicitly **SYNTHETIC OUTPUT
REPLACEMENT**, not passive observation or demonstrated native passthrough.
A passing test or an offline receipt never grants device permission. A separate
operator obtains fresh authorization and readiness before device staging or any
manual restart. This helper has no SSH, service, restart, automatic timer,
image-acquisition or `capture.arm` operation. It does not change camera settings.

The wrapper admits an explicit whole-library SHA256, a bounded ELF64 ET_DYN
(AArch64 outside controlled host fixtures), and exactly one NUL-terminated
`GHVR_SYNTHETIC_PRESENTER_COLD/1` literal in a read-only file-backed PT_LOAD.
A passive library, writable/overlay marker, duplicate literal or different hash
is refused. The private guarded fragment must be exactly18416 bytes with SHA256
`0aaf356d3789473cd32ff0d60cf5971bc1473813241657877db5c147bb1d4364`.
No command argument, receipt Boolean or host-fixture flag relaxes this code pin.

A functional run is a direct bounded lowercase child of the existing owned
`SteamLinkMaskProbe/runs` directory. Both run and runs require0700; pre-existing
permissions are never repaired. All staged files are owned regular0600 files,
without symbolic or hard links. Before invoking base.prepare, the wrapper writes
an exclusive sibling `<run>.synthetic-presenter-session.json` receipt. It binds
the full original base plan, library/mode metadata, candidate, profile and
terminal code group. Keeping this reservation outside the run preserves the
base helper's initially-empty-run contract. A pre-existing passive session
without this receipt is never adopted as functional.

Order: validate source inputs -> reserve receipt -> base.prepare -> copy exact
`synthetic-presenter.spv` -> recheck code/config/manifest and map scan -> create
`synthetic-presenter.enable` LAST, containing only
`GHVR_SYNTHETIC_PRESENTER/1\n`. A partial stage cannot accidentally opt in. Exact
own partial states resume without overwriting existing files; foreign or changed
files remain untouched. A used status/launch claim, capture arm, disarm, consumed
marker or previous rollback makes the code group terminal. Use a fresh run.

`status()` verifies the sibling receipt and base receipt, delegates exact base
file/PID checks to the existing helper, checks all new sidefiles, and reads at
most1024 bytes from `synthetic-presenter-status.json`. This separate writer's
schema/mode/session/pin/types/reasons/budget and explicit false native authority
are validated. Its counters are diagnostic atomics, not a coherent source/frame
receipt. The wrapper omits the base's large progress string; the original helper
still exposes that independently. Unknown/foreign maps, a conflicting capture
arm or unsafe/changed files make integrity invalid. No picture is inspected.

`rollback()` first consumes the code group, then removes only the exact owned
enable marker, and delegates loader/config/library rollback to the original
helper. Candidate bytes, status and receipts remain as private evidence. A loaded
or uncertain library is retained until a separately coordinated MANUAL restart
and retry. Deleting the enable marker does not revoke a loaded source or an
already returned app pipeline. The runtime reads its opt-in once, cold; its GPU
guard handles ordinary source/menu images by selecting original output. The
wrapper always reports `running_replacements_revoked:false`. Source OFF still
waits for the first newly consumed nonpacked image; a frozen valid source remains
a known limitation. Neither route authenticates the image body or proves liveness.

CLI: `prepare --library <path> --library-sha256 <sha> --candidate <private-spv>`,
`status`, and `rollback`, each with the explicit owned `--run-dir`. A preparation
never implies the current native process has loaded it:
`activation_requires_new_native_process:true` is explicit. An optional protected
map scan reuses only the base helper's bounded read-only `sudo -n` policy; it
never prompts or changes services. No Frame action is part of this implementation.

New sandbox tests use fake ELF/proc files, controlled code-pin substitution only
inside the test process, interrupt each staging phase, prove enable-last and
idempotent inode preservation, and exercise foreign/passive/mutated files,
permissions/links, loaded/unknown mappings, terminal rollback, malformed status
and CLI scope. These are software file-ownership proofs, not live device evidence.
