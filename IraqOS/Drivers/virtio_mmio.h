#ifndef IRAQOS_VIRTIO_MMIO_H
#define IRAQOS_VIRTIO_MMIO_H
#include <stdint.h>
uint64_t virtio_mmio_find(const void *fdt, uint32_t device_id);
uint64_t virtio_mmio_find_nth(const void *fdt, uint32_t device_id, uint32_t index);
#endif
