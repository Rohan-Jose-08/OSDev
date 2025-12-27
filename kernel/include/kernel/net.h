#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include <stdbool.h>
#include <stdint.h>

void net_init(void);
void net_print_info(void);
void net_print_arp_table(void);
bool net_ping(const uint8_t dst_ip[4], uint32_t timeout_ms, uint32_t *rtt_ms);
bool net_udp_send(const uint8_t dst_ip[4], uint16_t dst_port, const uint8_t *payload, uint16_t len);
bool net_udp_listen(uint16_t port);
bool net_udp_recv(uint16_t port, uint8_t *payload, uint16_t *len, uint8_t src_ip[4], uint16_t *src_port);

#endif
