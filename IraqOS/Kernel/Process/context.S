.text

.global context_switch

context_switch:
    // x0 = old context
    // x1 = new context

    // احتفظ بمؤشري الـcontexts في registers مؤقتة
    mov x16, x0
    mov x17, x1

    // Save x19-x30
    stp x19, x20, [x16, #0]
    stp x21, x22, [x16, #16]
    stp x23, x24, [x16, #32]
    stp x25, x26, [x16, #48]
    stp x27, x28, [x16, #64]
    stp x29, x30, [x16, #80]

    // Save current SP
    mov x9, sp
    str x9, [x16, #96]

    // Save address to return to
    adr x9, resume_old
    str x9, [x16, #104]

    // Load x19-x30
    ldp x19, x20, [x17, #0]
    ldp x21, x22, [x17, #16]
    ldp x23, x24, [x17, #32]
    ldp x25, x26, [x17, #48]
    ldp x27, x28, [x17, #64]
    ldp x29, x30, [x17, #80]

    // Load new SP
    ldr x9, [x17, #96]
    mov sp, x9

    // Load new PC
    ldr x9, [x17, #104]
    br x9

resume_old:
    ret
    //mukhtar making his own OS saturday 2026/8/8 I LOVE MIKU!!!
