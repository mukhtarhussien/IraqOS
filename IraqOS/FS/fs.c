#include "fs.h"
#include "../Drivers/virtio_blk.h"

#define SECTOR_SIZE 512u
#define CLUSTER_SECTORS 8u
#define CLUSTER_BYTES (SECTOR_SIZE * CLUSTER_SECTORS)
#define DIR_NAME 48u
#define ENTRY_FILE 1u
#define ENTRY_DIR 2u
#define FAT_END 0xFFFFFFFFu
#define FAT_RESERVED 0xFFFFFFF8u
#define FS_MAGIC "IRAQFS1"
#define FS_VERSION 1u
#define MAX_FILE_CLUSTERS 64u
#define PATH_MAX 192u

struct superblock {
    char magic[8];
    uint32_t version;
    uint32_t sector_size;
    uint32_t cluster_sectors;
    uint32_t fat_start;
    uint32_t fat_sectors;
    uint32_t data_start;
    uint32_t total_clusters;
    uint32_t root_cluster;
    uint32_t checksum;
    uint8_t padding[468];
};

struct dir_entry {
    char name[DIR_NAME];
    uint32_t cluster;
    uint32_t size;
    uint32_t type;
    uint32_t parent;
};

static uint8_t sector_buffer[SECTOR_SIZE] __attribute__((aligned(16)));
static struct superblock sb;
static int mounted;
static uint32_t alloc_hint = 3;
static char working_dir[PATH_MAX] = "/";

