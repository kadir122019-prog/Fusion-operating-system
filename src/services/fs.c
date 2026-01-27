#include "services/fs.h"
#include "drivers/virtio_blk.h"
#include "kernel/memory.h"

#define FAT32_ATTR_LFN 0x0F
#define FAT32_ATTR_DIR 0x10

typedef struct {
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u32 fat_size;
    u32 total_sectors;
    u32 root_cluster;
    u32 fat_start_lba;
    u32 data_start_lba;
    u32 total_clusters;
    u32 part_lba;
    int mounted;
} fat32_fs_t;

typedef struct {
    u8 name[11];
    u8 attr;
    u8 ntres;
    u8 crt_time_tenth;
    u16 crt_time;
    u16 crt_date;
    u16 lst_acc_date;
    u16 fst_clus_hi;
    u16 wrt_time;
    u16 wrt_date;
    u16 fst_clus_lo;
    u32 file_size;
} __attribute__((packed)) fat_dirent_t;

typedef struct {
    u8 order;
    u16 name1[5];
    u8 attr;
    u8 type;
    u8 checksum;
    u16 name2[6];
    u16 zero;
    u16 name3[2];
} __attribute__((packed)) fat_lfn_t;

static fat32_fs_t fs;
static u8 sector_buf[512];

static u16 le16(const void *p) {
    const u8 *b = p;
    return (u16)(b[0] | (b[1] << 8));
}

static u32 le32(const void *p) {
    const u8 *b = p;
    return (u32)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

static void set_le32(void *p, u32 v) {
    u8 *b = p;
    b[0] = (u8)(v & 0xFF);
    b[1] = (u8)((v >> 8) & 0xFF);
    b[2] = (u8)((v >> 16) & 0xFF);
    b[3] = (u8)((v >> 24) & 0xFF);
}

static int blk_read(u64 lba, u32 count, void *buf) {
    return virtio_blk_read(lba, count, buf);
}

static int blk_write(u64 lba, u32 count, const void *buf) {
    return virtio_blk_write(lba, count, buf);
}

static u32 cluster_to_lba(u32 cluster) {
    return fs.data_start_lba + (cluster - 2) * fs.sectors_per_cluster;
}

static int fat_read_sector(u32 lba) {
    return blk_read(lba, 1, sector_buf);
}

static int fat_write_sector(u32 lba) {
    return blk_write(lba, 1, sector_buf);
}

static u32 fat_get_entry(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 sector = fs.fat_start_lba + (fat_offset / fs.bytes_per_sector);
    u32 offset = fat_offset % fs.bytes_per_sector;
    if (!fat_read_sector(sector)) return 0x0FFFFFFF;
    u32 value = le32(&sector_buf[offset]) & 0x0FFFFFFF;
    return value;
}

static int fat_set_entry(u32 cluster, u32 value) {
    u32 fat_offset = cluster * 4;
    for (u8 copy = 0; copy < fs.fat_count; copy++) {
        u32 sector = fs.fat_start_lba + (fat_offset / fs.bytes_per_sector) + (copy * fs.fat_size);
        u32 offset = fat_offset % fs.bytes_per_sector;
        if (!fat_read_sector(sector)) return 0;
        set_le32(&sector_buf[offset], value);
        if (!fat_write_sector(sector)) return 0;
    }
    return 1;
}

static int fat_alloc_cluster(u32 *out_cluster) {
    for (u32 cluster = 2; cluster < fs.total_clusters; cluster++) {
        if (fat_get_entry(cluster) == 0) {
            if (!fat_set_entry(cluster, 0x0FFFFFFF)) return 0;
            *out_cluster = cluster;
            return 1;
        }
    }
    return 0;
}

static int fat_free_chain(u32 start) {
    u32 cluster = start;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 next = fat_get_entry(cluster);
        if (!fat_set_entry(cluster, 0)) return 0;
        cluster = next;
    }
    return 1;
}

static u32 dirent_first_cluster(const fat_dirent_t *ent) {
    return ((u32)ent->fst_clus_hi << 16) | ent->fst_clus_lo;
}

static void dirent_set_first_cluster(fat_dirent_t *ent, u32 cluster) {
    ent->fst_clus_hi = (u16)(cluster >> 16);
    ent->fst_clus_lo = (u16)(cluster & 0xFFFF);
}

