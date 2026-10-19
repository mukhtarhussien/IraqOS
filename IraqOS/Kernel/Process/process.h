#ifndef PROCESS_H
#define PROCESS_H

#include "context.h"

typedef enum
{
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

typedef struct
{
    int pid;
    process_state_t state;
    void *stack;
    void (*entry)(void);
    cpu_context_t context;

} process_t;

process_t *process_create(void (*entry)(void));

#endif
