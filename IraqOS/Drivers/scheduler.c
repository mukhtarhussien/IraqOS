#include "scheduler.h"

static process_t *processes[MAX_PROCESSES];

static int process_count = 0;
static int current_index = -1;
static process_t *current_process = 0;

void scheduler_init(void)
{
    process_count = 0;
    current_index = -1;
    current_process = 0;

    for (int i = 0; i < MAX_PROCESSES; i++)
        processes[i] = 0;
}

int scheduler_add(process_t *process)
{
    if (process == 0)
        return -1;

    if (process_count >= MAX_PROCESSES)
        return -1;

    processes[process_count] = process;
    process_count++;

    process->state = PROCESS_READY;

    return 0;
}

process_t *scheduler_next(void)
{
    if (process_count == 0)
        return 0;

    for (int i = 0; i < process_count; i++)
    {
        current_index++;

        if (current_index >= process_count)
            current_index = 0;

        process_t *next = processes[current_index];

        if (next == 0)
            continue;

        if (next->state != PROCESS_READY)
            continue;

        if (current_process != 0 &&
            current_process != next &&
            current_process->state == PROCESS_RUNNING)
        {
            current_process->state = PROCESS_READY;
        }

        next->state = PROCESS_RUNNING;
        current_process = next;

        return next;
    }

    return 0;
}

process_t *scheduler_current(void)
{
    return current_process;
}

void scheduler_start(void)
{
    process_t *first = scheduler_next();

    if (first == 0)
        return;

    static cpu_context_t boot_context;

    context_switch(&boot_context, &first->context);
}
    /* mukhtar loves HATSUNE MIKU!!!
     * Alhayat_Programing_center
     * 2026/8/10 at 1:44PM :)
     */

