/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup ES_BARRIER Execution-Stream Barrier
 * This group is for Execution-Stream Barrier.
 */

/**
 * @ingroup ES_BARRIER
 * @brief   Create a new execution-stream barrier.
 *
 * \c ABT_xstream_barrier_create() creates a new execution-stream barrier and
 * returns its handle through \c newbarrier.  \c num_waiters specifies the
 * number of waiters that must call \c ABT_xstream_barrier_wait() before any of
 * the waiters successfully return from the call.  \c num_waiters must be
 * greater than zero.
 *
 * \c newbarrier must be freed by \c ABT_xstream_barrier_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newbarrier, \c ABT_XSTREAM_BARRIER_NULL}
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
 * @param[out] newbarrier   execution-stream barrier handle
 * @return Error code
 */
int ABT_xstream_barrier_create(uint32_t num_waiters,
                               ABT_xstream_barrier *newbarrier)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newbarrier);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newbarrier to NULL on error. */
    *newbarrier = ABT_XSTREAM_BARRIER_NULL;
#endif
    int abt_errno;
    ABTI_xstream_barrier *p_newbarrier;
    ABTI_CHECK_TRUE(num_waiters != 0, ABT_ERR_INV_ARG);

    abt_errno =
        ABTU_malloc(sizeof(ABTI_xstream_barrier), (void **)&p_newbarrier);
    ABTI_CHECK_ERROR(abt_errno);

    p_newbarrier->num_waiters = num_waiters;
#ifdef HAVE_PTHREAD_BARRIER_INIT
    abt_errno = ABTD_xstream_barrier_init(num_waiters, &p_newbarrier->bar);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        ABTU_free(p_newbarrier);
        ABTI_HANDLE_ERROR(abt_errno);
    }
#else
    ABTD_spinlock_clear(&p_newbarrier->lock);
    p_newbarrier->counter = 0;
    ABTD_atomic_relaxed_store_uint64(&p_newbarrier->tag, 0);
#endif

    /* Return value */
    *newbarrier = ABTI_xstream_barrier_get_handle(p_newbarrier);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES_BARRIER
 * @brief   Free an execution-stream barrier.
 *
 * \c ABT_xstream_barrier_free() deallocates the resource used for the
 * execution-stream barrier \c barrier and sets \c barrier to
 * \c ABT_XSTREAM_BARRIER_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_BARRIER_PTR{\c barrier}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c barrier}
 * \DOC_UNDEFINED_WAITER{\c barrier}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c barrier}
 *
 * @param[in,out] barrier  execution-stream barrier handle
 * @return Error code
 */
int ABT_xstream_barrier_free(ABT_xstream_barrier *barrier)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(barrier);

    ABT_xstream_barrier h_barrier = *barrier;
    ABTI_xstream_barrier *p_barrier = ABTI_xstream_barrier_get_ptr(h_barrier);
    ABTI_CHECK_NULL_XSTREAM_BARRIER_PTR(p_barrier);

#ifdef HAVE_PTHREAD_BARRIER_INIT
    ABTD_xstream_barrier_destroy(&p_barrier->bar);
#endif
    ABTU_free(p_barrier);

    /* Return value */
    *barrier = ABT_XSTREAM_BARRIER_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES_BARRIER
 * @brief   Wait on an execution-stream barrier.
 *
 * The caller of \c ABT_xstream_barrier_wait() waits on the execution-stream
 * barrier \c barrier.  The caller is blocked until as many waiters as the
 * number of waiters specified by \c ABT_xstream_barrier_create() reach
 * \c barrier.  If the caller is either a ULT or a tasklet, the underlying
 * execution stream is blocked on \c barrier.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_BARRIER_HANDLE{\c barrier}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] barrier  execution-stream barrier handle
 * @return Error code
 */
int ABT_xstream_barrier_wait(ABT_xstream_barrier barrier)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream_barrier *p_barrier = ABTI_xstream_barrier_get_ptr(barrier);
    ABTI_CHECK_NULL_XSTREAM_BARRIER_PTR(p_barrier);

    if (p_barrier->num_waiters > 1) {
#ifdef HAVE_PTHREAD_BARRIER_INIT
        ABTD_xstream_barrier_wait(&p_barrier->bar);
#else
        /* The following implementation is a simple sense-reversal barrier
         * implementation while it uses uint64_t instead of boolean to prevent
         * a sense variable from wrapping around. */
        ABTD_spinlock_acquire(&p_barrier->lock);
        p_barrier->counter++;
        if (p_barrier->counter == p_barrier->num_waiters) {
            /* Wake up the other waiters. */
            p_barrier->counter = 0;
            /* Updating tag wakes up other waiters.  Note that this tag is
             * sufficiently large, so it will not wrap around. */
            uint64_t cur_tag = ABTD_atomic_relaxed_load_uint64(&p_barrier->tag);
            uint64_t new_tag = (cur_tag + 1) & (UINT64_MAX >> 1);
            ABTD_atomic_release_store_uint64(&p_barrier->tag, new_tag);
            ABTD_spinlock_release(&p_barrier->lock);
        } else {
            /* Wait until the tag is updated by the last waiter */
            uint64_t cur_tag = ABTD_atomic_relaxed_load_uint64(&p_barrier->tag);
            ABTD_spinlock_release(&p_barrier->lock);
            while (cur_tag == ABTD_atomic_acquire_load_uint64(&p_barrier->tag))
                ABTD_atomic_pause();
        }
#endif
    }
    return ABT_SUCCESS;
}
