#include "../include/kernel.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PIT_CONTROL_PORT   0x43
#define PIT_CHANNEL_2_PORT 0x42
#define PC_SPEAKER_PORT    0x61
#define PIT_FREQUENCY      1193180u

#define AUDIO_PCI_CLASS_MULTIMEDIA 0x04
#define AUDIO_PCI_SUBCLASS_HDA     0x03
#define AUDIO_PCI_PROGIF_HDA       0x00

#define HDA_REG_GCTL 0x08
#define HDA_GCTL_RESET 0x00000001

static bool audio_initialized = false;
static bool hda_controller_present = false;
static volatile uint32_t* hda_mmio = NULL;
static uint32_t hda_bar0 = 0;
static uint8_t audio_volume = 128;

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_build_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)device << 11)
         | ((uint32_t)function << 8)
         | ((uint32_t)offset & 0xFC);
}

uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_initialize(void) {
    // PCI config space is available through I/O ports by default.
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out) {
    if (!out) {
        return false;
    }

    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t vendor_device = pci_read_config_dword(bus, device, function, 0x00);
                uint16_t vendor = vendor_device & 0xFFFF;
                uint16_t device_id_raw = (vendor_device >> 16) & 0xFFFF;
                if (vendor == 0xFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                if (vendor == vendor_id && device_id_raw == device_id) {
                    uint32_t class_code = pci_read_config_dword(bus, device, function, 0x08);
                    out->bus = bus;
                    out->device = device;
                    out->function = function;
                    out->vendor_id = vendor;
                    out->device_id = device_id_raw;
                    out->base_class = (class_code >> 24) & 0xFF;
                    out->subclass = (class_code >> 16) & 0xFF;
                    out->prog_if = (class_code >> 8) & 0xFF;
                    out->bar0 = pci_read_config_dword(bus, device, function, 0x10) & ~0x0Ful;
                    return true;
                }

                uint8_t header_type = (pci_read_config_dword(bus, device, function, 0x0C) >> 16) & 0xFF;
                if (function == 0 && (header_type & 0x80) == 0) {
                    break;
                }
            }
        }
    }

    return false;
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out) {
    if (!out) {
        return false;
    }

    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t vendor_device = pci_read_config_dword(bus, device, function, 0x00);
                uint16_t vendor = vendor_device & 0xFFFF;
                if (vendor == 0xFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                uint32_t class_code_reg = pci_read_config_dword(bus, device, function, 0x08);
                uint8_t base_class = (class_code_reg >> 24) & 0xFF;
                uint8_t sub_class = (class_code_reg >> 16) & 0xFF;
                uint8_t prog_interface = (class_code_reg >> 8) & 0xFF;

                if (base_class == class_code && sub_class == subclass && prog_interface == prog_if) {
                    out->bus = bus;
                    out->device = device;
                    out->function = function;
                    out->vendor_id = vendor;
                    out->device_id = (vendor_device >> 16) & 0xFFFF;
                    out->base_class = base_class;
                    out->subclass = sub_class;
                    out->prog_if = prog_interface;
                    out->bar0 = pci_read_config_dword(bus, device, function, 0x10) & ~0x0Ful;
                    return true;
                }

                uint8_t header_type = (pci_read_config_dword(bus, device, function, 0x0C) >> 16) & 0xFF;
                if (function == 0 && (header_type & 0x80) == 0) {
                    break;
                }
            }
        }
    }

    return false;
}

static void audio_pit_disable(void) {
    uint8_t value = inb(PC_SPEAKER_PORT);
    value &= 0xFC;
    outb(PC_SPEAKER_PORT, value);
}

static void audio_pit_set_frequency(uint32_t frequency) {
    if (frequency == 0) {
        audio_pit_disable();
        return;
    }

    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / frequency);
    outb(PIT_CONTROL_PORT, 0xB6);
    outb(PIT_CHANNEL_2_PORT, divisor & 0xFF);
    outb(PIT_CHANNEL_2_PORT, (divisor >> 8) & 0xFF);

    uint8_t value = inb(PC_SPEAKER_PORT);
    value |= 0x03;
    outb(PC_SPEAKER_PORT, value);
}

static void audio_busy_wait(uint32_t milliseconds) {
    volatile uint64_t count = (uint64_t)milliseconds * 2000ULL;
    while (count--) {
        asm volatile("nop");
    }
}

static bool hda_reset(void) {
    if (!hda_mmio) {
        return false;
    }

    hda_mmio[HDA_REG_GCTL / 4] |= HDA_GCTL_RESET;
    for (int i = 0; i < 100000; i++) {
        if ((hda_mmio[HDA_REG_GCTL / 4] & HDA_GCTL_RESET) == 0) {
            return true;
        }
    }
    return false;
}

static bool hda_enable_bus_master(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t command = pci_read_config_dword(bus, device, function, 0x04);
    command |= 0x00000006u; // Enable memory space and bus mastering
    pci_write_config_dword(bus, device, function, 0x04, command);
    return true;
}

static bool hda_find_controller(void) {
    pci_device_t controller;
    if (!pci_find_class(AUDIO_PCI_CLASS_MULTIMEDIA, AUDIO_PCI_SUBCLASS_HDA, AUDIO_PCI_PROGIF_HDA, &controller)) {
        return false;
    }

    if (controller.bar0 == 0) {
        return false;
    }

    hda_bar0 = controller.bar0;
    hda_mmio = (volatile uint32_t*)(uintptr_t)controller.bar0;
    if (!hda_enable_bus_master(controller.bus, controller.device, controller.function)) {
        return false;
    }

    return true;
}

void audio_initialize(void) {
    audio_volume = 128;
    audio_initialized = false;
    hda_controller_present = false;
    hda_mmio = NULL;
    hda_bar0 = 0;

    pci_initialize();

    if (hda_find_controller()) {
        if (hda_reset()) {
            hda_controller_present = true;
            KLOG("Intel HDA controller detected at BAR0=0x%x", hda_bar0);
        } else {
            KLOG("Intel HDA controller found but reset failed");
            hda_mmio = NULL;
            hda_bar0 = 0;
        }
    } else {
        KLOG("No Intel HDA controller detected; audio will use PC speaker fallback");
    }

    audio_initialized = true;
}

bool audio_is_ready(void) {
    return audio_initialized && (hda_controller_present || true);
}

void audio_set_volume(uint8_t volume) {
    audio_volume = (volume > 255) ? 255 : volume;
}

void audio_play_tone(uint32_t frequency, uint32_t duration_ms) {
    if (!audio_initialized || frequency == 0 || duration_ms == 0) {
        audio_pit_disable();
        return;
    }

    audio_pit_set_frequency(frequency);
    audio_busy_wait(duration_ms);
    audio_pit_disable();
}

void audio_stop_tone(void) {
    audio_pit_disable();
}

bool audio_play_pcm(const int16_t* samples, size_t sample_count, uint32_t sample_rate) {
    if (!audio_initialized) {
        return false;
    }

    if (!hda_controller_present) {
        return false;
    }

    // Full HDA PCM playback is not implemented yet.
    return false;
}
