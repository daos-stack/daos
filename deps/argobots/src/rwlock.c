/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup RWLOCK Readers-Writer Lock
 * A Readers writer lock allows concurrent access for readers and exclusionary
 * access for writers.
 */

/**
 * @ingroup RWLOCK
 * @brief   Create a new readers-writer lock.
 *
 * \c ABT_rwlock_create() creates a new readers-writer lock and returns its
 * handle through \c newrwlock.
 *
 * \c newrwlock must be freed by \c ABT_rwlock_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newrwlock, \c ABT_RWLOCK_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newrwlock}
 *
 * @param[out] newrwlock  readers-writer lock handle
 * @return Error code
 */
int ABT_rwlock_create(ABT_rwlock *newrwlock)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newrwlock);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newrwlock to NULL on error. */
    *newrwlock = ABT_RWLOCK_NULL;
#endif
    ABTI_rwlock *p_newrwlock;

    int abt_errno = ABTU_malloc(sizeof(ABTI_rwlock), (void **)&p_newrwlock);
    ABTI_CHECK_ERROR(abt_errno);

    ABTI_mutex_init(&p_newrwlock->mutex);
    ABTI_cond_init(&p_newrwlock->cond);
    p_newrwlock->reader_count = 0;
    p_newrwlock->write_flag = 0;

    /* Return value */
    *newrwlock = ABTI_rwlock_get_handle(p_newrwlock);
    return ABT_SUCCESS;
}

/**
 * @ingroup RWLOCK
 * @brief   Free a readers-writer lock.
 *
 * \c ABT_rwlock_free() deallocates the resource used for the readers-writer
 * lock \c rwlock and sets \c rwlock to \c ABT_RWLOCK_NULL.
 *
 * @note
 * This routine frees \c rwlock regardless of whether it is locked or not.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_RWLOCK_PTR{\c rwlock}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c rwlock}
 * \DOC_UNDEFINED_WAITER{\c rwlock}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c rwlock}
 *
 * @param[in,out] rwlock  readers-writer lock handle
 * @return Error code
 */
int ABT_rwlock_free(ABT_rwlock *rwlock)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(rwlock);

    ABT_rwlock h_rwlock = *rwlock;
    ABTI_rwlock *p_rwlock = ABTI_rwlock_get_ptr(h_rwlock);
    ABTI_CHECK_NULL_RWLOCK_PTR(p_rwlock);

    ABTI_cond_fini(&p_rwlock->cond);
    ABTU_free(p_rwlock);

    /* Return value */
    *rwlock = ABT_RWLOCK_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup RWLOCK
 * @brief   Lock a readers-writer lock as a reader.
 *
 * \c ABT_rwlock_rdlock() locks the readers-writer lock \c rwlock as a reader.
 * If this routine successfully returns, the caller acquires \c rwlock.  If
 * \c rwlock has been locked by a writer, the caller is blocked on \c rwlock
 * until \c rwlock becomes available.
 *
 * \c rwlock may be acquired by multiple readers.
 *
 * @note
 * If \c rwlock is locked by multiple readers, \c ABT_rwlock_unlock() must be
 * called as many as the number of readers (i.e., the number of calls of
 * \c ABT_rwlock_rdlock()) to make  \c rwlock available to a writer.
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_RWLOCK}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOTASK \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{
 * \c rwlock is locked by a writer}\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{
 * \c rwlock is locked by a writer}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_RWLOCK_HANDLE{\c rwlock}
 * \DOC_V1X \DOC_ERROR_TASK{\c ABT_ERR_RWLOCK}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] rwlock  readers-writer lock handle
 * @return Error code
 */
int ABT_rwlock_rdlock(ABT_rwlock rwlock)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_rwlock *p_rwlock = ABTI_rwlock_get_ptr(rwlock);
    ABTI_CHECK_NULL_RWLOCK_PTR(p_rwlock);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Calling this routine on a tasklet is not allowed. */
    if (ABTI_IS_ERROR_CHECK_ENABLED && p_local) {
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
        ABTI_CHECK_TRUE(p_local_xstream->p_thread->type &
                            ABTI_THREAD_TYPE_YIELDABLE,
                        ABT_ERR_RWLOCK);
    }
