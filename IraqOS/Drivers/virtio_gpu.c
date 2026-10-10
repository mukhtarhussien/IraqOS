#include "virtio_gpu.h"
#include "../Kernel/printk.h"

#define VIRTIO_MMIO_BASE   0x0A000000
#define VIRTIO_MMIO_STRIDE 0x200
#define VIRTIO_MMIO_COUNT  32

#define REG_MAGIC           0x000
#define REG_DEVICE_ID       0x008
#define REG_DEVICE_FEATURES  0x010
#define REG_DEVICE_FEATURES_SEL 0x014
#define REG_DRIVER_FEATURES  0x020
#define REG_DRIVER_FEATURES_SEL 0x024
#define REG_QUEUE_SEL       0x030
#define REG_QUEUE_NUM_MAX   0x034
#define REG_QUEUE_NUM       0x038
#define REG_QUEUE_READY     0x044
#define REG_QUEUE_NOTIFY    0x050
#define REG_STATUS          0x070
#define REG_QUEUE_DESC_LOW  0x080
#define REG_QUEUE_DESC_HIGH 0x084
#define REG_QUEUE_DRIVER_LOW  0x090
#define REG_QUEUE_DRIVER_HIGH 0x094
#define REG_QUEUE_DEVICE_LOW  0x0A0
#define REG_QUEUE_DEVICE_HIGH 0x0A4

#define VIRTIO_MAGIC     0x74726976
#define VIRTIO_GPU_ID    16

#define STATUS_ACK          1
#define STATUS_DRIVER       2
#define STATUS_FEATURES_OK  8
#define STATUS_DRIVER_OK    4
#define STATUS_FAILED       128

#define VIRTIO_F_VERSION_1  0

#define QUEUE_SIZE 8

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO      0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D    0x0101
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_SET_SCANOUT           0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH        0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D   0x0105

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1

struct virtq_desc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail
{
    uint16_t flags;
    volatile uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
};

struct virtq_used_elem
{
    uint32_t id;
    uint32_t len;
};

struct virtq_used
{
    uint16_t flags;
    volatile uint16_t idx;
    struct virtq_used_elem ring[QUEUE_SIZE];
};

struct gpu_ctrl_hdr
{
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
};

