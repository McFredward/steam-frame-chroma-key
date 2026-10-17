# Scoped support for the observed Valve subpass override

Status: offline implementation and focused checks complete. The new package has
not run on the Frame. It preserves original output and does not enable native
streaming passthrough or establish frame/eye/UV association.

The exact source and binary findings are recorded in
[the compatible-mirror review](native-compatible-mirror-20261017.md). This adapter
supports only the compositor's observed zero sRGB-override value, not arbitrary
private structures or a generic numeric allowlist.

## Cold provider verification

Before any private payload is interpreted, a background worker checks the
actual saved downstream Begin2/Next2 core/KHR function pointers for the actual
device. Only the independently established chip6/7/8 entrypoint offsets in the
exact current Valve ICD are supported. Unknown loader/layer wrappers refuse.

The cold verifier pins the already-loaded module without loading a new module;
checks the protected no-follow file, exact SHA256/GNU ID, bounded ELF structure,
actual map/device/inode identity and loader-held mapped read-only code/notes;
then repeats identity checks before publishing an opaque capability. The narrow
Btrfs reconciliation requires the actual top-level mount, exact options and
longest matching path. Unrelated escaped mount names remain harmless.

Callback authorization uses only bounded atomics and scalar checks: same device,
exact saved function, route, epoch and live lease. It performs no resolver,
file operation, lock, allocation or wait. A scoped permit holds the module
through the unchanged original call. Actual device teardown revokes entry,
joins the worker, observes callback quiescence and releases the pin after the
original DestroyDevice call. A revoked preparation cannot publish Ready.

The decoder validates all operation roots and the entire bounded chain before
reading the known 32-bit payload. Begin and Next can use this contract; End,
unsupported chains, nonzero masks and expired/wrong permits remain refused.
The original entrypoint, node pointer, payload and other arguments are never
rewritten. Native color-conversion state therefore remains the driver's state.

One bounded cold status reports actual provider/path/entrypoint refusal facts.
Final metadata additionally records state and bounded permit-acquisition misses;
there is no per-frame diagnostic stream. The operator must require Ready for
the same prepared device and both exact KHR routes before requesting a stream
or creating the manual arm file. Its old optional gate remains compatible with
historical packages; this new package explicitly requests the stronger gate.

## Evidence and limits

- Verifier:1148 focused owned-file/DSO/loader/map/code/lifetime checks pass
  normally and under ASan/UBSan/leak checking. The acquired ARM file's exact
  hash/build ID/code pin is checked separately, not loaded on the host.
- Decoder:112 new checks plus9 epoch controls pass normally and sanitised;
  protected pages catch otherwise hidden private-payload reads.
- Actual Snapshot callbacks:2090 focused normal/sanitised checks use a synthetic
  provider and fake native dispatch. Final bounded formatters pass772 public
  checks. Callback evidence is inherited for that formatter-only repair.
- Root's seven affected CTests pass as six unchanged passes plus the repaired
  owned-provider fixture pass. The first fixture correctly refused group-writable
  build output; its CMake setup now gives it0755 independently of the host umask.
- The actual C++ provider/preparation formatters are executed by the Python
  consumer contract test. An incomplete synthetic provider refuses; controlled
  value changes test the real schema. These are not native-provider receipts.
- Strict host and ARM optional builds pass. The stripped ARM library is526280B
  with94 required imports and three private exports. Twelve imports are new,
  including the C23 scanf symbol atGLIBC2.38. The cached libc matches the last
  actual native receipt and exports them; fresh actual-provider resolution and
  exact-provider isolated loading remain mandatory before activation.

An early worker sanitizer attempt mixed old/new Status headers while compiling
different translation units and correctly failed. That invalid build is retained
separately. A fresh frozen-source normal/sanitised build passes, with matching
begin/end source hashes. It is not evidence of a native device failure.

The previous compatible-mirror GPU result is inherited, not rerun or relabelled.
No complete main-repository/wire gate was repeated for these separate fork
changes. Private receipts, frozen sources and cleanup records live under
`build/private-native-r7-integration-20261017/` and the three corresponding
`private-native-*-worker-r7-20261017/` directories.

The next device attempt requires fresh explicit permission after the final
package checks and hosted CI. It is one manually armed, bounded original-output
observer plus the established paired source copy. There is no automatic expiry,
rollback or restart. The Frame is currently restored and safe for standby.
