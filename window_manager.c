/*
 * ============================================================================
 * WINDOW MANAGER WITH Z-ORDERING & FOCUS MANAGEMENT
 * ============================================================================
 */

#include "../include/kernel.h"

#define MAX_WINDOWS 8

typedef struct {
    int id;
    int x;
    int y;
    int width;
    int height;
    const char* title;
    int z_order;
    bool active;
    bool minimized;
    bool focused;
} window_t;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int next_window_id = 1;
static int focused_window = -1;

void window_manager_initialize(void) {
    window_count = 0;
    next_window_id = 1;
    focused_window = -1;
    KLOG("Window manager initialized with z-ordering support");
}

void window_manager_create_window(int x, int y, int width, int height, const char* title) {
    if (window_count >= MAX_WINDOWS) {
        KERROR("Window manager: max windows reached");
        return;
    }

    windows[window_count].id = next_window_id++;
    windows[window_count].x = x;
    windows[window_count].y = y;
    windows[window_count].width = width;
    windows[window_count].height = height;
    windows[window_count].title = title;
    windows[window_count].z_order = window_count;
    windows[window_count].active = true;
    windows[window_count].minimized = false;
    windows[window_count].focused = (focused_window == -1);

    if (focused_window == -1) {
        focused_window = window_count;
    }

    window_count++;
    KLOG("Window created: '%s' at (%d, %d) %dx%d", title, x, y, width, height);
}

void window_manager_set_focus(int window_id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == window_id) {
            focused_window = i;
            windows[i].focused = true;
            windows[i].z_order = window_count - 1;
            KLOG("Window focus set: %s", windows[i].title);
            return;
        }
    }
}

void window_manager_raise_window(int window_id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == window_id) {
            for (int j = 0; j < window_count; j++) {
                if (windows[j].z_order > windows[i].z_order) {
                    windows[j].z_order--;
                }
            }
            windows[i].z_order = window_count - 1;
            focused_window = i;
            KLOG("Window raised: %s", windows[i].title);
            return;
        }
    }
}

void window_manager_lower_window(int window_id) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].id == window_id) {
            for (int j = 0; j < window_count; j++) {
                if (windows[j].z_order < windows[i].z_order) {
                    windows[j].z_order++;
                }
            }
            windows[i].z_order = 0;
            KLOG("Window lowered: %s", windows[i].title);
            return;
        }
    }
}

void window_manager_render(void) {
    window_t* sorted[MAX_WINDOWS];
    
    for (int i = 0; i < window_count; i++) {
        sorted[i] = &windows[i];
    }

    for (int i = 0; i < window_count - 1; i++) {
        for (int j = i + 1; j < window_count; j++) {
            if (sorted[i]->z_order > sorted[j]->z_order) {
                window_t* temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }

    for (int i = 0; i < window_count; i++) {
        window_t* w = sorted[i];
        if (!w->active || w->minimized) {
            continue;
        }

        uint8_t frame_color = w->focused ? 0x1F : 0x0F;

        vga_write_char(w->x, w->y, '+', frame_color);
        vga_write_char(w->x + w->width - 1, w->y, '+', frame_color);
        vga_write_char(w->x, w->y + w->height - 1, '+', frame_color);
        vga_write_char(w->x + w->width - 1, w->y + w->height - 1, '+', frame_color);

        for (int j = 1; j < w->width - 1; j++) {
            vga_write_char(w->x + j, w->y, '-', frame_color);
            vga_write_char(w->x + j, w->y + w->height - 1, '-', frame_color);
        }

        for (int j = 1; j < w->height - 1; j++) {
            vga_write_char(w->x, w->y + j, '|', frame_color);
            vga_write_char(w->x + w->width - 1, w->y + j, '|', frame_color);
        }

        if (w->title) {
            int tx = w->x + 2;
            for (const char* p = w->title; *p && tx < w->x + w->width - 2; p++, tx++) {
                vga_write_char(tx, w->y, *p, 0x1F);
            }
        }

        for (int row = w->y + 1; row < w->y + w->height - 1; row++) {
            for (int col = w->x + 1; col < w->x + w->width - 1; col++) {
                vga_write_char(col, row, ' ', 0x07);
            }
        }
    }
}