struct gpu_rect
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct gpu_resource_create_2d
{
    struct gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

struct gpu_resource_attach_backing
{
    struct gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
};

struct gpu_set_scanout
{
    struct gpu_ctrl_hdr hdr;
    struct gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
};

struct gpu_transfer_to_host_2d
{
    struct gpu_ctrl_hdr hdr;
    struct gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
};

struct gpu_resource_flush
{
    struct gpu_ctrl_hdr hdr;
    struct gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
};

static uint32_t framebuffer[FB_WIDTH * FB_HEIGHT] __attribute__((aligned(4096)));

static struct virtq_desc   desc_table[QUEUE_SIZE]  __attribute__((aligned(16)));
static struct virtq_avail  avail_ring              __attribute__((aligned(2)));
static struct virtq_used   used_ring               __attribute__((aligned(4)));

static uint8_t cmd_buf[512]   __attribute__((aligned(16)));
static uint8_t resp_buf[512]  __attribute__((aligned(16)));

static uint64_t gpu_base = 0;
static int gpu_ready = 0;
static uint16_t avail_idx = 0;
static uint16_t last_used_idx = 0;
static int dirty_valid;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static void mark_dirty(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;
    if (!dirty_valid) {
        dirty_x0 = x0; dirty_y0 = y0;
        dirty_x1 = x1; dirty_y1 = y1;
        dirty_valid = 1;
        return;
    }
    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

static uint32_t mmio_read(uint64_t base, uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(base + offset);
    return *reg;
}

static void mmio_write(uint64_t base, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(base + offset);
    *reg = value;
}

static uint64_t find_gpu_device(void)
{
    for (int i = 0; i < VIRTIO_MMIO_COUNT; i++)
    {
        uint64_t base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = mmio_read(base, REG_MAGIC);

        if (magic != VIRTIO_MAGIC)
            continue;

        uint32_t device_id = mmio_read(base, REG_DEVICE_ID);

        if (device_id == VIRTIO_GPU_ID)
            return base;
    }

    return 0;
}

static int queue_setup(void)
{
    mmio_write(gpu_base, REG_QUEUE_SEL, 0);

    uint32_t max = mmio_read(gpu_base, REG_QUEUE_NUM_MAX);

    if (max == 0)
    {
        printk("VirtIO GPU: queue not available\n");
        return 0;
    }

    mmio_write(gpu_base, REG_QUEUE_NUM, QUEUE_SIZE);

    uint64_t desc_addr = (uint64_t)&desc_table[0];
    uint64_t avail_addr = (uint64_t)&avail_ring;
    uint64_t used_addr = (uint64_t)&used_ring;

    mmio_write(gpu_base, REG_QUEUE_DESC_LOW, (uint32_t)(desc_addr & 0xFFFFFFFF));
    mmio_write(gpu_base, REG_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));

    mmio_write(gpu_base, REG_QUEUE_DRIVER_LOW, (uint32_t)(avail_addr & 0xFFFFFFFF));
    mmio_write(gpu_base, REG_QUEUE_DRIVER_HIGH, (uint32_t)(avail_addr >> 32));

    mmio_write(gpu_base, REG_QUEUE_DEVICE_LOW, (uint32_t)(used_addr & 0xFFFFFFFF));
    mmio_write(gpu_base, REG_QUEUE_DEVICE_HIGH, (uint32_t)(used_addr >> 32));

    mmio_write(gpu_base, REG_QUEUE_READY, 1);

    if (mmio_read(gpu_base, REG_QUEUE_READY) != 1)
    {
        printk("VirtIO GPU: queue setup rejected\n");
        return 0;
    }

    printk("VirtIO GPU: queue ready\n");
    return 1;
}

static int send_command(uint32_t cmd_len, uint32_t resp_len)
{
    desc_table[0].addr = (uint64_t)&cmd_buf[0];
    desc_table[0].len = cmd_len;
    desc_table[0].flags = VIRTQ_DESC_F_NEXT;
    desc_table[0].next = 1;

    desc_table[1].addr = (uint64_t)&resp_buf[0];
    desc_table[1].len = resp_len;
    desc_table[1].flags = VIRTQ_DESC_F_WRITE;
    desc_table[1].next = 0;

    avail_ring.ring[avail_idx % QUEUE_SIZE] = 0;
    avail_idx++;

    __asm__ volatile("dmb sy" ::: "memory");

    avail_ring.idx = avail_idx;

    __asm__ volatile("dmb sy" ::: "memory");

    mmio_write(gpu_base, REG_QUEUE_NOTIFY, 0);

    uint32_t timeout = 100000000;
    while (used_ring.idx == last_used_idx && timeout != 0)
        timeout--;

    if (used_ring.idx == last_used_idx)
    {
        printk("VirtIO GPU: command timed out\n");
        return 0;
    }

    __asm__ volatile("dmb sy" ::: "memory");

    last_used_idx = used_ring.idx;
    return 1;
}

static void gpu_resource_create(uint32_t resource_id, uint32_t width, uint32_t height)
{
    struct gpu_resource_create_2d *cmd = (struct gpu_resource_create_2d *)cmd_buf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->resource_id = resource_id;
    cmd->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd->width = width;
    cmd->height = height;

    if (!send_command(sizeof(struct gpu_resource_create_2d), sizeof(struct gpu_ctrl_hdr)))
        printk("VirtIO GPU: create resource failed\n");
}

static void gpu_attach_backing(uint32_t resource_id, uint64_t addr, uint32_t length)
{
    struct gpu_resource_attach_backing *cmd = (struct gpu_resource_attach_backing *)cmd_buf;

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->resource_id = resource_id;
    cmd->nr_entries = 1;
    cmd->addr = addr;
    cmd->length = length;
    cmd->padding = 0;

    if (!send_command(sizeof(struct gpu_resource_attach_backing), sizeof(struct gpu_ctrl_hdr)))
        printk("VirtIO GPU: attach backing failed\n");
}

static void gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t width, uint32_t height)
{
    struct gpu_set_scanout *cmd = (struct gpu_set_scanout *)cmd_buf;

    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->r.x = 0;
    cmd->r.y = 0;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->scanout_id = scanout_id;
    cmd->resource_id = resource_id;

    if (!send_command(sizeof(struct gpu_set_scanout), sizeof(struct gpu_ctrl_hdr)))
        printk("VirtIO GPU: set scanout failed\n");
}

