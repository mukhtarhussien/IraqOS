#ifndef UART_H
#define UART_H

void uart_putc(char c);
void uart_print(const char *str);
char uart_getc(void);
int uart_try_getc(char *value);

#endif
