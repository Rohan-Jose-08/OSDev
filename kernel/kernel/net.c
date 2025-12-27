#include <kernel/net.h>
#include <kernel/pci.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/io.h>
#include <kernel/memory.h>
#include <kernel/timer.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ETH_ADDR_LEN 6
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV4 0x0800
#define ETH_MIN_FRAME 60
#define ETH_MAX_PAYLOAD 1500
#define ETH_HEADER_LEN 14
#define ETH_MAX_FRAME (ETH_HEADER_LEN + ETH_MAX_PAYLOAD)

#define ARP_TABLE_SIZE 16

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL8139_RX_BUF_SIZE (8192 + 16 + 1500)
#define RTL8139_TX_BUF_SIZE 1536

#define RTL8139_REG_IDR0 0x00
#define RTL8139_REG_TSD0 0x10
#define RTL8139_REG_TSAD0 0x20
#define RTL8139_REG_RBSTART 0x30
#define RTL8139_REG_CAPR 0x38
#define RTL8139_REG_IMR 0x3C
#define RTL8139_REG_ISR 0x3E
#define RTL8139_REG_TCR 0x40
#define RTL8139_REG_RCR 0x44
#define RTL8139_REG_CMD 0x37

#define RTL8139_CMD_RESET 0x10
#define RTL8139_CMD_RXTX_ENABLE 0x0C

#define RTL8139_ISR_ROK 0x0001
#define RTL8139_ISR_RER 0x0002
#define RTL8139_ISR_TOK 0x0004
#define RTL8139_ISR_TER 0x0008

#define RTL8139_RCR_ACCEPT_ALL 0x0000000F
#define RTL8139_RCR_WRAP 0x00000080

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY 0

#define ICMP_PAYLOAD_SIZE 32
#define TIMER_TICK_MS 10

#define UDP_PROTOCOL 17
#define UDP_HEADER_LEN 8
#define UDP_PAYLOAD_MAX 512
#define UDP_SOCKETS_MAX 4
#define UDP_QUEUE_LEN 4

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC_COOKIE 0x63825363
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER 2
#define DHCP_MSG_REQUEST 3
#define DHCP_MSG_ACK 5
#define DHCP_OPTION_SUBNET 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_REQ_IP 50
#define DHCP_OPTION_LEASE 51
#define DHCP_OPTION_MSG_TYPE 53
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_PARAM_REQ 55
#define DHCP_OPTION_END 255

typedef struct {
    uint8_t dst[ETH_ADDR_LEN];
    uint8_t src[ETH_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed)) eth_header_t;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[ETH_ADDR_LEN];
    uint8_t spa[4];
    uint8_t tha[ETH_ADDR_LEN];
    uint8_t tpa[4];
} __attribute__((packed)) arp_packet_t;

typedef struct {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t src[4];
    uint8_t dst[4];
} __attribute__((packed)) ipv4_header_t;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_header_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t ciaddr[4];
    uint8_t yiaddr[4];
    uint8_t siaddr[4];
    uint8_t giaddr[4];
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
} __attribute__((packed)) dhcp_header_t;

typedef struct {
    uint8_t src[4];
    uint8_t dst[4];
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} __attribute__((packed)) udp_pseudo_header_t;

typedef struct {
    uint16_t len;
    uint8_t src_ip[4];
    uint16_t src_port;
    uint8_t payload[UDP_PAYLOAD_MAX];
} udp_packet_t;

typedef struct {
    bool in_use;
    uint16_t port;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    udp_packet_t queue[UDP_QUEUE_LEN];
} udp_socket_t;

typedef struct {
    bool valid;
    uint8_t ip[4];
    uint8_t mac[ETH_ADDR_LEN];
} arp_entry_t;

typedef struct {
    uint16_t io_base;
    uint8_t irq_line;
    uint32_t rx_offset;
    bool initialized;
} rtl8139_state_t;

static const uint8_t net_default_ip[4] = {10, 0, 2, 15};
static const uint8_t net_default_netmask[4] = {255, 255, 255, 0};
static const uint8_t net_default_gateway[4] = {10, 0, 2, 2};
static const uint8_t net_default_dns[4] = {10, 0, 2, 3};

static uint8_t net_ip_addr[4] = {0, 0, 0, 0};
static uint8_t net_netmask[4] = {0, 0, 0, 0};
static uint8_t net_gateway[4] = {0, 0, 0, 0};
static uint8_t net_dns[4] = {0, 0, 0, 0};

static uint8_t net_mac[ETH_ADDR_LEN];
static bool net_ready = false;
static bool net_configured = false;
static bool net_dhcp_active = false;

static arp_entry_t arp_table[ARP_TABLE_SIZE];
static uint8_t arp_next_slot = 0;

static rtl8139_state_t rtl8139;

