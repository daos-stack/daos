/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_WAITLIST_H_INCLUDED
#define ABTI_WAITLIST_H_INCLUDED

#include "abt_config.h"

static inline void ABTI_waitlist_init(ABTI_waitlist *p_waitlist)
{
#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
    ABTD_futex_multiple_init(&p_waitlist->futex);
#endif
    p_waitlist->p_head = NULL;
    p_waitlist->p_tail = NULL;
}

static inline void
ABTI_waitlist_wait_and_unlock(ABTI_local **pp_local, ABTI_waitlist *p_waitlist,
                              ABTD_spinlock *p_lock,
                              ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_ASSERT(ABTD_spinlock_is_locked(p_lock) == ABT_TRUE);
    ABTI_ythread *p_ythread = NULL;
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(*pp_local);
    if (!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream) {
        p_ythread = ABTI_thread_get_ythread_or_null(p_local_xstream->p_thread);
    }
    if (!p_ythread) {
        /* External thread or non-yieldable thread. */
        ABTI_thread thread;
        thread.type = ABTI_THREAD_TYPE_EXT;
        /* use state for synchronization */
        ABTD_atomic_relaxed_store_int(&thread.state, ABT_THREAD_STATE_BLOCKED);
        /* Add thread to the list. */
        thread.p_next = NULL;
        if (p_waitlist->p_head == NULL) {
            p_waitlist->p_head = &thread;
        } else {
            p_waitlist->p_tail->p_next = &thread;
        }
        p_waitlist->p_tail = &thread;

        /* Non-yieldable thread is waiting here. */
#ifdef ABT_CONFIG_ACTIVE_WAIT_POLICY
        ABTD_spinlock_release(p_lock);
        while (ABTD_atomic_acquire_load_int(&thread.state) !=
               ABT_THREAD_STATE_READY)
            ;
#else
        while (1) {
            /* While taking a lock check if this thread is not ready. This is
             * necessary to sleep while ready; otherwise deadlock. */
            if (ABTD_atomic_relaxed_load_int(&thread.state) ==
                ABT_THREAD_STATE_READY) {
                ABTD_spinlock_release(p_lock);
                break;
            }
            ABTD_futex_wait_and_unlock(&p_waitlist->futex, p_lock);

            /* Quick check. */
            if (ABTD_atomic_acquire_load_int(&thread.state) ==
                ABT_THREAD_STATE_READY)
                break;

            /* Take a lock again. */
            ABTD_spinlock_acquire(p_lock);
        }
#endif
    } else {
        /* Add p_thread to the list. */
        p_ythread->thread.p_next = NULL;
        if (p_waitlist->p_head == NULL) {
            p_waitlist->p_head = &p_ythread->thread;
        } else {
            p_waitlist->p_tail->p_next = &p_ythread->thread;
        }
        p_waitlist->p_tail = &p_ythread->thread;

        /* Suspend the current ULT */
        ABTI_ythread_suspend_unlock(&p_local_xstream, p_ythread, p_lock,
                                    sync_event_type, p_sync);
        /* Resumed. */
        *pp_local = ABTI_xstream_get_local(p_local_xstream);
    }
}