static void name_to_short(const char *name, u8 out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0;
    int j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[j++] = (u8)c;
    }
    if (name[i] == '.') i++;
    j = 8;
    while (name[i] && j < 11) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[j++] = (u8)c;
    }
}

static int short_to_name(const u8 in[11], char *out, int max) {
    int pos = 0;
    for (int i = 0; i < 8 && pos < max - 1; i++) {
        if (in[i] == ' ') break;
        out[pos++] = (char)in[i];
    }
    int ext_start = pos;
    for (int i = 8; i < 11; i++) {
        if (in[i] != ' ') {
            if (ext_start == pos && pos < max - 1) {
                out[pos++] = '.';
            }
            if (pos < max - 1) out[pos++] = (char)in[i];
        }
    }
    out[pos] = 0;
    return pos;
}

static int name_equals(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static int fs_stricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int fs_extcmp(const char *a, const char *b) {
    const char *ea = a;
    const char *eb = b;
    for (const char *p = a; *p; p++) if (*p == '.') ea = p + 1;
    for (const char *p = b; *p; p++) if (*p == '.') eb = p + 1;
    int cmp = fs_stricmp(ea, eb);
    if (cmp != 0) return cmp;
    return fs_stricmp(a, b);
}

static void lfn_extract_part(const fat_lfn_t *lfn, char *out, int max) {
    int pos = 0;
    const u8 *name1 = (const u8 *)lfn->name1;
    const u8 *name2 = (const u8 *)lfn->name2;
    const u8 *name3 = (const u8 *)lfn->name3;
    for (int i = 0; i < 5 && pos < max - 1; i++) {
        u16 ch = le16(name1 + i * 2);
        if (ch == 0x0000 || ch == 0xFFFF) break;
        out[pos++] = (char)(ch & 0xFF);
    }
    for (int i = 0; i < 6 && pos < max - 1; i++) {
        u16 ch = le16(name2 + i * 2);
        if (ch == 0x0000 || ch == 0xFFFF) break;
        out[pos++] = (char)(ch & 0xFF);
    }
    for (int i = 0; i < 2 && pos < max - 1; i++) {
        u16 ch = le16(name3 + i * 2);
        if (ch == 0x0000 || ch == 0xFFFF) break;
        out[pos++] = (char)(ch & 0xFF);
    }
    out[pos] = 0;
}

static void lfn_prepend(char *dst, int max, const char *part) {
    int part_len = (int)strlen(part);
    int dst_len = (int)strlen(dst);
    if (part_len + dst_len >= max) part_len = max - dst_len - 1;
    if (part_len <= 0) return;
    memmove(dst + part_len, dst, (size_t)dst_len + 1);
    memcpy(dst, part, (size_t)part_len);
}

static int fat_find_entry(u32 dir_cluster, const char *name,
                          fat_dirent_t *out, u32 *out_cluster, u32 *out_offset) {
    u32 cluster = dir_cluster;
    char lfn_buf[256];
    lfn_buf[0] = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = cluster_to_lba(cluster);
        for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) return 0;
            for (u32 off = 0; off < fs.bytes_per_sector; off += 32) {
                const u8 *entry = &sector_buf[off];
                if (entry[0] == 0x00) return 0;
                if (entry[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    continue;
                }
                u8 attr = entry[11];
                if (attr == FAT32_ATTR_LFN) {
                    const fat_lfn_t *lfn = (const fat_lfn_t *)entry;
                    if (lfn->order & 0x40) lfn_buf[0] = 0;
                    char part[32];
                    lfn_extract_part(lfn, part, (int)sizeof(part));
                    lfn_prepend(lfn_buf, (int)sizeof(lfn_buf), part);
                    continue;
                }
                char name_buf[64];
                if (lfn_buf[0]) {
                    strcpy(name_buf, lfn_buf);
                } else {
                    short_to_name(entry, name_buf, (int)sizeof(name_buf));
                }
                lfn_buf[0] = 0;
                if (name_equals(name_buf, name)) {
                    if (out) memcpy(out, entry, sizeof(fat_dirent_t));
                    if (out_cluster) *out_cluster = cluster;
                    if (out_offset) *out_offset = off + s * fs.bytes_per_sector;
                    return 1;
                }
            }
        }
        cluster = fat_get_entry(cluster);
    }
    return 0;
}

