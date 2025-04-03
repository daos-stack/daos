/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTU_ret_err static int sched_create(ABT_sched_def *def, int num_pools,
                                     ABT_pool *pools,
                                     ABTI_sched_config *p_config,
                                     ABT_bool def_automatic,
                                     ABTI_sched **pp_newsched);
static inline ABTI_sched_kind sched_get_kind(ABT_sched_def *def);
#ifdef ABT_CONFIG_USE_DEBUG_LOG
static inline uint64_t sched_get_new_id(void);
#endif

/** @defgroup SCHED Scheduler
 * This group is for Scheduler.
 */

/**
 * @ingroup SCHED
 * @brief   Create a new scheduler with a scheduler definition.
 *
 * \c ABT_sched_create() creates a new scheduler defined by the definition
 * \c def and the scheduler configuration \c config and returns its handle
 * through \c newsched.
 *
 * \c def must define all non-optional functions.  See \c #ABT_sched_def for
 * details.
 *
 * \c newsched is associated with the array of pools \c pools, which has
 * \c num_pools \c ABT_pool handles.  If the \a i th element of \c pools is
 * \c ABT_POOL_NULL, the default FIFO pool with the default pool configuration
 * is newly created and used as the \a i th pool.
 *
 * @note
 * \DOC_NOTE_DEFAULT_POOL\n
 * \DOC_NOTE_DEFAULT_POOL_CONFIG
 *
 * \c newsched can be configured via \c config.  If the user passes
 * \c ABT_CONFIG_NULL for \c config, the default configuration is used.
 * \c config is also passed as the second argument of the user-defined scheduler
 * initialization function \c init() if \c init is not \c NULL.  This routine
 * returns an error returned by \c init() if \c init() does not return
 * \c ABT_SUCCESS.
 *
 * @note
 * \DOC_NOTE_DEFAULT_SCHED_CONFIG
 *
 * This routine copies \c def, \c config, and the contents of \c pools, so the
 * user can free \c def, \c config, and \c pools after this routine returns.
 *
 * \DOC_DESC_SCHED_AUTOMATIC{\c newsched} By default \c newsched created by this
 * routine is not automatically freed.
 *
 * @note
 * \DOC_NOTE_EFFECT_ABT_FINALIZE
 *
 * @changev20
 * \DOC_DESC_V1X_NULL_PTR{\c newsched, \c ABT_ERR_SCHED}
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newsched, \c ABT_SCHED_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c num_pools}
 * \DOC_ERROR_USR_SCHED_INIT{\c init()}
 * \DOC_ERROR_RESOURCE
 * \DOC_V1X \DOC_ERROR_NULL_PTR{\c newsched, \c ABT_ERR_SCHED}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c def}
 * \DOC_UNDEFINED_NULL_PTR{any non-optional scheduler function of \c def}
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c pools, \c num_pools is positive}
 * \DOC_V20 \DOC_UNDEFINED_NULL_PTR{\c newsched}
 *
 * @param[in]  def        scheduler definition
 * @param[in]  num_pools  number of pools associated with this scheduler
 * @param[in]  pools      pools associated with this scheduler
 * @param[in]  config     scheduler configuration for scheduler creation
 * @param[out] newsched   scheduler handle
 * @return Error code
 */
int ABT_sched_create(ABT_sched_def *def, int num_pools, ABT_pool *pools,
                     ABT_sched_config config, ABT_sched *newsched)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(def);
    ABTI_UB_ASSERT(def->run);
    ABTI_UB_ASSERT(pools || num_pools == 0);

    ABTI_sched *p_sched;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *newsched = ABT_SCHED_NULL;
    ABTI_CHECK_TRUE(newsched != NULL, ABT_ERR_SCHED);
#else
    ABTI_UB_ASSERT(newsched);
