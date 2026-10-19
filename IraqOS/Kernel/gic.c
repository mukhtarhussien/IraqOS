#include "gic.h"

#define GICR_BASE        0x080A0000UL

#define GICR_WAKER       0x014
#define GICR_IGROUPR0    0x080
#define GICR_ISENABLER0  0x100
#define GICR_IPRIORITYR  0x400

#define TIMER_INTID      30

void gic_init(void)
{
    /*
     * Wake the Redistributor.
     *
     * Because apparently the CPU has to wake up
     * the interrupt controller before doing any work.
     *
     * Very professional.
     */
    volatile unsigned int *waker =
        (volatile unsigned int *)(GICR_BASE + GICR_WAKER);

    /*
     * Interrupt groups.
     *
     * Basically asking:
     * "Are you Group 1 or are you just here to waste my time?"
     */
    volatile unsigned int *igroupr =
        (volatile unsigned int *)(GICR_BASE + GICR_IGROUPR0);

    /*
     * Interrupt enable register.
     *
     * This is the ON button.
     * Please don't press it twice.
     */
    volatile unsigned int *isenabler =
        (volatile unsigned int *)(GICR_BASE + GICR_ISENABLER0);

    /*
     * Timer priority.
     *
     * Lower number = more important.
     * Just like school students, except the timer actually does something.
     */
    volatile unsigned char *priority =
        (volatile unsigned char *)(GICR_BASE +
                                    GICR_IPRIORITYR +
                                    TIMER_INTID);

    /*
     * Wake up the Redistributor.
     *
     * Wake up bro, we have interrupts to process.
     */
    *waker &= ~(1U << 1);

    /*
     * Wait until the Redistributor stops pretending to be asleep.
     *
     * ChildrenAsleep = 1
     *              ↓
     *        "I'm sleeping bro"
     *
     * ChildrenAsleep = 0
     *              ↓
     *        "fine, what do you want?"
     */
    while (*waker & (1U << 2))
    {
    }

    /*
     * Put timer interrupt 30 into Group 1.
     *
     * Congratulations timer,
     * you have been officially promoted to Group 1.
     */
    *igroupr |= (1U << TIMER_INTID);

    /*
     * Give the timer a priority.
     *
     * 0x80 because apparently numbers like 128
     * are more sophisticated when written in hex.
     */
    *priority = 0x80;

    /*
     * Turn the timer interrupt ON.
     *
     * This is where the problems begin.
     */
    *isenabler |= (1U << TIMER_INTID);

    /*
     * Enable the CPU interface.
     *
     * Tell the CPU:
     * "Yes, interrupts exist. Please don't panic."
     */
    unsigned long value = 0x7;

    __asm__ volatile(
        "msr ICC_SRE_EL1, %0"
        :
        : "r"(value)
    );

    /*
     * CPU has been informed.
     *
     * Hopefully it listened.
     */
    __asm__ volatile("isb");

    /*
     * Allow interrupt priorities.
     *
     * Basically:
     * "Fine, everyone can interrupt me."
     */
    value = 0xFF;

    __asm__ volatile(
        "msr ICC_PMR_EL1, %0"
        :
        : "r"(value)
    );

    /*
     * Enable Group 1 interrupts.
     *
     * The moment the kernel becomes everyone's problem.
     */
    value = 1;

    __asm__ volatile(
        "msr ICC_IGRPEN1_EL1, %0"
        :
        : "r"(value)
    );

    /*
     * Make absolutely sure the CPU isn't
     * doing something stupid before continuing.
     */
    __asm__ volatile("dsb sy");

    /*
     * Synchronize everything.
     *
     * ARM said so.
     * Don't ask questions.
     */
    __asm__ volatile("isb");
}

