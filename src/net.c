#include "net.h"
#include "e1000.h"
#include "cpu.h"
#include "memory.h"

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806

#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DNS_CLIENT_PORT 49152
#define DNS_SERVER_PORT 53

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define ARP_CACHE_SIZE 8

typedef struct {
    u8 dst[6];
    u8 src[6];
    u16 type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
    u16 htype;
    u16 ptype;
    u8 hlen;
    u8 plen;
    u16 oper;
    u8 sha[6];
    u32 spa;
    u8 tha[6];
    u32 tpa;
} __attribute__((packed)) arp_pkt_t;

typedef struct {
    u8 ver_ihl;
    u8 tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8 ttl;
    u8 proto;
    u16 checksum;
    u32 src;
    u32 dst;
} __attribute__((packed)) ipv4_hdr_t;

typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 len;
    u16 checksum;
} __attribute__((packed)) udp_hdr_t;

typedef struct {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u16 offset_flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} __attribute__((packed)) tcp_hdr_t;

typedef struct {
    u8 op;
    u8 htype;
    u8 hlen;
    u8 hops;
    u32 xid;
    u16 secs;
    u16 flags;
    u32 ciaddr;
    u32 yiaddr;
    u32 siaddr;
    u32 giaddr;
    u8 chaddr[16];
    u8 sname[64];
    u8 file[128];
    u32 magic;
    u8 options[312];
} __attribute__((packed)) dhcp_msg_t;

typedef struct {
    u16 id;
    u16 flags;
    u16 qdcount;
    u16 ancount;
    u16 nscount;
    u16 arcount;
} __attribute__((packed)) dns_hdr_t;

typedef struct {
    u32 ip;
    u8 mac[6];
    u64 last_seen;
    int valid;
} arp_entry_t;

static u8 local_mac[6];
static u32 local_ip = 0;
static u32 netmask = 0;
static u32 gateway = 0;
static u32 dns_server = 0;
static int net_ready = 0;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static u32 ip_id = 1;

typedef enum {
    DHCP_INIT = 0,
    DHCP_DISCOVER_SENT,
    DHCP_REQUEST_SENT,
    DHCP_BOUND
} dhcp_state_t;

static dhcp_state_t dhcp_state = DHCP_INIT;
static u32 dhcp_xid = 0;
static u32 dhcp_server = 0;
static u32 dhcp_offer_ip = 0;
static u64 dhcp_last_send = 0;

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
    TCP_CLOSE_WAIT
} tcp_state_t;

typedef struct {
    tcp_state_t state;
    u32 dest_ip;
    u16 dest_port;
    u16 src_port;
    u32 snd_nxt;
    u32 snd_una;
    u32 rcv_nxt;
    u8 recv_buf[65536];
    u32 recv_len;
    u32 recv_read;
    u8 last_payload[1460];
    u16 last_len;
    u8 last_flags;
    u32 last_seq;
    u64 last_send_tick;
    int waiting_ack;
} tcp_conn_t;

static tcp_conn_t tcp_conn;
static int dns_pending = 0;
static u16 dns_txid = 0;
static u32 dns_result_ip = 0;

static u16 net_htons(u16 v) {
    return (u16)((v << 8) | (v >> 8));
}

static u32 net_htonl(u32 v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) |
           ((v & 0xFF000000u) >> 24);
}

