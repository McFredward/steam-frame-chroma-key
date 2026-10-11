# Read-only live bridge inspection

`tools/inspect_live_bridge.py` inspects existing Frame process and Steam log
metadata. It does not install a receiver, initialize VR, attach to a process,
change settings, or activate passthrough. It cannot prove a working mask/video
association or infer an active stream from old logs.

## Run without installing anything

From the checkout, an operator with an existing SSH connection can pipe the
script to the device's Python interpreter:

```sh
ssh frame-host 'python3 -B -' < tools/inspect_live_bridge.py
```

Substitute the existing SSH host alias. The tool neither reads nor manages SSH
credentials. The remote Python process receives the program through stdin,
uses only the standard library and prints one JSON report to stdout. `-B`
prevents bytecode writes. There are no temporary files, output files, local
repository imports, package installs, shell subprocesses, environment reads,
configuration reads, or network calls inside the observer. It can also be run
locally as `python3 -B tools/inspect_live_bridge.py`.

The default existing log directory is
`/home/steamos/.local/share/Steam/logs`. An explicit `--logs-root DIRECTORY`
changes only the directory; it never expands the fixed list of filenames.
Do not use an output-redirection command on the device if a no-write inspection
is required. Save stdout on the operator's computer if needed.

## Read and output bounds

| Input | Bound |
| --- | --- |
| `/proc` directory entries | 4,096 |
| Selected processes | 16; `vrlink`, its exact `vrlinkrunthread` comm alias, `vrcompositor`, `vrserver` |
| Process reads | Bounded `comm`, executable basename/stat and ELF build ID |
| Existing logs | `vrlink.txt`, `vrcompositor.txt`, `streaming_log.txt` |
| Tail per log | 128 KiB |
| Total bytes read | 1 MiB |
| Individual log line | 2 KiB; longer lines omitted |
| Numeric records | 128 across all logs |
| Runtime | 5 seconds; CLI alarm interrupts stalled reads |

Known logs must be regular files. Final symlinks and special files are rejected.
The process executable symlink is read only for an allowlisted executable
basename; the report contains no filesystem paths or executable contents.
ELF parsing reads bounded headers/notes and emits only class, machine and GNU
build ID. No shader code, binary body, `/proc` command line, environment, memory
or maps is emitted or inspected. Read descriptors are read-only and nonblocking.
The real streaming process on the tested Frame uses `vrlinkrunthread` for its
`comm` while its executable is `vrlink`. Only this exact alias is accepted;
matching prefixes are not accepted, and executable identities must match the
expected basename.

Original log bodies are never printed. Only integer values of these literal
frame-association labels are retained:

```text
unFrameId lnFrameId frame_id m_iPresentNumber history_idx fdiff
source_frame_id encoder_frame_id app_frame_id decode_frame_id present_frame_id
capture_buffer_index buffer_index buffer_id
```

The longer labels are eligible if an existing diagnostic contains them; this
does not assert that the current Valve build logs them. Unknown labels,
credentials, addresses, user IDs and prose are omitted. Partial boundary lines,
duplicate labels and malformed integers are discarded. A recognised
missing-metadata line adds a fixed event name, never its message text.
Existing `OutputSetup`/`ENOMEM` lines contribute only a decoder-allocation
failure count; this does not assert that a failure occurred in the current run.
The observed output-buffer allocation error with errno 12 contributes to the
same count, without exposing its message body.
The existing `SVLFEC` substantive-retry diagnostic contributes its five numeric
values in a separate `fec_reassembly` namespace. Its packet, skipped, last-sub,
group and size numbers are **not** decoded, presented or source frame IDs.
An exact existing `SVLCodecV4L2::CaptureSetup` diagnostic can also emit decoded
width and height (1–32,768) and an explicitly allowlisted format (`C01Q`, `NV12`,
`P010`). Arbitrary format strings are omitted. `uCaptureBufferCount` can emit its
numeric value. These belong only to `decoder_configuration`: they establish
neither an eye layout nor the frame/surface identity to which a mask belongs.
No guessed size/stride labels are parsed.

## Interpret the report conservatively

`schema_version` is 1. `snapshot_unix_ns` and each log's `mtime_ns` are file and
inspection metadata, **not** video-frame timestamps. A file modified recently
can still contain old records. Logs may change during inspection, and
`changed_during_read` records that limitation.
`age_ms` and `file_recency` describe only the file modification time, with an
explicit five-minute boundary and a future-clock case. They never establish
record recency. With no observed `vrlink` process, each log is labelled
`historical_or_non_streaming_no_vrlink_observed` even if it contains frame labels
or errors; otherwise stream activity remains unverified.

`live_status` describes process observation:

- `vrlink_process_present_stream_unverified`: an allowlisted process was found;
  this is not proof that video is being streamed.
- `no_vrlink_process_observed`: a complete process scan found no `vrlink`; old
  log records do not become live evidence.
- `process_snapshot_incomplete`: a limit or read restriction prevented a
  complete scan; absence cannot be inferred.

`stream_active` remains `unverified`, and `association_proven` and
`mask_activation_safe` remain false. Numeric labels keep their original names:
`unFrameId`, `frame_id` and `m_iPresentNumber` are not interchangeable, even when
their values happen to match. Do not create a latest-mask, timestamp-nearest or
counter-equality synchronization rule from this report.

Limits, read errors and unavailable files yield fixed warnings and partial
metadata instead of unbounded scans or input-dependent error messages. Numeric
process IDs and known process/log names are intentional report fields; no user
account identifier, IP address, private string or log text is copied.

This observation can show which existing diagnostics are available during a
short active stream and whether missing-metadata records exist. It cannot
recover the missing PC source-to-encoder identity, decoded surface generation,
private compositor ABI, resource lifetime or exact UV mapping. Those remain
the evidence requirements in [the decoder investigation](FRAME-DECODER-20261011.md).
Collect both ends' exact, supported frame/resource association before enabling
mask composition. Missing evidence must keep opaque rendering.
