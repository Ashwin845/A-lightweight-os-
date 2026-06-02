#include "../include/kernel.h"
#include <stdint.h>
#include <stdbool.h>

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL8139_MAC_ADDR      0x00
#define RTL8139_TxAddr0       0x20
#define RTL8139_TxStatus0     0x10
#define RTL8139_RxBuf         0x30
#define RTL8139_Command       0x37
#define RTL8139_TxConfig      0x40
#define RTL8139_RxConfig      0x44
#define RTL8139_IntMask       0x3C
#define RTL8139_IntStatus     0x3E
#define RTL8139_RxMissed      0x4C
#define RTL8139_CapR          0x38
#define RTL8139_CurR          0x3A
#define RTL8139_Config1       0x52

#define RTL8139_CMD_RESET     0x10
#define RTL8139_CMD_RX_EN     0x08
#define RTL8139_CMD_TX_EN     0x04

#define RTL8139_RCR_AAP       0x0001
#define RTL8139_RCR_APM       0x0002
#define RTL8139_RCR_AM        0x0004
#define RTL8139_RCR_AB        0x0008
#define RTL8139_RCR_WRAP      0x0080
#define RTL8139_RCR_MAXDMA_2048 0x0300
#define RTL8139_RCR_FIFO_THRESH_64 0xE00

#define ETH_P_IP              0x0800
#define ETH_P_ARP             0x0806
#define ARP_HTYPE_ETHERNET    0x0001
#define ARP_PTYPE_IPV4        0x0800
#define ARP_OPER_REQUEST      0x0001
#define ARP_OPER_REPLY        0x0002

#define IP_PROTO_UDP          17
#define IP_PROTO_ICMP         1

#define DHCP_CLIENT_PORT      68
#define DHCP_SERVER_PORT      67
#define DHCP_MAGIC_COOKIE     0x63825363
#define DHCP_OP_REQUEST       1
#define DHCP_OP_REPLY         2
#define DHCP_OPT_MESSAGE_TYPE 53
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_SUBNET_MASK  1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS          6
#define DHCP_OPT_END          255

#define RX_BUFFER_SIZE        (8192 + 16 + 1500)
#define TX_BUFFER_SIZE        2048
#define TX_DESC_COUNT         4

static uint16_t io_base = 0;
static bool network_ready = false;
static bool dhcp_acquired = false;
static uint8_t local_mac[6] = {0};
static uint32_t local_ip = 0;
static uint32_t local_netmask = 0;
static uint32_t local_gateway = 0;
static uint32_t dhcp_server_ip = 0;
static uint32_t dhcp_transaction_id = 0x393047;
static uint8_t arp_peer_mac[6] = {0};
static uint32_t arp_peer_ip = 0;
static uint8_t rx_buffer[RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[TX_DESC_COUNT][TX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint32_t tx_phys[TX_DESC_COUNT];
static uint8_t tx_index = 0;

extern uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
extern void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
extern bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);
extern bool pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out);

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
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

static inline uintptr_t virt_to_phys(void* address) {
    uintptr_t ptr = (uintptr_t)address;
    if (ptr >= KERNEL_BASE) {
        return ptr - KERNEL_BASE;
    }
    return ptr;
}

static inline void io_write8(uint16_t reg, uint8_t value) {
    outb(io_base + reg, value);
}

static inline uint8_t io_read8(uint16_t reg) {
    return inb(io_base + reg);
}

static inline void io_write16(uint16_t reg, uint16_t value) {
    outw(io_base + reg, value);
}

static inline uint16_t io_read16(uint16_t reg) {
    return inw(io_base + reg);
}

static inline void io_write32(uint16_t reg, uint32_t value) {
    outl(io_base + reg, value);
}

static inline uint32_t io_read32(uint16_t reg) {
    return inl(io_base + reg);
}