/* Return ABT_TRUE if timed out. */
static inline ABT_bool ABTI_waitlist_wait_timedout_and_unlock(
    ABTI_local **pp_local, ABTI_waitlist *p_waitlist, ABTD_spinlock *p_lock,
    double target_time, ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_ASSERT(ABTD_spinlock_is_locked(p_lock) == ABT_TRUE);
    ABTI_ythread *p_ythread = NULL;
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(*pp_local);
    if (!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream)
        p_ythread = ABTI_thread_get_ythread_or_null(p_local_xstream->p_thread);

    /* Always use a dummy thread. */
    ABTI_thread thread;
    thread.type = ABTI_THREAD_TYPE_EXT;
    /* use state for synchronization */
    ABTD_atomic_relaxed_store_int(&thread.state, ABT_THREAD_STATE_BLOCKED);

    /* Add p_thread to the list.  This implementation is tricky since this
     * updates p_prev as well for removal on timeout while the other functions
     * (e.g., wait, broadcast, signal) do not update it. */
    thread.p_next = NULL;
    if (p_waitlist->p_head == NULL) {
        p_waitlist->p_head = &thread;
        thread.p_prev = NULL;
    } else {
        p_waitlist->p_tail->p_next = &thread;
        thread.p_prev = p_waitlist->p_tail;
    }
    p_waitlist->p_tail = &thread;

    /* Waiting here. */
    if (p_ythread) {
        /* When an underlying entity is yieldable. */
        ABTD_spinlock_release(p_lock);
        while (ABTD_atomic_acquire_load_int(&thread.state) !=
               ABT_THREAD_STATE_READY) {
            double cur_time = ABTI_get_wtime();
            if (cur_time >= target_time) {
                ABTD_spinlock_acquire(p_lock);
                goto timeout;
            }
            ABTI_ythread_yield(&p_local_xstream, p_ythread,
                               ABTI_YTHREAD_YIELD_KIND_YIELD_LOOP,
                               sync_event_type, p_sync);
            *pp_local = ABTI_xstream_get_local(p_local_xstream);
        }
    } else {
        /* When an underlying entity is non-yieldable. */
#ifdef ABT_CONFIG_ACTIVE_WAIT_POLICY
        ABTD_spinlock_release(p_lock);
        while (ABTD_atomic_acquire_load_int(&thread.state) !=
               ABT_THREAD_STATE_READY) {
            double cur_time = ABTI_get_wtime();
            if (cur_time >= target_time) {
                ABTD_spinlock_acquire(p_lock);
                goto timeout;
            }
        }
#else
        while (1) {
            double cur_time = ABTI_get_wtime();
            if (cur_time >= target_time) {
                goto timeout;
            }
            /* While taking a lock check if this thread is not ready. This is
             * necessary to sleep while ready; otherwise deadlock. */
            if (ABTD_atomic_relaxed_load_int(&thread.state) ==
                ABT_THREAD_STATE_READY) {
                ABTD_spinlock_release(p_lock);
                break;
            }
            ABTD_futex_timedwait_and_unlock(&p_waitlist->futex, p_lock,
                                            target_time - cur_time);
            /* Quick check. */
            if (ABTD_atomic_acquire_load_int(&thread.state) ==
                ABT_THREAD_STATE_READY)
                break;
            /* Take a lock again. */
            ABTD_spinlock_acquire(p_lock);
        }
#endif
    }
    /* Singled */
    return ABT_FALSE;
timeout:
    /* Timeout.  Remove this thread if not signaled even after taking a lock. */
    ABTI_ASSERT(ABTD_spinlock_is_locked(p_lock) == ABT_TRUE);
    ABT_bool is_timedout =
        (ABTD_atomic_relaxed_load_int(&thread.state) != ABT_THREAD_STATE_READY)
            ? ABT_TRUE
            : ABT_FALSE;
    if (is_timedout) {
        /* This thread is still in the list. */
        if (p_waitlist->p_head == &thread) {
            /* thread is a head. */
            /* Note that thread->p_prev cannot be used to check whether
             * thread is a head or not because signal and broadcast do
             * not modify thread->p_prev. */
            p_waitlist->p_head = thread.p_next;
            if (!thread.p_next) {
                /* This thread is p_tail */
                ABTI_ASSERT(p_waitlist->p_tail == &thread);
                p_waitlist->p_tail = NULL;
            }
        } else {
            /* thread is not a head and thus p_prev exists. */
            ABTI_ASSERT(thread.p_prev);
            thread.p_prev->p_next = thread.p_next;
            if (thread.p_next && thread.type == ABTI_THREAD_TYPE_EXT) {
                /* Only a dummy external thread created by this function
                 * checks p_prev.  Note that a real external thread is
                 * also dummy, so updating p_prev is allowed. */
                thread.p_next->p_prev = thread.p_prev;
            } else {
                /* This thread is p_tail */
                ABTI_ASSERT(p_waitlist->p_tail == &thread);
                p_waitlist->p_tail = thread.p_prev;
            }
        }
        /* We do not need to modify thread->p_prev and p_next since this
         * dummy thread is no longer used. */
    }
    ABTD_spinlock_release(p_lock);
    return is_timedout;
}

static inline void ABTI_waitlist_signal(ABTI_local *p_local,
                                        ABTI_waitlist *p_waitlist)
{
    ABTI_thread *p_thread = p_waitlist->p_head;
    if (p_thread) {
        ABTI_thread *p_next = p_thread->p_next;
        p_thread->p_next = NULL;

        ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
        if (p_ythread) {
            ABTI_ythread_resume_and_push(p_local, p_ythread);
        } else {
            /* When p_thread is an external thread or a tasklet */
            ABTD_atomic_release_store_int(&p_thread->state,
                                          ABT_THREAD_STATE_READY);
#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
            /* There's no way to selectively wake up threads.  Let's just
             * wake up all the threads.  They will sleep again since their
             * states are not marked as READY. */
            ABTD_futex_broadcast(&p_waitlist->futex);
#endif
        }
        /* After updating p_thread->state, p_thread can be updated and
         * freed. */
        p_waitlist->p_head = p_next;
        if (!p_next)
            p_waitlist->p_tail = NULL;
    }
}

static inline void ABTI_waitlist_broadcast(ABTI_local *p_local,
                                           ABTI_waitlist *p_waitlist)
{
    ABTI_thread *p_thread = p_waitlist->p_head;
    if (p_thread) {
        ABT_bool wakeup_nonyieldable = ABT_FALSE;
        do {
            ABTI_thread *p_next = p_thread->p_next;
            p_thread->p_next = NULL;

            ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
            if (p_ythread) {
                ABTI_ythread_resume_and_push(p_local, p_ythread);
            } else {
                /* When p_thread is an external thread or a tasklet */
                wakeup_nonyieldable = ABT_TRUE;
                ABTD_atomic_release_store_int(&p_thread->state,
                                              ABT_THREAD_STATE_READY);
            }
            /* After updating p_thread->state, p_thread can be updated and
             * freed. */
            p_thread = p_next;
        } while (p_thread);
        p_waitlist->p_head = NULL;
        p_waitlist->p_tail = NULL;
#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
        if (wakeup_nonyieldable) {
            ABTD_futex_broadcast(&p_waitlist->futex);
        }
#else
        /* Do nothing. */
        (void)wakeup_nonyieldable;
#endif
    }
}

static inline ABT_bool ABTI_waitlist_is_empty(ABTI_waitlist *p_waitlist)
{
    return p_waitlist->p_head ? ABT_FALSE : ABT_TRUE;
}

#endif /* ABTI_WAITLIST_H_INCLUDED */