static int gpu_transfer_to_host(uint32_t resource_id, uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height)
{
    struct gpu_transfer_to_host_2d *cmd = (struct gpu_transfer_to_host_2d *)cmd_buf;
    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->offset = ((uint64_t)y * FB_WIDTH + x) * sizeof(uint32_t);
    cmd->resource_id = resource_id;
    cmd->padding = 0;
    return send_command(sizeof(struct gpu_transfer_to_host_2d),
                        sizeof(struct gpu_ctrl_hdr));
}

static int gpu_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                     uint32_t width, uint32_t height)
{
    struct gpu_resource_flush *cmd = (struct gpu_resource_flush *)cmd_buf;
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->resource_id = resource_id;
    cmd->padding = 0;
    return send_command(sizeof(struct gpu_resource_flush), sizeof(struct gpu_ctrl_hdr));
}

int virtio_gpu_is_ready(void)
{
    return gpu_ready;
}

void virtio_gpu_init(void)
{
    printk("VirtIO GPU: initializing\n");

    fb_clear(0x000000FF);

    gpu_base = find_gpu_device();

    if (gpu_base == 0)
    {
        printk("VirtIO GPU: device not found\n");
        return;
    }

    printk("VirtIO GPU: device found\n");

    mmio_write(gpu_base, REG_STATUS, 0);
    mmio_write(gpu_base, REG_STATUS, STATUS_ACK);
    mmio_write(gpu_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER);

    mmio_write(gpu_base, REG_DEVICE_FEATURES_SEL, 1);
    uint32_t device_features_hi = mmio_read(gpu_base, REG_DEVICE_FEATURES);
    if ((device_features_hi & (1u << VIRTIO_F_VERSION_1)) == 0)
    {
        printk("VirtIO GPU: VERSION_1 feature missing\n");
        mmio_write(gpu_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FAILED);
        return;
    }

    mmio_write(gpu_base, REG_DRIVER_FEATURES_SEL, 1);
    mmio_write(gpu_base, REG_DRIVER_FEATURES, 1u << VIRTIO_F_VERSION_1);
    mmio_write(gpu_base, REG_DEVICE_FEATURES_SEL, 0);
    mmio_write(gpu_base, REG_DRIVER_FEATURES_SEL, 0);
    mmio_write(gpu_base, REG_DRIVER_FEATURES, 0);
    __asm__ volatile("dmb sy" ::: "memory");

    mmio_write(gpu_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);

    uint32_t status = mmio_read(gpu_base, REG_STATUS);

    if ((status & STATUS_FEATURES_OK) == 0)
    {
        printk("VirtIO GPU: features not accepted\n");
        mmio_write(gpu_base, REG_STATUS, status | STATUS_FAILED);
        return;
    }

    if (!queue_setup())
    {
        mmio_write(gpu_base, REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_FAILED);
        return;
    }

    mmio_write(gpu_base, REG_STATUS, status | STATUS_DRIVER_OK);

    printk("VirtIO GPU: driver ok\n");

    gpu_resource_create(1, FB_WIDTH, FB_HEIGHT);
    printk("VirtIO GPU: resource created\n");

    gpu_attach_backing(1, (uint64_t)&framebuffer[0], FB_WIDTH * FB_HEIGHT * 4);
    printk("VirtIO GPU: backing attached\n");

    gpu_set_scanout(0, 1, FB_WIDTH, FB_HEIGHT);
    printk("VirtIO GPU: scanout set\n");

    printk("VirtIO GPU: ready\n");
    gpu_ready = 1;
}

