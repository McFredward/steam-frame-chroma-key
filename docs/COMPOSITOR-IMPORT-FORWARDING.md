# Import entry forwarding checkpoint

This component is built locally and remains uninstalled. It is not the complete
native import observer and does not enable explicit-mask passthrough.

## Actual forwarding

The optional `FRAME_MASK_BUILD_IMPORT_ENTRY` AArch64 build links eleven hidden
entry veneers. They capture native x0..x30/SP before a C++ prologue, forward the
native result and retain unwind rules. Eight dispatchers use the actual distinct
GLIBC_2.17 aliases for mutex init/destroy/lock/unlock, read/write, close and
connect. No render-thread symbol lookup or raw syscall substitutes for libc.

The three Vulkan dispatchers reuse the layer's already resolved per-device
`vkCreateImage`, `vkBindImageMemory` and `vkCreateImageView` providers. Both GPA
routes return the true-entry veneer when the existing passive binding capture
is enabled. Unsupported commands retain native forwarding; no new VkResult is
manufactured. Device lookup uses the same loader record as ordinary layer hooks.
The change adds no observer lookup mutex or resource-generation table.

Callbacks are inactive until a caller explicitly registers an immutable,
process-lifetime descriptor before sampling threads. They receive copied
registers and public scalar metadata. Input create structures are borrowed only
during synchronous publication into the existing bounded journal; they are
never retained for deferred reading. An externally validated Before label can
tag creation, but it is neither a resource generation nor a video-frame key.
Recursive or partially registered observations set sticky evidence loss.

The background owner now validates receipts through
`Journal::validateImportResourceReceipt`, after `drain()` has returned. Active
publishers, unread lifecycle tickets, loss, closed capture or retired/recreated
resources refuse validation. A receipt callback inside `drain()` cannot claim
the journal has already caught up. Validation is CPU evidence, not a resource
lifetime pin or GPU completion.

## Focused evidence

The local focused gate passes:

* 87 real-host libc checks, also ASan/UBSan, including positive partial reads
  and writes, EOF, native failures, real abstract Unix connect, and read/write/
  close cancellation without a false completion callback.
* 79 Vulkan checks, also ASan/UBSan: actual local software-driver image,
  allocation, binding and view calls resolve through the real journal; original
  pointers/results/errno, native provider unwind and recursive-loss behavior
  are checked separately with controlled providers.
* 695 resource-receipt checks, also ASan/UBSan, including background validation
  during an active unpublished ticket and before/after resource retirement.
* 2,816 AArch64 machine-byte model executions across eleven entries, with CFI
  checked at every instruction; twelve mutated-object controls are rejected.
* 894 checks execute all eleven actual AArch64 veneers through QEMU with the
  hash-pinned Frame libc/loader. All eight GLIBC_2.17 aliases are verified through
  real `dladdr`; read/write/close cancellation unwinds into caller cleanup.
  Two executed register/CFI mutants fail at their intended controls. Vulkan
  providers in this fixture are controlled ABI stubs, not a Frame GPU.
* The actual AArch64 layer links with `--no-undefined`. All eleven entries and
  dispatchers are local, and all eight distinct libc aliases have the expected
  GLIBC_2.17 imports. Cross-building this dispatch layer needs Vulkan headers,
  not a fictitious foreign Vulkan loader library.

The complete CI at `2edf052` passed. This checkpoint's local evidence
is focused, not a fresh complete local gate. Current source hashes, small logs
and the reviewable linked artifact are retained privately under
`build/private-import-vulkan-root-evidence-20261012/`; completed CMake and
sanitizer caches were removed.

## Remaining activation gates

No public libc interposition aliases, native-memory acquisition adapter or
automatic registration are enabled by this checkpoint. Before any deployment,
bind the copied events to the exact current module maps, owned native windows,
observed pipe lifetimes, immutable successful reply and receiver publication.
The new private scalar scope requires a concrete reviewed diagnostic and
separate authorization. QEMU execution establishes actual ARM64 register/CFI
and current-provider forwarding behavior under host kernel translation. It
does not establish Frame-native kernel/cancellation or headset behavior,
cross-endpoint association, or PC source-frame/eye/UV/GPU ownership.

The optional CMake native-execution test requires both explicit
`FRAME_MASK_IMPORT_QEMU` and `FRAME_MASK_IMPORT_PROVIDERS` paths. Private provider
files are neither downloaded automatically nor added to public CI. It retains
a bounded JSON receipt and removes its completed temporary build directory.
