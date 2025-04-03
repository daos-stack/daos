/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTU_ret_err static int pool_create(
    ABT_pool_access access, const ABTI_pool_required_def *p_required_def,
    const ABTI_pool_optional_def *p_optional_def,
    const ABTI_pool_deprecated_def *p_deprecated_def,
    const ABTI_pool_old_def *p_old_def, ABTI_pool_config *p_config,
    ABT_bool def_automatic, ABT_bool is_builtin, ABTI_pool **pp_newpool);
static void
pool_create_def_from_old_def(const ABT_pool_def *p_def,
                             ABTI_pool_old_def *p_old_def,
                             ABTI_pool_required_def *p_required_def,
                             ABTI_pool_optional_def *p_optional_def,
                             ABTI_pool_deprecated_def *p_deprecated_def);
static inline int pool_pop_thread_ex(ABT_pool pool, ABT_thread *thread,
                                     ABT_pool_context pool_ctx);
static inline int pool_pop_threads_ex(ABT_pool pool, ABT_thread *threads,
                                      size_t len, size_t *num,
                                      ABT_pool_context pool_ctx);
static inline int pool_push_thread_ex(ABT_pool pool, ABT_thread thread,
                                      ABT_pool_context pool_ctx);
static inline int pool_push_threads_ex(ABT_pool pool, const ABT_thread *threads,
                                       size_t num, ABT_pool_context pool_ctx);
static inline int pool_pop_wait_thread_ex(ABT_pool pool, ABT_thread *thread,
                                          double time_secs,
                                          ABT_pool_context pool_ctx);
typedef struct {
    void *arg;
    void (*print_fn)(void *, ABT_unit);
} pool_print_thread_to_unit_arg_t;
static void pool_print_thread_to_unit(void *arg, ABT_thread thread);
typedef struct {
    void *arg;
    void (*print_fn)(void *, ABT_thread);
} pool_print_unit_to_thread_arg_t;
static void pool_print_unit_to_thread(void *arg, ABT_unit unit);

/** @defgroup POOL Pool
 * This group is for Pool.
 */

/**
 * @ingroup POOL
 * @brief   Create a new pool.
 *
 * \c ABT_pool_create() creates a new pool, given by the pool definition
 * (\c def) and a pool configuration (\c config), and returns its handle through
 * \c newpool.
 *
 * \c def must define all the non-optional functions.  See
 * \c ABT_pool_user_def_create() for details.
 *
 * @note
 * \c #ABT_pool_def is kept for compatibility.  The user is highly recommended
 * to use \c ABT_pool_user_def instead.
 *
 * The caller of each pool function is undefined, so a program that relies on
 * the caller of pool functions is non-conforming.
 *
 * @note
 * Specifically, any explicit or implicit context-switching operation in a pool
 * function may cause undefined behavior.
 *
 * \c newpool can be configured via \c config.  If the user passes
 * \c ABT_POOL_CONFIG_NULL for \c config, the default configuration is used.
 * If \c p_init is not \c NULL, this routine calls \c p_init() with
 * \c newpool as the first argument and \c config as the second argument.  This
 * routine returns an error returned by \c p_init() if \c p_init() does not
 * return \c ABT_SUCCESS.
 *
 * @note
 * \DOC_NOTE_DEFAULT_POOL_CONFIG
 *
 * This routine copies \c def and \c config, so the user can free \c def and
 * \c config after this routine returns.
 *
 * \DOC_DESC_POOL_AUTOMATIC{\c newpool} By default \c newpool created by this
 * routine is not automatically freed.
 *
 * @note
 * \DOC_NOTE_EFFECT_ABT_FINALIZE
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newpool, \c ABT_POOL_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_USR_POOL_INIT{\c p_init()}
 * \DOC_ERROR_INV_POOL_USER_DEF_HANDLE{\c def}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{any non-optional pool function of \c def}
 * \DOC_UNDEFINED_NULL_PTR{\c newpool}
 *
 * @param[in]  def      pool definition required for pool creation
 * @param[in]  config   pool configuration for pool creation
 * @param[out] newpool  pool handle
 * @return Error code
 */
int ABT_pool_create(ABT_pool_user_def def, ABT_pool_config config,
                    ABT_pool *newpool)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newpool);
    ABTI_UB_ASSERT(def);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newpool to NULL on error. */
    *newpool = ABT_POOL_NULL;