static u16 checksum16(const void *data, size_t len) {
    const u8 *buf = data;
    u32 sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (u16)((buf[i] << 8) | buf[i + 1]);
    }
    if (len & 1) {
        sum += (u16)(buf[len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (u16)(~sum);
}

static u16 checksum_tcp(u32 src, u32 dst, const u8 *tcp, u16 len) {
    struct {
        u32 src;
        u32 dst;
        u8 zero;
        u8 proto;
        u16 length;
    } pseudo;
    pseudo.src = src;
    pseudo.dst = dst;
    pseudo.zero = 0;
    pseudo.proto = IP_PROTO_TCP;
    pseudo.length = net_htons(len);

    u32 sum = 0;
    const u8 *p = (const u8 *)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo); i += 2) {
        sum += (u16)((p[i] << 8) | p[i + 1]);
    }
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (u16)((tcp[i] << 8) | tcp[i + 1]);
    }
    if (len & 1) {
        sum += (u16)(tcp[len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (u16)(~sum);
}

static void arp_cache_update(u32 ip, const u8 mac[6]) {
    int slot = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            slot = i;
            break;
        }
        if (!arp_cache[i].valid && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0;
    arp_cache[slot].ip = ip;
    for (int j = 0; j < 6; j++) arp_cache[slot].mac[j] = mac[j];
    arp_cache[slot].last_seen = ticks;
    arp_cache[slot].valid = 1;
}

static int arp_cache_lookup(u32 ip, u8 out_mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) out_mac[j] = arp_cache[i].mac[j];
            return 1;
        }
    }
    return 0;
}

static int net_send_frame(const u8 dst[6], u16 eth_type, const u8 *payload, u16 len) {
    u8 frame[1514];
    if (len + sizeof(eth_hdr_t) > sizeof(frame)) return 0;
    eth_hdr_t *eth = (eth_hdr_t *)frame;
    for (int i = 0; i < 6; i++) {
        eth->dst[i] = dst[i];
        eth->src[i] = local_mac[i];
    }
    eth->type = net_htons(eth_type);
    memcpy(frame + sizeof(eth_hdr_t), payload, len);
    return e1000_send(frame, (u16)(len + sizeof(eth_hdr_t)));
}

static void arp_send_request(u32 target_ip);

static int net_send_ipv4(u32 dest_ip, u8 proto, const u8 *payload, u16 len) {
    u8 dst_mac[6];
    int is_broadcast = (dest_ip == 0xFFFFFFFFu || local_ip == 0);
    if (is_broadcast) {
        for (int i = 0; i < 6; i++) dst_mac[i] = 0xFF;
    } else {
        u32 target = dest_ip;
        if (netmask != 0 && gateway != 0) {
            if ((local_ip & netmask) != (dest_ip & netmask)) {
                target = gateway;
            }
        }
        if (!arp_cache_lookup(target, dst_mac)) {
            arp_send_request(target);
            return 0;
        }
    }

    u8 packet[1500];
    if (sizeof(ipv4_hdr_t) + len > sizeof(packet)) return 0;
    ipv4_hdr_t *ip = (ipv4_hdr_t *)packet;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = net_htons((u16)(sizeof(ipv4_hdr_t) + len));
    ip->id = net_htons((u16)ip_id++);
    ip->flags_frag = net_htons(0x4000);
    ip->ttl = 64;
    ip->proto = proto;
    ip->checksum = 0;
    ip->src = local_ip;
    ip->dst = dest_ip;
    ip->checksum = checksum16(ip, sizeof(ipv4_hdr_t));

    memcpy(packet + sizeof(ipv4_hdr_t), payload, len);
    return net_send_frame(dst_mac, ETH_TYPE_IPV4, packet,
                          (u16)(sizeof(ipv4_hdr_t) + len));
}

static void arp_send_request(u32 target_ip) {
    u8 dst[6];
    for (int i = 0; i < 6; i++) dst[i] = 0xFF;
    arp_pkt_t pkt;
    pkt.htype = net_htons(1);
    pkt.ptype = net_htons(ETH_TYPE_IPV4);
    pkt.hlen = 6;
    pkt.plen = 4;
    pkt.oper = net_htons(1);
    for (int i = 0; i < 6; i++) pkt.sha[i] = local_mac[i];
    pkt.spa = local_ip;
    for (int i = 0; i < 6; i++) pkt.tha[i] = 0x00;
    pkt.tpa = target_ip;
    net_send_frame(dst, ETH_TYPE_ARP, (u8 *)&pkt, sizeof(pkt));
}

