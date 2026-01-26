#ifndef FS_H
#define FS_H

#include "types.h"

typedef struct {
    char name[64];
    u32 size;
    u8 is_dir;
} fs_entry_t;

int fs_init(void);
int fs_is_ready(void);
int fs_list_dir(const char *path, fs_entry_t *entries, int max_entries, int *out_count);
int fs_read_file(const char *path, u8 **out_data, u32 *out_len);
int fs_write_file(const char *path, const u8 *data, u32 len);
int fs_append_file(const char *path, const u8 *data, u32 len);
int fs_mkdir(const char *path);

#endif
