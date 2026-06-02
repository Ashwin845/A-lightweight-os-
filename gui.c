/*
 * ============================================================================
 * ENHANCED GUI LAYER (Modern Desktop + Floating Taskbar)
 * ============================================================================
 */

#include "../include/kernel.h"
#include <stdint.h>

#define GUI_STATUS_COLOR 0x1F
#define GUI_STATUS_TEXT  0x0F
#define GUI_PANEL_COLOR  0x2F
#define GUI_PANEL_TEXT   0x0F
#define GUI_FRAME_COLOR  0x0F
#define GUI_WINDOW_FILL  0x07

typedef struct {
    int x;
    int y;
    int width;
    int height;
    const char* title;
    bool active;
    bool minimized;
} gui_window_t;

typedef struct {
    const char* label;
    bool active;
    int window_id;
} taskbar_item_t;

#define GUI_MAX_WINDOWS 6
#define GUI_MAX_TASKBAR 8

static gui_window_t gui_windows[GUI_MAX_WINDOWS];
static int gui_window_count;

static taskbar_item_t taskbar_items[GUI_MAX_TASKBAR];
static int taskbar_count;

static int active_window = -1;
static int screen_width = 80;
static int screen_height = 24;

static void draw_text(int x, int y, const char* text, uint8_t color) {
    for (const char* p = text; *p && x < screen_width; p++, x++) {
        vga_write_char(x, y, *p, color);
    }
}

static void fill_rect(int x, int y, int width, int height, uint8_t color) {
    for (int row = y; row < y + height && row < screen_height; row++) {
        for (int col = x; col < x + width && col < screen_width; col++) {
            vga_write_char(col, row, ' ', color);
        }
    }
}

static void draw_horizontal(int x, int y, int length, char glyph, uint8_t color) {
    for (int i = 0; i < length && x + i < screen_width; i++) {
        vga_write_char(x + i, y, glyph, color);
    }
}

static void draw_vertical(int x, int y, int length, char glyph, uint8_t color) {
    for (int i = 0; i < length && y + i < screen_height; i++) {
        vga_write_char(x, y + i, glyph, color);
    }
}

static void draw_panel(int x, int y, int width, int height, const char* title) {
    if (width < 2 || height < 2 || x < 0 || y < 0) {
        return;
    }

    if (x + width > screen_width) {
        width = screen_width - x;
    }
    if (y + height > screen_height) {
        height = screen_height - y;
    }

    fill_rect(x, y, width, height, GUI_PANEL_COLOR);
    vga_write_char(x, y, '+', GUI_FRAME_COLOR);
    vga_write_char(x + width - 1, y, '+', GUI_FRAME_COLOR);
    vga_write_char(x, y + height - 1, '+', GUI_FRAME_COLOR);
    vga_write_char(x + width - 1, y + height - 1, '+', GUI_FRAME_COLOR);
    draw_horizontal(x + 1, y, width - 2, '-', GUI_FRAME_COLOR);
    draw_horizontal(x + 1, y + height - 1, width - 2, '-', GUI_FRAME_COLOR);
    draw_vertical(x, y + 1, height - 2, '|', GUI_FRAME_COLOR);
    draw_vertical(x + width - 1, y + 1, height - 2, '|', GUI_FRAME_COLOR);

    if (title && title[0]) {
        int tx = x + 2;
        for (const char* p = title; *p && tx < x + width - 2; p++, tx++) {
            vga_write_char(tx, y, *p, GUI_PANEL_TEXT);
        }
    }
}

static void draw_background(void) {
    uint8_t colors[] = {0x16, 0x15, 0x14, 0x13};
    for (int y = 1; y < screen_height - 3; y++) {
        uint8_t color = colors[y % 4];
        for (int x = 0; x < screen_width; x++) {
            vga_write_char(x, y, ' ', color);
        }
    }
}

static void draw_status_bar(void) {
    fill_rect(0, 0, screen_width, 1, GUI_STATUS_COLOR);
    draw_text(1, 0, ".C OS", GUI_STATUS_TEXT);
    draw_text(10, 0, "Online | WiFi | 89% Battery", GUI_STATUS_TEXT);
    draw_text(screen_width - 18, 0, "[N]", GUI_STATUS_TEXT);
    draw_text(screen_width - 10, 0, "[C]", GUI_STATUS_TEXT);
}