static int fat_mark_deleted(u32 dir_cluster, u32 offset) {
    u32 lba = cluster_to_lba(dir_cluster);
    u32 sector = offset / fs.bytes_per_sector;
    u32 off = offset % fs.bytes_per_sector;
    if (!blk_read(lba + sector, 1, sector_buf)) return 0;
    sector_buf[off] = 0xE5;
    return blk_write(lba + sector, 1, sector_buf);
}

static int fat_delete_entry(u32 dir_cluster, const char *name, fat_dirent_t *out_ent) {
    u32 cluster = dir_cluster;
    char lfn_buf[256];
    lfn_buf[0] = 0;
    u32 lfn_offsets[20];
    int lfn_count = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        lfn_buf[0] = 0;
        lfn_count = 0;
        u32 lba = cluster_to_lba(cluster);
        for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) return 0;
            for (u32 off = 0; off < fs.bytes_per_sector; off += 32) {
                const u8 *entry = &sector_buf[off];
                if (entry[0] == 0x00) return 0;
                if (entry[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    lfn_count = 0;
                    continue;
                }
                u8 attr = entry[11];
                if (attr == FAT32_ATTR_LFN) {
                    const fat_lfn_t *lfn = (const fat_lfn_t *)entry;
                    if (lfn->order & 0x40) {
                        lfn_buf[0] = 0;
                        lfn_count = 0;
                    }
                    if (lfn_count < (int)(sizeof(lfn_offsets) / sizeof(lfn_offsets[0]))) {
                        lfn_offsets[lfn_count++] = off + s * fs.bytes_per_sector;
                    }
                    char part[32];
                    lfn_extract_part(lfn, part, (int)sizeof(part));
                    lfn_prepend(lfn_buf, (int)sizeof(lfn_buf), part);
                    continue;
                }
                char name_buf[64];
                if (lfn_buf[0]) {
                    strcpy(name_buf, lfn_buf);
                } else {
                    short_to_name(entry, name_buf, (int)sizeof(name_buf));
                }
                lfn_buf[0] = 0;
                if (name_equals(name_buf, name)) {
                    if (out_ent) memcpy(out_ent, entry, sizeof(fat_dirent_t));
                    for (int i = 0; i < lfn_count; i++) {
                        if (!fat_mark_deleted(cluster, lfn_offsets[i])) return 0;
                    }
                    if (!fat_mark_deleted(cluster, off + s * fs.bytes_per_sector)) return 0;
                    return 1;
                }
                lfn_count = 0;
            }
        }
        cluster = fat_get_entry(cluster);
    }
    return 0;
}

static int fat_dir_is_empty(u32 dir_cluster) {
    u32 cluster = dir_cluster;
    char lfn_buf[256];
    lfn_buf[0] = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = cluster_to_lba(cluster);
        for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) return 0;
            for (u32 off = 0; off < fs.bytes_per_sector; off += 32) {
                const u8 *entry = &sector_buf[off];
                if (entry[0] == 0x00) return 1;
                if (entry[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    continue;
                }
                u8 attr = entry[11];
                if (attr == FAT32_ATTR_LFN) {
                    const fat_lfn_t *lfn = (const fat_lfn_t *)entry;
                    if (lfn->order & 0x40) lfn_buf[0] = 0;
                    char part[32];
                    lfn_extract_part(lfn, part, (int)sizeof(part));
                    lfn_prepend(lfn_buf, (int)sizeof(lfn_buf), part);
                    continue;
                }
                if (attr & 0x08) {
                    lfn_buf[0] = 0;
                    continue;
                }
                char name_buf[64];
                if (lfn_buf[0]) {
                    strcpy(name_buf, lfn_buf);
                } else {
                    short_to_name(entry, name_buf, (int)sizeof(name_buf));
                }
                lfn_buf[0] = 0;
                if (strcmp(name_buf, ".") == 0 || strcmp(name_buf, "..") == 0) {
                    continue;
                }
                return 0;
            }
        }
        cluster = fat_get_entry(cluster);
    }
    return 1;
}

