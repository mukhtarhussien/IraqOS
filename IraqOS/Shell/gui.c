#include "gui.h"
#include "../Drivers/console.h"
#include "../Drivers/uart.h"
#include "../Drivers/virtio_gpu.h"
#include "../Drivers/virtio_input.h"

#define BG       0xFF101820u
#define PANEL    0xFF182430u
#define WHITE    0xFFFFFFFFu
#define CYAN     0xFF00D7FFu
#define BLUE     0xFFFFA040u
#define GREEN    0xFF40D080u
#define MUTED    0xFF90A0B0u
#define TERM_ROWS 12
#define TERM_COLS 128
#define TASKBAR_TOP (UI_HEIGHT - 58)
#define START_MENU_X 12
#define APP_X ((UI_WIDTH - 656) / 2)
#define START_MENU_Y (TASKBAR_TOP - 464)

static int pointer_x = UI_WIDTH / 2;
static int pointer_y = UI_HEIGHT / 2;
static int mouse_left_down;
static int mouse_press_pending;
static int terminal_drag_active;
static int terminal_x = (UI_WIDTH - (UI_WIDTH - 160)) / 2;
static int terminal_y = 76;
static int terminal_drag_dirty;
static char (*active_terminal_log)[TERM_COLS];
static const char *active_terminal_username;
static int same_word(const char *a, const char *b);
static void pointer_hide(void);
static void pointer_show(void);
static int poll_mouse_click(void);
static void draw_terminal_window(const char *username,
                                 char log[TERM_ROWS][TERM_COLS],
                                 int maximized);

static void draw_disc(int cx, int cy, int radius, uint32_t color)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int span = 0;
        while ((span + 1) * (span + 1) + dy * dy <= radius * radius)
            span++;
        console_fill_rect(cx - span, cy + dy, span * 2 + 1, 1, color);
    }
}

static void draw_glossy_button(int x, int y, int w, int h,
                               uint32_t edge, uint32_t face, uint32_t shine)
{
    console_fill_rect(x, y + 2, w, h - 2, edge);
    console_fill_rect(x + 2, y, w - 4, h - 3, face);
    console_fill_rect(x + 3, y + 1, w - 6, h / 3, shine);
    console_fill_rect(x + 2, y + h - 3, w - 4, 1, 0xFF0E344Fu);
}

static void draw_aero_titlebar(int x, int y, int width)
{
    for (int row = 0; row < 44; row += 2) {
        uint32_t shade = 0xFF000000u | ((31u + 33u * (unsigned int)row / 44u) << 16) |
                         ((112u + 50u * (unsigned int)row / 44u) << 8) |
                         (158u + 54u * (unsigned int)row / 44u);
        console_fill_rect(x, y + row, width, 2, shade);
    }
    console_fill_rect(x, y, width, 2, 0xFFD2F1FBu);
    console_fill_rect(x, y + 42, width, 2, 0xFF1A5576u);
}

static void ui_sound(const char *name)
{
    uart_print("\x1eIRAQOS_SOUND:");
    uart_print(name);
    uart_putc('\x1f');
}

static void delay_briefly(void)
{
    for (volatile unsigned int i = 0; i < 5000000; i++)
        __asm__ volatile("yield");
}

static int text_width(const char *text)
{
    int n = 0;
    while (text[n]) n++;
    return n * 6 * console_get_font_scale();
}

static void centered_text(int y, const char *text, uint32_t color)
{
    int x = (UI_WIDTH - text_width(text)) / 2;
    if (x < 8) x = 8;
    console_draw_text_at(x, y, text, color);
}

static void outline(int x, int y, int w, int h, uint32_t color)
{
    console_fill_rect(x, y, w, 3, color);
    console_fill_rect(x, y + h - 3, w, 3, color);
    console_fill_rect(x, y, 3, h, color);
    console_fill_rect(x + w - 3, y, 3, h, color);
}

void boot_splash(void)
{
    ui_sound("startup");
    static const char *stages[] = {
        "STARTING IRAQOS",
        "CHECKING MEMORY",
        "INITIALIZING DEVICES",
        "LOADING TURTULE TERMINAL",
        "READY"
    };
    const int bar_x = (UI_WIDTH - 300) / 2, bar_y = 490, bar_w = 300, bar_h = 18;

    for (int y = 0; y < UI_HEIGHT; y += 4) {
        unsigned int t = (unsigned int)y * 255u / UI_HEIGHT;
        unsigned int r = 4u + (12u * t / 255u);
        unsigned int g = 18u + (39u * t / 255u);
        unsigned int b = 39u + (58u * t / 255u);
        console_fill_rect(0, y, UI_WIDTH, 4,
            0xFF000000u | (r << 16) | (g << 8) | b);
    }
    draw_disc(UI_WIDTH / 2, 294, 150, 0xFF12365Au);
    draw_disc(UI_WIDTH / 2, 294, 118, 0xFF176392u);
    draw_disc(UI_WIDTH / 2, 294, 92, 0xFF2585B1u);
    console_fill_rect(UI_WIDTH / 2 - 26, 269, 22, 22, 0xFFF44336u);
    console_fill_rect(UI_WIDTH / 2 + 4, 269, 22, 22, 0xFFFFD54Fu);
    console_fill_rect(UI_WIDTH / 2 - 26, 299, 22, 22, 0xFF66BB6Au);
    console_fill_rect(UI_WIDTH / 2 + 4, 299, 22, 22, 0xFF42A5F5u);
    centered_text(382, "IRAQOS", WHITE);
    centered_text(426, "PERSONAL COMPUTER", 0xFFD8EAF4u);
    console_fill_rect(bar_x, bar_y, bar_w, bar_h, 0xFF071725u);
    outline(bar_x, bar_y, bar_w, bar_h, 0xFF8ABAD3u);

    for (int i = 0; i < 5; i++) {
        console_fill_rect(bar_x + 3, bar_y + 3, bar_w - 6, bar_h - 6, 0xFF071725u);
        int filled = (bar_w - 6) * (i + 1) / 5;
        console_fill_rect(bar_x + 3, bar_y + 3, filled, bar_h - 6, 0xFF39BDE1u);
        console_fill_rect(190, 534, UI_WIDTH - 380, 40, 0xFF0A2036u);
        centered_text(542, stages[i], 0xFFE8F4FAu);
        console_present();
        delay_briefly();
    }
}

