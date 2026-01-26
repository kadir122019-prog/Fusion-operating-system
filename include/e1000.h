#ifndef E1000_H
#define E1000_H

#include "types.h"

typedef void (*e1000_rx_cb)(const u8 *data, u16 len);

int e1000_init(u8 mac_out[6]);
void e1000_set_rx_callback(e1000_rx_cb cb);
void e1000_poll(void);
int e1000_send(const void *data, u16 len);

#endif