static int fat_read_dir(u32 dir_cluster, fs_entry_t *entries, int max_entries, int *out_count) {
    int count = 0;
    u32 cluster = dir_cluster;
    char lfn_buf[256];
    lfn_buf[0] = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = cluster_to_lba(cluster);
        for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) return 0;
            for (u32 off = 0; off < fs.bytes_per_sector; off += 32) {
                const u8 *entry = &sector_buf[off];
                if (entry[0] == 0x00) {
                    if (out_count) *out_count = count;
                    return 1;
                }
                if (entry[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    continue;
                }
                u8 attr = entry[11];
                if (attr == FAT32_ATTR_LFN) {
                    const fat_lfn_t *lfn = (const fat_lfn_t *)entry;
                    if (lfn->order & 0x40) lfn_buf[0] = 0;
                    char part[32];
                    lfn_extract_part(lfn, part, (int)sizeof(part));
                    lfn_prepend(lfn_buf, (int)sizeof(lfn_buf), part);
                    continue;
                }
                if (attr & 0x08) {
                    lfn_buf[0] = 0;
                    continue;
                }
                if (count >= max_entries) {
                    if (out_count) *out_count = count;
                    return 1;
                }
                char name_buf[64];
                if (lfn_buf[0]) {
                    strcpy(name_buf, lfn_buf);
                } else {
                    short_to_name(entry, name_buf, (int)sizeof(name_buf));
                }
                lfn_buf[0] = 0;
                fs_entry_t *e = &entries[count++];
                strcpy(e->name, name_buf);
                e->is_dir = (attr & FAT32_ATTR_DIR) ? 1 : 0;
                e->size = le32(entry + 28);
            }
        }
        cluster = fat_get_entry(cluster);
    }
    if (out_count) *out_count = count;
    return 1;
}

static u32 path_dir_cluster(const char *path, char *leaf, int leaf_cap) {
    if (!path || !path[0]) return fs.root_cluster;
    const char *p = path;
    if (*p == '/') p++;
    u32 dir = fs.root_cluster;
    char part[64];
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '/' && len < (int)sizeof(part) - 1) {
            part[len] = p[len];
            len++;
        }
        part[len] = 0;
        while (p[len] && p[len] != '/') len++;
        p += len;
        while (*p == '/') p++;
        if (*p == 0) {
            if (leaf) {
                int copy = len;
                if (copy >= leaf_cap) copy = leaf_cap - 1;
                memcpy(leaf, part, (size_t)copy);
                leaf[copy] = 0;
            }
            return dir;
        }
        fat_dirent_t ent;
        if (!fat_find_entry(dir, part, &ent, NULL, NULL)) return 0;
        if (!(ent.attr & FAT32_ATTR_DIR)) return 0;
        dir = dirent_first_cluster(&ent);
        if (dir == 0) return 0;
    }
    if (leaf) leaf[0] = 0;
    return dir;
}

static int fat_read_file(u32 dir_cluster, const char *name, u8 **out_data, u32 *out_len) {
    fat_dirent_t ent;
    if (!fat_find_entry(dir_cluster, name, &ent, NULL, NULL)) return 0;
    if (ent.attr & FAT32_ATTR_DIR) return 0;

    u32 size = ent.file_size;
    u8 *data = (u8 *)malloc(size + 1);
    if (!data) return 0;
    u32 cluster = dirent_first_cluster(&ent);
    u32 offset = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && offset < size) {
        u32 lba = cluster_to_lba(cluster);
        for (u8 s = 0; s < fs.sectors_per_cluster && offset < size; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) {
                free(data);
                return 0;
            }
            u32 to_copy = fs.bytes_per_sector;
            if (offset + to_copy > size) to_copy = size - offset;
            memcpy(data + offset, sector_buf, to_copy);
            offset += to_copy;
        }
        cluster = fat_get_entry(cluster);
    }
    data[size] = 0;
    if (out_data) *out_data = data;
    if (out_len) *out_len = size;
    return 1;
}

static u8 fat_short_checksum(const u8 short_name[11]) {
    u8 sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (u8)((sum >> 1) | (sum << 7));
        sum = (u8)(sum + short_name[i]);
    }
    return sum;
}