static uint8_t rtl8139_rx_buffer[RTL8139_RX_BUF_SIZE] __attribute__((aligned(256)));
static uint8_t rtl8139_tx_buffers[4][RTL8139_TX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t rtl8139_tx_cur = 0;

static void rtl8139_rx_process(void);
static bool rtl8139_send(const uint8_t *data, uint16_t len);

static volatile bool ping_in_flight = false;
static volatile bool ping_got_reply = false;
static uint16_t ping_id = 0xBEEF;
static uint16_t ping_seq_counter = 0;
static uint16_t ping_seq_active = 0;
static uint8_t ping_target[4];
static volatile uint32_t ping_start_ticks = 0;
static volatile uint32_t ping_reply_ticks = 0;

static const uint16_t udp_default_src_port = 12345;
static udp_socket_t udp_sockets[UDP_SOCKETS_MAX];

static inline uint16_t net_htons(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint16_t net_ntohs(uint16_t value) {
    return net_htons(value);
}

static inline uint32_t net_htonl(uint32_t value) {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static inline uint32_t net_ntohl(uint32_t value) {
    return net_htonl(value);
}

static uint16_t net_checksum(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (uint16_t)((bytes[i] << 8) | bytes[i + 1]);
    }

    if (len & 1) {
        sum += (uint16_t)(bytes[len - 1] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return net_htons((uint16_t)(~sum));
}

static uint16_t net_udp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                                 const uint8_t *udp_packet, uint16_t udp_len) {
    uint8_t buffer[sizeof(udp_pseudo_header_t) + UDP_HEADER_LEN + UDP_PAYLOAD_MAX];
    udp_pseudo_header_t pseudo;

    if (udp_len > (uint16_t)(UDP_HEADER_LEN + UDP_PAYLOAD_MAX)) {
        return 0;
    }

    memcpy(pseudo.src, src_ip, 4);
    memcpy(pseudo.dst, dst_ip, 4);
    pseudo.zero = 0;
    pseudo.protocol = UDP_PROTOCOL;
    pseudo.length = net_htons(udp_len);

    size_t total = sizeof(pseudo) + udp_len;
    memcpy(buffer, &pseudo, sizeof(pseudo));
    memcpy(buffer + sizeof(pseudo), udp_packet, udp_len);

    return net_checksum(buffer, total);
}

static udp_socket_t *udp_socket_find(uint16_t port) {
    for (int i = 0; i < UDP_SOCKETS_MAX; i++) {
        if (udp_sockets[i].in_use && udp_sockets[i].port == port) {
            return &udp_sockets[i];
        }
    }
    return NULL;
}

static udp_socket_t *udp_socket_alloc(uint16_t port) {
    udp_socket_t *sock = udp_socket_find(port);
    if (sock) {
        return sock;
    }

    for (int i = 0; i < UDP_SOCKETS_MAX; i++) {
        if (!udp_sockets[i].in_use) {
            udp_sockets[i].in_use = true;
            udp_sockets[i].port = port;
            udp_sockets[i].head = 0;
            udp_sockets[i].tail = 0;
            udp_sockets[i].count = 0;
            return &udp_sockets[i];
        }
    }

    return NULL;
}

static bool udp_socket_queue_push(udp_socket_t *sock, const uint8_t *payload, uint16_t len,
                                  const uint8_t src_ip[4], uint16_t src_port) {
    if (!sock || sock->count >= UDP_QUEUE_LEN) {
        return false;
    }

    if (len > UDP_PAYLOAD_MAX) {
        len = UDP_PAYLOAD_MAX;
    }

    udp_packet_t *pkt = &sock->queue[sock->tail];
    pkt->len = len;
    memcpy(pkt->payload, payload, len);
    memcpy(pkt->src_ip, src_ip, 4);
    pkt->src_port = src_port;

    sock->tail = (uint8_t)((sock->tail + 1) % UDP_QUEUE_LEN);
    sock->count++;
    return true;
}

static bool udp_socket_queue_pop(udp_socket_t *sock, uint8_t *payload, uint16_t *len,
                                 uint8_t src_ip[4], uint16_t *src_port) {
    if (!sock || sock->count == 0) {
        return false;
    }

    udp_packet_t *pkt = &sock->queue[sock->head];
    uint16_t copy_len = pkt->len;
    if (payload && len) {
        if (*len < copy_len) {
            copy_len = *len;
        }
        memcpy(payload, pkt->payload, copy_len);
        *len = copy_len;
    } else if (len) {
        *len = copy_len;
    }

    if (src_ip) {
        memcpy(src_ip, pkt->src_ip, 4);
    }
    if (src_port) {
        *src_port = pkt->src_port;
    }

    sock->head = (uint8_t)((sock->head + 1) % UDP_QUEUE_LEN);
    sock->count--;
    return true;
}

static bool net_mac_equal(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, ETH_ADDR_LEN) == 0;
}

static bool net_ip_equal(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 4) == 0;
}

static bool net_is_broadcast_mac(const uint8_t *mac) {
    for (int i = 0; i < ETH_ADDR_LEN; i++) {
        if (mac[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

static bool net_ip_is_zero(const uint8_t ip[4]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static bool net_ip_is_broadcast(const uint8_t ip[4]) {
    return ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255;
}

static void net_print_ip(const uint8_t ip[4]) {
    printf("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void net_print_mac(const uint8_t mac[ETH_ADDR_LEN]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < ETH_ADDR_LEN; i++) {
        uint8_t byte = mac[i];
        printf("%c%c", hex[byte >> 4], hex[byte & 0x0F]);
        if (i + 1 < ETH_ADDR_LEN) {
            printf(":");
        }
    }
}

static void arp_update(const uint8_t ip[4], const uint8_t mac[ETH_ADDR_LEN]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && net_ip_equal(arp_table[i].ip, ip)) {
            memcpy(arp_table[i].mac, mac, ETH_ADDR_LEN);
            return;
        }
    }

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            arp_table[i].valid = true;
            memcpy(arp_table[i].ip, ip, 4);
            memcpy(arp_table[i].mac, mac, ETH_ADDR_LEN);
            return;
        }
    }

    arp_table[arp_next_slot].valid = true;
    memcpy(arp_table[arp_next_slot].ip, ip, 4);
    memcpy(arp_table[arp_next_slot].mac, mac, ETH_ADDR_LEN);
    arp_next_slot = (uint8_t)((arp_next_slot + 1) % ARP_TABLE_SIZE);
}

static bool arp_lookup(const uint8_t ip[4], uint8_t mac_out[ETH_ADDR_LEN]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && net_ip_equal(arp_table[i].ip, ip)) {
            memcpy(mac_out, arp_table[i].mac, ETH_ADDR_LEN);
            return true;
        }
    }
    return false;
}