#endif
    ABT_pool_access access;
    ABTI_pool_required_def required_def, *p_required_def;
    ABTI_pool_optional_def optional_def, *p_optional_def;
    ABTI_pool_deprecated_def deprecated_def, *p_deprecated_def;
    ABTI_pool_old_def old_def, *p_old_def;
    /* Copy def */
    if (ABTI_pool_user_def_is_new(def)) {
        /* New ABTI_pool_user_def */
        access = ABT_POOL_ACCESS_MPMC;
        ABTI_pool_user_def *p_def = ABTI_pool_user_def_get_ptr(def);
        ABTI_CHECK_NULL_POOL_USER_DEF_PTR(p_def);
        p_required_def = &p_def->required_def;
        p_optional_def = &p_def->optional_def;
        p_deprecated_def = NULL;
        p_old_def = NULL;
    } else {
        /* Old ABT_pool_def */
        ABTI_UB_ASSERT(def->u_create_from_thread);
        ABTI_UB_ASSERT(def->u_free);
        ABTI_UB_ASSERT(def->p_get_size);
        ABTI_UB_ASSERT(def->p_push);
        ABTI_UB_ASSERT(def->p_pop);
        access = def->access;
        pool_create_def_from_old_def(def, &old_def, &required_def,
                                     &optional_def, &deprecated_def);
        p_required_def = &required_def;
        p_optional_def = &optional_def;
        p_deprecated_def = &deprecated_def;
        p_old_def = &old_def;
    }

    ABTI_pool *p_newpool;
    ABTI_pool_config *p_config = ABTI_pool_config_get_ptr(config);
    const ABT_bool def_automatic = ABT_FALSE;
    int abt_errno =
        pool_create(access, p_required_def, p_optional_def, p_deprecated_def,
                    p_old_def, p_config, def_automatic, ABT_FALSE, &p_newpool);
    ABTI_CHECK_ERROR(abt_errno);

    *newpool = ABTI_pool_get_handle(p_newpool);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Create a new pool from a predefined type.
 *
 * \c ABT_pool_create_basic() creates a new pool, given by the pool type
 * \c kind, the access type \c access, and the automatic flag \c automatic, and
 * returns its handle through \c newpool.
 *
 * \c kind specifies the implementation of \c newpool.  See \c #ABT_pool_kind
 * for details of predefined pools.
 *
 * \c access hints at the usage of the created pool.  Argobots may choose an
 * optimized implementation for a pool with a more restricted access type
 * (\c #ABT_POOL_ACCESS_PRIV is the most strict access type).  See
 * \c #ABT_pool_access for details.
 *
 * If \c automatic is \c ABT_FALSE, \c newpool is not automatically freed, so
 * \c newpool must be freed by \c ABT_pool_free() after its use unless
 * \c newpool is associated with the main scheduler of the primary execution
 * stream.
 *
 * @note
 * \DOC_NOTE_EFFECT_ABT_FINALIZE
 *
 * If \c automatic is \c ABT_TRUE, \c newpool is automatically freed when all
 * the schedulers associated with \c newpool are freed.  If the user does not
 * associate \c newpool with a scheduler, the user needs to manually free
 * \c newpool regardless of \c automatic.
 *
 * @changev11
 * \DOC_DESC_V10_POOL_NOACCESS
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newpool, \c ABT_POOL_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_KIND{\c kind}
 * \DOC_ERROR_INV_POOL_ACCESS{\c access}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_BOOL{automatic}
 * \DOC_UNDEFINED_NULL_PTR{\c newpool}
 *
 * @param[in]  kind       type of the predefined pool
 * @param[in]  access     access type of the predefined pool
 * @param[in]  automatic  \c ABT_TRUE if the pool should be automatically freed
 * @param[out] newpool    pool handle
 * @return Error code
 */
int ABT_pool_create_basic(ABT_pool_kind kind, ABT_pool_access access,
                          ABT_bool automatic, ABT_pool *newpool)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT_BOOL(automatic);
    ABTI_UB_ASSERT(newpool);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newpool to NULL on error. */
    *newpool = ABT_POOL_NULL;
#endif
    ABTI_pool *p_newpool;
    int abt_errno = ABTI_pool_create_basic(kind, access, automatic, &p_newpool);
    ABTI_CHECK_ERROR(abt_errno);

    *newpool = ABTI_pool_get_handle(p_newpool);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Free a pool.
 *
 * \c ABT_pool_free() frees the resource used for the pool \c pool and sets
 * \c pool to \c ABT_POOL_NULL.  If \c pool is created by \c ABT_pool_create()
 * and \c p_free is not \c NULL, this routine calls \c p_free() with \c pool as
 * the argument.  The return value of \c p_free() is ignored.  Afterward, this
 * routine deallocates the resource used for \c pool and sets \c pool to
 * \c ABT_POOL_NULL.
 *
 * \c pool must be empty and no work unit may be associated with \c pool.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_PTR{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c pool}
 * \DOC_UNDEFINED_POOL_FREE{\c pool}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c pool}
 *
 * @param[in,out] pool  pool handle
 * @return Error code
 */
int ABT_pool_free(ABT_pool *pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pool);

    ABT_pool h_pool = *pool;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(h_pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_UB_ASSERT(ABTI_pool_is_empty(p_pool));

    ABTI_pool_free(p_pool);

    *pool = ABT_POOL_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Get an access type of a pool.
 *
 * \c ABT_pool_get_access() returns the access type of the pool \c pool through
 * \c access.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c access}
 *
 * @param[in]  pool    pool handle
 * @param[out] access  access type
 * @return Error code
 */
int ABT_pool_get_access(ABT_pool pool, ABT_pool_access *access)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(access);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    *access = p_pool->access;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Check if a pool is empty.
 *
 * \c ABT_pool_is_empty() returns whether the pool \c pool is or not through
 * \c is_empty.  If \c pool is empty, \c ABT_TRUE is set to \c is_empty.
 * Otherwise, \c ABT_FALSE is set to \c is_empty.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_empty}
 *
 * @param[in]  pool      pool handle
 * @param[out] is_empty  emptiness of a pool
 * @return Error code
 */
int ABT_pool_is_empty(ABT_pool pool, ABT_bool *is_empty)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_empty);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    *is_empty = ABTI_pool_is_empty(p_pool);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Get the total size of a pool.
 *
 * \c ABT_pool_get_total_size() returns the total size of the pool \c pool
 * through \c size.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine sets \c size to the sum of a value returned by \c p_get_size()
 *   called with \c pool as its argument and the number of blocking work units
 *   that are associated with \c pool.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine sets \c size to the sum of the number of work units including
 *   works units in \c pool and suspended work units associated with \c pool.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_get_size()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c size}
 *
 * @param[in]  pool  pool handle
 * @param[out] size  total size of \c pool
 * @return Error code
 */
int ABT_pool_get_total_size(ABT_pool pool, size_t *size)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(size);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_get_size, ABT_ERR_POOL);

    *size = ABTI_pool_get_total_size(p_pool);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Get the size of a pool.
 *
 * \c ABT_pool_get_size() returns the size of the pool \c pool through \c size.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine sets \c size to a value returned by \c p_get_size() called
 *   with \c pool as its argument.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine sets \c size to the number of work units in \c pool.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_get_size()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c size}
 *
 * @param[in]  pool  pool handle
 * @param[out] size  size of \c pool
 * @return Error code
 */
int ABT_pool_get_size(ABT_pool pool, size_t *size)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(size);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_get_size, ABT_ERR_POOL);

    *size = ABTI_pool_get_size(p_pool);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Pop a work unit from a pool.
 *
 * The functionality of this routine is the same as \c ABT_pool_pop_thread_ex()
 * while \c ABT_POOL_CONTEXT_OP_POOL_OTHER is passed as \c pool_ctx.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 *
 * @param[in]  pool    pool handle
 * @param[out] thread  work unit handle
 * @return Error code
 */
int ABT_pool_pop_thread(ABT_pool pool, ABT_thread *thread)
{
    return pool_pop_thread_ex(pool, thread, ABT_POOL_CONTEXT_OP_POOL_OTHER);
}