static int fat_write_lfn_entries(u8 *entry, const char *name, const u8 short_name[11], int order, int is_last) {
    fat_lfn_t *lfn = (fat_lfn_t *)entry;
    memset(lfn, 0xFF, sizeof(*lfn));
    lfn->order = (u8)(order | (is_last ? 0x40 : 0));
    lfn->attr = FAT32_ATTR_LFN;
    lfn->type = 0;
    lfn->checksum = fat_short_checksum(short_name);
    lfn->zero = 0;

    int start = (order - 1) * 13;
    int idx = 0;
    for (int i = 0; i < 5; i++) {
        u16 ch = 0xFFFF;
        if (name[start + idx] && start + idx < (int)strlen(name)) ch = (u8)name[start + idx];
        else if (start + idx == (int)strlen(name)) ch = 0x0000;
        lfn->name1[i] = ch;
        idx++;
    }
    for (int i = 0; i < 6; i++) {
        u16 ch = 0xFFFF;
        if (name[start + idx] && start + idx < (int)strlen(name)) ch = (u8)name[start + idx];
        else if (start + idx == (int)strlen(name)) ch = 0x0000;
        lfn->name2[i] = ch;
        idx++;
    }
    for (int i = 0; i < 2; i++) {
        u16 ch = 0xFFFF;
        if (name[start + idx] && start + idx < (int)strlen(name)) ch = (u8)name[start + idx];
        else if (start + idx == (int)strlen(name)) ch = 0x0000;
        lfn->name3[i] = ch;
        idx++;
    }
    return 1;
}

static int fat_create_entry(u32 dir_cluster, const char *name, u8 attr,
                            fat_dirent_t *out_entry, u32 *out_cluster, u32 *out_offset) {
    u8 short_name[11];
    name_to_short(name, short_name);
    int needs_lfn = 0;
    char short_buf[32];
    short_to_name(short_name, short_buf, (int)sizeof(short_buf));
    if (!name_equals(short_buf, name)) needs_lfn = 1;
    int lfn_count = needs_lfn ? ((int)strlen(name) + 12) / 13 : 0;
    int total_entries = lfn_count + 1;

    u32 cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = cluster_to_lba(cluster);
        for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) return 0;
            int free_run = 0;
            for (u32 off = 0; off < fs.bytes_per_sector; off += 32) {
                u8 first = sector_buf[off];
                if (first == 0x00 || first == 0xE5) {
                    free_run++;
                } else {
                    free_run = 0;
                }
                if (free_run >= total_entries) {
                    u32 start_off = off - (total_entries - 1) * 32;
                    u32 entry_off = start_off;
                    if (needs_lfn) {
                        for (int i = lfn_count; i >= 1; i--) {
                            fat_write_lfn_entries(&sector_buf[entry_off], name, short_name, i, i == lfn_count);
                            entry_off += 32;
                        }
                    }
                    fat_dirent_t *ent = (fat_dirent_t *)&sector_buf[entry_off];
                    memset(ent, 0, sizeof(*ent));
                    memcpy(ent->name, short_name, 11);
                    ent->attr = attr;
                    if (!blk_write(lba + s, 1, sector_buf)) return 0;
                    if (out_entry) memcpy(out_entry, ent, sizeof(*ent));
                    if (out_cluster) *out_cluster = cluster;
                    if (out_offset) *out_offset = start_off + s * fs.bytes_per_sector;
                    return 1;
                }
            }
            if (!blk_write(lba + s, 1, sector_buf)) return 0;
        }
        u32 next = fat_get_entry(cluster);
        if (next >= 0x0FFFFFF8) break;
        cluster = next;
    }
    u32 new_cluster = 0;
    if (!fat_alloc_cluster(&new_cluster)) return 0;
    fat_set_entry(cluster, new_cluster);
    fat_set_entry(new_cluster, 0x0FFFFFFF);
    u32 lba = cluster_to_lba(new_cluster);
    memset(sector_buf, 0, sizeof(sector_buf));
    for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
        if (!blk_write(lba + s, 1, sector_buf)) return 0;
    }
    return fat_create_entry(new_cluster, name, attr, out_entry, out_cluster, out_offset);
}

static int fat_update_dirent(u32 dir_cluster, u32 offset, const fat_dirent_t *ent) {
    u32 lba = cluster_to_lba(dir_cluster);
    u32 sector = offset / fs.bytes_per_sector;
    u32 off = offset % fs.bytes_per_sector;
    if (!blk_read(lba + sector, 1, sector_buf)) return 0;
    memcpy(&sector_buf[off], ent, sizeof(*ent));
    return blk_write(lba + sector, 1, sector_buf);
}

