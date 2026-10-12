# Protected receiver capture on SteamOS 0.4.5

The manually coordinated PC VR-stream run captured **32 valid protected receiver
selection attempts**, with all 32 serialized and no loss, mutex, pointer or I/O
errors. The [Btrfs identity reconciliation](RECEIVER-OBSERVER-SESSION.md) and
[updated receiver audit](FRAME-RECEIVER-UPDATE-20261012.md) passed on the actual
running receiver before arming. The diagnostic was then fully removed and the
original native Steam configuration restored.

This closes the bounded receiver-observation experiment. It does **not** prove
accepted presentation, PC source-frame identity, an import namespace, physical
eye selection or coherent GPU consumption, and it does not activate passthrough.

## Native provenance and scope

Fresh checks used the actual Frame's files and symbol providers, not a cross
sysroot or an old native proposal:

- Receiver: 2,425,024 bytes, SHA-256
  `31addef0c9553d3770b5570487699e96d70271d4073cd6c03b080fdee6ae2153`.
- Reviewed production observer: 75,048 bytes, SHA-256
  `393e39c3bb9970032cac54a9fd3e3547389d62f78bb5056b014139c677e4011a`.
- Actual-file proposal v2: SHA-256
  `080901aaebd5a2cf97fecdefa97a253d52b9450a63a573783c3b36c561d66990`.
- Current source checkpoint: `689e97c`; its 21-suite CI, Windows sender build and
  protocol sanitizer checks [passed](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38227015280).

The maintainer's Steam-client-service preload permission remained limited to
the same six protected scalar fields. Restart readiness and visible PC VR-stream
readiness were separately confirmed. Only the exact reviewed receiver samples;
other inherited processes forward mutex operations unchanged.

The capture writer was native `vrlink` PID 53474 in `steam.service`. Arming
verified its exact executable, owned observer mapping, current libc/loader,
expected environment, exclusive writer lock, stable PID/mapping/file identities
and unchanged native environment file. The two Btrfs provider mappings used
superblock device `0:29`; file stat used anonymous root device `0:30` with current
inodes 50332 and 48434. The narrow root/mount checks passed and were recorded in
the arm event. No guard was bypassed or active helper overwritten.

## Capture and interpretation

The private 9,103-byte JSONL capture has SHA-256
`7b2bfffae81c0e326b1a13e2e4fbd74976a46b9b389a0a0cb8d8053fc2c15c07`.
It contains one production header, 32 observations numbered 1 through 32, and a
quiescent footer. Every observation has one valid metadata pair and zero reason
bits. The footer records 32 completed/valid/serialized entries, zero invalid or
lost entries, no active scopes or pending callbacks, `io_ok:true` and
`selection_evidence_complete:true`. The sample limit closed the capture before
the deadline; the writer lock was released before archival.

| Observed scalar property | Result |
| --- | --- |
| Native selected keys | 32 distinct values, consecutive within this short run |
| Decoded content generations | 32 distinct values, consecutive within this run |
| Native metadata first words | 32 distinct consecutive values; none equals the selected key, including its low 16 bits |
| Shared-resource token values | 12 distinct values, all observed again with another key/content generation |
| Entry/exit tuple consistency | All 32 scopes valid |
| Repeated selection of one key | Not exercised by this capture |

Reused token values demonstrate that a token alone cannot identify the selected
video content. Keep content generation and resource lifetime/namespace separate.
The run does not independently establish registration/retirement continuity for
those tokens, so do not interpret equality as proof of a shared cross-process
resource. Likewise, consecutive metadata words are not proof of a PC source-frame
identifier or TX-key reuse generation. Preserve each native field independently;
do not manufacture an association from equal deltas or a fixed numeric offset.

All records retain `accepted_submit:false` and `source_frame_associated:false`.
The footer also retains `gpu_consumed_coherent:false`. Native unlock sites can
still be reached after failed/early presentation paths; protected selection and
valid-key metadata are the evidence actually obtained.

## Verified recovery and storage cleanup

After the maintainer confirmed manual rollback readiness, the original copied
helper removed its exact owned Steam-service activation and restarted native
Steam. The service, manager environment and native environment file matched the
original snapshots **exactly**. Independent checks confirmed:

- Native receiver, libc, loader and environment-file hashes remained unchanged.
- Instrumented Steam client PID 50965 and receiver PID 53474 had exited.
- Both diagnostic service drop-ins were absent; native Steam was active.
- All 11 inspected Steam-service processes lacked diagnostic mappings and
  environment values. Four unrelated processes denied mapping access; the
  private record explicitly does not claim inspection of every headset process.

The final manifest, ledger, restoration checks and exact file inventory were
archived before deleting 17 owned diagnostic/staging files, including the arm
marker and generated Python cache, and both private directories. SSH was closed,
and the maintainer was told the Frame could enter standby. No timer restarted
the headset and no native script, executable or system configuration was patched.

Private evidence remains in ignored
`build/private-receiver-frame045-run-20261012-0901/`. The updated native compositor
was also copied read-only for local audit; verified transfer chunks were removed.
Native references are private inputs, not distributable artifacts or test caches.

## Next bridge

Use this protected selected key/content/token evidence as one receiver column.
The next independent requirements remain:

1. Prove the current compositor's successful resource import, exact client/token
   namespace, creation/retirement and selected image/view content.
2. Observe the PC composition-to-encoder-to-transmitter-key association, including
   source session, drops and explicit reuse generations.
3. Prove coherent physical-eye, crop/foveation/reprojection and GPU ownership for
   the matching content before enabling the original explicit-mask shader path.

The updated compositor differs from the binary used for the earlier Vulkan
snapshots. Do not join those earlier snapshots to this capture or treat their
old native addresses as current. Static audit can proceed locally; additional
runtime private fields or shader/image operations require their own reviewed
scope before deployment. Missing links continue to render opaque.