static uint16_t checksum16(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
    }
    if (len & 1) {
        sum += data[len - 1];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void mac_copy(uint8_t* dest, const uint8_t* src) {
    for (int i = 0; i < 6; i++) {
        dest[i] = src[i];
    }
}

static bool mac_equal(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static void ip_copy(uint8_t* dest, const uint8_t* src) {
    for (int i = 0; i < 4; i++) {
        dest[i] = src[i];
    }
}

static uint32_t ip_pack(const uint8_t* addr) {
    return ((uint32_t)addr[0] << 24) | ((uint32_t)addr[1] << 16) | ((uint32_t)addr[2] << 8) | addr[3];
}

static void ip_unpack(uint32_t value, uint8_t* out) {
    out[0] = (value >> 24) & 0xFF;
    out[1] = (value >> 16) & 0xFF;
    out[2] = (value >> 8) & 0xFF;
    out[3] = value & 0xFF;
}

static bool rtl8139_send_frame(const uint8_t* dest_mac, uint16_t ethertype, const uint8_t* payload, size_t payload_len) {
    if (payload_len + 14 > TX_BUFFER_SIZE) {
        return false;
    }

    uint8_t* frame = tx_buffers[tx_index];
    for (int i = 0; i < 6; i++) {
        frame[i] = dest_mac[i];
        frame[6 + i] = local_mac[i];
    }
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    for (size_t i = 0; i < payload_len; i++) {
        frame[14 + i] = payload[i];
    }

    uint32_t phys = (uint32_t)virt_to_phys(frame);
    io_write32(RTL8139_TxAddr0 + tx_index * 4, phys);
    io_write32(RTL8139_TxStatus0 + tx_index * 4, (uint32_t)payload_len);

    uint8_t current_index = tx_index;
    tx_index = (tx_index + 1) % TX_DESC_COUNT;

    for (int i = 0; i < 100000; i++) {
        uint32_t status = io_read32(RTL8139_TxStatus0 + current_index * 4);
        if ((status & 0x2000) != 0) {
            return true;
        }
    }
    return true;
}

static void rtl8139_process_rx(void);
static void network_handle_arp(const uint8_t* frame, size_t len);
static void network_handle_ip(const uint8_t* frame, size_t len);

static bool rtl8139_setup(void) {
    io_write8(RTL8139_Command, RTL8139_CMD_RESET);
    for (int i = 0; i < 100000; i++) {
        if ((io_read8(RTL8139_Command) & RTL8139_CMD_RESET) == 0) {
            break;
        }
    }

    uint32_t rx_phys = (uint32_t)virt_to_phys(rx_buffer);
    io_write32(RTL8139_RxBuf, rx_phys);
    io_write32(RTL8139_TxConfig, 0x03000000);
    io_write32(RTL8139_RxConfig, RTL8139_RCR_AAP | RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB | RTL8139_RCR_WRAP | RTL8139_RCR_MAXDMA_2048);
    io_write8(RTL8139_Config1, 0x00);
    io_write16(RTL8139_IntMask, 0x0005);
    io_write8(RTL8139_Command, RTL8139_CMD_RX_EN | RTL8139_CMD_TX_EN);

    for (int i = 0; i < TX_DESC_COUNT; i++) {
        tx_phys[i] = (uint32_t)virt_to_phys(tx_buffers[i]);
    }

    return true;
}

static void rtl8139_read_mac(void) {
    for (int i = 0; i < 6; i++) {
        local_mac[i] = io_read8(RTL8139_MAC_ADDR + i);
    }
}

static void write_eth_header(uint8_t* buffer, const uint8_t* dest_mac, uint16_t ethertype) {
    for (int i = 0; i < 6; i++) {
        buffer[i] = dest_mac[i];
        buffer[6 + i] = local_mac[i];
    }
    buffer[12] = (uint8_t)(ethertype >> 8);
    buffer[13] = (uint8_t)(ethertype & 0xFF);
}

static void write_ip_header(uint8_t* buffer, uint16_t total_length, uint8_t protocol, uint32_t src_ip, uint32_t dest_ip) {
    buffer[0] = 0x45;
    buffer[1] = 0x00;
    buffer[2] = (uint8_t)(total_length >> 8);
    buffer[3] = (uint8_t)total_length;
    buffer[4] = 0x00;
    buffer[5] = 0x00;
    buffer[6] = 0x40;
    buffer[7] = 0x00;
    buffer[8] = 64;
    buffer[9] = protocol;
    buffer[10] = 0;
    buffer[11] = 0;
    buffer[12] = (src_ip >> 24) & 0xFF;
    buffer[13] = (src_ip >> 16) & 0xFF;
    buffer[14] = (src_ip >> 8) & 0xFF;
    buffer[15] = src_ip & 0xFF;
    buffer[16] = (dest_ip >> 24) & 0xFF;
    buffer[17] = (dest_ip >> 16) & 0xFF;
    buffer[18] = (dest_ip >> 8) & 0xFF;
    buffer[19] = dest_ip & 0xFF;
    uint16_t checksum = checksum16(buffer, 20);
    buffer[10] = (uint8_t)(checksum >> 8);
    buffer[11] = (uint8_t)checksum;
}

static void write_arp_request(uint8_t* buffer, uint32_t target_ip) {
    for (int i = 0; i < 6; i++) {
        buffer[i] = 0xFF;
        buffer[6 + i] = local_mac[i];
    }
    buffer[12] = 0x08;
    buffer[13] = 0x06;
    buffer[14] = 0x00;
    buffer[15] = 0x01;
    buffer[16] = 0x08;
    buffer[17] = 0x00;
    buffer[18] = 0x06;
    buffer[19] = 0x04;
    buffer[20] = 0x00;
    buffer[21] = 0x01;
    for (int i = 0; i < 6; i++) {
        buffer[22 + i] = local_mac[i];
    }
    buffer[28] = 0;
    buffer[29] = 0;
    buffer[30] = 0;
    buffer[31] = 0;
    buffer[32] = (uint8_t)(target_ip >> 24);
    buffer[33] = (uint8_t)(target_ip >> 16);
    buffer[34] = (uint8_t)(target_ip >> 8);
    buffer[35] = (uint8_t)target_ip;
    for (int i = 0; i < 6; i++) {
        buffer[36 + i] = 0;
    }
    buffer[42] = 0;
    buffer[43] = 0;
    buffer[44] = 0;
    buffer[45] = 0;
    buffer[46] = 0;
    buffer[47] = 0;
}

static void send_arp_request(uint32_t target_ip) {
    uint8_t packet[60];
    write_arp_request(packet, target_ip);
    rtl8139_send_frame(packet, ETH_P_ARP, packet + 14, 46);
}

static void send_arp_reply(const uint8_t* target_mac, uint32_t target_ip) {
    uint8_t packet[60];
    write_eth_header(packet, target_mac, ETH_P_ARP);
    packet[14] = 0x00;
    packet[15] = 0x02;
    packet[16] = 0x08;
    packet[17] = 0x00;
    packet[18] = 0x06;
    packet[19] = 0x04;
    packet[20] = 0x00;
    packet[21] = 0x02;
    for (int i = 0; i < 6; i++) {
        packet[22 + i] = local_mac[i];
        packet[28 + i] = target_mac[i];
    }
    packet[34] = (local_ip >> 24) & 0xFF;
    packet[35] = (local_ip >> 16) & 0xFF;
    packet[36] = (local_ip >> 8) & 0xFF;
    packet[37] = local_ip & 0xFF;
    packet[38] = (target_ip >> 24) & 0xFF;
    packet[39] = (target_ip >> 16) & 0xFF;
    packet[40] = (target_ip >> 8) & 0xFF;
    packet[41] = target_ip & 0xFF;
    for (int i = 0; i < 18; i++) {
        packet[42 + i] = 0;
    }
    rtl8139_send_frame(target_mac, ETH_P_ARP, packet + 14, 46);
}

static void build_dhcp_discover(uint8_t* buffer, size_t* length) {
    for (int i = 0; i < 240; i++) {
        buffer[i] = 0;
    }
    buffer[0] = DHCP_OP_REQUEST;
    buffer[1] = 1;
    buffer[2] = 6;
    buffer[3] = 0;
    buffer[4] = (uint8_t)(dhcp_transaction_id >> 24);
    buffer[5] = (uint8_t)(dhcp_transaction_id >> 16);
    buffer[6] = (uint8_t)(dhcp_transaction_id >> 8);
    buffer[7] = (uint8_t)(dhcp_transaction_id);
    buffer[8] = 0;
    buffer[9] = 0;
    buffer[10] = 0;
    buffer[11] = 0;
    buffer[12] = 0;
    buffer[13] = 0;
    buffer[14] = 0;
    buffer[15] = 0;
    for (int i = 0; i < 6; i++) {
        buffer[28 + i] = local_mac[i];
    }
    buffer[236] = 0x63;
    buffer[237] = 0x82;
    buffer[238] = 0x53;
    buffer[239] = 0x63;
    buffer[240] = DHCP_OPT_MESSAGE_TYPE;
    buffer[241] = 1;
    buffer[242] = 1;
    buffer[243] = DHCP_OPT_END;
    *length = 244;
}

static void build_dhcp_request(uint8_t* buffer, size_t* length) {
    build_dhcp_discover(buffer, length);
    buffer[242] = DHCP_OPT_REQUESTED_IP;
    buffer[243] = 4;
    buffer[244] = (local_ip >> 24) & 0xFF;
    buffer[245] = (local_ip >> 16) & 0xFF;
    buffer[246] = (local_ip >> 8) & 0xFF;
    buffer[247] = local_ip & 0xFF;
    buffer[248] = DHCP_OPT_SERVER_ID;
    buffer[249] = 4;
    buffer[250] = (dhcp_server_ip >> 24) & 0xFF;
    buffer[251] = (dhcp_server_ip >> 16) & 0xFF;
    buffer[252] = (dhcp_server_ip >> 8) & 0xFF;
    buffer[253] = dhcp_server_ip & 0xFF;
    buffer[254] = DHCP_OPT_END;
    *length = 255;
}

static uint32_t parse_ipv4_address(const uint8_t* bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void parse_dhcp_options(const uint8_t* options, size_t len) {
    size_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t opt = options[offset];
        if (opt == DHCP_OPT_END) {
            return;
        }
        if (opt == 0) {
            offset++;
            continue;
        }
        uint8_t opt_len = options[offset + 1];
        if (offset + 2 + opt_len > len) {
            return;
        }
        const uint8_t* value = options + offset + 2;
        switch (opt) {
            case DHCP_OPT_MESSAGE_TYPE:
                if (opt_len >= 1 && value[0] == 2) {
                    dhcp_acquired = true;
                }
                break;
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len == 4) {
                    local_netmask = parse_ipv4_address(value);
                }
                break;
            case DHCP_OPT_ROUTER:
                if (opt_len >= 4) {
                    local_gateway = parse_ipv4_address(value);
                }
                break;
            case DHCP_OPT_SERVER_ID:
                if (opt_len == 4) {
                    dhcp_server_ip = parse_ipv4_address(value);
                }
                break;
            default:
                break;
        }
        offset += 2 + opt_len;
    }
}

static void network_handle_dhcp_offer(const uint8_t* packet, size_t len) {
    if (len < 248) {
        return;
    }
    uint32_t xid = (packet[4] << 24) | (packet[5] << 16) | (packet[6] << 8) | packet[7];
    if (xid != dhcp_transaction_id) {
        return;
    }
    local_ip = parse_ipv4_address(packet + 24);
    parse_dhcp_options(packet + 248, len - 248);
}

static bool rtl8139_receive_packet(uint8_t* buffer, size_t* out_len) {
    uint16_t cur = io_read16(RTL8139_CurR);
    uint16_t boundary = io_read16(RTL8139_CapR);
    if (boundary == 0) {
        boundary = 0x3F8; // fallback
    }
    uint16_t offset = (boundary + 16) & 0xFFF8;
    if (offset >= RX_BUFFER_SIZE) {
        offset = 0;
    }
    uint16_t status = rx_buffer[offset] | (rx_buffer[offset + 1] << 8);
    if ((status & 0x01) == 0) {
        return false;
    }
    uint16_t length = rx_buffer[offset + 2] | (rx_buffer[offset + 3] << 8);
    if (length > 1500 || offset + 4 + length > RX_BUFFER_SIZE) {
        io_write16(RTL8139_CapR, (offset - 16) & 0xFFF8);
        return false;
    }
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = rx_buffer[offset + 4 + i];
    }
    *out_len = length;
    uint16_t next = offset + length + 4;
    next = (next + 3) & ~3;
    if (next >= RX_BUFFER_SIZE) {
        next = 0;
    }
    io_write16(RTL8139_CapR, (next - 16) & 0xFFF8);
    return true;
}

