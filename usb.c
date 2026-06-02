#include "../include/kernel.h"
#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define XHCI_PCI_CLASS_SERIAL_BUS 0x0C
#define XHCI_PCI_SUBCLASS_USB     0x03
#define XHCI_PCI_PROGIF_XHCI      0x30

#define XHCI_CMD_RUN_STOP         0x01
#define XHCI_TRB_CYCLE           (1u << 0)
#define XHCI_TRB_TYPE_SHIFT      10
#define XHCI_TRB_TYPE_MASK       (0x3Fu << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_CHAIN           (1u << 4)
#define XHCI_TRB_INTERRUPT_ON_COMPLETE (1u << 5)

#define XHCI_TRB_TYPE_NORMAL          1
#define XHCI_TRB_TYPE_SETUP_STAGE     2
#define XHCI_TRB_TYPE_DATA_STAGE      3
#define XHCI_TRB_TYPE_STATUS_STAGE    4
#define XHCI_TRB_TYPE_LINK            6
#define XHCI_TRB_TYPE_ENABLE_SLOT     9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE  11
#define XHCI_TRB_TYPE_CONFIG_ENDPOINT 12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13

#define XHCI_RUNTIME_REG_OFFSET       0x200
#define XHCI_DOORBELL_OFFSET          0x1000

#define MAX_HID_DEVICES 4
#define USB_KEYBOARD_QUEUE_SIZE 64
#define USB_MOUSE_QUEUE_SIZE 32

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t base_class;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar0;
} xhci_pci_device_t;

typedef struct {
    uint8_t cap_length;
    uint8_t reserved;
    uint16_t interface_version;
    uint32_t hcs_params1;
    uint32_t hcs_params2;
    uint32_t hcs_params3;
    uint32_t hcc_params;
} xhci_cap_regs_t;

typedef struct {
    uint32_t command;
    uint32_t status;
    uint32_t page_size;
    uint32_t reserved0;
    uint64_t command_ring_ctrl;
    uint64_t reserved1;
    uint64_t dcbaap;
    uint32_t config;
    uint32_t reserved2;
} xhci_op_regs_t;

typedef struct {
    uint32_t port_status_ctrl;
    uint32_t port_power_mgmt_status_ctrl;
    uint32_t port_link_info;
    uint32_t port_hardware_lpm_ctrl;
} xhci_port_regs_t;

typedef struct {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

typedef struct {
    uint32_t reserved[8];
    uint32_t iman;
    uint32_t imod;
    uint32_t erstsz;
    uint32_t reserved2;
    uint64_t erstba;
    uint64_t erdp;
} xhci_runtime_regs_t;

typedef struct {
    uint64_t ring_segment_base_address;
    uint32_t ring_segment_size;
    uint32_t reserved;
} xhci_ring_segment_table_t;

typedef enum {
    USB_HID_NONE = 0,
    USB_HID_KEYBOARD,
    USB_HID_MOUSE,
} usb_hid_type_t;

typedef struct {
    bool attached;
    uint8_t slot_id;
    uint8_t address;
    usb_hid_type_t type;
    uint8_t endpoint_address;
    uint16_t max_packet_size;
    uint8_t interface_number;
} usb_hid_device_t;

static volatile xhci_cap_regs_t* xhci_cap = NULL;
static volatile xhci_op_regs_t* xhci_op = NULL;
static volatile xhci_port_regs_t* xhci_ports = NULL;
static volatile xhci_runtime_regs_t* xhci_runtime = NULL;
static volatile uint32_t* xhci_doorbell = NULL;
static xhci_trb_t cmd_ring[256];
static xhci_trb_t event_ring[256];
static xhci_ring_segment_table_t cmd_ring_segment;
static xhci_ring_segment_table_t event_ring_segment;
static uint64_t dcbaa[256];
static usb_hid_device_t hid_devices[MAX_HID_DEVICES];
static uint8_t keyboard_buffer[USB_KEYBOARD_QUEUE_SIZE];
static uint32_t keyboard_head = 0;
static uint32_t keyboard_tail = 0;
static uint8_t mouse_events[USB_MOUSE_QUEUE_SIZE][4];
static uint32_t mouse_head = 0;
static uint32_t mouse_tail = 0;
static int32_t mouse_x = 40;
static int32_t mouse_y = 12;
static uint32_t max_ports = 0;
static bool xhci_ready = false;
static uint16_t cmd_ring_index = 0;
static uint8_t cmd_cycle = 1;
static uint16_t event_ring_index = 0;
static uint8_t event_cycle = 1;

static inline void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void xhci_ring_doorbell(uint32_t ring) {
    if (!xhci_doorbell) {
        return;
    }
    xhci_doorbell[ring] = 1;
}

static uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000u
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)device << 11)
                     | ((uint32_t)function << 8)
                     | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint32_t pci_config_bar0(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t bar0 = pci_config_read(bus, device, function, 0x10);
    return bar0 & ~0x0F;
}

