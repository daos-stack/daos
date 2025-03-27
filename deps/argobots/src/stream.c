/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTU_ret_err static int xstream_create(ABTI_global *p_global,
                                       ABTI_sched *p_sched,
                                       ABTI_xstream_type xstream_type, int rank,
                                       ABT_bool start,
                                       ABTI_xstream **pp_xstream);
ABTU_ret_err static int xstream_join(ABTI_local **pp_local,
                                     ABTI_xstream *p_xstream);
static ABT_bool xstream_set_new_rank(ABTI_global *p_global,
                                     ABTI_xstream *p_newxstream, int rank);
static ABT_bool xstream_change_rank(ABTI_global *p_global,
                                    ABTI_xstream *p_xstream, int rank);
static void xstream_return_rank(ABTI_global *p_global, ABTI_xstream *p_xstream);
static void xstream_init_main_sched(ABTI_xstream *p_xstream,
                                    ABTI_sched *p_sched);
ABTU_ret_err static int
xstream_update_main_sched(ABTI_global *p_global,
                          ABTI_xstream **pp_local_xstream,
                          ABTI_xstream *p_xstream, ABTI_sched *p_sched);
static void *xstream_launch_root_ythread(void *p_xstream);

/** @defgroup ES Execution Stream
 * This group is for Execution Stream.
 */

/**
 * @ingroup ES
 * @brief   Create a new execution stream.
 *
 * \c ABT_xstream_create() creates a new execution stream with the scheduler
 * \c sched and returns its handle through \c newxstream.  If \c sched is
 * \c ABT_SCHED_NULL, the default scheduler with a basic FIFO queue and the
 * default scheduler configuration is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_SCHED\n
 * \DOC_NOTE_DEFAULT_POOL\n
 * \DOC_NOTE_DEFAULT_SCHED_CONFIG
 *
 * If \c sched is not \c ABT_SCHED_NULL, the user may not reuse \c sched to
 * create another execution stream.  If \c sched is not configured to be
 * automatically freed, it is the user's responsibility to free \c sched after
 * \c newxstream is freed.
 *
 * \c newxstream must be freed by \c ABT_xstream_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_CRUDE_SCHED_USED_CHECK{\c sched, \c ABT_ERR_INV_SCHED}
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newxstream, \c ABT_XSTREAM_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 * \DOC_V1X \DOC_ERROR_SCHED_USED{\c sched, \c ABT_ERR_INV_SCHED}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newxstream}
 * \DOC_V20 \DOC_UNDEFINED_SCHED_USED{\c sched}
 *
 * @param[in]  sched       scheduler handle for \c newxstream
 * @param[out] newxstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_create(ABT_sched sched, ABT_xstream *newxstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newxstream);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newxstream to NULL on error. */
    *newxstream = ABT_XSTREAM_NULL;
#endif
    int abt_errno;
    ABTI_xstream *p_newxstream;

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    if (!p_sched) {
        abt_errno =
            ABTI_sched_create_basic(ABT_SCHED_DEFAULT, 0, NULL, NULL, &p_sched);
        ABTI_CHECK_ERROR(abt_errno);
    } else {
#ifndef ABT_CONFIG_ENABLE_VER_20_API
        ABTI_CHECK_TRUE(p_sched->used == ABTI_SCHED_NOT_USED,
                        ABT_ERR_INV_SCHED);
#else
        ABTI_UB_ASSERT(p_sched->used == ABTI_SCHED_NOT_USED);
#endif
    }

    abt_errno = xstream_create(p_global, p_sched, ABTI_XSTREAM_TYPE_SECONDARY,
                               -1, ABT_TRUE, &p_newxstream);
    if (abt_errno != ABT_SUCCESS) {
        if (!ABTI_sched_get_ptr(sched)) {
            ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                            ABT_FALSE);
        }
        ABTI_HANDLE_ERROR(abt_errno);
    }

    /* Return value */
    *newxstream = ABTI_xstream_get_handle(p_newxstream);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Create a new execution stream with a predefined scheduler.
 *
 * ABT_xstream_create_basic() creates a new execution stream with the predefined
 * scheduler \c predef and returns its handle through \c newxstream.  The
 * functionality provided by this routine is the same as the combination of
 * \c ABT_sched_create_basic() and \c ABT_xstream_create().
 *
 * @code{.c}
 * int ABT_xstream_create_basic(...) {
 *   int abt_errno;
 *   ABT_sched sched = ABT_SCHED_NULL;
 *   abt_errno = ABT_sched_create_basic(predef, num_pools, pools, config,
 *                                      sched);
 *   if (abt_errno == ABT_SUCCESS)
 *     abt_errno = ABT_xstream_create(sched, newxstream);
 *   if (abt_errno != ABT_SUCCESS && sched != ABT_SCHED_NULL)
 *     ABT_sched_free(&sched);
 *   return abt_errno;
 * }
 * @endcode
 *
 * Please see \c ABT_sched_create_basic() and \c ABT_xstream_create() for
 * details.
 *
 * \c newxstream must be freed by \c ABT_xstream_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newxstream, \c ABT_XSTREAM_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c num_pools}
 * \DOC_ERROR_INV_ARG_INV_SCHED_PREDEF{\c predef}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c pools, \c num_pools is positive}
 * \DOC_UNDEFINED_NULL_PTR{\c newxstream}
 *
 * @param[in]  predef      predefined scheduler
 * @param[in]  num_pools   number of pools associated with the scheduler
 * @param[in]  pools       pools associated with the scheduler
 * @param[in]  config      scheduler config for scheduler creation
 * @param[out] newxstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_create_basic(ABT_sched_predef predef, int num_pools,
                             ABT_pool *pools, ABT_sched_config config,
                             ABT_xstream *newxstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pools || num_pools <= 0);
    ABTI_UB_ASSERT(newxstream);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newxstream to NULL on error. */
    *newxstream = ABT_XSTREAM_NULL;
#endif
    ABTI_CHECK_TRUE(num_pools >= 0, ABT_ERR_INV_ARG);

    int abt_errno;
    ABTI_xstream *p_newxstream;
    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_sched *p_sched;
    abt_errno =
        ABTI_sched_create_basic(predef, num_pools, pools, p_config, &p_sched);
    ABTI_CHECK_ERROR(abt_errno);

    abt_errno = xstream_create(p_global, p_sched, ABTI_XSTREAM_TYPE_SECONDARY,
                               -1, ABT_TRUE, &p_newxstream);
    if (abt_errno != ABT_SUCCESS) {
        int i;
        for (i = 0; i < num_pools; i++) {
            if (pools[i] != ABT_POOL_NULL) {
                /* Avoid freeing user-given pools. */
                ABTI_pool_release(ABTI_pool_get_ptr(p_sched->pools[i]));
                p_sched->pools[i] = ABT_POOL_NULL;
            }
        }
        ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                        ABT_FALSE);
        ABTI_HANDLE_ERROR(abt_errno);
    }

    *newxstream = ABTI_xstream_get_handle(p_newxstream);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Create a new execution stream with a specific rank.
 *
 * \c ABT_xstream_create_with_rank() creates a new execution stream with the
 * scheduler \c sched and returns its handle through \c newxstream.  If \c sched
 * is \c ABT_SCHED_NULL, the default scheduler with a basic FIFO queue and the
 * default scheduler configuration is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_SCHED\n
 * \DOC_NOTE_DEFAULT_POOL\n
 * \DOC_NOTE_DEFAULT_SCHED_CONFIG
 *
 * If \c sched is not \c ABT_SCHED_NULL, the user may not reuse \c sched to
 * create another execution stream.  If \c sched is not configured to be
 * automatically freed, it is the user's responsibility to free \c sched after
 * \c newxstream is freed.
 *
 * This routine allocates the rank \c rank for \c newxstream.  \c rank must be
 * non-negative and not used by another execution stream.
 *
 * \DOC_DESC_ATOMICITY_RANK
 *
 * \c newxstream must be freed by \c ABT_xstream_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_CRUDE_SCHED_USED_CHECK{\c sched, \c ABT_ERR_INV_SCHED}
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newxstream, \c ABT_XSTREAM_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_RANK{\c rank}
 * \DOC_ERROR_RESOURCE
 * \DOC_V1X \DOC_ERROR_SCHED_USED{\c sched, \c ABT_ERR_INV_SCHED}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newxstream}
 * \DOC_V20 \DOC_UNDEFINED_SCHED_USED{\c sched}
 *
 * @param[in]  sched       scheduler handle for \c newxstream
 * @param[in]  rank        execution stream rank
 * @param[out] newxstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_create_with_rank(ABT_sched sched, int rank,
                                 ABT_xstream *newxstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newxstream);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newxstream to NULL on error. */
    *newxstream = ABT_XSTREAM_NULL;
