#include "../include/kernel.h"

#define CONTROL_CENTER_X 50
#define CONTROL_CENTER_Y 2
#define CONTROL_CENTER_W 28
#define CONTROL_CENTER_H 14

static bool control_center_visible = false;

void control_center_initialize(void) {
    control_center_visible = false;
    KLOG("Control center initialized");
}

void control_center_toggle(void) {
    control_center_visible = !control_center_visible;
}

void control_center_render(void) {
    if (!control_center_visible) {
        return;
    }

    int x = CONTROL_CENTER_X;
    int y = CONTROL_CENTER_Y;
    int w = CONTROL_CENTER_W;
    int h = CONTROL_CENTER_H;

    vga_write_char(x, y, '+', 0x0F);
    vga_write_char(x + w - 1, y, '+', 0x0F);
    vga_write_char(x, y + h - 1, '+', 0x0F);
    vga_write_char(x + w - 1, y + h - 1, '+', 0x0F);

    for (int i = 1; i < w - 1; i++) {
        vga_write_char(x + i, y, '-', 0x0F);
        vga_write_char(x + i, y + h - 1, '-', 0x0F);
    }

    for (int i = 1; i < h - 1; i++) {
        vga_write_char(x, y + i, '|', 0x0F);
        vga_write_char(x + w - 1, y + i, '|', 0x0F);
    }

    const char* title = "Control Center";
    int tx = x + 2;
    for (const char* p = title; *p && tx < x + w - 2; p++, tx++) {
        vga_write_char(tx, y, *p, 0x1F);
    }

    int cy = y + 2;
    const char* items[] = {
        "WiFi:     [ON]",
        "Bluetooth:[OFF]",
        "Airplane: [OFF]",
        "Brightness: 80%",
        "Volume:   65%",
        "Battery:  85%"
    };

    for (int i = 0; i < 6 && cy < y + h - 1; i++, cy++) {
        int ix = x + 2;
        for (const char* p = items[i]; *p && ix < x + w - 1; p++, ix++) {
            vga_write_char(ix, cy, *p, 0x0F);
        }
    }
}
