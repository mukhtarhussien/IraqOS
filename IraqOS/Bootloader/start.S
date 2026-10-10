.section .text
.global _start
.extern boot_main

_start:
    ldr x0, =stack_top
    mov sp, x0

    bl boot_main

1:
    wfe
    b 1b


.section .bss
.align 12

stack:
    /* The GUI and serial terminal use more than a page of nested C frames. */
    .skip 65536

stack_top:
