/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup EVENTUAL Eventual
 * This group is for Eventual.
 */

/**
 * @ingroup EVENTUAL
 * @brief   Create a new eventual.
 *
 * \c ABT_eventual_create() creates a new eventual and returns its handle
 * through \c neweventual.  \c neweventual is set to unready.  If \c nbytes is
 * greater than zero, this routine allocates a memory buffer of \c nbytes bytes
 * for \c neweventual.  This memory buffer can be set by \c ABT_eventual_set()
 * and read by \c ABT_eventual_wait() or \c ABT_eventual_test().  If \c nbytes
 * is zero, the user cannot pass data from a setter to waiters through
 * \c neweventual.
 *
 * \c neweventual must be freed by \c ABT_eventual_free() after its use.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c nbytes}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c neweventual}
 *
 * @param[in]  nbytes       size in bytes of the memory buffer
 * @param[out] neweventual  eventual handle
 * @return Error code
 */
int ABT_eventual_create(int nbytes, ABT_eventual *neweventual)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(neweventual);

    /* Check if the size of ABT_eventual_memory is okay. */
    ABTI_STATIC_ASSERT(sizeof(ABTI_eventual) <= sizeof(ABT_eventual_memory));

    int abt_errno;
    ABTI_eventual *p_eventual;
    ABTI_CHECK_TRUE(nbytes >= 0, ABT_ERR_INV_ARG);
    size_t arg_nbytes = nbytes;

    abt_errno = ABTU_malloc(sizeof(ABTI_eventual), (void **)&p_eventual);
    ABTI_CHECK_ERROR(abt_errno);

    ABTD_spinlock_clear(&p_eventual->lock);
    p_eventual->ready = ABT_FALSE;
    p_eventual->nbytes = arg_nbytes;
    if (arg_nbytes == 0) {
        p_eventual->value = NULL;
    } else {
        abt_errno = ABTU_malloc(arg_nbytes, &p_eventual->value);
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            ABTU_free(p_eventual);
            ABTI_HANDLE_ERROR(abt_errno);
        }
    }
    ABTI_waitlist_init(&p_eventual->waitlist);

    *neweventual = ABTI_eventual_get_handle(p_eventual);
    return ABT_SUCCESS;
}

/**
 * @ingroup EVENTUAL
 * @brief   Free an eventual.
 *
 * \c ABT_eventual_free() deallocates the resource used for the eventual
 * \c eventual and sets \c eventual to \c ABT_EVENTUAL_NULL.
 *
 * @note
 * This routine frees \c eventual regardless of its readiness.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_EVENTUAL_PTR{\c eventual}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c eventual}
 * \DOC_UNDEFINED_WAITER{\c eventual}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c eventual}
 *
 * @param[in,out] eventual  eventual handle
 * @return Error code
 */
int ABT_eventual_free(ABT_eventual *eventual)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(eventual);

    ABTI_eventual *p_eventual = ABTI_eventual_get_ptr(*eventual);
    ABTI_CHECK_NULL_EVENTUAL_PTR(p_eventual);

    /* The lock needs to be acquired to safely free the eventual structure.
     * However, we do not have to unlock it because the entire structure is
     * freed here. */
    ABTD_spinlock_acquire(&p_eventual->lock);
    ABTI_UB_ASSERT(ABTI_waitlist_is_empty(&p_eventual->waitlist));

    if (p_eventual->value)
        ABTU_free(p_eventual->value);
    ABTU_free(p_eventual);

    *eventual = ABT_EVENTUAL_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup EVENTUAL
 * @brief   Wait on an eventual.
 *
 * The caller of \c ABT_eventual_wait() waits on the eventual \c eventual.  If
 * \c eventual is ready, this routine returns immediately.  If \c eventual is
 * not ready, the caller suspends and will be resumed once \c eventual gets
 * ready.
 *
 * If \c value is not \c NULL, \c value is set to the memory buffer of
 * \c eventual.  If \c value is not \c NULL but the size of the memory buffer of
 * \c eventual (i.e., \c nbytes passed to \c ABT_eventual_create()) is zero,
 * \c value is set to \c NULL.
 *
 * The memory buffer pointed to by \c value is deallocated when \c eventual is
 * freed by \c ABT_eventual_free().  The memory buffer is properly aligned for
 * storage of any type of object that has the given size.  If the data written
 * by \c ABT_eventual_set() is smaller than the size of the memory buffer of
 * \c eventual, the contents of the memory buffer that was not written by
 * \c ABT_eventual_set() are undefined.  The contents of the memory buffer get
 * undefined if either \c ABT_eventual_set() or \c ABT_eventual_reset() is
 * called for \c eventual.  The memory buffer is read-only, so rewriting the
 * contents of the obtained memory buffer causes undefined behavior.
 *
 * \DOC_DESC_ATOMICITY_EVENTUAL_READINESS
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_EVENTUAL}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOTASK \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{
 * \c eventual is not ready}\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{\c eventual is
 *                                                               not ready}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_EVENTUAL_HANDLE{\c eventual}
 * \DOC_V1X \DOC_ERROR_TASK{\c ABT_ERR_EVENTUAL}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_EVENTUAL_BUFFER{\c eventual, \c value}
 *
 * @param[in]  eventual  eventual handle
 * @param[out] value     memory buffer of the eventual
 * @return Error code
 */