#endif
    ABTI_CHECK_TRUE(num_pools >= 0, ABT_ERR_INV_ARG);

    /* The default automatic is different from ABT_sched_create_basic(). */
    const ABT_bool def_automatic = ABT_FALSE;
    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);
    int abt_errno =
        sched_create(def, num_pools, pools, p_config, def_automatic, &p_sched);
    ABTI_CHECK_ERROR(abt_errno);

    /* Return value */
    *newsched = ABTI_sched_get_handle(p_sched);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Create a new scheduler with a predefined scheduler type.
 *
 * \c ABT_sched_create_basic() creates a new scheduler with the predefined
 * scheduler type \c predef and the scheduler configuration \c config and
 * returns its handle through \c newsched.
 *
 * \c newsched is associated with the array of pools \c pools, which has
 * \c num_pools \c ABT_pool handles.  If the \a i th element of \c pools is
 * \c ABT_POOL_NULL, the default FIFO pool with the default pool configuration
 * is newly created and used as the \a i th pool.
 *
 * @note
 * \DOC_NOTE_DEFAULT_POOL\n
 * \DOC_NOTE_DEFAULT_POOL_CONFIG
 *
 * If \c pools is \c NULL, this routine creates pools automatically.  The number
 * of created pools is undefined regardless of \c num_pools, so the user should
 * get this number of associated pools via \c ABT_sched_get_num_pools().
 *
 * @note
 * The user is recommended to manually create and set pools to avoid any
 * unexpected behavior caused by automatically created pools.
 *
 * \c newsched can be configured via \c config.  If the user passes
 * \c ABT_CONFIG_NULL as \c config, the default configuration is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_SCHED_CONFIG
 *
 * \c config and the contents of \c pools are copied in this routine, so the
 * user can free \c config and \c pools after this routine returns.
 *
 * \DOC_DESC_SCHED_AUTOMATIC{\c newsched} By default \c newsched created by this
 * routine is automatically freed unless \c config disables it.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newsched, \c ABT_SCHED_NULL}
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
 * \DOC_UNDEFINED_NULL_PTR{\c newsched}
 *
 * @param[in]  predef     predefined scheduler type
 * @param[in]  num_pools  number of pools associated with the scheduler
 * @param[in]  pools      pools associated with this scheduler
 * @param[in]  config     scheduler config for scheduler creation
 * @param[out] newsched   scheduler handle
 * @return Error code
 */
int ABT_sched_create_basic(ABT_sched_predef predef, int num_pools,
                           ABT_pool *pools, ABT_sched_config config,
                           ABT_sched *newsched)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *newsched = ABT_SCHED_NULL;
    ABTI_CHECK_TRUE(newsched != NULL, ABT_ERR_SCHED);
#else
    ABTI_UB_ASSERT(newsched);
#endif
    ABTI_CHECK_TRUE(num_pools >= 0, ABT_ERR_INV_ARG);

    ABTI_sched *p_newsched;
    ABTI_sched_config *p_config = ABTI_sched_config_get_ptr(config);
    int abt_errno = ABTI_sched_create_basic(predef, num_pools, pools, p_config,
                                            &p_newsched);
    ABTI_CHECK_ERROR(abt_errno);
    *newsched = ABTI_sched_get_handle(p_newsched);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Free a scheduler.
 *
 * \c ABT_sched_free() frees the scheduler \c sched and sets \c sched to
 * \c ABT_SCHED_NULL.
 *
 * - If \c sched is created by \c ABT_sched_create():
 *
 *   If \c free is not \c NULL, This routine first calls the scheduler
 *   finalization function \c free() with the handle of \c sched as the first
 *   argument.  The error returned by \c free() is ignored.  Afterward, this
 *   routine deallocates the resource for \c sched and sets \c sched to
 *   \c ABT_SCHED_NULL.
 *
 * - If \c sched is created by \c ABT_sched_create_basic():
 *
 *   This routine deallocates the resource for \c sched and sets \c sched to
 *   \c ABT_SCHED_NULL.
 *
 * @changev20
 * \DOC_DESC_V1X_CRUDE_SCHED_USED_CHECK{\c sched, \c ABT_ERR_SCHED}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_PTR{\c sched}
 * \DOC_V1X \DOC_ERROR_SCHED_USED{\c sched, \c ABT_ERR_SCHED}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c sched}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c sched}
 * \DOC_V20 \DOC_UNDEFINED_SCHED_USED{\c sched}
 *
 * @param[in,out] sched  scheduler handle
 * @return Error code
 */