void gui_login(char *username, int capacity)
{
    static const char default_user[] = "admin";
    int i = 0;

    if (capacity <= 0) return;
    while (default_user[i] && i < capacity - 1) {
        username[i] = default_user[i];
        i++;
    }
    username[i] = '\0';

    pointer_hide();
    for (int y = 0; y < UI_HEIGHT; y += 4) {
        unsigned int t = (unsigned int)y * 255u / UI_HEIGHT;
        unsigned int r = 8u + (37u * t / 255u);
        unsigned int g = 38u + (87u * t / 255u);
        unsigned int b = 77u + (105u * t / 255u);
        console_fill_rect(0, y, UI_WIDTH, 4,
            0xFF000000u | (r << 16) | (g << 8) | b);
    }
    int center = UI_WIDTH / 2;
    draw_disc(center, 310, 210, 0xFF1B5B85u);
    draw_disc(center, 310, 160, 0xFF246D9Au);
    console_fill_rect(center - 232, 224, 464, 316, 0x88203F54u);
    console_fill_rect(center - 226, 218, 452, 316, 0xFFECF3F6u);
    console_fill_rect(center - 226, 218, 452, 5, 0xFFE8FAFFu);
    draw_disc(center, 290, 54, 0xFF9CC7D9u);
    draw_disc(center, 278, 20, 0xFF527A90u);
    draw_disc(center, 309, 34, 0xFF527A90u);
    centered_text(358, username, 0xFF19394Fu);

    console_fill_rect(center - 162, 394, 318, 50, WHITE);
    outline(center - 162, 394, 318, 50, 0xFF9BB4C2u);
    console_draw_text_at(center - 142, 404, username, 0xFF304B5Cu);
    console_fill_rect(center - 162, 452, 318, 50, WHITE);
    outline(center - 162, 452, 318, 50, 0xFF9BB4C2u);
    console_draw_text_at(center - 142, 462, "****", 0xFF304B5Cu);
    draw_glossy_button(center + 172, 409, 48, 48,
                       0xFF194F73u, 0xFF318DB8u, 0xFF66B7D4u);
    console_draw_text_at(center + 188, 418, ">", WHITE);
    centered_text(582, "PRESS ENTER OR CLICK THE ARROW TO SIGN IN", WHITE);
    centered_text(632, "IRAQOS DESKTOP", 0xFFD7EAF4u);
    console_present();
    pointer_show();

    uart_print("\r\nIRAQOS READY. PRESS ENTER TO SIGN IN: ");
    for (;;) {
        if ((poll_mouse_click() & 1) && pointer_x >= UI_WIDTH / 2 + 168 &&
            pointer_x <= UI_WIDTH / 2 + 224 && pointer_y >= 400 && pointer_y <= 468) {
            uart_print("\r\nWelcome to IraqOS, admin.\r\n");
            ui_sound("login");
            pointer_hide();
            return;
        }
        char c;
        if (uart_try_getc(&c) && (c == '\r' || c == '\n')) {
            uart_print("\r\nWelcome to IraqOS, admin.\r\n");
            ui_sound("login");
            pointer_hide();
            return;
        }
        __asm__ volatile("yield");
    }
}

#define DESK_TOP 0xFF1169ABu
#define DESK_LOW 0xFF4AA4D4u
#define GLASS 0xE8183040u
#define TASK 0xFF123D68u
#define GOLD 0xFFFFC34Du

static int same_word(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}


#define GUI_MODE_DESKTOP 0
#define GUI_MODE_TERMINAL 1
#define GUI_MODE_APP 2
#define GUI_MODE_SETTINGS 3

static int gui_mode = GUI_MODE_DESKTOP;
static int gui_menu_open;
static int gui_context_open;
static int gui_context_x;
static int gui_context_y;
static int gui_terminal_maximized;
static int gui_terminal_minimized;
static int pointer_visible;
static int mouse_poll_active;
static int mouse_pointer_moved;
static uint32_t pointer_under[16][12];
static const uint16_t pointer_shape[16] = {
    0x800, 0xC00, 0xE00, 0xF00, 0xF80, 0xFC0, 0xFE0, 0xFF0,
    0xFF8, 0xF80, 0xDC0, 0x8E0, 0x060, 0x030, 0x018, 0x000
};

static void pointer_hide(void)
{
    if (!pointer_visible) return;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 12; x++)
            if (pointer_x + x < UI_WIDTH && pointer_y + y < UI_HEIGHT)
                fb_put_pixel(pointer_x + x, pointer_y + y, pointer_under[y][x]);
    pointer_visible = 0;
}

static void pointer_show(void)
{
    if (pointer_visible || !virtio_gpu_is_ready()) return;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 12; x++) {
            int px = pointer_x + x, py = pointer_y + y;
            if (px >= UI_WIDTH || py >= UI_HEIGHT) continue;
            pointer_under[y][x] = fb_get_pixel(px, py);
            int set = (pointer_shape[y] & (1u << (11 - x))) != 0;
            int edge = set && (x == 0 || x == 11 || y == 0 || y == 15);
            if (set) fb_put_pixel(px, py, edge ? 0xFF09121Au : WHITE);
        }
    }
    pointer_visible = 1;
    console_present();
}


