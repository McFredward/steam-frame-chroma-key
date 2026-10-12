# Startup mapped-client loader lease

`frame_import_loader::Reader::verify` supplies the production mapped reader for
`frame_import_module::verify_startup`. It enumerates with the prepared real public
`dl_iterate_phdr` provider and hashes the client inside the exact matching loader
callback. The adapter never accepts a filesystem path, arbitrary caller-provided
mapping ranges or an external read callback as evidence of a live client mapping.

The startup owner constructs one reader with the immutable reviewed profile,
the original main-namespace libc provider, and a local lifetime label. Root's
public-forwarder preflight must validate that provider before construction;
passing an arbitrary function pointer does not establish the contract below.
Call `verify(original_dladdr_info, original_entry.x[30])` only through the guarded
startup gate. Different threads, repeated uses and adapter callback reentry fail
closed. The reader makes no lookup, file access, allocation or additional observer
lock. The native iterator does take its own loader lock during this one startup
operation; this mechanism must not run on an ordinary graphics/import callback.

## Why the mapped bytes remain owned

The glibc 2.39 iterator holds `dl_load_write_lock` across the application callback
and releases it on normal or exceptional exit. It supplies `l_real`'s load bias,
program-header pointer and count. Therefore these public metadata are loader-owned
within the callback; the adapter copies at most eight PHDRs instead of retaining
the pointer. [glibc 2.39 iterator source](https://github.com/bminor/glibc/blob/glibc-2.39/elf/dl-iteratephdr.c)

The same glibc release takes `dl_load_write_lock` before the removal/unmap loop in
`_dl_close_worker` and releases it after that loop. A concurrent native `dlclose`
cannot unmap the selected object while the iterator callback holds this lock.
Destructors and some native close bookkeeping precede this lock: the guarantee
is mapped-code availability, not that the object is still logically usable by
arbitrary application code. [glibc 2.39 close source](https://github.com/bminor/glibc/blob/glibc-2.39/elf/dl-close.c)

The mutex is recursive. Glibc initializes the loader's mutex function pointers
from the main namespace's libc `pthread_mutex_lock` and `pthread_mutex_unlock`.
The production callback only performs bounded copies and hashing: no `dlclose`,
`dlopen`, `dladdr`, recursive observer entry, cancellation point, mapping mutation
or user callback is invoked. A same-thread recursive close would not be blocked;
it is explicitly outside this contract. [glibc 2.39 mutex initialization source](https://github.com/bminor/glibc/blob/glibc-2.39/sysdeps/nptl/dl-mutex.c)

## Current Frame provider audit

The retained SteamOS 0.4.5 provider files were checked privately. These hashes
identify audited file inputs, not an unobserved runtime mapping.

| Input | SHA-256 |
| --- | --- |
| `libc.so.6` | `be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b` |
| `ld-linux-aarch64.so.1` | `2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e` |

The actual libc reports glibc 2.39 and exports `dl_iterate_phdr@@GLIBC_2.17`.
Its iterator uses `_rtld_global + 0xab8` for both lock and unlock around the
callback. The retained loader's corresponding mutex has recursive kind 1;
its close unmap region uses that same object. Actual mutex-provider initialization
resolves the main libc's public mutex exports. The private audit checks exact
instructions, relocation, data initialization and the actual munmap call path;
no Valve code or disassembly is distributed.

## Bounds and identity

The selected `dlpi_addr` must equal the **original** public `Dl_info.dli_fbase`.
The original caller PC must belong to an actual readable executable file-backed
LOAD interval. Up to eight program headers and 8 MiB of mapped extents are
accepted. Overflows, writable executable segments, overlapping RX extents,
missing metadata and excessive enumeration fail closed.

Only RX file bytes become readable through the internal callback capability.
Every copy is within an actual loader-derived interval and limited to 4096 bytes.
RW data, BSS, gaps, page padding and non-executable RO data are excluded. The
production path also requires exact PHDR metadata from the reviewed profile,
then the existing verifier checks the fixed caller RVA, mapped ELF header,
PHDR bytes, GNU build ID and all RX hashes. The borrowed capability is invalidated
before callback exit; no native PHDR pointer or mapped reader escapes.

The local label is bookkeeping only. The returned copied identity **does not
keep the loader lock or prevent a later unload**. Root must retire its observation
on shutdown/unload/loss and must bind this startup observation to the same live
native entry. A stale `Dl_info` with a removed module fails; identical-code
unload/reload at an identical address before enumeration cannot be distinguished
from `Dl_info` alone. Do not promote address/hash equality into an independently
proven uninterrupted historical module lifetime. Direct third-party `munmap`,
`mprotect`, text writes or asynchronous loader calls are not protected by this
loader contract. This adapter provides no source frame, resource namespace, UV,
accepted submission, GPU ownership or passthrough proof.

The public scoped inspection helper exercises the same range builder with a
real host DSO. Its visitor is strictly borrowed and forbids loader reentry,
retention and blocking work. The close-race fixture deliberately delays only its
offline test visitor to corroborate the lock; production verification has no
such wait. Host lease checks and private Frame source checks are separate from a
future on-device runtime identity observation.

Cold main-observer preflight additionally uses `inspect_cold`: at most sixteen
public PHDRs, a sixteen-MiB mapped virtual span and eight MiB of readonly file
bytes. This supports the actual receiver/libc ten-header tables and receiver
BSS extent, as well as ELF/note metadata in separate readonly segments.
`program_header` copies public metadata only while the callback capability is
active. Readback still excludes writable data, BSS, gaps and page padding.
The existing client `verify` and ordinary `inspect` retain their original
eight-header/eight-MiB/RX-only rules; cold preflight is never a general private
native-memory reader or an ordinary import/render operation.
