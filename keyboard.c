#include "../include/kernel.h"

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64
#define KBD_STATUS_OUTPUT_FULL 0x01

static char key_queue[256];
static int key_head = 0;
static int key_tail = 0;
static bool shift_down = false;

static inline uint8_t keyboard_inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void keyboard_outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void keyboard_queue_char(char c) {
    int next = (key_tail + 1) & 0xFF;
    if (next != key_head) {
        key_queue[key_tail] = c;
        key_tail = next;
    }
}

extern bool usb_has_keyboard(void);
extern bool usb_get_keyboard_char(char* c);

static void usb_keyboard_poll(void) {
    char c = 0;
    if (usb_has_keyboard() && usb_get_keyboard_char(&c) && c) {
        keyboard_queue_char(c);
    }
}

static const char keymap[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0, '*', 0, ' ', 0
};

static const char shift_keymap[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0, '*', 0, ' ', 0
};

static void keyboard_poll(void) {
    uint8_t status = keyboard_inb(KBD_STATUS_PORT);
    if ((status & KBD_STATUS_OUTPUT_FULL) == 0 || (status & 0x20)) {
        return;
    }

    uint8_t scancode = keyboard_inb(KBD_DATA_PORT);
    bool released = (scancode & 0x80) != 0;
    scancode &= 0x7F;

    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = !released;
        return;
    }

    if (released) {
        return;
    }

    char chr = shift_down ? shift_keymap[scancode] : keymap[scancode];
    if (chr) {
        keyboard_queue_char(chr);
    }
}

void keyboard_initialize(void) {
    key_head = 0;
    key_tail = 0;
    shift_down = false;
}

bool keyboard_has_data(void) {
    keyboard_poll();
    usb_keyboard_poll();
    return key_head != key_tail;
}

char keyboard_read_char(void) {
    keyboard_poll();
    usb_keyboard_poll();
    if (key_head == key_tail) {
        return 0;
    }

    char c = key_queue[key_head];
    key_head = (key_head + 1) & 0xFF;
    return c;
}
