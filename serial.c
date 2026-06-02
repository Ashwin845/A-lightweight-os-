/* 
 * ============================================================================
 * SERIAL PORT DRIVER (COM1) - For debugging output
 * ============================================================================
 */

#include "../include/kernel.h"

/* COM1 port addresses */
#define COM1_PORT 0x3F8
#define COM_DATA 0
#define COM_INT_EN 1
#define COM_INT_ID 2
#define COM_LINE_CTRL 3
#define COM_MODEM_CTRL 4
#define COM_LINE_STATUS 5
#define COM_MODEM_STATUS 6

/* ============================================================================
 * I/O PORT HELPER FUNCTIONS
 * ============================================================================ */

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("out %b0, %w1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("in %w1, %b0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile("out %w0, %w1" :: "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile("in %w1, %w0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile("out %0, %w1" :: "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("in %w1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============================================================================
 * SERIAL PORT IMPLEMENTATION
 * ============================================================================ */

void serial_initialize(void) {
    // Disable all interrupts
    outb(COM1_PORT + COM_INT_EN, 0x00);
    
    // Set speed to 115200 baud
    // Set DLAB (Divisor Latch Access Bit)
    outb(COM1_PORT + COM_LINE_CTRL, 0x80);
    
    // Set divisor to 1 (115200 / 1)
    outb(COM1_PORT + COM_DATA, 0x01);
    outb(COM1_PORT + COM_INT_EN, 0x00);
    
    // 8 bits, 1 stop bit, no parity
    outb(COM1_PORT + COM_LINE_CTRL, 0x03);
    
    // Enable FIFO, clear FIFO
    outb(COM1_PORT + COM_INT_ID, 0xC7);
    
    // Set RTS and DTR
    outb(COM1_PORT + COM_MODEM_CTRL, 0x0B);
}

static bool serial_is_transmit_empty(void) {
    return (inb(COM1_PORT + COM_LINE_STATUS) & 0x20) != 0;
}

void serial_putchar(char c) {
    while (!serial_is_transmit_empty());
    outb(COM1_PORT + COM_DATA, (uint8_t)c);
}

static bool serial_is_receive_full(void) {
    return (inb(COM1_PORT + COM_LINE_STATUS) & 1) != 0;
}

char serial_getchar(void) {
    while (!serial_is_receive_full());
    return (char)inb(COM1_PORT + COM_DATA);
}