static bool net_send_frame(const uint8_t dst_mac[ETH_ADDR_LEN], uint16_t ethertype,
                           const uint8_t *payload, uint16_t payload_len) {
    if (!net_ready || payload_len > ETH_MAX_PAYLOAD) {
        return false;
    }

    uint8_t frame[ETH_MAX_FRAME];
    eth_header_t *eth = (eth_header_t *)frame;

    memcpy(eth->dst, dst_mac, ETH_ADDR_LEN);
    memcpy(eth->src, net_mac, ETH_ADDR_LEN);
    eth->ethertype = net_htons(ethertype);

    memcpy(frame + sizeof(eth_header_t), payload, payload_len);

    uint16_t frame_len = (uint16_t)(sizeof(eth_header_t) + payload_len);
    if (frame_len < ETH_MIN_FRAME) {
        memset(frame + frame_len, 0, (size_t)(ETH_MIN_FRAME - frame_len));
        frame_len = ETH_MIN_FRAME;
    }

    return rtl8139_send(frame, frame_len);
}

static bool net_ip_is_local(const uint8_t ip[4]) {
    if (net_ip_is_zero(net_ip_addr) || net_ip_is_zero(net_netmask)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if ((uint8_t)(ip[i] & net_netmask[i]) != (uint8_t)(net_ip_addr[i] & net_netmask[i])) {
            return false;
        }
    }
    return true;
}

static bool net_send_arp_request(const uint8_t target_ip[4]) {
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));

    arp.htype = net_htons(1);
    arp.ptype = net_htons(ETH_TYPE_IPV4);
    arp.hlen = ETH_ADDR_LEN;
    arp.plen = 4;
    arp.oper = net_htons(1);
    memcpy(arp.sha, net_mac, ETH_ADDR_LEN);
    memcpy(arp.spa, net_ip_addr, 4);
    memcpy(arp.tpa, target_ip, 4);

    uint8_t broadcast[ETH_ADDR_LEN];
    memset(broadcast, 0xFF, sizeof(broadcast));

    return net_send_frame(broadcast, ETH_TYPE_ARP, (const uint8_t *)&arp, sizeof(arp));
}

static bool net_send_arp_reply(const uint8_t dst_mac[ETH_ADDR_LEN], const uint8_t dst_ip[4]) {
    arp_packet_t arp;
    memset(&arp, 0, sizeof(arp));

    arp.htype = net_htons(1);
    arp.ptype = net_htons(ETH_TYPE_IPV4);
    arp.hlen = ETH_ADDR_LEN;
    arp.plen = 4;
    arp.oper = net_htons(2);
    memcpy(arp.sha, net_mac, ETH_ADDR_LEN);
    memcpy(arp.spa, net_ip_addr, 4);
    memcpy(arp.tha, dst_mac, ETH_ADDR_LEN);
    memcpy(arp.tpa, dst_ip, 4);

    return net_send_frame(dst_mac, ETH_TYPE_ARP, (const uint8_t *)&arp, sizeof(arp));
}

static bool net_send_ipv4_raw(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                              const uint8_t dst_mac[ETH_ADDR_LEN], uint8_t protocol,
                              const uint8_t *payload, uint16_t payload_len) {
    if (payload_len > (ETH_MAX_PAYLOAD - sizeof(ipv4_header_t))) {
        return false;
    }

    uint8_t ip_packet[sizeof(ipv4_header_t) + ETH_MAX_PAYLOAD];
    ipv4_header_t *ip = (ipv4_header_t *)ip_packet;
    memset(ip, 0, sizeof(*ip));

    ip->ver_ihl = (uint8_t)((4 << 4) | 5);
    ip->tos = 0;
    ip->total_length = net_htons((uint16_t)(sizeof(ipv4_header_t) + payload_len));
    ip->id = net_htons(0);
    ip->flags_frag = net_htons(0);
    ip->ttl = 64;
    ip->protocol = protocol;
    memcpy(ip->src, src_ip, 4);
    memcpy(ip->dst, dst_ip, 4);

    memcpy(ip_packet + sizeof(ipv4_header_t), payload, payload_len);
    ip->checksum = 0;
    ip->checksum = net_checksum(ip, sizeof(ipv4_header_t));

    return net_send_frame(dst_mac, ETH_TYPE_IPV4, ip_packet,
                          (uint16_t)(sizeof(ipv4_header_t) + payload_len));
}

static bool net_send_ipv4(const uint8_t dst_ip[4], uint8_t protocol,
                          const uint8_t *payload, uint16_t payload_len) {
    if (net_ip_is_zero(net_ip_addr)) {
        return false;
    }

    uint8_t next_hop_ip[4];
    if (net_ip_is_local(dst_ip)) {
        memcpy(next_hop_ip, dst_ip, 4);
    } else {
        memcpy(next_hop_ip, net_gateway, 4);
    }

    uint8_t dst_mac[ETH_ADDR_LEN];
    if (!arp_lookup(next_hop_ip, dst_mac)) {
        net_send_arp_request(next_hop_ip);
        return false;
    }

    return net_send_ipv4_raw(net_ip_addr, dst_ip, dst_mac, protocol, payload, payload_len);
}

static void net_set_config(const uint8_t ip[4], const uint8_t netmask[4],
                           const uint8_t gateway[4], const uint8_t dns[4]) {
    memcpy(net_ip_addr, ip, 4);
    memcpy(net_netmask, netmask, 4);
    memcpy(net_gateway, gateway, 4);
    memcpy(net_dns, dns, 4);
    net_configured = true;
}

