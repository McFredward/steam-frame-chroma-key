// Optional passive client-connect diagnostic. Reuse the exact reviewed entry
// and CFI macro; the ordinary eleven-entry and two-entry startup objects retain
// their existing symbol sets. This does not establish socket ownership.
    .set FRAME_CONNECTION_ENTRY_BUILD, 1
#if defined(FRAME_CLIENT_RESOURCE_THREAD_PREPARATION)
    .set FRAME_RESOURCE_THREAD_ENTRY_BUILD, 1
#endif
#include "native_import_entry.S"