int ABT_sched_free(ABT_sched *sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(sched);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_sched *p_sched = ABTI_sched_get_ptr(*sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE(p_sched->used == ABTI_SCHED_NOT_USED, ABT_ERR_SCHED);
#else
    ABTI_UB_ASSERT(p_sched->used == ABTI_SCHED_NOT_USED);
#endif

    /* Free the scheduler */
    ABTI_sched_free(p_global, p_local, p_sched, ABT_FALSE);

    /* Return value */
    *sched = ABT_SCHED_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Obtain the number of pools associated with a scheduler.
 *
 * \c ABT_sched_get_num_pools() returns the number of pools associated with the
 * scheduler \c sched through \c num_pools.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c num_pools}
 *
 * @param[in]  sched      scheduler handle
 * @param[out] num_pools  number of pools associated with \c sched
 * @return Error code
 */
int ABT_sched_get_num_pools(ABT_sched sched, int *num_pools)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(num_pools);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);

    *num_pools = p_sched->num_pools;
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Retrieve pools associated with a scheduler.
 *
 * \c ABT_sched_get_pools() sets the array of pools \c pools to pools associated
 * with the scheduler \c sched.  The index of the copied pools starts from
 * \c idx and at most \c max_pools pool handles are copied with respect to the
 * number of pools associated with \c sched.
 *
 * @note
 * \DOC_NOTE_NO_PADDING{\c pools, \c max_pools}
 *
 * @changev20
 * \DOC_DESC_V1X_SCHED_GET_POOLS_IDX{\c idx, \c max_pools, \c sched, \c pools}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_NEG{\c max_pools}
 * \DOC_ERROR_INV_ARG_NEG{\c idx}
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_V1X \DOC_ERROR_SCHED_GET_POOLS_IDX{\c idx, \c max_pools, \c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c pools, \c max_pools is not zero}
 *
 * @param[in]  sched      scheduler handle
 * @param[in]  max_pools  maximum number of pools to obtain
 * @param[in]  idx        index of the first pool to obtain
 * @param[out] pools      array of pool handles
 * @return Error code
 */
int ABT_sched_get_pools(ABT_sched sched, int max_pools, int idx,
                        ABT_pool *pools)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pools || max_pools > 0);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
    ABTI_CHECK_TRUE(max_pools >= 0, ABT_ERR_INV_ARG);
    ABTI_CHECK_TRUE(idx >= 0, ABT_ERR_INV_ARG);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE((size_t)(idx + max_pools) <= p_sched->num_pools,
                    ABT_ERR_SCHED);
#endif

    size_t p;
    for (p = idx; p < (size_t)idx + max_pools; p++) {
        if (p >= p_sched->num_pools) {
            /* Out of range. */
            break;
        }
        pools[p - idx] = p_sched->pools[p];
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Request a scheduler to finish after its pools get empty.
 *
 * \c ABT_sched_finish() requests the scheduler \c sched to finish.  The
 * scheduler will terminate after all of its pools get empty.  This routine does
 * not wait until \c sched terminates.
 *
 * The request of \c ABT_sched_exit() is prioritized over the request of
 * \c ABT_sched_finish().  Calling \c ABT_sched_finish() does not overwrite the
 * previous request made by \c ABT_sched_exit().
 *
 * \DOC_DESC_ATOMICITY_SCHED_REQUEST
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] sched  scheduler handle
 * @return Error code
 */
int ABT_sched_finish(ABT_sched sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);

    ABTI_sched_finish(p_sched);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Request a scheduler to finish.
 *
 * \c ABT_sched_exit() requests the scheduler \c sched to finish even if its
 * pools are not empty.  This routine does not wait until \c sched terminates.
 *
 * The request of \c ABT_sched_exit() is prioritized over the request of
 * \c ABT_sched_finish().
 *
 * \DOC_DESC_ATOMICITY_SCHED_REQUEST
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] sched  scheduler handle
 * @return Error code
 */
int ABT_sched_exit(ABT_sched sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);

    ABTI_sched_exit(p_sched);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Check if a scheduler needs to stop.
 *
 * \c ABT_sched_has_to_stop() checks if the scheduler \c sched needs to stop
 * with respect to the finish request and returns its value through \c stop.  If
 * \c sched needs to stop, \c stop is set to \c ABT_TRUE.  Otherwise, \c stop is
 * set to \c ABT_FALSE.  If \c sched is not running, \c stop is set to an
 * undefined value.
 *
 * If \c sched is created by \c ABT_sched_create(), it is the user's
 * responsibility to take proper measures to stop \c sched when \c stop is set
 * to \c ABT_TRUE.
 *
 * @changev20
 * \DOC_DESC_V1X_NOEXT{\c ABT_ERR_INV_XSTREAM}
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c stop, \c ABT_FALSE}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_CTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_V1X \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c stop}
 *
 * @param[in]  sched  scheduler handle
 * @param[out] stop   indicate if the scheduler has to stop
 * @return Error code
 */
int ABT_sched_has_to_stop(ABT_sched sched, ABT_bool *stop)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(stop);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *stop = ABT_FALSE;
#endif
    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE(ABTI_local_get_local(), ABT_ERR_INV_XSTREAM);