static void net_set_defaults(void) {
    net_set_config(net_default_ip, net_default_netmask, net_default_gateway, net_default_dns);
    net_dhcp_active = false;
}

static void dhcp_write_option(uint8_t *buf, size_t *offset, uint8_t opt,
                              const uint8_t *data, uint8_t len, size_t max_len) {
    if (!buf || !offset) {
        return;
    }
    if (*offset + 2 + len > max_len) {
        return;
    }
    buf[(*offset)++] = opt;
    buf[(*offset)++] = len;
    if (len > 0 && data) {
        memcpy(&buf[*offset], data, len);
        *offset += len;
    }
}

static bool dhcp_parse_options(const uint8_t *options, size_t len, uint8_t *msg_type,
                               uint8_t server_id[4], bool *has_server,
                               uint8_t subnet[4], bool *has_subnet,
                               uint8_t router[4], bool *has_router,
                               uint8_t dns[4], bool *has_dns) {
    size_t i = 0;
    while (i < len) {
        uint8_t opt = options[i++];
        if (opt == 0) {
            continue;
        }
        if (opt == DHCP_OPTION_END) {
            return true;
        }
        if (i >= len) {
            return false;
        }
        uint8_t opt_len = options[i++];
        if (i + opt_len > len) {
            return false;
        }
        const uint8_t *data = &options[i];
        if (opt == DHCP_OPTION_MSG_TYPE && opt_len >= 1 && msg_type) {
            *msg_type = data[0];
        } else if (opt == DHCP_OPTION_SERVER_ID && opt_len >= 4 && server_id) {
            memcpy(server_id, data, 4);
            if (has_server) {
                *has_server = true;
            }
        } else if (opt == DHCP_OPTION_SUBNET && opt_len >= 4 && subnet) {
            memcpy(subnet, data, 4);
            if (has_subnet) {
                *has_subnet = true;
            }
        } else if (opt == DHCP_OPTION_ROUTER && opt_len >= 4 && router) {
            memcpy(router, data, 4);
            if (has_router) {
                *has_router = true;
            }
        } else if (opt == DHCP_OPTION_DNS && opt_len >= 4 && dns) {
            memcpy(dns, data, 4);
            if (has_dns) {
                *has_dns = true;
            }
        }
        i += opt_len;
    }
    return true;
}

static bool dhcp_receive(uint32_t xid, uint8_t expected_type, uint32_t timeout_ms,
                         uint8_t yiaddr[4], uint8_t server_id[4],
                         uint8_t subnet[4], uint8_t router[4], uint8_t dns[4]) {
    uint32_t remaining = timeout_ms;
    uint8_t payload[512];
    uint16_t payload_len = sizeof(payload);
    uint8_t src_ip[4];
    uint16_t src_port = 0;

    while (remaining > 0) {
        payload_len = sizeof(payload);
        if (net_udp_recv(DHCP_CLIENT_PORT, payload, &payload_len, src_ip, &src_port)) {
            if (src_port != DHCP_SERVER_PORT || payload_len < sizeof(dhcp_header_t) + 4) {
                goto next_wait;
            }
            dhcp_header_t *hdr = (dhcp_header_t *)payload;
            if (hdr->op != 2 || net_ntohl(hdr->xid) != xid || hdr->hlen != ETH_ADDR_LEN) {
                goto next_wait;
            }
            if (memcmp(hdr->chaddr, net_mac, ETH_ADDR_LEN) != 0) {
                goto next_wait;
            }
            uint32_t cookie = ((uint32_t)payload[sizeof(dhcp_header_t)] << 24) |
                              ((uint32_t)payload[sizeof(dhcp_header_t) + 1] << 16) |
                              ((uint32_t)payload[sizeof(dhcp_header_t) + 2] << 8) |
                              (uint32_t)payload[sizeof(dhcp_header_t) + 3];
            if (cookie != DHCP_MAGIC_COOKIE) {
                goto next_wait;
            }

            uint8_t msg_type = 0;
            bool has_server = false;
            bool has_subnet = false;
            bool has_router = false;
            bool has_dns = false;
            uint8_t opt_server[4] = {0};
            uint8_t opt_subnet[4] = {0};
            uint8_t opt_router[4] = {0};
            uint8_t opt_dns[4] = {0};
            const uint8_t *opts = payload + sizeof(dhcp_header_t) + 4;
            size_t opts_len = payload_len - sizeof(dhcp_header_t) - 4;
            if (!dhcp_parse_options(opts, opts_len, &msg_type,
                                    opt_server, &has_server,
                                    opt_subnet, &has_subnet,
                                    opt_router, &has_router,
                                    opt_dns, &has_dns)) {
                goto next_wait;
            }
            if (msg_type != expected_type) {
                goto next_wait;
            }

            memcpy(yiaddr, hdr->yiaddr, 4);
            if (server_id) {
                if (has_server) {
                    memcpy(server_id, opt_server, 4);
                } else {
                    memcpy(server_id, src_ip, 4);
                }
            }
            if (subnet) {
                if (has_subnet) {
                    memcpy(subnet, opt_subnet, 4);
                } else {
                    memcpy(subnet, net_default_netmask, 4);
                }
            }
            if (router) {
                if (has_router) {
                    memcpy(router, opt_router, 4);
                } else {
                    memcpy(router, net_default_gateway, 4);
                }
            }
            if (dns) {
                if (has_dns) {
                    memcpy(dns, opt_dns, 4);
                } else {
                    memcpy(dns, net_default_dns, 4);
                }
            }
            return true;
        }
next_wait:
        if (remaining > 50) {
            timer_sleep_ms(50);
            remaining -= 50;
        } else {
            break;
        }
    }

    return false;
}

