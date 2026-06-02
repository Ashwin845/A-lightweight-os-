#include "../include/kernel.h"

#define MAX_APPS 16

typedef struct {
    const char* name;
    void (*entry)(void);
    bool running;
    uint32_t pid;
} app_t;

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

static app_t app_registry[MAX_APPS];
static int app_count = 0;

void dummy_app(void) {
    KLOG("Dummy app running");
}

void file_manager_app(void) {
    KLOG("File Manager started");
}

void terminal_app(void) {
    KLOG("Terminal started");
}

void settings_app(void) {
    KLOG("Settings started");
}

void app_launcher_initialize(void) {
    app_count = 0;

    app_registry[app_count].name = "Files";
    app_registry[app_count].entry = file_manager_app;
    app_registry[app_count].running = false;
    app_registry[app_count].pid = 0;
    app_count++;

    app_registry[app_count].name = "Terminal";
    app_registry[app_count].entry = terminal_app;
    app_registry[app_count].running = false;
    app_registry[app_count].pid = 0;
    app_count++;

    app_registry[app_count].name = "Settings";
    app_registry[app_count].entry = settings_app;
    app_registry[app_count].running = false;
    app_registry[app_count].pid = 0;
    app_count++;

    KLOG("Application launcher initialized with %d apps", app_count);
}

int app_launch(const char* app_name) {
    if (!app_name) {
        return -1;
    }

    for (int i = 0; i < app_count; i++) {
        if (strings_equal(app_registry[i].name, app_name)) {
            if (app_registry[i].running) {
                KLOG("App already running: %s", app_name);
                return app_registry[i].pid;
            }

            tcb_t* task = create_task(app_name, app_registry[i].entry, 1);
            if (task) {
                app_registry[i].running = true;
                app_registry[i].pid = task->pid;
                KLOG("Application launched: %s (PID %u)", app_name, task->pid);
                return task->pid;
            }
        }
    }

    KERROR("Application not found: %s", app_name);
    return -1;
}

static void draw_box(int x, int y, int width, int height) {
    vga_write_char(x, y, '+', 0x0F);
    vga_write_char(x + width - 1, y, '+', 0x0F);
    vga_write_char(x, y + height - 1, '+', 0x0F);
    vga_write_char(x + width - 1, y + height - 1, '+', 0x0F);

    for (int i = 1; i < width - 1; i++) {
        vga_write_char(x + i, y, '-', 0x0F);
        vga_write_char(x + i, y + height - 1, '-', 0x0F);
    }

    for (int i = 1; i < height - 1; i++) {
        vga_write_char(x, y + i, '|', 0x0F);
        vga_write_char(x + width - 1, y + i, '|', 0x0F);
    }
}

void app_launcher_render(void) {
    int x = 4;
    int y = 16;
    int width = 28;
    int height = 8;

    draw_box(x, y, width, height);
    const char* title = "App Launcher";
    int tx = x + 2;
    for (const char* p = title; *p && tx < x + width - 2; p++, tx++) {
        vga_write_char(tx, y, *p, 0x1F);
    }

    for (int i = 0; i < app_count && i < height - 2; i++) {
        const char* name = app_registry[i].name;
        int line_x = x + 2;
        int line_y = y + 1 + i;
        for (int j = 0; name[j] && line_x + j < x + width - 2; j++) {
            vga_write_char(line_x + j, line_y, name[j], 0x0F);
        }
    }
}
