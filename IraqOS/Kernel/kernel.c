#include "printk.h"
#include "../Memory/memory.h"

void kernel_main(void)
{
    memory_init();

    void *p1 = kmalloc(16);
    void *p2 = kmalloc(32);

    printk("IRAQOS KERNEL STARTED!\n");
    printk(p1 ? "MEMORY TEST 1 OK\n" : "MEMORY TEST 1 FAILED\n");
    printk(p2 ? "MEMORY TEST 2 OK\n" : "MEMORY TEST 2 FAILED\n");
}
