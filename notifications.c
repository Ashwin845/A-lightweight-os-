#include "../include/kernel.h"
#include <stdint.h>

#define MAX_NOTIFICATIONS 16
#define NOTIFICATION_WIDTH 30
#define NOTIFICATION_HEIGHT 4

typedef struct {
    const char* title;
    const char* message;
    int priority;
    uint32_t timestamp;
    bool active;
} notification_t;

static notification_t notifications[MAX_NOTIFICATIONS];
static int notification_count = 0;
static int display_y = 2;
static bool notification_center_visible = false;

void notification_center_initialize(void) {
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        notifications[i].active = false;
    }
    notification_count = 0;
    notification_center_visible = false;
    KLOG("Notification center initialized");
}

void notification_center_toggle(void) {
    notification_center_visible = !notification_center_visible;
    KLOG("Notification center %s", notification_center_visible ? "shown" : "hidden");
}

void notification_post(const char* title, const char* message, int priority) {
    if (notification_count >= MAX_NOTIFICATIONS) {
        return;
    }

    notifications[notification_count].title = title;
    notifications[notification_count].message = message;
    notifications[notification_count].priority = priority;
    notifications[notification_count].timestamp = 0;
    notifications[notification_count].active = true;

    notification_count++;
    KLOG("Notification posted: %s", title);
}

void notification_center_render(void) {
    if (!notification_center_visible) {
        return;
    }

    int render_y = display_y;

    for (int i = 0; i < notification_count && render_y < 22; i++) {
        if (!notifications[i].active) {
            continue;
        }

        uint8_t color = (notifications[i].priority > 1) ? 0x0C : 0x0F;

        vga_write_char(75, render_y, '+', color);
        for (int j = 76; j < 78; j++) {
            vga_write_char(j, render_y, '-', color);
        }
        vga_write_char(78, render_y, '+', color);

        int tx = 76;
        for (const char* p = notifications[i].title; *p && tx < 78; p++, tx++) {
            vga_write_char(tx, render_y, *p, color);
        }

        vga_write_char(75, render_y + 1, '|', color);
        tx = 76;
        for (const char* p = notifications[i].message; *p && tx < 78; p++, tx++) {
            vga_write_char(tx, render_y + 1, *p, 0x07);
        }
        vga_write_char(78, render_y + 1, '|', color);

        vga_write_char(75, render_y + 2, '+', color);
        vga_write_char(76, render_y + 2, '-', color);
        vga_write_char(77, render_y + 2, '-', color);
        vga_write_char(78, render_y + 2, '+', color);

        render_y += 4;
    }
}
