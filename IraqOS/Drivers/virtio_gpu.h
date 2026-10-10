#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include <stdint.h>
#include "display_size.h"

void virtio_gpu_init(void);
int virtio_gpu_is_ready(void);
void virtio_gpu_flush(void);
void fb_put_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_clear(uint32_t color);
void fb_scroll_up(int pixels, uint32_t color);

#endif