static void mouse_click_command(char *line, int capacity)
{
    const char *action = 0;
    int x = pointer_x, y = pointer_y;
    if (gui_context_open) {
        if (x >= gui_context_x && x < gui_context_x + 210 &&
            y >= gui_context_y + 8 && y < gui_context_y + 116) {
            if (y < gui_context_y + 44) action = "REFRESH";
            else if (y < gui_context_y + 80) action = "SETTINGS";
            else action = "TERMINAL";
        } else action = "HIDE-CONTEXT";
        gui_context_open = 0;
    } else if (gui_mode == GUI_MODE_TERMINAL) {
        if (gui_terminal_minimized) {
            if (y >= TASKBAR_TOP && x >= 76 && x <= 380) action = "RESTORE";
        } else {
            int wx = gui_terminal_maximized ? 12 : terminal_x;
            int wy = gui_terminal_maximized ? 10 : terminal_y;
            int ww = gui_terminal_maximized ? UI_WIDTH - 24 : UI_WIDTH - 160;
            int right = wx + ww;
            if (y >= TASKBAR_TOP && x >= UI_WIDTH - 10) action = "MINIMIZE";
            else if (y >= wy + 10 && y <= wy + 38) {
                if (x >= right - 40 && x < right - 10) action = "CLOSE";
                else if (x >= right - 78 && x < right - 48) action = gui_terminal_maximized ? "RESTORE" : "MAXIMIZE";
                else if (x >= right - 116 && x < right - 86) action = "MINIMIZE";
            }
        }
    } else if (gui_mode == GUI_MODE_APP) {
        if (x >= APP_X + 612 && x <= APP_X + 642 && y >= 138 && y <= 166) action = "BACK";
    } else if (gui_mode == GUI_MODE_SETTINGS) {
        if (x >= APP_X + 612 && x <= APP_X + 642 && y >= 138 && y <= 166) action = "BACK";
        else if (x >= 560 && x <= 620 && y >= 250 && y <= 304) action = "FONT-";
        else if (x >= 700 && x <= 760 && y >= 250 && y <= 304) action = "FONT+";
    } else if (gui_mode == GUI_MODE_DESKTOP) {
        if (gui_menu_open && x >= START_MENU_X && x <= START_MENU_X + 450 &&
            y >= START_MENU_Y && y < TASKBAR_TOP - 8) {
            if (y >= START_MENU_Y + 377 && x >= START_MENU_X + 280) action = "LOGOUT";
            else if (x >= START_MENU_X + 283) {
                if (y >= START_MENU_Y + 72 && y < START_MENU_Y + 116) action = "COMPUTER";
                else if (y >= START_MENU_Y + 116 && y < START_MENU_Y + 160) action = "SETTINGS";
                else if (y >= START_MENU_Y + 160 && y < START_MENU_Y + 204) action = "ABOUT";
                else if (y >= START_MENU_Y + 204 && y < START_MENU_Y + 248) action = "TERMINAL";
                else if (y >= START_MENU_Y + 248 && y < START_MENU_Y + 292) action = "LOGOUT";
                else action = "HIDE-START";
            } else if (y >= START_MENU_Y + 72 && y < START_MENU_Y + 128) action = "COMPUTER";
            else if (y >= START_MENU_Y + 128 && y < START_MENU_Y + 184) action = "TERMINAL";
            else if (y >= START_MENU_Y + 184 && y < START_MENU_Y + 240) action = "SETTINGS";
            else if (y >= START_MENU_Y + 240 && y < START_MENU_Y + 296) action = "ABOUT";
            else action = "HIDE-START";
        } else if (gui_menu_open) action = "HIDE-START";
        else if (y >= TASKBAR_TOP && x <= 68) action = "START";
        else if (y >= TASKBAR_TOP && x >= 76 && x <= 380) action = "TERMINAL";
        else if (x >= 20 && x <= 145 && y >= 30 && y < 116) action = "COMPUTER";
        else if (x >= 20 && x <= 145 && y >= 118 && y < 204) action = "TERMINAL";
        else if (x >= 20 && x <= 145 && y >= 206 && y < 292) action = "SETTINGS";
    }
    if (!action) return;
    if (same_word(action, "START")) ui_sound(gui_menu_open ? "close" : "open");
    else if (same_word(action, "CLOSE") || same_word(action, "BACK") ||
             same_word(action, "LOGOUT")) ui_sound("close");
    else if (same_word(action, "TERMINAL") || same_word(action, "COMPUTER") ||
             same_word(action, "SETTINGS") || same_word(action, "ABOUT")) ui_sound("open");
    else ui_sound("click");
    int i = 0;
    while (action[i] && i < capacity - 1) { line[i] = action[i]; i++; }
    line[i] = '\0';
}

static void show_context_menu(void)
{
    pointer_hide();
    console_fill_rect(gui_context_x - 3, gui_context_y + 3, 216, 116, 0xAA0A1B2Bu);
    console_fill_rect(gui_context_x, gui_context_y, 210, 110, 0xFFEEF3F6u);
    outline(gui_context_x, gui_context_y, 210, 110, 0xFF8AA8B9u);
    console_fill_rect(gui_context_x + 2, gui_context_y + 37, 206, 1, 0xFFD2DFE6u);
    console_fill_rect(gui_context_x + 2, gui_context_y + 73, 206, 1, 0xFFD2DFE6u);
    console_draw_text_at(gui_context_x + 16, gui_context_y + 9, "REFRESH", 0xFF233D4E);
    console_draw_text_at(gui_context_x + 16, gui_context_y + 45, "SETTINGS", 0xFF233D4E);
    console_draw_text_at(gui_context_x + 16, gui_context_y + 81, "TERMINAL", 0xFF233D4E);
    console_present();
    pointer_show();
}

static void update_pointer(int next_x, int next_y, int moved)
{
    int old_x = pointer_x, old_y = pointer_y;
    int changed;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x > UI_WIDTH - 12) next_x = UI_WIDTH - 12;
    if (next_y > UI_HEIGHT - 16) next_y = UI_HEIGHT - 16;
    changed = next_x != old_x || next_y != old_y;
    if (changed) {
        pointer_hide();
        if (mouse_poll_active) mouse_pointer_moved = 1;
    }
    pointer_x = next_x;
    pointer_y = next_y;

    if (mouse_press_pending) {
        int wx = gui_terminal_maximized ? 12 : terminal_x;
        int wy = gui_terminal_maximized ? 10 : terminal_y;
        int ww = gui_terminal_maximized ? UI_WIDTH - 24 : UI_WIDTH - 160;
        int right = wx + ww;
        terminal_drag_active = gui_mode == GUI_MODE_TERMINAL &&
            !gui_terminal_minimized && !gui_terminal_maximized &&
            pointer_y >= wy + 4 && pointer_y < wy + 48 &&
            pointer_x >= wx + 8 && pointer_x < right - 124;
        mouse_press_pending = 0;
    }

    if (moved && changed && mouse_left_down && terminal_drag_active &&
        gui_mode == GUI_MODE_TERMINAL && !gui_terminal_maximized) {
        terminal_x += pointer_x - old_x;
        terminal_y += pointer_y - old_y;
        if (terminal_x < 0) terminal_x = 0;
        if (terminal_y < 0) terminal_y = 0;
        if (terminal_x > 160) terminal_x = 160;
        if (terminal_y > TASKBAR_TOP - 590) terminal_y = TASKBAR_TOP - 590;
        terminal_drag_dirty = 1;
    }

    if (terminal_drag_dirty && active_terminal_log) {
        if (!mouse_poll_active) {
            draw_terminal_window(active_terminal_username, active_terminal_log,
                                 gui_terminal_maximized);
            terminal_drag_dirty = 0;
            pointer_show();
        }
    } else if (changed && !mouse_poll_active) {
        pointer_show();
    }
}