static void draw_desktop_icons(void) {
    draw_text(2, 2, "[Files]", GUI_PANEL_TEXT);
    draw_text(2, 4, "[Term ]", GUI_PANEL_TEXT);
    draw_text(2, 6, "[Sets ]", GUI_PANEL_TEXT);
    draw_text(14, 2, "[Web  ]", GUI_PANEL_TEXT);
    draw_text(14, 4, "[Edit ]", GUI_PANEL_TEXT);
}

static void draw_gui_windows(void) {
    for (int i = 0; i < gui_window_count; i++) {
        gui_window_t* window = &gui_windows[i];
        if (!window->active || window->minimized) {
            continue;
        }

        draw_panel(window->x, window->y, window->width, window->height, window->title);
    }
}

static void draw_context_menu(int x, int y) {
    int menu_width = 18;
    int menu_height = 5;
    int menu_x = x;
    int menu_y = y;

    if (menu_x + menu_width >= screen_width) {
        menu_x = screen_width - menu_width - 1;
    }
    if (menu_y + menu_height >= screen_height) {
        menu_y = screen_height - menu_height - 1;
    }

    draw_horizontal(menu_x, menu_y, menu_width, '-', GUI_FRAME_COLOR);
    draw_horizontal(menu_x, menu_y + menu_height - 1, menu_width, '-', GUI_FRAME_COLOR);
    draw_vertical(menu_x, menu_y + 1, menu_height - 2, '|', GUI_FRAME_COLOR);
    draw_vertical(menu_x + menu_width - 1, menu_y + 1, menu_height - 2, '|', GUI_FRAME_COLOR);
    vga_write_char(menu_x, menu_y, '+', GUI_FRAME_COLOR);
    vga_write_char(menu_x + menu_width - 1, menu_y, '+', GUI_FRAME_COLOR);
    vga_write_char(menu_x, menu_y + menu_height - 1, '+', GUI_FRAME_COLOR);
    vga_write_char(menu_x + menu_width - 1, menu_y + menu_height - 1, '+', GUI_FRAME_COLOR);

    const char* options[] = {"Open", "Edit", "Move", "Close"};
    for (int i = 0; i < 4; i++) {
        int text_x = menu_x + 2;
        int text_y = menu_y + 1 + i;
        const char* label = options[i];
        for (int j = 0; label[j] && text_x + j < menu_x + menu_width - 2; j++) {
            vga_write_char(text_x + j, text_y, label[j], GUI_PANEL_TEXT);
        }
    }
}

static bool point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void gui_handle_left_click(int x, int y) {
    KLOG("Left-click at (%d, %d)", x, y);

    int status_notif_x = screen_width - 18;
    int status_ctrl_x = screen_width - 10;
    int taskbar_y = screen_height - 6;

    if (point_in_rect(x, y, status_notif_x, 0, 3, 1)) {
        notification_center_toggle();
        KLOG("Notification center toggled from status bar");
        return;
    }

    if (point_in_rect(x, y, status_ctrl_x, 0, 3, 1)) {
        control_center_toggle();
        KLOG("Control center toggled from status bar");
        return;
    }

    if (point_in_rect(x, y, screen_width - 18, taskbar_y + 1, 7, 1)) {
        notification_center_toggle();
        KLOG("Notification center toggled from taskbar");
        return;
    }

    if (point_in_rect(x, y, screen_width - 10, taskbar_y + 1, 6, 1)) {
        control_center_toggle();
        KLOG("Control center toggled from taskbar");
        return;
    }

    if (point_in_rect(x, y, 6, taskbar_y + 1, 7, 1)) {
        app_launch("Files");
        KLOG("Launching Files app from taskbar");
        return;
    }
    if (point_in_rect(x, y, 14, taskbar_y + 1, 6, 1)) {
        app_launch("Terminal");
        KLOG("Launching Terminal app from taskbar");
        return;
    }
    if (point_in_rect(x, y, 21, taskbar_y + 1, 6, 1)) {
        app_launch("Settings");
        KLOG("Launching Settings app from taskbar");
        return;
    }
    if (point_in_rect(x, y, 28, taskbar_y + 1, 5, 1)) {
        app_launch("Web");
        KLOG("Launching Web app from taskbar");
        return;
    }
    if (point_in_rect(x, y, 34, taskbar_y + 1, 6, 1)) {
        app_launch("Edit");
        KLOG("Launching Edit app from taskbar");
        return;
    }

    if (point_in_rect(x, y, 2, 2, 7, 1)) {
        app_launch("Files");
        KLOG("Launching Files app from desktop icon");
        return;
    }
    if (point_in_rect(x, y, 2, 4, 7, 1)) {
        app_launch("Terminal");
        KLOG("Launching Terminal app from desktop icon");
        return;
    }
    if (point_in_rect(x, y, 2, 6, 7, 1)) {
        app_launch("Settings");
        KLOG("Launching Settings app from desktop icon");
        return;
    }
}

