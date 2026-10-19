#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_putc(char c);
void console_write(const char *text);
void console_clear(uint32_t color);
void console_draw_text_at(int x, int y, const char *text, uint32_t color);
void console_fill_rect(int x, int y, int width, int height, uint32_t color);
void console_present(void);
void console_erase_char(void);
void console_set_font_scale(int scale);
int console_get_font_scale(void);

#endif
