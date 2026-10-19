#include "timer.h"
#include "gic.h"

extern void exception_vector_table(void);

void timer_init(void)
{
    unsigned long vbar = (unsigned long)&exception_vector_table;

    __asm__ volatile(
        "msr vbar_el1, %0"
        :
        : "r"(vbar)
    );

    __asm__ volatile("isb");

    /*
     * Initialize GIC before enabling interrupts.
     */
    gic_init();

    unsigned long freq;

    __asm__ volatile(
        "mrs %0, cntfrq_el0"
        : "=r"(freq)
    );

    unsigned long interval = freq / 100;

    __asm__ volatile(
        "msr cntp_tval_el0, %0"
        :
        : "r"(interval)
    );

    /*
     * Enable physical timer.
     */
    unsigned long enable = 1;

    __asm__ volatile(
        "msr cntp_ctl_el0, %0"
        :
        : "r"(enable)
    );

    __asm__ volatile("isb");

    /*
     * Unmask IRQ at CPU level.
     */
    __asm__ volatile("msr daifclr, #2");

    __asm__ volatile("isb");
}

void timer_reset(void)
{
    unsigned long freq;

    __asm__ volatile(
        "mrs %0, cntfrq_el0"
        : "=r"(freq)
    );

    unsigned long interval = freq / 100;

    __asm__ volatile(
        "msr cntp_tval_el0, %0"
        :
        : "r"(interval)
    );

    __asm__ volatile("isb");
}
