#include "task.h"
#include "cpu.h"
#include "memory.h"
#include "lapic.h"

#define MAX_TASKS 64
#define TASK_STACK_SIZE (32 * 1024)

typedef struct {
    u64 rsp;
} task_context_t;

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_ZOMBIE
} task_state_t;

typedef struct {
    task_state_t state;
    task_context_t ctx;
    u64 wake_tick;
    const char *name;
    task_entry_t entry;
    void *arg;
    void *stack;
    int cpu_affinity;
    int running_cpu;
    int is_idle;
} task_t;

typedef struct {
    volatile int locked;
} spinlock_t;

static inline void spin_lock(spinlock_t *l) {
    while (__sync_lock_test_and_set(&l->locked, 1)) {
        asm volatile("pause");
    }
}

static inline void spin_unlock(spinlock_t *l) {
    __sync_lock_release(&l->locked);
}

static inline int spin_try_lock(spinlock_t *l) {
    return __sync_lock_test_and_set(&l->locked, 1) == 0;
}

static task_t tasks[MAX_TASKS];
static u32 task_count = 0;
static u32 cpu_count_global = 1;
static task_t *current_task[64];
static int idle_index[64];
static int last_pick[64];
static spinlock_t sched_lock;
static volatile int scheduler_active = 0;
static u32 lapic_map[256];
static u32 lapic_map_count = 0;
static u16 kernel_cs = 0x28;
static u16 kernel_ds = 0x30;

static int cpu_index(void) {
    u32 id = lapic_id();
    if (lapic_map_count == 0) return 0;
    if (id < 256) return (int)lapic_map[id];
    return 0;
}

static u64 task_build_stack(void *stack, void (*entry)(void)) {
    u64 *sp = (u64 *)((uintptr_t)stack + TASK_STACK_SIZE);
    sp = (u64 *)((uintptr_t)sp & ~0xFULL);

    *--sp = 0x202;        // RFLAGS
    *--sp = kernel_cs;    // CS (kernel code selector)
    *--sp = (u64)entry;   // RIP

    *--sp = 0; // RAX
    *--sp = 0; // RBX
    *--sp = 0; // RCX
    *--sp = 0; // RDX
    *--sp = 0; // RBP
    *--sp = 0; // RDI
    *--sp = 0; // RSI
    *--sp = 0; // R8
    *--sp = 0; // R9
    *--sp = 0; // R10
    *--sp = 0; // R11
    *--sp = 0; // R12
    *--sp = 0; // R13
    *--sp = 0; // R14
    *--sp = 0; // R15

    return (u64)sp;
}

static void task_trampoline(void) {
    asm volatile(
        "movw %0, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        : "r"(kernel_ds)
        : "ax"
    );
    int cpu = cpu_index();
    task_t *t = current_task[cpu];
    if (!t) {
        while (1) asm volatile("hlt");
    }
    t->entry(t->arg);
    spin_lock(&sched_lock);
    t->state = TASK_ZOMBIE;
    t->running_cpu = -1;
    spin_unlock(&sched_lock);
    task_yield();
    while (1) asm volatile("hlt");
}

static void task_idle(void *arg) {
    (void)arg;
    while (1) {
        asm volatile("hlt");
    }
}

static void task_cleanup(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_ZOMBIE) {
            if (tasks[i].stack) {
                free(tasks[i].stack);
                tasks[i].stack = 0;
            }
            tasks[i].state = TASK_UNUSED;
            tasks[i].running_cpu = -1;
        }
    }
}

static int task_find_free(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) return i;
    }
    return -1;
}

static task_t *task_pick_next(int cpu) {
    task_t *idle_candidate = 0;
    int idle_index_candidate = -1;
    int start = (last_pick[cpu] + 1) % MAX_TASKS;
    for (int n = 0; n < MAX_TASKS; n++) {
        int i = (start + n) % MAX_TASKS;
        task_t *t = &tasks[i];
        if (t->state != TASK_READY) continue;
        if (t->running_cpu != -1) continue;
        if (t->cpu_affinity >= 0 && t->cpu_affinity != cpu) continue;
        if (!t->is_idle) {
            last_pick[cpu] = i;
            return t;
        }
        if (!idle_candidate) {
            idle_candidate = t;
            idle_index_candidate = i;
        }
    }
    if (idle_candidate) {
        last_pick[cpu] = idle_index_candidate;
    }
    return idle_candidate;
}

