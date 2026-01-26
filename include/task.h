#ifndef TASK_H
#define TASK_H

#include "types.h"

typedef void (*task_entry_t)(void *arg);

void task_init(u32 cpu_count);
void task_register_cpu(u32 lapic_id, u32 index);
int task_create(const char *name, task_entry_t entry, void *arg);
int task_create_affinity(const char *name, task_entry_t entry, void *arg, int cpu);
void task_start_bsp(void);
void task_start_ap(void);
void task_yield(void);
void task_preempt(void);
void task_tick(void);
void task_sleep(u64 ticks);
const char *task_current_name(void);
u64 task_schedule_isr(u64 rsp);

#endif
