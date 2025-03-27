/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MUTEX_H_INCLUDED
#define ABTI_MUTEX_H_INCLUDED

static inline ABTI_mutex *ABTI_mutex_get_ptr(ABT_mutex mutex)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_mutex *p_mutex;
    if (mutex == ABT_MUTEX_NULL) {
        p_mutex = NULL;
    } else {
        p_mutex = (ABTI_mutex *)mutex;
    }
    return p_mutex;
#else
    return (ABTI_mutex *)mutex;
#endif
}

static inline ABT_mutex ABTI_mutex_get_handle(ABTI_mutex *p_mutex)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_mutex h_mutex;
    if (p_mutex == NULL) {
        h_mutex = ABT_MUTEX_NULL;
    } else {
        h_mutex = (ABT_mutex)p_mutex;
    }
    return h_mutex;
#else
    return (ABT_mutex)p_mutex;
#endif
}

static inline void ABTI_mutex_init(ABTI_mutex *p_mutex)
{
    ABTD_spinlock_clear(&p_mutex->lock);
#ifndef ABT_CONFIG_USE_SIMPLE_MUTEX
    ABTD_spinlock_clear(&p_mutex->waiter_lock);
    ABTI_waitlist_init(&p_mutex->waitlist);
#endif
    p_mutex->attrs = ABTI_MUTEX_ATTR_NONE;
    p_mutex->nesting_cnt = 0;
    p_mutex->owner_id = 0;
}

static inline void ABTI_mutex_fini(ABTI_mutex *p_mutex)
{
#ifndef ABT_CONFIG_USE_SIMPLE_MUTEX
    ABTD_spinlock_acquire(&p_mutex->waiter_lock);
#endif
}

static inline void ABTI_mutex_lock_no_recursion(ABTI_local **pp_local,
                                                ABTI_mutex *p_mutex)
{
#ifndef ABT_CONFIG_USE_SIMPLE_MUTEX
    while (ABTD_spinlock_try_acquire(&p_mutex->lock)) {
        /* Failed to take a lock, so let's add it to the waiter list. */
        ABTD_spinlock_acquire(&p_mutex->waiter_lock);
        /* Maybe the mutex lock has been already released.  Check it. */
        if (!ABTD_spinlock_try_acquire(&p_mutex->lock)) {
            /* Lock has been taken. */
            ABTD_spinlock_release(&p_mutex->waiter_lock);
            break;
        }
        /* Wait on waitlist. */
        ABTI_waitlist_wait_and_unlock(pp_local, &p_mutex->waitlist,
                                      &p_mutex->waiter_lock,
                                      ABT_SYNC_EVENT_TYPE_MUTEX,
                                      (void *)p_mutex);
    }
    /* Take a lock. */
#else
    /* Simple yield-based implementation */
    ABTI_ythread *p_ythread = NULL;
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(*pp_local);
    if (!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream)
        p_ythread = ABTI_thread_get_ythread_or_null(p_local_xstream->p_thread);

    if (p_ythread) {
        while (ABTD_spinlock_try_acquire(&p_mutex->lock)) {
            ABTI_ythread_yield(&p_local_xstream, p_ythread,
                               ABTI_YTHREAD_YIELD_KIND_YIELD_LOOP,
                               ABT_SYNC_EVENT_TYPE_MUTEX, (void *)p_mutex);
            *pp_local = ABTI_xstream_get_local(p_local_xstream);
        }
    } else {
        /* Use spinlock. */
        ABTD_spinlock_acquire(&p_mutex->lock);
    }
#endif
}

static inline void ABTI_mutex_lock(ABTI_local **pp_local, ABTI_mutex *p_mutex)
{
    if (p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) {
        /* Recursive mutex */
        ABTI_thread_id self_id = ABTI_self_get_thread_id(*pp_local);
        if (self_id != p_mutex->owner_id) {
            ABTI_mutex_lock_no_recursion(pp_local, p_mutex);
            ABTI_ASSERT(p_mutex->nesting_cnt == 0);
            p_mutex->owner_id = self_id;
        } else {
            /* Increment a nesting count. */
            p_mutex->nesting_cnt++;
        }
    } else {
        ABTI_mutex_lock_no_recursion(pp_local, p_mutex);
    }
}

static inline ABT_bool ABTI_mutex_is_locked(ABTI_mutex *p_mutex)
{
    return ABTD_spinlock_is_locked(&p_mutex->lock);
}

static inline int ABTI_mutex_trylock_no_recursion(ABTI_mutex *p_mutex)
{
    return ABTD_spinlock_try_acquire(&p_mutex->lock) ? ABT_ERR_MUTEX_LOCKED
                                                     : ABT_SUCCESS;
}

static inline int ABTI_mutex_trylock(ABTI_local *p_local, ABTI_mutex *p_mutex)
{
    if (p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) {
        /* Recursive mutex */
        ABTI_thread_id self_id = ABTI_self_get_thread_id(p_local);
        if (self_id != p_mutex->owner_id) {
            int abt_errno = ABTI_mutex_trylock_no_recursion(p_mutex);
            if (abt_errno == ABT_SUCCESS) {
                ABTI_ASSERT(p_mutex->nesting_cnt == 0);
                p_mutex->owner_id = self_id;
            }
            return abt_errno;
        } else {
            /* Increment a nesting count. */
            p_mutex->nesting_cnt++;
            return ABT_SUCCESS;
        }
    } else {
        return ABTI_mutex_trylock_no_recursion(p_mutex);
    }
}

static inline void ABTI_mutex_spinlock_no_recursion(ABTI_mutex *p_mutex)
{
    ABTD_spinlock_acquire(&p_mutex->lock);
}

static inline void ABTI_mutex_spinlock(ABTI_local *p_local, ABTI_mutex *p_mutex)
{
    if (p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) {
        /* Recursive mutex */
        ABTI_thread_id self_id = ABTI_self_get_thread_id(p_local);
        if (self_id != p_mutex->owner_id) {
            ABTI_mutex_spinlock_no_recursion(p_mutex);
            ABTI_ASSERT(p_mutex->nesting_cnt == 0);
            p_mutex->owner_id = self_id;
        } else {
            /* Increment a nesting count. */
            p_mutex->nesting_cnt++;
        }
    } else {
        ABTI_mutex_spinlock_no_recursion(p_mutex);
    }
}

static inline void ABTI_mutex_unlock_no_recursion(ABTI_local *p_local,
                                                  ABTI_mutex *p_mutex)
{
#ifndef ABT_CONFIG_USE_SIMPLE_MUTEX
    ABTD_spinlock_acquire(&p_mutex->waiter_lock);
    ABTD_spinlock_release(&p_mutex->lock);
    /* Operations of waitlist must be done while taking waiter_lock. */
    ABTI_waitlist_broadcast(p_local, &p_mutex->waitlist);
    ABTD_spinlock_release(&p_mutex->waiter_lock);
#else
    ABTD_spinlock_release(&p_mutex->lock);
#endif
}

static inline void ABTI_mutex_unlock(ABTI_local *p_local, ABTI_mutex *p_mutex)
{
    if (p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) {
        /* recursive mutex */
        if (p_mutex->nesting_cnt == 0) {
            p_mutex->owner_id = 0;
            ABTI_mutex_unlock_no_recursion(p_local, p_mutex);
        } else {
            p_mutex->nesting_cnt--;
        }
    } else {
        /* unknown attributes */
        ABTI_mutex_unlock_no_recursion(p_local, p_mutex);
    }
}

#endif /* ABTI_MUTEX_H_INCLUDED */
