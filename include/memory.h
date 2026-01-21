#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

typedef struct block {
    size_t size;
    bool free;
    struct block *next;
} block_t;

extern u64 pmm_total_pages;
extern u64 pmm_used_pages;
extern u64 pmm_free_pages;

extern u64 heap_allocated;
extern u64 heap_freed;
extern u64 heap_blocks;

void heap_init(void);
void *malloc(size_t size);
void free(void *ptr);

void pmm_init(void);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);

#endif