/**
 * @ingroup POOL
 * @brief   Pop a work unit from a pool.
 *
 * \c ABT_pool_pop_thread_ex() pops a work unit from the pool \c pool and sets
 * it to \c thread.  The pool context \c pool_ctx is passed to \c pool.  If the
 * underlying pool implementation successfully pops a work unit, this routine
 * sets \c thread to a work unit handle associated with the returned
 * \c ABT_unit.  Otherwise, this routine sets \c thread to \c ABT_THREAD_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 *
 * @param[in]  pool      pool handle
 * @param[out] p_unit    work unit handle
 * @param[in]  pool_ctx  pool context
 * @return Error code
 */
int ABT_pool_pop_thread_ex(ABT_pool pool, ABT_thread *thread,
                           ABT_pool_context pool_ctx)
{
    return pool_pop_thread_ex(pool, thread, pool_ctx);
}

/**
 * @ingroup POOL
 * @brief   Pop work units from a pool.
 *
 * The functionality of this routine is the same as \c ABT_pool_pop_threads_ex()
 * while \c ABT_POOL_CONTEXT_OP_POOL_OTHER is passed as \c pool_ctx.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_pop_many_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c threads, \c len is positive}
 * \DOC_UNDEFINED_NULL_PTR{\c num}
 *
 * @param[in]  pool    pool handle
 * @param[out] thread  work unit handle
 * @return Error code
 */
int ABT_pool_pop_threads(ABT_pool pool, ABT_thread *threads, size_t len,
                         size_t *num)
{
    return pool_pop_threads_ex(pool, threads, len, num,
                               ABT_POOL_CONTEXT_OP_POOL_OTHER);
}

/**
 * @ingroup POOL
 * @brief   Pop work units from a pool.
 *
 * \c ABT_pool_pop_thread_ex() pops at most \c len work units from the pool
 * \c pool and sets them to \c threads.  The number of popped work units is set
 * to \c num.  The pool context \c pool_ctx is passed to \c pool.
 *
 * If the underlying pool implementation successfully pops work units, this
 * routine sets the first \c num elements of \c threads to work unit handles
 * associated with the returned \c ABT_unit.
 *
 * @note
 * \DOC_NOTE_NO_PADDING{\c threads, \c len}
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_pop_many_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c threads}
 * \DOC_UNDEFINED_NULL_PTR{\c num}
 *
 * @param[in]  pool      pool handle
 * @param[out] threads   work unit handles
 * @param[in]  len       the number of \c threads entries
 * @param[out] num       the number of popped work units
 * @param[in]  pool_ctx  pool context
 * @return Error code
 */
int ABT_pool_pop_threads_ex(ABT_pool pool, ABT_thread *threads, size_t len,
                            size_t *num, ABT_pool_context pool_ctx)
{
    return pool_pop_threads_ex(pool, threads, len, num, pool_ctx);
}

/**
 * @ingroup POOL
 * @brief   Push a work unit to a pool.
 *
 * The functionality of this routine is the same as \c ABT_pool_push_thread_ex()
 * while \c ABT_POOL_CONTEXT_OP_POOL_OTHER is passed as \c pool_ctx.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] pool    pool handle
 * @param[in] thread  work unit handle
 * @return Error code
 */
int ABT_pool_push_thread(ABT_pool pool, ABT_thread thread)
{
    return pool_push_thread_ex(pool, thread, ABT_POOL_CONTEXT_OP_POOL_OTHER);
}

/**
 * @ingroup POOL
 * @brief   Push a work unit to a pool.
 *
 * \c ABT_pool_push_thread_ex() pushes the work unit \c thread to the pool
 * \c pool.  The pool context \c pool_ctx is passed to \c pool.  If \c thread
 * is \c ABT_THREAD_NULL, this routine does not push a work unit and returns
 * \c ABT_SUCCESS.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] pool      pool handle
 * @param[in] thread    work unit handle
 * @param[in] pool_ctx  pool context
 * @return Error code
 */
int ABT_pool_push_thread_ex(ABT_pool pool, ABT_thread thread,
                            ABT_pool_context pool_ctx)
{
    return pool_push_thread_ex(pool, thread, pool_ctx);
}

/**
 * @ingroup POOL
 * @brief   Push work units to a pool.
 *
 * The functionality of this routine is the same as
 * \c ABT_pool_push_threads_ex() while \c ABT_POOL_CONTEXT_OP_POOL_OTHER is
 * passed as \c pool_ctx.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_push_many_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c threads, \c num is positive}
 *
 * @param[in] pool     pool handle
 * @param[in] threads  work unit handles
 * @param[in] num      the number of work unit handles
 * @return Error code
 */
int ABT_pool_push_threads(ABT_pool pool, const ABT_thread *threads, size_t num)
{
    return pool_push_threads_ex(pool, threads, num,
                                ABT_POOL_CONTEXT_OP_POOL_OTHER);
}

/**
 * @ingroup POOL
 * @brief   Push work units to a pool.
 *
 * \c ABT_pool_push_threads_ex() pushes \c num work units stored in \c threads
 * to the pool \c pool.  The pool context \c pool_ctx is passed to \c pool.
 * This routine ignores \c ABT_THREAD_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_push_many_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR_CONDITIONAL{\c threads, \c num is positive}
 *
 * @param[in] pool      pool handle
 * @param[in] threads   work unit handles
 * @param[in] num       the number of work unit handles
 * @param[in] pool_ctx  pool context
 * @return Error code
 */
int ABT_pool_push_threads_ex(ABT_pool pool, const ABT_thread *threads,
                             size_t num, ABT_pool_context pool_ctx)
{
    return pool_push_threads_ex(pool, threads, num, pool_ctx);
}

/**
 * @ingroup POOL
 * @brief   Pop a work unit from a pool.
 *
 * The functionality of this routine is the same as
 * \c ABT_pool_pop_wait_thread_ex() while \c ABT_POOL_CONTEXT_OP_POOL_OTHER is
 * passed as \c pool_ctx.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_pop_wait_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 *
 * @param[in]  pool       pool handle
 * @param[out] thread     work unit handle
 * @param[in]  time_secs  duration of waiting time (seconds)
 * @return Error code
 */
int ABT_pool_pop_wait_thread(ABT_pool pool, ABT_thread *thread,
                             double time_secs)
{
    return pool_pop_wait_thread_ex(pool, thread, time_secs,
                                   ABT_POOL_CONTEXT_OP_POOL_OTHER);
}