#endif

    *stop = ABTI_sched_has_to_stop(p_sched);
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Associate a user value with a scheduler.
 *
 * \c ABT_sched_set_data() associates the user value \c data with the scheduler
 * \c sched.  The old user value associated with \c sched is overwritten.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c sched}
 *
 * @param[in] sched  scheduler handle
 * @param[in] data   specific data of the scheduler
 * @return Error code
 */
int ABT_sched_set_data(ABT_sched sched, void *data)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);

    p_sched->data = data;
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Retrieve a user value associated with a scheduler.
 *
 * \c ABT_pool_get_data() returns a user value associated with the scheduler
 * \c sched through \c data.  The user value of the newly created scheduler is
 * \c NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c data}
 *
 * @param[in]  sched  scheduler handle
 * @param[out] data   specific data of the scheduler
 * @return Error code
 */
int ABT_sched_get_data(ABT_sched sched, void **data)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(data);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);

    *data = p_sched->data;
    return ABT_SUCCESS;
}
/**
 * @ingroup SCHED
 * @brief   Obtain the sum of sizes of pools associated with a scheduler.
 *
 * \c ABT_sched_get_size() returns the sum of the sizes of pools associated with
 * the scheduler \c sched through \c size.
 *
 * @note
 * \DOC_NOTE_POOL_SIZE
 *
 * @note
 * This routine does not check the size of all the pools atomically, so the
 * returned value might not be the sum of the sizes of pools associated with the
 * scheduler at a specific point.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c size, zero}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{one of the associated pools,
 *                                     \c p_get_size()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c size}
 *
 * @param[in]  sched  scheduler handle
 * @param[out] size   sum of sizes of pools associated with \c sched
 * @return Error code
 */
int ABT_sched_get_size(ABT_sched sched, size_t *size)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(size);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *size = 0;
#endif

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
    /* Check availability of p_get_size() */
    size_t p;
    for (p = 0; p < p_sched->num_pools; p++) {
        ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->pools[p]);
        ABTI_CHECK_TRUE(p_pool->optional_def.p_get_size, ABT_ERR_POOL);
    }

    /* Sum up all the sizes */
    size_t pool_size = 0;
    for (p = 0; p < p_sched->num_pools; p++) {
        ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->pools[p]);
        pool_size += ABTI_pool_get_size(p_pool);
    }
    *size = pool_size;
    return ABT_SUCCESS;
}

/**
 * @ingroup SCHED
 * @brief   Obtain the sum of the total sizes of pools associated with a
 *          scheduler.
 *
 * \c ABT_sched_get_total_size() returns the sum of the total sizes of pools
 * associated with the scheduler \c sched through \c size.
 *
 * @note
 * \DOC_NOTE_POOL_TOTAL_SIZE
 *
 * @note
 * This routine does not check the size of all the pools atomically, so the
 * returned value might not be the sum of the sizes of pools associated with the
 * scheduler at a specific point.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c size, zero}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{one of the associated pools,
 *                                     \c p_get_size()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c size}
 *
 * @param[in]  sched  scheduler handle
 * @param[out] size   sum of the total sizes of pools associated with \c sched
 * @return Error code
 */
int ABT_sched_get_total_size(ABT_sched sched, size_t *size)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(size);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *size = 0;
#endif

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
    /* Check availability of p_get_size() */
    size_t p;
    for (p = 0; p < p_sched->num_pools; p++) {
        ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->pools[p]);
        ABTI_CHECK_TRUE(p_pool->optional_def.p_get_size, ABT_ERR_POOL);
    }

    /* Sum up all the sizes */
    size_t pool_size = 0;
    for (p = 0; p < p_sched->num_pools; p++) {
        ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->pools[p]);
        pool_size += ABTI_pool_get_total_size(p_pool);
    }
    *size = pool_size;
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

void ABTI_sched_finish(ABTI_sched *p_sched)
{
    ABTI_sched_set_request(p_sched, ABTI_SCHED_REQ_FINISH);
}

void ABTI_sched_exit(ABTI_sched *p_sched)
{
    ABTI_sched_set_request(p_sched, ABTI_SCHED_REQ_EXIT);
}

