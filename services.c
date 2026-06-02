#include "../include/kernel.h"
#include <stdint.h>
#include <stdbool.h>

#define BACKGROUND_SERVICE_COUNT 200
#define SERVICE_NAME_BUFFER 32
#define PRINT_QUEUE_SLOTS 8
#define PRINT_MESSAGE_SIZE 128
#define AUDIO_QUEUE_SLOTS 8

typedef void (*service_entry_t)(void);

static char service_names[BACKGROUND_SERVICE_COUNT][SERVICE_NAME_BUFFER];
static uint32_t service_id_map[BACKGROUND_SERVICE_COUNT];

static bool disk_index_ready = false;
static uint32_t disk_indexed_files = 0;
static char clipboard_snapshot[64] = {0};

static char print_queue[PRINT_QUEUE_SLOTS][PRINT_MESSAGE_SIZE];
static uint8_t print_head = 0;
static uint8_t print_tail = 0;
static uint8_t print_offset = 0;
static bool print_queue_full = false;

static struct {
    uint32_t frequency;
    uint32_t duration_ms;
    bool valid;
} audio_queue[AUDIO_QUEUE_SLOTS];
static uint8_t audio_head = 0;
static uint8_t audio_tail = 0;
static bool audio_queue_full = false;

static void disk_index_callback(const char* filename, uint32_t size, void* ctx) {
    uint32_t* count = (uint32_t*)ctx;
    if (count && filename) {
        (*count)++;
    }
}

static void disk_indexer_service(void) {
    while (1) {
        if (!disk_index_ready) {
            uint32_t count = 0;
            if (fat32_enumerate_root(disk_index_callback, &count)) {
                disk_index_ready = true;
                disk_indexed_files = count;
                notification_post("Disk/File Indexer", "FAT32 root directory indexed", 0);
            } else {
                notification_post("Disk/File Indexer", "Indexing failed", 1);
            }
        }

        for (int i = 0; i < 64; i++) {
            asm volatile("nop");
        }
        task_yield();
    }
}

static void input_monitor_service(void) {
    bool had_keyboard = false;
    bool had_mouse = false;

    while (1) {
        usb_poll();

        bool has_keyboard = usb_has_keyboard();
        bool has_mouse = usb_has_mouse();

        if (has_keyboard && !had_keyboard) {
            notification_post("Input Monitor", "USB keyboard detected", 0);
        }
        if (has_mouse && !had_mouse) {
            notification_post("Input Monitor", "USB mouse detected", 0);
        }
        had_keyboard = has_keyboard;
        had_mouse = has_mouse;

        for (int i = 0; i < 32; i++) {
            asm volatile("nop");
        }
        task_yield();
    }
}

static void clipboard_daemon_service(void) {
    while (1) {
        const char* data = clipboard_paste();
        if (data) {
            uint32_t changed = 0;
            for (uint32_t i = 0; i < sizeof(clipboard_snapshot) - 1; i++) {
                char c = data[i];
                if (clipboard_snapshot[i] != c) {
                    changed = 1;
                }
                clipboard_snapshot[i] = c;
                if (!c) {
                    break;
                }
            }
            if (changed) {
                notification_post("Clipboard Daemon", "Clipboard contents updated", 0);
            }
        }

        for (int i = 0; i < 64; i++) {
            asm volatile("nop");
        }
        task_yield();
    }
}

static void audio_spooler_service(void) {
    while (1) {
        if (audio_queue[audio_head].valid) {
            uint32_t freq = audio_queue[audio_head].frequency;
            uint32_t duration = audio_queue[audio_head].duration_ms;
            audio_queue[audio_head].valid = false;
            audio_head = (audio_head + 1) % AUDIO_QUEUE_SLOTS;
            audio_queue_full = false;
            audio_play_tone(freq, duration);
        }

        if (!print_queue_full && print_offset == 0 && print_head != print_tail) {
            // keep the task alive while waiting for a new queue entry
        }

        if (print_head != print_tail || print_offset != 0 || print_queue_full) {
            char c = print_queue[print_head][print_offset];
            if (c != 0) {
                serial_putchar(c);
                print_offset++;
            } else {
                print_queue[print_head][0] = 0;
                print_head = (print_head + 1) % PRINT_QUEUE_SLOTS;
                print_offset = 0;
                print_queue_full = false;
            }
        }

        for (int i = 0; i < 16; i++) {
            asm volatile("nop");
        }
        task_yield();
    }
}

static void generic_background_service(void) {
    tcb_t* current = get_current_task();
    uint32_t service_id = 0;
    if (current) {
        service_id = current->memory_kb;
    }

    while (1) {
        for (int i = 0; i < 8; i++) {
            asm volatile("nop");
        }
        task_yield();
        (void)service_id;
    }
}

void background_services_initialize(void) {
    static const struct {
        const char* name;
        service_entry_t entry;
    } service_table[] = {
        {"Disk/File Indexer", disk_indexer_service},
        {"Input Monitor / Polling", input_monitor_service},
        {"Clipboard Daemon", clipboard_daemon_service},
        {"Print / Audio Spooler", audio_spooler_service},
    };

    for (uint32_t i = 0; i < BACKGROUND_SERVICE_COUNT; i++) {
        char* name = service_names[i];
        service_entry_t entry = generic_background_service;
        if (i < (uint32_t)(sizeof(service_table) / sizeof(service_table[0]))) {
            entry = service_table[i].entry;
            const char* base = service_table[i].name;
            int index = 0;
            while (*base && index < SERVICE_NAME_BUFFER - 1) {
                name[index++] = *base++;
            }
            name[index] = '\0';
        } else {
            int index = 0;
            const char* base = "BackgroundService";
            while (*base && index < SERVICE_NAME_BUFFER - 1) {
                name[index++] = *base++;
            }
            uint32_t value = i + 1;
            if (index < SERVICE_NAME_BUFFER - 4) {
                name[index++] = '0' + ((value / 100) % 10);
                name[index++] = '0' + ((value / 10) % 10);
                name[index++] = '0' + (value % 10);
            }
            name[index] = '\0';
        }

        tcb_t* task = create_task(name, entry, 1);
        if (task) {
            task->memory_kb = i;
            service_id_map[i] = i;
        }
    }

    spool_print("[Spooler] System print queue initialized\n");
    spool_audio_tone(440, 40);
}

void spool_print(const char* text) {
    if (!text) {
        return;
    }

    uint8_t next = (print_tail + 1) % PRINT_QUEUE_SLOTS;
    if (next == print_head && print_queue_full) {
        return;
    }

    uint32_t index = 0;
    while (*text && index < PRINT_MESSAGE_SIZE - 1) {
        print_queue[print_tail][index++] = *text++;
    }
    print_queue[print_tail][index] = 0;
    print_tail = next;
    if (print_tail == print_head) {
        print_queue_full = true;
    }
}

void spool_audio_tone(uint32_t frequency, uint32_t duration_ms) {
    uint8_t next = (audio_tail + 1) % AUDIO_QUEUE_SLOTS;
    if (next == audio_head && audio_queue_full) {
        return;
    }

    audio_queue[audio_tail].frequency = frequency;
    audio_queue[audio_tail].duration_ms = duration_ms;
    audio_queue[audio_tail].valid = true;
    audio_tail = next;
    if (audio_tail == audio_head) {
        audio_queue_full = true;
    }
}
