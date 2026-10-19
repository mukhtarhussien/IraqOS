.text
.align 11

.global exception_vector_table

exception_vector_table:

    /* EL1t */

    .align 7
    b sync_invalid_el1t

    .align 7
    b irq_invalid_el1t

    .align 7
    b fiq_invalid_el1t

    .align 7
    b error_invalid_el1t


    /* EL1h */

    .align 7
    b sync_el1h

    .align 7
    b irq_el1h

    .align 7
    b fiq_invalid_el1h

    .align 7
    b error_invalid_el1h


    /* EL0 64-bit */

    .align 7
    b sync_invalid_el0_64

    .align 7
    b irq_invalid_el0_64

    .align 7
    b fiq_invalid_el0_64

    .align 7
    b error_invalid_el0_64


    /* EL0 32-bit */

    .align 7
    b sync_invalid_el0_32

    .align 7
    b irq_invalid_el0_32

    .align 7
    b fiq_invalid_el0_32

    .align 7
    b error_invalid_el0_32


/* -------------------------------------------------- */
/* Invalid handlers                                   */
/* -------------------------------------------------- */

/*
 * إذا وصلنا هنا، الـCPU اكتشف مشكلة
 * وقرر ما يخبرنا شنو هي.
 */

sync_invalid_el1t:
    b .

irq_invalid_el1t:
    b .

fiq_invalid_el1t:
    b .

error_invalid_el1t:
    b .


sync_el1h:
    b .

fiq_invalid_el1h:
    b .

error_invalid_el1h:
    b .


sync_invalid_el0_64:
    b .

irq_invalid_el0_64:
    b .

fiq_invalid_el0_64:
    b .

error_invalid_el0_64:
    b .


sync_invalid_el0_32:
    b .

irq_invalid_el0_32:
    b .

fiq_invalid_el0_32:
    b .

error_invalid_el0_32:
    b .


/* -------------------------------------------------- */
/* IRQ handler                                        */
/* -------------------------------------------------- */

irq_el1h:

    /*
     * Exception frame:
     *
     * x0-x30  = 31 registers
     * ELR_EL1
     * SPSR_EL1
     * INTID
     *
     * 272 bytes keeps SP 16-byte aligned.
     */
    sub sp, sp, #272

    /*
     * Save general-purpose registers.
     *
     * لا نريد الـIRQ يخرب حياة الـprocess.
     */
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
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
    str x30,      [sp, #240]

    /*
     * Save exception return state.
     *
     * ELR = وين نرجع
     * SPSR = شلون نرجع
     */
    mrs x20, elr_el1
    mrs x21, spsr_el1

    str x20, [sp, #248]
    str x21, [sp, #256]

    /*
     * Read interrupt ID.
     *
     * "منو دگ الباب؟"
     */
    mrs x0, ICC_IAR1_EL1

    /*
     * Save INTID because x0 is caller-saved.
     */
    str x0, [sp, #264]

    /*
     * Call C IRQ handler.
     */
    bl irq_handler

    /*
     * Restore INTID for EOI.
     */
    ldr x22, [sp, #264]

    /*
     * Complete interrupt.
     *
     * "تمام، عرفناك. روح."
     */
    msr ICC_EOIR1_EL1, x22

    isb

    /*
     * Restore exception return state.
     */
    ldr x20, [sp, #248]
    ldr x21, [sp, #256]

    msr elr_el1, x20
    msr spsr_el1, x21

    /*
     * Restore general-purpose registers.
     */
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30,      [sp, #240]

    add sp, sp, #272

    /*
     * Return from exception.
     *
     * إذا هذا ما اشتغل، نروح نشرب چاي.
     */
    eret