ABTU_ret_err int ABTI_sched_create_basic(ABT_sched_predef predef, int num_pools,
                                         ABT_pool *pools,
                                         ABTI_sched_config *p_config,
                                         ABTI_sched **pp_newsched)
{
    int abt_errno;
    ABT_pool_kind kind = ABT_POOL_FIFO;
    /* The default value is different from ABT_sched_create. */
    const ABT_bool def_automatic = ABT_TRUE;
    /* Always use MPMC pools */
    const ABT_pool_access def_access = ABT_POOL_ACCESS_MPMC;

    /* A pool array is provided, predef has to be compatible */
    if (pools != NULL) {
        /* Copy of the contents of pools */
        ABT_pool *pool_list;
        if (num_pools > 0) {
            abt_errno =
                ABTU_malloc(num_pools * sizeof(ABT_pool), (void **)&pool_list);
            ABTI_CHECK_ERROR(abt_errno);

            int p;
            for (p = 0; p < num_pools; p++) {
                if (pools[p] == ABT_POOL_NULL) {
                    ABTI_pool *p_newpool;
                    abt_errno =
                        ABTI_pool_create_basic(ABT_POOL_FIFO, def_access,
                                               ABT_TRUE, &p_newpool);
                    if (ABTI_IS_ERROR_CHECK_ENABLED &&
                        abt_errno != ABT_SUCCESS) {
                        /* Remove pools that are already created. */
                        int i;
                        for (i = 0; i < p; i++) {
                            if (pools[i] != ABT_POOL_NULL)
                                continue; /* User given pool. */
                            /* Free a pool created in this function. */
                            ABTI_pool_free(ABTI_pool_get_ptr(pool_list[i]));
                        }
                        ABTU_free(pool_list);
                        ABTI_HANDLE_ERROR(abt_errno);
                    }
                    pool_list[p] = ABTI_pool_get_handle(p_newpool);
                } else {
                    pool_list[p] = pools[p];
                }
            }
        } else {
            /* TODO: Check if it works. */
            pool_list = NULL;
        }

        /* Creation of the scheduler */
        switch (predef) {
            case ABT_SCHED_DEFAULT:
            case ABT_SCHED_BASIC:
                abt_errno = sched_create(ABTI_sched_get_basic_def(), num_pools,
                                         pool_list, p_config, def_automatic,
                                         pp_newsched);
                break;
            case ABT_SCHED_BASIC_WAIT:
                abt_errno = sched_create(ABTI_sched_get_basic_wait_def(),
                                         num_pools, pool_list, p_config,
                                         def_automatic, pp_newsched);
                break;
            case ABT_SCHED_PRIO:
                abt_errno = sched_create(ABTI_sched_get_prio_def(), num_pools,
                                         pool_list, p_config, def_automatic,
                                         pp_newsched);
                break;
            case ABT_SCHED_RANDWS:
                abt_errno = sched_create(ABTI_sched_get_randws_def(), num_pools,
                                         pool_list, p_config, def_automatic,
                                         pp_newsched);
                break;
            default:
                abt_errno = ABT_ERR_INV_SCHED_PREDEF;
                break;
        }
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            /* Remove pools that are already created. */
            int i;
            for (i = 0; i < num_pools; i++) {
                if (pools[i] != ABT_POOL_NULL)
                    continue; /* User given pool. */
                /* Free a pool created in this function. */
                ABTI_pool_free(ABTI_pool_get_ptr(pool_list[i]));
            }
            ABTU_free(pool_list);
            ABTI_HANDLE_ERROR(abt_errno);
        }
        ABTU_free(pool_list);
    } else { /* No pool array is provided, predef has to be compatible */
        /* Set the number of pools */
        switch (predef) {
            case ABT_SCHED_DEFAULT:
            case ABT_SCHED_BASIC:
                num_pools = 1;
                break;
            case ABT_SCHED_BASIC_WAIT:
                /* FIFO_WAIT is default pool for use with BASIC_WAIT sched */
                kind = ABT_POOL_FIFO_WAIT;
                num_pools = 1;
                break;
            case ABT_SCHED_PRIO:
                num_pools = ABTI_SCHED_NUM_PRIO;
                break;
            case ABT_SCHED_RANDWS:
                num_pools = 1;
                break;
            default:
                abt_errno = ABT_ERR_INV_SCHED_PREDEF;
                ABTI_CHECK_ERROR(abt_errno);
                break;
        }

        /* Creation of the pools */
        /* To avoid the malloc overhead, we use a stack array. */
        ABT_pool pool_list[ABTI_SCHED_NUM_PRIO];
        int p;
        /* ICC 19 warns an unused variable in the following error path.  To
         * suppress the warning, let's initialize the array member here. */
        for (p = 0; p < num_pools; p++)
            pool_list[p] = ABT_POOL_NULL;
        for (p = 0; p < num_pools; p++) {
            ABTI_pool *p_newpool;
            abt_errno =
                ABTI_pool_create_basic(kind, def_access, ABT_TRUE, &p_newpool);
            if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
                /* Remove pools that are already created. */
                int i;
                for (i = 0; i < p; i++) {
                    /* Free a pool created in this function. */
                    ABTI_pool_free(ABTI_pool_get_ptr(pool_list[i]));
                }
                ABTI_HANDLE_ERROR(abt_errno);
            }
            pool_list[p] = ABTI_pool_get_handle(p_newpool);
        }

        /* Creation of the scheduler */
        switch (predef) {
            case ABT_SCHED_DEFAULT:
            case ABT_SCHED_BASIC:
                abt_errno = sched_create(ABTI_sched_get_basic_def(), num_pools,
                                         pool_list, p_config, def_automatic,
                                         pp_newsched);
                break;
            case ABT_SCHED_BASIC_WAIT:
                abt_errno = sched_create(ABTI_sched_get_basic_wait_def(),
                                         num_pools, pool_list, p_config,
                                         def_automatic, pp_newsched);
                break;
            case ABT_SCHED_PRIO:
                abt_errno = sched_create(ABTI_sched_get_prio_def(), num_pools,
                                         pool_list, p_config, def_automatic,
                                         pp_newsched);
                break;
            case ABT_SCHED_RANDWS:
                abt_errno = sched_create(ABTI_sched_get_randws_def(), num_pools,
                                         pool_list, p_config, def_automatic,
                                         pp_newsched);
                break;
            default:
                abt_errno = ABT_ERR_INV_SCHED_PREDEF;
                break;
        }
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            /* Remove pools that are already created. */
            int i;
            for (i = 0; i < num_pools; i++) {
                /* Free a pool created in this function. */
                ABTI_pool_free(ABTI_pool_get_ptr(pool_list[i]));
            }
            ABTI_HANDLE_ERROR(abt_errno);
        }
    }
    return ABT_SUCCESS;
}