static int bytes_equal(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void bytes_copy(void *destination, const void *source, uint32_t n)
{
    uint8_t *d = destination;
    const uint8_t *s = source;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void bytes_zero(void *destination, uint32_t n)
{
    uint8_t *d = destination;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static uint32_t string_length(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int read_sector(uint64_t lba, void *buffer)
{
    return virtio_blk_read(lba, buffer);
}

static int write_sector(uint64_t lba, const void *buffer)
{
    return virtio_blk_write(lba, buffer);
}

static uint32_t cluster_lba(uint32_t cluster)
{
    return sb.data_start + (cluster - 2) * sb.cluster_sectors;
}

static int fat_get(uint32_t cluster, uint32_t *value)
{
    if (cluster >= sb.total_clusters + 2) return 0;
    uint32_t byte_offset = cluster * 4;
    uint32_t lba = sb.fat_start + byte_offset / SECTOR_SIZE;
    uint32_t offset = byte_offset % SECTOR_SIZE;
    if (!read_sector(lba, sector_buffer)) return 0;
    bytes_copy(value, sector_buffer + offset, sizeof(*value));
    return 1;
}

static int fat_set(uint32_t cluster, uint32_t value)
{
    if (cluster >= sb.total_clusters + 2) return 0;
    uint32_t byte_offset = cluster * 4;
    uint32_t lba = sb.fat_start + byte_offset / SECTOR_SIZE;
    uint32_t offset = byte_offset % SECTOR_SIZE;
    if (!read_sector(lba, sector_buffer)) return 0;
    bytes_copy(sector_buffer + offset, &value, sizeof(value));
    return write_sector(lba, sector_buffer);
}

static int clear_cluster(uint32_t cluster)
{
    bytes_zero(sector_buffer, sizeof(sector_buffer));
    uint32_t lba = cluster_lba(cluster);
    for (uint32_t i = 0; i < sb.cluster_sectors; i++)
        if (!write_sector(lba + i, sector_buffer)) return 0;
    return 1;
}

static int alloc_cluster(uint32_t *cluster_out)
{
    uint8_t fat_sector[SECTOR_SIZE] __attribute__((aligned(16)));
    uint32_t first = alloc_hint;
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t start = pass ? 2 : first;
        uint32_t end = pass ? first : sb.total_clusters + 2;
        uint32_t c = start;
        while (c < end) {
            uint32_t fat_sector_index = (c * 4) / SECTOR_SIZE;
            uint32_t sector_end = ((fat_sector_index + 1) * (SECTOR_SIZE / 4));
            if (!read_sector(sb.fat_start + fat_sector_index, fat_sector)) return 0;
            if (sector_end > end) sector_end = end;
            while (c < sector_end) {
                uint32_t value;
                bytes_copy(&value, fat_sector + (c % (SECTOR_SIZE / 4)) * 4, 4);
                if (value == 0) {
                    if (!fat_set(c, FAT_END) || !clear_cluster(c)) return 0;
                    alloc_hint = c + 1;
                    if (alloc_hint >= sb.total_clusters + 2) alloc_hint = 2;
                    *cluster_out = c;
                    return 1;
                }
                c++;
            }
        }
    }
    return 0;
}

static void free_chain(uint32_t first)
{
    uint32_t current = first;
    uint32_t guard = 0;
    while (current >= 2 && current < sb.total_clusters + 2 && guard++ < sb.total_clusters) {
        uint32_t next;
        if (!fat_get(current, &next) || !fat_set(current, 0)) return;
        if (next >= FAT_RESERVED) return;
        current = next;
    }
}

static int zero_disk_sector(const uint8_t *data)
{
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) if (data[i]) return 0;
    return 1;
}

static int format_blank_disk(void)
{
    uint64_t sectors = virtio_blk_sector_count();
    if (sectors < 1024 || sectors > 0xFFFFFFFFu) return 0;
    uint32_t fat_sectors = 1;
    uint32_t clusters = 0;
    for (int i = 0; i < 8; i++) {
        clusters = (uint32_t)((sectors - 1 - fat_sectors) / CLUSTER_SECTORS);
        uint32_t needed = ((clusters + 2) * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (needed == fat_sectors) break;
        fat_sectors = needed;
    }
    clusters = (uint32_t)((sectors - 1 - fat_sectors) / CLUSTER_SECTORS);
    bytes_zero(&sb, sizeof(sb));
    bytes_copy(sb.magic, FS_MAGIC, 7);
    sb.version = FS_VERSION;
    sb.sector_size = SECTOR_SIZE;
    sb.cluster_sectors = CLUSTER_SECTORS;
    sb.fat_start = 1;
    sb.fat_sectors = fat_sectors;
    sb.data_start = 1 + fat_sectors;
    sb.total_clusters = clusters;
    sb.root_cluster = 2;
    sb.checksum = 0x49524151u ^ sb.total_clusters ^ sb.data_start;
    bytes_zero(sector_buffer, sizeof(sector_buffer));
    if (!write_sector(sb.fat_start, sector_buffer)) return 0;
    uint32_t root = FAT_END;
    bytes_copy(sector_buffer + 2 * sizeof(uint32_t), &root, sizeof(root));
    if (!write_sector(sb.fat_start, sector_buffer)) return 0;
    if (!clear_cluster(sb.root_cluster)) return 0;
    return write_sector(0, &sb);
}

static int valid_superblock(const struct superblock *s)
{
    return bytes_equal(s->magic, FS_MAGIC, 7) && s->version == FS_VERSION &&
           s->sector_size == SECTOR_SIZE && s->cluster_sectors == CLUSTER_SECTORS &&
           s->fat_start == 1 && s->fat_sectors > 0 && s->data_start == 1 + s->fat_sectors &&
           s->root_cluster == 2 && s->total_clusters > 1 &&
           s->checksum == (0x49524151u ^ s->total_clusters ^ s->data_start) &&
           (uint64_t)s->data_start + (uint64_t)s->total_clusters * CLUSTER_SECTORS <= virtio_blk_sector_count();
}

static int load_superblock(void)
{
    if (!read_sector(0, sector_buffer)) return 0;
    if (zero_disk_sector(sector_buffer)) {
        if (!format_blank_disk()) return 0;
        if (!read_sector(0, sector_buffer)) return 0;
    }
    bytes_copy(&sb, sector_buffer, sizeof(sb));
    return valid_superblock(&sb);
}

int fs_init(void)
{
    mounted = 0;
    if (!virtio_blk_ready() || !load_superblock()) return 0;
    uint32_t root;
    if (!fat_get(sb.root_cluster, &root) || root < FAT_RESERVED) return 0;
    mounted = 1;
    alloc_hint = 3;
    working_dir[0] = '/'; working_dir[1] = '\0';
    return 1;
}

int fs_ready(void) { return mounted; }
uint64_t fs_total_bytes(void) { return (uint64_t)sb.total_clusters * CLUSTER_BYTES; }

static int normalize_path(const char *path, char *out)
{
    char joined[PATH_MAX];
    uint32_t n = 0;
    if (!path || !path[0]) path = ".";
    if (path[0] != '/') {
        uint32_t cwd_len = string_length(working_dir);
        if (cwd_len + 1 >= sizeof(joined)) return 0;
        for (uint32_t i = 0; i < cwd_len; i++) joined[n++] = working_dir[i];
        if (n > 1) joined[n++] = '/';
    }
    while (*path && n + 1 < sizeof(joined)) joined[n++] = *path++;
    if (*path) return 0;
    joined[n] = '\0';
    uint32_t pos = 0, out_len = 1;
    out[0] = '/'; out[1] = '\0';
    while (pos < n) {
        while (joined[pos] == '/') pos++;
        if (pos >= n) break;
        uint32_t start = pos;
        while (joined[pos] && joined[pos] != '/') pos++;
        uint32_t len = pos - start;
        if (len == 1 && joined[start] == '.') continue;
        if (len == 2 && joined[start] == '.' && joined[start + 1] == '.') {
            if (out_len > 1) {
                while (out_len > 1 && out[out_len - 1] != '/') out_len--;
                if (out_len > 1) out_len--;
                out[out_len] = '\0';
            }
            continue;
        }
        if (len >= DIR_NAME || out_len + len + 1 >= PATH_MAX) return 0;
        if (out_len > 1) out[out_len++] = '/';
        for (uint32_t i = 0; i < len; i++) out[out_len++] = joined[start + i];
        out[out_len] = '\0';
    }
    return 1;
}

static int find_entry(uint32_t dir_cluster, const char *name, struct dir_entry *found,
                      uint32_t *sector_out, uint32_t *slot_out)
{
    uint32_t current = dir_cluster, guard = 0;
    while (current >= 2 && current < sb.total_clusters + 2 && guard++ < sb.total_clusters) {
        uint32_t base = cluster_lba(current);
        for (uint32_t s = 0; s < sb.cluster_sectors; s++) {
            if (!read_sector(base + s, sector_buffer)) return 0;
            struct dir_entry *entries = (struct dir_entry *)sector_buffer;
            for (uint32_t i = 0; i < SECTOR_SIZE / sizeof(*entries); i++) {
                if (entries[i].name[0] && entries[i].type &&
                    bytes_equal(entries[i].name, name, string_length(name) + 1)) {
                    if (found) bytes_copy(found, &entries[i], sizeof(*found));
                    if (sector_out) *sector_out = base + s;
                    if (slot_out) *slot_out = i;
                    return 1;
                }
            }
        }
        uint32_t next;
        if (!fat_get(current, &next) || next >= FAT_RESERVED) break;
        current = next;
    }
    return 0;
}

static int add_entry(uint32_t dir_cluster, const struct dir_entry *entry)
{
    uint32_t current = dir_cluster, guard = 0;
    while (current >= 2 && current < sb.total_clusters + 2 && guard++ < sb.total_clusters) {
        uint32_t base = cluster_lba(current);
        for (uint32_t s = 0; s < sb.cluster_sectors; s++) {
            if (!read_sector(base + s, sector_buffer)) return 0;
            struct dir_entry *entries = (struct dir_entry *)sector_buffer;
            for (uint32_t i = 0; i < SECTOR_SIZE / sizeof(*entries); i++) {
                if (!entries[i].name[0]) {
                    bytes_copy(&entries[i], entry, sizeof(*entry));
                    return write_sector(base + s, sector_buffer);
                }
            }
        }
        uint32_t next;
        if (!fat_get(current, &next)) return 0;
        if (next >= FAT_RESERVED) {
            uint32_t extra;
            if (!alloc_cluster(&extra) || !fat_set(current, extra)) return 0;
            next = extra;
        }
        current = next;
    }
    return 0;
}

static int resolve_dir(const char *canonical, uint32_t *cluster_out)
{
    if (canonical[0] != '/') return 0;
    uint32_t current = sb.root_cluster;
    uint32_t pos = 1;
    while (canonical[pos]) {
        uint32_t start = pos;
        while (canonical[pos] && canonical[pos] != '/') pos++;
        uint32_t len = pos - start;
        if (!len || len >= DIR_NAME) return 0;
        char name[DIR_NAME];
        for (uint32_t i = 0; i < len; i++) name[i] = canonical[start + i];
        name[len] = '\0';
        struct dir_entry entry;
        if (!find_entry(current, name, &entry, 0, 0) || entry.type != ENTRY_DIR) return 0;
        current = entry.cluster;
        if (canonical[pos] == '/') pos++;
    }
    *cluster_out = current;
    return 1;
}

static int resolve_parent(const char *path, uint32_t *parent_out, char *leaf)
{
    char canonical[PATH_MAX];
    if (!normalize_path(path, canonical) || canonical[0] != '/') return 0;
    uint32_t len = string_length(canonical), split = len;
    while (split > 1 && canonical[split - 1] != '/') split--;
    uint32_t leaf_start = split == 1 ? 1 : split;
    uint32_t leaf_len = len - leaf_start;
    if (!leaf_len || leaf_len >= DIR_NAME) return 0;
    for (uint32_t i = 0; i < leaf_len; i++) leaf[i] = canonical[leaf_start + i];
    leaf[leaf_len] = '\0';
    char parent_path[PATH_MAX];
    uint32_t parent_len = split == 1 ? 1 : split - 1;
    for (uint32_t i = 0; i < parent_len; i++) parent_path[i] = canonical[i];
    parent_path[parent_len] = '\0';
    return resolve_dir(parent_path, parent_out);
}

static int resolve_file(const char *path, struct dir_entry *entry)
{
    uint32_t parent;
    char leaf[DIR_NAME];
    return resolve_parent(path, &parent, leaf) && find_entry(parent, leaf, entry, 0, 0) &&
           entry->type == ENTRY_FILE;
}

int fs_list(const char *path, fs_list_fn callback, void *context)
{
    if (!mounted || !callback) return 0;
    char canonical[PATH_MAX];
    uint32_t dir;
    if (!normalize_path(path, canonical) || !resolve_dir(canonical, &dir)) return 0;
    uint32_t current = dir, guard = 0;
    while (current >= 2 && current < sb.total_clusters + 2 && guard++ < sb.total_clusters) {
        uint32_t base = cluster_lba(current);
        for (uint32_t s = 0; s < sb.cluster_sectors; s++) {
            if (!read_sector(base + s, sector_buffer)) return 0;
            struct dir_entry *entries = (struct dir_entry *)sector_buffer;
            for (uint32_t i = 0; i < SECTOR_SIZE / sizeof(*entries); i++) {
                if (entries[i].name[0] && entries[i].type &&
                    !callback(entries[i].name, entries[i].type == ENTRY_DIR, entries[i].size, context))
                    return 1;
            }
        }
        uint32_t next;
        if (!fat_get(current, &next) || next >= FAT_RESERVED) break;
        current = next;
    }
    return 1;
}

int fs_read_file(const char *path, void *buffer, uint32_t capacity, uint32_t *size_out)
{
    if (!mounted || !buffer) return 0;
    struct dir_entry entry;
    if (!resolve_file(path, &entry) || entry.size >= capacity) return 0;
    uint32_t remaining = entry.size, current = entry.cluster, offset = 0, guard = 0;
    while (remaining && current >= 2 && current < sb.total_clusters + 2 && guard++ < MAX_FILE_CLUSTERS) {
        uint32_t base = cluster_lba(current);
        for (uint32_t s = 0; s < sb.cluster_sectors && remaining; s++) {
            if (!read_sector(base + s, sector_buffer)) return 0;
            uint32_t count = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            bytes_copy((uint8_t *)buffer + offset, sector_buffer, count);
            offset += count; remaining -= count;
        }
        if (remaining && (!fat_get(current, &current) || current >= FAT_RESERVED)) return 0;
    }
    if (remaining) return 0;
    if (size_out) *size_out = entry.size;
    ((uint8_t *)buffer)[entry.size] = 0;
    return 1;
}

int fs_write_file(const char *path, const void *buffer, uint32_t size)
{
    if (!mounted || (!buffer && size)) return 0;
    uint32_t parent;
    char leaf[DIR_NAME];
    if (!resolve_parent(path, &parent, leaf)) return 0;
    struct dir_entry entry;
    uint32_t old_cluster = 0;
    if (find_entry(parent, leaf, &entry, 0, 0)) {
        if (entry.type != ENTRY_FILE) return 0;
        old_cluster = entry.cluster;
    } else {
        bytes_zero(&entry, sizeof(entry));
        uint32_t name_len = string_length(leaf);
        bytes_copy(entry.name, leaf, name_len + 1);
        entry.type = ENTRY_FILE;
        entry.parent = parent;
    }
    uint32_t needed = (size + CLUSTER_BYTES - 1) / CLUSTER_BYTES;
    if (needed > MAX_FILE_CLUSTERS) return 0;
    uint32_t allocated[MAX_FILE_CLUSTERS], count = 0;
    for (; count < needed; count++) {
        if (!alloc_cluster(&allocated[count])) {
            for (uint32_t i = 0; i < count; i++) free_chain(allocated[i]);
            return 0;
        }
        if (count && !fat_set(allocated[count - 1], allocated[count])) {
            for (uint32_t i = 0; i <= count; i++) free_chain(allocated[i]);
            return 0;
        }
    }
    uint32_t remaining = size, offset = 0;
    for (uint32_t c = 0; c < needed; c++) {
        uint32_t base = cluster_lba(allocated[c]);
        for (uint32_t s = 0; s < sb.cluster_sectors; s++) {
            uint32_t count_bytes = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            bytes_zero(sector_buffer, sizeof(sector_buffer));
            if (count_bytes) bytes_copy(sector_buffer, (const uint8_t *)buffer + offset, count_bytes);
            if (!write_sector(base + s, sector_buffer)) return 0;
            offset += count_bytes;
            remaining -= count_bytes;
        }
    }
    entry.cluster = needed ? allocated[0] : 0;
    entry.size = size;
    if (old_cluster) free_chain(old_cluster);
    if (find_entry(parent, leaf, 0, 0, 0)) {
        uint32_t sec, slot;
        if (!find_entry(parent, leaf, 0, &sec, &slot) || !read_sector(sec, sector_buffer)) return 0;
        bytes_copy(((struct dir_entry *)sector_buffer) + slot, &entry, sizeof(entry));
        return write_sector(sec, sector_buffer);
    }
    return add_entry(parent, &entry);
}

int fs_mkdir(const char *path)
{
    if (!mounted) return 0;
    uint32_t parent, cluster;
    char leaf[DIR_NAME];
    if (!resolve_parent(path, &parent, leaf) || find_entry(parent, leaf, 0, 0, 0)) return 0;
    if (!alloc_cluster(&cluster)) return 0;
    struct dir_entry entry;
    bytes_zero(&entry, sizeof(entry));
    bytes_copy(entry.name, leaf, string_length(leaf) + 1);
    entry.cluster = cluster; entry.type = ENTRY_DIR; entry.parent = parent;
    if (!add_entry(parent, &entry)) { free_chain(cluster); return 0; }
    return 1;
}

int fs_remove(const char *path)
{
    if (!mounted) return 0;
    uint32_t parent, sec, slot;
    char leaf[DIR_NAME];
    if (!resolve_parent(path, &parent, leaf)) return 0;
    struct dir_entry entry;
    if (!find_entry(parent, leaf, &entry, &sec, &slot) || entry.type != ENTRY_FILE ||
        !read_sector(sec, sector_buffer)) return 0;
    bytes_zero(((struct dir_entry *)sector_buffer) + slot, sizeof(entry));
    if (!write_sector(sec, sector_buffer)) return 0;
    if (entry.cluster) free_chain(entry.cluster);
    return 1;
}

static int directory_exists(const char *canonical, uint32_t *cluster)
{
    return resolve_dir(canonical, cluster);
}

int fs_chdir(const char *path)
{
    if (!mounted) return 0;
    char canonical[PATH_MAX];
    uint32_t cluster;
    if (!normalize_path(path, canonical) || !directory_exists(canonical, &cluster)) return 0;
    uint32_t len = string_length(canonical);
    bytes_copy(working_dir, canonical, len + 1);
    return 1;
}

const char *fs_getcwd(void) { return working_dir; }

uint64_t fs_free_bytes(void)
{
    if (!mounted) return 0;
    uint8_t fat_sector[SECTOR_SIZE] __attribute__((aligned(16)));
    uint64_t free_clusters = 0;
    uint32_t max_index = sb.total_clusters + 2;
    for (uint32_t s = 0; s < sb.fat_sectors; s++) {
        if (!read_sector(sb.fat_start + s, fat_sector)) return 0;
        for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
            uint32_t c = s * (SECTOR_SIZE / 4) + i;
            if (c >= 2 && c < max_index) {
                uint32_t value;
                bytes_copy(&value, fat_sector + i * 4, 4);
                if (!value) free_clusters++;
            }
        }
    }
    return free_clusters * CLUSTER_BYTES;
}
