/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup FUTURE Future
 * This group is for Future.
 */

/**
 * @ingroup FUTURE
 * @brief   Create a new future.
 *
 * \c ABT_future_create() creates a new future and returns its handle through
 * \c newfuture.  \c newfuture is unready and has \c num_compartments
 * compartments.  \c newfuture gets ready if \c ABT_future_set() for
 * \c newfuture succeeds \c num_compartments times.
 *
 * @note
 * Calling \c ABT_future_set() for a future that has no compartment is
 * erroneous.  \c ABT_future_wait() and \c ABT_future_test() succeed without
 * \c ABT_future_set() for a future that has no compartment.\n
 * \c cb_func() is never called if \c num_compartments is zero.
 *
 * If \c cb_func is not \c NULL, the callback function \c cb_func() is
 * registered to \c future.  \c cb_func() will be called before all the
 * compartments are set by \c ABT_future_set().  The caller of \c cb_func() is
 * undefined, so a program that relies on the caller of \c cb_func() is
 * non-conforming.  The state of the future that invokes \c cb_func() is
 * undefined in \c cb_func().  The argument \c arg of \c cb_func() is a properly
 * aligned array each of which element stores \c value passed to
 * \c ABT_future_set().  The contents of \c arg are read-only and may not be
 * accessed after \c cb_func() finishes.
 *
 * \c newfuture must be freed by \c ABT_future_free() after its use.
 *
 * @changev11
 * \DOC_DESC_V10_FUTURE_COMPARTMENT_ORDER
 * @endchangev11
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
 * \DOC_UNDEFINED_NULL_PTR{\c newfuture}
 * \DOC_UNDEFINED_FUTURE_CALLBACK{\c cb_func(), \c arg}
 *
 * @param[in]  num_compartments  number of compartments of the future
 * @param[in]  cb_func           callback function to be called when the future
 *                               is ready
 * @param[out] newfuture         future handle
 * @return Error code
 */
int ABT_future_create(uint32_t num_compartments, void (*cb_func)(void **arg),
                      ABT_future *newfuture)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newfuture);

    int abt_errno;
    ABTI_future *p_future;
    size_t arg_num_compartments = num_compartments;

    abt_errno = ABTU_malloc(sizeof(ABTI_future), (void **)&p_future);
    ABTI_CHECK_ERROR(abt_errno);
    ABTD_spinlock_clear(&p_future->lock);
    ABTD_atomic_relaxed_store_size(&p_future->counter, 0);
    p_future->num_compartments = arg_num_compartments;
    if (arg_num_compartments > 0) {
        abt_errno = ABTU_malloc(arg_num_compartments * sizeof(void *),
                                (void **)&p_future->array);
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            ABTU_free(p_future);
            ABTI_HANDLE_ERROR(abt_errno);
        }
    } else {
        p_future->array = NULL;
    }
    p_future->p_callback = cb_func;
    ABTI_waitlist_init(&p_future->waitlist);

    *newfuture = ABTI_future_get_handle(p_future);
    return ABT_SUCCESS;
}

/**
 * @ingroup FUTURE
 * @brief   Free a future.
 *
 * \c ABT_future_free() deallocates the resource used for the future \c future
 * and sets \c future to \c ABT_FUTURE_NULL.
 *
 * @note
 * This routine frees \c future regardless of its readiness.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_FUTURE_PTR{\c future}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c future}
 * \DOC_UNDEFINED_WAITER{\c future}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c future}
 *
 * @param[in,out] future  future handle
 * @return Error code
 */
int ABT_future_free(ABT_future *future)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(future);

    ABTI_future *p_future = ABTI_future_get_ptr(*future);
    ABTI_CHECK_NULL_FUTURE_PTR(p_future);

    /* The lock needs to be acquired to safely free the future structure.
     * However, we do not have to unlock it because the entire structure is
     * freed here. */
    ABTD_spinlock_acquire(&p_future->lock);
    ABTI_UB_ASSERT(ABTI_waitlist_is_empty(&p_future->waitlist));

    ABTU_free(p_future->array);
    ABTU_free(p_future);

    *future = ABT_FUTURE_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup FUTURE
 * @brief   Wait on a future.
 *
 * The caller of \c ABT_future_wait() waits on the future \c future.  If
 * \c future is ready, this routine returns immediately.  If \c future is not
 * ready, the caller of this routine suspends.  The caller will be resumed once
 * \c future gets ready.
 *
 * \DOC_DESC_ATOMICITY_FUTURE_READINESS
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_FUTURE}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOTASK \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{
 * \c future is not ready}\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{\c future is
 *                                                               not ready}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_FUTURE_HANDLE{\c future}
 * \DOC_V1X \DOC_ERROR_TASK{\c ABT_ERR_FUTURE}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] future  future handle
 * @return Error code
 */
