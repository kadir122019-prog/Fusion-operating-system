#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include <limine.h>

#define FUSION_VERSION "1.0"
#define FUSION_ARCH "x86_64"

extern struct limine_memmap_request memmap_request;
extern struct limine_framebuffer_request framebuffer_request;
extern struct limine_hhdm_request hhdm_request;

void kernel_init(void);

#endif