void gui_handle_mouse_click(int x, int y, bool left, bool right, bool middle) {
    if (left) {
        gui_handle_left_click(x, y);
    }
    if (right) {
        gui_handle_right_click(x, y);
    }
    if (middle) {
        control_center_toggle();
        KLOG("Control center toggled from middle mouse button");
    }
}

static void draw_floating_taskbar(void) {
    int taskbar_height = 4;
    int taskbar_y = screen_height - taskbar_height - 2;
    int taskbar_x = 4;
    int taskbar_width = screen_width - 8;

    draw_panel(taskbar_x, taskbar_y, taskbar_width, taskbar_height, " ");
    draw_text(taskbar_x + 2, taskbar_y + 1, "[Files] [Term] [Sets] [Web] [Edit]", GUI_PANEL_TEXT);
    draw_text(screen_width - 18, taskbar_y + 1, "[NOTIF]", GUI_PANEL_TEXT);
    draw_text(screen_width - 10, taskbar_y + 1, "[CTRL]", GUI_PANEL_TEXT);
}

void gui_render(void) {
    draw_background();
    draw_status_bar();
    draw_desktop_icons();
    draw_gui_windows();
    window_manager_render();
    draw_floating_taskbar();
    notification_center_render();
    control_center_render();
    app_launcher_render();
}

void gui_initialize(void) {
    gui_window_count = 0;
    taskbar_count = 0;
    active_window = -1;
    screen_width = 80;
    screen_height = 24;
    vga_clear_screen();
    KLOG("Enhanced GUI layer initialized");
}

void gui_open_window(const char* title, int x, int y, int width, int height) {
    if (gui_window_count >= GUI_MAX_WINDOWS) {
        KERROR("GUI: cannot allocate more windows");
        return;
    }

    gui_windows[gui_window_count].x = x;
    gui_windows[gui_window_count].y = y;
    gui_windows[gui_window_count].width = width;
    gui_windows[gui_window_count].height = height;
    gui_windows[gui_window_count].title = title;
    gui_windows[gui_window_count].active = true;
    gui_windows[gui_window_count].minimized = false;

    if (taskbar_count < GUI_MAX_TASKBAR) {
        taskbar_items[taskbar_count].label = title;
        taskbar_items[taskbar_count].active = true;
        taskbar_items[taskbar_count].window_id = gui_window_count;
        taskbar_count++;
    }

    if (active_window < 0) {
        active_window = gui_window_count;
    }

    gui_window_count++;
}

void gui_draw_icon(int x, int y, const char* label) {
    draw_text(x, y, label, GUI_PANEL_TEXT);
}

void gui_handle_right_click(int x, int y) {
    KLOG("Right-click at (%d, %d)", x, y);

    int status_notif_x = screen_width - 18;
    int status_ctrl_x = screen_width - 10;
    int taskbar_y = screen_height - 6;

    if (y == 0 && x >= status_ctrl_x && x < status_ctrl_x + 3) {
        control_center_toggle();
        KLOG("Control center toggled from status bar");
        return;
    }

    if (y == 0 && x >= status_notif_x && x < status_notif_x + 3) {
        notification_center_toggle();
        KLOG("Notification center toggled from status bar");
        return;
    }

    if (x >= 2 && x <= 8 && y == 2) {
        app_launch("Files");
        KLOG("Launching Files app from desktop icon");
        return;
    } else if (x >= 2 && x <= 8 && y == 4) {
        app_launch("Terminal");
        KLOG("Launching Terminal app from desktop icon");
        return;
    } else if (x >= 2 && x <= 8 && y == 6) {
        app_launch("Settings");
        KLOG("Launching Settings app from desktop icon");
        return;
    }

    if (x >= screen_width - 18 && x < screen_width - 11 && y == taskbar_y + 1) {
        notification_center_toggle();
        KLOG("Notification center toggled from taskbar");
        return;
    }

    if (x >= screen_width - 10 && x < screen_width - 4 && y == taskbar_y + 1) {
        control_center_toggle();
        KLOG("Control center toggled from taskbar");
        return;
    }

    draw_context_menu(x, y);
}

void gui_set_resolution(int width, int height) {
    if (width > 0 && height > 0) {
        screen_width = width;
        screen_height = height;
        KLOG("Display resolution set to %dx%d", width, height);
    }
}