static void arp_send_reply(const u8 target_mac[6], u32 target_ip) {
    arp_pkt_t pkt;
    pkt.htype = net_htons(1);
    pkt.ptype = net_htons(ETH_TYPE_IPV4);
    pkt.hlen = 6;
    pkt.plen = 4;
    pkt.oper = net_htons(2);
    for (int i = 0; i < 6; i++) pkt.sha[i] = local_mac[i];
    pkt.spa = local_ip;
    for (int i = 0; i < 6; i++) pkt.tha[i] = target_mac[i];
    pkt.tpa = target_ip;
    net_send_frame(target_mac, ETH_TYPE_ARP, (u8 *)&pkt, sizeof(pkt));
}

static void arp_handle(const u8 *payload, u16 len) {
    if (len < sizeof(arp_pkt_t)) return;
    const arp_pkt_t *pkt = (const arp_pkt_t *)payload;
    if (pkt->htype != net_htons(1) || pkt->ptype != net_htons(ETH_TYPE_IPV4)) return;
    if (pkt->hlen != 6 || pkt->plen != 4) return;

    if (pkt->oper == net_htons(2)) {
        arp_cache_update(pkt->spa, pkt->sha);
        return;
    }
    if (pkt->oper == net_htons(1)) {
        if (local_ip != 0 && pkt->tpa == local_ip) {
            arp_send_reply(pkt->sha, pkt->spa);
        }
    }
}

static int net_send_udp(u32 dest_ip, u16 src_port, u16 dst_port, const u8 *data, u16 len) {
    u8 packet[1500];
    if (sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + len > sizeof(packet)) return 0;
    udp_hdr_t *udp = (udp_hdr_t *)(packet + sizeof(ipv4_hdr_t));
    u8 *payload = packet + sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t);

    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    udp->len = net_htons((u16)(sizeof(udp_hdr_t) + len));
    udp->checksum = 0;

    memcpy(payload, data, len);
    return net_send_ipv4(dest_ip, IP_PROTO_UDP, packet + sizeof(ipv4_hdr_t),
                         (u16)(sizeof(udp_hdr_t) + len));
}

static void dhcp_send_discover(void) {
    dhcp_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = 1;
    msg.htype = 1;
    msg.hlen = 6;
    msg.xid = net_htonl(dhcp_xid);
    msg.flags = net_htons(0x8000);
    for (int i = 0; i < 6; i++) msg.chaddr[i] = local_mac[i];
    msg.magic = net_htonl(0x63825363u);

    u8 *opt = msg.options;
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = 1;
    *opt++ = 55;
    *opt++ = 3;
    *opt++ = 1;
    *opt++ = 3;
    *opt++ = 6;
    *opt++ = 255;

    net_send_udp(0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                 (u8 *)&msg, (u16)(sizeof(msg) - sizeof(msg.options) + (opt - msg.options)));
    dhcp_last_send = ticks;
    dhcp_state = DHCP_DISCOVER_SENT;
}

static void dhcp_send_request(void) {
    dhcp_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = 1;
    msg.htype = 1;
    msg.hlen = 6;
    msg.xid = net_htonl(dhcp_xid);
    msg.flags = net_htons(0x8000);
    for (int i = 0; i < 6; i++) msg.chaddr[i] = local_mac[i];
    msg.magic = net_htonl(0x63825363u);

    u8 *opt = msg.options;
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = 3;
    *opt++ = 50;
    *opt++ = 4;
    memcpy(opt, &dhcp_offer_ip, 4);
    opt += 4;
    *opt++ = 54;
    *opt++ = 4;
    memcpy(opt, &dhcp_server, 4);
    opt += 4;
    *opt++ = 55;
    *opt++ = 3;
    *opt++ = 1;
    *opt++ = 3;
    *opt++ = 6;
    *opt++ = 255;

    net_send_udp(0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                 (u8 *)&msg, (u16)(sizeof(msg) - sizeof(msg.options) + (opt - msg.options)));
    dhcp_last_send = ticks;
    dhcp_state = DHCP_REQUEST_SENT;
}

