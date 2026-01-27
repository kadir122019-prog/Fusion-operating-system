#ifndef LAPIC_H
#define LAPIC_H

#include "types.h"

void lapic_init(void);
void lapic_init_ap(void);
u32 lapic_id(void);
void lapic_eoi(void);
void lapic_timer_setup(u32 hz);
u32 lapic_timer_ticks_per_sec(void);

#endif