#endif
    int abt_errno;
    ABTI_xstream *p_newxstream;

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_CHECK_TRUE(rank >= 0, ABT_ERR_INV_XSTREAM_RANK);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    if (!p_sched) {
        abt_errno =
            ABTI_sched_create_basic(ABT_SCHED_DEFAULT, 0, NULL, NULL, &p_sched);
        ABTI_CHECK_ERROR(abt_errno);
    } else {
#ifndef ABT_CONFIG_ENABLE_VER_20_API
        ABTI_CHECK_TRUE(p_sched->used == ABTI_SCHED_NOT_USED,
                        ABT_ERR_INV_SCHED);
#else
        ABTI_UB_ASSERT(p_sched->used == ABTI_SCHED_NOT_USED);
#endif
    }

    abt_errno = xstream_create(p_global, p_sched, ABTI_XSTREAM_TYPE_SECONDARY,
                               rank, ABT_TRUE, &p_newxstream);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        if (!ABTI_sched_get_ptr(sched)) {
            ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                            ABT_FALSE);
        }
        ABTI_HANDLE_ERROR(abt_errno);
    }

    /* Return value */
    *newxstream = ABTI_xstream_get_handle(p_newxstream);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Revive a terminated execution stream.
 *
 * \c ABT_xstream_revive() revives the execution stream \c xstream that has been
 * terminated by \c ABT_xstream_join().  \c xstream starts to run immediately.
 *
 * \DOC_DESC_ATOMICITY_XSTREAM_STATE
 *
 * \c xstream may not be an execution stream that has been freed by
 * \c ABT_xstream_free().  An execution stream that is blocked on by a caller of
 * \c ABT_xstream_free() may not be revived.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_NOT_TERMINATED{\c xstream}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_XSTREAM_BLOCKED{\c xstream, \c ABT_xstream_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c xstream}
 *
 * @param[in] xstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_revive(ABT_xstream xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    /* Revives the main scheduler thread. */
    ABTI_sched *p_main_sched = p_xstream->p_main_sched;
    ABTI_ythread *p_main_sched_ythread = p_main_sched->p_ythread;
    /* TODO: should we check the thread state instead of the xstream state? */
    ABTI_CHECK_TRUE(ABTD_atomic_relaxed_load_int(
                        &p_main_sched_ythread->thread.state) ==
                        ABT_THREAD_STATE_TERMINATED,
                    ABT_ERR_INV_XSTREAM);

    ABTD_atomic_relaxed_store_uint32(&p_main_sched->request, 0);
    ABTI_event_thread_join(p_local, &p_main_sched_ythread->thread,
                           ABTI_local_get_xstream_or_null(p_local)
                               ? ABTI_local_get_xstream(p_local)->p_thread
                               : NULL);

    int abt_errno =
        ABTI_thread_revive(p_global, p_local, p_xstream->p_root_pool,
                           p_main_sched_ythread->thread.f_thread,
                           p_main_sched_ythread->thread.p_arg,
                           &p_main_sched_ythread->thread);
    /* ABTI_thread_revive() never fails since it does not update an associated
     * pool.*/
    assert(abt_errno == ABT_SUCCESS);

    ABTD_atomic_relaxed_store_int(&p_xstream->state, ABT_XSTREAM_STATE_RUNNING);
    ABTD_xstream_context_revive(&p_xstream->ctx);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Free an execution stream.
 *
 * \c ABT_xstream_free() deallocates the resource used for the execution stream
 * \c xstream and sets \c xstream to \c ABT_XSTREAM_NULL.  If \c xstream is
 * still running, this routine will be blocked on \c xstream until \c xstream
 * terminates.
 *
 * \DOC_DESC_ATOMICITY_XSTREAM_STATE
 *
 * @note
 * This routine cannot free an execution stream that is running the caller.\n
 * This routine cannot free the primary execution stream.\n
 * Only one caller can be blocked on the same \c xstream by
 * \c ABT_xstream_join() and \c ABT_xstream_free().
 *
 * @changev11
 * \DOC_DESC_V10_ERROR_CODE_CHANGE{\c ABT_SUCCESS, \c ABT_ERR_INV_XSTREAM,
 *                                 \c xstream is \c ABT_XSTREAM_NULL}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_PTR{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_PRIMARY{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_RUNNING_CALLER{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c xstream}
 * \DOC_UNDEFINED_XSTREAM_BLOCKED{\c xstream, \c ABT_xstream_join() or
 *                                            \c ABT_xstream_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c xstream}
 *
 * @param[in,out] xstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_free(ABT_xstream *xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(xstream);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_local *p_local = ABTI_local_get_local();
    ABT_xstream h_xstream = *xstream;

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(h_xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    /* We first need to check whether p_local_xstream is NULL because this
     * routine might be called by external threads. */
    ABTI_CHECK_TRUE_MSG(p_xstream != ABTI_local_get_xstream_or_null(p_local),
                        ABT_ERR_INV_XSTREAM,
                        "The current xstream cannot be freed.");

    ABTI_CHECK_TRUE_MSG(p_xstream->type != ABTI_XSTREAM_TYPE_PRIMARY,
                        ABT_ERR_INV_XSTREAM,
                        "The primary xstream cannot be freed explicitly.");

    /* Wait until xstream terminates */
    int abt_errno = xstream_join(&p_local, p_xstream);
    ABTI_CHECK_ERROR(abt_errno);

    /* Free the xstream object */
    ABTI_xstream_free(p_global, p_local, p_xstream, ABT_FALSE);

    /* Return value */
    *xstream = ABT_XSTREAM_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Wait for an execution stream to terminate.
 *
 * The caller of \c ABT_thread_join() waits for the execution stream \c xstream
 * until \c xstream terminates.
 *
 * \DOC_DESC_ATOMICITY_XSTREAM_STATE
 *
 * @note
 * This routine cannot free an execution stream that is running the caller.\n
 * This routine cannot free the primary execution stream.\n
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_PRIMARY{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_RUNNING_CALLER{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_XSTREAM_BLOCKED{\c xstream, \c ABT_xstream_join() or
 *                                            \c ABT_xstream_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c xstream}
 *
 * @param[in] xstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_join(ABT_xstream xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    int abt_errno = xstream_join(&p_local, p_xstream);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Terminate an execution stream that is running the calling ULT.
 *
 * \c ABT_xstream_exit() sends a cancellation request to the execution stream
 * that is running the calling ULT and terminates the calling ULT.  This routine
 * does not return if it succeeds.  An execution stream that receives a
 * cancellation request will terminate.
 *
 * \DOC_DESC_ATOMICITY_XSTREAM_REQUEST
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{the caller}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_XSTREAM_PRIMARY{an execution stream that is running the
 *                                calling ULT}
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @return Error code
 */
int ABT_xstream_exit(void)
{
    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_ythread;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_ythread);
    /* Check if the target is the primary execution stream. */
    ABTI_CHECK_TRUE(p_local_xstream->type != ABTI_XSTREAM_TYPE_PRIMARY,
                    ABT_ERR_INV_XSTREAM);

    /* Terminate the main scheduler. */
    ABTD_atomic_fetch_or_uint32(&p_local_xstream->p_main_sched->p_ythread
                                     ->thread.request,
                                ABTI_THREAD_REQ_CANCEL);
    /* Terminate this ULT */
    ABTI_ythread_exit(p_local_xstream, p_ythread);
    ABTU_unreachable();
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Send a cancellation request to an execution stream.
 *
 * \c ABT_xstream_cancel() sends a cancellation request to the execution stream
 * \c xstream.  An execution stream that receives a cancellation request will
 * terminate.
 *
 * \DOC_DESC_ATOMICITY_XSTREAM_REQUEST
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_PRIMARY{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_XSTREAM_NOT_RUNNING{\c xstream}
 *
 * @param[in] xstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_cancel(ABT_xstream xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);
    ABTI_CHECK_TRUE(p_xstream->type != ABTI_XSTREAM_TYPE_PRIMARY,
                    ABT_ERR_INV_XSTREAM);

    /* Terminate the main scheduler of the target xstream. */
    ABTD_atomic_fetch_or_uint32(&p_xstream->p_main_sched->p_ythread->thread
                                     .request,
                                ABTI_THREAD_REQ_CANCEL);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Get an execution stream that is running the calling work unit.
 *
 * \c ABT_xstream_self() returns the handle of the execution stream that is
 * running the calling work unit through \c xstream.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_xstream()}
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c xstream, \c ABT_XSTREAM_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c xstream}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] xstream  execution stream handle
 * @return Error code
 */
int ABT_xstream_self(ABT_xstream *xstream)
{
    ABTI_UB_ASSERT(xstream);

    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *xstream = ABT_XSTREAM_NULL;
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    /* Return value */
    *xstream = ABTI_xstream_get_handle(p_local_xstream);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Return a rank of an execution stream associated with a caller.
 *
 * \c ABT_xstream_self_rank() returns the rank of the execution stream that is
 * running the calling work unit through \c rank.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_xstream_rank()}
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c rank}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] rank  execution stream rank
 * @return Error code
 */
int ABT_xstream_self_rank(int *rank)
{
    ABTI_UB_ASSERT(rank);

    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    /* Return value */
    *rank = (int)p_local_xstream->rank;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Set a rank for an execution stream.
 *
 * \c ABT_xstream_set_rank() allocates the new rank \c rank and assigns it to
 * the execution stream \c xstream.  The original rank of \c xstream is
 * deallocated.
 *
 * \c rank must be non-negative and not used by another execution stream.
 *
 * \DOC_DESC_ATOMICITY_RANK
 *
 * The rank of the primary execution stream may not be changed.
 *
 * @note
 * If the affinity setting is enabled, this routine updates the CPU binding of
 * \c xstream based on \c rank.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_RANK{\c rank}
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_XSTREAM_PRIMARY{\c xstream}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] xstream  execution stream handle
 * @param[in] rank     execution stream rank
 * @return Error code
 */
int ABT_xstream_set_rank(ABT_xstream xstream, int rank)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);
    ABTI_CHECK_TRUE(p_xstream->type != ABTI_XSTREAM_TYPE_PRIMARY,
                    ABT_ERR_INV_XSTREAM);
    ABTI_CHECK_TRUE(rank >= 0, ABT_ERR_INV_XSTREAM_RANK);

    ABT_bool is_changed = xstream_change_rank(p_global, p_xstream, rank);
    ABTI_CHECK_TRUE(is_changed, ABT_ERR_INV_XSTREAM_RANK);

    /* Set the CPU affinity for the ES */
    if (p_global->set_affinity == ABT_TRUE) {
        ABTD_affinity_cpuset_apply_default(&p_xstream->ctx, p_xstream->rank);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Retrieve a rank of an execution stream.
 *
 * \c ABT_xstream_get_rank() returns a rank of the execution stream \c xstream
 * through \c rank.
 *
 * \DOC_DESC_ATOMICITY_RANK
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c rank}
 *
 * @param[in]  xstream  execution stream handle
 * @param[out] rank     execution stream rank
 * @return Error code
 */
int ABT_xstream_get_rank(ABT_xstream xstream, int *rank)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(rank);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    *rank = (int)p_xstream->rank;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Set the main scheduler of an execution stream.
 *
 * \c ABT_xstream_set_main_sched() sets \c sched as the main scheduler of the
 * execution stream \c xstream.  The old scheduler associated with \c xstream
 * will be freed if it is configured to be automatically freed.
 *
 * The caller must be a ULT.
 *
 * This routine works in the following two cases:
 *
 * - If \c xstream is terminated:
 *
 *   This routine updates the main scheduler of \c xstream to \c sched.
 *   \c sched will be used when \c xstream is revived.
 *
 * - If \c xstream is running:
 *
 *   The caller must be running on the main scheduler of \c xstream.  The caller
 *   will be associated with the first pool of the scheduler.  It is the user's
 *   responsibility to handle work units in pools associated with the old main
 *   scheduler.
 *
 * If \c sched is \c ABT_SCHED_NULL, the default basic scheduler with the
 * default scheduler configuration will be created.
 *
 * @note
 * \DOC_NOTE_DEFAULT_SCHED\n
 * \DOC_NOTE_DEFAULT_SCHED_CONFIG
 *
 * @changev11
 * \DOC_DESC_V10_INCOMPLETE_SCHED_SIZE_CHECK{\c xstream, \c ABT_ERR_XSTREAM}
 *
 * \DOC_DESC_V10_ACCESS_VIOLATION
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_ERROR_CODE_CHANGE{\c ABT_ERR_XSTREAM_STATE,
 *                                 \c ABT_ERR_INV_XSTREAM,
 *                                 the caller is not running on \c xstream while
 *                                 \c xstream is running}
 *
 * \DOC_DESC_V1X_CRUDE_SCHED_USED_CHECK{\c sched, \c ABT_ERR_INV_SCHED}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_RESOURCE_CONDITIONAL{\c sched is \c ABT_SCHED_NULL}
 * \DOC_V1X \DOC_ERROR_SCHED_USED_CONDITIONAL{\c sched, \c ABT_ERR_INV_SCHED,
 *                                            \c sched is not \c ABT_SCHED_NULL}
 * \DOC_V1X \DOC_ERROR_XSTREAM_STATE_RUNNING_CALLER_CONDITIONAL{\c xstream,
 *                                                              \c xstream is
 *                                                              running}
 * \DOC_V20 \DOC_ERROR_INV_XSTREAM_RUNNING_CALLER_CONDITIONAL{\c xstream,
 *                                                            \c xstream is
 *                                                            running}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_XSTREAM_SET_MAIN_SCHED
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c sched}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c xstream}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{the old main scheduler associated with
 *                                   \c xstream}
 * \DOC_V20 \DOC_UNDEFINED_SCHED_USED_CONDITIONAL{\c sched, \c sched is not
 *                                                          \c ABT_SCHED_NULL}
 *
 * @param[in] xstream  execution stream handle
 * @param[in] sched    scheduler handle
 * @return Error code
 */
int ABT_xstream_set_main_sched(ABT_xstream xstream, ABT_sched sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_self;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_self);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE(ABTD_atomic_acquire_load_int(&p_xstream->state) !=
                            ABT_XSTREAM_STATE_RUNNING ||
                        p_local_xstream == p_xstream,
                    ABT_ERR_XSTREAM_STATE);
#else
    ABTI_CHECK_TRUE(ABTD_atomic_acquire_load_int(&p_xstream->state) !=
                            ABT_XSTREAM_STATE_RUNNING ||
                        p_local_xstream == p_xstream,
                    ABT_ERR_INV_XSTREAM);
#endif

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    if (!p_sched) {
        int abt_errno =
            ABTI_sched_create_basic(ABT_SCHED_DEFAULT, 0, NULL, NULL, &p_sched);
        ABTI_CHECK_ERROR(abt_errno);
    } else {
#ifndef ABT_CONFIG_ENABLE_VER_20_API
        ABTI_CHECK_TRUE(p_sched->used == ABTI_SCHED_NOT_USED,
                        ABT_ERR_INV_SCHED);
#else
        ABTI_UB_ASSERT(p_sched->used == ABTI_SCHED_NOT_USED);
#endif
    }

    int abt_errno = xstream_update_main_sched(p_global, &p_local_xstream,
                                              p_xstream, p_sched);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        if (!ABTI_sched_get_ptr(sched)) {
            ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                            ABT_FALSE);
        }
        ABTI_HANDLE_ERROR(abt_errno);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Set the main scheduler of an execution stream to a predefined
 *          scheduler.
 *
 * \c ABT_xstream_set_main_sched() sets the predefined scheduler \c predefined
 * as the main scheduler of the execution stream \c xstream.  The functionality
 * provided by this routine when it succeeds is the same as the combination of
 * \c ABT_sched_create_basic() and \c ABT_xstream_set_main_sched().
 *
 * @code{.c}
 * int ABT_xstream_set_main_sched_basic(...) {
 *   ABT_sched sched;
 *   ABT_sched_create_basic(predef, num_pools, pools, ABT_SCHED_CONFIG_NULL,
 *                          &sched);
 *   ABT_xstream_set_main_sched(xstream, sched);
 *   return ABT_SUCCESS;
 * }
 * @endcode
 *
 * Please check \c ABT_sched_create_basic() and \c ABT_xstream_set_main_sched()
 * for details.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_ARG_NEG{\c num_pools}
 * \DOC_ERROR_INV_ARG_INV_SCHED_PREDEF{\c predef}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c pools, \c num_pools is positive}
 * \DOC_UNDEFINED_XSTREAM_SET_MAIN_SCHED
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c xstream}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{the old main scheduler associated with
 *                                   \c xstream}
 *
 * @param[in] xstream     execution stream handle
 * @param[in] predef      predefined scheduler
 * @param[in] num_pools   number of pools associated with the scheduler
 * @param[in] pools       pools associated with the scheduler
 * @return Error code
 */
