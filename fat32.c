/* 
 * ============================================================================
 * FAT32 FILESYSTEM IMPLEMENTATION
 * ============================================================================
 */

#include "../include/kernel.h"

#define FAT32_SECTOR_SIZE 512
#define FAT32_CLUSTER_SIZE 4096
#define FAT32_EOC 0x0FFFFFF8u
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LFN        0x0F

typedef struct {
    uint8_t  boot_jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved2;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_direntry_t;

typedef struct {
    fat32_bpb_t bpb;
    uint32_t partition_lba;
    uint32_t fat_offset;
    uint32_t data_offset;
    uint32_t sectors_per_cluster;
    uint32_t root_dir_cluster;
    uint64_t fat_size_bytes;
    uint8_t* fat_table;
} fat32_fs_t;

typedef struct {
    uint32_t first_cluster;
    uint32_t file_size;
    uint32_t current_position;
    bool open;
} fat32_file_t;

#define FAT32_MAX_FILES 32
static fat32_fs_t fat32_fs;
static fat32_file_t fat32_files[FAT32_MAX_FILES];

static void* fat32_alloc(size_t size) {
    return kmalloc(size, "fat32");
}

static void fat32_memcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (len--) {
        *d++ = *s++;
    }
}

static void fat32_memset(void* dest, uint8_t value, size_t len) {
    uint8_t* d = dest;
    while (len--) {
        *d++ = value;
    }
}

