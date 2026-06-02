/* Multiboot2 header for GRUB bootloader compatibility */
#ifndef MULTIBOOT2_HEADER
#define MULTIBOOT2_HEADER 1

#define MULTIBOOT2_HEADER_MAGIC 0xe85250d6
#define MULTIBOOT_ARCHITECTURE_I386 0
#define MULTIBOOT_HEADER_TAG_END 0

#ifndef ASM_FILE
#include <stdint.h>

struct multiboot_header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
} __attribute__((packed));

struct multiboot_header_tag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
} __attribute__((packed));

struct multiboot_tag_information_request {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t requests[0];
} __attribute__((packed));

#endif /* ASM_FILE */

#endif