/**
 * @ingroup POOL
 * @brief   Pop a work unit from a pool.
 *
 * \c ABT_pool_pop_wait_thread_ex() pops a work unit from the pool \c pool and
 * sets it to \c thread.  The pool context \c pool_ctx is passed to \c pool.
 * This routine might block on \c pool to wait for up to \c time_sec seconds
 * when \c pool does not have a work unit to return.
 *
 * If the underlying pool implementation successfully pops a work unit, this
 * routine sets \c thread to a work unit handle associated with the returned
 * \c ABT_unit.  Otherwise, this routine sets \c thread to \c ABT_THREAD_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_pop_wait_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 *
 * @param[in]  pool       pool handle
 * @param[out] thread     work unit handle
 * @param[in]  time_secs  duration of waiting time (seconds)
 * @param[in]  pool_ctx   pool context
 * @return Error code
 */
int ABT_pool_pop_wait_thread_ex(ABT_pool pool, ABT_thread *thread,
                                double time_secs, ABT_pool_context pool_ctx)
{
    return pool_pop_wait_thread_ex(pool, thread, time_secs, pool_ctx);
}

/**
 * @ingroup POOL
 * @brief   Apply a print function to every work unit in a pool.
 *
 * \c ABT_pool_print_all_threads() calls \c print_fn() for every work unit in
 * the pool \c pool.  \c print_fn() is called with \c arg as its first argument
 * and the handle of the work unit as the second argument.
 *
 * @note
 * As the name of the argument implies, \c print_fn() may not have any side
 * effect; \c ABT_pool_print_all_threads() is for debugging and profiling.  For
 * example, changing the state of \c ABT_thread in \c print_fn() is forbidden.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c ABT_pool_user_print_all_fn}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c print_fn}
 * \DOC_UNDEFINED_CHANGE_STATE{\c print_fn()}
 *
 * @param[in] pool      pool handle
 * @param[in] arg       argument passed to \c print_fn
 * @param[in] print_fn  user-defined print function
 * @return Error code
 */
int ABT_pool_print_all_threads(ABT_pool pool, void *arg,
                               void (*print_fn)(void *arg, ABT_thread))
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(print_fn);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_print_all, ABT_ERR_POOL);

    p_pool->optional_def.p_print_all(pool, arg, print_fn);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Pop a work unit from a pool.
 *
 * \c ABT_pool_pop() pops a work unit from the pool \c pool and sets it to
 * \c p_unit.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine sets \c p_unit to a value returned by \c p_pop() called with
 *   \c pool as its argument.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine tries to pop a work unit from \c pool.  If this routine
 *   successfully pops a work unit, this routine sets \c p_unit to the obtained
 *   handle of \c ABT_unit.  Otherwise, this routine sets \c p_unit to
 *   \c ABT_UNIT_NULL.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 *
 * \DOC_DESC_V10_NOEXT{\c ABT_ERR_INV_XSTREAM}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c p_unit, \c ABT_UNIT_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c p_unit}
 *
 * @param[in]  pool    pool handle
 * @param[out] p_unit  unit handle
 * @return Error code
 */
int ABT_pool_pop(ABT_pool pool, ABT_unit *p_unit)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(p_unit);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    ABT_thread thread = ABTI_pool_pop(p_pool, ABT_POOL_CONTEXT_OP_POOL_OTHER);
    if (thread != ABT_THREAD_NULL) {
        ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
        *p_unit = p_thread->unit;
    } else {
        *p_unit = ABT_UNIT_NULL;
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Pop a unit from a pool with wait.
 *
 * \c ABT_pool_pop_wait() pops a work unit from the pool \c pool and sets it to
 * \c p_unit.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine sets \c p_unit to a value returned by \c p_pop_wait() called
 *   with \c pool as its first argument and \c time_sec as the second argument.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine tries to pop a work unit from \c pool.  If \c pool is empty,
 *   an underlying execution stream or an external thread that calls this
 *   routine is blocked on \c pool for \c time_sec seconds.  If this routine
 *   successfully pops a work unit, this routine sets \c p_unit to the obtained
 *   handle of \c ABT_unit.  Otherwise, this routine sets \c p_unit to
 *   \c ABT_UNIT_NULL.
 *
 * @note
 * In most cases, \c ABT_pool_pop() is more efficient.  \c ABT_pool_pop_wait()
 * would be useful in cases where the user wants to sleep execution streams when
 * \c pool is empty.
 *
 * @changev20
 * \DOC_DESC_V1X_P_POP_WAIT
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c p_unit, \c ABT_UNIT_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_pop_wait()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c p_unit}
 *
 * @param[in]  pool       pool handle
 * @param[out] p_unit     unit handle
 * @param[in]  time_secs  duration of waiting time (seconds)
 * @return Error code
 */
int ABT_pool_pop_wait(ABT_pool pool, ABT_unit *p_unit, double time_secs)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(p_unit);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_pop_wait, ABT_ERR_POOL);

    ABT_thread thread =
        ABTI_pool_pop_wait(p_pool, time_secs, ABT_POOL_CONTEXT_OP_POOL_OTHER);
    if (thread != ABT_THREAD_NULL) {
        ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
        *p_unit = p_thread->unit;
    } else {
        *p_unit = ABT_UNIT_NULL;
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Pop a unit from a pool with timed wait.
 *
 * \c ABT_pool_pop_timedwait() pops a work unit from the pool \c pool and sets
 * it to \c p_unit.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine sets \c p_unit to a value returned by \c p_pop_timedwait()
 *   called with \c pool as its first argument and \c abstime_secs as the second
 *   argument.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine tries to pop a work unit from \c pool.  If \c pool is empty,
 *   an underlying execution stream or an external thread that calls this
 *   routine is blocked on \c pool until the current time exceeds
 *   \c abstime_secs.  If this routine successfully pops a work unit, this
 *   routine sets \c p_unit to the obtained handle of \c ABT_unit.  Otherwise,
 *   this routine sets \c p_unit to \c ABT_UNIT_NULL.
 *
 * @note
 * \c abstime_secs can be calculated by adding an offset time to a value
 * returned by \c ABT_get_wtime().\n
 * \DOC_NOTE_REPLACEMENT{\c ABT_pool_pop_wait()}.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 *
 * \DOC_DESC_V10_NOEXT{\c ABT_ERR_INV_XSTREAM}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c p_unit, \c ABT_UNIT_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_pop_timedwait()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c p_unit}
 *
 * @param[in]  pool          pool handle
 * @param[out] p_unit        unit handle
 * @param[in]  abstime_secs  absolute time for timeout
 * @return Error code
 */
