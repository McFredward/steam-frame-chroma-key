# Steam UI failure and narrowed observer scope

## Hardware facts and recovery

The initial register-connect observer loaded into Steam at approximately
20:33:46 CEST. `steam.service` reported active/running and both own libraries
were mapped, but the headset remained at its loading indicator and then reported
an issue starting Steam. No `vrlink` process or diagnostic capture existed.
Service/process presence was therefore insufficient evidence of a working UI.

The native assertion dump at 20:35:48 is 818,264 bytes, SHA-256
`f7ef66d5b46699b6d5475eb20026fdc9eb89f3f1afc2e4d4d5016752237c15c3`.
Its actual CommentStream reports `Failed creating offscreen shared JS context`
at `steamuisharedjscontroller.cpp:583`. The existing stored ARM64 frame chain
contains Steam/UI/tier0/crashhandler frames; it does not establish a blocking
instruction inside the observer. No live process memory was inspected to obtain
this existing dump. It and bounded native log tails remain private, untracked.

After explicit maintainer recovery instruction, the helper removed its exact
owned override and restarted the original Steam service. The old process needed
termination after its stop timeout; no timer initiated this recovery. Original
service/native/environment-file snapshots matched, and 58 readable user-owned
process maps contained no diagnostic library. Four mapping-restricted
systemd/PAM/Gamescope/SSH processes were outside the changed service scope.
Both inventory-owned temporary directories were removed after their metadata
was archived. The maintainer confirmed Steam started normally; standby was safe.

## Proven defect, uncertain internal consequence

The failed ELF newly defined global public `pthread_mutex_lock`,
`pthread_mutex_unlock` and `connect`, in addition to its existing public
init/destroy definitions. Because the preload was inherited through the Steam
service, unrelated Steam/UI and descendant calls entered the observer even
when its sealed preparation rejected that process. Refusing diagnostic sampling
does not undo ELF symbol interposition. This was a real excessive instrumentation
scope; the intended client-only audit route already existed.

The real provider imports/versions were unchanged and matched native libc.
Actual pinned-loader/libc ARM fixtures passed ordinary, recursive, error-check
and adaptive mutexes, contention, constructor locks, loader activity, cancellation
and fork checks for both the working and failed artifacts. Thus a universal
forwarding/alias-recursion failure is not reproduced. Static TLS increased from
18 to 314 bytes, but the collected logs do not prove TLS exhaustion. The exact
reason Steam's JavaScript context failed remains unproven; do not invent it from
the visible hang or unrelated historical graphics/update messages.

## Repair and better acceptance checks

The optional connection object now contains only five hidden original veneers;
its sole public export is the own audit bridge. All unrelated public libc calls
bind directly to native libc. Only authenticated actual-client-to-libc audit
bindings redirect the five intended calls, retaining native DEEPBIND flags,
original entry snapshots, arguments, results, errno and unwind behavior.
Legacy ordinary/startup entry objects retain their original bytes and CFI.

The one-shot scope moved from each thread's static TLS to process-lifetime state
owned exclusively by the permanent atomic sampler claim. Only that live owner
accesses mutable scope fields. A fresh thread's zero marker prevents reuse of a
previous owner's TLS address; global completion excludes later unrelated RPCs.
Actual final `PT_TLS` is again 18 bytes. This removes unnecessary footprint;
it is not evidence that TLS caused the failed UI.

The original 29-check inert fixture explicitly required the global public
overrides. It tested their basic forwarding, while accepting the wrong scope
for a service-wide preload. The revised fixture requires native public bindings
instead, and rejects the actual failed ELF. Connection audit fixtures separately
prove the intended five redirects and zero unscoped callbacks. ABI controls
reject reintroducing any of the five public names. Artifact preparation and
session staging also reject the old broad-interception metadata.

Focused root glue/gate checks, actual final ARM preload (three cases, 29 checks
each), native selective audit controls and independent final artifact/bundle
review (147 checks) pass. Passing unchanged evidence was reused rather than
rerunning a full local gate after each bounded repair. No new private field,
packet, pixel, shader, mask or PC Steam sender modification was introduced.
The integrated repair at `38b5b16` also passed
[CI](https://github.com/McFredward/steam-frame-chroma-key/actions/runs/38295295162).

The repaired artifact is documented in
[CLIENT-CONNECT-OBSERVATION.md](CLIENT-CONNECT-OBSERVATION.md). A subsequent
[coordinated hardware run](CLIENT-CONNECT-CAPTURE-20261012.md) confirmed normal
Frame startup, a visible PC-VR stream and a successful native connection call.
This supports the narrowed scope repair without proving the earlier failure's
exact internal instruction chain. The diagnostic was fully removed afterward.
The Frame remains restored; no automatic redeployment/restart is scheduled.
