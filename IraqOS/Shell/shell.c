#include "shell.h"
#include "../Drivers/console.h"
#include "../Drivers/uart.h"
#include "../Drivers/virtio_blk.h"
#include "../Kernel/printk.h"
#include "../FS/fs.h"

#define SCRIPT_MAX 4096u
#define COMMAND_MAX 128u
#define PACKAGE_PATH "/usr/bin/"

static int text_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void text_copy(char *out, const char *in, int max)
{
    int i = 0;
    while (in[i] && i + 1 < max) { out[i] = in[i]; i++; }
    out[i] = '\0';
}

static int text_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static void emit(shell_output_fn output, void *context, const char *text)
{
    if (output && text && text[0]) output(text, context);
}

static void emit_number(shell_output_fn output, void *context, uint64_t value)
{
    char digits[24];
    int n = 0;
    if (!value) digits[n++] = '0';
    while (value && n < (int)sizeof(digits)) { digits[n++] = (char)('0' + value % 10); value /= 10; }
    char result[24];
    for (int i = 0; i < n; i++) result[i] = digits[n - i - 1];
    result[n] = '\0';
    emit(output, context, result);
}

struct list_context { shell_output_fn output; void *user_context; };

static int print_ls_entry(const char *name, int is_dir, uint32_t size, void *context)
{
    struct list_context *list = context;
    shell_output_fn output = list->output;
    void *user_context = list->user_context;
    char line[96];
    int n = 0;
    if (is_dir) { line[n++] = 'd'; line[n++] = 'i'; line[n++] = 'r'; line[n++] = ' '; }
    else { line[n++] = 'f'; line[n++] = 'i'; line[n++] = 'l'; line[n++] = 'e'; line[n++] = ' '; }
    while (*name && n < (int)sizeof(line) - 1) line[n++] = *name++;
    if (!is_dir && n < (int)sizeof(line) - 1) {
        line[n++] = ' ';
        line[n++] = '(';
        char digits[16]; int count = 0;
        if (!size) digits[count++] = '0';
        while (size && count < 15) { digits[count++] = (char)('0' + size % 10); size /= 10; }
        while (count && n < (int)sizeof(line) - 2) line[n++] = digits[--count];
        line[n++] = ')';
    }
    line[n] = '\0';
    emit(output, user_context, line);
    return 1;
}

static void ensure_package_dir(void)
{
    const char *saved = fs_getcwd();
    char cwd[192];
    text_copy(cwd, saved, sizeof(cwd));
    if (!fs_chdir("/usr")) fs_mkdir("/usr");
    if (!fs_chdir("/usr/bin")) fs_mkdir("/usr/bin");
    fs_chdir(cwd);
}

static const char *package_contents(const char *name)
{
    if (text_eq(name, "hello")) return "echo Hello from IraqOS!\n";
    if (text_eq(name, "sysinfo")) return "version\npwd\n";
    return 0;
}

static void run_line(const char *line, shell_output_fn output, void *context, int depth);

static void package_command(char *args, shell_output_fn output, void *context)
{
    char *action = args;
    while (*action == ' ') action++;
    char *name = action;
    while (*name && *name != ' ') name++;
    if (*name) *name++ = '\0';
    while (*name == ' ') name++;

    if (text_eq(action, "repo")) {
        emit(output, context, "Bundled packages: hello, sysinfo");
        return;
    }
    if (text_eq(action, "list") || text_eq(action, "installed")) {
        char cwd[192];
        text_copy(cwd, fs_getcwd(), sizeof(cwd));
        if (!fs_chdir("/usr/bin")) { emit(output, context, "No packages installed"); return; }
        struct list_context ctx = { output, context };
        if (!fs_list("/usr/bin", print_ls_entry, &ctx)) emit(output, context, "Package list unavailable");
        fs_chdir(cwd);
        return;
    }
    if (!*name) { emit(output, context, "Usage: dijlah install|remove|list|repo <package>"); return; }
    if (text_eq(action, "install")) {
        const char *contents = package_contents(name);
        if (!contents) { emit(output, context, "Package not in bundled repository"); return; }
        ensure_package_dir();
        char path[96];
        int n = 0;
        const char *prefix = PACKAGE_PATH;
        while (*prefix && n < (int)sizeof(path) - 1) path[n++] = *prefix++;
        const char *p = name;
        while (*p && n < (int)sizeof(path) - 5) path[n++] = *p++;
        path[n++] = '\0';
        uint32_t len = 0; while (contents[len]) len++;
        if (fs_write_file(path, contents, len)) emit(output, context, "Package installed on IraqOS disk");
        else emit(output, context, "Install failed: disk full or filesystem error");
        return;
    }
    if (text_eq(action, "remove")) {
        char path[96]; int n = 0;
        const char *prefix = PACKAGE_PATH;
        while (*prefix) path[n++] = *prefix++;
        while (*name && n < (int)sizeof(path) - 1) path[n++] = *name++;
        path[n] = '\0';
        emit(output, context, fs_remove(path) ? "Package removed" : "Package not installed");
        return;
    }
    emit(output, context, "Unknown dijlah action");
}

