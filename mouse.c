#include "../include/kernel.h"
#include <stdint.h>
#include <stdbool.h>

#define PS2_STATUS_PORT 0x64
#define PS2_DATA_PORT   0x60
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_MOUSE_DATA  0x20

typedef struct {
    int8_t dx;
    int8_t dy;
    bool left;
    bool right;
    bool middle;
} mouse_event_t;

static mouse_event_t mouse_queue[32];
static int mouse_queue_head = 0;
static int mouse_queue_tail = 0;
static uint8_t mouse_packet[3];
static int mouse_packet_offset = 0;
static int mouse_x = 40;
static int mouse_y = 12;
static bool mouse_initialized = false;

extern void usb_initialize(void);
extern void usb_poll(void);
extern bool usb_has_mouse(void);
extern bool usb_get_mouse_report(int8_t* dx, int8_t* dy, bool* left, bool* right, bool* middle);

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static bool queue_is_full(void) {
    return ((mouse_queue_tail + 1) & 31) == mouse_queue_head;
}

static bool queue_is_empty(void) {
    return mouse_queue_head == mouse_queue_tail;
}

static void enqueue_mouse_event(int8_t dx, int8_t dy, bool left, bool right, bool middle) {
    if (queue_is_full()) {
        return;
    }

    mouse_queue[mouse_queue_tail].dx = dx;
    mouse_queue[mouse_queue_tail].dy = dy;
    mouse_queue[mouse_queue_tail].left = left;
    mouse_queue[mouse_queue_tail].right = right;
    mouse_queue[mouse_queue_tail].middle = middle;
    mouse_queue_tail = (mouse_queue_tail + 1) & 31;
}

static void process_ps2_packet(void) {
    uint8_t status = mouse_packet[0];
    bool left = (status & 0x01) != 0;
    bool right = (status & 0x02) != 0;
    bool middle = (status & 0x04) != 0;
    int8_t dx = (int8_t)mouse_packet[1];
    int8_t dy = (int8_t)mouse_packet[2];

    enqueue_mouse_event(dx, dy, left, right, middle);
}

static void mouse_poll_ps2(void) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0 || (status & PS2_STATUS_MOUSE_DATA) == 0) {
        return;
    }

    uint8_t data = inb(PS2_DATA_PORT);
    if (mouse_packet_offset == 0 && (data & 0x08) == 0) {
        return;
    }

    mouse_packet[mouse_packet_offset++] = data;
    if (mouse_packet_offset >= 3) {
        process_ps2_packet();
        mouse_packet_offset = 0;
    }
}

static void wait_input_buffer_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS_PORT) & 0x02) == 0) {
            return;
        }
    }
}

static bool send_mouse_command(uint8_t command) {
    wait_input_buffer_clear();
    outb(PS2_STATUS_PORT, 0xD4);
    wait_input_buffer_clear();
    outb(PS2_DATA_PORT, command);

    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            uint8_t response = inb(PS2_DATA_PORT);
            return response == 0xFA;
        }
    }
    return false;
}

void mouse_initialize(void) {
    mouse_queue_head = 0;
    mouse_queue_tail = 0;
    mouse_packet_offset = 0;
    mouse_x = 40;
    mouse_y = 12;
    mouse_initialized = true;

    send_mouse_command(0xF6); // Set defaults
    send_mouse_command(0xF4); // Enable packet streaming

    usb_initialize();
    usb_poll();
}

static void poll_usb_mouse(void) {
    usb_poll();
    if (!usb_has_mouse()) {
        return;
    }

    int8_t dx = 0;
    int8_t dy = 0;
    bool left = false;
    bool right = false;
    bool middle = false;

    if (usb_get_mouse_report(&dx, &dy, &left, &right, &middle)) {
        enqueue_mouse_event(dx, dy, left, right, middle);
    }
}

bool mouse_has_event(void) {
    if (!mouse_initialized) {
        return false;
    }

    mouse_poll_ps2();
    poll_usb_mouse();
    return !queue_is_empty();
}

void mouse_get_event(int* dx, int* dy, bool* left, bool* right, bool* middle) {
    if (queue_is_empty() || !dx || !dy || !left || !right || !middle) {
        if (dx) *dx = 0;
        if (dy) *dy = 0;
        if (left) *left = false;
        if (right) *right = false;
        if (middle) *middle = false;
        return;
    }

    mouse_event_t event = mouse_queue[mouse_queue_head];
    mouse_queue_head = (mouse_queue_head + 1) & 31;

    mouse_x += event.dx;
    mouse_y -= event.dy;

    if (mouse_x < 0) {
        mouse_x = 0;
    } else if (mouse_x > 79) {
        mouse_x = 79;
    }

    if (mouse_y < 0) {
        mouse_y = 0;
    } else if (mouse_y > 23) {
        mouse_y = 23;
    }

    *dx = event.dx;
    *dy = event.dy;
    *left = event.left;
    *right = event.right;
    *middle = event.middle;
}

int mouse_get_x(void) {
    return mouse_x;
}

int mouse_get_y(void) {
    return mouse_y;
}