void ABTI_sched_free(ABTI_global *p_global, ABTI_local *p_local,
                     ABTI_sched *p_sched, ABT_bool force_free)
{
    ABTI_ASSERT(p_sched->used == ABTI_SCHED_NOT_USED);
    /* Free the scheduler first. */
    if (p_sched->free) {
        p_sched->free(ABTI_sched_get_handle(p_sched));
    }
    /* If sched is a default provided one, it should free its pool here.
     * Otherwise, freeing the pool is the user's responsibility. */
    size_t p;
    for (p = 0; p < p_sched->num_pools; p++) {
        ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->pools[p]);
        if (!p_pool) {
            /* p_pool can be set to NULL when that p_pool must be preserved,
             * for example, when this function is called because
             * ABT_xstream_create_basic() fails. */
            continue;
        }
        int32_t num_scheds = ABTI_pool_release(p_pool);
        if ((p_pool->automatic == ABT_TRUE && num_scheds == 0) || force_free) {
            ABTI_pool_free(p_pool);
        }
    }
    ABTU_free(p_sched->pools);

    /* Free the associated work unit */
    if (p_sched->p_ythread) {
        ABTI_thread_free(p_global, p_local, &p_sched->p_ythread->thread);
    }

    p_sched->data = NULL;

    ABTU_free(p_sched);
}

ABT_bool ABTI_sched_has_to_stop(ABTI_sched *p_sched)
{
    /* Check exit request */
    if (ABTD_atomic_acquire_load_uint32(&p_sched->request) &
        ABTI_SCHED_REQ_EXIT) {
        return ABT_TRUE;
    }

    if (!ABTI_sched_has_unit(p_sched)) {
        if (ABTD_atomic_acquire_load_uint32(&p_sched->request) &
            (ABTI_SCHED_REQ_FINISH | ABTI_SCHED_REQ_REPLACE)) {
            /* Check join request */
            if (!ABTI_sched_has_unit(p_sched))
                return ABT_TRUE;
        } else if (p_sched->used == ABTI_SCHED_IN_POOL) {
            /* Let's finish it anyway.
             * TODO: think about the condition. */
            return ABT_TRUE;
        }
    }
    return ABT_FALSE;
}