static bool net_dhcp_configure(void) {
    if (!net_udp_listen(DHCP_CLIENT_PORT)) {
        return false;
    }

    uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    uint8_t zero_ip[4] = {0, 0, 0, 0};
    uint8_t broadcast_mac[ETH_ADDR_LEN];
    memset(broadcast_mac, 0xFF, sizeof(broadcast_mac));

    uint32_t xid = 0xA5A50000u | (timer_get_ticks() & 0xFFFF);

    uint8_t discover[300];
    memset(discover, 0, sizeof(discover));
    dhcp_header_t *hdr = (dhcp_header_t *)discover;
    hdr->op = 1;
    hdr->htype = 1;
    hdr->hlen = ETH_ADDR_LEN;
    hdr->xid = net_htonl(xid);
    hdr->flags = net_htons(0x8000);
    memcpy(hdr->chaddr, net_mac, ETH_ADDR_LEN);

    size_t offset = sizeof(dhcp_header_t);
    discover[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE >> 24);
    discover[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE >> 16);
    discover[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE >> 8);
    discover[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE);

    uint8_t msg = DHCP_MSG_DISCOVER;
    dhcp_write_option(discover, &offset, DHCP_OPTION_MSG_TYPE, &msg, 1, sizeof(discover));
    uint8_t params[] = {DHCP_OPTION_SUBNET, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS};
    dhcp_write_option(discover, &offset, DHCP_OPTION_PARAM_REQ, params, sizeof(params), sizeof(discover));
    discover[offset++] = DHCP_OPTION_END;

    uint16_t discover_len = (uint16_t)offset;
    uint16_t udp_len = (uint16_t)(UDP_HEADER_LEN + discover_len);
    uint8_t udp_packet[UDP_HEADER_LEN + 300];
    udp_header_t *udp = (udp_header_t *)udp_packet;
    udp->src_port = net_htons(DHCP_CLIENT_PORT);
    udp->dst_port = net_htons(DHCP_SERVER_PORT);
    udp->length = net_htons(udp_len);
    udp->checksum = 0;
    memcpy(udp_packet + UDP_HEADER_LEN, discover, discover_len);
    uint16_t csum = net_udp_checksum(zero_ip, broadcast_ip, udp_packet, udp_len);
    if (csum == 0) {
        csum = 0xFFFF;
    }
    udp->checksum = csum;

    if (!net_send_ipv4_raw(zero_ip, broadcast_ip, broadcast_mac, UDP_PROTOCOL,
                           udp_packet, udp_len)) {
        return false;
    }

    uint8_t offered_ip[4] = {0};
    uint8_t server_id[4] = {0};
    uint8_t subnet[4] = {0};
    uint8_t router[4] = {0};
    uint8_t dns[4] = {0};

    if (!dhcp_receive(xid, DHCP_MSG_OFFER, 3000, offered_ip, server_id, subnet, router, dns)) {
        return false;
    }

    uint8_t request[300];
    memset(request, 0, sizeof(request));
    hdr = (dhcp_header_t *)request;
    hdr->op = 1;
    hdr->htype = 1;
    hdr->hlen = ETH_ADDR_LEN;
    hdr->xid = net_htonl(xid);
    hdr->flags = net_htons(0x8000);
    memcpy(hdr->chaddr, net_mac, ETH_ADDR_LEN);

    offset = sizeof(dhcp_header_t);
    request[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE >> 24);
    request[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE >> 16);
    request[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE >> 8);
    request[offset++] = (uint8_t)(DHCP_MAGIC_COOKIE);

    msg = DHCP_MSG_REQUEST;
    dhcp_write_option(request, &offset, DHCP_OPTION_MSG_TYPE, &msg, 1, sizeof(request));
    dhcp_write_option(request, &offset, DHCP_OPTION_REQ_IP, offered_ip, 4, sizeof(request));
    dhcp_write_option(request, &offset, DHCP_OPTION_SERVER_ID, server_id, 4, sizeof(request));
    dhcp_write_option(request, &offset, DHCP_OPTION_PARAM_REQ, params, sizeof(params), sizeof(request));
    request[offset++] = DHCP_OPTION_END;

    uint16_t request_len = (uint16_t)offset;
    udp_len = (uint16_t)(UDP_HEADER_LEN + request_len);
    memset(udp_packet, 0, sizeof(udp_packet));
    udp = (udp_header_t *)udp_packet;
    udp->src_port = net_htons(DHCP_CLIENT_PORT);
    udp->dst_port = net_htons(DHCP_SERVER_PORT);
    udp->length = net_htons(udp_len);
    udp->checksum = 0;
    memcpy(udp_packet + UDP_HEADER_LEN, request, request_len);
    csum = net_udp_checksum(zero_ip, broadcast_ip, udp_packet, udp_len);
    if (csum == 0) {
        csum = 0xFFFF;
    }
    udp->checksum = csum;

    if (!net_send_ipv4_raw(zero_ip, broadcast_ip, broadcast_mac, UDP_PROTOCOL,
                           udp_packet, udp_len)) {
        return false;
    }

    if (!dhcp_receive(xid, DHCP_MSG_ACK, 3000, offered_ip, server_id, subnet, router, dns)) {
        return false;
    }

    net_set_config(offered_ip, subnet, router, dns);
    net_dhcp_active = true;
    return true;
}