static bool pci_find_xhci_controller(xhci_pci_device_t* out) {
    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t vendor_device = pci_config_read(bus, device, function, 0x00);
                if ((vendor_device & 0xFFFF) == 0xFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                uint32_t class_code = pci_config_read(bus, device, function, 0x08);
                uint8_t prog_if = (class_code >> 8) & 0xFF;
                uint8_t subclass = (class_code >> 16) & 0xFF;
                uint8_t base_class = (class_code >> 24) & 0xFF;

                if (base_class == XHCI_PCI_CLASS_SERIAL_BUS &&
                    subclass == XHCI_PCI_SUBCLASS_USB &&
                    prog_if == XHCI_PCI_PROGIF_XHCI) {
                    out->bus = bus;
                    out->device = device;
                    out->function = function;
                    out->vendor_id = vendor_device & 0xFFFF;
                    out->device_id = (vendor_device >> 16) & 0xFFFF;
                    out->base_class = base_class;
                    out->subclass = subclass;
                    out->prog_if = prog_if;
                    out->bar0 = pci_config_bar0(bus, device, function);
                    return true;
                }

                uint8_t header_type = (pci_config_read(bus, device, function, 0x0C) >> 16) & 0xFF;
                if (function == 0 && (header_type & 0x80) == 0) {
                    break;
                }
            }
        }
    }
    return false;
}

static bool usb_queue_keyboard_char(char c) {
    uint32_t next = (keyboard_tail + 1) % USB_KEYBOARD_QUEUE_SIZE;
    if (next == keyboard_head) {
        return false;
    }
    keyboard_buffer[keyboard_tail] = (uint8_t)c;
    keyboard_tail = next;
    return true;
}

static bool usb_dequeue_keyboard_char(char* c) {
    if (keyboard_head == keyboard_tail) {
        return false;
    }
    *c = (char)keyboard_buffer[keyboard_head];
    keyboard_head = (keyboard_head + 1) % USB_KEYBOARD_QUEUE_SIZE;
    return true;
}

static bool usb_queue_mouse_event(const uint8_t report[4]) {
    uint32_t next = (mouse_tail + 1) % USB_MOUSE_QUEUE_SIZE;
    if (next == mouse_head) {
        return false;
    }
    mouse_events[mouse_tail][0] = report[0];
    mouse_events[mouse_tail][1] = report[1];
    mouse_events[mouse_tail][2] = report[2];
    mouse_events[mouse_tail][3] = report[3];
    mouse_tail = next;
    return true;
}

static bool usb_dequeue_mouse_event(uint8_t report[4]) {
    if (mouse_head == mouse_tail) {
        return false;
    }
    report[0] = mouse_events[mouse_head][0];
    report[1] = mouse_events[mouse_head][1];
    report[2] = mouse_events[mouse_head][2];
    report[3] = mouse_events[mouse_head][3];
    mouse_head = (mouse_head + 1) % USB_MOUSE_QUEUE_SIZE;
    return true;
}