static void dhcp_handle(const u8 *payload, u16 len) {
    if (len < sizeof(dhcp_msg_t) - sizeof(((dhcp_msg_t *)0)->options)) return;
    const dhcp_msg_t *msg = (const dhcp_msg_t *)payload;
    if (msg->op != 2 || net_htonl(msg->xid) != dhcp_xid) return;
    if (msg->magic != net_htonl(0x63825363u)) return;

    u8 msg_type = 0;
    u32 server_id = 0;
    u32 subnet = 0;
    u32 router = 0;
    u32 dns = 0;

    const u8 *opt = msg->options;
    const u8 *end = payload + len;
    while (opt < end && *opt != 255) {
        if (*opt == 0) {
            opt++;
            continue;
        }
        if (opt + 1 >= end) break;
        u8 tag = opt[0];
        u8 olen = opt[1];
        opt += 2;
        if (opt + olen > end) break;

        if (tag == 53 && olen == 1) msg_type = opt[0];
        else if (tag == 54 && olen == 4) memcpy(&server_id, opt, 4);
        else if (tag == 1 && olen == 4) memcpy(&subnet, opt, 4);
        else if (tag == 3 && olen >= 4) memcpy(&router, opt, 4);
        else if (tag == 6 && olen >= 4) memcpy(&dns, opt, 4);

        opt += olen;
    }

    if (msg_type == 2 && dhcp_state == DHCP_DISCOVER_SENT) {
        dhcp_offer_ip = msg->yiaddr;
        dhcp_server = server_id;
        dhcp_send_request();
        return;
    }

    if (msg_type == 5 && dhcp_state == DHCP_REQUEST_SENT) {
        local_ip = msg->yiaddr;
        netmask = subnet;
        gateway = router;
        dns_server = dns;
        dhcp_state = DHCP_BOUND;
        net_ready = 1;
    }
}

static int dns_skip_name(const u8 *end, const u8 **cursor) {
    const u8 *p = *cursor;
    while (p < end) {
        u8 len = *p;
        if (len == 0) {
            p++;
            *cursor = p;
            return 1;
        }
        if ((len & 0xC0) == 0xC0) {
            if (p + 1 >= end) return 0;
            p += 2;
            *cursor = p;
            return 1;
        }
        p++;
        if (p + len > end) return 0;
        p += len;
    }
    return 0;
}

static void dns_handle(const u8 *payload, u16 len) {
    if (!dns_pending || len < sizeof(dns_hdr_t)) return;
    const u8 *end = payload + len;
    const dns_hdr_t *hdr = (const dns_hdr_t *)payload;
    if (hdr->id != net_htons(dns_txid)) return;
    if ((hdr->flags & net_htons(0x8000)) == 0) return;
    u16 qdcount = net_htons(hdr->qdcount);
    u16 ancount = net_htons(hdr->ancount);

    const u8 *p = payload + sizeof(dns_hdr_t);
    for (u16 i = 0; i < qdcount; i++) {
        if (!dns_skip_name(end, &p)) return;
        if (p + 4 > end) return;
        p += 4;
    }

    for (u16 i = 0; i < ancount; i++) {
        if (!dns_skip_name(end, &p)) return;
        if (p + 10 > end) return;
        u16 type = net_htons(*(u16 *)p);
        u16 class_ = net_htons(*(u16 *)(p + 2));
        u16 rdlen = net_htons(*(u16 *)(p + 8));
        p += 10;
        if (p + rdlen > end) return;
        if (type == 1 && class_ == 1 && rdlen == 4) {
            memcpy(&dns_result_ip, p, 4);
            dns_pending = 0;
            return;
        }
        p += rdlen;
    }
    if (ancount == 0) dns_pending = 0;
}