static int poll_mouse_click(void)
{
    struct virtio_input_event event;
    int dx = 0, dy = 0, moved = 0, clicked = 0;
    int next_x = pointer_x, next_y = pointer_y;

    mouse_poll_active = 1;
    mouse_pointer_moved = 0;

    /* Apply absolute and relative coordinates together at SYN_REPORT. */
    for (int count = 0; count < 64 && virtio_input_next_event(&event); count++) {
        if (event.type == IRAQOS_EV_REL && event.code == IRAQOS_REL_X) {
            dx += (int32_t)event.value;
            moved = 1;
        } else if (event.type == IRAQOS_EV_REL && event.code == IRAQOS_REL_Y) {
            dy += (int32_t)event.value;
            moved = 1;
        } else if (event.type == IRAQOS_EV_ABS && event.code == IRAQOS_ABS_X) {
            next_x = (int)((uint64_t)event.value * (UI_WIDTH - 1) / 32767u);
            moved = 1;
        } else if (event.type == IRAQOS_EV_ABS && event.code == IRAQOS_ABS_Y) {
            next_y = (int)((uint64_t)event.value * (UI_HEIGHT - 1) / 32767u);
            moved = 1;
        } else if (event.type == IRAQOS_EV_KEY && event.code == IRAQOS_BTN_LEFT) {
            if (event.value == 1) {
                mouse_left_down = 1;
                mouse_press_pending = 1;
                clicked |= 1;
            } else if (event.value == 0) {
                mouse_left_down = 0;
                terminal_drag_active = 0;
            }
        } else if (event.type == IRAQOS_EV_KEY &&
                   event.code == IRAQOS_BTN_RIGHT && event.value == 1) {
            clicked |= 2;
        } else if (event.type == IRAQOS_EV_SYN && event.code == 0) {
            update_pointer(next_x + dx, next_y + dy, moved);
            next_x = pointer_x;
            next_y = pointer_y;
            dx = dy = 0;
            moved = 0;
        }
    }
    if (moved || mouse_press_pending)
        update_pointer(next_x + dx, next_y + dy, moved);

    virtio_input_kick();
    mouse_poll_active = 0;

    if (terminal_drag_dirty && active_terminal_log) {
        draw_terminal_window(active_terminal_username, active_terminal_log,
                             gui_terminal_maximized);
        terminal_drag_dirty = 0;
    }
    if (mouse_pointer_moved)
        pointer_show();

    return clicked;
}

static void draw_wallpaper(void)
{
    for (int y = 0; y < UI_HEIGHT; y += 4) {
        unsigned int t = (unsigned int)y * 255u / UI_HEIGHT;
        unsigned int r = 14u + (68u * t / 255u);
        unsigned int g = 82u + (105u * t / 255u);
        unsigned int b = 151u + (77u * t / 255u);
        console_fill_rect(0, y, UI_WIDTH, 4,
            0xFF000000u | (r << 16) | (g << 8) | b);
    }

    /* A soft blue bloom and sloping green hills give IraqOS a calm desktop. */
    for (int y = 126; y < 430; y += 3) {
        int distance = y > 290 ? y - 290 : 290 - y;
        int width = 240 - distance / 2;
        if (width > 0)
            console_fill_rect(UI_WIDTH * 3 / 5 - width / 2, y, width, 3,
                              (distance < 42) ? 0xFF55A8D4u : 0xFF378AC4u);
    }
    for (int x = 0; x < UI_WIDTH; x += 4) {
        int ridge = 594 - x * (UI_WIDTH - x) / 7800;
        int front = 632 - x * (UI_WIDTH - x) / 5200;
        console_fill_rect(x, ridge, 5, UI_HEIGHT - ridge, 0xFF398A78u);
        console_fill_rect(x, front, 5, UI_HEIGHT - front, 0xFF246F64u);
    }
    console_fill_rect(0, TASKBAR_TOP - 3, UI_WIDTH, 3, 0xFF368B7Au);
}

static void draw_desktop_icon(int x, int y, const char *label, int kind)
{
    uint32_t c = kind == 0 ? 0xFFFFD35Cu : kind == 1 ? 0xFF62D7F5u : 0xFF72DC91u;
    console_fill_rect(x + 12, y, 42, 36, 0xAAFFFFFFu);
    console_fill_rect(x + 16, y + 4, 34, 28, c);
    console_fill_rect(x + 16, y + 4, 34, 5, WHITE);
    if (kind == 0) {
        console_fill_rect(x + 20, y + 12, 9, 3, BLUE);
        console_fill_rect(x + 20, y + 19, 24, 3, BLUE);
        console_fill_rect(x + 20, y + 25, 19, 3, BLUE);
    } else if (kind == 1) {
        console_fill_rect(x + 21, y + 11, 22, 3, 0xFF155477u);
        console_fill_rect(x + 21, y + 18, 22, 3, 0xFF155477u);
        console_fill_rect(x + 21, y + 25, 15, 3, 0xFF155477u);
    } else {
        console_fill_rect(x + 22, y + 11, 20, 4, 0xFF27643Cu);
        console_fill_rect(x + 30, y + 7, 4, 22, 0xFF27643Cu);
    }
    console_draw_text_at(x, y + 44, label, WHITE);
}