bool net_udp_send(const uint8_t dst_ip[4], uint16_t dst_port, const uint8_t *payload, uint16_t len) {
    if (!dst_ip || !payload || len == 0) {
        return false;
    }
    if (len > UDP_PAYLOAD_MAX) {
        return false;
    }

    uint8_t packet[UDP_HEADER_LEN + UDP_PAYLOAD_MAX];
    udp_header_t *udp = (udp_header_t *)packet;
    udp->src_port = net_htons(udp_default_src_port);
    udp->dst_port = net_htons(dst_port);
    uint16_t udp_len = (uint16_t)(UDP_HEADER_LEN + len);
    udp->length = net_htons(udp_len);
    udp->checksum = 0;

    memcpy(packet + UDP_HEADER_LEN, payload, len);
    uint16_t checksum = net_udp_checksum(net_ip_addr, dst_ip, packet, udp_len);
    if (checksum == 0) {
        checksum = 0xFFFF;
    }
    udp->checksum = checksum;

    for (int attempt = 0; attempt < 2; attempt++) {
        if (net_send_ipv4(dst_ip, UDP_PROTOCOL, packet, udp_len)) {
            return true;
        }
        timer_sleep_ms(100);
    }

    return false;
}

static void net_handle_arp(const uint8_t *payload, uint16_t len,
                           const uint8_t src_mac[ETH_ADDR_LEN]) {
    if (len < sizeof(arp_packet_t)) {
        return;
    }

    const arp_packet_t *arp = (const arp_packet_t *)payload;
    if (net_ntohs(arp->htype) != 1 || net_ntohs(arp->ptype) != ETH_TYPE_IPV4) {
        return;
    }
    if (arp->hlen != ETH_ADDR_LEN || arp->plen != 4) {
        return;
    }

    arp_update(arp->spa, arp->sha);

    if (!net_ip_is_zero(net_ip_addr) &&
        net_ntohs(arp->oper) == 1 &&
        net_ip_equal(arp->tpa, net_ip_addr)) {
        net_send_arp_reply(src_mac, arp->spa);
    }
}

static void net_handle_ipv4(const uint8_t *payload, uint16_t len,
                            const uint8_t src_mac[ETH_ADDR_LEN]) {
    if (len < sizeof(ipv4_header_t)) {
        return;
    }

    const ipv4_header_t *ip = (const ipv4_header_t *)payload;
    uint8_t version = (uint8_t)(ip->ver_ihl >> 4);
    uint8_t ihl = (uint8_t)(ip->ver_ihl & 0x0F);
    if (version != 4 || ihl < 5) {
        return;
    }

    uint16_t header_len = (uint16_t)(ihl * 4);
    if (len < header_len) {
        return;
    }

    uint16_t total_len = net_ntohs(ip->total_length);
    if (total_len > len) {
        total_len = len;
    }
    if (total_len < header_len) {
        return;
    }

    if (!net_ip_equal(ip->dst, net_ip_addr)) {
        if (!net_ip_is_zero(net_ip_addr) && !net_ip_is_broadcast(ip->dst)) {
            return;
        }
    }

    arp_update(ip->src, src_mac);

    if (ip->protocol == 1) {
        uint16_t icmp_len = (uint16_t)(total_len - header_len);
        if (icmp_len < sizeof(icmp_header_t)) {
            return;
        }
        if (icmp_len > 1024) {
            return;
        }

        const uint8_t *icmp_payload = payload + header_len;
        const icmp_header_t *icmp = (const icmp_header_t *)icmp_payload;
        if (icmp->code != 0) {
            return;
        }

        if (icmp->type == ICMP_ECHO_REQUEST) {
            uint8_t reply[1024];
            memcpy(reply, icmp_payload, icmp_len);
            reply[0] = ICMP_ECHO_REPLY;
            reply[1] = 0;
            reply[2] = 0;
            reply[3] = 0;
            uint16_t csum = net_checksum(reply, icmp_len);
            memcpy(reply + 2, &csum, sizeof(csum));

            net_send_ipv4(ip->src, 1, reply, icmp_len);
        } else if (icmp->type == ICMP_ECHO_REPLY) {
            if (ping_in_flight &&
                net_ip_equal(ip->src, ping_target) &&
                net_ntohs(icmp->id) == ping_id &&
                net_ntohs(icmp->seq) == ping_seq_active) {
                ping_got_reply = true;
                ping_reply_ticks = timer_get_ticks();
            }
        }
    } else if (ip->protocol == UDP_PROTOCOL) {
        uint16_t udp_total = (uint16_t)(total_len - header_len);
        if (udp_total < UDP_HEADER_LEN) {
            return;
        }

        const udp_header_t *udp = (const udp_header_t *)(payload + header_len);
        uint16_t udp_len = net_ntohs(udp->length);
        if (udp_len < UDP_HEADER_LEN || udp_len > udp_total) {
            return;
        }
        if (udp_len > (uint16_t)(UDP_HEADER_LEN + UDP_PAYLOAD_MAX)) {
            return;
        }

        uint16_t dst_port = net_ntohs(udp->dst_port);
        udp_socket_t *sock = udp_socket_find(dst_port);
        if (!sock) {
            return;
        }

        if (udp->checksum != 0) {
            uint16_t check = net_udp_checksum(ip->src, ip->dst,
                                              (const uint8_t *)udp, udp_len);
            if (check != 0) {
                return;
            }
        }

        uint16_t data_len = (uint16_t)(udp_len - UDP_HEADER_LEN);
        if (data_len > UDP_PAYLOAD_MAX) {
            data_len = UDP_PAYLOAD_MAX;
        }

        udp_socket_queue_push(sock, (const uint8_t *)udp + UDP_HEADER_LEN, data_len,
                              ip->src, net_ntohs(udp->src_port));
    }
}

