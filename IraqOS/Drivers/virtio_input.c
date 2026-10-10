#include "virtio_input.h"
#include "virtio_gpu.h"
#include "../Kernel/printk.h"

#define MMIO_BASE 0x0A000000u
#define MMIO_STRIDE 0x200u
#define MMIO_COUNT 32
#define VIRTIO_MAGIC 0x74726976u
#define VIRTIO_INPUT_ID 18
#define QUEUE_SIZE 64

#define REG_MAGIC 0x000
#define REG_DEVICE_ID 0x008
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

static struct virtq_desc desc[QUEUE_SIZE] __attribute__((aligned(16)));
static struct virtq_avail avail __attribute__((aligned(2)));
static struct virtq_used used __attribute__((aligned(4)));
static struct virtio_input_event events[QUEUE_SIZE] __attribute__((aligned(16)));
static uint64_t input_base;
static uint16_t avail_idx;
static uint16_t used_idx;
static uint16_t queue_size;
static int ready;

static uint32_t read_reg(uint64_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void write_reg(uint64_t base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(base + offset) = value;
}

static uint64_t find_device(void)
{
    for (int i = 0; i < MMIO_COUNT; i++) {
        uint64_t base = MMIO_BASE + i * MMIO_STRIDE;
        if (read_reg(base, REG_MAGIC) == VIRTIO_MAGIC &&
            read_reg(base, REG_DEVICE_ID) == VIRTIO_INPUT_ID)
            return base;
    }
    return 0;
}

static int setup_event_queue(void)
{
    write_reg(input_base, REG_QUEUE_SEL, 0);
    uint32_t max = read_reg(input_base, REG_QUEUE_NUM_MAX);
    if (!max) return 0;
    uint32_t size = max < QUEUE_SIZE ? max : QUEUE_SIZE;
    if (size < 2 || (size & (size - 1))) return 0;
    queue_size = (uint16_t)size;
    write_reg(input_base, REG_QUEUE_NUM, size);

    for (uint32_t i = 0; i < size; i++) {
        desc[i].addr = (uint64_t)&events[i];
        desc[i].len = sizeof(struct virtio_input_event);
        desc[i].flags = DESC_F_WRITE;
        desc[i].next = 0;
        avail.ring[i] = (uint16_t)i;
    }
    avail_idx = (uint16_t)size;
    used_idx = 0;
    __asm__ volatile("dmb sy" ::: "memory");
    avail.idx = avail_idx;

    uint64_t desc_addr = (uint64_t)&desc[0];
    uint64_t avail_addr = (uint64_t)&avail;
    uint64_t used_addr = (uint64_t)&used;
    write_reg(input_base, REG_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    write_reg(input_base, REG_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
    write_reg(input_base, REG_QUEUE_DRIVER_LOW, (uint32_t)avail_addr);
    write_reg(input_base, REG_QUEUE_DRIVER_HIGH, (uint32_t)(avail_addr >> 32));
    write_reg(input_base, REG_QUEUE_DEVICE_LOW, (uint32_t)used_addr);
    write_reg(input_base, REG_QUEUE_DEVICE_HIGH, (uint32_t)(used_addr >> 32));
    write_reg(input_base, REG_QUEUE_READY, 1);
    return read_reg(input_base, REG_QUEUE_READY) == 1;
}

void virtio_input_init(void)
{
    printk("VirtIO input: initializing mouse\n");
    input_base = find_device();
    if (!input_base) {
        printk("VirtIO input: device not found\n");
        return;
    }
    write_reg(input_base, REG_STATUS, 0);
    write_reg(input_base, REG_STATUS, STATUS_ACK);
    write_reg(input_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER);

    write_reg(input_base, REG_DEVICE_FEATURES_SEL, 1);
    uint32_t features_hi = read_reg(input_base, REG_DEVICE_FEATURES);
    if (!(features_hi & (1u << VIRTIO_F_VERSION_1))) {
        printk("VirtIO input: VERSION_1 missing\n");
        write_reg(input_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FAILED);
        return;
    }
    write_reg(input_base, REG_DRIVER_FEATURES_SEL, 1);
    write_reg(input_base, REG_DRIVER_FEATURES, 1u << VIRTIO_F_VERSION_1);
    write_reg(input_base, REG_DEVICE_FEATURES_SEL, 0);
    write_reg(input_base, REG_DRIVER_FEATURES_SEL, 0);
    write_reg(input_base, REG_DRIVER_FEATURES, 0);
    __asm__ volatile("dmb sy" ::: "memory");

    write_reg(input_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
    uint32_t status = read_reg(input_base, REG_STATUS);
    if (!(status & STATUS_FEATURES_OK) || !setup_event_queue()) {
        write_reg(input_base, REG_STATUS, status | STATUS_FAILED);
        printk("VirtIO input: queue setup failed\n");
        return;
    }
    write_reg(input_base, REG_STATUS, status | STATUS_DRIVER_OK);
    write_reg(input_base, REG_QUEUE_NOTIFY, 0);
    ready = 1;
    printk("VirtIO input: mouse ready\n");
}

int virtio_input_ready(void)
{
    return ready;
}

int virtio_input_next_event(struct virtio_input_event *event)
{
    if (!ready || used_idx == used.idx) return 0;
    __asm__ volatile("dmb sy" ::: "memory");
    uint16_t slot = used_idx % queue_size;
    /* Virtio used elements are only 4-byte aligned; avoid a 64-bit aggregate load. */
    uint32_t event_id = used.ring[slot].id;
    used_idx++;
    if (event_id >= queue_size) return 0;

    *event = events[event_id];
    avail.ring[avail_idx % queue_size] = (uint16_t)event_id;
    avail_idx++;
    __asm__ volatile("dmb sy" ::: "memory");
    avail.idx = avail_idx;
    __asm__ volatile("dmb sy" ::: "memory");
    write_reg(input_base, REG_QUEUE_NOTIFY, 0);
    return 1;
}
