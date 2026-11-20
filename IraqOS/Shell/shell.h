#ifndef SHELL_H
#define SHELL_H

typedef void (*shell_output_fn)(const char *text, void *context);
void shell_start(const char *username);
void shell_execute(const char *command, shell_output_fn output, void *context);

#endif
