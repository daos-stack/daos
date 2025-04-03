/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTD_SPINLOCK_H_INCLUDED
#define ABTD_SPINLOCK_H_INCLUDED

typedef struct ABTD_spinlock {
    ABTD_atomic_bool val;
} ABTD_spinlock;

#define ABTD_SPINLOCK_STATIC_INITIALIZER()                                     \
    {                                                                          \
        ABTD_ATOMIC_BOOL_STATIC_INITIALIZER(0)                                 \
    }

static inline ABT_bool ABTD_spinlock_is_locked(const ABTD_spinlock *p_lock)
{
    return ABTD_atomic_acquire_load_bool(&p_lock->val);
}

static inline void ABTD_spinlock_clear(ABTD_spinlock *p_lock)
{
    ABTD_atomic_relaxed_clear_bool(&p_lock->val);
}

static inline void ABTD_spinlock_acquire(ABTD_spinlock *p_lock)
{
    while (ABTD_atomic_test_and_set_bool(&p_lock->val)) {
        while (ABTD_spinlock_is_locked(p_lock) != ABT_FALSE)
            ;
    }
}

/* Return ABT_FALSE if the lock is acquired. */
static inline ABT_bool ABTD_spinlock_try_acquire(ABTD_spinlock *p_lock)
{
    return ABTD_atomic_test_and_set_bool(&p_lock->val) ? ABT_TRUE : ABT_FALSE;
}

static inline void ABTD_spinlock_release(ABTD_spinlock *p_lock)
{
    ABTD_atomic_release_clear_bool(&p_lock->val);
}

#endif /* ABTD_SPINLOCK_H_INCLUDED */
