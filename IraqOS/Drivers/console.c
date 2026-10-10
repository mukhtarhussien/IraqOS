#include "console.h"
#include "uart.h"
#include "virtio_gpu.h"

#define TEXT_X 16
#define TEXT_Y 16
#define DEFAULT_GLYPH_SCALE 2
#define DEFAULT_TEXT_COLOR 0xFFFFFFFFu
#define CONSOLE_BACKGROUND 0xFF101010u

static int cursor_x;
static int cursor_y;
static int flush_busy;
static uint32_t text_color;
static int font_scale;

static const uint8_t font5x7[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static const uint8_t lower_font5x7[26][7] = {
    {0,0,14,1,15,17,15}, {16,16,30,17,17,17,30},
    {0,0,14,16,16,17,14}, {1,1,15,17,17,17,15},
    {0,0,14,17,31,16,14}, {6,8,30,8,8,8,8},
    {0,15,17,17,15,1,14}, {16,16,30,17,17,17,17},
    {4,0,12,4,4,4,14}, {2,0,6,2,2,18,12},
    {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14},
    {0,0,26,21,21,21,21}, {0,0,30,17,17,17,17},
    {0,0,14,17,17,17,14}, {0,30,17,17,30,16,16},
    {0,15,17,17,15,1,1}, {0,0,22,25,16,16,16},
    {0,0,15,16,14,1,30}, {8,8,30,8,8,9,6},
    {0,0,17,17,17,19,13}, {0,0,17,17,17,10,4},
    {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17},
    {0,0,17,17,15,1,14}, {0,0,31,2,4,8,31}
};

static void glyph_rows(char c, uint8_t rows[7])
{
    if (c >= 'A' && c <= 'Z') {
        const uint8_t *src = font5x7[c - 'A'];
        for (int i = 0; i < 7; i++) rows[i] = src[i];
        return;
    }
    if (c >= 'a' && c <= 'z') {
        const uint8_t *src = lower_font5x7[c - 'a'];
        for (int i = 0; i < 7; i++) rows[i] = src[i];
        return;
    }
    if (c >= '0' && c <= '9') {
        const uint8_t *src = font5x7[26 + c - '0'];
        for (int i = 0; i < 7; i++) rows[i] = src[i];
        return;
    }

    for (int i = 0; i < 7; i++) rows[i] = 0;
    switch (c) {
    case '=': rows[1]=31; rows[3]=31; rows[5]=31; break;
    case '+': rows[1]=4; rows[2]=4; rows[3]=31; rows[4]=4; rows[5]=4; break;
    case '*': rows[1]=10; rows[2]=4; rows[3]=31; rows[4]=4; rows[5]=10; break;
    case ',': rows[5]=4; rows[6]=8; break;
    case ';': rows[2]=4; rows[4]=4; rows[5]=4; rows[6]=8; break;
    case '\'': rows[0]=4; rows[1]=4; break;
    case '"': rows[0]=10; rows[1]=10; break;
    case '|': rows[0]=4; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=4; break;
    case '\\': rows[0]=16; rows[1]=8; rows[2]=8; rows[3]=4; rows[4]=2; rows[5]=2; rows[6]=1; break;
    case '<': rows[1]=2; rows[2]=4; rows[3]=8; rows[4]=4; rows[5]=2; break;
    case '#': rows[1]=10; rows[2]=31; rows[3]=10; rows[4]=31; rows[5]=10; break;
    case '$': rows[0]=4; rows[1]=15; rows[2]=20; rows[3]=14; rows[4]=5; rows[5]=30; rows[6]=4; break;
    case '@': rows[0]=14; rows[1]=17; rows[2]=23; rows[3]=21; rows[4]=23; rows[5]=16; rows[6]=14; break;
    case '-': rows[3]=31; break;
    case ':': rows[2]=4; rows[5]=4; break;
    case '!': rows[0]=4; rows[1]=4; rows[2]=4; rows[3]=4; rows[5]=4; break;
    case '[': rows[0]=14; rows[1]=8; rows[2]=8; rows[3]=8; rows[4]=8; rows[5]=8; rows[6]=14; break;
    case ']': rows[0]=14; rows[1]=2; rows[2]=2; rows[3]=2; rows[4]=2; rows[5]=2; rows[6]=14; break;
    case '>': rows[1]=16; rows[2]=8; rows[3]=4; rows[4]=8; rows[5]=16; break;
    case '.': rows[6]=4; break;
    case '?': rows[0]=14; rows[1]=17; rows[2]=1; rows[3]=2; rows[4]=4; rows[6]=4; break;
    case '_': rows[6]=31; break;
    case '/': rows[0]=1; rows[1]=2; rows[2]=2; rows[3]=4; rows[4]=8; rows[5]=8; rows[6]=16; break;
    case '(': rows[0]=2; rows[1]=4; rows[2]=8; rows[3]=8; rows[4]=8; rows[5]=4; rows[6]=2; break;
    case ')': rows[0]=8; rows[1]=4; rows[2]=2; rows[3]=2; rows[4]=2; rows[5]=4; rows[6]=8; break;
    default: break;
    }
}

static void newline(void)
{
    int glyph_height = 9 * font_scale;
    cursor_x = TEXT_X;
    cursor_y += glyph_height;
    if (cursor_y + glyph_height > UI_HEIGHT) {
        fb_scroll_up(glyph_height, 0xFF101010u);
        cursor_y -= glyph_height;
    }
}

static void draw_char(char c)
{
    if (c == '\r') return;
    if (c == '\n') { newline(); return; }
    int glyph_scale = font_scale;
    int glyph_width = 6 * glyph_scale;
    if (cursor_x + glyph_width > UI_WIDTH) newline();

    uint8_t rows[7];
    glyph_rows(c, rows);
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 5; x++) {
            if (rows[y] & (1u << (4 - x)))
                fb_fill_rect(cursor_x + x * glyph_scale,
                             cursor_y + y * glyph_scale,
                             glyph_scale, glyph_scale, text_color);
        }
    }
    cursor_x += glyph_width;
}

