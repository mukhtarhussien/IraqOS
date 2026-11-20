#ifndef IRAQOS_FS_H
#define IRAQOS_FS_H
#include <stdint.h>

typedef int (*fs_list_fn)(const char *name, int is_dir, uint32_t size, void *context);
int fs_init(void);
int fs_ready(void);
int fs_list(const char *path, fs_list_fn callback, void *context);
int fs_read_file(const char *path, void *buffer, uint32_t capacity, uint32_t *size_out);
int fs_write_file(const char *path, const void *buffer, uint32_t size);
int fs_mkdir(const char *path);
int fs_remove(const char *path);
int fs_chdir(const char *path);
const char *fs_getcwd(void);
uint64_t fs_total_bytes(void);
uint64_t fs_free_bytes(void);
#endif
