/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup BARRIER Barrier
 * This group is for Barrier.
 */

/**
 * @ingroup BARRIER
 * @brief   Create a new barrier.
 *
 * \c ABT_barrier_create() creates a new barrier and returns its handle through
 * \c newbarrier.  \c num_waiters specifies the number of waiters that must call
 * \c ABT_barrier_wait() before any of the waiters successfully return from the
 * call.  \c num_waiters must be greater than zero.
 *
 * \c newbarrier must be freed by \c ABT_barrier_free() after its use.
 *
 * @changev11
 * \DOC_DESC_V10_BARRIER_NUM_WAITERS{\c num_waiters}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newbarrier, \c ABT_BARRIER_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_ZERO{\c num_waiters}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newbarrier}
 *
 * @param[in]  num_waiters  number of waiters
 * @param[out] newbarrier   barrier handle
 * @return Error code
 */
int ABT_barrier_create(uint32_t num_waiters, ABT_barrier *newbarrier)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newbarrier);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newbarrier to NULL on error. */
    *newbarrier = ABT_BARRIER_NULL;
#endif
    int abt_errno;
    ABTI_barrier *p_newbarrier;
    ABTI_CHECK_TRUE(num_waiters != 0, ABT_ERR_INV_ARG);
    size_t arg_num_waiters = num_waiters;

    abt_errno = ABTU_malloc(sizeof(ABTI_barrier), (void **)&p_newbarrier);
    ABTI_CHECK_ERROR(abt_errno);

    ABTD_spinlock_clear(&p_newbarrier->lock);
    p_newbarrier->num_waiters = arg_num_waiters;
    p_newbarrier->counter = 0;
    ABTI_waitlist_init(&p_newbarrier->waitlist);
    /* Return value */
    *newbarrier = ABTI_barrier_get_handle(p_newbarrier);
    return ABT_SUCCESS;
}

/**
 * @ingroup BARRIER
 * @brief   Reinitialize a barrier with a new number of waiters.
 *
 * \c ABT_barrier_reinit() reinitializes the barrier \c barrier with the new
 * number of waiters \c num_waiters.  \c num_waiters must be greater than zero.
 *
 * @changev11
 * \DOC_DESC_V10_BARRIER_NUM_WAITERS{\c num_waiters}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_ZERO{\c num_waiters}
 * \DOC_ERROR_INV_BARRIER_HANDLE{\c barrier}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WAITER{\c barrier}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c barrier}
 *
 * @param[in] barrier      barrier handle
 * @param[in] num_waiters  number of waiters
 * @return Error code
 */
int ABT_barrier_reinit(ABT_barrier barrier, uint32_t num_waiters)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_barrier *p_barrier = ABTI_barrier_get_ptr(barrier);
    ABTI_CHECK_NULL_BARRIER_PTR(p_barrier);
    ABTI_UB_ASSERT(p_barrier->counter == 0);
    ABTI_CHECK_TRUE(num_waiters != 0, ABT_ERR_INV_ARG);
    size_t arg_num_waiters = num_waiters;

    /* Only when num_waiters is different from p_barrier->num_waiters, we
     * change p_barrier. */
    if (arg_num_waiters != p_barrier->num_waiters) {
        /* We can reuse waiters and waiter_type arrays */
        p_barrier->num_waiters = arg_num_waiters;
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup BARRIER
 * @brief   Free a barrier.
 *
 * \c ABT_barrier_free() deallocates the resource used for the barrier
 * \c barrier and sets \c barrier to \c ABT_BARRIER_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_BARRIER_PTR{\c barrier}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c barrier}
 * \DOC_UNDEFINED_WAITER{\c barrier}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c barrier}
 *
 * @param[in,out] barrier  barrier handle
 * @return Error code
 */
int ABT_barrier_free(ABT_barrier *barrier)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(barrier);

    ABT_barrier h_barrier = *barrier;
    ABTI_barrier *p_barrier = ABTI_barrier_get_ptr(h_barrier);
    ABTI_CHECK_NULL_BARRIER_PTR(p_barrier);

    /* The lock needs to be acquired to safely free the barrier structure.
     * However, we do not have to unlock it because the entire structure is
     * freed here. */
    ABTD_spinlock_acquire(&p_barrier->lock);

    /* p_barrier->counter must be checked after taking a lock. */
    ABTI_UB_ASSERT(p_barrier->counter == 0);

    ABTU_free(p_barrier);

    /* Return value */
    *barrier = ABT_BARRIER_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup BARRIER
 * @brief   Wait on a barrier.
 *
 * The caller of \c ABT_barrier_wait() waits on the barrier \c barrier.  The
 * caller suspends until as many waiters as the number of waiters specified by
 * \c ABT_barrier_create() or \c ABT_barrier_reinit() reach \c barrier.
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_BARRIER}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOTASK \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{the
 * caller is not the last waiter that waits on \c barrier}\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{the caller is
 * not the last waiter that waits on \c barrier}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_BARRIER_HANDLE{\c barrier}
 * \DOC_V1X \DOC_ERROR_TASK{\c ABT_ERR_BARRIER}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] barrier  barrier handle
 * @return Error code
 */
int ABT_barrier_wait(ABT_barrier barrier)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_barrier *p_barrier = ABTI_barrier_get_ptr(barrier);
    ABTI_CHECK_NULL_BARRIER_PTR(p_barrier);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Calling a barrier on a tasklet is not allowed. */
    if (ABTI_IS_ERROR_CHECK_ENABLED && p_local) {
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
        ABTI_CHECK_TRUE(p_local_xstream->p_thread->type &
                            ABTI_THREAD_TYPE_YIELDABLE,
                        ABT_ERR_BARRIER);
    }
#endif

    ABTD_spinlock_acquire(&p_barrier->lock);

    ABTI_ASSERT(p_barrier->counter < p_barrier->num_waiters);
    p_barrier->counter++;

    /* If we do not have all the waiters yet */
    if (p_barrier->counter < p_barrier->num_waiters) {
        ABTI_waitlist_wait_and_unlock(&p_local, &p_barrier->waitlist,
                                      &p_barrier->lock,
                                      ABT_SYNC_EVENT_TYPE_BARRIER,
                                      (void *)p_barrier);
    } else {
        ABTI_waitlist_broadcast(p_local, &p_barrier->waitlist);
        /* Reset counter */
        p_barrier->counter = 0;
        ABTD_spinlock_release(&p_barrier->lock);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup BARRIER
 * @brief   Get the number of waiters of a barrier.
 *
 * \c ABT_barrier_get_num_waiters() returns the number of waiters of the barrier
 * \c barrier through \c num_waiters.  The number of waiters is set by
 * \c ABT_barrier_create() and can be updated by \c ABT_barrier_reinit().
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_BARRIER_HANDLE{\c barrier}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c num_waiters}
 *
 * @param[in]  barrier      handle to the barrier
 * @param[out] num_waiters  number of waiters
 * @return Error code
 */
int ABT_barrier_get_num_waiters(ABT_barrier barrier, uint32_t *num_waiters)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(num_waiters);

    ABTI_barrier *p_barrier = ABTI_barrier_get_ptr(barrier);
    ABTI_CHECK_NULL_BARRIER_PTR(p_barrier);

    *num_waiters = p_barrier->num_waiters;
    return ABT_SUCCESS;
}
