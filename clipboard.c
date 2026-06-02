#include "../include/kernel.h"

#define CLIPBOARD_SIZE 512

static char clipboard_buffer[CLIPBOARD_SIZE];
static size_t clipboard_used = 0;

void clipboard_initialize(void) {
    clipboard_used = 0;
    KLOG("Clipboard system initialized");
}

void clipboard_copy(const char* data, size_t size) {
    if (!data || size == 0 || size > CLIPBOARD_SIZE) {
        KERROR("Clipboard: invalid copy operation");
        return;
    }

    for (size_t i = 0; i < size; i++) {
        clipboard_buffer[i] = data[i];
    }
    clipboard_used = size;
    KLOG("Clipboard: copied %zu bytes", size);
}

const char* clipboard_paste(void) {
    if (clipboard_used == 0) {
        return NULL;
    }
    return clipboard_buffer;
}

void clipboard_clear(void) {
    clipboard_used = 0;
    KLOG("Clipboard cleared");
}