static void draw_taskbar(void)
{
    for (int y = TASKBAR_TOP; y < UI_HEIGHT; y++) {
        unsigned int drop = (unsigned int)(y - TASKBAR_TOP) * 22u / 58u;
        uint32_t color = 0xFF000000u | ((24u - drop) << 16) |
                         ((79u - drop) << 8) | (119u - drop);
        console_fill_rect(0, y, UI_WIDTH, 1, color);
    }
    console_fill_rect(0, TASKBAR_TOP, UI_WIDTH, 1, 0xFFEAF8FFu);
    console_fill_rect(0, TASKBAR_TOP + 1, UI_WIDTH, 1, 0xFF77BDE6u);

    /* Glossy Start orb with IraqOS's own four-colour mark. */
    draw_disc(37, TASKBAR_TOP + 28, 27, 0xFF0A2A45u);
    draw_disc(37, TASKBAR_TOP + 27, 24, 0xFF54CBE9u);
    draw_disc(37, TASKBAR_TOP + 26, 20, 0xFF1384BDu);
    draw_disc(37, TASKBAR_TOP + 18, 13, 0xFF62D9F2u);
    console_fill_rect(30, TASKBAR_TOP + 16, 6, 6, 0xFFF44336u);
    console_fill_rect(38, TASKBAR_TOP + 16, 6, 6, 0xFFFFD54Fu);
    console_fill_rect(30, TASKBAR_TOP + 24, 6, 6, 0xFF66BB6Au);
    console_fill_rect(38, TASKBAR_TOP + 24, 6, 6, 0xFF42A5F5u);

    draw_glossy_button(78, TASKBAR_TOP + 6, 294, 46,
                       0xFF0C3659u, 0xFF24699Au, 0xFF3985B5u);
    /* Small monitor icon for the pinned Turtle Terminal. */
    console_fill_rect(94, TASKBAR_TOP + 15, 31, 24, 0xFF071A29u);
    console_fill_rect(97, TASKBAR_TOP + 18, 25, 17, 0xFF082315u);
    console_fill_rect(100, TASKBAR_TOP + 21, 15, 2, 0xFF79E5A2u);
    console_fill_rect(100, TASKBAR_TOP + 26, 19, 2, 0xFF79E5A2u);
    console_fill_rect(100, TASKBAR_TOP + 31, 12, 2, 0xFF79E5A2u);
    console_fill_rect(108, TASKBAR_TOP + 39, 5, 5, 0xFFB6D8E9u);
    console_draw_text_at(140, TASKBAR_TOP + 19, "TURTULE TERMINAL", WHITE);

    console_fill_rect(UI_WIDTH - 150, TASKBAR_TOP + 8, 1, 42, 0xFF41708Fu);
    console_draw_text_at(UI_WIDTH - 136, TASKBAR_TOP + 18, "EN", 0xFFDCECF6u);
    console_fill_rect(UI_WIDTH - 92, TASKBAR_TOP + 8, 1, 42, 0xFF41708Fu);
    console_fill_rect(UI_WIDTH - 78, TASKBAR_TOP + 35, 5, 8, 0xFF9FD9F2u);
    console_fill_rect(UI_WIDTH - 70, TASKBAR_TOP + 28, 5, 15, 0xFF9FD9F2u);
    console_fill_rect(UI_WIDTH - 62, TASKBAR_TOP + 21, 5, 22, 0xFF9FD9F2u);
    console_draw_text_at(UI_WIDTH - 48, TASKBAR_TOP + 19, "IQ", 0xFFB8D9E8u);
    console_fill_rect(UI_WIDTH - 7, TASKBAR_TOP + 2, 3, 54, 0xFF83B2D0u);
    console_fill_rect(UI_WIDTH - 4, TASKBAR_TOP + 2, 2, 54, 0xFF4C82A2u);
}

static void draw_desktop(const char *username)
{
    pointer_hide();
    console_clear(DESK_TOP);
    draw_wallpaper();
    draw_desktop_icon(30, 36, "COMPUTER", 0);
    draw_desktop_icon(30, 124, "TERMINAL", 1);
    draw_desktop_icon(30, 212, "SETTINGS", 2);
    console_draw_text_at(UI_WIDTH - 330, 82, "IRAQOS DESKTOP", 0xCCFFFFFFu);
    console_draw_text_at(UI_WIDTH - 330, 116, "WELCOME", 0xCCFFFFFFu);
    console_draw_text_at(UI_WIDTH - 330, 150, username, 0xCCFFFFFFu);
    draw_taskbar();
    console_present();
}

