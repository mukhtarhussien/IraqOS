#ifndef CONTEXT_H
#define CONTEXT_H

typedef struct
{
    unsigned long x19;
    unsigned long x20;
    unsigned long x21;
    unsigned long x22;
    unsigned long x23;
    unsigned long x24;
    unsigned long x25;
    unsigned long x26;
    unsigned long x27;
    unsigned long x28;
    unsigned long x29;
    unsigned long x30;

    unsigned long sp;
    unsigned long pc;

} cpu_context_t;

void context_switch(cpu_context_t *old, cpu_context_t *new);

#endif
