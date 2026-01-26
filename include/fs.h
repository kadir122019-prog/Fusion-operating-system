#ifndef FS_H
#define FS_H

#include "types.h"

typedef struct {
    char name[64];
    u32 size;
    u8 is_dir;
} fs_entry_t;

typedef enum {
    FS_SORT_NAME = 0,
    FS_SORT_SIZE = 1,
    FS_SORT_TYPE = 2
} fs_sort_mode_t;

int fs_init(void);
int fs_is_ready(void);
int fs_list_dir(const char *path, fs_entry_t *entries, int max_entries, int *out_count);
int fs_read_file(const char *path, u8 **out_data, u32 *out_len);
int fs_write_file(const char *path, const u8 *data, u32 len);
int fs_append_file(const char *path, const u8 *data, u32 len);
int fs_mkdir(const char *path);
int fs_exists(const char *path);
int fs_delete(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_copy(const char *src_path, const char *dst_path);
int fs_move(const char *src_path, const char *dst_path);
int fs_stat(const char *path, fs_entry_t *out);
void fs_sort_entries(fs_entry_t *entries, int count, fs_sort_mode_t mode, int descending);

#endif
