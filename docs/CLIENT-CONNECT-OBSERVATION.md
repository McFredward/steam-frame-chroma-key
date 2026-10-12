# Register-only native connection observation

The optional observer extends the confirmed
[native startup capture](CLIENT-STARTUP-CAPTURE-20261012.md) with one actual
client connect invocation. This is a passive diagnostic candidate, not a mask
transport, live socket lease or working Steam Link passthrough.

## Concrete scope

`FRAME_MASK_CLIENT_CONNECT_OBSERVATION=ON` selects five hidden libc entry
veneers: mutex init/destroy, lock/unlock and connect. Only the own bridge is
public: the observer does not define or replace any public libc function.
Authenticated client-to-libc audit binding redirects only the intended native
client calls; unrelated Steam/UI calls retain their original providers.
The ordinary eleven-entry
object, two-entry OpenVR object and two-entry startup-mutex object retain their
original instruction and unwind bytes. The existing distinct native providers
forward arguments, flags, results, errno and cancellation without a lookup or
substitute syscall.

The sealed preparation and actual startup gate must first authenticate the real
modules and successful target birth. Only then is an immutable copied target
published. A bounded atomic claim selects one thread's exact supported native
lock-before entry. Its exclusively owned process-lifetime scope matches successful original lock,
connect entry/return and final unlock. After that single sample completes, later
ordinary native RPC locks are ignored across all threads. Destroy/re-init of the
exact target, module/audit loss, queue loss, child/shutdown and finalization retire
admission. Unrelated mutex helper calls do not invent target ownership. Only
small owner markers use static TLS, totaling 18 bytes as in the working startup
artifact; a new thread with a reused marker address cannot adopt the old scope.

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

The repaired private ARM main library is **209,904 bytes**, SHA-256
`cfaf1cf6147fbcf7b219f8fa9a630fb688cc1a9caacfcc560039ac0e94a96703`.
Its prepared dependency-free audit is **69,080 bytes**, SHA-256
`169918eaeaa8eab6c8657eaa2b74f123c6f90db4074d828614419383834f275f`.
The offline plan SHA-256 is
`2eaa1452669c6cec1b6941fb7566213775ca2bd6ce60a1d21bf127e7f101448a`.
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
a connected Frame endpoint or native OpenVR execution. The independent final
bundle review passed **142** bounded checks on the actual ELF imports/exports,
audit's compiled immutable initializer and relocations, native provider sizes,
unchanged DEEPBIND flags, distinct envelope and manual scope/recovery guards.
The repaired hidden-only artifact and bundle subsequently passed **147**
independent checks. Its actual ARM preload cases now require every unrelated
public libc function to resolve directly to native libc; the old global-export
artifact fails that acceptance criterion. Selective native client binding still
redirects all five intended calls, preserving arguments, LR, results and flags.
The CI run for the integrated source checkpoint `4088828` also passed:
<https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38290719178>.
The repaired source checkpoint `38b5b16` passed CI as well, including the
focused parallel checks, Windows sender cross-build and protocol sanitizers:
<https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38295295162>.
This is software evidence. The subsequent
[authentic hardware capture](CLIENT-CONNECT-CAPTURE-20261012.md) also confirms
normal headset startup, a visible PC-VR stream and one successful supported
native connect invocation. It does not establish a live descriptor lease or
video/UV/GPU association.

The first expanded artifact failed during Steam UI initialization, before any
receiver or connection capture. It was fully removed and the original service
and readable process mappings were verified; the maintainer confirmed normal
Steam startup. The failed variant is superseded, and the current session helper
rejects its global-interception plan before staging. The cause and evidence limits
are recorded in [STEAM-START-FAILURE-20261012.md](STEAM-START-FAILURE-20261012.md).
The repaired bundle was deployed for that authorized hardware run and has
since been fully removed. It is inactive and cannot authorize its own installation.
Fresh native-file/service
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

Both earlier coordinated tests have completed recovery and left the Frame
unchanged. A repaired-artifact headset run remains unverified and requires
new coordinated restart readiness; there is no automatic retry.