static const char usb_hid_keyboard_map[256] = {
    [4] = 'a', [5] = 'b', [6] = 'c', [7] = 'd', [8] = 'e', [9] = 'f', [10] = 'g', [11] = 'h',
    [12] = 'i', [13] = 'j', [14] = 'k', [15] = 'l', [16] = 'm', [17] = 'n', [18] = 'o', [19] = 'p',
    [20] = 'q', [21] = 'r', [22] = 's', [23] = 't', [24] = 'u', [25] = 'v', [26] = 'w', [27] = 'x',
    [28] = 'y', [29] = 'z', [30] = '1', [31] = '2', [32] = '3', [33] = '4', [34] = '5', [35] = '6',
    [36] = '7', [37] = '8', [38] = '9', [39] = '0', [40] = '\n', [41] = 0x1B, [42] = '\b', [43] = '\t',
    [44] = ' ', [45] = '-', [46] = '=', [47] = '[', [48] = ']', [49] = '\\', [51] = ';', [52] = '\'',
    [53] = '`', [54] = ',', [55] = '.', [56] = '/',
};

static const char usb_hid_shift_map[256] = {
    [4] = 'A', [5] = 'B', [6] = 'C', [7] = 'D', [8] = 'E', [9] = 'F', [10] = 'G', [11] = 'H',
    [12] = 'I', [13] = 'J', [14] = 'K', [15] = 'L', [16] = 'M', [17] = 'N', [18] = 'O', [19] = 'P',
    [20] = 'Q', [21] = 'R', [22] = 'S', [23] = 'T', [24] = 'U', [25] = 'V', [26] = 'W', [27] = 'X',
    [28] = 'Y', [29] = 'Z', [30] = '!', [31] = '@', [32] = '#', [33] = '$', [34] = '%', [35] = '^',
    [36] = '&', [37] = '*', [38] = '(', [39] = ')', [40] = '\n', [41] = 0x1B, [42] = '\b', [43] = '\t',
    [44] = ' ', [45] = '_', [46] = '+', [47] = '{', [48] = '}', [49] = '|', [51] = ':', [52] = '"',
    [53] = '~', [54] = '<', [55] = '>', [56] = '?',
};

static const char* usb_hid_keycode_to_ascii(uint8_t keycode, bool shift) {
    if (shift) {
        return usb_hid_shift_map[keycode] ? &usb_hid_shift_map[keycode] : NULL;
    }

    return usb_hid_keyboard_map[keycode] ? &usb_hid_keyboard_map[keycode] : NULL;
}

static void usb_process_keyboard_report(const uint8_t report[8]) {
    bool shift = (report[0] & 0x22) != 0;
    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (!code) {
            continue;
        }
        if (code == 0x2A || code == 0x2C) {
            continue;
        }
        const char c = shift ? usb_hid_shift_map[code] : usb_hid_keyboard_map[code];
        if (c) {
            usb_queue_keyboard_char(c);
        }
    }
}

static void usb_process_mouse_report(const uint8_t report[4]) {
    bool left = (report[0] & 0x01) != 0;
    bool right = (report[0] & 0x02) != 0;
    bool middle = (report[0] & 0x04) != 0;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    int8_t wheel = (int8_t)report[3];

    if (usb_queue_mouse_event(report)) {
        mouse_x += dx;
        mouse_y -= dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x > 79) mouse_x = 79;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y > 23) mouse_y = 23;
    }
}

static bool usb_poll_hid_devices(void) {
    if (!xhci_ready || !xhci_ports) {
        return false;
    }

    bool found = false;
    for (uint32_t port = 0; port < max_ports && port < 256; port++) {
        uint32_t portsc = xhci_ports[port].port_status_ctrl;
        bool connected = (portsc & 1) != 0;
        bool enabled = ((portsc >> 1) & 1) != 0;
        if (!connected || !enabled) {
            continue;
        }

        found = true;
        uint8_t existing = 0xFF;
        for (int i = 0; i < MAX_HID_DEVICES; i++) {
            if (hid_devices[i].attached && hid_devices[i].address == port + 1) {
                existing = i;
                break;
            }
        }

        if (existing == 0xFF) {
            for (int i = 0; i < MAX_HID_DEVICES; i++) {
                if (!hid_devices[i].attached) {
                    hid_devices[i].attached = true;
                    hid_devices[i].address = port + 1;
                    hid_devices[i].slot_id = (uint8_t)(i + 1);
                    hid_devices[i].type = (i == 0) ? USB_HID_KEYBOARD : USB_HID_MOUSE;
                    hid_devices[i].endpoint_address = (i == 0) ? 0x81 : 0x82;
                    hid_devices[i].max_packet_size = 8;
                    hid_devices[i].interface_number = 0;
                    KLOG("USB: HID device attached at port %u, type=%u", port, hid_devices[i].type);
                    break;
                }
            }
        }
    }

    return found;
}

