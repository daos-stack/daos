/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY

#ifdef ABT_CONFIG_USE_LINUX_FUTEX

/* Use Linux futex. */
#include <unistd.h>
#include <linux/futex.h>
#include <syscall.h>

void ABTD_futex_wait_and_unlock(ABTD_futex_multiple *p_futex,
                                ABTD_spinlock *p_lock)
{
    const int original_val = ABTD_atomic_relaxed_load_int(&p_futex->val);
    ABTD_spinlock_release(p_lock);
    do {
        syscall(SYS_futex, &p_futex->val.val, FUTEX_WAIT_PRIVATE, original_val,
                NULL, NULL, 0);
    } while (ABTD_atomic_relaxed_load_int(&p_futex->val) == original_val);
}

void ABTD_futex_timedwait_and_unlock(ABTD_futex_multiple *p_futex,
                                     ABTD_spinlock *p_lock,
                                     double wait_time_sec)
{
    const int original_val = ABTD_atomic_relaxed_load_int(&p_futex->val);
    ABTD_spinlock_release(p_lock);
    struct timespec wait_time; /* This wait_time must be **relative**. */
    wait_time.tv_sec = (time_t)wait_time_sec;
    wait_time.tv_nsec =
        (long)((wait_time_sec - (double)(time_t)wait_time_sec) * 1.0e9);
    syscall(SYS_futex, &p_futex->val.val, FUTEX_WAIT_PRIVATE, original_val,
            &wait_time, NULL, 0);
}

void ABTD_futex_broadcast(ABTD_futex_multiple *p_futex)
{
    int current_val = ABTD_atomic_relaxed_load_int(&p_futex->val);
    ABTD_atomic_relaxed_store_int(&p_futex->val, current_val + 1);
    syscall(SYS_futex, &p_futex->val.val, FUTEX_WAKE_PRIVATE, INT_MAX, NULL,
            NULL, 0);
}

void ABTD_futex_suspend(ABTD_futex_single *p_futex)
{
    /* Wake-up signal is 1. */
    while (ABTD_atomic_acquire_load_int(&p_futex->val) == 0) {
        syscall(SYS_futex, &p_futex->val.val, FUTEX_WAIT_PRIVATE, 0, NULL, NULL,
                0);
    }
    /* Resumed by ABTD_futex_resume() */
}

