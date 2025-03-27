/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include <sys/time.h>

static inline double convert_timespec_to_sec(const struct timespec *p_ts);

/** @defgroup COND Condition Variable
 * This group is for Condition Variable.
 */

/**
 * @ingroup COND
 * @brief   Create a new condition variable.
 *
 * \c ABT_cond_create() creates a new condition variable and returns its handle
 * through \c newcond.
 *
 * \c newcond must be freed by \c ABT_cond_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newcond, \c ABT_COND_NULL}
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
 * \DOC_UNDEFINED_NULL_PTR{\c newcond}
 *
 * @param[out] newcond  condition variable handle
 * @return Error code
 */
int ABT_cond_create(ABT_cond *newcond)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newcond);

    /* Check if the size of ABT_cond_memory is okay. */
    ABTI_STATIC_ASSERT(sizeof(ABTI_cond) <= sizeof(ABT_cond_memory));

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newcond to NULL on error. */
    *newcond = ABT_COND_NULL;
#endif
    ABTI_cond *p_newcond;
    int abt_errno = ABTU_malloc(sizeof(ABTI_cond), (void **)&p_newcond);
    ABTI_CHECK_ERROR(abt_errno);

    ABTI_cond_init(p_newcond);
    /* Return value */
    *newcond = ABTI_cond_get_handle(p_newcond);
    return ABT_SUCCESS;
}

/**
 * @ingroup COND
 * @brief   Free a condition variable.
 *
 * \c ABT_cond_free() deallocates the resource used for the condition variable
 * \c cond and sets \c cond to \c ABT_COND_NULL.
 *
 * @changev20
 * \DOC_DESC_V1X_CRUDE_WAITER_CHECK{\c cond, \c ABT_ERR_COND}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_COND_PTR{\c cond}
 * \DOC_V1X \DOC_ERROR_WAITER{\c cond, \c ABT_ERR_COND}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c cond}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c cond}
 * \DOC_V20 \DOC_UNDEFINED_WAITER{\c cond}
 *
 * @param[in,out] cond  condition variable handle
 * @return Error code
 */
int ABT_cond_free(ABT_cond *cond)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(cond);

    ABT_cond h_cond = *cond;
    ABTI_cond *p_cond = ABTI_cond_get_ptr(h_cond);
    ABTI_CHECK_NULL_COND_PTR(p_cond);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* This check will be removed in Argobots 2.0 */
    ABTI_CHECK_TRUE(ABTI_waitlist_is_empty(&p_cond->waitlist), ABT_ERR_COND);
#else
    ABTI_UB_ASSERT(ABTI_waitlist_is_empty(&p_cond->waitlist));
#endif

    ABTI_cond_fini(p_cond);
    ABTU_free(p_cond);
    /* Return value */
    *cond = ABT_COND_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup COND
 * @brief   Wait on a condition variable.
 *
 * The caller of \c ABT_cond_wait() waits on the condition variable \c cond
 * until it is signaled.  The user must call this routine while the mutex
 * \c mutex is locked.  \c mutex will be automatically released while waiting.
 * When the caller is woken up, \c mutex will be automatically locked by the
 * caller.  The user is then responsible for unlocking \c mutex.
 *
 * This routine associates \c mutex with \c cond until this routine returns, so
 * the user may not use more than one mutex for the same \c cond.
 *
 * This routine returns with \c mutex locked even if an error occurs.
 *
 * If \c mutex is a recursive mutex, \c mutex must be locked only once by the
 * caller.
 *
 * @note
 * Unlike other implementations of condition variables, a spurious wakeup never
 * occurs.
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_COND}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_ANY_NOTASK \DOC_CONTEXT_CTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_COND_HANDLE{\c cond}
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 * \DOC_V1X \DOC_ERROR_TASK{\c ABT_ERR_COND}
 *
 * @undefined
 * \DOC_UNDEFINED_NOT_LOCKED{\c mutex}
 * \DOC_UNDEFINED_MUTEX_ILLEGAL_UNLOCK{\c mutex}
 * \DOC_UNDEFINED_COND_WAIT{\c cond, \c mutex}
 *
 * @param[in] cond   condition variable handle
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_cond_wait(ABT_cond cond, ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x does not allow a tasklet to call this routine. */
    if (ABTI_IS_ERROR_CHECK_ENABLED && p_local) {
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
        ABTI_CHECK_TRUE(p_local_xstream->p_thread->type &
                            ABTI_THREAD_TYPE_YIELDABLE,
                        ABT_ERR_COND);
    }
#endif
    ABTI_cond *p_cond = ABTI_cond_get_ptr(cond);
    ABTI_CHECK_NULL_COND_PTR(p_cond);
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);

    /* Check if a given mutex is valid. */
    /* p_mutex must be locked. */
    ABTI_UB_ASSERT(ABTI_mutex_is_locked(p_mutex));
    /* If p_mutex is recursive, the caller of this function must be an owner. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->owner_id != ABTI_self_get_thread_id(p_local)));
    /* If p_mutex is recursive, p_mutex must not be locked more than once. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->nesting_cnt > 1));

    int abt_errno = ABTI_cond_wait(&p_local, p_cond, p_mutex);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup COND
 * @brief   Wait on a condition variable with a timeout limit.
 *
 * The caller of \c ABT_cond_timedwait() waits on the condition variable \c cond
 * until either it is signaled or the absolute time specified by \c abstime
 * passes.  The user must call this routine while the mutex \c mutex is locked.
 * \c mutex will be automatically released while waiting.  When the caller is
 * woken up, \c mutex will be automatically locked by the caller.  The user is
 * then responsible for unlocking \c mutex.  If the system time exceeds
 * \c abstime before \c cond is signaled, \c ABT_ERR_COND_TIMEDOUT is returned.
 *
 * @note
 * \c clock_gettime() can be used to obtain the current system time.
 * @code{.c}
 * // #include <time.h> is needed.
 * struct timespec ts;
 * clock_gettime(CLOCK_REALTIME, &ts);
 * ts.tv_sec += 10; // timeout = current time + 10 seconds.
 * ABT_cond_timedwait(cond, mutex, &ts);
 * @endcode
 *
 * This routine associates \c mutex with \c cond until this routine returns, so
 * the user may not use more than one mutex for the same \c cond.
 *
 * This routine returns with \c mutex locked even if an error occurs.
 *
 * If \c mutex is a recursive mutex, \c mutex must be locked only once by the
 * caller.
 *
 * @note
 * Unlike other implementations of condition variables, a spurious wakeup never
 * occurs.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS_COND_SIGNALED
 * \DOC_ERROR_SUCCESS_COND_TIMEDOUT
 * \DOC_ERROR_INV_COND_HANDLE{\c cond}
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c abstime}
 * \DOC_UNDEFINED_NOT_LOCKED{\c mutex}
 * \DOC_UNDEFINED_MUTEX_ILLEGAL_UNLOCK{\c mutex}
 * \DOC_UNDEFINED_COND_WAIT{\c cond, \c mutex}
 *
 * @param[in] cond     condition variable handle
 * @param[in] mutex    mutex handle
 * @param[in] abstime  absolute time for timeout
 * @return Error code
 */