static int fat_write_file(u32 dir_cluster, const char *name, const u8 *data, u32 len, int append) {
    fat_dirent_t ent;
    u32 ent_cluster = 0;
    u32 ent_offset = 0;
    int exists = fat_find_entry(dir_cluster, name, &ent, &ent_cluster, &ent_offset);
    if (!exists) {
        if (!fat_create_entry(dir_cluster, name, 0x20, &ent, &ent_cluster, &ent_offset)) return 0;
    }
    if (ent.attr & FAT32_ATTR_DIR) return 0;

    u32 start_cluster = dirent_first_cluster(&ent);
    u32 offset = 0;
    if (append && start_cluster != 0) {
        offset = ent.file_size;
    } else if (start_cluster != 0) {
        fat_free_chain(start_cluster);
        start_cluster = 0;
    }

    u32 cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
    u32 need_size = offset + len;
    u32 needed_clusters = (need_size + cluster_bytes - 1) / cluster_bytes;

    if (needed_clusters == 0) {
        dirent_set_first_cluster(&ent, 0);
        ent.file_size = 0;
        return fat_update_dirent(ent_cluster, ent_offset, &ent);
    }

    u32 first = start_cluster;
    u32 last = 0;
    if (start_cluster == 0) {
        if (!fat_alloc_cluster(&first)) return 0;
        last = first;
    } else {
        u32 cur = start_cluster;
        while (1) {
            u32 next = fat_get_entry(cur);
            if (next >= 0x0FFFFFF8) { last = cur; break; }
            cur = next;
        }
    }

    u32 existing_clusters = 0;
    if (start_cluster != 0) {
        u32 cur = start_cluster;
        while (cur >= 2 && cur < 0x0FFFFFF8) {
            existing_clusters++;
            cur = fat_get_entry(cur);
        }
    }

    while (existing_clusters < needed_clusters) {
        u32 new_cluster = 0;
        if (!fat_alloc_cluster(&new_cluster)) return 0;
        fat_set_entry(last, new_cluster);
        last = new_cluster;
        existing_clusters++;
    }
    fat_set_entry(last, 0x0FFFFFFF);

    u32 cur = first;
    u32 pos = 0;
    while (cur >= 2 && cur < 0x0FFFFFF8 && pos < need_size) {
        u32 lba = cluster_to_lba(cur);
        for (u8 s = 0; s < fs.sectors_per_cluster && pos < need_size; s++) {
            if (!blk_read(lba + s, 1, sector_buf)) return 0;
            u32 sector_start = pos;
            u32 sector_end = pos + fs.bytes_per_sector;
            u32 write_start = offset > sector_start ? offset - sector_start : 0;
            u32 write_end = offset + len < sector_end ? offset + len - sector_start : fs.bytes_per_sector;
            if (write_end > write_start) {
                u32 write_len = write_end - write_start;
                memcpy(sector_buf + write_start, data + (sector_start + write_start - offset), write_len);
                if (!blk_write(lba + s, 1, sector_buf)) return 0;
            }
            pos += fs.bytes_per_sector;
        }
        cur = fat_get_entry(cur);
    }

    dirent_set_first_cluster(&ent, first);
    ent.file_size = offset + len;
    return fat_update_dirent(ent_cluster, ent_offset, &ent);
}

int fs_init(void) {
    memset(&fs, 0, sizeof(fs));
    if (!virtio_blk_init()) return 0;
    if (!virtio_blk_is_ready()) return 0;

    if (!blk_read(0, 1, sector_buf)) return 0;
    u32 part_lba = 0;
    if (sector_buf[510] == 0x55 && sector_buf[511] == 0xAA) {
        const u8 *pt = &sector_buf[0x1BE];
        for (int i = 0; i < 4; i++) {
            u8 type = pt[i * 16 + 4];
            if (type == 0x0B || type == 0x0C || type == 0x0E) {
                part_lba = le32(&pt[i * 16 + 8]);
                break;
            }
        }
    }
    if (!blk_read(part_lba, 1, sector_buf)) return 0;

    if (sector_buf[510] != 0x55 || sector_buf[511] != 0xAA) return 0;
    u16 bytes_per_sector = le16(&sector_buf[11]);
    u8 sectors_per_cluster = sector_buf[13];
    u16 reserved = le16(&sector_buf[14]);
    u8 fat_count = sector_buf[16];
    u32 fat_size = le32(&sector_buf[36]);
    u32 total_sectors = le32(&sector_buf[32]);
    u32 root_cluster = le32(&sector_buf[44]);

    if (bytes_per_sector != 512 || sectors_per_cluster == 0 || fat_size == 0) return 0;
    fs.bytes_per_sector = bytes_per_sector;
    fs.sectors_per_cluster = sectors_per_cluster;
    fs.reserved_sectors = reserved;
    fs.fat_count = fat_count;
    fs.fat_size = fat_size;
    fs.total_sectors = total_sectors;
    fs.root_cluster = root_cluster;
    fs.part_lba = part_lba;
    fs.fat_start_lba = part_lba + reserved;
    fs.data_start_lba = fs.fat_start_lba + fat_count * fat_size;
    fs.total_clusters = (total_sectors - reserved - fat_count * fat_size) / sectors_per_cluster;
    fs.mounted = 1;
    return 1;
}