int ABT_xstream_set_main_sched_basic(ABT_xstream xstream,
                                     ABT_sched_predef predef, int num_pools,
                                     ABT_pool *pools)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pools || num_pools <= 0);

    int abt_errno;
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, NULL);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    ABTI_sched *p_sched;
    abt_errno =
        ABTI_sched_create_basic(predef, num_pools, pools, NULL, &p_sched);
    ABTI_CHECK_ERROR(abt_errno);

    abt_errno = xstream_update_main_sched(p_global, &p_local_xstream, p_xstream,
                                          p_sched);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        int i;
        for (i = 0; i < num_pools; i++) {
            if (pools[i] != ABT_POOL_NULL) {
                /* Avoid freeing user-given pools. */
                ABTI_pool_release(ABTI_pool_get_ptr(p_sched->pools[i]));
                p_sched->pools[i] = ABT_POOL_NULL;
            }
        }
        ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                        ABT_FALSE);
        ABTI_HANDLE_ERROR(abt_errno);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Retrieve the main scheduler of an execution stream.
 *
 * \c ABT_xstream_get_main_sched() returns the main scheduler of the execution
 * stream \c xstream through \c sched.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c sched}
 *
 * @param[in]  xstream  execution stream handle
 * @param[out] sched    scheduler handle
 * @return Error code
 */
