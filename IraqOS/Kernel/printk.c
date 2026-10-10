#include "printk.h"
#include "../Drivers/console.h"

void printk(const char *text)
{
    console_write(text);
}
