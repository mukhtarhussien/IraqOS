#include "shell.h"
#include "../Kernel/printk.h"
#include "../Drivers/console.h"
#include "../Drivers/uart.h"
#include "../Drivers/virtio_gpu.h"

#define BG 0xFF101820u

static int command_is(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == *b;
}

void shell_start(const char *username)
{
    char line[64];
    console_clear(BG);
    printk("TURTULE TERMINAL\n");
    console_fill_rect(448, 20, 40, 28, 0xFF187050u);
    console_fill_rect(452, 24, 32, 20, 0xFF40D080u);
    console_fill_rect(484, 28, 16, 16, 0xFF40D080u);
    console_fill_rect(454, 44, 8, 8, 0xFF40D080u);
    console_fill_rect(474, 44, 8, 8, 0xFF40D080u);
    console_fill_rect(490, 44, 8, 8, 0xFF40D080u);
    console_fill_rect(444, 30, 8, 8, 0xFF40D080u);
    console_fill_rect(494, 31, 3, 3, 0xFFFFFFFFu);
    console_present();
    printk("HELP CLEAR WHOAMI VERSION LOGOUT\n\n");

    for (;;) {
        int length = 0;
        line[0] = '\0';
        printk("> ");
        for (;;) {
            char c = uart_getc();
            if (c == '\r' || c == '\n') {
                printk("\n");
                break;
            }
            if (c == '\b' || c == 127) {
                if (length > 0) {
                    length--;
                    line[length] = '\0';
                    uart_print("\b \b");
                    console_erase_char();
                }
                continue;
            }
            if (c >= 32 && c <= 126 && length < (int)sizeof(line) - 1) {
                line[length++] = c;
                line[length] = '\0';
                console_putc(c);
            }
        }

        if (command_is(line, "help")) {
            printk("HELP CLEAR WHOAMI VERSION LOGOUT\n");
        } else if (command_is(line, "clear")) {
            console_clear(BG);
        } else if (command_is(line, "whoami")) {
            printk(username);
            printk("\n");
        } else if (command_is(line, "version")) {
            printk("IRAQOS V0.2\n");
        } else if (command_is(line, "logout")) {
            return;
        } else if (line[0]) {
            printk("UNKNOWN COMMAND\n");
        }
    }
}