static void animate_panel(int x, int y, int w, int h)
{
    /*
     * Window animation disabled:
     * the old implementation presented six intermediate frames,
     * synchronously flushed every frame, then callers redrew the
     * complete window anyway. Callers handle the final drawing.
     */
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void draw_app_window(const char *title, const char *line1,
                            const char *line2, const char *line3)
{
    int x = APP_X, y = 130;
    pointer_hide();
    animate_panel(x, y, 656, 446);
    console_fill_rect(x - 6, y - 6, 668, 460, 0xAA0B1A28u);
    console_fill_rect(x - 3, y - 3, 662, 452, 0xFF91B2C5u);
    console_fill_rect(x, y, 656, 446, 0xFFEAF1F5u);
    draw_aero_titlebar(x, y, 656);
    console_draw_text_at(x + 22, y + 12, title, WHITE);
    draw_glossy_button(x + 612, y + 8, 30, 28,
                       0xFF842B35u, 0xFFC54651u, 0xFFE56D76u);
    console_draw_text_at(x + 622, y + 12, "X", WHITE);
    console_fill_rect(x, y + 44, 656, 34, 0xFFCAD9E2u);
    console_draw_text_at(x + 20, y + 54, "IRAQOS / SYSTEM", 0xFF253B4Au);
    console_draw_text_at(x + 34, y + 122, line1, 0xFF172B38u);
    console_draw_text_at(x + 34, y + 174, line2, 0xFF172B38u);
    console_draw_text_at(x + 34, y + 226, line3, 0xFF172B38u);
    console_draw_text_at(x + 34, y + 380, "TYPE BACK TO RETURN TO DESKTOP", 0xFF326B84u);
    console_present();
}

static void show_start_menu(const char *username)
{
    int x = START_MENU_X, y = START_MENU_Y;
    pointer_hide();
    console_fill_rect(x - 5, y + 8, 460, 462, 0xAA071A2Cu);
    console_fill_rect(x, y, 450, 454, 0xFF102A40u);
    console_fill_rect(x + 4, y + 4, 442, 446, 0xFFF1F5F7u);

    /* Account strip, familiar two-column programs and system links. */
    console_fill_rect(x + 4, y + 4, 442, 52, 0xFF197BA9u);
    console_fill_rect(x + 4, y + 53, 442, 3, 0xFF8DD8F0u);
    draw_disc(x + 30, y + 29, 18, 0xFF0C4168u);
    draw_disc(x + 30, y + 26, 14, 0xFFBBD6E4u);
    draw_disc(x + 30, y + 22, 7, 0xFF557B91u);
    console_fill_rect(x + 21, y + 30, 18, 8, 0xFF557B91u);
    console_draw_text_at(x + 58, y + 20, username, WHITE);
    console_draw_text_at(x + 58, y + 39, "IRAQOS DESKTOP", 0xFFD6EAF4u);

    console_fill_rect(x + 14, y + 67, 264, 300, 0xFFFFFFFFu);
    console_fill_rect(x + 283, y + 67, 151, 300, 0xFFE2EBF0u);
    console_fill_rect(x + 278, y + 67, 1, 300, 0xFFB8CAD4u);

    console_fill_rect(x + 20, y + 80, 30, 30, 0xFFDFECF2u);
    console_fill_rect(x + 24, y + 84, 22, 21, 0xFF3E9CCB);
    console_fill_rect(x + 27, y + 88, 16, 3, WHITE);
    console_draw_text_at(x + 58, y + 86, "COMPUTER", 0xFF1C3444u);
    console_draw_text_at(x + 58, y + 108, "Browse your system", 0xFF657985u);

    console_fill_rect(x + 20, y + 136, 30, 30, 0xFFDFECF2u);
    console_fill_rect(x + 24, y + 140, 22, 22, 0xFF101C27u);
    console_fill_rect(x + 27, y + 144, 16, 3, 0xFF77E3A2u);
    console_fill_rect(x + 27, y + 150, 12, 3, 0xFF77E3A2u);
    console_draw_text_at(x + 58, y + 142, "TURTULE TERMINAL", 0xFF1C3444u);
    console_draw_text_at(x + 58, y + 164, "Linux command line", 0xFF657985u);

    console_fill_rect(x + 20, y + 192, 30, 30, 0xFFDFECF2u);
    console_fill_rect(x + 26, y + 198, 18, 18, 0xFF69AE71u);
    console_fill_rect(x + 33, y + 194, 4, 26, 0xFF377B48u);
    console_fill_rect(x + 22, y + 205, 26, 4, 0xFF377B48u);
    console_draw_text_at(x + 58, y + 198, "CONTROL PANEL", 0xFF1C3444u);
    console_draw_text_at(x + 58, y + 220, "Personalize IraqOS", 0xFF657985u);

    console_fill_rect(x + 20, y + 248, 30, 30, 0xFFDFECF2u);
    console_fill_rect(x + 25, y + 253, 20, 20, 0xFFFFD166u);
    console_draw_text_at(x + 58, y + 254, "ABOUT IRAQOS", 0xFF1C3444u);
    console_draw_text_at(x + 58, y + 276, "System information", 0xFF657985u);
    console_draw_text_at(x + 24, y + 340, "ALL PROGRAMS", 0xFF326982u);
    console_draw_text_at(x + 253, y + 340, ">", 0xFF326982u);

    console_draw_text_at(x + 301, y + 84, "COMPUTER", 0xFF29495D);
    console_draw_text_at(x + 301, y + 128, "SETTINGS", 0xFF29495D);
    console_draw_text_at(x + 301, y + 172, "ABOUT", 0xFF29495D);
    console_draw_text_at(x + 301, y + 216, "TERMINAL", 0xFF29495D);
    console_draw_text_at(x + 301, y + 260, "LOG OUT", 0xFF29495D);

    console_fill_rect(x + 14, y + 377, 420, 52, 0xFFE0E8EDu);
    console_fill_rect(x + 24, y + 385, 250, 34, 0xFFFFFFFFu);
    outline(x + 24, y + 385, 250, 34, 0xFFB5C7D1u);
    console_draw_text_at(x + 35, y + 394, "SEARCH PROGRAMS", 0xFF778B97u);
    draw_glossy_button(x + 292, y + 385, 128, 34,
                       0xFF315773u, 0xFF477FA0u, 0xFF6D9FBAu);
    console_draw_text_at(x + 317, y + 394, "LOG OUT", WHITE);
    console_present();
}

static void read_desktop_command(char *line, int capacity)
{
    int n = 0;
    int alt_pending = 0;
    line[0] = '\0';
    for (;;) {
        int buttons = poll_mouse_click();
        if ((buttons & 2) && gui_mode == GUI_MODE_DESKTOP && gui_menu_open) {
            gui_menu_open = 0;
            line[0] = 'H'; line[1] = 'I'; line[2] = 'D'; line[3] = 'E';
            line[4] = '-'; line[5] = 'S'; line[6] = 'T'; line[7] = 'A';
            line[8] = 'R'; line[9] = 'T'; line[10] = '\0';
            uart_print("HIDE-START\r\n");
            return;
        }
        if ((buttons & 2) && gui_mode == GUI_MODE_DESKTOP) {
            gui_context_x = pointer_x;
            gui_context_y = pointer_y;
            if (gui_context_x > UI_WIDTH - 214) gui_context_x = UI_WIDTH - 214;
            if (gui_context_y > TASKBAR_TOP - 120) gui_context_y = TASKBAR_TOP - 120;
            gui_context_open = 1;
            gui_menu_open = 0;
            ui_sound("click");
            show_context_menu();
        }
        if (buttons & 1) {
            mouse_click_command(line, capacity);
            if (line[0]) { uart_print(line); uart_print("\r\n"); return; }
        }
        char c;
        if (!uart_try_getc(&c)) { __asm__ volatile("yield"); continue; }
        if (c == 0x1b) {
            alt_pending = 1;
            continue;
        }
        if (gui_mode == GUI_MODE_DESKTOP &&
            (c == 0x14 || (alt_pending && (c == 't' || c == 'T')))) {
            line[0] = 'T'; line[1] = 'E'; line[2] = 'R'; line[3] = 'M';
            line[4] = 'I'; line[5] = 'N'; line[6] = 'A'; line[7] = 'L';
            line[8] = '\0';
            uart_print("\r\nOpening TURTULE TERMINAL...\r\n");
            return;
        }
        alt_pending = 0;
        if (c == '\r' || c == '\n') {
            line[n] = '\0';
            uart_print("\r\n");
            return;
        }
        if (c == '\b' || c == 127) {
            if (n) { n--; uart_print("\b \b"); }
        } else if (c >= 32 && c <= 126 && n < capacity - 1) {
            line[n++] = c;
            uart_putc(c);
        }
    }
}

static void wait_return(void)
{
    char command[24];
    uart_print("Click X or press Enter to return to desktop.\r\n");
    gui_mode = GUI_MODE_APP;
    pointer_show();
    for (;;) {
        read_desktop_command(command, sizeof(command));
        if (!command[0] || same_word(command, "BACK") || same_word(command, "CLOSE")) break;
    }
    gui_mode = GUI_MODE_DESKTOP;
}


static int terminal_columns(void)
{
    int columns = (gui_terminal_maximized ? UI_WIDTH - 72 : UI_WIDTH - 208) /
                  (6 * console_get_font_scale());
    if (columns < 1) columns = 1;
    if (columns >= TERM_COLS) columns = TERM_COLS - 1;
    return columns;
}

static void term_add(char log[TERM_ROWS][TERM_COLS], const char *message)
{
    int max = terminal_columns();
    int length = 0;
    while (message[length]) length++;
    if (length == 0) {
        for (int i = 1; i < TERM_ROWS; i++)
            for (int j = 0; j < TERM_COLS; j++) log[i - 1][j] = log[i][j];
        log[TERM_ROWS - 1][0] = '\0';
        return;
    }
    for (int start = 0; start < length; start += max) {
        for (int i = 1; i < TERM_ROWS; i++)
            for (int j = 0; j < TERM_COLS; j++) log[i - 1][j] = log[i][j];
        int count = length - start;
        if (count > max) count = max;
        for (int i = 0; i < count; i++) log[TERM_ROWS - 1][i] = message[start + i];
        log[TERM_ROWS - 1][count] = '\0';
    }
}

#define HOST_RESULT_MAX 8192
#define HOST_ACK 0x06

static void terminal_host_command(const char *command,
                                 char log[TERM_ROWS][TERM_COLS])
{
    char echoed[TERM_COLS];
    const char *full_command = command;
    int max = terminal_columns();
    int n = 0;
    echoed[n++] = '$';
    echoed[n++] = ' ';
    while (*command && n < max && n < TERM_COLS - 1)
        echoed[n++] = *command++;
    echoed[n] = '\0';
    term_add(log, echoed);

    uart_print("\x1eIRAQOS_EXEC:");
    uart_print(full_command);
    uart_putc('\x1f');

    static const char response_prefix[] = "\x1eIRAQOS_RESULT:";
    for (int i = 0; i < (int)sizeof(response_prefix) - 1; i++) {
        if (uart_getc() != response_prefix[i]) {
            term_add(log, "TERMINAL LINK ERROR");
            return;
        }
    }
    int length = 0;
    int digits = 0;
    char c;
    while ((c = uart_getc()) != '\n') {
        if (c < '0' || c > '9' || digits >= 5) {
            term_add(log, "BAD TERMINAL RESPONSE");
            return;
        }
        length = length * 10 + c - '0';
        digits++;
    }
    if (length > HOST_RESULT_MAX) {
        term_add(log, "TERMINAL RESPONSE TOO LARGE");
        return;
    }

    char output_line[TERM_COLS];
    int line_length = 0;
    int columns = terminal_columns();
    for (int offset = 0; offset < length; ) {
        int chunk = length - offset;
        if (chunk > 16) chunk = 16;
        for (int j = 0; j < chunk; j++, offset++) {
            unsigned char out = (unsigned char)uart_getc();
            if (out == '\r') continue;
            if (out == '\n') {
                output_line[line_length] = '\0';
                term_add(log, output_line);
                line_length = 0;
            } else if (out == '\t') {
                output_line[line_length++] = ' ';
            } else if (out >= 32 && out <= 126) {
                output_line[line_length++] = (char)out;
            } else if (out >= 128) {
                output_line[line_length++] = '?';
            }
            if (line_length >= columns) {
                output_line[line_length] = '\0';
                term_add(log, output_line);
                line_length = 0;
            }
        }
        uart_putc(HOST_ACK);
    }
    if (line_length) {
        output_line[line_length] = '\0';
        term_add(log, output_line);
    }
}

static void draw_settings_window(void)
{
    int x = APP_X, y = 130;
    pointer_hide();
    console_fill_rect(x - 6, y - 6, 668, 460, 0xAA0B1A28u);
    console_fill_rect(x - 3, y - 3, 662, 452, 0xFF91B2C5u);
    console_fill_rect(x, y, 656, 446, 0xFFEAF1F5u);
    draw_aero_titlebar(x, y, 656);
    console_draw_text_at(x + 22, y + 12, "DISPLAY SETTINGS", WHITE);
    draw_glossy_button(x + 612, y + 8, 30, 28,
                       0xFF842B35u, 0xFFC54651u, 0xFFE56D76u);
    console_draw_text_at(x + 622, y + 12, "X", WHITE);
    console_draw_text_at(x + 34, y + 94, "FONT SIZE", 0xFF172B38u);
    draw_glossy_button(x + 376, y + 120, 60, 54,
                       0xFF285A79u, 0xFF3983A9u, 0xFF63A4C4u);
    console_draw_text_at(x + 394, y + 128, "-", WHITE);
    console_fill_rect(x + 436, y + 120, 80, 54, 0xFFE4EDF2u);
    outline(x + 436, y + 120, 80, 54, 0xFFB5C7D1u);
    char size_text[2] = { (char)('0' + console_get_font_scale()), '\0' };
    console_draw_text_at(x + 470, y + 128, size_text, 0xFF172B38u);
    draw_glossy_button(x + 516, y + 120, 60, 54,
                       0xFF285A79u, 0xFF3983A9u, 0xFF63A4C4u);
    console_draw_text_at(x + 534, y + 128, "+", WHITE);
    console_draw_text_at(x + 34, y + 224, "SAMPLE TEXT: IRAQOS", 0xFF172B38u);
    console_draw_text_at(x + 34, y + 380, "FONT SIZE APPLIES UNTIL THE NEXT REBOOT", 0xFF326B84u);
    console_present();
}

static void settings_window(void)
{
    char command[24];
    gui_mode = GUI_MODE_SETTINGS;
    animate_panel(APP_X, 130, 656, 446);
    for (;;) {
        draw_settings_window();
        pointer_show();
        uart_print("DISPLAY SETTINGS [FONT- FONT+ BACK]> ");
        read_desktop_command(command, sizeof(command));
        if (same_word(command, "BACK") || same_word(command, "CLOSE") ||
            !command[0])
            break;
        if (same_word(command, "FONT+"))
            console_set_font_scale(console_get_font_scale() + 1);
        else if (same_word(command, "FONT-"))
            console_set_font_scale(console_get_font_scale() - 1);
    }
    gui_mode = GUI_MODE_DESKTOP;
}

static void draw_terminal_window(const char *username,
                                 char log[TERM_ROWS][TERM_COLS],
                                 int maximized)
{
    pointer_hide();
    int x = maximized ? 12 : terminal_x;
    int y = maximized ? 10 : terminal_y;
    int w = maximized ? UI_WIDTH - 24 : UI_WIDTH - 160;
    int h = maximized ? TASKBAR_TOP - y - 8 : 590;
    int right = x + w;

    console_fill_rect(x - 7, y + 8, w + 14, h + 12, 0xAA0A1722u);
    console_fill_rect(x - 2, y - 2, w + 4, h + 4, 0xFF88AFC3u);
    console_fill_rect(x, y, w, h, 0xFFE5EDF1u);
    console_fill_rect(x + 4, y + 4, w - 8, h - 8, 0xFF07131Bu);
    for (int row = 0; row < 44; row += 2) {
        uint32_t shade = 0xFF000000u | ((28u + (30u * (unsigned int)row / 44u)) << 16) |
                         ((100u + (48u * (unsigned int)row / 44u)) << 8) |
                         (143u + (55u * (unsigned int)row / 44u));
        console_fill_rect(x + 4, y + 4 + row, w - 8, 2, shade);
    }
    console_fill_rect(x + 4, y + 4, w - 8, 2, 0xFFB6E4F5u);
    console_fill_rect(x + 4, y + 46, w - 8, 2, 0xFF0A3B59u);
    console_draw_text_at(x + 20, y + 14, "TURTULE TERMINAL", WHITE);

    /* Familiar minimize, maximize and close controls in the title bar. */
    draw_glossy_button(right - 116, y + 10, 30, 28,
                       0xFF245777u, 0xFF3A87ABu, 0xFF6EAFC9u);
    console_fill_rect(right - 107, y + 29, 12, 3, WHITE);
    draw_glossy_button(right - 78, y + 10, 30, 28,
                       0xFF245777u, 0xFF3A87ABu, 0xFF6EAFC9u);
    console_fill_rect(right - 69, y + 17, 13, 12, WHITE);
    console_fill_rect(right - 67, y + 19, 9, 8, 0xFF3187A8u);
    draw_glossy_button(right - 40, y + 10, 30, 28,
                       0xFF842B35u, 0xFFC54651u, 0xFFE56D76u);
    console_draw_text_at(right - 31, y + 14, "X", WHITE);

    console_fill_rect(x + 5, y + 48, w - 10, h - 54, 0xFF071018u);
    console_draw_text_at(x + 24, y + 66, "TERMUX LINUX SHELL", 0xFF71E6A2u);
    int row_pitch = 9 * console_get_font_scale() + 8;
    int visible_rows = (h - 176) / row_pitch;
    if (visible_rows > TERM_ROWS) visible_rows = TERM_ROWS;
    if (visible_rows < 1) visible_rows = 1;
    int first_row = TERM_ROWS - visible_rows;
    for (int row = first_row; row < TERM_ROWS; row++) {
        if (log[row][0])
            console_draw_text_at(x + 24, y + 104 + (row - first_row) * row_pitch,
                                 log[row], 0xFFD9E8EFu);
    }
    console_fill_rect(x + 18, y + h - 68, w - 36, 2, 0xFF244150u);
    console_draw_text_at(x + 24, y + h - 54,
                         "TERMUX LINUX  |  COMMANDS RUN IN IRAQOS FOLDER", 0xFF77B7CFu);
    console_draw_text_at(x + 24, y + h - 30,
                         "MAXIMIZE   X CLOSE", 0xFF77B7CFu);
    (void)username;
    console_present();
}

static void terminal_window(const char *username)
{
    char log[TERM_ROWS][TERM_COLS];
    char command[128];
    for (int row = 0; row < TERM_ROWS; row++) log[row][0] = '\0';
    int maximized = 0;
    int minimized = 0;
    int desktop_dirty = 0;
    gui_mode = GUI_MODE_TERMINAL;
    term_add(log, "TURTULE TERMINAL 0.4");
    term_add(log, "CONNECTED TO TERMUX LINUX SHELL");
    terminal_x = (UI_WIDTH - (UI_WIDTH - 160)) / 2;
    terminal_y = 76;
    active_terminal_log = log;
    active_terminal_username = username;
    animate_panel(terminal_x, terminal_y, UI_WIDTH - 160, 590);

    for (;;) {
        gui_terminal_maximized = maximized;
        gui_terminal_minimized = minimized;
        if (desktop_dirty) {
            draw_desktop(username);
            desktop_dirty = 0;
        }
        if (!minimized) draw_terminal_window(username, log, maximized);
        else {
            console_draw_text_at(270, TASKBAR_TOP + 20, "TERMINAL MINIMIZED", GOLD);
            console_present();
        }
        pointer_show();
        uart_print("TERMINAL [HELP CLEAR MAXIMIZE MINIMIZE RESTORE CLOSE]> ");
        read_desktop_command(command, sizeof(command));

        if (same_word(command, "CLOSE") || same_word(command, "X")) {
            active_terminal_log = 0;
            active_terminal_username = 0;
            terminal_drag_active = 0;
            gui_mode = GUI_MODE_DESKTOP;
            return;
        } else if (same_word(command, "MAXIMIZE")) {
            if (!maximized || minimized) desktop_dirty = 1;
            maximized = 1;
            minimized = 0;
        } else if (same_word(command, "RESTORE")) {
            if (maximized || minimized) desktop_dirty = 1;
            maximized = 0;
            minimized = 0;
        } else if (same_word(command, "MINIMIZE")) {
            if (!minimized) desktop_dirty = 1;
            minimized = 1;
        } else if (same_word(command, "CLEAR")) {
            for (int row = 0; row < TERM_ROWS; row++) log[row][0] = '\0';
            term_add(log, "TERMINAL CLEARED");
        } else if (same_word(command, "HELP")) {
            term_add(log, "LINUX SHELL: LS, PWD, CD, CAT, SH");
            term_add(log, "WINDOW: MAXIMIZE MINIMIZE RESTORE CLOSE");
        } else if (command[0]) {
            terminal_host_command(command, log);
        }
    }
}

void gui_desktop(const char *username)
{
    char command[48];
    gui_menu_open = 0;
    gui_context_open = 0;
    gui_mode = GUI_MODE_DESKTOP;
    for (;;) {
        draw_desktop(username);
        if (gui_menu_open) show_start_menu(username);
        pointer_show();
        uart_print("\r\nIRAQOS DESKTOP. COMMANDS: START COMPUTER TERMINAL SETTINGS ABOUT LOGOUT\r\nDESKTOP> ");
        read_desktop_command(command, sizeof(command));
        if (same_word(command, "START")) {
            gui_context_open = 0;
            gui_menu_open = !gui_menu_open;
        } else if (same_word(command, "HIDE-START")) {
            gui_menu_open = 0;
        } else if (same_word(command, "HIDE-CONTEXT")) {
            gui_context_open = 0;
        } else if (same_word(command, "REFRESH")) {
            gui_context_open = 0;
        } else if (same_word(command, "COMPUTER")) {
            gui_menu_open = 0;
            draw_app_window("COMPUTER", "THIS PC", "SYSTEM DISK: NOT READY", "FILESYSTEM DRIVER IS BEING BUILT");
            wait_return();
        } else if (same_word(command, "SETTINGS")) {
            gui_menu_open = 0;
            settings_window();
        } else if (same_word(command, "ABOUT")) {
            gui_menu_open = 0;
            draw_app_window("ABOUT IRAQOS", "IRAQOS DESKTOP 0.4", "BUILT FOR ARM64 VIRTUAL MACHINES", "POWERED BY TURTULE TERMINAL");
            wait_return();
        } else if (same_word(command, "TERMINAL")) {
            gui_menu_open = 0;
            terminal_window(username);
        } else if (same_word(command, "LOGOUT")) {
            gui_mode = GUI_MODE_DESKTOP;
            return;
        } else if (same_word(command, "HELP")) {
            uart_print("Use START, COMPUTER, TERMINAL, SETTINGS, ABOUT or LOGOUT.\r\n");
        } else if (command[0]) {
            uart_print("UNKNOWN DESKTOP COMMAND. TYPE HELP.\r\n");
        }
    }
}
