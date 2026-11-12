// Part of the kernel (kernel.c; split from it 06/11/2026, same sources and licences as its header).
// The six virtual cores: the gate guest threads hold while they run guest code, spin waits and checkpoints.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "kernel_int.h"
#include "platform/platform.h"

// ---- virtual cores
// Horizon runs at most one thread per core, and TotK keys per-core data (allocators, job queues) by the current core
// number without locks. A guest thread therefore holds one of the three application cores while it runs guest code
// and gives it up only where Horizon would switch threads (waits, sleep, yield, exit, blocking host calls); a free
// core goes to the best waiting thread. A waiter better than a holder asks one holder to give its core up (core_kick),
// which it does at its next SVC or indirect call (kern_checkpoint, rt_dispatch), as Horizon runs the better thread at
// once; guest code without either keeps the core until it reaches one. MKW_FREE_CORES=1 turns the gate off (the game
// crashes); MKW_CORE_NUM_GATE keeps it only for threads that read their core number (core_take_num).
RtThread *g_core_owner[NCORE], *g_core_wait;
static int g_cores_on = -1, g_num_gate, g_held_max;


// Whether every guest thread needs a core to run guest code now (MKW_CORE_NUM_GATE: only those that use the number).
int cores_enabled(void) {
    if (g_cores_on < 0) {
        g_cores_on = !cfg_bool("MKW_FREE_CORES");   // "0" keeps the gate, like unset
        g_num_gate = g_cores_on ? rt_knob("CORE_NUM_GATE", "0") : 0;
        diag_init();
        ap_init();
        if (g_num_gate)
            rt_log("kernel: virtual cores only for threads that read their core number%s (MKW_CORE_NUM_GATE)",
                   g_num_gate == 2 ? " in the B blocks" : "");
    }
    return g_cores_on && !RT_KNOB_ON(g_num_gate);
}

int core_allowed(const RtThread *t, int c) { return (int)((t->mask >> c) & 1); }


static void core_count(void) {
    int n = 0;
    for (int i = 0; i < NCORE; i++) n += g_core_owner[i] != NULL;
    if (n > g_held_max) g_held_max = n;
}

static struct DgRel g_dg_rel;   // MKW_CORE_DIAG: the owner core_put is releasing, for core_grant

static void core_grant(int c) {
    RtThread *b = NULL, **bp = NULL;
    int bpri = 0;
    for (RtThread **p = &g_core_wait; *p; p = &(*p)->cnext) {
        if (!core_allowed(*p, c)) continue;
        int pr = prio_of(*p);
        if (!b || pr < bpri || (pr == bpri && (*p)->cseq < b->cseq)) {
            b = *p;
            bp = p;
            bpri = pr;
        }
    }
    if (!b) return;
    *bp = b->cnext;
    b->cnext = NULL;
    b->cur_core = c;
    g_core_owner[c] = b;
    core_count();
    if (g_diag > 0) b->dg_rel = g_dg_rel;
    if (rt_trace & RT_TRACE_SCHED) b->grant_at = kern_now_ns();
    plat_cond_signal(&b->cv);
}

void core_put(RtThread *t) {
    if (t->cur_core < 0) return;
    int c = t->cur_core;
    if (t->preempt && t->pre_since) {   // sched trace: time from the preempt request to this release
        uint64_t d = kern_now_ns() - t->pre_since;
        g_pre_n++;
        g_pre_ns += d;
        if (d > g_pre_max) g_pre_max = d;
        t->npre++;
    }
    t->pre_since = 0;
    if (g_diag > 0) {
        g_dg_rel = (struct DgRel){.kick = t->dg_kick, .host_end = t->dg_host_end, .pc = t->dg_pc, .lr = t->dg_lr,
                                  .host = t->dg_host_kick, .pre = t->preempt, .prio = t->prio, .h = t->handle};
        t->dg_kick = t->dg_host_end = t->dg_pc = t->dg_lr = 0;
        t->dg_host_kick = 0;
    }
    t->cur_core = -1;
    t->preempt = 0;
    g_core_owner[c] = NULL;
    // t may exit and be freed next (thread_finish): no waiter's ap_try may still read it
    for (RtThread *w = g_core_wait; w; w = w->cnext)
        if (w->target == t) w->target = NULL;
    core_grant(c);
}