int ABT_pool_pop_timedwait(ABT_pool pool, ABT_unit *p_unit, double abstime_secs)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(p_unit);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->deprecated_def.p_pop_timedwait, ABT_ERR_POOL);

    ABT_thread thread = ABTI_pool_pop_timedwait(p_pool, abstime_secs);
    if (thread != ABT_THREAD_NULL) {
        ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
        *p_unit = p_thread->unit;
    } else {
        *p_unit = ABT_UNIT_NULL;
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Push a unit to a pool
 *
 * \c ABT_pool_push() pushes a work unit \c unit to the pool \c pool.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine calls \c p_push() with \c pool as its first argument and
 *   \c unit as the second argument.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine pushes a work unit \c unit to \c pool.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 *
 * \DOC_DESC_V10_ERROR_CODE_CHANGE{\c ABT_ERR_UNIT, \c ABT_ERR_INV_UNIT,
 *                                 \c unit is \c ABT_UNIT_NULL}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_INV_UNIT_HANDLE{\c unit}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] pool  pool handle
 * @param[in] unit  unit handle
 * @return Error code
 */
int ABT_pool_push(ABT_pool pool, ABT_unit unit)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    ABTI_CHECK_TRUE(unit != ABT_UNIT_NULL, ABT_ERR_INV_UNIT);

    ABTI_thread *p_thread;
    int abt_errno =
        ABTI_unit_set_associated_pool(p_global, unit, p_pool, &p_thread);
    ABTI_CHECK_ERROR(abt_errno);
    /* ABTI_unit_set_associated_pool() might change unit, so "unit" must be read
     * again from p_thread. */
    ABTI_pool_push(p_pool, p_thread->unit, ABT_POOL_CONTEXT_OP_POOL_OTHER);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Remove a specified work unit from a pool
 *
 * \c ABT_pool_remove() removes a work unit \c unit from the pool \c pool.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine calls \c p_remove() with \c pool as its first argument and
 *   \c unit as the second argument.  The return value of \c p_remove() is
 *   ignored.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine removes a work unit \c unit from the pool \c pool and returns
 *   \c ABT_SUCCESS.
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 *
 * \DOC_DESC_V10_NOEXT{\c ABT_ERR_INV_XSTREAM}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_INV_UNIT_HANDLE{\c unit}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_remove()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WORK_UNIT_NOT_IN_POOL{\c pool, \c unit}
 *
 * @param[in] pool  pool handle
 * @param[in] unit  unit handle
 * @return Error code
 */
int ABT_pool_remove(ABT_pool pool, ABT_unit unit)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->deprecated_def.p_remove, ABT_ERR_POOL);

    /* unit must be in this pool, so we do not need to reset its associated
     * pool. */
    int abt_errno = ABTI_pool_remove(p_pool, unit);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Apply a print function to every work unit in a pool using a
 *          user-defined function.
 *
 * \c ABT_pool_print_all() calls \c print_fn() for every work unit in the pool
 * \c pool.
 *
 * - If \c pool is created by \c ABT_pool_create():
 *
 *   This routine calls \c p_pop_print() with \c pool as its first argument,
 *   \c arg as the second argument, and \c print_fn as the third argument  The
 *   return value of \c p_pop_print() is ignored.
 *
 * - If \c pool is created by \c ABT_pool_create_basic():
 *
 *   This routine calls \c print_fn() for every work unit in \c pool.
 *   \c print_fn() is called with \c arg as its first argument and the handle of
 *   the work unit as the second argument.
 *
 * @note
 * As the name of the argument implies, \c print_fn() may not have any side
 * effect; \c ABT_pool_print_all() is for debugging and profiling.  For example,
 * changing the state of \c ABT_unit in \c print_fn() is forbidden.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_print_all()}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c print_fn}
 * \DOC_UNDEFINED_CHANGE_STATE{\c print_fn()}
 *
 * @param[in] pool      pool handle
 * @param[in] arg       argument passed to \c print_fn
 * @param[in] print_fn  user-defined print function
 * @return Error code
 */
int ABT_pool_print_all(ABT_pool pool, void *arg,
                       void (*print_fn)(void *, ABT_unit))
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(print_fn);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_print_all, ABT_ERR_POOL);

    pool_print_thread_to_unit_arg_t func_arg = { arg, print_fn };
    p_pool->optional_def.p_print_all(pool, (void *)&func_arg,
                                     pool_print_thread_to_unit);
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Set user data in a pool.
 *
 * \c ABT_pool_set_data() sets user data of the pool \c pool to \c data.  The
 * old value is overwritten.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c pool}
 *
 * @param[in]  pool  pool handle
 * @param[in]  data  user data in \c pool
 * @return Error code
 */
int ABT_pool_set_data(ABT_pool pool, void *data)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    p_pool->data = data;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Retrieve user data from a pool
 *
 * \c ABT_pool_get_data() returns user data in the pool \c pool through \c data.
 *
 * @note
 * The user data of the newly created pool is \c NULL.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c data}
 *
 * @param[in]  pool  pool handle
 * @param[out] data  user data in \c pool
 * @return Error code
 */
int ABT_pool_get_data(ABT_pool pool, void **data)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(data);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    *data = p_pool->data;
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Create a new work unit associated with a scheduler and push it to a
 *          pool
 *
 * ABT_pool_add_sched() creates a work unit that works as a scheduler \c sched
 * and pushes the newly created work unit to \c pool.  See \c ABT_pool_push()
 * for the push operation.  The created work unit is automatically freed when it
 * finishes its scheduling function.
 *
 * While the created work unit is using \c sched, the user may not free
 * \c sched.  Associating \c sched with more than one work unit causes undefined
 * behavior.
 *
 * \c sched should have been created by \c ABT_sched_create() or
 * \c ABT_sched_create_basic().
 *
 * @changev11
 * \DOC_DESC_V10_ACCESS_VIOLATION
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_CRUDE_SCHED_USED_CHECK{\c sched, \c ABT_ERR_INV_SCHED}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_ERROR_RESOURCE
 * \DOC_V1X \DOC_ERROR_SCHED_USED{\c sched, \c ABT_ERR_INV_SCHED}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_V20 \DOC_UNDEFINED_SCHED_USED{\c sched}
 *
 * @param[in] pool   pool handle
 * @param[in] sched  scheduler handle
 * @return Error code
 */