int ABT_xstream_get_main_sched(ABT_xstream xstream, ABT_sched *sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(sched);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    *sched = ABTI_sched_get_handle(p_xstream->p_main_sched);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Get pools associated with the main scheduler of an execution stream.
 *
 * \c ABT_xstream_get_main_pools() sets the pools \c pools to at maximum
 * \c max_pools pools associated with the main scheduler of the execution stream
 * \c xstream.
 *
 * @note
 * \DOC_NOTE_NO_PADDING{\c pools, \c max_pools}
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c max_pools}
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c pools, \c max_pools is positive}
 *
 * @param[in]  xstream    execution stream handle
 * @param[in]  max_pools  maximum number of pools
 * @param[out] pools      array of handles to the pools
 * @return Error code
 */
int ABT_xstream_get_main_pools(ABT_xstream xstream, int max_pools,
                               ABT_pool *pools)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pools || max_pools <= 0);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    ABTI_sched *p_sched = p_xstream->p_main_sched;
    max_pools = ABTU_min_int(p_sched->num_pools, max_pools);
    memcpy(pools, p_sched->pools, sizeof(ABT_pool) * max_pools);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Get a state of an execution stream.
 *
 * \c ABT_xstream_get_state() returns the state of the execution stream
 * \c xstream through \c state.
 *
 * \DOC_DESC_ATOMICITY_XSTREAM_STATE
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c state}
 *
 * @param[in]  xstream  execution stream handle
 * @param[out] state    state of \c xstream
 * @return Error code
 */
int ABT_xstream_get_state(ABT_xstream xstream, ABT_xstream_state *state)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(state);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    *state = (ABT_xstream_state)ABTD_atomic_acquire_load_int(&p_xstream->state);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Compare two execution stream handles for equality.
 *
 * \c ABT_xstream_equal() compares two execution stream handles \c xstream1 and
 * \c xstream2 for equality and returns the result through \c result.
 *
 * This function is deprecated since its behavior is the same as comparing
 * values of \c xstream1 and \c xstream2.
 * @code{.c}
 * *result = (xstream1 == xstream2) ? ABT_TRUE : ABT_FALSE;
 * @endcode
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c result}
 *
 * @param[in]  xstream1  execution stream handle 1
 * @param[in]  xstream2  execution stream handle 2
 * @param[out] result    result (\c ABT_TRUE: same, \c ABT_FALSE: not same)
 * @return Error code
 */
int ABT_xstream_equal(ABT_xstream xstream1, ABT_xstream xstream2,
                      ABT_bool *result)
{
    ABTI_UB_ASSERT(result);

    ABTI_xstream *p_xstream1 = ABTI_xstream_get_ptr(xstream1);
    ABTI_xstream *p_xstream2 = ABTI_xstream_get_ptr(xstream2);
    *result = (p_xstream1 == p_xstream2) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Get the number of current existing execution streams.
 *
 * \c ABT_xstream_get_num() returns the number of execution streams that exist
 * in the Argobots execution environment through \c num_xstreams.  This routine
 * counts both running and terminated execution streams.
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c num_xstreams, zero}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c num_xstreams}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] num_xstreams  the number of execution streams
 * @return Error code
 */
