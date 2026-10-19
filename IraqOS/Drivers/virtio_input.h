#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H

#include <stdint.h>

#define IRAQOS_EV_SYN 0
#define IRAQOS_EV_KEY 1
#define IRAQOS_EV_REL 2
#define IRAQOS_EV_ABS 3
#define IRAQOS_REL_X 0
#define IRAQOS_REL_Y 1
#define IRAQOS_ABS_X 0
#define IRAQOS_ABS_Y 1
#define IRAQOS_BTN_LEFT 272
#define IRAQOS_BTN_RIGHT 273

struct virtio_input_event
{
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

void virtio_input_init(void);
int virtio_input_ready(void);
int virtio_input_next_event(struct virtio_input_event *event);
void virtio_input_kick(void);

#endif
