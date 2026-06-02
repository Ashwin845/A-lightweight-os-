#include "../include/kernel.h"

#define MAX_FILES 64

typedef struct {
    const char* path;
    bool is_directory;
    uint32_t size;
    bool exists;
} vfs_file_t;

static bool strings_equal(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }

    return *a == *b;
}

static vfs_file_t file_table[MAX_FILES];
static int file_count = 0;

void file_ops_initialize(void) {
    file_count = 0;
    KLOG("File operations subsystem initialized");
}

int file_op_delete(const char* path) {
    if (!path) {
        return -1;
    }

    for (int i = 0; i < file_count; i++) {
        if (strings_equal(file_table[i].path, path) && file_table[i].exists) {
            file_table[i].exists = false;
            KLOG("File deleted: %s", path);
            return 0;
        }
    }

    KERROR("File not found: %s", path);
    return -1;
}

int file_op_copy(const char* src, const char* dst) {
    if (!src || !dst) {
        return -1;
    }

    for (int i = 0; i < file_count; i++) {
        if (strings_equal(file_table[i].path, src) && file_table[i].exists) {
            if (file_count >= MAX_FILES) {
                KERROR("File table full");
                return -1;
            }

            file_table[file_count].path = dst;
            file_table[file_count].is_directory = file_table[i].is_directory;
            file_table[file_count].size = file_table[i].size;
            file_table[file_count].exists = true;
            file_count++;

            KLOG("File copied: %s -> %s", src, dst);
            return 0;
        }
    }

    return -1;
}

int file_op_mkdir(const char* path) {
    if (!path || file_count >= MAX_FILES) {
        return -1;
    }

    file_table[file_count].path = path;
    file_table[file_count].is_directory = true;
    file_table[file_count].size = 0;
    file_table[file_count].exists = true;
    file_count++;

    KLOG("Directory created: %s", path);
    return 0;
}

int file_op_move(const char* src, const char* dst) {
    if (!src || !dst) {
        return -1;
    }

    for (int i = 0; i < file_count; i++) {
        if (strings_equal(file_table[i].path, src) && file_table[i].exists) {
            file_table[i].path = dst;
            KLOG("File moved: %s -> %s", src, dst);
            return 0;
        }
    }

    return -1;
}
