#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "types.h"

int virtio_blk_init(void);
int virtio_blk_is_ready(void);
u64 virtio_blk_capacity(void);
int virtio_blk_read(u64 lba, u32 count, void *buffer);
int virtio_blk_write(u64 lba, u32 count, const void *buffer);

#endif
