#ifndef STORAGE_H
#define STORAGE_H

#include "types.h"

typedef struct {
    const char *name;
    u32 size;
    u8 is_dir;
} storage_entry_t;

const storage_entry_t *storage_list(u32 *count);

#endif