int ABT_xstream_get_num(int *num_xstreams)
{
    ABTI_UB_ASSERT(num_xstreams);
#ifdef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_UB_ASSERT(ABTI_initialized());
#endif

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    *num_xstreams = p_global->num_xstreams;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Check if the target execution stream is primary.
 *
 * \c ABT_xstream_is_primary() checks if the execution stream \c xstream is the
 * primary execution stream and returns the result through \c is_primary.  If
 * \c xstream is the primary execution stream, \c is_primary is set to
 * \c ABT_TRUE.  Otherwise, \c is_primary is set to \c ABT_FALSE.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_primary}
 *
 * @param[in]  xstream     execution stream handle
 * @param[out] is_primary  result (\c ABT_TRUE: primary, \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_xstream_is_primary(ABT_xstream xstream, ABT_bool *is_primary)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_primary);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    /* Return value */
    *is_primary =
        (p_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Execute a work unit.
 *
 * \c ABT_xstream_run_unit() associates the work unit \c unit with the pool
 * \c pool and runs \c unit as a child ULT on the calling ULT, which becomes a
 * parent ULT.  The calling ULT will be resumed when \c unit finishes or yields.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_UNIT_HANDLE{\c unit}
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WORK_UNIT_NOT_READY{\c unit}
 *
 * @param[in] unit  unit handle
 * @param[in] pool  pool handle
 * @return Error code
 */
int ABT_xstream_run_unit(ABT_unit unit, ABT_pool pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(unit != ABT_UNIT_NULL, ABT_ERR_INV_UNIT);
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, NULL);

    ABTI_thread *p_thread;
    int abt_errno =
        ABTI_unit_set_associated_pool(p_global, unit, p_pool, &p_thread);
    ABTI_CHECK_ERROR(abt_errno);
    ABTI_ythread_schedule(p_global, &p_local_xstream, p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Process events associated with a scheduler
 *
 * \c ABT_xstream_check_events() processes events associated with the scheduler
 * \c sched.  The calling work unit must be associated with \c sched.
 *
 * This routine must be called by a scheduler periodically.  For example, a
 * user-defined scheduler should call this routine every N iterations of its
 * scheduling loop.
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_SCHED{\c sched} \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_ERROR_INV_THREAD_NOT_CALLER{a work unit associated with \c sched}
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c sched}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[in] sched  scheduler handle
 * @return Error code
 */
int ABT_xstream_check_events(ABT_sched sched)
{
    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
    ABTI_CHECK_TRUE(p_local_xstream->p_thread == &p_sched->p_ythread->thread,
                    ABT_ERR_INV_THREAD);

    ABTI_xstream_check_events(p_local_xstream, p_sched);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Bind an execution stream to a target CPU.
 *
 * \c ABT_xstream_set_cpubind() binds the execution stream \c xstream to a CPU
 * that is corresponding to the CPU ID \c cpuid.
 *
 * @note
 * \DOC_NOTE_CPUID
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_SYS_CPUBIND
 * \DOC_ERROR_CPUID{\c cpuid}
 * \DOC_ERROR_FEATURE_NA{the affinity feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c xstream}
 *
 * @param[in] xstream  execution stream handle
 * @param[in] cpuid    CPU ID
 * @return Error code
 */
int ABT_xstream_set_cpubind(ABT_xstream xstream, int cpuid)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    ABTD_affinity_cpuset cpuset;
    cpuset.num_cpuids = 1;
    cpuset.cpuids = &cpuid;
    int abt_errno = ABTD_affinity_cpuset_apply(&p_xstream->ctx, &cpuset);
    /* Do not free cpuset since cpuids points to a user pointer. */
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Get CPU ID of a CPU to which an execution stream is bound.
 *
 * \c ABT_xstream_get_cpubind() returns the CPU ID of a CPU to which the
 * execution stream \c xstream is bound through \c cpuid.  If \c xstream is
 * bound to more than one CPU, \c cpuid is set to one of the CPU IDs.
 *
 * @note
 * \DOC_NOTE_CPUID
 *
 * @changev20
 * \DOC_DESC_V1X_ERROR_CODE_CHANGE{\c ABT_ERR_FEATURE_NA, \c ABT_ERR_CPUID,
 *                                 \c xstream is not bound to any CPU}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_SYS_CPUBIND
 * \DOC_ERROR_FEATURE_NA{the affinity feature}
 * \DOC_V1X \DOC_ERROR_FEATURE_NA_NO_BINDING{\c xstream}
 * \DOC_V20 \DOC_ERROR_CPUID_NO_BINDING{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c cpuid}
 *
 * @param[in]  xstream  execution stream handle
 * @param[out] cpuid    CPU ID
 * @return Error code
 */
int ABT_xstream_get_cpubind(ABT_xstream xstream, int *cpuid)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(cpuid);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    int num_cpuid;
    int cpuids[1];
    int abt_errno =
        ABTD_affinity_cpuset_read(&p_xstream->ctx, 1, cpuids, &num_cpuid);
    ABTI_CHECK_ERROR(abt_errno);
    ABTI_CHECK_TRUE(num_cpuid > 0, ABT_ERR_CPUID);

    *cpuid = cpuids[0];
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Bind an execution stream to target CPUs.
 *
 * \c ABT_xstream_set_affinity() updates the CPU binding of the execution stream
 * \c xstream.  If \c num_cpuids is positive, this routine binds \c xstream to
 * CPUs that are corresponding to \c cpuids that has \c num_cpuids CPU IDs.  If
 * \c num_cpuids is zero, this routine resets the CPU binding of \c xstream.
 *
 * @note
 * \DOC_NOTE_CPUID
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_ARG_NEG{\c num_cpuids}
 * \DOC_ERROR_SYS_CPUBIND
 * \DOC_ERROR_CPUID{any of \c cpuids}
 * \DOC_ERROR_FEATURE_NA{the affinity feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c cpuids, \c num_cpuids is positive}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c xstream}
 *
 * @param[in] xstream     execution stream handle
 * @param[in] num_cpuids  the number of \c cpuids entries
 * @param[in] cpuids      array of CPU IDs
 * @return Error code
 */
int ABT_xstream_set_affinity(ABT_xstream xstream, int num_cpuids, int *cpuids)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(cpuids || num_cpuids <= 0);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);
    ABTI_CHECK_TRUE(num_cpuids >= 0, ABT_ERR_INV_ARG);

    ABTD_affinity_cpuset affinity;
    affinity.num_cpuids = num_cpuids;
    affinity.cpuids = cpuids;
    int abt_errno = ABTD_affinity_cpuset_apply(&p_xstream->ctx, &affinity);
    /* Do not free affinity since cpuids may not be freed. */
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ES
 * @brief   Get CPU IDs of CPUs to which an execution stream is bound.
 *
 * \c ABT_xstream_get_affinity() returns the CPU IDs of CPUs to which the
 * execution stream \c xstream is bound through \c cpuids and \c num_cpuids.
 *
 * If \c max_cpuids is positive, this routine writes at most \c max_cpuids CPU
 * IDs to \c cpuids.  If \c xstream is not bound to any CPU, \c cpuids is not
 * updated.
 *
 * @note
 * \DOC_NOTE_NO_PADDING{\c cpuids, \c max_cpuids}
 *
 * If \c num_cpuids is not \c NULL, this routine returns the number of CPUs to
 * which \c xstream is bound through \c num_cpuids.  If \c xstream is not bound
 * to any CPU, \c num_cpus is set to zero.
 *
 * @note
 * \DOC_NOTE_CPUID
 *
 * @changev20
 * \DOC_DESC_V1X_ERROR_CODE_CHANGE{\c ABT_ERR_FEATURE_NA, \c ABT_ERR_CPUID,
 *                                 \c xstream is not bound to any CPU}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_INV_ARG_NEG{\c max_cpuids}
 * \DOC_ERROR_SYS_CPUBIND
 * \DOC_ERROR_FEATURE_NA{the affinity feature}
 * \DOC_V1X \DOC_ERROR_FEATURE_NA_NO_BINDING{\c xstream}
 * \DOC_V20 \DOC_ERROR_CPUID_NO_BINDING{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c cpuids, \c max_cpuids is positive}
 *
 * @param[in]  xstream     execution stream handle
 * @param[in]  max_cpuids  the number of \c cpuids entries
 * @param[out] cpuids      array of CPU IDs
 * @param[out] num_cpuids  the number of total CPU IDs
 * @return Error code
 */
int ABT_xstream_get_affinity(ABT_xstream xstream, int max_cpuids, int *cpuids,
                             int *num_cpuids)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(cpuids || max_cpuids <= 0);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);
    ABTI_CHECK_TRUE(max_cpuids >= 0, ABT_ERR_INV_ARG);

    int abt_errno = ABTD_affinity_cpuset_read(&p_xstream->ctx, max_cpuids,
                                              cpuids, num_cpuids);
    ABTI_CHECK_ERROR(abt_errno);
    return abt_errno;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