int ABT_eventual_wait(ABT_eventual eventual, void **value)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_eventual *p_eventual = ABTI_eventual_get_ptr(eventual);
    ABTI_CHECK_NULL_EVENTUAL_PTR(p_eventual);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* This routine cannot be called by a tasklet. */
    if (ABTI_IS_ERROR_CHECK_ENABLED && p_local) {
        ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
        ABTI_CHECK_TRUE(p_local_xstream->p_thread->type &
                            ABTI_THREAD_TYPE_YIELDABLE,
                        ABT_ERR_EVENTUAL);
    }
#endif

    ABTD_spinlock_acquire(&p_eventual->lock);
    if (p_eventual->ready == ABT_FALSE) {
        ABTI_waitlist_wait_and_unlock(&p_local, &p_eventual->waitlist,
                                      &p_eventual->lock,
                                      ABT_SYNC_EVENT_TYPE_EVENTUAL,
                                      (void *)p_eventual);
    } else {
        ABTD_spinlock_release(&p_eventual->lock);
    }
    /* This value is updated outside the critical section, but it is okay since
     * the "pointer" to the memory buffer is constant and there is no way to
     * avoid updating this memory buffer by ABT_eventual_set() etc. */
    if (value)
        *value = p_eventual->value;
    return ABT_SUCCESS;
}

/**
 * @ingroup EVENTUAL
 * @brief   Check if an eventual is ready.
 *
 * \c ABT_eventual_test() checks if the eventual \c eventual is ready and
 * returns the result through \c is_ready.  If \c eventual is not ready, this
 * routine leaves \c value unchanged and sets \c is_ready to \c ABT_FALSE.  If
 * \c eventual is ready, \c is_ready is set to \c ABT_TRUE and, if \c value is
 * not \c NULL, \c value is set to the memory buffer of \c eventual.  This
 * routine returns \c ABT_SUCCESS even if \c eventual is not ready.
 *
 * The memory buffer pointed to by \c value is deallocated when \c eventual is
 * freed by \c ABT_eventual_free().  The memory buffer is properly aligned for
 * storage of any type of object that has the given size.  If the data written
 * by \c ABT_eventual_set() is smaller than the size of the memory buffer of
 * \c eventual, the contents of the memory buffer that was not written by
 * \c ABT_eventual_set() are undefined.  The contents of the memory buffer get
 * undefined if either \c ABT_eventual_set() or \c ABT_eventual_reset() is
 * called for \c eventual.  The memory buffer is read-only, so rewriting the
 * contents of the obtained memory buffer causes undefined behavior.
 *
 * \DOC_DESC_ATOMICITY_EVENTUAL_READINESS
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_EVENTUAL_HANDLE{\c eventual}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_ready}
 * \DOC_UNDEFINED_EVENTUAL_BUFFER{\c eventual, \c value}
 *
 * @param[in]  eventual  handle to the eventual
 * @param[out] value     pointer to the memory buffer of the eventual
 * @param[out] is_ready  user flag
 * @return Error code
 */
int ABT_eventual_test(ABT_eventual eventual, void **value, ABT_bool *is_ready)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_ready);

    ABTI_eventual *p_eventual = ABTI_eventual_get_ptr(eventual);
    ABTI_CHECK_NULL_EVENTUAL_PTR(p_eventual);
    ABT_bool flag = ABT_FALSE;

    ABTD_spinlock_acquire(&p_eventual->lock);
    if (p_eventual->ready != ABT_FALSE) {
        if (value)
            *value = p_eventual->value;
        flag = ABT_TRUE;
    }
    ABTD_spinlock_release(&p_eventual->lock);

    *is_ready = flag;
    return ABT_SUCCESS;
}