static void tcp_send_segment(u8 flags, const u8 *data, u16 len) {
    u8 packet[1500];
    if (sizeof(tcp_hdr_t) + len > sizeof(packet)) return;
    tcp_hdr_t *tcp = (tcp_hdr_t *)packet;
    tcp->src_port = net_htons(tcp_conn.src_port);
    tcp->dst_port = net_htons(tcp_conn.dest_port);
    tcp->seq = net_htonl(tcp_conn.snd_nxt);
    tcp->ack = net_htonl(tcp_conn.rcv_nxt);
    tcp->offset_flags = net_htons((u16)((5u << 12) | flags));
    tcp->window = net_htons(4096);
    tcp->checksum = 0;
    tcp->urgent = 0;
    if (len > 0) {
        memcpy(packet + sizeof(tcp_hdr_t), data, len);
    }
    tcp->checksum = checksum_tcp(local_ip, tcp_conn.dest_ip, packet,
                                 (u16)(sizeof(tcp_hdr_t) + len));
    net_send_ipv4(tcp_conn.dest_ip, IP_PROTO_TCP, packet,
                  (u16)(sizeof(tcp_hdr_t) + len));
    tcp_conn.last_send_tick = ticks;
    tcp_conn.last_flags = flags;
    tcp_conn.last_seq = tcp_conn.snd_nxt;
    tcp_conn.last_len = len;
    if (len > 0 && len <= sizeof(tcp_conn.last_payload)) {
        memcpy(tcp_conn.last_payload, data, len);
    }
    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
        tcp_conn.snd_nxt += 1;
    } else {
        tcp_conn.snd_nxt += len;
    }
    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) || len > 0) {
        tcp_conn.waiting_ack = 1;
    }
}

static void tcp_handle(const u8 *payload, u16 len, u32 src_ip) {
    if (len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t *tcp = (const tcp_hdr_t *)payload;
    u16 src_port = net_htons(tcp->src_port);
    u16 dst_port = net_htons(tcp->dst_port);
    u32 seq = net_htonl(tcp->seq);
    u32 ack = net_htonl(tcp->ack);
    u16 off_flags = net_htons(tcp->offset_flags);
    u8 data_offset = (u8)((off_flags >> 12) & 0xF);
    u8 flags = (u8)(off_flags & 0x3F);
    u16 hdr_len = (u16)(data_offset * 4);
    if (hdr_len < sizeof(tcp_hdr_t) || len < hdr_len) return;
    u16 data_len = (u16)(len - hdr_len);
    const u8 *data = payload + hdr_len;

    if (tcp_conn.state == TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            src_ip == tcp_conn.dest_ip && src_port == tcp_conn.dest_port &&
            dst_port == tcp_conn.src_port && ack == tcp_conn.snd_nxt) {
            tcp_conn.rcv_nxt = seq + 1;
            tcp_conn.snd_una = ack;
            tcp_conn.waiting_ack = 0;
            tcp_conn.state = TCP_ESTABLISHED;
            tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
        }
        return;
    }

    if (tcp_conn.state == TCP_ESTABLISHED || tcp_conn.state == TCP_FIN_WAIT) {
        if (src_ip != tcp_conn.dest_ip || src_port != tcp_conn.dest_port || dst_port != tcp_conn.src_port) {
            return;
        }
        if (flags & TCP_FLAG_ACK) {
            if (ack > tcp_conn.snd_una) {
                tcp_conn.snd_una = ack;
            }
            if (ack >= tcp_conn.snd_nxt) {
                tcp_conn.waiting_ack = 0;
            }
        }
        if (data_len > 0 && seq == tcp_conn.rcv_nxt) {
            u32 space = (u32)(sizeof(tcp_conn.recv_buf) - tcp_conn.recv_len);
            if (data_len > space) data_len = (u16)space;
            memcpy(tcp_conn.recv_buf + tcp_conn.recv_len, data, data_len);
            tcp_conn.recv_len += data_len;
            tcp_conn.rcv_nxt += data_len;
            tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
        }
        if (flags & TCP_FLAG_FIN) {
            tcp_conn.rcv_nxt = seq + data_len + 1;
            tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
            if (tcp_conn.state == TCP_ESTABLISHED) {
                tcp_conn.state = TCP_CLOSE_WAIT;
            } else {
                tcp_conn.state = TCP_CLOSED;
            }
        }
    }
}

