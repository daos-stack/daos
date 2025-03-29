/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_COND_H_INCLUDED
#define ABTI_COND_H_INCLUDED

#include "abti_mutex.h"

/* Inlined functions for Condition Variable  */

static inline void ABTI_cond_init(ABTI_cond *p_cond)
{
    ABTD_spinlock_clear(&p_cond->lock);
    p_cond->p_waiter_mutex = NULL;
    ABTI_waitlist_init(&p_cond->waitlist);
}

static inline void ABTI_cond_fini(ABTI_cond *p_cond)
{
    /* The lock needs to be acquired to safely free the condition structure.
     * However, we do not have to unlock it because the entire structure is
     * freed here. */
    ABTD_spinlock_acquire(&p_cond->lock);
    ABTI_UB_ASSERT(ABTI_waitlist_is_empty(&p_cond->waitlist));
}

static inline ABTI_cond *ABTI_cond_get_ptr(ABT_cond cond)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_cond *p_cond;
    if (cond == ABT_COND_NULL) {
        p_cond = NULL;
    } else {
        p_cond = (ABTI_cond *)cond;
    }
    return p_cond;
#else
    return (ABTI_cond *)cond;
#endif
}

static inline ABT_cond ABTI_cond_get_handle(ABTI_cond *p_cond)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_cond h_cond;
    if (p_cond == NULL) {
        h_cond = ABT_COND_NULL;
    } else {
        h_cond = (ABT_cond)p_cond;
    }
    return h_cond;
#else
    return (ABT_cond)p_cond;
#endif
}

ABTU_ret_err static inline int
ABTI_cond_wait(ABTI_local **pp_local, ABTI_cond *p_cond, ABTI_mutex *p_mutex)
{
    ABTD_spinlock_acquire(&p_cond->lock);

    if (p_cond->p_waiter_mutex == NULL) {
        p_cond->p_waiter_mutex = p_mutex;
    } else {
        if (p_cond->p_waiter_mutex != p_mutex) {
            ABTD_spinlock_release(&p_cond->lock);
            return ABT_ERR_INV_MUTEX;
        }
    }

    ABTI_mutex_unlock(*pp_local, p_mutex);
    ABTI_waitlist_wait_and_unlock(pp_local, &p_cond->waitlist, &p_cond->lock,
                                  ABT_SYNC_EVENT_TYPE_COND, (void *)p_cond);
    /* Lock the mutex again */
    ABTI_mutex_lock(pp_local, p_mutex);
    return ABT_SUCCESS;
}

static inline void ABTI_cond_broadcast(ABTI_local *p_local, ABTI_cond *p_cond)
{
    ABTD_spinlock_acquire(&p_cond->lock);
    /* Wake up all waiting ULTs */
    ABTI_waitlist_broadcast(p_local, &p_cond->waitlist);
    ABTD_spinlock_release(&p_cond->lock);
}

#endif /* ABTI_COND_H_INCLUDED */