int fs_is_ready(void) {
    return fs.mounted;
}

int fs_list_dir(const char *path, fs_entry_t *entries, int max_entries, int *out_count) {
    if (!fs.mounted) return 0;
    if (!path || path[0] == 0 || strcmp(path, "/") == 0) {
        return fat_read_dir(fs.root_cluster, entries, max_entries, out_count);
    }
    u32 dir = path_dir_cluster(path, NULL, 0);
    if (dir == 0) return 0;
    return fat_read_dir(dir, entries, max_entries, out_count);
}

int fs_read_file(const char *path, u8 **out_data, u32 *out_len) {
    if (!fs.mounted) return 0;
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    return fat_read_file(dir, name, out_data, out_len);
}

int fs_write_file(const char *path, const u8 *data, u32 len) {
    if (!fs.mounted) return 0;
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    return fat_write_file(dir, name, data, len, 0);
}

int fs_append_file(const char *path, const u8 *data, u32 len) {
    if (!fs.mounted) return 0;
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    return fat_write_file(dir, name, data, len, 1);
}

int fs_mkdir(const char *path) {
    if (!fs.mounted) return 0;
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    fat_dirent_t ent;
    u32 ent_cluster = 0;
    u32 ent_offset = 0;
    if (fat_find_entry(dir, name, &ent, &ent_cluster, &ent_offset)) return 0;
    if (!fat_create_entry(dir, name, FAT32_ATTR_DIR, &ent, &ent_cluster, &ent_offset)) return 0;
    u32 cluster = 0;
    if (!fat_alloc_cluster(&cluster)) return 0;
    dirent_set_first_cluster(&ent, cluster);
    ent.file_size = 0;
    if (!fat_update_dirent(ent_cluster, ent_offset, &ent)) return 0;

    u32 lba = cluster_to_lba(cluster);
    memset(sector_buf, 0, sizeof(sector_buf));
    fat_dirent_t *dot = (fat_dirent_t *)sector_buf;
    fat_dirent_t *dotdot = (fat_dirent_t *)(sector_buf + 32);
    memset(dot, 0, sizeof(*dot));
    memset(dotdot, 0, sizeof(*dotdot));
    memcpy(dot->name, ".          ", 11);
    dot->attr = FAT32_ATTR_DIR;
    dirent_set_first_cluster(dot, cluster);
    memcpy(dotdot->name, "..         ", 11);
    dotdot->attr = FAT32_ATTR_DIR;
    dirent_set_first_cluster(dotdot, dir == fs.root_cluster ? fs.root_cluster : dir);
    for (u8 s = 0; s < fs.sectors_per_cluster; s++) {
        if (!blk_write(lba + s, 1, sector_buf)) return 0;
    }
    return 1;
}

int fs_exists(const char *path) {
    if (!fs.mounted) return 0;
    if (!path || !path[0]) return 0;
    if (strcmp(path, "/") == 0) return 1;
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    return fat_find_entry(dir, name, NULL, NULL, NULL);
}

int fs_delete(const char *path) {
    if (!fs.mounted) return 0;
    if (!path || !path[0] || strcmp(path, "/") == 0) return 0;
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    fat_dirent_t ent;
    if (!fat_find_entry(dir, name, &ent, NULL, NULL)) return 0;
    u32 start_cluster = dirent_first_cluster(&ent);
    if (ent.attr & FAT32_ATTR_DIR) {
        if (start_cluster == 0) return 0;
        if (!fat_dir_is_empty(start_cluster)) return 0;
    }
    if (!fat_delete_entry(dir, name, NULL)) return 0;
    if (start_cluster != 0) {
        fat_free_chain(start_cluster);
    }
    return 1;
}