static void flush_if_ready(void)
{
    if (!flush_busy && virtio_gpu_is_ready()) {
        flush_busy = 1;
        virtio_gpu_flush();
        flush_busy = 0;
    }
}

void console_init(void)
{
    cursor_x = TEXT_X;
    cursor_y = TEXT_Y;
    flush_busy = 0;
    text_color = DEFAULT_TEXT_COLOR;
    font_scale = DEFAULT_GLYPH_SCALE;
}

void console_clear(uint32_t color)
{
    fb_clear(color);
    cursor_x = TEXT_X;
    cursor_y = TEXT_Y;
}

void console_draw_text_at(int x, int y, const char *text, uint32_t color)
{
    int old_x = cursor_x;
    int old_y = cursor_y;
    uint32_t old_color = text_color;
    cursor_x = x;
    cursor_y = y;
    text_color = color;
    while (*text) draw_char(*text++);
    cursor_x = old_x;
    cursor_y = old_y;
    text_color = old_color;
}

void console_fill_rect(int x, int y, int width, int height, uint32_t color)
{
    fb_fill_rect(x, y, width, height, color);
}

void console_present(void)
{
    flush_if_ready();
}

void console_erase_char(void)
{
    int glyph_width = 6 * font_scale;
    int glyph_height = 9 * font_scale;
    if (cursor_x < TEXT_X + glyph_width) return;
    cursor_x -= glyph_width;
    fb_fill_rect(cursor_x, cursor_y, glyph_width, glyph_height, CONSOLE_BACKGROUND);
    flush_if_ready();
}

void console_set_font_scale(int scale)
{
    if (scale < 2) scale = 2;
    if (scale > 5) scale = 5;
    font_scale = scale;
}

int console_get_font_scale(void)
{
    return font_scale;
}

void console_putc(char c)
{
    draw_char(c);
    char str[2] = {c, '\0'};
    uart_print(str);
    flush_if_ready();
}

void console_write(const char *text)
{
    const char *start = text;
    while (*text) draw_char(*text++);
    uart_print(start);
    flush_if_ready();
}
