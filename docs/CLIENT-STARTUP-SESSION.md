# Manual client-startup diagnostic session

`tools/client_startup_session.py` stages, activates and removes the narrowly
prepared [OpenVR startup observer](IMPORT-STARTUP-FORWARDING.md). It does not
download anything, patch native binaries or launch scripts, modify Vulkan,
activate passthrough, run sudo, or schedule a restart. Its tests use injected
service responses and synthetic files; they never contact a Frame.

An offline bundle, a matching hash and a green test are not runtime permission.
Before staging, obtain the maintainer's separate approval for this diagnostic.
Before either service restart, obtain coordinated restart readiness. Closing or
restarting Steam can interrupt the Frame interface and its PC stream. This
manager makes no automatic restart or timed rollback.

## Reviewed inputs and private ownership

The manager accepts only `frame-client-startup-offline-bundle-v1` with its
activation and runtime-authorization flags still false. Supply the independently
reviewed plan SHA, separately reviewed SHA values for both manager scripts,
the three own artifacts, and fresh read-only files for all five canonical native
roles: `vrlink`, `libopenvr_api.so`, `libc.so.6`, `ld-linux-aarch64.so.1`, and
`vrclient.so`. Each native file must have the exact current canonical hash and
AArch64 ELF ABI. The envelope's canonical module/client descriptors, observer
review identity, client raw loader name and RX interval must agree with those
files and the reviewed plan. Expected loader names in the offline plan remain
candidates until the actual runtime validates them.

Only these files are copied under the private `0700` directory
`~/.local/share/GloomhavenVR/frame-client-startup`:

- `libframe_client_startup.so`, `libframe_client_startup_audit.so`, and
  `client-startup-envelope.bin`;
- the reviewed `deployment-plan.json` receipt and byte-identical, independently
  reviewed `client_startup_session.py` and `receiver_observer_session.py`.

All copied files are `0600`. `audit-preparation.cpp` stays offline. Native files
are read and hashed, never copied or changed. The manifest, ledger and operation
lock belong to the same private directory. Existing directories, unsafe parents,
symlinks, foreign files and unsupported activation paths are refused rather than
replaced or chmodded.

The manager snapshots the exact service environment, manager environment and
reviewed native `EnvironmentFile` contents before creating an activation. It
rejects foreign preload/audit/tracing in the caller, manager, service drop-ins
and native environment files, including earlier receiver/mask activations.
Every listed native environment file requires its own fresh reviewed hash;
optional/missing, shell-source and unsupported multiline forms are refused.

## Stage and activate

Run the private reviewed manager through `python3`; no executable-bit change is
required. The production CLI requires native Linux/AArch64 and the current
`/run/user/<uid>` runtime directory. Use actual independently reviewed hashes
and fresh physical read-only paths in place of the placeholders below:

```sh
python3 /private-reviewed/client_startup_session.py stage \
  --reviewed-plan /private-reviewed/deployment-plan.json \
  --reviewed-plan-sha256 REVIEWED_PLAN_SHA256 \
  --observer /private-reviewed/libframe_client_startup.so \
  --audit /private-reviewed/libframe_client_startup_audit.so \
  --envelope /private-reviewed/client-startup-envelope.bin \
  --native-receiver FRESH_VRLINK_PATH \
  --native-api FRESH_LIBOPENVR_API_PATH \
  --native-libc FRESH_LIBC_PATH \
  --native-loader FRESH_LOADER_PATH \
  --native-client FRESH_VRCLIENT_PATH \
  --reviewed-session-helper-sha256 REVIEWED_SESSION_HELPER_SHA256 \
  --reviewed-receiver-helper-sha256 REVIEWED_RECEIVER_HELPER_SHA256 \
  --ack-startup-diagnostic-scope
```

Append `--reviewed-environment-file /actual/native.env=REVIEWED_SHA256` for every
native file listed by `systemctl --user show steam.service`. `stage` copies and
records ownership only: it does not create a drop-in, reload systemd or restart
Steam. Incomplete staging cannot be activated. Staging will not replace an
existing session; preserve and inspect its receipts before deciding to remove
any old inventory-owned files.

After obtaining restart readiness, activate the completed staged session:

```sh
python3 ~/.local/share/GloomhavenVR/frame-client-startup/client_startup_session.py activate \
  --ack-startup-diagnostic-scope \
  --ack-steam-client-restart-readiness
```

The manager rechecks all copied files, fresh native hashes and original
environment snapshots, then exclusively publishes
`$XDG_RUNTIME_DIR/systemd/user/steam.service.d/94-frame-client-startup.conf`.
The manifest records its exact bytes before publication. A non-`.conf` pending
file prevents short/full-disk writes from publishing partial activation bytes.
Publication uses an exclusive hard link, never a replacing rename.

The four service-local assignments are `LD_PRELOAD`, `LD_AUDIT`,
`GHVR_FRAME_CLIENT_STARTUP_ENVELOPE`, and `GHVR_FRAME_CLIENT_STARTUP_OUTPUT`.
They name only the owned main library, audit library, envelope and bounded
`client-startup.jsonl` capture. The manager does not alter systemd's manager
environment. It reloads the user manager and requests one explicit
`steam.service` restart. Exact acquired service environment is checked, but this
is not proof that the diagnostic loaded into a receiver or captured a birth.
The observer's bounded recording deadline does not trigger a rollback/restart.

`start` combines `stage` and `activate` and requires both acknowledgments before
creating files. It has the same input arguments as `stage` plus restart readiness.

## Manual rollback and interrupted runs

Removing the activation without restarting is always available:

```sh
python3 ~/.local/share/GloomhavenVR/frame-client-startup/client_startup_session.py rollback
```

This removes only the exact owned drop-in (and an exact owned pending hard-link
pair after an interrupted publication). It does not restart or claim that
already loaded libraries are gone. Foreign/tampered overrides, partial pending
bytes and unsafe links are preserved for manual review; the manager never
guesses ownership or overwrites them.

Once restart readiness is granted, complete the rollback:

```sh
python3 ~/.local/share/GloomhavenVR/frame-client-startup/client_startup_session.py rollback \
  --ack-steam-client-restart-readiness
```

Exact owned activation removal is the first recovery mutation, before logging
or archiving. The manager requests one coordinated restart, verifies the
original service/native-file snapshots and manager environment, and archives
the capture (at most 64 KiB) and bounded receipts. The only permitted manager
differences are the existing native per-session Gamescope/MangoHUD file paths;
their exact changed keys are recorded. Native environment-file bytes remain
exact. Inherited caller environment may contain this session's four exact
assignments during rollback, so a terminal started during the diagnostic can
perform recovery; different values are refused.

An archive records expected source hashes before copying. A retry accepts only
matching existing private copies and completes missing copies without replacing
anything. Partial or foreign mismatches stop receipt completion and remain for
manual review. Interruption, a full disk, or a failed service command disables
the exact owned activation where possible and leaves recovery receipts; it
never schedules another restart. A partial pending write is not a `.conf` and
cannot become an enabled drop-in. A forged/incomplete completion receipt is not
accepted as an idempotent rollback.

`rollback-complete.json` explicitly records
`native_mapping_unload_verified: false`. This checkpoint verifies configuration
restoration only. The integrator must separately inspect actual process ancestry,
namespaces, mappings and capture-writer liveness before certifying the Frame's
runtime baseline or removing final inventory-owned staging files. Do not remove
live libraries or captures solely because the service environment is restored.
The capture proves no receiver/compositor token join, source frame, mask, UV,
pixels or coherent GPU ownership.
