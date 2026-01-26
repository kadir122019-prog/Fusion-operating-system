#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

struct limine_memmap_response;

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

void memory_set_hhdm_offset(u64 offset);
u64 memory_hhdm_offset(void);
void memory_set_memmap(struct limine_memmap_response *memmap,
                       u64 kernel_phys_base, u64 kernel_phys_end);
void *phys_to_virt(u64 phys);
void *phys_alloc(size_t size, size_t align, u64 *out_phys);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);

#endif