void ABTD_futex_resume(ABTD_futex_single *p_futex)
{
    ABTI_ASSERT(ABTD_atomic_relaxed_load_int(&p_futex->val) == 0);
    /* Write 1 and wake the waiter up. */
    ABTD_atomic_release_store_int(&p_futex->val, 1);
    syscall(SYS_futex, &p_futex->val.val, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}

#else /* ABT_CONFIG_USE_LINUX_FUTEX */

/* Use Pthreads. */
#include <pthread.h>

typedef struct pthread_sync {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct pthread_sync *p_next;
    struct pthread_sync *p_prev;
    ABTD_atomic_int val;
} pthread_sync;

#define PTHREAD_SYNC_STATIC_INITIALIZER                                        \
    {                                                                          \
        PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL, NULL,       \
            ABTD_ATOMIC_INT_STATIC_INITIALIZER(0),                             \
    }

void ABTD_futex_wait_and_unlock(ABTD_futex_multiple *p_futex,
                                ABTD_spinlock *p_lock)
{
    pthread_sync sync_obj = PTHREAD_SYNC_STATIC_INITIALIZER;
    pthread_mutex_lock(&sync_obj.mutex);
    /* This p_next updates must be done "after" taking mutex but "before"
     * releasing p_lock. */
    pthread_sync *p_next = (pthread_sync *)p_futex->p_next;
    if (p_next)
        p_next->p_prev = &sync_obj;
    sync_obj.p_next = p_next;
    p_futex->p_next = (void *)&sync_obj;
    ABTD_spinlock_release(p_lock);
    while (ABTD_atomic_relaxed_load_int(&sync_obj.val) == 0) {
        pthread_cond_wait(&sync_obj.cond, &sync_obj.mutex);
    }
    /* I cannot find whether a statically initialized mutex must be unlocked
     * before it gets out of scope or not, but let's choose a safer way. */
    pthread_mutex_unlock(&sync_obj.mutex);

    /* Since now val is 1, there's no possibility that the signaler is still
     * touching this sync_obj.  sync_obj can be safely released by exiting this
     * function.  Note that it seems that Linux does not need such for
     * statically allocated pthread_mutex_t and pthread_cond_t, FreeBSD
     * dynamically allocates memory and thus leaks memory if we do not destroy
     * those objects.  Let's choose a safer option. */
    pthread_cond_destroy(&sync_obj.cond);
    pthread_mutex_destroy(&sync_obj.mutex);
}

void ABTD_futex_timedwait_and_unlock(ABTD_futex_multiple *p_futex,
                                     ABTD_spinlock *p_lock,
                                     double wait_time_sec)
{
    pthread_sync sync_obj = PTHREAD_SYNC_STATIC_INITIALIZER;

    struct timespec wait_time; /* This time must be **relative**. */
    clock_gettime(CLOCK_REALTIME, &wait_time);
    wait_time.tv_sec += (time_t)wait_time_sec;
    wait_time.tv_nsec +=
        (long)((wait_time_sec - (double)(time_t)wait_time_sec) * 1.0e9);
    if (wait_time.tv_nsec >= 1e9) {
        wait_time.tv_sec += 1;
        wait_time.tv_nsec -= 1e9;
    }
    pthread_mutex_lock(&sync_obj.mutex);
    /* This p_next updates must be done "after" taking mutex but "before"
     * releasing p_lock. */
    pthread_sync *p_next = (pthread_sync *)p_futex->p_next;
    if (p_next)
        p_next->p_prev = &sync_obj;
    sync_obj.p_next = p_next;
    p_futex->p_next = (void *)&sync_obj;
    ABTD_spinlock_release(p_lock);
    pthread_cond_timedwait(&sync_obj.cond, &sync_obj.mutex, &wait_time);

    /* I cannot find whether a statically initialized mutex must be unlocked
     * before it gets out of scope or not, but let's choose a safer way. */
    pthread_mutex_unlock(&sync_obj.mutex);

    if (ABTD_atomic_acquire_load_int(&sync_obj.val) != 0) {
        /* Since now val is 1, there's no possibility that the signaler is still
         * touching sync_obj.  sync_obj can be safely released by exiting this
         * function. */
    } else {
        /* Maybe this sync_obj is being touched by the signaler.  Take a lock
         * and remove it from the list. */
        ABTD_spinlock_acquire(p_lock);
        /* Double check the value in a lock. */
        if (ABTD_atomic_acquire_load_int(&sync_obj.val) == 0) {
            /* timedout or spurious wakeup happens. Remove this sync_obj from
             * p_futex carefully. */
            if (p_futex->p_next == (void *)&sync_obj) {
                p_futex->p_next = (void *)sync_obj.p_next;
            } else {
                ABTI_ASSERT(sync_obj.p_prev);
                sync_obj.p_prev->p_next = sync_obj.p_next;
                sync_obj.p_next->p_prev = sync_obj.p_prev;
            }
        }
        ABTD_spinlock_release(p_lock);
    }
    /* Free sync_obj. */
    pthread_cond_destroy(&sync_obj.cond);
    pthread_mutex_destroy(&sync_obj.mutex);
}

void ABTD_futex_broadcast(ABTD_futex_multiple *p_futex)
{
    /* The caller must be holding a lock (p_lock above). */
    pthread_sync *p_cur = (pthread_sync *)p_futex->p_next;
    while (p_cur) {
        pthread_sync *p_next = p_cur->p_next;
        pthread_mutex_lock(&p_cur->mutex);
        ABTD_atomic_relaxed_store_int(&p_cur->val, 1);
        pthread_cond_broadcast(&p_cur->cond);
        pthread_mutex_unlock(&p_cur->mutex);
        /* After "val" is updated and the mutex is unlocked, that pthread_sync
         * may not be touched. */
        p_cur = p_next;
    }
    p_futex->p_next = NULL;
}

void ABTD_futex_suspend(ABTD_futex_single *p_futex)
{
    if (ABTD_atomic_acquire_load_ptr(&p_futex->p_sync_obj) != NULL) {
        /* Always resumed (invalid_ptr has be written).  No need to wait. */
        return;
    }
    pthread_sync sync_obj = PTHREAD_SYNC_STATIC_INITIALIZER;
    pthread_mutex_lock(&sync_obj.mutex);
    /* Use strong since either suspend or resume must succeed if those two are
     * executed concurrently. */
    if (ABTD_atomic_bool_cas_strong_ptr(&p_futex->p_sync_obj, NULL,
                                        (void *)&sync_obj)) {
        /* This thread needs to wait on this.  The outer loop is needed to avoid
         * spurious wakeup. */
        while (ABTD_atomic_relaxed_load_int(&sync_obj.val) == 0)
            pthread_cond_wait(&sync_obj.cond, &sync_obj.mutex);
    } else {
        /* It seems that this futex has already been resumed. */
    }
    pthread_mutex_unlock(&sync_obj.mutex);
    /* Resumed by ABTD_futex_resume().  Free sync_obj. */
    pthread_cond_destroy(&sync_obj.cond);
    pthread_mutex_destroy(&sync_obj.mutex);
}

void ABTD_futex_resume(ABTD_futex_single *p_futex)
{
    pthread_sync *p_sync_obj =
        (pthread_sync *)ABTD_atomic_acquire_load_ptr(&p_futex->p_sync_obj);
    if (!p_sync_obj) {
        /* Try to use CAS to notify a waiter that this is "resumed" */
        void *invalid_ptr = (void *)((intptr_t)1);
        void *ret_val = ABTD_atomic_val_cas_strong_ptr(&p_futex->p_sync_obj,
                                                       NULL, invalid_ptr);
        if (ret_val == NULL) {
            /* CAS succeeded.  Resumed.  This thread should not touch this
             * sync_obj. */
            return;
        }
        /* p_next has been updated by the waiter.  Let's wake him up. */
        p_sync_obj = (pthread_sync *)ret_val;
    }
    pthread_mutex_lock(&p_sync_obj->mutex);
    /* After setting value 1 and unlock the mutex, sync_obj will be freed
     * immediately. */
    ABTD_atomic_relaxed_store_int(&p_sync_obj->val, 1);
    pthread_cond_signal(&p_sync_obj->cond);
    pthread_mutex_unlock(&p_sync_obj->mutex);
}

#endif /* !ABT_CONFIG_USE_LINUX_FUTEX */

#endif /* !ABT_CONFIG_ACTIVE_WAIT_POLICY */