// Whether a waiter better than t may run on core c.
static int core_better_waiter(const RtThread *t, int c) {
    int tp = prio_of(t);
    for (const RtThread *w = g_core_wait; w; w = w->cnext)
        if (core_allowed(w, c) && prio_of(w) < tp) return 1;
    return 0;
}

// Asks holders to give their cores up, one holder per waiter: waiters best first, each claims the worst holder worse
// than itself on a core it may run on that no better waiter claimed (a request already pending is reused). Asking
// every worse holder made several drop their cores for one waiter; the spare cores went to worse waiters, which asked
// again.
void core_kick(void) {
    RtThread *w[64];
    int wp[64], op[NCORE], n = 0, claimed = 0;
    for (int i = 0; i < NCORE; i++) op[i] = g_core_owner[i] ? prio_of(g_core_owner[i]) : 0;
    for (RtThread *p = g_core_wait; p && n < 64; p = p->cnext) {
        int i = n++, pp = prio_of(p);
        for (; i > 0 && (wp[i - 1] > pp || (wp[i - 1] == pp && w[i - 1]->cseq > p->cseq)); i--) {
            w[i] = w[i - 1];
            wp[i] = wp[i - 1];
        }
        w[i] = p;
        wp[i] = pp;
    }
    for (int k = 0; k < n && claimed != (1 << NCORE) - 1; k++) {
        int v = -1;
        for (int i = 0; i < NCORE; i++) {
            RtThread *o = g_core_owner[i];
            if ((claimed >> i & 1) || !core_allowed(w[k], i) || !o || op[i] <= wp[k]) continue;
            RtThread *b = v < 0 ? NULL : g_core_owner[v];
            if (!b || op[i] > op[v] || (op[i] == op[v] && o->preempt && !b->preempt)) v = i;
        }
        if (v < 0) continue;
        claimed |= 1 << v;
        RtThread *o = g_core_owner[v];
        w[k]->target = o;
        if (o->preempt) continue;
        if (rt_trace & RT_TRACE_SCHED) {
            o->pre_since = kern_now_ns();
            g_pre_req++;
        }
        if (g_diag > 0) {
            o->dg_kick = kern_now_ns();
            o->dg_host_kick = dg_in_host(o);
            o->dg_host_end = 0;
        }
        __atomic_store_n(&o->preempt, 1, __ATOMIC_RELEASE);
    }
}

static void core_take_gated(RtThread *t);

void core_take(RtThread *t) {
    if (t->cur_core >= 0) return;
    if (!cores_enabled()) {
        acct(t, S_RUN);
        return;
    }
    core_take_gated(t);
}

// SVC 0x10 on a thread without a core. MKW_CORE_NUM_GATE: threads run guest code without a core, and the game keys
// per-core data by the core number without locks (with MKW_FREE_CORES=1 two
// threads reported core 0 at once and the game crashed in frame 1), so one that asks for it takes a core through the
// gate and keeps it until its next wait, host call or preemption: two threads running at once never report the same
// number. Also a thread left without a core when the A/B blocks turn the whole gate back on. Holding G.
void core_take_num(RtThread *t) {
    if (t->cur_core >= 0 || !g_cores_on) return;
    core_take_gated(t);
}

