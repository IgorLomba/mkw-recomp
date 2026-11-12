// Part of the kernel (kernel.c; split from it 06/11/2026, same sources and licences as its header).
// Fibers (nn::os::*Fiber, replaced by name).
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "kernel_int.h"
#include "platform/platform.h"

// ---- fibers
// The SDK switches fibers by swapping register context, which breaks "RET = C return". Each guest fiber gets a host
// fiber (own 256 MiB reserved host stack) and its own Cpu, so a suspended fiber's registers and host frames stay
// untouched while others run, and a fiber may resume on another thread. A switch carries the thread state (TLR, TLS
// pointer, FPCR/FPSR) over to the target's Cpu and clears the exclusive monitor.
enum { F_NEW, F_STARTED, F_DONE };
struct Fiber {
    uint64_t addr, func, arg, stack, size;
    Cpu *cpu;
    PlatFiber *host;
    int state;
    volatile int running;
};
static Fiber *g_fibers[FIBER_MAX];
static PlatMutex g_fiber_lock = PLAT_MUTEX_INIT;

static Fiber *fiber_find(uint64_t addr) {
    Fiber *f = NULL;
    plat_mutex_lock_shared(&g_fiber_lock);
    for (int i = 0; i < FIBER_MAX && !f; i++)
        if (g_fibers[i] && g_fibers[i]->addr == addr) f = g_fibers[i];
    plat_mutex_unlock_shared(&g_fiber_lock);
    return f;
}

static void fiber_switch_to(RtThread *t, const Cpu *from, Fiber *to);

// A fiber may resume on another guest thread's host thread, and the compiler may keep tcur's TLS address across
// plat_fiber_switch: after a switch the thread is read again through this call, never from a local or tcur.
static __attribute__((noinline)) RtThread *tcur_now(void) { return tcur; }

// After a host fiber switch: release the context left behind, and finish an ExitThread issued on a fiber.
static void fiber_resumed(void) {
    RtThread *t = tcur_now();
    rt_fpcr_sync(t->cur);   // the host fiber resumes with its own MXCSR (platform.h fibers switch it)
    if (t->leaving) {
        __atomic_store_n(&t->leaving->running, 0, __ATOMIC_RELEASE);
        t->leaving = NULL;
    }
    if (t->exit_pending && !t->fiber) exit_now(t);
}

static void fiber_main(void *p) {
    Fiber *f = p;
    fiber_resumed();
    Cpu *c = f->cpu;
    tcur_now()->host_depth = 0;   // a new fiber starts at the top level; each context restores its own depth on resume
    uint64_t next = rt_call(c, f->func, 1, f->arg);
    f->state = F_DONE;
    Fiber *n = NULL;
    if (next && !(n = fiber_find(next)))
        rt_fatal("fiber %#llx returned %#llx, which is not an initialized fiber", (unsigned long long)f->addr,
                 (unsigned long long)next);
    fiber_switch_to(tcur_now(), c, n);   // NULL: back to the context of the thread it finished on
    rt_fatal("fiber %#llx resumed after it finished", (unsigned long long)f->addr);
}

