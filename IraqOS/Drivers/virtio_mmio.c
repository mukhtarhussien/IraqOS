#include "virtio_mmio.h"

#define FDT_MAGIC 0xD00DFEEDu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE 2u
#define FDT_PROP 3u
#define FDT_NOP 4u
#define FDT_END 9u
#define FDT_MAX_DEPTH 64
#define VIRTIO_MAGIC 0x74726976u

struct fdt_node {
    uint32_t reg_addr_cells, reg_size_cells;
    uint32_t child_addr_cells, child_size_cells;
    const uint8_t *reg;
    uint32_t reg_len;
    int virtio_mmio;
};

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t align4(uint32_t value) { return (value + 3u) & ~3u; }

static uint64_t read_cells(const uint8_t *p, uint32_t cells)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < cells; i++)
        value = (value << 32) | read_be32(p + i * 4);
    return value;
}

static int has_compatible(const uint8_t *list, uint32_t len)
{
    static const char wanted[] = "virtio,mmio";
    uint32_t pos = 0;
    while (pos < len) {
        uint32_t end = pos;
        while (end < len && list[end]) end++;
        if (end == len) return 0;
        uint32_t i = 0;
        while (pos + i < end && wanted[i] && list[pos + i] == wanted[i]) i++;
        if (pos + i == end && wanted[i] == '\0') return 1;
        pos = end + 1;
    }
    return 0;
}

static uint64_t find_in_node(const struct fdt_node *node, uint32_t wanted_id)
{
    uint32_t ac = node->reg_addr_cells, sc = node->reg_size_cells;
    if (!node->virtio_mmio || !node->reg || ac == 0 || ac > 2 || sc > 2) return 0;
    uint32_t tuple = (ac + sc) * 4;
    if (!tuple || node->reg_len < tuple || node->reg_len % tuple) return 0;
    for (uint32_t off = 0; off < node->reg_len; off += tuple) {
        uint64_t base = read_cells(node->reg + off, ac);
        uint64_t size = sc ? read_cells(node->reg + off + ac * 4, sc) : 0;
        if (!base || size < 0x100) continue;
        volatile uint32_t *magic = (volatile uint32_t *)(base + 0x000);
        volatile uint32_t *id = (volatile uint32_t *)(base + 0x008);
        if (*magic == VIRTIO_MAGIC && *id == wanted_id) return base;
    }
    return 0;
}

uint64_t virtio_mmio_find_nth(const void *blob, uint32_t device_id, uint32_t index)
{
    if (!blob) return 0;
    const uint8_t *fdt = (const uint8_t *)blob;
    if (read_be32(fdt) != FDT_MAGIC) return 0;
    uint32_t total = read_be32(fdt + 4), so = read_be32(fdt + 8);
    uint32_t sto = read_be32(fdt + 12), ss = read_be32(fdt + 32);
    uint32_t zs = read_be32(fdt + 36);
    if (total < 40 || so > total || zs > total - so || sto > total || ss > total - sto)
        return 0;

    const uint8_t *structure = fdt + so, *end = structure + zs;
    const uint8_t *strings = fdt + sto, *p = structure;
    struct fdt_node nodes[FDT_MAX_DEPTH];
    int depth = -1;
    uint32_t matches = 0;
    while (p + 4 <= end) {
        uint32_t token = read_be32(p); p += 4;
        if (token == FDT_BEGIN_NODE) {
            if (depth + 1 >= FDT_MAX_DEPTH) return 0;
            depth++;
            struct fdt_node *n = &nodes[depth];
            uint32_t ac = depth ? nodes[depth - 1].child_addr_cells : 2;
            uint32_t sc = depth ? nodes[depth - 1].child_size_cells : 1;
            n->reg_addr_cells = n->child_addr_cells = ac;
            n->reg_size_cells = n->child_size_cells = sc;
            n->reg = 0; n->reg_len = 0; n->virtio_mmio = 0;
            while (p < end && *p) p++;
            if (p == end) return 0;
            p = structure + align4((uint32_t)(p + 1 - structure));
        } else if (token == FDT_END_NODE) {
            if (depth < 0) return 0;
            uint64_t base = find_in_node(&nodes[depth], device_id);
            if (base && matches++ == index) return base;
            depth--;
        } else if (token == FDT_PROP) {
            if (depth < 0 || p + 8 > end) return 0;
            uint32_t len = read_be32(p), nameoff = read_be32(p + 4); p += 8;
            if (len > (uint32_t)(end - p) || nameoff >= ss) return 0;
            const uint8_t *value = p;
            const char *name = (const char *)(strings + nameoff);
            uint32_t left = ss - nameoff, nlen = 0;
            while (nlen < left && name[nlen]) nlen++;
            if (nlen == left) return 0;
            struct fdt_node *n = &nodes[depth];
            if (nlen == 3 && name[0] == 'r' && name[1] == 'e' && name[2] == 'g') {
                n->reg = value; n->reg_len = len;
            } else if (nlen == 10 && name[0] == 'c' && name[1] == 'o' && name[2] == 'm' &&
                       name[3] == 'p' && name[4] == 'a' && name[5] == 't' && name[6] == 'i' &&
                       name[7] == 'b' && name[8] == 'l' && name[9] == 'e') {
                n->virtio_mmio = has_compatible(value, len);
            } else if (len == 4 && nlen == 14 && name[0] == '#' && name[1] == 'a' &&
                       name[2] == 'd' && name[3] == 'd' && name[4] == 'r' && name[5] == 'e' &&
                       name[6] == 's' && name[7] == 's' && name[8] == '-' && name[9] == 'c' &&
                       name[10] == 'e' && name[11] == 'l' && name[12] == 'l' && name[13] == 's') {
                n->child_addr_cells = read_be32(value);
            } else if (len == 4 && nlen == 11 && name[0] == '#' && name[1] == 's' &&
                       name[2] == 'i' && name[3] == 'z' && name[4] == 'e' && name[5] == '-' &&
                       name[6] == 'c' && name[7] == 'e' && name[8] == 'l' && name[9] == 'l' &&
                       name[10] == 's') {
                n->child_size_cells = read_be32(value);
            }
            p = structure + align4((uint32_t)(p + len - structure));
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            return 0;
        }
    }
    return 0;
}

uint64_t virtio_mmio_find(const void *blob, uint32_t device_id)
{
    return virtio_mmio_find_nth(blob, device_id, 0);
}
