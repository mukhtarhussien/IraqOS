
/*
 * Invalid exception handlers.
 *
 * إذا وصلنا هنا فالمعالج قرر:
 * "خليني أخرب يومهم شوي."
 */

sync_invalid_el1t:
    b sync_invalid_el1t

irq_invalid_el1t:
    b irq_invalid_el1t

fiq_invalid_el1t:
    b fiq_invalid_el1t

error_invalid_el1t:
    b error_invalid_el1t

sync_el1h:
    b sync_el1h

/*
 * Invalid exception handlers.
 *
 * إذا وصلنا هنا فالمعالج قرر:
 * "خليني أخرب يومهم شوي."
 */

sync_invalid_el1t:
    b sync_invalid_el1t

irq_invalid_el1t:
    b irq_invalid_el1t

fiq_invalid_el1t:
    b fiq_invalid_el1t

error_invalid_el1t:
    b error_invalid_el1t

sync_el1h:
    b sync_el1h

/*
 * Invalid exception handlers.
 *
 * إذا وصلنا هنا:
 * الـCPU عنده شكوى، ونحن لا نملك خدمة العملاء.
 */

sync_invalid_el1t:
    b sync_invalid_el1t

irq_invalid_el1t:
    b irq_invalid_el1t

fiq_invalid_el1t:
    b fiq_invalid_el1t

error_invalid_el1t:
    b error_invalid_el1t

sync_el1h:
    b sync_el1h