ABTU_ret_err int ABTI_xstream_create_primary(ABTI_global *p_global,
                                             ABTI_xstream **pp_xstream)
{
    int abt_errno;
    ABTI_xstream *p_newxstream;
    ABTI_sched *p_sched;

    /* For the primary ES, a default scheduler is created. */
    abt_errno =
        ABTI_sched_create_basic(ABT_SCHED_DEFAULT, 0, NULL, NULL, &p_sched);
    ABTI_CHECK_ERROR(abt_errno);

    abt_errno = xstream_create(p_global, p_sched, ABTI_XSTREAM_TYPE_PRIMARY, -1,
                               ABT_FALSE, &p_newxstream);
    if (abt_errno != ABT_SUCCESS) {
        ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                        ABT_TRUE);
        ABTI_HANDLE_ERROR(abt_errno);
    }

    *pp_xstream = p_newxstream;
    return ABT_SUCCESS;
}

/* This routine starts the primary ES. It should be called in ABT_init. */
void ABTI_xstream_start_primary(ABTI_global *p_global,
                                ABTI_xstream **pp_local_xstream,
                                ABTI_xstream *p_xstream,
                                ABTI_ythread *p_ythread)
{
    /* p_ythread must be the main thread. */
    ABTI_ASSERT(p_ythread->thread.type & ABTI_THREAD_TYPE_PRIMARY);
    /* The ES's state must be running here. */
    ABTI_ASSERT(ABTD_atomic_relaxed_load_int(&p_xstream->state) ==
                ABT_XSTREAM_STATE_RUNNING);

    ABTD_xstream_context_set_self(&p_xstream->ctx);

    /* Set the CPU affinity for the ES */
    if (p_global->set_affinity == ABT_TRUE) {
        ABTD_affinity_cpuset_apply_default(&p_xstream->ctx, p_xstream->rank);
    }

    /* Context switch to the root thread. */
    p_xstream->p_root_ythread->thread.p_last_xstream = p_xstream;
    ABTI_ythread_context_switch(*pp_local_xstream, p_ythread,
                                p_xstream->p_root_ythread);
    /* Come back to the primary thread.  Now this thread is executed on top of
     * the main scheduler, which is running on the root thread. */
    (*pp_local_xstream)->p_thread = &p_ythread->thread;
}

void ABTI_xstream_check_events(ABTI_xstream *p_xstream, ABTI_sched *p_sched)
{
    ABTI_info_check_print_all_thread_stacks();

    uint32_t request = ABTD_atomic_acquire_load_uint32(
        &p_xstream->p_main_sched->p_ythread->thread.request);
    if (request & ABTI_THREAD_REQ_JOIN) {
        ABTI_sched_finish(p_sched);
    }

    if (request & ABTI_THREAD_REQ_CANCEL) {
        ABTI_sched_exit(p_sched);
    }
}

void ABTI_xstream_free(ABTI_global *p_global, ABTI_local *p_local,
                       ABTI_xstream *p_xstream, ABT_bool force_free)
{
    /* Clean up memory pool. */
    ABTI_mem_finalize_local(p_xstream);
    /* Return rank for reuse. rank must be returned prior to other free
     * functions so that other xstreams cannot refer to this xstream. */
    xstream_return_rank(p_global, p_xstream);

    /* Free the scheduler */
    ABTI_sched *p_cursched = p_xstream->p_main_sched;
    if (p_cursched != NULL) {
        /* Join a scheduler thread. */
        ABTI_event_thread_join(p_local, &p_cursched->p_ythread->thread,
                               ABTI_local_get_xstream_or_null(p_local)
                                   ? ABTI_local_get_xstream(p_local)->p_thread
                                   : NULL);
        ABTI_sched_discard_and_free(p_global, p_local, p_cursched, force_free);
        /* The main scheduler thread is also freed. */
    }

    /* Free the root thread and pool. */
    ABTI_ythread_free_root(p_global, p_local, p_xstream->p_root_ythread);
    ABTI_pool_free(p_xstream->p_root_pool);

    /* Free the context if a given xstream is secondary. */
    if (p_xstream->type == ABTI_XSTREAM_TYPE_SECONDARY) {
        ABTD_xstream_context_free(&p_xstream->ctx);
    }

    ABTU_free(p_xstream);
}

void ABTI_xstream_print(ABTI_xstream *p_xstream, FILE *p_os, int indent,
                        ABT_bool print_sub)
{
    if (p_xstream == NULL) {
        fprintf(p_os, "%*s== NULL ES ==\n", indent, "");
    } else {
        const char *type, *state;
        switch (p_xstream->type) {
            case ABTI_XSTREAM_TYPE_PRIMARY:
                type = "PRIMARY";
                break;
            case ABTI_XSTREAM_TYPE_SECONDARY:
                type = "SECONDARY";
                break;
            default:
                type = "UNKNOWN";
                break;
        }
        switch (ABTD_atomic_acquire_load_int(&p_xstream->state)) {
            case ABT_XSTREAM_STATE_RUNNING:
                state = "RUNNING";
                break;
            case ABT_XSTREAM_STATE_TERMINATED:
                state = "TERMINATED";
                break;
            default:
                state = "UNKNOWN";
                break;
        }

        fprintf(p_os,
                "%*s== ES (%p) ==\n"
                "%*srank         : %d\n"
                "%*stype         : %s\n"
                "%*sstate        : %s\n"
                "%*sroot_ythread : %p\n"
                "%*sroot_pool    : %p\n"
                "%*sthread       : %p\n"
                "%*smain_sched   : %p\n",
                indent, "", (void *)p_xstream, indent, "", p_xstream->rank,
                indent, "", type, indent, "", state, indent, "",
                (void *)p_xstream->p_root_ythread, indent, "",
                (void *)p_xstream->p_root_pool, indent, "",
                (void *)p_xstream->p_thread, indent, "",
                (void *)p_xstream->p_main_sched);

        if (print_sub == ABT_TRUE) {
            ABTI_sched_print(p_xstream->p_main_sched, p_os,
                             indent + ABTI_INDENT, ABT_TRUE);
        }
        fprintf(p_os, "%*sctx          :\n", indent, "");
        ABTD_xstream_context_print(&p_xstream->ctx, p_os, indent + ABTI_INDENT);
    }
    fflush(p_os);
}