static void network_handle_arp(const uint8_t* frame, size_t len) {
    if (len < 42) {
        return;
    }
    const uint8_t* arp = frame + 14;
    uint16_t oper = (arp[6] << 8) | arp[7];
    const uint8_t* sender_mac = arp + 8;
    uint32_t src_ip = parse_ipv4_address(arp + 14);
    uint32_t target_ip = parse_ipv4_address(arp + 24);
    if (oper == ARP_OPER_REQUEST && target_ip == local_ip) {
        send_arp_reply(sender_mac, src_ip);
    }
    if (oper == ARP_OPER_REPLY) {
        mac_copy(arp_peer_mac, sender_mac);
        arp_peer_ip = src_ip;
    }
}

static void network_handle_ip(const uint8_t* frame, size_t len) {
    if (len < 20) {
        return;
    }
    uint8_t protocol = frame[9];
    uint32_t dest_ip = parse_ipv4_address(frame + 16);
    if (dest_ip != local_ip && dest_ip != 0xFFFFFFFF) {
        return;
    }
    if (protocol == IP_PROTO_UDP && len >= 28) {
        uint16_t src_port = (frame[20] << 8) | frame[21];
        uint16_t dest_port = (frame[22] << 8) | frame[23];
        if (dest_port == DHCP_CLIENT_PORT && src_port == DHCP_SERVER_PORT) {
            network_handle_dhcp_offer(frame + 20, len - 20);
        }
    }
}

