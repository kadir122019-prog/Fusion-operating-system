#include "memory.h"
#include "kernel.h"

static u8 heap[HEAP_SIZE];
static block_t *heap_start = NULL;

u64 heap_allocated = 0;
u64 heap_freed = 0;
u64 heap_blocks = 0;

u64 pmm_total_pages = 0;
u64 pmm_used_pages = 0;
u64 pmm_free_pages = 0;

void *memset(void *s, int c, size_t n) {
    u8 *p = s;
    while (n--) *p++ = (u8)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    u8 *d = dest;
    const u8 *s = src;
    while (n--) *d++ = *s++;
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

void pmm_init(void) {
    if (memmap_request.response == NULL) return;
    
    struct limine_memmap_response *memmap = memmap_request.response;
    
    for (u64 i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        u64 pages = entry->length / PAGE_SIZE;
        pmm_total_pages += pages;
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_free_pages += pages;
        } else {
            pmm_used_pages += pages;
        }
    }
}