static void *xstream_launch_root_ythread(void *p_xstream)
{
    ABTI_xstream *p_local_xstream = (ABTI_xstream *)p_xstream;

    /* Initialization of the local variables */
    ABTI_local_set_xstream(p_local_xstream);

    /* Set the root thread as the current thread */
    ABTI_ythread *p_root_ythread = p_local_xstream->p_root_ythread;
    p_local_xstream->p_thread = &p_local_xstream->p_root_ythread->thread;
    p_root_ythread->thread.p_last_xstream = p_local_xstream;

    /* Run the root thread. */
    p_root_ythread->thread.f_thread(p_root_ythread->thread.p_arg);
    ABTI_thread_terminate(ABTI_global_get_global(), p_local_xstream,
                          &p_root_ythread->thread);

    /* Reset the current ES and its local info. */
    ABTI_local_set_xstream(NULL);
    return NULL;
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

ABTU_ret_err static int xstream_create(ABTI_global *p_global,
                                       ABTI_sched *p_sched,
                                       ABTI_xstream_type xstream_type, int rank,
                                       ABT_bool start,
                                       ABTI_xstream **pp_xstream)
{
    int abt_errno, init_stage = 0;
    ABTI_xstream *p_newxstream;

    abt_errno = ABTU_malloc(sizeof(ABTI_xstream), (void **)&p_newxstream);
    ABTI_CHECK_ERROR(abt_errno);

    p_newxstream->p_prev = NULL;
    p_newxstream->p_next = NULL;

    if (xstream_set_new_rank(p_global, p_newxstream, rank) == ABT_FALSE) {
        abt_errno = ABT_ERR_INV_XSTREAM_RANK;
        goto FAILED;
    }
    init_stage = 1;

    p_newxstream->type = xstream_type;
    ABTD_atomic_relaxed_store_int(&p_newxstream->state,
                                  ABT_XSTREAM_STATE_RUNNING);
    p_newxstream->p_main_sched = NULL;
    p_newxstream->p_thread = NULL;
    abt_errno = ABTI_mem_init_local(p_global, p_newxstream);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 2;

    /* Set the main scheduler */
    xstream_init_main_sched(p_newxstream, p_sched);

    /* Create the root thread. */
    abt_errno =
        ABTI_ythread_create_root(p_global, ABTI_xstream_get_local(p_newxstream),
                                 p_newxstream, &p_newxstream->p_root_ythread);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 3;

    /* Create the root pool. */
    abt_errno = ABTI_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC,
                                       ABT_FALSE, &p_newxstream->p_root_pool);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 4;

    /* Create the main scheduler thread. */
    abt_errno =
        ABTI_ythread_create_main_sched(p_global,
                                       ABTI_xstream_get_local(p_newxstream),
                                       p_newxstream,
                                       p_newxstream->p_main_sched);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 5;

    if (start) {
        /* The ES's state must be RUNNING */
        ABTI_ASSERT(ABTD_atomic_relaxed_load_int(&p_newxstream->state) ==
                    ABT_XSTREAM_STATE_RUNNING);
        ABTI_ASSERT(p_newxstream->type != ABTI_XSTREAM_TYPE_PRIMARY);
        /* Start the main scheduler on a different ES */
        abt_errno = ABTD_xstream_context_create(xstream_launch_root_ythread,
                                                (void *)p_newxstream,
                                                &p_newxstream->ctx);
        if (abt_errno != ABT_SUCCESS)
            goto FAILED;
        init_stage = 6;

        /* Set the CPU affinity for the ES */
        if (p_global->set_affinity == ABT_TRUE) {
            ABTD_affinity_cpuset_apply_default(&p_newxstream->ctx,
                                               p_newxstream->rank);
        }
    }

    /* Return value */
    *pp_xstream = p_newxstream;
    return ABT_SUCCESS;
FAILED:
    if (init_stage >= 5) {
        ABTI_thread_free(p_global, ABTI_xstream_get_local(p_newxstream),
                         &p_newxstream->p_main_sched->p_ythread->thread);
        p_newxstream->p_main_sched->p_ythread = NULL;
    }
    if (init_stage >= 4) {
        ABTI_pool_free(p_newxstream->p_root_pool);
    }
    if (init_stage >= 3) {
        ABTI_ythread_free_root(p_global, ABTI_xstream_get_local(p_newxstream),
                               p_newxstream->p_root_ythread);
    }
    if (init_stage >= 2) {
        p_sched->used = ABTI_SCHED_NOT_USED;
        ABTI_mem_finalize_local(p_newxstream);
    }
    if (init_stage >= 1) {
        xstream_return_rank(p_global, p_newxstream);
    }
    ABTU_free(p_newxstream);
    return abt_errno;
}

ABTU_ret_err static int xstream_join(ABTI_local **pp_local,
                                     ABTI_xstream *p_xstream)
{
    /* The primary ES cannot be joined. */
    ABTI_CHECK_TRUE(p_xstream->type != ABTI_XSTREAM_TYPE_PRIMARY,
                    ABT_ERR_INV_XSTREAM);
    /* The main scheduler cannot join itself. */
    ABTI_CHECK_TRUE(!ABTI_local_get_xstream_or_null(*pp_local) ||
                        &p_xstream->p_main_sched->p_ythread->thread !=
                            ABTI_local_get_xstream(*pp_local)->p_thread,
                    ABT_ERR_INV_XSTREAM);

    /* Wait until the target ES terminates */
    ABTI_sched_finish(p_xstream->p_main_sched);
    ABTI_thread_join(pp_local, &p_xstream->p_main_sched->p_ythread->thread);

    /* Normal join request */
    ABTD_xstream_context_join(&p_xstream->ctx);

    ABTI_ASSERT(ABTD_atomic_acquire_load_int(&p_xstream->state) ==
                ABT_XSTREAM_STATE_TERMINATED);
    return ABT_SUCCESS;
}

static void xstream_init_main_sched(ABTI_xstream *p_xstream,
                                    ABTI_sched *p_sched)
{
    ABTI_ASSERT(p_xstream->p_main_sched == NULL);
    /* Set the scheduler as a main scheduler */
    p_sched->used = ABTI_SCHED_MAIN;
    /* Set the scheduler */
    p_xstream->p_main_sched = p_sched;
}

static int xstream_update_main_sched(ABTI_global *p_global,
                                     ABTI_xstream **pp_local_xstream,
                                     ABTI_xstream *p_xstream,
                                     ABTI_sched *p_sched)
{
    ABTI_sched *p_main_sched = p_xstream->p_main_sched;
    if (p_main_sched == NULL) {
        /* Set the scheduler as a main scheduler */
        p_sched->used = ABTI_SCHED_MAIN;
        /* Set the scheduler */
        p_xstream->p_main_sched = p_sched;
        return ABT_SUCCESS;
    } else if (*pp_local_xstream != p_xstream) {
        /* Changing the scheduler of another execution stream. */
        ABTI_ASSERT(p_xstream->ctx.state == ABTD_XSTREAM_CONTEXT_STATE_WAITING);
        /* Use the original scheduler's thread.  Unit creation might fail, so it
         * should be done first. */
        ABTI_pool *p_tar_pool = ABTI_pool_get_ptr(p_sched->pools[0]);
        int abt_errno =
            ABTI_thread_set_associated_pool(p_global,
                                            &p_main_sched->p_ythread->thread,
                                            p_tar_pool);
        ABTI_CHECK_ERROR(abt_errno);

        /* Set the scheduler as a main scheduler */
        p_sched->used = ABTI_SCHED_MAIN;
        p_sched->p_ythread = p_main_sched->p_ythread;
        p_main_sched->p_ythread = NULL;
        /* p_main_sched is no longer used. */
        p_xstream->p_main_sched->used = ABTI_SCHED_NOT_USED;
        if (p_xstream->p_main_sched->automatic) {
            /* Free that scheduler. */
            ABTI_sched_free(p_global, ABTI_xstream_get_local(*pp_local_xstream),
                            p_xstream->p_main_sched, ABT_FALSE);
        }
        p_xstream->p_main_sched = p_sched;
        return ABT_SUCCESS;
    } else {
        /* If the ES has a main scheduler, we have to free it */
        ABTI_thread *p_thread = (*pp_local_xstream)->p_thread;
        ABTI_ASSERT(p_thread->type & ABTI_THREAD_TYPE_YIELDABLE);
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        ABTI_pool *p_tar_pool = ABTI_pool_get_ptr(p_sched->pools[0]);

        /* If the caller ULT is associated with a pool of the current main
         * scheduler, it needs to be associated to a pool of new scheduler. */
        size_t p;
        for (p = 0; p < p_main_sched->num_pools; p++) {
            if (p_ythread->thread.p_pool ==
                ABTI_pool_get_ptr(p_main_sched->pools[p])) {
                /* Associate the work unit to the first pool of new scheduler */
                int abt_errno =
                    ABTI_thread_set_associated_pool(p_global,
                                                    &p_ythread->thread,
                                                    p_tar_pool);
                ABTI_CHECK_ERROR(abt_errno);
                break;
            }
        }
        if (p_main_sched->p_replace_sched) {
            /* We need to overwrite the scheduler.  Free the existing one. */
            ABTI_ythread *p_waiter = p_main_sched->p_replace_waiter;
            ABTI_sched_discard_and_free(p_global,
                                        ABTI_xstream_get_local(
                                            *pp_local_xstream),
                                        p_main_sched->p_replace_sched,
                                        ABT_FALSE);
            p_main_sched->p_replace_sched = NULL;
            p_main_sched->p_replace_waiter = NULL;
            /* Resume the waiter.  This waiter sees that the scheduler finished
             * immediately and was replaced by this new scheduler. */
            ABTI_ythread_resume_and_push(ABTI_xstream_get_local(
                                             *pp_local_xstream),
                                         p_waiter);
        }
        /* Set the replace scheduler */
        p_main_sched->p_replace_sched = p_sched;
        p_main_sched->p_replace_waiter = p_ythread;

        /* Switch to the current main scheduler.  The current ULT is pushed to
         * the new scheduler's pool so that when the new scheduler starts, this
         * ULT can be scheduled by the new scheduler.  The existing main
         * scheduler will be freed by ABTI_SCHED_REQ_RELEASE. */
        ABTI_ythread_suspend_replace_sched(pp_local_xstream, p_ythread,
                                           p_main_sched,
                                           ABT_SYNC_EVENT_TYPE_OTHER, NULL);
        return ABT_SUCCESS;
    }
}