ABT_bool ABTI_sched_has_unit(ABTI_sched *p_sched)
{
    /* ABTI_sched_has_unit() does not count the number of blocked ULTs if a pool
     * has more than one consumer or the caller ES is not the latest consumer.
     * This is necessary when the ES associated with the target scheduler has to
     * be joined and the pool is shared between different schedulers associated
     * with different ESs. */
    size_t p, num_pools = p_sched->num_pools;
    for (p = 0; p < num_pools; p++) {
        ABT_pool pool = p_sched->pools[p];
        ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
        if (!ABTI_pool_is_empty(p_pool))
            return ABT_TRUE;
        switch (p_pool->access) {
            case ABT_POOL_ACCESS_PRIV:
                if (ABTD_atomic_acquire_load_int32(&p_pool->num_blocked))
                    return ABT_TRUE;
                break;
            case ABT_POOL_ACCESS_SPSC:
            case ABT_POOL_ACCESS_MPSC:
            case ABT_POOL_ACCESS_SPMC:
            case ABT_POOL_ACCESS_MPMC:
                if (ABTD_atomic_acquire_load_int32(&p_pool->num_scheds) == 1) {
                    if (ABTD_atomic_acquire_load_int32(&p_pool->num_blocked))
                        return ABT_TRUE;
                }
                break;
            default:
                break;
        }
    }
    return ABT_FALSE;
}

/* Get the pool suitable for receiving a migrating thread */
ABTU_ret_err int ABTI_sched_get_migration_pool(ABTI_sched *p_sched,
                                               ABTI_pool *source_pool,
                                               ABTI_pool **pp_pool)
{
    /* Find a pool.  If get_migr_pool is not defined, we pick the first pool */
    if (p_sched->get_migr_pool == NULL) {
        ABTI_CHECK_TRUE(p_sched->num_pools > 0, ABT_ERR_MIGRATION_TARGET);
        *pp_pool = ABTI_pool_get_ptr(p_sched->pools[0]);
    } else {
        ABT_sched sched = ABTI_sched_get_handle(p_sched);
        ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->get_migr_pool(sched));
        ABTI_CHECK_TRUE(p_pool, ABT_ERR_MIGRATION_TARGET);
        *pp_pool = p_pool;
    }
    return ABT_SUCCESS;
}

void ABTI_sched_print(ABTI_sched *p_sched, FILE *p_os, int indent,
                      ABT_bool print_sub)
{
    if (p_sched == NULL) {
        fprintf(p_os, "%*s== NULL SCHED ==\n", indent, "");
    } else {
        ABTI_sched_kind kind;
        const char *kind_str, *used;

        kind = p_sched->kind;
        if (kind == sched_get_kind(ABTI_sched_get_basic_def())) {
            kind_str = "BASIC";
        } else if (kind == sched_get_kind(ABTI_sched_get_basic_wait_def())) {
            kind_str = "BASIC_WAIT";
        } else if (kind == sched_get_kind(ABTI_sched_get_prio_def())) {
            kind_str = "PRIO";
        } else if (kind == sched_get_kind(ABTI_sched_get_randws_def())) {
            kind_str = "RANDWS";
        } else {
            kind_str = "USER";
        }

        switch (p_sched->used) {
            case ABTI_SCHED_NOT_USED:
                used = "NOT_USED";
                break;
            case ABTI_SCHED_MAIN:
                used = "MAIN";
                break;
            case ABTI_SCHED_IN_POOL:
                used = "IN_POOL";
                break;
            default:
                used = "UNKNOWN";
                break;
        }

        fprintf(p_os,
                "%*s== SCHED (%p) ==\n"
#ifdef ABT_CONFIG_USE_DEBUG_LOG
                "%*sid       : %" PRIu64 "\n"
#endif
                "%*skind     : %" PRIxPTR " (%s)\n"
                "%*sused     : %s\n"
                "%*sautomatic: %s\n"
                "%*srequest  : 0x%x\n"
                "%*snum_pools: %zu\n"
                "%*shas_unit : %s\n"
                "%*sthread   : %p\n"
                "%*sdata     : %p\n",
                indent, "", (void *)p_sched,
#ifdef ABT_CONFIG_USE_DEBUG_LOG
                indent, "", p_sched->id,
#endif
                indent, "", p_sched->kind, kind_str, indent, "", used, indent,
                "", (p_sched->automatic == ABT_TRUE) ? "TRUE" : "FALSE", indent,
                "", ABTD_atomic_acquire_load_uint32(&p_sched->request), indent,
                "", p_sched->num_pools, indent, "",
                (ABTI_sched_has_unit(p_sched) ? "TRUE" : "FALSE"), indent, "",
                (void *)p_sched->p_ythread, indent, "", p_sched->data);
        if (print_sub == ABT_TRUE) {
            size_t i;
            for (i = 0; i < p_sched->num_pools; i++) {
                ABTI_pool *p_pool = ABTI_pool_get_ptr(p_sched->pools[i]);
                ABTI_pool_print(p_pool, p_os, indent + 2);
            }
        }
    }
    fflush(p_os);
}