void task_init(u32 cpu_count) {
    asm volatile("mov %%cs, %0" : "=r"(kernel_cs));
    asm volatile("mov %%ds, %0" : "=r"(kernel_ds));
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_UNUSED;
        tasks[i].stack = 0;
        tasks[i].running_cpu = -1;
        tasks[i].cpu_affinity = -1;
        tasks[i].is_idle = 0;
    }
    for (int i = 0; i < 64; i++) {
        current_task[i] = 0;
        idle_index[i] = -1;
        last_pick[i] = -1;
    }
    sched_lock.locked = 0;
    cpu_count_global = cpu_count;
    for (u32 cpu = 0; cpu < cpu_count_global && cpu < 64; cpu++) {
        int idx = task_create_affinity("idle", task_idle, NULL, (int)cpu);
        idle_index[cpu] = idx;
        if (idx >= 0) {
            tasks[idx].is_idle = 1;
        }
    }
    scheduler_active = 0;
}

void task_register_cpu(u32 lapic_id, u32 index) {
    if (lapic_id < 256) {
        lapic_map[lapic_id] = index;
        if (index + 1 > lapic_map_count) lapic_map_count = index + 1;
    }
}

int task_create(const char *name, task_entry_t entry, void *arg) {
    return task_create_affinity(name, entry, arg, -1);
}

int task_create_affinity(const char *name, task_entry_t entry, void *arg, int cpu) {
    spin_lock(&sched_lock);
    int idx = task_find_free();
    if (idx < 0) {
        spin_unlock(&sched_lock);
        return -1;
    }
    task_t *t = &tasks[idx];
    void *stack = malloc(TASK_STACK_SIZE);
    if (!stack) {
        spin_unlock(&sched_lock);
        return -1;
    }

    t->state = TASK_READY;
    t->name = name;
    t->entry = entry;
    t->arg = arg;
    t->stack = stack;
    t->wake_tick = 0;
    t->cpu_affinity = cpu;
    t->running_cpu = -1;
    t->is_idle = 0;
    t->ctx.rsp = task_build_stack(stack, task_trampoline);
    task_count++;
    spin_unlock(&sched_lock);
    return idx;
}

void task_start_bsp(void) {
    int cpu = 0;
    if (idle_index[cpu] >= 0) {
        current_task[cpu] = &tasks[idle_index[cpu]];
        current_task[cpu]->state = TASK_RUNNING;
        current_task[cpu]->running_cpu = lapic_id();
    }
    scheduler_active = 1;
    task_yield();
}

void task_start_ap(void) {
    int cpu = cpu_index();
    if (idle_index[cpu] >= 0) {
        current_task[cpu] = &tasks[idle_index[cpu]];
        current_task[cpu]->state = TASK_RUNNING;
        current_task[cpu]->running_cpu = lapic_id();
    }
    scheduler_active = 1;
    task_yield();
}

void task_yield(void) {
    if (!scheduler_active) return;
    asm volatile("int $0xF0");
}

void task_preempt(void) {
    task_yield();
}

void task_tick(void) {
    if (!scheduler_active) return;
    if (!spin_try_lock(&sched_lock)) return;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && ticks >= tasks[i].wake_tick) {
            tasks[i].state = TASK_READY;
        }
    }
    spin_unlock(&sched_lock);
}

void task_sleep(u64 sleep_ticks) {
    int cpu = cpu_index();
    task_t *t = current_task[cpu];
    if (!t) return;
    spin_lock(&sched_lock);
    t->state = TASK_SLEEPING;
    t->wake_tick = ticks + sleep_ticks;
    t->running_cpu = -1;
    spin_unlock(&sched_lock);
    task_yield();
}

const char *task_current_name(void) {
    int cpu = cpu_index();
    if (!current_task[cpu]) return "none";
    return current_task[cpu]->name ? current_task[cpu]->name : "task";
}

u64 task_schedule_isr(u64 rsp) {
    if (!scheduler_active) return rsp;

    int cpu = cpu_index();
    if (!spin_try_lock(&sched_lock)) return rsp;
    task_cleanup();

    task_t *prev = current_task[cpu];
    if (prev) {
        prev->ctx.rsp = rsp;
        if (prev->state == TASK_RUNNING) {
            prev->state = TASK_READY;
            prev->running_cpu = -1;
        }
    }

    task_t *next = task_pick_next(cpu);
    if (!next) {
        if (prev) {
            prev->state = TASK_RUNNING;
            prev->running_cpu = lapic_id();
        }
        spin_unlock(&sched_lock);
        return rsp;
    }

    if (next != prev) {
        next->state = TASK_RUNNING;
        next->running_cpu = lapic_id();
        current_task[cpu] = next;
    } else if (prev) {
        prev->state = TASK_RUNNING;
        prev->running_cpu = lapic_id();
    }
    u64 next_rsp = current_task[cpu]->ctx.rsp;
    if (next_rsp) {
        u64 *frame = (u64 *)next_rsp;
        u64 rip = frame[15];
        u64 cs = frame[16];
        if (cs != kernel_cs || (rip & 0xFFFF800000000000ull) != 0xFFFF800000000000ull) {
            next_rsp = rsp;
        }
    } else {
        next_rsp = rsp;
    }
    spin_unlock(&sched_lock);
    return next_rsp;
}
