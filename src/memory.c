#include "memory.h"
#include <limine.h>

static u8 heap[HEAP_SIZE];
static block_t *heap_start = NULL;

u64 heap_allocated = 0;
u64 heap_freed = 0;
u64 heap_blocks = 0;

u64 pmm_total_pages = 0;
u64 pmm_used_pages = 0;
u64 pmm_free_pages = 0;

static u64 hhdm_offset = 0;
static struct limine_memmap_response *memmap_response = NULL;
static u64 phys_alloc_base = 0;
static u64 phys_alloc_end = 0;
static u64 phys_alloc_next = 0;

static u64 align_up(u64 value, u64 align) {
    if (align == 0) return value;
    u64 mask = align - 1;
    return (value + mask) & ~mask;
}

static u64 align_down(u64 value, u64 align) {
    if (align == 0) return value;
    return value & ~(align - 1);
}

static int ranges_overlap(u64 a0, u64 a1, u64 b0, u64 b1) {
    return a0 < b1 && b0 < a1;
}

void *memset(void *s, int c, size_t n) {
    u8 *p = s;
    while (n--) *p++ = (u8)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    u8 *d = dest;
    const u8 *s = src;
    if (n == 0 || d == s) return dest;

    while (n && ((uintptr_t)d & 7)) {
        *d++ = *s++;
        n--;
    }

    u64 *d64 = (u64 *)d;
    const u64 *s64 = (const u64 *)s;
    while (n >= 8) {
        *d64++ = *s64++;
        n -= 8;
    }

    d = (u8 *)d64;
    s = (const u8 *)s64;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    u8 *d = dest;
    const u8 *s = src;
    if (d == s || n == 0) return dest;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

void heap_init(void) {
    heap_start = (block_t *)heap;
    heap_start->size = HEAP_SIZE - sizeof(block_t);
    heap_start->free = true;
    heap_start->next = NULL;
    heap_blocks = 1;
}

void memory_set_hhdm_offset(u64 offset) {
    hhdm_offset = offset;
}

u64 memory_hhdm_offset(void) {
    return hhdm_offset;
}

void memory_set_memmap(struct limine_memmap_response *memmap,
                       u64 kernel_phys_base, u64 kernel_phys_end) {
    memmap_response = memmap;
    phys_alloc_base = 0;
    phys_alloc_end = 0;
    phys_alloc_next = 0;
    if (!memmap_response) return;

    u64 best_base = 0;
    u64 best_len = 0;

    for (u64 i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap_response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        u64 base = entry->base;
        u64 end = entry->base + entry->length;

        if (kernel_phys_end > kernel_phys_base &&
            ranges_overlap(base, end, kernel_phys_base, kernel_phys_end)) {
            if (kernel_phys_end < end) {
                base = kernel_phys_end;
            } else if (kernel_phys_base > base) {
                end = kernel_phys_base;
            } else {
                continue;
            }
        }

        base = align_up(base, PAGE_SIZE);
        end = align_down(end, PAGE_SIZE);
        if (end <= base) continue;

        u64 len = end - base;
        if (len > best_len) {
            best_len = len;
            best_base = base;
        }
    }

    if (best_len > 0) {
        phys_alloc_base = best_base;
        phys_alloc_end = best_base + best_len;
        phys_alloc_next = phys_alloc_base;
    }
}

void *phys_to_virt(u64 phys) {
    return (void *)(phys + hhdm_offset);
}

void *phys_alloc(size_t size, size_t align, u64 *out_phys) {
    if (size == 0 || phys_alloc_end <= phys_alloc_base) return NULL;
    u64 align_val = align == 0 ? 8 : align;
    u64 next = align_up(phys_alloc_next, align_val);
    if (next + size > phys_alloc_end) return NULL;
    phys_alloc_next = next + size;
    if (out_phys) *out_phys = next;
    return phys_to_virt(next);
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    size = (size + 7) & ~7;
    
    block_t *current = heap_start;
    while (current) {
        if (current->free && current->size >= size) {
            if (current->size >= size + sizeof(block_t) + 8) {
                block_t *new_block = (block_t *)((u8 *)current + sizeof(block_t) + size);
                new_block->size = current->size - size - sizeof(block_t);
                new_block->free = true;
                new_block->next = current->next;
                current->next = new_block;
                current->size = size;
                heap_blocks++;
            }
            current->free = false;
            heap_allocated += current->size;
            return (void *)((u8 *)current + sizeof(block_t));
        }
        current = current->next;
    }
    
    return NULL;
}

void free(void *ptr) {
    if (!ptr) return;
    
    block_t *block = (block_t *)((u8 *)ptr - sizeof(block_t));
    block->free = true;
    heap_freed += block->size;
    
    block_t *current = heap_start;
    while (current && current->next) {
        if (current->free && current->next->free) {
            current->size += sizeof(block_t) + current->next->size;
            current->next = current->next->next;
            heap_blocks--;
        } else {
            current = current->next;
        }
    }
}
