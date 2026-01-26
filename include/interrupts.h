#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"

typedef void (*irq_handler_t)(int irq, void *ctx);

void interrupts_init(void);
void interrupts_init_ap(void);
void interrupts_set_irq_handler(int irq, irq_handler_t handler, void *ctx);
void interrupts_unmask_irq(int irq);
void interrupts_mask_irq(int irq);
u64 interrupts_get_irq_count(int irq);
void interrupts_set_vector(int vector, void (*handler)(void));

#endif
