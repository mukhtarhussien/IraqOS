#include "virtio_input.h"
#include "virtio_mmio.h"
#include "virtio_gpu.h"
#include "../Kernel/printk.h"

#define VIRTIO_INPUT_ID 18
#define QUEUE_SIZE 64

#define REG_DEVICE_FEATURES 0x010
#define REG_DEVICE_FEATURES_SEL 0x014
#define REG_DRIVER_FEATURES 0x020
#define REG_DRIVER_FEATURES_SEL 0x024
#define REG_QUEUE_SEL 0x030
#define REG_QUEUE_NUM_MAX 0x034
#define REG_QUEUE_NUM 0x038
#define REG_QUEUE_READY 0x044
#define REG_QUEUE_NOTIFY 0x050
#define REG_STATUS 0x070
#define REG_CONFIG_SELECT 0x100
#define REG_CONFIG_SUBSELECT 0x101
#define REG_CONFIG_SIZE 0x102
#define REG_CONFIG_DATA 0x108
#define REG_QUEUE_DESC_LOW 0x080
#define REG_QUEUE_DESC_HIGH 0x084
#define REG_QUEUE_DRIVER_LOW 0x090
#define REG_QUEUE_DRIVER_HIGH 0x094
#define REG_QUEUE_DEVICE_LOW 0x0A0
#define REG_QUEUE_DEVICE_HIGH 0x0A4

#define STATUS_ACK 1
#define STATUS_DRIVER 2
#define STATUS_DRIVER_OK 4
#define STATUS_FEATURES_OK 8
#define STATUS_FAILED 128
#define VIRTIO_F_VERSION_1 0
#define DESC_F_WRITE 2

struct virtq_desc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct virtq_avail { uint16_t flags; volatile uint16_t idx; uint16_t ring[QUEUE_SIZE]; };
struct virtq_used_elem { uint32_t id; uint32_t len; };
struct virtq_used { uint16_t flags; volatile uint16_t idx; struct virtq_used_elem ring[QUEUE_SIZE]; };

struct input_device {
    struct virtq_desc desc[QUEUE_SIZE] __attribute__((aligned(16)));
    struct virtq_avail avail __attribute__((aligned(2)));
    struct virtq_used used __attribute__((aligned(4)));
    struct virtio_input_event events[QUEUE_SIZE] __attribute__((aligned(16)));
    uint64_t base;
    uint16_t avail_idx, used_idx, queue_size;
    int ready, pending_notify;
};

static struct input_device mouse;
static struct input_device keyboard;

static uint32_t read_reg(uint64_t base, uint32_t offset)
{ return *(volatile uint32_t *)(base + offset); }
static void write_reg(uint64_t base, uint32_t offset, uint32_t value)
{ *(volatile uint32_t *)(base + offset) = value; }

static int setup_event_queue(struct input_device *dev)
{
    write_reg(dev->base, REG_QUEUE_SEL, 0);
    uint32_t max = read_reg(dev->base, REG_QUEUE_NUM_MAX);
    if (!max) return 0;
    uint32_t size = max < QUEUE_SIZE ? max : QUEUE_SIZE;
    if (size < 2 || (size & (size - 1))) return 0;
    dev->queue_size = (uint16_t)size;
    write_reg(dev->base, REG_QUEUE_NUM, size);
    for (uint32_t i = 0; i < size; i++) {
        dev->desc[i].addr = (uint64_t)&dev->events[i];
        dev->desc[i].len = sizeof(struct virtio_input_event);
        dev->desc[i].flags = DESC_F_WRITE;
        dev->desc[i].next = 0;
        dev->avail.ring[i] = (uint16_t)i;
    }
    dev->avail_idx = (uint16_t)size;
    dev->used_idx = 0;
    __asm__ volatile("dmb sy" ::: "memory");
    dev->avail.idx = dev->avail_idx;
    uint64_t da = (uint64_t)&dev->desc[0], aa = (uint64_t)&dev->avail;
    uint64_t ua = (uint64_t)&dev->used;
    write_reg(dev->base, REG_QUEUE_DESC_LOW, (uint32_t)da);
    write_reg(dev->base, REG_QUEUE_DESC_HIGH, (uint32_t)(da >> 32));
    write_reg(dev->base, REG_QUEUE_DRIVER_LOW, (uint32_t)aa);
    write_reg(dev->base, REG_QUEUE_DRIVER_HIGH, (uint32_t)(aa >> 32));
    write_reg(dev->base, REG_QUEUE_DEVICE_LOW, (uint32_t)ua);
    write_reg(dev->base, REG_QUEUE_DEVICE_HIGH, (uint32_t)(ua >> 32));
    write_reg(dev->base, REG_QUEUE_READY, 1);
    return read_reg(dev->base, REG_QUEUE_READY) == 1;
}

