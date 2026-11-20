#include "virtio_blk.h"
#include "virtio_mmio.h"
#include "../Kernel/printk.h"

#define VIRTIO_BLK_ID 2u
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
#define REG_CONFIG 0x100
#define STATUS_ACK 1u
#define STATUS_DRIVER 2u
#define STATUS_DRIVER_OK 4u
#define STATUS_FEATURES_OK 8u
#define STATUS_FAILED 128u
#define VIRTIO_F_VERSION_1 0u
#define DESC_NEXT 1u
#define DESC_WRITE 2u
#define QUEUE_CAPACITY 128u
#define BLOCK_READ 0u
#define BLOCK_WRITE 1u

struct virtq_desc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct virtq_avail { uint16_t flags; volatile uint16_t idx; uint16_t ring[QUEUE_CAPACITY]; };
struct virtq_used_elem { uint32_t id; uint32_t len; };
struct virtq_used { uint16_t flags; volatile uint16_t idx; struct virtq_used_elem ring[QUEUE_CAPACITY]; };
struct block_request { uint32_t type; uint32_t reserved; uint64_t sector; };

static struct virtq_desc desc[QUEUE_CAPACITY] __attribute__((aligned(16)));
static struct virtq_avail avail __attribute__((aligned(2)));
static struct virtq_used used __attribute__((aligned(4)));
static struct block_request request __attribute__((aligned(16)));
static uint8_t request_status __attribute__((aligned(16)));
static uint64_t device_base;
static uint64_t sectors;
static uint16_t queue_size;
static uint16_t avail_idx;
static uint16_t used_idx;
static int ready;

static uint32_t mmio_read(uint32_t offset)
{
    return *(volatile uint32_t *)(device_base + offset);
}

static void mmio_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(device_base + offset) = value;
}

static int queue_init(void)
{
    mmio_write(REG_QUEUE_SEL, 0);
    uint32_t max = mmio_read(REG_QUEUE_NUM_MAX);
    uint32_t size = max < QUEUE_CAPACITY ? max : QUEUE_CAPACITY;
    if (size < 3) return 0;
    uint32_t power = 1;
    while ((power << 1) <= size) power <<= 1;
    queue_size = (uint16_t)power;
    mmio_write(REG_QUEUE_NUM, queue_size);
    mmio_write(REG_QUEUE_DESC_LOW, (uint32_t)(uint64_t)&desc[0]);
    mmio_write(REG_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)&desc[0] >> 32));
    mmio_write(REG_QUEUE_DRIVER_LOW, (uint32_t)(uint64_t)&avail);
    mmio_write(REG_QUEUE_DRIVER_HIGH, (uint32_t)((uint64_t)&avail >> 32));
    mmio_write(REG_QUEUE_DEVICE_LOW, (uint32_t)(uint64_t)&used);
    mmio_write(REG_QUEUE_DEVICE_HIGH, (uint32_t)((uint64_t)&used >> 32));
    mmio_write(REG_QUEUE_READY, 1);
    return mmio_read(REG_QUEUE_READY) == 1;
}

void virtio_blk_init(const void *fdt)
{
    printk("VirtIO block: initializing disk\n");
    ready = 0;
    device_base = virtio_mmio_find(fdt, VIRTIO_BLK_ID);
    if (!device_base) {
        printk("VirtIO block: device not found\n");
        return;
    }
    mmio_write(REG_STATUS, 0);
    mmio_write(REG_STATUS, STATUS_ACK);
    mmio_write(REG_STATUS, STATUS_ACK | STATUS_DRIVER);
    mmio_write(REG_DEVICE_FEATURES_SEL, 1);
    if (!(mmio_read(REG_DEVICE_FEATURES) & (1u << VIRTIO_F_VERSION_1))) {
        mmio_write(REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FAILED);
        printk("VirtIO block: VERSION_1 feature missing\n");
        return;
    }
    mmio_write(REG_DRIVER_FEATURES_SEL, 1);
    mmio_write(REG_DRIVER_FEATURES, 1u << VIRTIO_F_VERSION_1);
    mmio_write(REG_DEVICE_FEATURES_SEL, 0);
    mmio_write(REG_DRIVER_FEATURES_SEL, 0);
    mmio_write(REG_DRIVER_FEATURES, 0);
    __asm__ volatile("dmb sy" ::: "memory");
    mmio_write(REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
    uint32_t status = mmio_read(REG_STATUS);
    if (!(status & STATUS_FEATURES_OK) || !queue_init()) {
        mmio_write(REG_STATUS, status | STATUS_FAILED);
        printk("VirtIO block: queue setup failed\n");
        return;
    }
    uint32_t capacity_low = mmio_read(REG_CONFIG);
    uint32_t capacity_high = mmio_read(REG_CONFIG + 4);
    sectors = ((uint64_t)capacity_high << 32) | capacity_low;
    if (!sectors) {
        mmio_write(REG_STATUS, status | STATUS_FAILED);
        printk("VirtIO block: empty disk\n");
        return;
    }
    mmio_write(REG_STATUS, status | STATUS_DRIVER_OK);
    ready = 1;
    printk("VirtIO block: disk ready\n");
}

int virtio_blk_ready(void) { return ready; }
uint64_t virtio_blk_sector_count(void) { return sectors; }

static int transfer(uint32_t type, uint64_t sector, void *buffer)
{
    if (!ready || !buffer || sector >= sectors) return 0;
    request.type = type;
    request.reserved = 0;
    request.sector = sector;
    request_status = 0xFF;
    desc[0].addr = (uint64_t)&request;
    desc[0].len = sizeof(request);
    desc[0].flags = DESC_NEXT;
    desc[0].next = 1;
    desc[1].addr = (uint64_t)buffer;
    desc[1].len = VIRTIO_BLK_SECTOR_SIZE;
    desc[1].flags = DESC_NEXT | (type == BLOCK_READ ? DESC_WRITE : 0);
    desc[1].next = 2;
    desc[2].addr = (uint64_t)&request_status;
    desc[2].len = 1;
    desc[2].flags = DESC_WRITE;
    desc[2].next = 0;
    avail.ring[avail_idx % queue_size] = 0;
    avail_idx++;
    __asm__ volatile("dmb sy" ::: "memory");
    avail.idx = avail_idx;
    __asm__ volatile("dmb sy" ::: "memory");
    mmio_write(REG_QUEUE_NOTIFY, 0);
    uint32_t timeout = 100000000;
    while (used.idx == used_idx && timeout) timeout--;
    if (used.idx == used_idx) return 0;
    __asm__ volatile("dmb sy" ::: "memory");
    used_idx++;
    return request_status == 0;
}

int virtio_blk_read(uint64_t sector, void *buffer)
{
    return transfer(BLOCK_READ, sector, buffer);
}

int virtio_blk_write(uint64_t sector, const void *buffer)
{
    return transfer(BLOCK_WRITE, sector, (void *)buffer);
}