void usb_initialize(void) {
    xhci_pci_device_t controller;
    if (!pci_find_xhci_controller(&controller)) {
        KLOG("USB: No xHCI controller found");
        return;
    }

    KLOG("USB: xHCI controller found at PCI %u:%u.%u", controller.bus,
         controller.device, controller.function);

    xhci_cap = (volatile xhci_cap_regs_t*)(uintptr_t)controller.bar0;
    if (!xhci_cap) {
        KLOG("USB: Failed to map xHCI capability registers");
        return;
    }

    max_ports = xhci_cap->hcs_params1 & 0xFF;
    KLOG("USB: xHCI reports %u port(s)", max_ports);

    uint8_t cap_len = xhci_cap->cap_length;
    xhci_op = (volatile xhci_op_regs_t*)((uintptr_t)xhci_cap + cap_len);
    xhci_ports = (volatile xhci_port_regs_t*)((uintptr_t)xhci_op + 0x400);
    xhci_runtime = (volatile xhci_runtime_regs_t*)((uintptr_t)xhci_cap + cap_len + XHCI_RUNTIME_REG_OFFSET);
    xhci_doorbell = (volatile uint32_t*)((uintptr_t)xhci_cap + cap_len + XHCI_DOORBELL_OFFSET);

    for (int i = 0; i < 256; i++) {
        cmd_ring[i].parameter_lo = 0;
        cmd_ring[i].parameter_hi = 0;
        cmd_ring[i].status = 0;
        cmd_ring[i].control = 0;

        event_ring[i].parameter_lo = 0;
        event_ring[i].parameter_hi = 0;
        event_ring[i].status = 0;
        event_ring[i].control = 0;
        dcbaa[i] = 0;
    }

    cmd_ring_segment.ring_segment_base_address = (uint64_t)(uintptr_t)&cmd_ring;
    cmd_ring_segment.ring_segment_size = 256;
    cmd_ring_segment.reserved = 0;

    event_ring_segment.ring_segment_base_address = (uint64_t)(uintptr_t)&event_ring;
    event_ring_segment.ring_segment_size = 256;
    event_ring_segment.reserved = 0;

    xhci_op->command_ring_ctrl = (uint64_t)(uintptr_t)&cmd_ring_segment | 1;
    xhci_op->dcbaap = (uint64_t)(uintptr_t)dcbaa;
    xhci_op->config = 1;
    xhci_op->command = xhci_op->command | XHCI_CMD_RUN_STOP;

    xhci_runtime->iman = 1;
    xhci_runtime->imod = 0;
    xhci_runtime->erstsz = 1;
    xhci_runtime->erstba = (uint64_t)(uintptr_t)&event_ring_segment;
    xhci_runtime->erdp = (uint64_t)(uintptr_t)&event_ring[0];

    xhci_ready = true;
    KLOG("USB: xHCI initialization complete");
}

void usb_poll(void) {
    if (!xhci_ready) {
        return;
    }

    usb_poll_hid_devices();
}

bool usb_has_keyboard(void) {
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (hid_devices[i].attached && hid_devices[i].type == USB_HID_KEYBOARD) {
            return true;
        }
    }
    return false;
}

bool usb_get_keyboard_char(char* c) {
    if (!c) {
        return false;
    }
    return usb_dequeue_keyboard_char(c);
}

bool usb_has_mouse(void) {
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (hid_devices[i].attached && hid_devices[i].type == USB_HID_MOUSE) {
            return true;
        }
    }
    return false;
}

bool usb_get_mouse_report(int8_t* dx, int8_t* dy, bool* left, bool* right, bool* middle) {
    uint8_t report[4];
    if (!usb_dequeue_mouse_event(report)) {
        return false;
    }

    *dx = (int8_t)report[1];
    *dy = (int8_t)report[2];
    *left = (report[0] & 0x01) != 0;
    *right = (report[0] & 0x02) != 0;
    *middle = (report[0] & 0x04) != 0;
    return true;
}
