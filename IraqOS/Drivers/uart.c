#include "uart.h"

#define UART_DR 0x09000000
#define UART_FR 0x09000018

void uart_putc(char c)
{
    volatile unsigned int *dr = (unsigned int *)UART_DR;
    *dr = c;
}

void uart_print(const char *str)
{
    while (*str)
    {
        uart_putc(*str++);
    }
}

char uart_getc(void)
{
    volatile unsigned int *fr = (unsigned int *)UART_FR;
    volatile unsigned int *dr = (unsigned int *)UART_DR;

    while (*fr & (1 << 4))
    {
    }

    return (char)(*dr);
}

int uart_try_getc(char *value)
{
    volatile unsigned int *fr = (unsigned int *)UART_FR;
    volatile unsigned int *dr = (unsigned int *)UART_DR;
    if (*fr & (1u << 4))
        return 0;
    *value = (char)(*dr);
    return 1;
}
