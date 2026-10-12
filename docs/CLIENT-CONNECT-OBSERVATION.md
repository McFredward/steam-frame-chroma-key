# Register-only native connection observation

The optional observer extends the confirmed
[native startup capture](CLIENT-STARTUP-CAPTURE-20261012.md) with one actual
client connect invocation. This is a passive diagnostic candidate, not a mask
transport, live socket lease or working Steam Link passthrough.

## Concrete scope

`FRAME_MASK_CLIENT_CONNECT_OBSERVATION=ON` selects five original libc entry
veneers: mutex init/destroy, lock/unlock and connect. The ordinary eleven-entry
object, two-entry OpenVR object and two-entry startup-mutex object retain their
original instruction and unwind bytes. The existing distinct native providers
forward arguments, flags, results, errno and cancellation without a lookup or
substitute syscall.

The sealed preparation and actual startup gate must first authenticate the real
modules and successful target birth. Only then is an immutable copied target
published. A bounded atomic claim selects one thread's exact supported native
lock-before entry. Its initial-exec TLS scope matches successful original lock,
connect entry/return and final unlock. After that single sample completes, later
ordinary native RPC locks are ignored across all threads. Destroy/re-init of the
exact target, module/audit loss, queue loss, child/shutdown and finalization retire
admission. Unrelated mutex helper calls do not invent target ownership.

The connection observation copies the existing true-entry registers and original
result/errno. It reads **zero additional private bytes**: no FD/PID fields,
sockaddr, packet, stack window, pixel or shader is acquired. The original five
constructor-word comparisons remain unchanged. A successful call is a historical
local operation, not proof that its FD remains connected. `Registry::connected()`
and carrier admission remain inactive; every continuity, credential, namespace,
source-frame, UV and GPU proof flag stays false. The PC Steam sender is unchanged.

The source constraints and deferred scopes are in
[CLIENT-CONNECTION-ADAPTER-PLAN-20261012.md](CLIENT-CONNECTION-ADAPTER-PLAN-20261012.md).

## Reviewed candidate and tests

The private final ARM main library is **210,096 bytes**, SHA-256
`3a9bf6a315394cc6ab5aa7b580cfb1cb7a029ba885218195beedd8130b2828e5`.
Its prepared dependency-free audit is **69,080 bytes**, SHA-256
`3f2499135a0e69766a3569ece278bb2fbde3649b48b18aeb3dcd140360b1bd55`.
The offline plan SHA-256 is
`0e07d64b954fd0e109f904aac7e939912aae1a05c69a1a39247558368b5e8524`.
These are own artifacts; Valve binaries and private reference/capture data are
not tracked or distributed.

Focused evidence covers 984 register-gate assertions under normal and ASan/UBSan
execution, two rejected compiled semantic mutations, opt-in host and actual ARM
libc/loader DEEPBIND fixtures, unchanged original veneer bytes and CFI, root
readiness/inert-loading checks, and offline scope/session controls. After limited
repairs only affected checks were repeated; unchanged passing controls were
inherited, not reported as a new full local gate. The actual final main ELF also
passed three unrelated-process ARM preload cases, 29 checks each, using the pinned
Frame libc/loader. Those cases prove inert forwarding, not production startup,
a connected Frame endpoint or native OpenVR execution.

No expanded connection scope has yet run on hardware. The prepared bundle is
inactive and cannot authorize its own installation. Fresh native-file/service
checks, a separately reviewed final bundle/helper inventory and maintainer scope
and restart readiness are required before deployment.

## Manual recovery

The reviewed session helper requires both the original startup acknowledgment
and the new `--ack-connect-register-diagnostic-scope`. The latter is required
when staging and activating this distinct `FCCONN01` version-2 envelope; a
startup-only acknowledgment cannot silently enable the larger callback set.
Caps remain **32 records, 64 KiB and 120 seconds**. The deadline stops recording
only; it never restarts Steam or performs automatic rollback.

Installation is restricted to inventory-owned files below the user's
`~/.local/share/GloomhavenVR/frame-client-startup` and an exact owned runtime
override for `steam.service`. Original service environment, native file hashes,
environment-file hashes and helper bytes are recorded before activation.

After maintainer rollback readiness, run the staged own helper's `rollback`
mode with `--ack-steam-client-restart-readiness`. It removes only the matching
owned override and restarts Steam with the original environment. Archive the
bounded capture/ownership/restoration receipts, verify the affected service and
readable user-process mappings are free of the diagnostics, then remove only
inventory-matching files. Do not remove foreign overrides or native/system/game
files. Tell the maintainer when standby is safe; no timed restart is scheduled.

The earlier coordinated test has already completed this recovery and left the
Frame unchanged. A new installation requires a new explicit scope approval.