static ABTD_atomic_uint64 g_sched_id = ABTD_ATOMIC_UINT64_STATIC_INITIALIZER(0);
void ABTI_sched_reset_id(void)
{
    ABTD_atomic_relaxed_store_uint64(&g_sched_id, 0);
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static inline ABTI_sched_kind sched_get_kind(ABT_sched_def *def)
{
    return (ABTI_sched_kind)def;
}

ABTU_ret_err static int sched_create(ABT_sched_def *def, int num_pools,
                                     ABT_pool *pools,
                                     ABTI_sched_config *p_config,
                                     ABT_bool def_automatic,
                                     ABTI_sched **pp_newsched)
{
    ABTI_sched *p_sched;
    int p, abt_errno;

    abt_errno = ABTU_malloc(sizeof(ABTI_sched), (void **)&p_sched);
    ABTI_CHECK_ERROR(abt_errno);

    /* We read the config and set the configured parameters */
    ABT_bool automatic = def_automatic;
    if (p_config) {
        int automatic_val = 0;
        abt_errno =
            ABTI_sched_config_read(p_config, ABT_sched_config_automatic.idx,
                                   &automatic_val);
        if (abt_errno == ABT_SUCCESS) {
            automatic = (automatic_val == 0) ? ABT_FALSE : ABT_TRUE;
        }
    }

    /* Copy of the contents of pools */
    ABT_pool *pool_list;
    abt_errno = ABTU_malloc(num_pools * sizeof(ABT_pool), (void **)&pool_list);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        ABTU_free(p_sched);
        return abt_errno;
    }
    for (p = 0; p < num_pools; p++) {
        if (pools[p] == ABT_POOL_NULL) {
            ABTI_pool *p_newpool;
            abt_errno =
                ABTI_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC,
                                       ABT_TRUE, &p_newpool);
            if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
                int i;
                for (i = 0; i < p; i++) {
                    if (pools[i] == ABT_POOL_NULL)
                        ABTI_pool_free(ABTI_pool_get_ptr(pool_list[i]));
                }
                ABTU_free(pool_list);
                ABTU_free(p_sched);
                return abt_errno;
            }
            pool_list[p] = ABTI_pool_get_handle(p_newpool);
        } else {
            pool_list[p] = pools[p];
        }
    }
    /* Check if the pools are available */
    for (p = 0; p < num_pools; p++) {
        ABTI_pool_retain(ABTI_pool_get_ptr(pool_list[p]));
    }

    p_sched->used = ABTI_SCHED_NOT_USED;
    p_sched->automatic = automatic;
    p_sched->kind = sched_get_kind(def);
    p_sched->p_replace_sched = NULL;
    p_sched->p_replace_waiter = NULL;
    ABTD_atomic_relaxed_store_uint32(&p_sched->request, 0);
    p_sched->pools = pool_list;
    p_sched->num_pools = num_pools;
    p_sched->type = def->type;
    p_sched->p_ythread = NULL;
    p_sched->data = NULL;

    p_sched->init = def->init;
    p_sched->run = def->run;
    p_sched->free = def->free;
    p_sched->get_migr_pool = def->get_migr_pool;

#ifdef ABT_CONFIG_USE_DEBUG_LOG
    p_sched->id = sched_get_new_id();
#endif

    /* Return value */
    ABT_sched newsched = ABTI_sched_get_handle(p_sched);

    /* Specific initialization */
    if (p_sched->init) {
        ABT_sched_config config = ABTI_sched_config_get_handle(p_config);
        abt_errno = p_sched->init(newsched, config);
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            for (p = 0; p < num_pools; p++) {
                if (pools[p] == ABT_POOL_NULL) {
                    ABTI_pool_free(ABTI_pool_get_ptr(pool_list[p]));
                } else {
                    ABTI_pool_release(ABTI_pool_get_ptr(pool_list[p]));
                }
            }
            ABTU_free(pool_list);
            ABTU_free(p_sched);
            return abt_errno;
        }
    }

    *pp_newsched = p_sched;

    return ABT_SUCCESS;
}

#ifdef ABT_CONFIG_USE_DEBUG_LOG
static inline uint64_t sched_get_new_id(void)
{
    return ABTD_atomic_fetch_add_uint64(&g_sched_id, 1);
}
#endif
