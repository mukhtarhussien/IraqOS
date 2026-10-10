#include "process.h"
#include "../../Memory/memory.h"

static int next_pid = 1;

process_t *process_create(void (*entry)(void))
{
    process_t *process = kmalloc(sizeof(process_t));

    if (process == 0)
        return 0;

    process->pid = next_pid++;
    process->state = PROCESS_READY;
    process->stack = kmalloc(4096);
    process->entry = entry;

    if (process->stack == 0)
        return 0;

    process->context.x19 = 0;
    process->context.x20 = 0;
    process->context.x21 = 0;
    process->context.x22 = 0;
    process->context.x23 = 0;
    process->context.x24 = 0;
    process->context.x25 = 0;
    process->context.x26 = 0;
    process->context.x27 = 0;
    process->context.x28 = 0;
    process->context.x29 = 0;
    process->context.x30 = 0;

    process->context.sp =
        (unsigned long)process->stack + 4096;

    process->context.pc =
        (unsigned long)entry;

    return process;
}
//mukhtar loves Tepa!! today at monday 2026/8/10 8:53PM