static void core_take_gated(RtThread *t) {
    if (!(t->mask & ((1u << NCORE) - 1)))
        rt_fatal("kernel: thread %#x core mask %#llx has no application core", t->handle, (unsigned long long)t->mask);
    int c = t->core >= 0 && t->core < NCORE && core_allowed(t, t->core) && !g_core_owner[t->core] ? t->core : -1;
    for (int i = 0; c < 0 && i < NCORE; i++)
        if (core_allowed(t, i) && !g_core_owner[i]) c = i;
    if (c >= 0) {
        t->cur_core = c;
        g_core_owner[c] = t;
        core_count();
        acct(t, S_RUN);
        return;
    }
    acct(t, S_CORE);
    t->cseq = ++g_wseq;
    t->cnext = g_core_wait;
    g_core_wait = t;
    t->target = NULL;
    if (g_diag > 0) {
        t->dg_since = kern_now_ns();
        memset(&t->dg_rel, 0, sizeof t->dg_rel);
    }
    core_kick();
    if (g_diag > 0) t->dg_cls = dg_class0(t);
    if (g_ap_int && t->target && t->cur_core < 0) {   // most waits end within microseconds: spin before interrupting
        uint64_t end = kern_now_ns() + AP_FIRST_NS;
        unlock();
        while (__atomic_load_n(&t->cur_core, __ATOMIC_RELAXED) < 0 && kern_now_ns() < end) plat_pause();
        lock();
        if (t->cur_core >= 0) g_ap_spin_ok++;
    }
    int sampled = 0;
    while (t->cur_core < 0) {
        uint32_t ms = 0;   // 0: until signalled
        if (g_ap_int && ap_try(t) != AP_NONE) ms = 1;   // the owner keeps its core: again in a millisecond
        int sample = g_diag > 0 && !sampled && (t->dg_cls == DG_GAME || t->dg_cls == DG_HOST);
        if (sample && !ms) ms = 2;
        if (ms) plat_cond_wait_ms(&t->cv, &G, ms);
        else plat_cond_wait(&t->cv, &G);
        if (sample && t->cur_core < 0 && kern_now_ns() - t->dg_since >= 2000000) {
            sampled = 1;
            dg_sample(t);
        }
    }
    if (g_diag > 0) dg_done(t);
    if (t->grant_at) {
        uint64_t d = kern_now_ns() - t->grant_at;
        t->lg_ns += d;
        t->lg_n++;
        if (d > t->lg_max) t->lg_max = d;
        t->grant_at = 0;
    }
    t->target = NULL;
    acct(t, S_RUN);
}

uint32_t block(RtThread *t, int kind, int64_t ns) {
    t->waker = 0;
    if (ns == 0) return block_inner(t, kind, ns);
    core_put(t);
    acct(t, st_of(kind));
    t->nblk++;
    uint32_t r = block_inner(t, kind, ns);
    core_take(t);
    return r;
}

int kern_core_away(void) {
    RtThread *t = tcur;
    if (!t || t->cur_core < 0) return 0;
    lock();
    core_put(t);
    acct(t, S_HOST);
    if (rt_trace & RT_TRACE_SCHED)   // the caller as an exe RVA (mkw.map)
        t->wobj = (uintptr_t)__builtin_return_address(0) - plat_exe_base();
    unlock();
    return 1;
}

void kern_core_back(int *away) {
    RtThread *t = tcur;
    if (!*away || !t) return;
    lock();
    core_take(t);
    unlock();
    *away = 0;
}

// ---- spin waits (recomp/patches.py `spin` lines, found by tools/spin_waits.py)
// Adapted from MacNativeGames games/totk/runtime/threads.c runtime_spin_wait @ a038e6b and nxnative dm/host/hle_thread.c
// dm_spin_wait @ 6d34984 (MIT, Copyright (c) 2026 MacNativeGames contributors, Copyright (c) 2026 seedds).
// A pass of a guest loop that polls memory until another thread changes it. On the Switch it fills only its own core,
// where a better thread pinned there preempts it; here it holds a virtual core and a host core, and pollers that take a
// lock on every pass hand its cache line between their host cores so the thread whose item it is rarely gets it
// (nxnative found this in Breath of the Wild's job queue). Every pass pauses the host core once. A wait is a run of
// passes through the same loop at most SPIN_GAP_NS apart; once one has lasted MKW_SPIN_HANDOFF_US (default 50) and a
// thread waits for a virtual core, each pass gives the core to the waiter, whatever its priority, yields the host CPU
// and takes a core again. Shorter waits keep the core: a lock held for a moment would otherwise make the next pass wait
// for another thread's next SVC (MacNativeGames). MKW_SPIN=0 keeps the core always (the gate's own cost only).
#define SPIN_GAP_NS 20000ull
static int g_spin_on = -1;
static uint64_t g_spin_handoff_ns;
static _Thread_local uint64_t sw_head, sw_t0, sw_last;