#endif

    ABTI_mutex_lock(&p_local, &p_rwlock->mutex);
    int abt_errno = ABT_SUCCESS;
    while (p_rwlock->write_flag && abt_errno == ABT_SUCCESS) {
        abt_errno = ABTI_cond_wait(&p_local, &p_rwlock->cond, &p_rwlock->mutex);
    }
    if (abt_errno == ABT_SUCCESS) {
        p_rwlock->reader_count++;
    }
    ABTI_mutex_unlock(p_local, &p_rwlock->mutex);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup RWLOCK
 * @brief   Lock a readers-writer lock as a writer.
 *
 * \c ABT_rwlock_wrlock() locks the readers-writer lock \c rwlock as a writer.
 * If this routine successfully returns, the caller acquires \c rwlock.  If
 * \c rwlock has been locked by either a reader or another writer, the caller
 * is blocked on \c rwlock until \c rwlock becomes available.
 *
 * \c rwlock may be acquired by only a single writer.
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_RWLOCK}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOTASK \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{
 * \c rwlock is locked}\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{\c rwlock is
 *                                                               locked}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_RWLOCK_HANDLE{\c rwlock}
 * \DOC_V1X \DOC_ERROR_TASK{\c ABT_ERR_RWLOCK}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] rwlock  readers-writer lock handle
 * @return Error code
 */
int ABT_rwlock_wrlock(ABT_rwlock rwlock)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_rwlock *p_rwlock = ABTI_rwlock_get_ptr(rwlock);
    ABTI_CHECK_NULL_RWLOCK_PTR(p_rwlock);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Calling this routine on a tasklet is not allowed. */
    if (ABTI_IS_ERROR_CHECK_ENABLED && p_local) {
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
        ABTI_CHECK_TRUE(p_local_xstream->p_thread->type &
                            ABTI_THREAD_TYPE_YIELDABLE,
                        ABT_ERR_RWLOCK);
    }
#endif

    ABTI_mutex_lock(&p_local, &p_rwlock->mutex);
    int abt_errno = ABT_SUCCESS;
    while ((p_rwlock->write_flag || p_rwlock->reader_count) &&
           abt_errno == ABT_SUCCESS) {
        abt_errno = ABTI_cond_wait(&p_local, &p_rwlock->cond, &p_rwlock->mutex);
    }
    if (abt_errno == ABT_SUCCESS) {
        p_rwlock->write_flag = 1;
    }
    ABTI_mutex_unlock(p_local, &p_rwlock->mutex);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup RWLOCK
 * @brief   Unlock a readers-writer lock.
 *
 * \c ABT_rwlock_unlock() unlocks the readers-writer lock \c rwlock.
 *
 * @note
 * Both readers and a writer can call this routine to unlock \c rwlock.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{a waiter is waiting on
 *                                                      \c mutex}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_RWLOCK_HANDLE{\c rwlock}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NOT_LOCKED{\c rwlock}
 *
 * @param[in] rwlock  readers-writer lock handle
 * @return Error code
 */
int ABT_rwlock_unlock(ABT_rwlock rwlock)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_rwlock *p_rwlock = ABTI_rwlock_get_ptr(rwlock);
    ABTI_CHECK_NULL_RWLOCK_PTR(p_rwlock);

    ABTI_mutex_lock(&p_local, &p_rwlock->mutex);
    if (p_rwlock->write_flag) {
        p_rwlock->write_flag = 0;
    } else {
        ABTI_UB_ASSERT(p_rwlock->reader_count > 0);
        p_rwlock->reader_count--;
    }
    ABTI_cond_broadcast(p_local, &p_rwlock->cond);
    ABTI_mutex_unlock(p_local, &p_rwlock->mutex);
    return ABT_SUCCESS;
}
