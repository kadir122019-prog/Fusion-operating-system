#include "services/storage.h"

static const storage_entry_t entries[] = {
    {"home", 0, 1},
    {"boot", 0, 1},
    {"kernel.elf", 512, 0},
    {"notes.txt", 4, 0},
    {"readme.md", 8, 0}
};

const storage_entry_t *storage_list(u32 *count) {
    if (count) *count = (u32)(sizeof(entries) / sizeof(entries[0]));
    return entries;
}