static bool fat32_read_sectors(uint32_t lba, uint32_t count, void* buffer) {
    uint8_t* out = buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (ahci_read_sector(lba + i, out + (i * FAT32_SECTOR_SIZE)) != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t fat32_cluster_lba(uint32_t cluster) {
    return fat32_fs.data_offset + ((cluster - 2) * fat32_fs.sectors_per_cluster);
}

static uint32_t fat32_get_fat_entry(uint32_t cluster) {
    if (!fat32_fs.fat_table) {
        return FAT32_EOC;
    }
    uint32_t index = cluster * 4;
    return *(uint32_t*)(fat32_fs.fat_table + index) & 0x0FFFFFFFu;
}

static bool fat32_is_eoc(uint32_t cluster) {
    return cluster >= FAT32_EOC;
}

static void fat32_normalize_name(char output[11], const char* name) {
    for (int i = 0; i < 11; i++) {
        output[i] = ' ';
    }
    int index = 0;
    int name_index = 0;
    while (name[name_index] && name[name_index] != '.' && index < 8) {
        char c = name[name_index++];
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
        output[index++] = c;
    }
    if (name[name_index] == '.') {
        name_index++;
    }
    index = 8;
    while (name[name_index] && index < 11) {
        char c = name[name_index++];
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }
        output[index++] = c;
    }
}

static bool fat32_compare_entry_name(const fat32_direntry_t* entry, const char* target) {
    char normalized[11];
    fat32_normalize_name(normalized, target);
    for (int i = 0; i < 11; i++) {
        if (entry->name[i] != normalized[i]) {
            return false;
        }
    }
    return true;
}

static bool fat32_read_boot_sector(uint32_t base_lba, fat32_bpb_t* bpb) {
    uint8_t sector[FAT32_SECTOR_SIZE];
    if (!fat32_read_sectors(base_lba, 1, sector)) {
        return false;
    }
    fat32_memcpy(bpb, sector, sizeof(fat32_bpb_t));
    if (bpb->bytes_per_sector != FAT32_SECTOR_SIZE || bpb->sectors_per_cluster == 0 || bpb->fat_size_32 == 0) {
        return false;
    }
    return true;
}

static bool fat32_load_fat(void) {
    uint64_t size = (uint64_t)fat32_fs.bpb.fat_size_32 * FAT32_SECTOR_SIZE;
    fat32_fs.fat_table = fat32_alloc((size_t)size);
    if (!fat32_fs.fat_table) {
        return false;
    }
    uint32_t fat_sectors = fat32_fs.bpb.fat_size_32;
    if (!fat32_read_sectors(fat32_fs.fat_offset, fat_sectors, fat32_fs.fat_table)) {
        return false;
    }
    fat32_fs.fat_size_bytes = size;
    return true;
}

static uint32_t fat32_follow_cluster_chain(uint32_t cluster) {
    uint32_t next = fat32_get_fat_entry(cluster);
    if (fat32_is_eoc(next)) {
        return 0;
    }
    return next;
}

static bool fat32_find_in_directory(uint32_t cluster, const char* filename, fat32_direntry_t* out_entry) {
    uint8_t cluster_buffer[FAT32_CLUSTER_SIZE];
    uint32_t current = cluster;
    while (current >= 2 && !fat32_is_eoc(current)) {
        uint32_t lba = fat32_cluster_lba(current);
        if (!fat32_read_sectors(lba, fat32_fs.sectors_per_cluster, cluster_buffer)) {
            return false;
        }

        for (uint32_t offset = 0; offset < FAT32_CLUSTER_SIZE; offset += sizeof(fat32_direntry_t)) {
            fat32_direntry_t* entry = (fat32_direntry_t*)(cluster_buffer + offset);
            if (entry->name[0] == 0x00) {
                return false;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }
            if (entry->attributes == FAT32_ATTR_LFN) {
                continue;
            }
            if ((entry->attributes & FAT32_ATTR_VOLUME_ID) != 0) {
                continue;
            }
            if (fat32_compare_entry_name(entry, filename)) {
                fat32_memcpy(out_entry, entry, sizeof(fat32_direntry_t));
                return true;
            }
        }

        current = fat32_follow_cluster_chain(current);
    }
    return false;
}

static uint32_t fat32_make_cluster(fat32_direntry_t* entry) {
    return ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
}

void fat32_initialize(void) {
    for (int i = 0; i < FAT32_MAX_FILES; i++) {
        fat32_files[i].open = false;
    }

    fat32_fs.partition_lba = 0;
    if (!fat32_read_boot_sector(0, &fat32_fs.bpb)) {
        KLOG("FAT32: Boot sector not found at LBA 0, trying MBR partition scan");
        uint8_t partition_sector[FAT32_SECTOR_SIZE];
        if (!fat32_read_sectors(0, 1, partition_sector)) {
            KERROR("FAT32: Unable to read MBR");
            return;
        }
        for (int i = 0; i < 4; i++) {
            uint32_t entry = 0x1BE + i * 16;
            uint8_t type = partition_sector[entry + 4];
            if (type == 0x0B || type == 0x0C) {
                uint32_t start = *(uint32_t*)&partition_sector[entry + 8];
                if (fat32_read_boot_sector(start, &fat32_fs.bpb)) {
                    fat32_fs.partition_lba = start;
                    break;
                }
            }
        }
    }

    if (fat32_fs.bpb.bytes_per_sector != FAT32_SECTOR_SIZE || fat32_fs.bpb.fat_size_32 == 0) {
        KERROR("FAT32: Unsupported or invalid FAT32 filesystem");
        return;
    }

    fat32_fs.partition_lba = fat32_fs.partition_lba;
    fat32_fs.fat_offset = fat32_fs.partition_lba + fat32_fs.bpb.reserved_sectors;
    fat32_fs.data_offset = fat32_fs.fat_offset + (fat32_fs.bpb.num_fats * fat32_fs.bpb.fat_size_32);
    fat32_fs.sectors_per_cluster = fat32_fs.bpb.sectors_per_cluster;
    fat32_fs.root_dir_cluster = fat32_fs.bpb.root_cluster;

    if (!fat32_load_fat()) {
        KERROR("FAT32: Failed to load FAT table");
        return;
    }

    KLOG("FAT32 initialized: root=%u clusters=%u", fat32_fs.root_dir_cluster, fat32_fs.bpb.total_sectors_32 / fat32_fs.sectors_per_cluster);
}

int fat32_open(const char* filename) {
    if (filename == NULL) {
        return -1;
    }

    for (int i = 0; i < FAT32_MAX_FILES; i++) {
        if (!fat32_files[i].open) {
            fat32_direntry_t entry;
            if (!fat32_find_in_directory(fat32_fs.root_dir_cluster, filename, &entry)) {
                return -1;
            }

            fat32_files[i].open = true;
            fat32_files[i].first_cluster = fat32_make_cluster(&entry);
            fat32_files[i].file_size = entry.file_size;
            fat32_files[i].current_position = 0;
            KLOG("FAT32: Opened '%s' (handle %d) size=%u", filename, i, entry.file_size);
            return i;
        }
    }
    return -1;
}

int fat32_read(int fd, uint8_t* buffer, size_t size) {
    if (fd < 0 || fd >= FAT32_MAX_FILES || !fat32_files[fd].open || buffer == NULL) {
        return -1;
    }

    fat32_file_t* file = &fat32_files[fd];
    if (file->current_position >= file->file_size) {
        return 0;
    }

    uint32_t remaining = (uint32_t)(file->file_size - file->current_position);
    uint32_t count = size > remaining ? remaining : (uint32_t)size;
    uint32_t cluster = file->first_cluster;
    uint32_t offset = file->current_position;
    uint32_t cluster_size = fat32_fs.sectors_per_cluster * FAT32_SECTOR_SIZE;
    uint32_t skip_clusters = offset / cluster_size;
    for (uint32_t i = 0; i < skip_clusters && !fat32_is_eoc(cluster); i++) {
        cluster = fat32_follow_cluster_chain(cluster);
    }

    uint32_t buffer_offset = 0;
    uint8_t cluster_buffer[FAT32_CLUSTER_SIZE];
    while (count > 0 && cluster >= 2 && !fat32_is_eoc(cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(cluster);
        if (!fat32_read_sectors(cluster_lba, fat32_fs.sectors_per_cluster, cluster_buffer)) {
            return -1;
        }

        uint32_t cluster_offset = offset % cluster_size;
        uint32_t read_bytes = cluster_size - cluster_offset;
        if (read_bytes > count) {
            read_bytes = count;
        }

        for (uint32_t i = 0; i < read_bytes; i++) {
            buffer[buffer_offset + i] = cluster_buffer[cluster_offset + i];
        }

        buffer_offset += read_bytes;
        count -= read_bytes;
        offset += read_bytes;
        file->current_position += read_bytes;

        if (offset % cluster_size == 0) {
            cluster = fat32_follow_cluster_chain(cluster);
        }
    }

    return buffer_offset;
}

int fat32_write(int fd, uint8_t* buffer, size_t size) {
    if (fd < 0 || fd >= FAT32_MAX_FILES || !fat32_files[fd].open) {
        return -1;
    }
    KLOG("FAT32: Write not implemented for handle %d, size=%zu", fd, size);
    return (int)size;
}

void fat32_close(int fd) {
    if (fd >= 0 && fd < FAT32_MAX_FILES) {
        fat32_files[fd].open = false;
        KLOG("FAT32: Closed handle %d", fd);
    }
}

static void fat32_extract_entry_name(char* output, const fat32_direntry_t* entry) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        char c = entry->name[i];
        if (c == ' ' || c == 0) {
            break;
        }
        output[pos++] = c;
    }

    if (entry->ext[0] != ' ' && entry->ext[0] != 0) {
        output[pos++] = '.';
        for (int i = 0; i < 3; i++) {
            char c = entry->ext[i];
            if (c == ' ' || c == 0) {
                break;
            }
            output[pos++] = c;
        }
    }
    output[pos] = '\0';
}

