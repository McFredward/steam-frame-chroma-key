// Offline entry adapter, deliberately NOT exported as public libc/Vulkan APIs.
// The C dispatchers and real-provider/lifecycle gates are not implemented here.
// Native entry context is saved before any compiler prologue or callback.
// Keep these veneers uninstalled until their complete forwarding path is reviewed.
#if !defined(__aarch64__)
#error "This entry ABI is AArch64-only"
#endif

    .text

    .macro IMPORT_ENTRY name, dispatcher
    .balign 4
    .global \name
    .hidden \name
    .type \name, %function
\name:
    .cfi_startproc
    sub sp, sp, #256
    .cfi_def_cfa_offset 256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    .cfi_offset 19, -104
    .cfi_offset 20, -96
    .cfi_offset 21, -88
    .cfi_offset 22, -80
    .cfi_offset 23, -72
    .cfi_offset 24, -64
    .cfi_offset 25, -56
    .cfi_offset 26, -48
    .cfi_offset 27, -40
    .cfi_offset 28, -32
    .cfi_offset 29, -24
    .cfi_offset 30, -16
    add x9, sp, #256
    str x9, [sp, #248]
    mov x0, sp
    bl \dispatcher
    // x0 is the forwarded native result. Other integer registers are restored;
    // standard C dispatchers preserve platform and callee-saved vector state.
    ldr x1, [sp, #8]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    .cfi_restore 19
    ldp x20, x21, [sp, #160]
    .cfi_restore 20
    .cfi_restore 21
    ldp x22, x23, [sp, #176]
    .cfi_restore 22
    .cfi_restore 23
    ldp x24, x25, [sp, #192]
    .cfi_restore 24
    .cfi_restore 25
    ldp x26, x27, [sp, #208]
    .cfi_restore 26
    .cfi_restore 27
    ldp x28, x29, [sp, #224]
    .cfi_restore 28
    .cfi_restore 29
    ldr x30, [sp, #240]
    .cfi_restore 30
    add sp, sp, #256
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
    .size \name, .-\name
    .endm

    IMPORT_ENTRY frame_import_lock_entry, frame_import_lock_dispatch
    IMPORT_ENTRY frame_import_unlock_entry, frame_import_unlock_dispatch
    IMPORT_ENTRY frame_import_write_entry, frame_import_write_dispatch
    IMPORT_ENTRY frame_import_image_entry, frame_import_image_dispatch
    IMPORT_ENTRY frame_import_bind_entry, frame_import_bind_dispatch
    IMPORT_ENTRY frame_import_view_entry, frame_import_view_dispatch
    IMPORT_ENTRY frame_import_init_entry, frame_import_init_dispatch
    IMPORT_ENTRY frame_import_destroy_entry, frame_import_destroy_dispatch
    IMPORT_ENTRY frame_import_close_entry, frame_import_close_dispatch

    .section .note.GNU-stack,"",%progbits