int ABT_pool_add_sched(ABT_pool pool, ABT_sched sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);

    /* Mark the scheduler as it is used in pool */
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE(p_sched->used == ABTI_SCHED_NOT_USED, ABT_ERR_INV_SCHED);
#else
    ABTI_UB_ASSERT(p_sched->used == ABTI_SCHED_NOT_USED);
#endif
    p_sched->used = ABTI_SCHED_IN_POOL;

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* In both ABT_SCHED_TYPE_ULT and ABT_SCHED_TYPE_TASK cases, we use ULT-type
     * scheduler to reduce the code maintenance cost. */
#endif
    int abt_errno =
        ABTI_ythread_create_sched(p_global, p_local, p_pool, p_sched);
    if (abt_errno != ABT_SUCCESS) {
        p_sched->used = ABTI_SCHED_NOT_USED;
        ABTI_HANDLE_ERROR(abt_errno);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup POOL
 * @brief   Get ID of a pool
 *
 * \c ABT_pool_get_id() returns the ID of the pool \c pool through \c id.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c id}
 *
 * @param[in]  pool  pool handle
 * @param[out] id    pool ID
 * @return Error code
 */
int ABT_pool_get_id(ABT_pool pool, int *id)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(id);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    *id = (int)p_pool->id;
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

ABTU_ret_err int ABTI_pool_create_basic(ABT_pool_kind kind,
                                        ABT_pool_access access,
                                        ABT_bool automatic,
                                        ABTI_pool **pp_newpool)
{
    int abt_errno;
    ABTI_CHECK_TRUE(access == ABT_POOL_ACCESS_PRIV ||
                        access == ABT_POOL_ACCESS_SPSC ||
                        access == ABT_POOL_ACCESS_MPSC ||
                        access == ABT_POOL_ACCESS_SPMC ||
                        access == ABT_POOL_ACCESS_MPMC,
                    ABT_ERR_INV_POOL_ACCESS);

    ABTI_pool_required_def required_def;
    ABTI_pool_optional_def optional_def;
    ABTI_pool_deprecated_def deprecated_def;
    switch (kind) {
        case ABT_POOL_FIFO:
            abt_errno = ABTI_pool_get_fifo_def(access, &required_def,
                                               &optional_def, &deprecated_def);
            break;
        case ABT_POOL_FIFO_WAIT:
            abt_errno =
                ABTI_pool_get_fifo_wait_def(access, &required_def,
                                            &optional_def, &deprecated_def);
            break;
        case ABT_POOL_RANDWS:
            abt_errno =
                ABTI_pool_get_randws_def(access, &required_def, &optional_def,
                                         &deprecated_def);
            break;
        default:
            abt_errno = ABT_ERR_INV_POOL_KIND;
            break;
    }
    ABTI_CHECK_ERROR(abt_errno);

    abt_errno =
        pool_create(access, &required_def, &optional_def, &deprecated_def, NULL,
                    NULL, automatic, ABT_TRUE, pp_newpool);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

void ABTI_pool_free(ABTI_pool *p_pool)
{
    ABT_pool h_pool = ABTI_pool_get_handle(p_pool);
    if (p_pool->optional_def.p_free) {
        p_pool->optional_def.p_free(h_pool);
    }
    ABTU_free(p_pool);
}

ABT_thread ABTI_pool_pop_timedwait(ABTI_pool *p_pool, double abstime_secs)
{
    ABTI_UB_ASSERT(p_pool->deprecated_def.p_pop_timedwait);
    ABT_unit unit =
        p_pool->deprecated_def.p_pop_timedwait(ABTI_pool_get_handle(p_pool),
                                               abstime_secs);
    if (unit == ABT_UNIT_NULL) {
        return ABT_THREAD_NULL;
    } else {
        ABTI_thread *p_thread =
            ABTI_unit_get_thread(ABTI_global_get_global(), unit);
        ABT_thread thread = ABTI_thread_get_handle(p_thread);
        LOG_DEBUG_POOL_POP(p_pool, thread);
        return thread;
    }
}

void ABTI_pool_print(ABTI_pool *p_pool, FILE *p_os, int indent)
{
    if (p_pool == NULL) {
        fprintf(p_os, "%*s== NULL POOL ==\n", indent, "");
    } else {
        const char *access;

        switch (p_pool->access) {
            case ABT_POOL_ACCESS_PRIV:
                access = "PRIV";
                break;
            case ABT_POOL_ACCESS_SPSC:
                access = "SPSC";
                break;
            case ABT_POOL_ACCESS_MPSC:
                access = "MPSC";
                break;
            case ABT_POOL_ACCESS_SPMC:
                access = "SPMC";
                break;
            case ABT_POOL_ACCESS_MPMC:
                access = "MPMC";
                break;
            default:
                access = "UNKNOWN";
                break;
        }

        fprintf(p_os,
                "%*s== POOL (%p) ==\n"
                "%*sid            : %" PRIu64 "\n"
                "%*saccess        : %s\n"
                "%*sautomatic     : %s\n"
                "%*snum_scheds    : %d\n"
                "%*sis_empty      : %s\n"
                "%*ssize          : %zu\n"
                "%*snum_blocked   : %d\n"
                "%*sdata          : %p\n",
                indent, "", (void *)p_pool, indent, "", p_pool->id, indent, "",
                access, indent, "",
                (p_pool->automatic == ABT_TRUE) ? "TRUE" : "FALSE", indent, "",
                ABTD_atomic_acquire_load_int32(&p_pool->num_scheds), indent, "",
                (ABTI_pool_is_empty(p_pool) ? "TRUE" : "FALSE"), indent, "",
                (p_pool->optional_def.p_get_size ? ABTI_pool_get_size(p_pool)
                                                 : 0),
                indent, "",
                ABTD_atomic_acquire_load_int32(&p_pool->num_blocked), indent,
                "", p_pool->data);
    }
    fflush(p_os);
}

static ABTD_atomic_uint64 g_pool_id = ABTD_ATOMIC_UINT64_STATIC_INITIALIZER(0);
void ABTI_pool_reset_id(void)
{
    ABTD_atomic_release_store_uint64(&g_pool_id, 0);
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static ABT_unit pool_create_unit_wrapper(ABT_pool pool, ABT_thread thread)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    return p_pool->old_def.u_create_from_thread(thread);
}

static void pool_free_unit_wrapper(ABT_pool pool, ABT_unit unit)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    p_pool->old_def.u_free(&unit);
}