static int execute_builtin(const char *command, shell_output_fn output, void *context, int depth)
{
    char line[COMMAND_MAX];
    text_copy(line, command, sizeof(line));
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = '\0';
    while (*arg == ' ') arg++;

    if (text_eq(line, "help")) {
        emit(output, context, "help pwd ls cd cat write mkdir rm disk whoami version echo");
        emit(output, context, "dijlah repo|install|remove|list");
    } else if (text_eq(line, "pwd")) {
        emit(output, context, fs_getcwd());
    } else if (text_eq(line, "whoami")) {
        emit(output, context, "admin");
    } else if (text_eq(line, "version")) {
        emit(output, context, "IraqOS 0.3 guest shell");
    } else if (text_eq(line, "echo")) {
        emit(output, context, arg);
    } else if (text_eq(line, "ls")) {
        const char *path = *arg ? arg : ".";
        struct list_context ctx = { output, context };
        if (!fs_list(path, print_ls_entry, &ctx)) emit(output, context, "Directory not found");
    } else if (text_eq(line, "cd")) {
        if (!*arg) arg = "/";
        if (!fs_chdir(arg)) emit(output, context, "Directory not found");
    } else if (text_eq(line, "mkdir")) {
        emit(output, context, fs_mkdir(arg) ? "Directory created" : "mkdir failed");
    } else if (text_eq(line, "rm")) {
        emit(output, context, fs_remove(arg) ? "File removed" : "File not found");
    } else if (text_eq(line, "write")) {
        char *data = arg;
        while (*data && *data != ' ') data++;
        if (!*data) { emit(output, context, "Usage: write <path> <text>"); return 1; }
        *data++ = '\0';
        while (*data == ' ') data++;
        emit(output, context, fs_write_file(arg, data, text_len(data)) ? "File written" : "write failed");
    } else if (text_eq(line, "touch")) {
        if (fs_write_file(arg, "", 0)) emit(output, context, "File created");
        else emit(output, context, "touch failed");
    } else if (text_eq(line, "cat")) {
        static char data[SCRIPT_MAX + 1];
        uint32_t size;
        if (!fs_read_file(arg, data, SCRIPT_MAX, &size)) { emit(output, context, "File not found or too large"); return 1; }
        char line_out[COMMAND_MAX]; int used = 0;
        for (uint32_t i = 0; i < size; i++) {
            if (data[i] == '\n') { line_out[used] = '\0'; emit(output, context, line_out); used = 0; }
            else if (used < (int)sizeof(line_out) - 1) line_out[used++] = data[i];
        }
        if (used) { line_out[used] = '\0'; emit(output, context, line_out); }
    } else if (text_eq(line, "disk")) {
        emit(output, context, "Guest disk total bytes:");
        emit_number(output, context, fs_total_bytes());
        emit(output, context, "Guest disk free bytes:");
        emit_number(output, context, fs_free_bytes());
    } else if (text_eq(line, "dijlah")) {
        package_command(arg, output, context);
    } else if (line[0] && depth < 4) {
        char path[96]; int n = 0;
        const char *prefix = PACKAGE_PATH;
        while (*prefix) path[n++] = *prefix++;
        const char *p = line;
        while (*p && n < (int)sizeof(path) - 1) path[n++] = *p++;
        path[n] = '\0';
        static char script[SCRIPT_MAX + 1];
        uint32_t size;
        if (fs_read_file(path, script, SCRIPT_MAX, &size)) {
            uint32_t start = 0;
            for (uint32_t i = 0; i <= size; i++) {
                if (i == size || script[i] == '\n') {
                    char script_line[COMMAND_MAX];
                    uint32_t len = i - start;
                    if (len >= sizeof(script_line)) len = sizeof(script_line) - 1;
                    for (uint32_t j = 0; j < len; j++) script_line[j] = script[start + j];
                    script_line[len] = '\0';
                    if (len) run_line(script_line, output, context, depth + 1);
                    start = i + 1;
                }
            }
        } else {
            emit(output, context, "Command not found");
        }
    }
    return 1;
}

static void run_line(const char *line, shell_output_fn output, void *context, int depth)
{
    while (*line == ' ') line++;
    if (!*line) return;
    execute_builtin(line, output, context, depth);
}

void shell_execute(const char *command, shell_output_fn output, void *context)
{
    if (!fs_ready()) { emit(output, context, "Guest filesystem is not mounted"); return; }
    run_line(command, output, context, 0);
}

static void console_output(const char *text, void *context)
{
    (void)context;
    printk(text);
    printk("\n");
}

void shell_start(const char *username)
{
    char line[COMMAND_MAX];
    (void)username;
    console_clear(0xFF101820u);
    printk("IRAQOS GUEST TERMINAL\nTYPE HELP\n");
    for (;;) {
        int len = 0;
        printk("iraqos> ");
        for (;;) {
            char c = uart_getc();
            if (c == '\r' || c == '\n') { printk("\n"); break; }
            if ((c == '\b' || c == 127) && len) { len--; line[len] = '\0'; uart_print("\b \b"); continue; }
            if (c >= 32 && c <= 126 && len < (int)sizeof(line) - 1) {
                line[len++] = c; line[len] = '\0'; uart_putc(c);
            }
        }
        if (text_eq(line, "logout")) return;
        shell_execute(line, console_output, 0);
    }
}