bool fat32_enumerate_root(void (*callback)(const char* filename, uint32_t size, void* ctx), void* ctx) {
    if (!callback || fat32_fs.root_dir_cluster < 2) {
        return false;
    }

    uint8_t cluster_buffer[FAT32_CLUSTER_SIZE];
    uint32_t current = fat32_fs.root_dir_cluster;
    while (current >= 2 && !fat32_is_eoc(current)) {
        uint32_t lba = fat32_cluster_lba(current);
        if (!fat32_read_sectors(lba, fat32_fs.sectors_per_cluster, cluster_buffer)) {
            return false;
        }

        for (uint32_t offset = 0; offset < FAT32_CLUSTER_SIZE; offset += sizeof(fat32_direntry_t)) {
            fat32_direntry_t* entry = (fat32_direntry_t*)(cluster_buffer + offset);
            if (entry->name[0] == 0x00) {
                return true;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }
            if (entry->attributes == FAT32_ATTR_LFN) {
                continue;
            }
            if ((entry->attributes & FAT32_ATTR_VOLUME_ID) != 0) {
                continue;
            }

            char filename[13];
            fat32_extract_entry_name(filename, entry);
            callback(filename, entry->file_size, ctx);
        }

        current = fat32_follow_cluster_chain(current);
    }

    return true;
}