/**
 * @ingroup EVENTUAL
 * @brief   Signal an eventual.
 *
 * \c ABT_eventual_set() makes the eventual \c eventual ready and resumes all
 * waiters that are blocked on \c eventual.  If \c nbytes is greater than zero,
 * this routine copies \c nbytes bytes of the memory buffer pointed to by
 * \c value to the memory buffer of \c eventual before making \c eventual ready.
 *
 * @note
 * A ready eventual can be set to unready by \c ABT_eventual_reset().
 *
 * \DOC_DESC_ATOMICITY_EVENTUAL_READINESS
 *
 * @changev11
 * \DOC_DESC_V10_EVENTUAL_UPDATE_READY_BUFFER{\c eventual}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_ERROR_CODE_CHANGE{\c ABT_ERR_INV_EVENTUAL, \c ABT_ERR_INV_ARG,
 *                                 \c nbytes is greater than the size of the
 *                                 memory buffer of \c eventual}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c nbytes}
 * \DOC_ERROR_INV_EVENTUAL_HANDLE{\c eventual}
 * \DOC_ERROR_EVENTUAL_READY{\c eventual}
 * \DOC_V1X \DOC_ERROR_INV_EVENTUAL_GREATER_THAN{\c nbytes, the size of the
 *                                               memory buffer of \c eventual}
 * \DOC_V20 \DOC_ERROR_INV_ARG_GREATER_THAN{\c nbytes, the size of the memory
 *                                          buffer of \c eventual}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c value, \c nbytes is greater than zero}
 *
 * @param[in] eventual  eventual handle
 * @param[in] value     pointer to the memory buffer
 * @param[in] nbytes    number of bytes to be copied
 * @return Error code
 */
int ABT_eventual_set(ABT_eventual eventual, void *value, int nbytes)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(value || nbytes <= 0);

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_eventual *p_eventual = ABTI_eventual_get_ptr(eventual);
    ABTI_CHECK_NULL_EVENTUAL_PTR(p_eventual);
    ABTI_CHECK_TRUE(nbytes >= 0, ABT_ERR_INV_ARG);
    size_t arg_nbytes = nbytes;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x */
    ABTI_CHECK_TRUE(arg_nbytes <= p_eventual->nbytes, ABT_ERR_INV_EVENTUAL);
#else
    /* Argobots 2.0 */
    ABTI_CHECK_TRUE(arg_nbytes <= p_eventual->nbytes, ABT_ERR_INV_ARG);
#endif

    ABTD_spinlock_acquire(&p_eventual->lock);

    ABT_bool ready = p_eventual->ready;
    if (ready == ABT_FALSE) {
        if (p_eventual->value)
            memcpy(p_eventual->value, value, arg_nbytes);
        p_eventual->ready = ABT_TRUE;
        /* Wake up all waiting ULTs */
        ABTI_waitlist_broadcast(p_local, &p_eventual->waitlist);
        ABTD_spinlock_release(&p_eventual->lock);
    } else {
        ABTD_spinlock_release(&p_eventual->lock);
        /* It has been ready.  Error. */
        ABTI_HANDLE_ERROR(ABT_ERR_EVENTUAL);
    }

    return ABT_SUCCESS;
}

/**
 * @ingroup EVENTUAL
 * @brief   Reset a readiness of an eventual.
 *
 * \c ABT_eventual_reset() makes the eventual \c eventual unready.
 *
 * @note
 * This routine makes \c eventual unready irrespective of its readiness.
 *
 * \DOC_DESC_ATOMICITY_EVENTUAL_READINESS
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_EVENTUAL_HANDLE{\c eventual}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WAITER{\c eventual}
 *
 * @param[in] eventual  eventual handle
 * @return Error code
 */
int ABT_eventual_reset(ABT_eventual eventual)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_eventual *p_eventual = ABTI_eventual_get_ptr(eventual);
    ABTI_CHECK_NULL_EVENTUAL_PTR(p_eventual);

    ABTD_spinlock_acquire(&p_eventual->lock);
    ABTI_UB_ASSERT(ABTI_waitlist_is_empty(&p_eventual->waitlist));
    p_eventual->ready = ABT_FALSE;
    ABTD_spinlock_release(&p_eventual->lock);
    return ABT_SUCCESS;
}
