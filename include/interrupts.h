#ifndef INTERRUPTS_H
#define INTERRUPTS_H

typedef void (*irq_handler_t)(int irq, void *ctx);

void interrupts_init(void);
void interrupts_set_irq_handler(int irq, irq_handler_t handler, void *ctx);
void interrupts_unmask_irq(int irq);
void interrupts_mask_irq(int irq);

#endif