static void fiber_switch_to(RtThread *t, const Cpu *from, Fiber *to) {
    if (!t->base_fiber) {
        t->base_fiber = plat_fiber_self();
        if (!t->base_fiber) rt_fatal("kernel: cannot make the thread a fiber");
    }
    Cpu *dst = to ? to->cpu : t->cpu;
    if (to) {
        if (to->state == F_DONE) rt_fatal("SwitchToFiber(%#llx): the fiber has finished", (unsigned long long)to->addr);
        int idle = 0;
        if (!__atomic_compare_exchange_n(&to->running, &idle, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            rt_fatal("SwitchToFiber(%#llx): the fiber is running", (unsigned long long)to->addr);
        if (to->state == F_NEW) {
            dst->sp = (to->stack + to->size) & ~15ull;
            to->state = F_STARTED;
        }
    }
    dst->tpidr = from->tpidr;
    dst->tpidrro = from->tpidrro;
    dst->fpcr = from->fpcr;
    dst->fpsr = from->fpsr;
    dst->excl_addr = ~0ull;
    dst->thread = t;
    t->leaving = t->fiber;
    t->fiber = to;
    t->cur = dst;
    int depth = t->host_depth;
    plat_fiber_switch(to ? to->host : t->base_fiber);
    tcur_now()->host_depth = depth;   // this context's depth, on the thread that resumed it
    fiber_resumed();
}

// InitializeFiber(x0 FiberType*, x1 function, x2 argument, x3 stack, x4 stack size, w5 flags)
void kern_fiber_initialize(Cpu *c) {
    uint64_t addr = c->x[0];
    Fiber *f = fiber_find(addr);
    if (f && f->running) rt_fatal("InitializeFiber(%#llx): the fiber is running", (unsigned long long)addr);
    if (!f) {
        f = calloc(1, sizeof *f);
        Cpu *fc = plat_aligned_alloc(sizeof(Cpu), 64);
        if (!f || !fc) rt_fatal("kernel: fiber allocation");
        f->cpu = fc;
        plat_mutex_lock(&g_fiber_lock);
        int i = 0;
        while (i < FIBER_MAX && g_fibers[i]) i++;
        if (i == FIBER_MAX) rt_fatal("kernel: more than %d fibers", FIBER_MAX);
        g_fibers[i] = f;
        plat_mutex_unlock(&g_fiber_lock);
    } else if (f->host) {
        plat_fiber_free(f->host);
    }
    memset(f->cpu, 0, sizeof *f->cpu);
    f->cpu->excl_addr = ~0ull;
    f->addr = addr;
    f->func = c->x[1];
    f->arg = c->x[2];
    f->stack = c->x[3];
    f->size = c->x[4];
    f->state = F_NEW;
    f->running = 0;
    f->host = plat_fiber_new(HOST_STACK, fiber_main, f);
    if (!f->host) rt_fatal("kernel: cannot create a host fiber");
}

// FinalizeFiber(x0 FiberType*)
void kern_fiber_finalize(Cpu *c) {
    uint64_t addr = c->x[0];
    plat_mutex_lock(&g_fiber_lock);
    Fiber *f = NULL;
    for (int i = 0; i < FIBER_MAX && !f; i++)
        if (g_fibers[i] && g_fibers[i]->addr == addr) {
            f = g_fibers[i];
            g_fibers[i] = NULL;
        }
    plat_mutex_unlock(&g_fiber_lock);
    if (!f) rt_fatal("FinalizeFiber(%#llx): not an initialized fiber", (unsigned long long)addr);
    if (f->running) rt_fatal("FinalizeFiber(%#llx): the fiber is running", (unsigned long long)addr);
    plat_fiber_free(f->host);
    plat_aligned_free(f->cpu);
    free(f);
}

// SwitchToFiber(x0 FiberType*)
void kern_fiber_switch(Cpu *c) {
    RtThread *t = tcur;
    if (!t) rt_fatal("SwitchToFiber on a host thread");
    Fiber *to = c->x[0] ? fiber_find(c->x[0]) : NULL;   // 0, what GetCurrentFiber gives there: the thread's own context
    if (!to && c->x[0]) rt_fatal("SwitchToFiber(%#llx): not an initialized fiber", (unsigned long long)c->x[0]);
    if (to == t->fiber) return;
    fiber_switch_to(t, c, to);
}

// GetCurrentFiber() -> x0, NULL on the thread's own context
void kern_fiber_current(Cpu *c) {
    RtThread *t = tcur;
    c->x[0] = t && t->fiber ? t->fiber->addr : 0;
}

HOST_OVERRIDE(KERN_SYM_INITIALIZE_FIBER, kern_fiber_initialize);
HOST_OVERRIDE(KERN_SYM_FINALIZE_FIBER, kern_fiber_finalize);
HOST_OVERRIDE(KERN_SYM_SWITCH_TO_FIBER, kern_fiber_switch);
HOST_OVERRIDE(KERN_SYM_GET_CURRENT_FIBER, kern_fiber_current);
