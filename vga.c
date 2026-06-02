/* 
 * ============================================================================
 * VGA & FRAMEBUFFER GRAPHICS DRIVER
 * ============================================================================
 */

#include "../include/kernel.h"

/* ============================================================================
 * VGA TEXT MODE
 * ============================================================================ */

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_VRAM ((uint16_t*)0xB8000)

/* VGA Color codes */
enum vga_color {
    VGA_BLACK = 0,
    VGA_BLUE = 1,
    VGA_GREEN = 2,
    VGA_CYAN = 3,
    VGA_RED = 4,
    VGA_MAGENTA = 5,
    VGA_BROWN = 6,
    VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8,
    VGA_LIGHT_BLUE = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW = 14,
    VGA_WHITE = 15,
};

static uint8_t vga_current_color = (VGA_WHITE << 4) | VGA_BLACK;
static int vga_cursor_x = 0;
static int vga_cursor_y = 0;

/* ============================================================================
 * VGA IMPLEMENTATION
 * ============================================================================ */

void vga_initialize(void) {
    vga_current_color = (VGA_BLACK << 4) | VGA_LIGHT_GREY;
    vga_cursor_x = 0;
    vga_cursor_y = 0;
    KLOG("VGA display initialized (%dx%d)", VGA_WIDTH, VGA_HEIGHT);
}

void vga_clear_screen(void) {
    uint16_t blank = (' ' | (vga_current_color << 8));
    
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_VRAM[y * VGA_WIDTH + x] = blank;
        }
    }
    
    vga_cursor_x = 0;
    vga_cursor_y = 0;
}

void vga_write_char(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) {
        return;
    }
    
    int index = y * VGA_WIDTH + x;
    VGA_VRAM[index] = (c | (color << 8));
}

static void vga_scroll_down(void) {
    uint16_t blank = (' ' | (vga_current_color << 8));
    
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_VRAM[y * VGA_WIDTH + x] = VGA_VRAM[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    for (int x = 0; x < VGA_WIDTH; x++) {
        VGA_VRAM[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
}

/* ============================================================================
 * LINEAR FRAMEBUFFER GRAPHICS (VESA)
 * ============================================================================ */

typedef struct {
    uint32_t* vram;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
} framebuffer_t;

static framebuffer_t fb;
static uint32_t backbuffer[1024 * 768];

void vga_initialize_framebuffer(uint32_t* vram, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) {
    fb.vram = vram;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = bpp;
    
    KLOG("Framebuffer initialized: %ux%u, %u bpp", width, height, bpp);
}

void fb_draw_pixel(int x, int y, uint32_t rgb) {
    if (x < 0 || x >= (int)fb.width || y < 0 || y >= (int)fb.height) {
        return;
    }
    
    int index = y * fb.width + x;
    backbuffer[index] = rgb;
}

void fb_clear(uint32_t rgb) {
    for (uint32_t i = 0; i < fb.width * fb.height; i++) {
        backbuffer[i] = rgb;
    }
}

void fb_swap_buffers(void) {
    if (fb.vram == NULL) return;
    
    // Copy backbuffer to framebuffer VRAM
    // Use assembly for fast copy (stosq)
    size_t qword_count = (fb.width * fb.height * 4) / 8;
    
    uint64_t* src = (uint64_t*)backbuffer;
    uint64_t* dst = (uint64_t*)fb.vram;
    
    asm volatile(
        "cld\n\t"
        "rep movsq"
        : "+S"(src), "+D"(dst), "+c"(qword_count)
        :: "memory"
    );
}

void fb_draw_rectangle(int x, int y, int w, int h, uint32_t rgb) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            fb_draw_pixel(px, py, rgb);
        }
    }
}

void fb_draw_circle(int cx, int cy, int r, uint32_t rgb) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                fb_draw_pixel(cx + x, cy + y, rgb);
            }
        }
    }
}
