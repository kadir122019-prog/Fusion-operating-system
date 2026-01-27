#ifndef NET_H
#define NET_H

#include "types.h"

void net_init(void);
void net_poll(void);
int net_is_up(void);
u32 net_get_ip(void);
u32 net_get_dns(void);
u32 net_get_netmask(void);
u32 net_get_gateway(void);
int net_dns_resolve(const char *host, u32 *out_ip);
int net_tcp_connect(u32 dest_ip, u16 dest_port);
int net_tcp_is_established(void);
int net_tcp_send(const u8 *data, u16 len);
int net_tcp_recv(u8 *out, u16 max);
int net_tcp_is_closed(void);
void net_tcp_close(void);

#endif