int fs_rename(const char *old_path, const char *new_path) {
    if (!fs.mounted) return 0;
    if (!old_path || !new_path) return 0;
    if (strcmp(old_path, new_path) == 0) return 1;
    char old_name[64];
    char new_name[64];
    u32 old_dir = path_dir_cluster(old_path, old_name, (int)sizeof(old_name));
    u32 new_dir = path_dir_cluster(new_path, new_name, (int)sizeof(new_name));
    if (old_dir == 0 || new_dir == 0 || old_name[0] == 0 || new_name[0] == 0) return 0;
    if (fat_find_entry(new_dir, new_name, NULL, NULL, NULL)) return 0;
    fat_dirent_t ent;
    if (!fat_find_entry(old_dir, old_name, &ent, NULL, NULL)) return 0;
    if ((ent.attr & FAT32_ATTR_DIR) && old_dir != new_dir) return 0;

    fat_dirent_t new_ent;
    u32 new_ent_cluster = 0;
    u32 new_ent_offset = 0;
    if (!fat_create_entry(new_dir, new_name, ent.attr, &new_ent, &new_ent_cluster, &new_ent_offset)) return 0;
    dirent_set_first_cluster(&new_ent, dirent_first_cluster(&ent));
    new_ent.file_size = ent.file_size;
    if (!fat_update_dirent(new_ent_cluster, new_ent_offset, &new_ent)) return 0;

    if (!fat_delete_entry(old_dir, old_name, NULL)) return 0;
    return 1;
}

int fs_copy(const char *src_path, const char *dst_path) {
    if (!fs.mounted) return 0;
    if (!src_path || !dst_path) return 0;
    char name[64];
    u32 dir = path_dir_cluster(src_path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    fat_dirent_t ent;
    if (!fat_find_entry(dir, name, &ent, NULL, NULL)) return 0;
    if (ent.attr & FAT32_ATTR_DIR) return 0;

    u8 *data = 0;
    u32 len = 0;
    if (!fs_read_file(src_path, &data, &len)) return 0;
    int ok = fs_write_file(dst_path, data, len);
    free(data);
    return ok;
}

int fs_move(const char *src_path, const char *dst_path) {
    if (!fs.mounted) return 0;
    if (fs_rename(src_path, dst_path)) return 1;
    if (!fs_copy(src_path, dst_path)) return 0;
    return fs_delete(src_path);
}

int fs_stat(const char *path, fs_entry_t *out) {
    if (!fs.mounted) return 0;
    if (!path || !path[0]) return 0;
    if (strcmp(path, "/") == 0) {
        if (out) {
            strcpy(out->name, "/");
            out->is_dir = 1;
            out->size = 0;
        }
        return 1;
    }
    char name[64];
    u32 dir = path_dir_cluster(path, name, (int)sizeof(name));
    if (dir == 0 || name[0] == 0) return 0;
    fat_dirent_t ent;
    if (!fat_find_entry(dir, name, &ent, NULL, NULL)) return 0;
    if (out) {
        strcpy(out->name, name);
        out->is_dir = (ent.attr & FAT32_ATTR_DIR) ? 1 : 0;
        out->size = ent.file_size;
    }
    return 1;
}

void fs_sort_entries(fs_entry_t *entries, int count, fs_sort_mode_t mode, int descending) {
    if (!entries || count <= 1) return;
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            fs_entry_t *a = &entries[j];
            fs_entry_t *b = &entries[j + 1];
            int cmp = 0;
            if (a->is_dir != b->is_dir) {
                cmp = a->is_dir ? -1 : 1;
            } else if (mode == FS_SORT_SIZE) {
                if (a->size < b->size) cmp = -1;
                else if (a->size > b->size) cmp = 1;
            } else if (mode == FS_SORT_TYPE) {
                cmp = fs_extcmp(a->name, b->name);
            } else {
                cmp = fs_stricmp(a->name, b->name);
            }
            if (descending) cmp = -cmp;
            if (cmp > 0) {
                fs_entry_t temp = *a;
                *a = *b;
                *b = temp;
            }
        }
    }
}