static int initialize_device(struct input_device *dev, uint64_t base)
{
    if (!base) return 0;
    dev->base = base;
    write_reg(base, REG_STATUS, 0);
    write_reg(base, REG_STATUS, STATUS_ACK);
    write_reg(base, REG_STATUS, STATUS_ACK | STATUS_DRIVER);
    write_reg(base, REG_DEVICE_FEATURES_SEL, 1);
    uint32_t features_hi = read_reg(base, REG_DEVICE_FEATURES);
    if (!(features_hi & (1u << VIRTIO_F_VERSION_1))) {
        write_reg(base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FAILED);
        return 0;
    }
    write_reg(base, REG_DRIVER_FEATURES_SEL, 1);
    write_reg(base, REG_DRIVER_FEATURES, 1u << VIRTIO_F_VERSION_1);
    write_reg(base, REG_DEVICE_FEATURES_SEL, 0);
    write_reg(base, REG_DRIVER_FEATURES_SEL, 0);
    write_reg(base, REG_DRIVER_FEATURES, 0);
    __asm__ volatile("dmb sy" ::: "memory");
    write_reg(base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
    uint32_t status = read_reg(base, REG_STATUS);
    if (!(status & STATUS_FEATURES_OK) || !setup_event_queue(dev)) {
        write_reg(base, REG_STATUS, status | STATUS_FAILED);
        return 0;
    }
    write_reg(base, REG_STATUS, status | STATUS_DRIVER_OK);
    write_reg(base, REG_QUEUE_NOTIFY, 0);
    dev->ready = 1;
    return 1;
}

static void read_device_name(uint64_t base, char *name, unsigned int capacity)
{
    if (!capacity) return;
    name[0] = '\0';
    if (!base) return;
    *(volatile uint8_t *)(base + REG_CONFIG_SELECT) = 1; /* ID_NAME */
    *(volatile uint8_t *)(base + REG_CONFIG_SUBSELECT) = 0;
    unsigned int size = *(volatile uint8_t *)(base + REG_CONFIG_SIZE);
    if (size >= capacity) size = capacity - 1;
    for (unsigned int i = 0; i < size; i++)
        name[i] = (char)*(volatile uint8_t *)(base + REG_CONFIG_DATA + i);
    name[size] = '\0';
}

static int contains_word(const char *text, const char *word)
{
    while (*text) {
        const char *a = text, *b = word;
        while (*a && *b) {
            char ca = *a++, cb = *b++;
            if (ca >= 'a' && ca <= 'z') ca -= 32;
            if (cb >= 'a' && cb <= 'z') cb -= 32;
            if (ca != cb) break;
        }
        if (!*b) return 1;
        text++;
    }
    return 0;
}

void virtio_input_init(const void *fdt)
{
    uint64_t mouse_base = 0, keyboard_base = 0;
    uint64_t first = virtio_mmio_find_nth(fdt, VIRTIO_INPUT_ID, 0);
    uint64_t second = virtio_mmio_find_nth(fdt, VIRTIO_INPUT_ID, 1);
    char first_name[48], second_name[48];
    read_device_name(first, first_name, sizeof(first_name));
    read_device_name(second, second_name, sizeof(second_name));
    printk("VirtIO input device 0: "); printk(first_name); printk("\n");
    printk("VirtIO input device 1: "); printk(second_name); printk("\n");

    if (contains_word(first_name, "KEYBOARD")) keyboard_base = first;
    else if (contains_word(first_name, "TABLET") || contains_word(first_name, "MOUSE")) mouse_base = first;
    if (contains_word(second_name, "KEYBOARD")) keyboard_base = second;
    else if (contains_word(second_name, "TABLET") || contains_word(second_name, "MOUSE")) mouse_base = second;

    /* Keep compatibility with unnamed VirtIO input devices. */
    if (!mouse_base && !keyboard_base) {
        mouse_base = first;
        keyboard_base = second;
    } else {
        if (!mouse_base && first != keyboard_base) mouse_base = first;
        if (!keyboard_base && second != mouse_base) keyboard_base = second;
    }

    if (initialize_device(&mouse, mouse_base)) printk("VirtIO input: mouse ready\n");
    else printk("VirtIO input: mouse not found or unavailable\n");
    if (initialize_device(&keyboard, keyboard_base)) printk("VirtIO input: keyboard ready\n");
    else printk("VirtIO input: keyboard not found (serial input available)\n");
}

int virtio_input_ready(void) { return mouse.ready; }
int virtio_keyboard_ready(void) { return keyboard.ready; }

static int next_event(struct input_device *dev, struct virtio_input_event *event)
{
    if (!dev->ready || dev->used_idx == dev->used.idx) return 0;
    __asm__ volatile("dmb sy" ::: "memory");
    uint16_t slot = dev->used_idx % dev->queue_size;
    uint32_t event_id = dev->used.ring[slot].id;
    dev->used_idx++;
    if (event_id >= dev->queue_size) return 0;
    *event = dev->events[event_id];
    dev->avail.ring[dev->avail_idx % dev->queue_size] = (uint16_t)event_id;
    dev->avail_idx++;
    __asm__ volatile("dmb sy" ::: "memory");
    dev->avail.idx = dev->avail_idx;
    __asm__ volatile("dmb sy" ::: "memory");
    dev->pending_notify = 1;
    return 1;
}

int virtio_input_next_event(struct virtio_input_event *event) { return next_event(&mouse, event); }
int virtio_keyboard_next_event(struct virtio_input_event *event) { return next_event(&keyboard, event); }

void virtio_input_kick(void)
{
    if (mouse.ready && mouse.pending_notify) {
        __asm__ volatile("dmb sy" ::: "memory");
        write_reg(mouse.base, REG_QUEUE_NOTIFY, 0);
        mouse.pending_notify = 0;
    }
    if (keyboard.ready && keyboard.pending_notify) {
        __asm__ volatile("dmb sy" ::: "memory");
        write_reg(keyboard.base, REG_QUEUE_NOTIFY, 0);
        keyboard.pending_notify = 0;
    }
}
