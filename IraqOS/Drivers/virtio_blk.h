#ifndef IRAQOS_VIRTIO_BLK_H
#define IRAQOS_VIRTIO_BLK_H

#include <stdint.h>

#define VIRTIO_BLK_SECTOR_SIZE 512u

void virtio_blk_init(const void *fdt);
int virtio_blk_ready(void);
uint64_t virtio_blk_sector_count(void);
int virtio_blk_read(uint64_t sector, void *buffer);
int virtio_blk_write(uint64_t sector, const void *buffer);

#endif
