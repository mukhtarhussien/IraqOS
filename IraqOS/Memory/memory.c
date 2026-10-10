#include "memory.h"

#define HEAP_SIZE 65536

static unsigned char heap[HEAP_SIZE];
static unsigned long heap_index = 0;

void memory_init(void)
{
    heap_index = 0;
}

void* kmalloc(unsigned long size)
{
    if (heap_index + size > HEAP_SIZE)
        return 0;

    void *ptr = &heap[heap_index];

    heap_index += size;

    return ptr;
}
