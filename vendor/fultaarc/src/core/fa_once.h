/* fa_once.h - FultaArc: run an initialiser once, safely across threads. MIT.
 * The library builds a few tables on first use (CRC-32/16/64, AES). Two threads opening their first archives at the
 * same time must not race on them: the first to arrive builds, the others wait until it is published (C11 atomics;
 * <threads.h> and its call_once are not on every target, mingw-w64 among them). */
#ifndef FA_ONCE_H
#define FA_ONCE_H

#include <stdatomic.h>

typedef atomic_int fa_once_t;   /* 0 not started, 1 being built, 2 ready; a static one starts at 0 */

static inline void fa_once(fa_once_t *once, void (*init)(void)) {
    if (atomic_load_explicit(once, memory_order_acquire) == 2) return;
    int idle = 0;
    if (atomic_compare_exchange_strong_explicit(once, &idle, 1, memory_order_acq_rel, memory_order_acquire)) {
        init();
        atomic_store_explicit(once, 2, memory_order_release);
        return;
    }
    while (atomic_load_explicit(once, memory_order_acquire) != 2) { }   /* another thread builds: microseconds */
}

#endif /* FA_ONCE_H */