static int clamp_logical(int value, int limit)
{
    if (value < 0) return 0;
    if (value > limit) return limit;
    return value;
}

static int map_x(int x)
{
    return (int)((uint64_t)x * FB_WIDTH / UI_WIDTH);
}

static int map_y(int y)
{
    return (int)((uint64_t)y * FB_HEIGHT / UI_HEIGHT);
}

void fb_put_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT)
        return;

    int x0 = map_x(x), x1 = map_x(x + 1);
    int y0 = map_y(y), y1 = map_y(y + 1);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++)
            framebuffer[py * FB_WIDTH + px] = color;
    mark_dirty(x0, y0, x1, y1);
}

uint32_t fb_get_pixel(int x, int y)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT)
        return 0;
    return framebuffer[map_y(y) * FB_WIDTH + map_x(x)];
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    int x0 = clamp_logical(x, UI_WIDTH);
    int y0 = clamp_logical(y, UI_HEIGHT);
    int x1 = clamp_logical(x + w, UI_WIDTH);
    int y1 = clamp_logical(y + h, UI_HEIGHT);
    if (x1 <= x0 || y1 <= y0) return;

    int px0 = map_x(x0), py0 = map_y(y0);
    int px1 = map_x(x1), py1 = map_y(y1);
    if (px1 <= px0) px1 = px0 + 1;
    if (py1 <= py0) py1 = py0 + 1;
    if (px1 > FB_WIDTH) px1 = FB_WIDTH;
    if (py1 > FB_HEIGHT) py1 = FB_HEIGHT;
    for (int py = py0; py < py1; py++)
        for (int px = px0; px < px1; px++)
            framebuffer[py * FB_WIDTH + px] = color;
    mark_dirty(px0, py0, px1, py1);
}

void fb_clear(uint32_t color)
{
    fb_fill_rect(0, 0, UI_WIDTH, UI_HEIGHT, color);
}

void fb_scroll_up(int pixels, uint32_t color)
{
    if (pixels <= 0) return;
    if (pixels > UI_HEIGHT) pixels = UI_HEIGHT;
    uint32_t physical_rows = (uint32_t)map_y(pixels);
    if (physical_rows == 0) physical_rows = 1;
    if (physical_rows > FB_HEIGHT) physical_rows = FB_HEIGHT;

    uint32_t move_pixels = (uint32_t)(FB_HEIGHT - physical_rows) * FB_WIDTH;
    uint32_t skip_pixels = physical_rows * FB_WIDTH;
    uint32_t total_pixels = (uint32_t)FB_HEIGHT * FB_WIDTH;
    for (uint32_t i = 0; i < move_pixels; i++)
        framebuffer[i] = framebuffer[i + skip_pixels];
    for (uint32_t i = move_pixels; i < total_pixels; i++)
        framebuffer[i] = color;
    mark_dirty(0, 0, FB_WIDTH, FB_HEIGHT);
}

void virtio_gpu_flush(void)
{
    if (gpu_base == 0 || !dirty_valid)
        return;

    int x = dirty_x0, y = dirty_y0;
    int width = dirty_x1 - dirty_x0, height = dirty_y1 - dirty_y0;
    if (!gpu_transfer_to_host(1, (uint32_t)x, (uint32_t)y,
                              (uint32_t)width, (uint32_t)height))
        return;
    if (!gpu_flush(1, (uint32_t)x, (uint32_t)y,
                   (uint32_t)width, (uint32_t)height))
        return;
    dirty_valid = 0;
}