void rt_spin_gate(Cpu *c, uint64_t head) {
    (void)c;
    plat_pause();
    RtThread *t = tcur;
    if (!t) return;
    if (g_spin_on < 0) {
        const char *e = cfg_get("MKW_SPIN_HANDOFF_US");
        g_spin_handoff_ns = (uint64_t)(e && *e ? atoll(e) : 50) * 1000;
        g_spin_on = !(cfg_get("MKW_SPIN") && !cfg_bool("MKW_SPIN"));
    }
    t->sp_n++;
    t->sp_site = head;
    uint64_t now = kern_now_ns();
    if (head != sw_head || now - sw_last > SPIN_GAP_NS) {   // a new wait
        sw_head = head;
        sw_t0 = now;
    }
    sw_last = now;
    if (!g_spin_on || t->cur_core < 0 || now - sw_t0 < g_spin_handoff_ns ||
        !__atomic_load_n(&g_core_wait, __ATOMIC_RELAXED))   // a racy look: a waiter missed now is seen at the next pass
        return;
    t->sp_ho++;
    int away = kern_core_away();
    plat_yield();
    kern_core_back(&away);
    sw_last = kern_now_ns();
}

// A pending preempt request, at an SVC or an indirect call. The core goes over only if a better thread still waits
// for it, so core_put grants it to a waiter better than t; a stale request (that waiter got another core) is dropped.
void kern_checkpoint(void) {
    RtThread *t = tcur;
    if (!t || !__atomic_load_n(&t->preempt, __ATOMIC_RELAXED)) return;
    lock();
    if (g_diag > 0 && t->cur) {
        t->dg_pc = t->cur->pc;
        t->dg_lr = t->cur->x[30];
    }
    int better = t->cur_core >= 0 && core_better_waiter(t, t->cur_core);
    if (better && ap_disabled(t)) {   // preemption disabled: the switch comes at SVC 0x36
        ap_tlr(t)[1] = 1;
        t->preempt = 0;
        g_ap_deferred++;
    } else if (better) {
        core_put(t);   // clears preempt (after the sched trace reads it)
        core_take(t);
    } else {
        t->preempt = 0;
        t->pre_since = 0;
        t->dg_kick = t->dg_host_end = 0;
        t->dg_host_kick = 0;
        g_pre_stale++;
    }
    unlock();
}

// kern_ap_stub, on the interrupted owner: where to jump back to, then the core given up as at a checkpoint.
uint64_t kern_ap_rip(void) { return tcur->ap_rip; }

void kern_ap_yield(void) {
    RtThread *t = tcur;
    lock();
    t->ap_pending = 0;
    if (t->preempt && t->cur_core >= 0 && !ap_disabled(t) && core_better_waiter(t, t->cur_core)) {
        if (g_diag > 0) {
            t->dg_pc = t->cur->pc;
            t->dg_lr = t->cur->x[30];
        }
        g_ap_taken++;
        core_put(t);
        core_take(t);
    }
    unlock();
}

// SVC 0x36 SynchronizePreemptionState: a preemption deferred by disable_count happens here.
void ap_sync(RtThread *t) {
    lock();
    if (g_ap > 0 && t->cur_core >= 0 && core_better_waiter(t, t->cur_core)) {
        core_put(t);
        core_take(t);
    }
    unlock();
}

void kern_core_stats(int *held_now, int *held_max) {
    lock();
    int n = 0;
    for (int i = 0; i < NCORE; i++) n += g_core_owner[i] != NULL;
    *held_now = n;
    *held_max = g_held_max;
    unlock();
}

void kern_core_stats_reset(void) {
    lock();
    g_held_max = 0;
    core_count();
    unlock();
}
