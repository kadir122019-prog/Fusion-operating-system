#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "types.h"
#include "input.h"
#include "fs.h"

typedef struct {
    char path[128];
    int selection;
    int entry_count;
    fs_entry_t entries[64];
    fs_sort_mode_t sort_mode;
    int sort_desc;
    char clipboard[128];
    int clipboard_cut;
    char input[64];
    int input_len;
    int mode;
    char status[64];
    u64 status_until;
} file_manager_t;

void file_manager_init(file_manager_t *fm);
void file_manager_render(file_manager_t *fm, int x, int y, int w, int h);
void file_manager_handle_key(file_manager_t *fm, const key_event_t *event);

#endif