int ABT_cond_timedwait(ABT_cond cond, ABT_mutex mutex,
                       const struct timespec *abstime)
{
    ABTI_UB_ASSERT(abstime);

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_cond *p_cond = ABTI_cond_get_ptr(cond);
    ABTI_CHECK_NULL_COND_PTR(p_cond);
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);

    /* Check if a given mutex is valid. */
    /* p_mutex must be locked. */
    ABTI_UB_ASSERT(ABTI_mutex_is_locked(p_mutex));
    /* If p_mutex is recursive, the caller of this function must be an owner. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->owner_id != ABTI_self_get_thread_id(p_local)));
    /* If p_mutex is recursive, p_mutex must not be locked more than once. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->nesting_cnt > 1));

    double tar_time = convert_timespec_to_sec(abstime);

    ABTI_thread thread;
    thread.type = ABTI_THREAD_TYPE_EXT;
    ABTD_atomic_relaxed_store_int(&thread.state, ABT_THREAD_STATE_BLOCKED);

    ABTD_spinlock_acquire(&p_cond->lock);

    if (p_cond->p_waiter_mutex == NULL) {
        p_cond->p_waiter_mutex = p_mutex;
    } else {
        if (p_cond->p_waiter_mutex != p_mutex) {
            ABTD_spinlock_release(&p_cond->lock);
            ABTI_HANDLE_ERROR(ABT_ERR_INV_MUTEX);
        }
    }

    /* Unlock the mutex that the calling ULT is holding */
    ABTI_mutex_unlock(p_local, p_mutex);
    ABT_bool is_timedout =
        ABTI_waitlist_wait_timedout_and_unlock(&p_local, &p_cond->waitlist,
                                               &p_cond->lock, tar_time,
                                               ABT_SYNC_EVENT_TYPE_COND,
                                               (void *)p_cond);
    /* Lock the mutex again */
    ABTI_mutex_lock(&p_local, p_mutex);
    return is_timedout ? ABT_ERR_COND_TIMEDOUT : ABT_SUCCESS;
}

/**
 * @ingroup COND
 * @brief   Signal a condition.
 *
 * \c ABT_cond_signal() signals another waiter that is blocked on the condition
 * variable \c cond.  Only one waiter is signaled and woken up.  The caller does
 * not need to be holding a mutex associated with \c cond.  This routine has no
 * effect if no waiter is currently blocked on \c cond.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_COND_HANDLE{\c cond}
 *
 * @param[in] cond  condition variable handle
 * @return Error code
 */
int ABT_cond_signal(ABT_cond cond)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_cond *p_cond = ABTI_cond_get_ptr(cond);
    ABTI_CHECK_NULL_COND_PTR(p_cond);

    ABTD_spinlock_acquire(&p_cond->lock);
    ABTI_waitlist_signal(p_local, &p_cond->waitlist);
    ABTD_spinlock_release(&p_cond->lock);

    return ABT_SUCCESS;
}

/**
 * @ingroup COND
 * @brief   Broadcast a condition.
 *
 * \c ABT_cond_broadcast() signals all waiters that are blocked on the condition
 * variable \c cond.  The caller does not need to be holding a mutex associated
 * with \c cond.  This routine has no effect if no waiter is currently blocked
 * on \c cond.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_COND_HANDLE{\c cond}
 *
 * @param[in] cond  condition variable handle
 * @return Error code
 */
int ABT_cond_broadcast(ABT_cond cond)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_cond *p_cond = ABTI_cond_get_ptr(cond);
    ABTI_CHECK_NULL_COND_PTR(p_cond);

    ABTI_cond_broadcast(p_local, p_cond);
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static inline double convert_timespec_to_sec(const struct timespec *p_ts)
{
    double secs;
    secs = ((double)p_ts->tv_sec) + 1.0e-9 * ((double)p_ts->tv_nsec);
    return secs;
}