static void ipv4_handle(const u8 *payload, u16 len) {
    if (len < sizeof(ipv4_hdr_t)) return;
    const ipv4_hdr_t *ip = (const ipv4_hdr_t *)payload;
    if ((ip->ver_ihl >> 4) != 4) return;
    u8 ihl = (u8)(ip->ver_ihl & 0x0F);
    u16 hdr_len = (u16)(ihl * 4);
    if (hdr_len < sizeof(ipv4_hdr_t) || len < hdr_len) return;
    u16 total_len = net_htons(ip->total_len);
    if (total_len > len) total_len = len;

    if (ip->proto == IP_PROTO_UDP) {
        const u8 *udp_start = payload + hdr_len;
        u16 udp_len = (u16)(total_len - hdr_len);
        if (udp_len < sizeof(udp_hdr_t)) return;
        const udp_hdr_t *udp = (const udp_hdr_t *)udp_start;
        u16 dst_port = net_htons(udp->dst_port);
        u16 data_len = (u16)(udp_len - sizeof(udp_hdr_t));
        const u8 *data = udp_start + sizeof(udp_hdr_t);

        if (dst_port == DHCP_CLIENT_PORT) {
            dhcp_handle(data, data_len);
        } else if (dst_port == DNS_CLIENT_PORT) {
            dns_handle(data, data_len);
        }
        return;
    }
    if (ip->proto == IP_PROTO_TCP) {
        const u8 *tcp_start = payload + hdr_len;
        u16 tcp_len = (u16)(total_len - hdr_len);
        tcp_handle(tcp_start, tcp_len, ip->src);
    }
}

static void net_rx_frame(const u8 *data, u16 len) {
    if (len < sizeof(eth_hdr_t)) return;
    const eth_hdr_t *eth = (const eth_hdr_t *)data;
    u16 type = net_htons(eth->type);
    const u8 *payload = data + sizeof(eth_hdr_t);
    u16 payload_len = (u16)(len - sizeof(eth_hdr_t));

    if (type == ETH_TYPE_ARP) {
        arp_handle(payload, payload_len);
    } else if (type == ETH_TYPE_IPV4) {
        ipv4_handle(payload, payload_len);
    }
}

void net_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) arp_cache[i].valid = 0;
    net_ready = 0;
    dhcp_state = DHCP_INIT;
    dhcp_offer_ip = 0;
    dhcp_server = 0;
    dhcp_last_send = 0;

    if (!e1000_init(local_mac)) return;
    e1000_set_rx_callback(net_rx_frame);

    dhcp_xid = (u32)(ticks ^ 0xA5A5A5A5u);
    dhcp_send_discover();

    tcp_conn.state = TCP_CLOSED;
    tcp_conn.recv_len = 0;
    tcp_conn.recv_read = 0;
}

void net_poll(void) {
    e1000_poll();

    if (tcp_conn.state == TCP_SYN_SENT || tcp_conn.waiting_ack) {
        if (ticks - tcp_conn.last_send_tick > PIT_HZ) {
            if (tcp_conn.last_flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
                tcp_conn.snd_nxt = tcp_conn.last_seq;
                tcp_send_segment(tcp_conn.last_flags, NULL, 0);
            } else if (tcp_conn.last_len > 0) {
                tcp_conn.snd_nxt = tcp_conn.last_seq;
                tcp_send_segment(tcp_conn.last_flags, tcp_conn.last_payload, tcp_conn.last_len);
            }
        }
    }

    if (dhcp_state != DHCP_BOUND) {
        u64 elapsed = ticks - dhcp_last_send;
        if (elapsed > (PIT_HZ * 2)) {
            dhcp_send_discover();
        }
    }
}

