#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

#define MAX_PROCESSES 16

void scheduler_init(void);
int scheduler_add(process_t *process);

process_t *scheduler_next(void);
process_t *scheduler_current(void);

void scheduler_start(void);

#endif