static void rtl8139_process_rx(void) {
    uint8_t packet[1514];
    size_t packet_len = 0;
    while (rtl8139_receive_packet(packet, &packet_len)) {
        if (packet_len < 14) {
            continue;
        }
        uint16_t ethertype = ((uint16_t)packet[12] << 8) | packet[13];
        if (ethertype == ETH_P_ARP) {
            network_handle_arp(packet, packet_len);
        } else if (ethertype == ETH_P_IP) {
            network_handle_ip(packet + 14, packet_len - 14);
        }
    }
}

static bool network_send_ipv4(uint32_t dest_ip, uint8_t protocol, const uint8_t* payload, size_t payload_len) {
    uint8_t packet[1514];
    size_t ip_header_len = 20;
    size_t total_len = ip_header_len + payload_len;
    write_eth_header(packet, arp_peer_mac, ETH_P_IP);
    write_ip_header(packet + 14, total_len, protocol, local_ip, dest_ip);
    for (size_t i = 0; i < payload_len; i++) {
        packet[14 + ip_header_len + i] = payload[i];
    }
    return rtl8139_send_frame(packet, ETH_P_IP, packet + 14, total_len);
}

static bool network_send_dhcp_packet(const uint8_t* data, size_t data_len) {
    uint8_t ip_packet[1514];
    size_t udp_len = 8 + data_len;
    write_eth_header(ip_packet, (uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, ETH_P_IP);
    write_ip_header(ip_packet + 14, 20 + udp_len, IP_PROTO_UDP, 0, 0xFFFFFFFF);
    ip_packet[34] = 0;
    ip_packet[35] = DHCP_CLIENT_PORT;
    ip_packet[36] = 0;
    ip_packet[37] = DHCP_SERVER_PORT;
    ip_packet[38] = (uint8_t)((udp_len >> 8) & 0xFF);
    ip_packet[39] = (uint8_t)(udp_len & 0xFF);
    ip_packet[40] = 0;
    ip_packet[41] = 0;
    for (size_t i = 0; i < data_len; i++) {
        ip_packet[42 + i] = data[i];
    }
    return rtl8139_send_frame(ip_packet, ETH_P_IP, ip_packet + 14, 20 + udp_len);
}

static bool network_dhcp_discover(void) {
    uint8_t packet[300];
    size_t length = 0;
    build_dhcp_discover(packet, &length);
    return network_send_dhcp_packet(packet, length);
}

static bool network_dhcp_request(void) {
    uint8_t packet[300];
    size_t length = 0;
    build_dhcp_request(packet, &length);
    return network_send_dhcp_packet(packet, length);
}

bool network_initialize(void) {
    pci_device_t nic;
    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &nic)) {
        KLOG("No Realtek RTL8139 network device detected");
        return false;
    }

    uint32_t command = pci_read_config_dword(nic.bus, nic.device, nic.function, 0x04);
    command |= 0x00000005u; // I/O space + bus mastering
    pci_write_config_dword(nic.bus, nic.device, nic.function, 0x04, command);

    io_base = (uint16_t)(nic.bar0 & ~0x03);
    if (io_base == 0) {
        KLOG("RTL8139 I/O base address invalid");
        return false;
    }

    rtl8139_read_mac();
    rtl8139_setup();

    KLOG("RTL8139 initialized with MAC %02x:%02x:%02x:%02x:%02x:%02x",
         local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);

    for (int i = 0; i < 3; i++) {
        network_dhcp_discover();
        for (int j = 0; j < 50000; j++) {
            rtl8139_process_rx();
            if (dhcp_acquired) {
                break;
            }
        }
        if (dhcp_acquired) {
            break;
        }
    }

    if (!dhcp_acquired) {
        KLOG("DHCP lease acquisition failed");
        return false;
    }

    network_dhcp_request();
    for (int j = 0; j < 50000; j++) {
        rtl8139_process_rx();
    }

    if (!dhcp_acquired) {
        KLOG("DHCP request failed");
        return false;
    }

    KLOG("Network configured: IP %u.%u.%u.%u Gateway %u.%u.%u.%u",
         (local_ip >> 24) & 0xFF, (local_ip >> 16) & 0xFF, (local_ip >> 8) & 0xFF, local_ip & 0xFF,
         (local_gateway >> 24) & 0xFF, (local_gateway >> 16) & 0xFF, (local_gateway >> 8) & 0xFF, local_gateway & 0xFF);

    network_ready = true;
    return true;
}

void network_poll(void) {
    if (!network_ready) {
        return;
    }
    rtl8139_process_rx();
}

uint32_t network_get_ip(void) {
    return local_ip;
}

uint32_t network_get_gateway(void) {
    return local_gateway;
}

const uint8_t* network_get_mac(void) {
    return local_mac;
}