int ABT_future_wait(ABT_future future)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_future *p_future = ABTI_future_get_ptr(future);
    ABTI_CHECK_NULL_FUTURE_PTR(p_future);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Calling this routine on a tasklet is not allowed. */
    if (ABTI_IS_ERROR_CHECK_ENABLED && p_local) {
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
        ABTI_CHECK_TRUE(p_local_xstream->p_thread->type &
                            ABTI_THREAD_TYPE_YIELDABLE,
                        ABT_ERR_FUTURE);
    }
#endif

    ABTD_spinlock_acquire(&p_future->lock);
    if (ABTD_atomic_relaxed_load_size(&p_future->counter) <
        p_future->num_compartments) {
        ABTI_waitlist_wait_and_unlock(&p_local, &p_future->waitlist,
                                      &p_future->lock,
                                      ABT_SYNC_EVENT_TYPE_FUTURE,
                                      (void *)p_future);
    } else {
        ABTD_spinlock_release(&p_future->lock);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup FUTURE
 * @brief   Check if a future is ready.
 *
 * \c ABT_future_test() checks if the future \c future is ready and returns the
 * result through \c is_ready.  If \c future is ready, this routine sets
 * \c is_ready to \c ABT_TRUE.  Otherwise, \c is_ready is set to \c ABT_FALSE.
 * This routine returns \c ABT_SUCCESS even if \c future is not ready.
 *
 * \DOC_DESC_ATOMICITY_FUTURE_READINESS
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_FUTURE_HANDLE{\c future}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_ready}
 *
 * @param[in]  future    handle to the future
 * @param[out] is_ready  \c ABT_TRUE if future is ready; otherwise, \c ABT_FALSE
 * @return Error code
 */
int ABT_future_test(ABT_future future, ABT_bool *is_ready)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_ready);

    ABTI_future *p_future = ABTI_future_get_ptr(future);
    ABTI_CHECK_NULL_FUTURE_PTR(p_future);

    size_t counter = ABTD_atomic_acquire_load_size(&p_future->counter);
    *is_ready = (counter == p_future->num_compartments) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
}

/**
 * @ingroup FUTURE
 * @brief   Signal a future.
 *
 * \c ABT_future_set() sets a value \c value to one of the unset compartments of
 * the future \c future.  If all the compartments of \c future are set, this
 * routine makes \c future ready and wakes up all waiters that are blocked on
 * \c future.  If the callback function is set to \c future, the callback
 * function is triggered before \c future is set to ready.
 *
 * \DOC_DESC_ATOMICITY_FUTURE_READINESS
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_FUTURE_HANDLE{\c future}
 * \DOC_ERROR_FUTURE_READY{\c future}
 * \DOC_ERROR_FUTURE_NO_COMPARTMENT{\c future}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] future  future handle
 * @param[in] value   value set to one of the compartments of \c future
 * @return Error code
 */
int ABT_future_set(ABT_future future, void *value)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_future *p_future = ABTI_future_get_ptr(future);
    ABTI_CHECK_NULL_FUTURE_PTR(p_future);

    ABTD_spinlock_acquire(&p_future->lock);

    size_t counter = ABTD_atomic_relaxed_load_size(&p_future->counter);
    size_t num_compartments = p_future->num_compartments;
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    /* If num_compartments is 0, this routine always returns ABT_ERR_FUTURE */
    if (counter >= num_compartments) {
        ABTD_spinlock_release(&p_future->lock);
        ABTI_HANDLE_ERROR(ABT_ERR_FUTURE);
    }
#endif
    p_future->array[counter] = value;
    counter++;
    /* Call a callback function before setting the counter. */
    if (counter == num_compartments && p_future->p_callback != NULL) {
        (*p_future->p_callback)(p_future->array);
    }

    ABTD_atomic_release_store_size(&p_future->counter, counter);

    if (counter == num_compartments) {
        ABTI_waitlist_broadcast(p_local, &p_future->waitlist);
    }

    ABTD_spinlock_release(&p_future->lock);
    return ABT_SUCCESS;
}

/**
 * @ingroup FUTURE
 * @brief   Reset readiness of a future.
 *
 * \c ABT_future_reset() resets the readiness of the future \c future.
 * A future reset by this routine will get ready if \c ABT_future_set() succeeds
 * as many times as the number of its compartments.
 *
 * @note
 * This routine makes \c future unready irrespective of its readiness.
 *
 * \DOC_DESC_ATOMICITY_FUTURE_READINESS
 *
 * @note
 * This routine has no effect if the number of compartments of \c future is
 * zero.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_FUTURE_HANDLE{\c future}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WAITER{\c future}
 *
 * @param[in] future  future handle
 * @return Error code
 */
int ABT_future_reset(ABT_future future)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_future *p_future = ABTI_future_get_ptr(future);
    ABTI_CHECK_NULL_FUTURE_PTR(p_future);

    ABTD_spinlock_acquire(&p_future->lock);
    ABTI_UB_ASSERT(ABTI_waitlist_is_empty(&p_future->waitlist));
    ABTD_atomic_release_store_size(&p_future->counter, 0);
    ABTD_spinlock_release(&p_future->lock);
    return ABT_SUCCESS;
}