static void xstream_update_max_xstreams(ABTI_global *p_global, int newrank)
{
    /* The lock must be taken. */
    if (newrank >= p_global->max_xstreams) {
        static int max_xstreams_warning_once = 0;
        if (max_xstreams_warning_once == 0) {
            /* Because some Argobots functionalities depend on the runtime value
             * ABT_MAX_NUM_XSTREAMS (or p_global->max_xstreams), changing
             * this value at run-time can cause an error.  For example, using
             * ABT_mutex created before updating max_xstreams causes an error
             * since ABTI_thread_htable's array size depends on
             * ABT_MAX_NUM_XSTREAMS.  To fix this issue, please set a larger
             * number to ABT_MAX_NUM_XSTREAMS in advance. */
            char *warning_message;
            int abt_errno =
                ABTU_malloc(sizeof(char) * 1024, (void **)&warning_message);
            if (!ABTI_IS_ERROR_CHECK_ENABLED || abt_errno == ABT_SUCCESS) {
                snprintf(warning_message, 1024,
                         "Warning: the number of execution streams exceeds "
                         "ABT_MAX_NUM_XSTREAMS (=%d). This may cause an error.",
                         p_global->max_xstreams);
                HANDLE_WARNING(warning_message);
                ABTU_free(warning_message);
                max_xstreams_warning_once = 1;
            }
        }
        /* Anyway. let's increase max_xstreams. */
        p_global->max_xstreams = newrank + 1;
    }
}

/* Add p_newxstream to the list. This does not check the rank duplication. */
static void xstream_add_xstream_list(ABTI_global *p_global,
                                     ABTI_xstream *p_newxstream)
{
    int rank = p_newxstream->rank;
    ABTI_xstream *p_prev_xstream = p_global->p_xstream_head;
    ABTI_xstream *p_xstream = p_prev_xstream;
    /* Check if a certain rank is available */
    while (p_xstream) {
        ABTI_ASSERT(p_xstream->rank != rank);
        if (p_xstream->rank > rank) {
            /* Use this p_xstream. */
            break;
        }
        p_prev_xstream = p_xstream;
        p_xstream = p_xstream->p_next;
    }

    if (!p_xstream) {
        /* p_newxstream is appended to p_prev_xstream */
        if (p_prev_xstream) {
            p_prev_xstream->p_next = p_newxstream;
            p_newxstream->p_prev = p_prev_xstream;
            p_newxstream->p_next = NULL;
        } else {
            ABTI_ASSERT(p_global->p_xstream_head == NULL);
            p_newxstream->p_prev = NULL;
            p_newxstream->p_next = NULL;
            p_global->p_xstream_head = p_newxstream;
        }
    } else {
        /* p_newxstream is inserted in the middle.
         * (p_xstream->p_prev) -> p_new_xstream -> p_xstream */
        if (p_xstream->p_prev) {
            p_xstream->p_prev->p_next = p_newxstream;
            p_newxstream->p_prev = p_xstream->p_prev;
        } else {
            /* This p_xstream is the first element */
            ABTI_ASSERT(p_global->p_xstream_head == p_xstream);
            p_global->p_xstream_head = p_newxstream;
        }
        p_xstream->p_prev = p_newxstream;
        p_newxstream->p_next = p_xstream;
    }
}

/* Remove p_xstream from the list. */
static void xstream_remove_xstream_list(ABTI_global *p_global,
                                        ABTI_xstream *p_xstream)
{
    if (!p_xstream->p_prev) {
        ABTI_ASSERT(p_global->p_xstream_head == p_xstream);
        p_global->p_xstream_head = p_xstream->p_next;
    } else {
        p_xstream->p_prev->p_next = p_xstream->p_next;
    }
    if (p_xstream->p_next) {
        p_xstream->p_next->p_prev = p_xstream->p_prev;
    }
}

/* Set a new rank to ES */
static ABT_bool xstream_set_new_rank(ABTI_global *p_global,
                                     ABTI_xstream *p_newxstream, int rank)
{
    ABTD_spinlock_acquire(&p_global->xstream_list_lock);

    if (rank == -1) {
        /* Find an unused rank from 0. */
        rank = 0;
        ABTI_xstream *p_xstream = p_global->p_xstream_head;
        while (p_xstream) {
            if (p_xstream->rank == rank) {
                rank++;
            } else {
                /* Use this rank. */
                break;
            }
            p_xstream = p_xstream->p_next;
        }
    } else {
        /* Check if a certain rank is available */
        ABTI_xstream *p_xstream = p_global->p_xstream_head;
        while (p_xstream) {
            if (p_xstream->rank == rank) {
                ABTD_spinlock_release(&p_global->xstream_list_lock);
                return ABT_FALSE;
            } else if (p_xstream->rank > rank) {
                break;
            }
            p_xstream = p_xstream->p_next;
        }
    }
    /* Set the rank */
    p_newxstream->rank = rank;
    xstream_add_xstream_list(p_global, p_newxstream);
    xstream_update_max_xstreams(p_global, rank);
    p_global->num_xstreams++;

    ABTD_spinlock_release(&p_global->xstream_list_lock);
    return ABT_TRUE;
}

/* Change the rank of ES */
static ABT_bool xstream_change_rank(ABTI_global *p_global,
                                    ABTI_xstream *p_xstream, int rank)
{
    if (p_xstream->rank == rank) {
        /* No need to change the rank. */
        return ABT_TRUE;
    }

    ABTD_spinlock_acquire(&p_global->xstream_list_lock);

    ABTI_xstream *p_next = p_global->p_xstream_head;
    /* Check if a certain rank is available. */
    while (p_next) {
        if (p_next->rank == rank) {
            ABTD_spinlock_release(&p_global->xstream_list_lock);
            return ABT_FALSE;
        } else if (p_next->rank > rank) {
            break;
        }
        p_next = p_next->p_next;
    }
    /* Let's remove p_xstream from the list first. */
    xstream_remove_xstream_list(p_global, p_xstream);
    /* Then, let's add this p_xstream. */
    p_xstream->rank = rank;
    xstream_add_xstream_list(p_global, p_xstream);
    xstream_update_max_xstreams(p_global, rank);

    ABTD_spinlock_release(&p_global->xstream_list_lock);
    return ABT_TRUE;
}

static void xstream_return_rank(ABTI_global *p_global, ABTI_xstream *p_xstream)
{
    /* Remove this xstream from the global ES list */
    ABTD_spinlock_acquire(&p_global->xstream_list_lock);

    xstream_remove_xstream_list(p_global, p_xstream);
    p_global->num_xstreams--;

    ABTD_spinlock_release(&p_global->xstream_list_lock);
}