static void net_handle_frame(const uint8_t *frame, uint16_t len) {
    if (len < sizeof(eth_header_t)) {
        return;
    }

    const eth_header_t *eth = (const eth_header_t *)frame;
    if (!net_mac_equal(eth->dst, net_mac) && !net_is_broadcast_mac(eth->dst)) {
        return;
    }

    uint16_t ethertype = net_ntohs(eth->ethertype);
    const uint8_t *payload = frame + sizeof(eth_header_t);
    uint16_t payload_len = (uint16_t)(len - sizeof(eth_header_t));

    if (ethertype == ETH_TYPE_ARP) {
        net_handle_arp(payload, payload_len, eth->src);
    } else if (ethertype == ETH_TYPE_IPV4) {
        net_handle_ipv4(payload, payload_len, eth->src);
    }
}

static void rtl8139_irq(uint8_t irq) {
    (void)irq;
    if (!rtl8139.initialized) {
        return;
    }

    uint16_t status = inw((uint16_t)(rtl8139.io_base + RTL8139_REG_ISR));
    if (!status) {
        return;
    }

    outw((uint16_t)(rtl8139.io_base + RTL8139_REG_ISR), status);

    if (status & (RTL8139_ISR_ROK | RTL8139_ISR_RER)) {
        rtl8139_rx_process();
    }
}

static void rtl8139_rx_process(void) {
    if (!rtl8139.initialized) {
        return;
    }

    while ((inb((uint16_t)(rtl8139.io_base + RTL8139_REG_CMD)) & 0x01) == 0) {
        uint32_t offset = rtl8139.rx_offset;
        uint16_t pkt_status = *(uint16_t *)(rtl8139_rx_buffer + offset);
        uint16_t pkt_len = *(uint16_t *)(rtl8139_rx_buffer + offset + 2);

        if ((pkt_status & 0x01) == 0 || pkt_len < 4) {
            rtl8139.rx_offset = (rtl8139.rx_offset + 4) % RTL8139_RX_BUF_SIZE;
            outw((uint16_t)(rtl8139.io_base + RTL8139_REG_CAPR),
                 (uint16_t)((rtl8139.rx_offset - 0x10) & 0xFFFF));
            continue;
        }

        uint16_t copy_len = pkt_len;
        if (copy_len > (ETH_MAX_FRAME + 4)) {
            copy_len = (uint16_t)(ETH_MAX_FRAME + 4);
        }

        uint16_t data_len = (uint16_t)(copy_len - 4);

        uint8_t packet[ETH_MAX_FRAME + 4];
        if (offset + copy_len > RTL8139_RX_BUF_SIZE) {
            uint32_t first = RTL8139_RX_BUF_SIZE - offset;
            if (first > copy_len) {
                first = copy_len;
            }
            memcpy(packet, rtl8139_rx_buffer + offset, first);
            memcpy(packet + first, rtl8139_rx_buffer, copy_len - first);
        } else {
            memcpy(packet, rtl8139_rx_buffer + offset, copy_len);
        }

        net_handle_frame(packet + 4, data_len);

        uint32_t advance = (uint32_t)(pkt_len + 4 + 3) & ~3u;
        rtl8139.rx_offset = (rtl8139.rx_offset + advance) % RTL8139_RX_BUF_SIZE;
        outw((uint16_t)(rtl8139.io_base + RTL8139_REG_CAPR),
             (uint16_t)((rtl8139.rx_offset - 0x10) & 0xFFFF));
    }
}

static bool rtl8139_send(const uint8_t *data, uint16_t len) {
    if (!rtl8139.initialized || len == 0) {
        return false;
    }

    if (len > RTL8139_TX_BUF_SIZE) {
        return false;
    }

    uint16_t send_len = len;
    if (send_len < ETH_MIN_FRAME) {
        send_len = ETH_MIN_FRAME;
    }

    uint8_t slot = rtl8139_tx_cur;
    memcpy(rtl8139_tx_buffers[slot], data, len);
    if (send_len > len) {
        memset(rtl8139_tx_buffers[slot] + len, 0, (size_t)(send_len - len));
    }

    outl((uint16_t)(rtl8139.io_base + RTL8139_REG_TSD0 + slot * 4), send_len);

    rtl8139_tx_cur = (uint8_t)((rtl8139_tx_cur + 1) % 4);
    return true;
}

static void rtl8139_read_mac(uint8_t mac[ETH_ADDR_LEN]) {
    for (int i = 0; i < ETH_ADDR_LEN; i++) {
        mac[i] = inb((uint16_t)(rtl8139.io_base + RTL8139_REG_IDR0 + i));
    }
}

static bool rtl8139_init(const pci_device_t *dev) {
    if (!dev) {
        return false;
    }

    uint32_t bar0 = dev->bar[0];
    if ((bar0 & 0x1) == 0) {
        return false;
    }

    rtl8139.io_base = (uint16_t)(bar0 & ~0x3);
    rtl8139.irq_line = dev->irq_line;
    rtl8139.rx_offset = 0;

    pci_enable_bus_master(dev);

    outb((uint16_t)(rtl8139.io_base + RTL8139_REG_CMD), RTL8139_CMD_RESET);
    while (inb((uint16_t)(rtl8139.io_base + RTL8139_REG_CMD)) & RTL8139_CMD_RESET) {
    }

    uint32_t rx_phys = virt_to_phys(rtl8139_rx_buffer);
    outl((uint16_t)(rtl8139.io_base + RTL8139_REG_RBSTART), rx_phys);
    outw((uint16_t)(rtl8139.io_base + RTL8139_REG_CAPR), 0);

    for (int i = 0; i < 4; i++) {
        uint32_t tx_phys = virt_to_phys(rtl8139_tx_buffers[i]);
        outl((uint16_t)(rtl8139.io_base + RTL8139_REG_TSAD0 + i * 4), tx_phys);
    }

    outw((uint16_t)(rtl8139.io_base + RTL8139_REG_ISR), 0xFFFF);
    outw((uint16_t)(rtl8139.io_base + RTL8139_REG_IMR),
         (RTL8139_ISR_ROK | RTL8139_ISR_RER | RTL8139_ISR_TOK | RTL8139_ISR_TER));
    outl((uint16_t)(rtl8139.io_base + RTL8139_REG_RCR), RTL8139_RCR_ACCEPT_ALL | RTL8139_RCR_WRAP);
    outb((uint16_t)(rtl8139.io_base + RTL8139_REG_CMD), RTL8139_CMD_RXTX_ENABLE);

    rtl8139_read_mac(net_mac);
    rtl8139.initialized = true;

    if (rtl8139.irq_line < 16) {
        irq_register(rtl8139.irq_line, rtl8139_irq);
        IRQ_clear_mask(rtl8139.irq_line);
    } else {
        printf("RTL8139 IRQ line invalid (%u).\\n", rtl8139.irq_line);
    }

    return true;
}