static ABT_bool pool_is_empty_wrapper(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    size_t size = p_pool->old_def.p_get_size(pool);
    return (size == 0) ? ABT_TRUE : ABT_FALSE;
}

static ABT_thread pool_pop_wrapper(ABT_pool pool, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABT_unit unit = p_pool->old_def.p_pop(pool);
    if (unit != ABT_UNIT_NULL) {
        ABTI_global *p_global = ABTI_global_get_global();
        ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
        return ABTI_thread_get_handle(p_thread);
    } else {
        return ABT_THREAD_NULL;
    }
}

static void pool_push_wrapper(ABT_pool pool, ABT_unit unit,
                              ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    p_pool->old_def.p_push(pool, unit);
}

static int pool_init_wrapper(ABT_pool pool, ABT_pool_config config)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    return p_pool->old_def.p_init(pool, config);
}

static void pool_free_wrapper(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    p_pool->old_def.p_free(pool);
}

static size_t pool_get_size_wrapper(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    return p_pool->old_def.p_get_size(pool);
}

static ABT_thread pool_pop_wait_wrapper(ABT_pool pool, double time_secs,
                                        ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABT_unit unit = p_pool->old_def.p_pop_wait(pool, time_secs);
    if (unit != ABT_UNIT_NULL) {
        ABTI_global *p_global = ABTI_global_get_global();
        ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
        return ABTI_thread_get_handle(p_thread);
    } else {
        return ABT_THREAD_NULL;
    }
}

static void pool_pop_many_wrapper(ABT_pool pool, ABT_thread *threads,
                                  size_t max_threads, size_t *num_popped,
                                  ABT_pool_context context)
{
    (void)context;
    size_t i;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    for (i = 0; i < max_threads; i++) {
        ABT_unit unit = p_pool->old_def.p_pop(pool);
        if (unit != ABT_UNIT_NULL) {
            ABTI_global *p_global = ABTI_global_get_global();
            ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
            threads[i] = ABTI_thread_get_handle(p_thread);
        } else {
            break;
        }
    }
    *num_popped = i;
}

static void pool_push_many_wrapper(ABT_pool pool, const ABT_unit *units,
                                   size_t num_units, ABT_pool_context context)
{
    (void)context;
    size_t i;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    for (i = 0; i < num_units; i++) {
        p_pool->old_def.p_push(pool, units[i]);
    }
}

static void pool_print_all_wrapper(ABT_pool pool, void *arg,
                                   void (*print_f)(void *, ABT_thread))
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    pool_print_unit_to_thread_arg_t wrapper_arg = { arg, print_f };
    p_pool->old_def.p_print_all(pool, (void *)&wrapper_arg,
                                pool_print_unit_to_thread);
}

static void
pool_create_def_from_old_def(const ABT_pool_def *p_def,
                             ABTI_pool_old_def *p_old_def,
                             ABTI_pool_required_def *p_required_def,
                             ABTI_pool_optional_def *p_optional_def,
                             ABTI_pool_deprecated_def *p_deprecated_def)
{
    /* Create p_old_def*/
    p_old_def->u_create_from_thread = p_def->u_create_from_thread;
    p_old_def->u_free = p_def->u_free;
    p_old_def->p_init = p_def->p_init;
    p_old_def->p_get_size = p_def->p_get_size;
    p_old_def->p_push = p_def->p_push;
    p_old_def->p_pop = p_def->p_pop;
#ifdef ABT_CONFIG_ENABLE_VER_20_API
    p_old_def->p_pop_wait = p_def->p_pop_wait;
#else
    p_old_def->p_pop_wait = NULL;
#endif
    p_old_def->p_free = p_def->p_free;
    p_old_def->p_print_all = p_def->p_print_all;

    /* Set up p_required_def */
    p_required_def->p_create_unit = pool_create_unit_wrapper;
    p_required_def->p_free_unit = pool_free_unit_wrapper;
    p_required_def->p_is_empty = pool_is_empty_wrapper;
    p_required_def->p_pop = pool_pop_wrapper;
    p_required_def->p_push = pool_push_wrapper;

    /* Set up p_optional_def */
    /* Must be created from ABT_pool_def */
    p_optional_def->p_get_size = pool_get_size_wrapper;
    p_optional_def->p_pop_many = pool_pop_many_wrapper;
    p_optional_def->p_push_many = pool_push_many_wrapper;
    /* Optional */
    p_optional_def->p_init = p_old_def->p_init ? pool_init_wrapper : NULL;
    p_optional_def->p_free = p_old_def->p_free ? pool_free_wrapper : NULL;
    p_optional_def->p_pop_wait =
        p_old_def->p_pop_wait ? pool_pop_wait_wrapper : NULL;
    p_optional_def->p_print_all =
        p_old_def->p_print_all ? pool_print_all_wrapper : NULL;

    /* Set up p_deprecated_def */
    p_deprecated_def->u_is_in_pool = p_def->u_is_in_pool;
    p_deprecated_def->p_pop_timedwait = p_def->p_pop_timedwait;
    p_deprecated_def->p_remove = p_def->p_remove;
}

