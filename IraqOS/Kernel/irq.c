#include "timer.h"

#define TIMER_INTID 30

/*
 * IRQ handler.
 *
 * CPU said:
 * "Someone interrupted me."
 *
 * We ask:
 * "Who?"
 *
 * If it's the timer:
 * congratulations, it's our problem.
 */
void irq_handler(unsigned long intid)
{
    /*
     * Timer PPI 30.
     *
     * If something else shows up here,
     * don't randomly reset the timer like a maniac.
     */
    if (intid == TIMER_INTID)
    {
        /*
         * Reset timer.
         *
         * Congratulations, you get another 10ms.
         */
        timer_reset();
    }
}