void net_init(void) {
    pci_device_t dev;
    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &dev)) {
        printf("No RTL8139 NIC found.\n");
        return;
    }

    if (!rtl8139_init(&dev)) {
        printf("RTL8139 init failed.\n");
        return;
    }

    net_ready = true;
    printf("RTL8139 online. MAC=");
    net_print_mac(net_mac);
    printf("\n");

    if (!net_dhcp_configure()) {
        printf("DHCP failed, using static defaults.\n");
        net_set_defaults();
    } else {
        printf("DHCP configured.\n");
    }

    printf("IP=");
    net_print_ip(net_ip_addr);
    printf(" NETMASK=");
    net_print_ip(net_netmask);
    printf(" GW=");
    net_print_ip(net_gateway);
    printf("\n");
}

void net_print_info(void) {
    if (!rtl8139.initialized) {
        printf("Network device not initialized.\n");
        return;
    }

    printf("Driver: RTL8139\n");
    printf("IO base: 0x%X\n", rtl8139.io_base);
    printf("IRQ: %u\n", rtl8139.irq_line);
    printf("MAC: ");
    net_print_mac(net_mac);
    printf("\n");
    printf("IP: ");
    net_print_ip(net_ip_addr);
    printf("\n");
    printf("Netmask: ");
    net_print_ip(net_netmask);
    printf("\n");
    printf("Gateway: ");
    net_print_ip(net_gateway);
    printf("\n");
    printf("DNS: ");
    net_print_ip(net_dns);
    printf("\n");
    if (net_configured) {
        printf("Config: %s\n", net_dhcp_active ? "dhcp" : "static");
    } else {
        printf("Config: down\n");
    }
    printf("Stack: %s\n", net_ready ? "up" : "down");
}

void net_print_arp_table(void) {
    int count = 0;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            continue;
        }
        printf("%d: ", i);
        net_print_ip(arp_table[i].ip);
        printf(" -> ");
        net_print_mac(arp_table[i].mac);
        printf("\n");
        count++;
    }

    if (count == 0) {
        printf("ARP table empty.\n");
    }
}

bool net_ping(const uint8_t dst_ip[4], uint32_t timeout_ms, uint32_t *rtt_ms) {
    if (!net_ready || !dst_ip) {
        return false;
    }

    ping_seq_active = (uint16_t)(ping_seq_counter + 1);
    ping_seq_counter = ping_seq_active;
    memcpy(ping_target, dst_ip, 4);
    ping_got_reply = false;
    ping_in_flight = true;

    uint8_t packet[sizeof(icmp_header_t) + ICMP_PAYLOAD_SIZE];
    icmp_header_t *icmp = (icmp_header_t *)packet;
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = net_htons(ping_id);
    icmp->seq = net_htons(ping_seq_active);

    for (int i = 0; i < ICMP_PAYLOAD_SIZE; i++) {
        packet[sizeof(icmp_header_t) + i] = (uint8_t)i;
    }

    icmp->checksum = net_checksum(packet, sizeof(packet));

    bool sent = false;
    for (int attempt = 0; attempt < 2; attempt++) {
        ping_start_ticks = timer_get_ticks();
        if (net_send_ipv4(dst_ip, 1, packet, sizeof(packet))) {
            sent = true;
            break;
        }
        timer_sleep_ms(100);
    }

    if (!sent) {
        ping_in_flight = false;
        return false;
    }

    uint32_t remaining = timeout_ms;
    while (!ping_got_reply && remaining > 0) {
        uint32_t slice = (remaining > TIMER_TICK_MS) ? TIMER_TICK_MS : remaining;
        timer_sleep_ms(slice);
        remaining -= slice;
    }

    ping_in_flight = false;

    if (!ping_got_reply) {
        return false;
    }

    if (rtt_ms) {
        uint32_t ticks = ping_reply_ticks - ping_start_ticks;
        *rtt_ms = ticks * TIMER_TICK_MS;
    }

    return true;
}

bool net_udp_listen(uint16_t port) {
    if (port == 0) {
        return false;
    }
    udp_socket_t *sock = udp_socket_alloc(port);
    if (!sock) {
        return false;
    }

    sock->head = 0;
    sock->tail = 0;
    sock->count = 0;
    return true;
}

bool net_udp_recv(uint16_t port, uint8_t *payload, uint16_t *len,
                  uint8_t src_ip[4], uint16_t *src_port) {
    udp_socket_t *sock = udp_socket_find(port);
    if (!sock) {
        return false;
    }

    return udp_socket_queue_pop(sock, payload, len, src_ip, src_port);
}
