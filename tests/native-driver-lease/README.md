# Native-driver lease focused controls

`test_pixel_native_driver_lease.cpp` includes the production implementation in
its own translation unit. Do not link `pixel_native_driver_lease.cpp` a second
time. Build `fixture.cpp` as a separate shared library with a SHA-1 GNU build ID
and pass its absolute path as the first test argument. The test owns that file:
it changes/restores its mode and briefly changes/restores its mapped RX bytes.
Never substitute a game, system driver, or another worker's library for it.

The owned host module exercises the actual cold file/ELF/maps/loader-lock/RX
verifier with an internal fixture profile. This alternate profile and the
before-stability/before-publication fault hooks are inaccessible from the public
Lease API; fault hooks are absent from the runtime library. The separate
`pixel_native_driver_lease_fixture.h` is explicitly synthetic callback-permission
scaffolding for decoder and wrapper tests. Neither is evidence that the native
Frame provider was verified.

An optional second argument is a **local copy** of the exact privately retained
ARM driver. That performs only its pinned file SHA, GNU build ID, ELF and RX hash
checks; it neither loads that driver nor proves current native module identity.
The local copy's owner policy is adjusted explicitly for that static check.
The production profile always requires the protected root-owned fixed path.

Focused controls cover all four routes; device/PFN/route/epoch mismatches;
move/revoke/explicit release; exact Btrfs mount token and longest-prefix rules;
legal unrelated escaped mounts; secure paths/FIFO rejection; changed file
identity; wrong file/build ID/architecture/owner; actual owned RX mutation;
RTLD reference retention; concurrent status/revoke; and deterministic close
immediately before Ready publication. ASan/UBSan/leak checks and strict ARM
compilation are separate evidence. No hardware test, frame/eye association,
mask publication, native visual result or passthrough is claimed.