int net_is_up(void) {
    return net_ready && local_ip != 0;
}

u32 net_get_ip(void) {
    return local_ip;
}

u32 net_get_dns(void) {
    return dns_server;
}

u32 net_get_netmask(void) {
    return netmask;
}

u32 net_get_gateway(void) {
    return gateway;
}

int net_dns_resolve(const char *host, u32 *out_ip) {
    if (!net_is_up()) return 0;
    if (dns_server == 0) dns_server = net_htonl(0x08080808u);
    dns_pending = 1;
    dns_result_ip = 0;
    dns_txid = (u16)(ticks ^ 0x1234u);

    u8 packet[256];
    dns_hdr_t *hdr = (dns_hdr_t *)packet;
    memset(hdr, 0, sizeof(*hdr));
    hdr->id = net_htons(dns_txid);
    hdr->flags = net_htons(0x0100);
    hdr->qdcount = net_htons(1);
    u8 *p = packet + sizeof(dns_hdr_t);

    const char *label = host;
    while (*label) {
        const char *dot = label;
        while (*dot && *dot != '.') dot++;
        u8 len = (u8)(dot - label);
        *p++ = len;
        for (u8 i = 0; i < len; i++) {
            *p++ = (u8)label[i];
        }
        label = *dot ? dot + 1 : dot;
    }
    *p++ = 0;
    *p++ = 0;
    *p++ = 1;
    *p++ = 0;
    *p++ = 1;

    net_send_udp(dns_server, DNS_CLIENT_PORT, DNS_SERVER_PORT, packet, (u16)(p - packet));
    u64 start = ticks;
    while (ticks - start < PIT_HZ * 3) {
        net_poll();
        if (!dns_pending && dns_result_ip != 0) {
            if (out_ip) *out_ip = dns_result_ip;
            return 1;
        }
        cpu_sleep_ticks(1);
    }
    dns_pending = 0;
    return 0;
}

int net_tcp_connect(u32 dest_ip, u16 dest_port) {
    if (!net_is_up()) return 0;
    tcp_conn.dest_ip = dest_ip;
    tcp_conn.dest_port = dest_port;
    tcp_conn.src_port = (u16)(1024 + (ticks % 40000));
    tcp_conn.snd_nxt = (u32)(ticks ^ 0xA5A5C3u);
    tcp_conn.snd_una = tcp_conn.snd_nxt;
    tcp_conn.rcv_nxt = 0;
    tcp_conn.recv_len = 0;
    tcp_conn.recv_read = 0;
    tcp_conn.waiting_ack = 0;
    tcp_conn.state = TCP_SYN_SENT;
    tcp_send_segment(TCP_FLAG_SYN, NULL, 0);
    return 1;
}

int net_tcp_is_established(void) {
    return tcp_conn.state == TCP_ESTABLISHED;
}

int net_tcp_send(const u8 *data, u16 len) {
    if (tcp_conn.state != TCP_ESTABLISHED) return 0;
    if (len == 0) return 0;
    if (len > sizeof(tcp_conn.last_payload)) len = (u16)sizeof(tcp_conn.last_payload);
    tcp_send_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
    return 1;
}

int net_tcp_recv(u8 *out, u16 max) {
    if (!out || max == 0) return 0;
    u32 available = tcp_conn.recv_len - tcp_conn.recv_read;
    if (available == 0) return 0;
    if (available > max) available = max;
    memcpy(out, tcp_conn.recv_buf + tcp_conn.recv_read, available);
    tcp_conn.recv_read += available;
    return (int)available;
}

int net_tcp_is_closed(void) {
    return tcp_conn.state == TCP_CLOSED || tcp_conn.state == TCP_CLOSE_WAIT;
}

void net_tcp_close(void) {
    if (tcp_conn.state == TCP_ESTABLISHED) {
        tcp_send_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        tcp_conn.state = TCP_FIN_WAIT;
    }
}