static inline uint64_t pool_get_new_id(void);
ABTU_ret_err static int
pool_create(ABT_pool_access access,
            const ABTI_pool_required_def *p_required_def,
            const ABTI_pool_optional_def *p_optional_def,
            const ABTI_pool_deprecated_def *p_deprecated_def,
            const ABTI_pool_old_def *p_old_def, ABTI_pool_config *p_config,
            ABT_bool def_automatic, ABT_bool is_builtin, ABTI_pool **pp_newpool)
{
    int abt_errno;
    ABTI_pool *p_pool;
    abt_errno = ABTU_malloc(sizeof(ABTI_pool), (void **)&p_pool);
    ABTI_CHECK_ERROR(abt_errno);

    /* Read the config and set the configured parameter */
    ABT_bool automatic = def_automatic;
    if (p_config) {
        int automatic_val = 0;
        abt_errno =
            ABTI_pool_config_read(p_config, ABT_pool_config_automatic.key,
                                  &automatic_val);
        if (abt_errno == ABT_SUCCESS) {
            automatic = (automatic_val == 0) ? ABT_FALSE : ABT_TRUE;
        }
    }

    p_pool->access = access;
    p_pool->automatic = automatic;
    p_pool->is_builtin = is_builtin;
    ABTD_atomic_release_store_int32(&p_pool->num_scheds, 0);
    ABTD_atomic_release_store_int32(&p_pool->num_blocked, 0);
    p_pool->data = NULL;
    memcpy(&p_pool->required_def, p_required_def,
           sizeof(ABTI_pool_required_def));
    if (p_optional_def) {
        memcpy(&p_pool->optional_def, p_optional_def,
               sizeof(ABTI_pool_optional_def));
    } else {
        memset(&p_pool->optional_def, 0, sizeof(ABTI_pool_optional_def));
    }
    if (p_deprecated_def) {
        memcpy(&p_pool->deprecated_def, p_deprecated_def,
               sizeof(ABTI_pool_deprecated_def));
    } else {
        memset(&p_pool->deprecated_def, 0, sizeof(ABTI_pool_deprecated_def));
    }
    if (p_old_def) {
        memcpy(&p_pool->old_def, p_old_def, sizeof(ABTI_pool_old_def));
    } else {
        memset(&p_pool->old_def, 0, sizeof(ABTI_pool_old_def));
    }
    p_pool->id = pool_get_new_id();

    /* Configure the pool */
    if (p_pool->optional_def.p_init) {
        ABT_pool_config config = ABTI_pool_config_get_handle(p_config);
        abt_errno =
            p_pool->optional_def.p_init(ABTI_pool_get_handle(p_pool), config);
        if (abt_errno != ABT_SUCCESS) {
            ABTU_free(p_pool);
            return abt_errno;
        }
    }
    *pp_newpool = p_pool;
    return ABT_SUCCESS;
}

static inline uint64_t pool_get_new_id(void)
{
    return (uint64_t)ABTD_atomic_fetch_add_uint64(&g_pool_id, 1);
}

static inline int pool_pop_thread_ex(ABT_pool pool, ABT_thread *thread,
                                     ABT_pool_context pool_ctx)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    *thread = ABTI_pool_pop(p_pool, pool_ctx);
    return ABT_SUCCESS;
}

static inline int pool_pop_threads_ex(ABT_pool pool, ABT_thread *threads,
                                      size_t len, size_t *num,
                                      ABT_pool_context pool_ctx)
{
    ABTI_STATIC_ASSERT(sizeof(ABT_unit) == sizeof(ABT_thread));
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(threads || len == 0);
    ABTI_UB_ASSERT(num);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_pop_many, ABT_ERR_POOL);

    if (len > 0) {
        ABTI_pool_pop_many(p_pool, threads, len, num, pool_ctx);
    }
    return ABT_SUCCESS;
}

static inline int pool_push_thread_ex(ABT_pool pool, ABT_thread thread,
                                      ABT_pool_context pool_ctx)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);

    if (p_thread) {
        ABTI_global *p_global = ABTI_global_get_global();
        int abt_errno =
            ABTI_thread_set_associated_pool(p_global, p_thread, p_pool);
        ABTI_CHECK_ERROR(abt_errno);
        ABTI_pool_push(p_pool, p_thread->unit, pool_ctx);
    }
    return ABT_SUCCESS;
}

static inline int pool_push_threads_ex(ABT_pool pool, const ABT_thread *threads,
                                       size_t num, ABT_pool_context pool_ctx)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(threads || num == 0);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_push_many, ABT_ERR_POOL);

    if (num > 0) {
        ABTI_global *p_global = ABTI_global_get_global();
        int abt_errno;
        ABT_unit *push_units, push_units_buffer[64];
        if (num > sizeof(push_units_buffer) / sizeof(push_units_buffer[0])) {
            abt_errno =
                ABTU_malloc(sizeof(ABT_unit) * num, (void **)&push_units);
            ABTI_CHECK_ERROR(abt_errno);
        } else {
            push_units = push_units_buffer;
        }

        size_t i, num_units = 0;
        for (i = 0; i < num; i++) {
            /* FIXME: the following can break the intermediate mapping if an
             * error happens. */
            ABTI_thread *p_thread = ABTI_thread_get_ptr(threads[i]);
            if (p_thread) {
                abt_errno =
                    ABTI_thread_set_associated_pool(p_global, p_thread, p_pool);
                if (abt_errno != ABT_SUCCESS) {
                    if (push_units != push_units_buffer)
                        ABTU_free(push_units);
                    ABTI_HANDLE_ERROR(abt_errno);
                }
                push_units[num_units++] = p_thread->unit;
            }
        }
        if (num_units > 0) {
            ABTI_pool_push_many(p_pool, push_units, num_units, pool_ctx);
        }
        if (push_units != push_units_buffer)
            ABTU_free(push_units);
    }
    return ABT_SUCCESS;
}

static inline int pool_pop_wait_thread_ex(ABT_pool pool, ABT_thread *thread,
                                          double time_secs,
                                          ABT_pool_context pool_ctx)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_pool->optional_def.p_pop_wait, ABT_ERR_POOL);

    *thread = ABTI_pool_pop_wait(p_pool, time_secs, pool_ctx);
    return ABT_SUCCESS;
}

static void pool_print_thread_to_unit(void *arg, ABT_thread thread)
{
    pool_print_thread_to_unit_arg_t *p_arg =
        (pool_print_thread_to_unit_arg_t *)arg;
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    p_arg->print_fn(p_arg->arg, p_thread->unit);
}

static void pool_print_unit_to_thread(void *arg, ABT_unit unit)
{
    pool_print_unit_to_thread_arg_t *p_arg =
        (pool_print_unit_to_thread_arg_t *)arg;
    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
    ABT_thread thread = ABTI_thread_get_handle(p_thread);
    p_arg->print_fn(p_arg->arg, thread);
}
