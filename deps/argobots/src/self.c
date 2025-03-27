/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup SELF Self
 * This group is for the self wok unit.
 */

/**
 * @ingroup SELF
 * @brief   Get an execution stream that is running the calling work unit.
 *
 * \c ABT_self_get_xstream() returns the handle of the execution stream that is
 * running the calling work unit through \c xstream.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c xstream}
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[out] xstream  execution stream handle
 * @return Error code
 */
int ABT_self_get_xstream(ABT_xstream *xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(xstream);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    /* Return value */
    *xstream = ABTI_xstream_get_handle(p_local_xstream);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Return a rank of an execution stream that is running the calling
 *          work unit.
 *
 * \c ABT_self_get_xstream_rank() returns the rank of the execution stream that
 * is running the calling work unit through \c rank.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c rank}
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[out] rank  execution stream rank
 * @return Error code
 */
int ABT_self_get_xstream_rank(int *rank)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(rank);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    /* Return value */
    *rank = (int)p_local_xstream->rank;
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Get the calling work unit.
 *
 * \c ABT_self_get_thread() returns the handle of the calling work unit through
 * \c thread.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[out] thread  work unit handle
 * @return Error code
 */
int ABT_self_get_thread(ABT_thread *thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    *thread = ABTI_thread_get_handle(p_local_xstream->p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Get ID of the calling work unit.
 *
 * \c ABT_self_get_thread_id() returns the ID of the calling work unit through
 * \c id.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c id}
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[out] id  ID of the calling work unit
 * @return Error code
 */
int ABT_self_get_thread_id(ABT_unit_id *id)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(id);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    *id = ABTI_thread_get_id(p_local_xstream->p_thread);
    return ABT_SUCCESS;
}

#ifdef ABT_CONFIG_USE_DOXYGEN
/**
 * @ingroup SELF
 * @brief   Get the calling work unit.
 *
 * The functionality of this routine is the same as \c ABT_self_get_thread().
 */
int ABT_self_get_task(ABT_thread *thread);
#endif

#ifdef ABT_CONFIG_USE_DOXYGEN
/**
 * @ingroup SELF
 * @brief   Get ID of the calling work unit.
 *
 * The functionality of this routine is the same as \c ABT_self_get_thread_id().
 */
int ABT_self_get_task_id(ABT_unit_id *id);
#endif

/**
 * @ingroup SELF
 * @brief   Associate a value with a work-unit-specific data key in the calling
 *          work unit.
 *
 * \c ABT_self_set_specific() associates a value \c value with the
 * work-unit-specific data key \c key in the calling work unit.  Different work
 * units may bind different values to the same key.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_KEY
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_KEY_HANDLE{\c key}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] key    work-unit-specific data key handle
 * @param[in] value  value associated with \c key
 * @return Error code
 */
int ABT_self_set_specific(ABT_key key, void *value)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_key *p_key = ABTI_key_get_ptr(key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    /* Obtain the key-value table pointer. */
    int abt_errno =
        ABTI_ktable_set(p_global, ABTI_xstream_get_local(p_local_xstream),
                        &p_local_xstream->p_thread->p_keytable, p_key, value);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Get a value associated with a work-unit-specific data key in the
 *          calling work unit.
 *
 * \c ABT_self_get_specific() returns the value in the caller associated with
 * the work-unit-specific data key \c key in the calling work unit through
 * \c value.  If the caller has never set a value for \c key, this routine sets
 * \c value to \c NULL.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_KEY
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_KEY_HANDLE{\c key}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c value}
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in]  key    work-unit-specific data key handle
 * @param[out] value  value associated with \c key
 * @return Error code
 */
int ABT_self_get_specific(ABT_key key, void **value)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(value);

    ABTI_key *p_key = ABTI_key_get_ptr(key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);

    /* We don't allow an external thread to call this routine. */
    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    /* Obtain the key-value table pointer */
    *value = ABTI_ktable_get(&p_local_xstream->p_thread->p_keytable, p_key);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Obtain a type of the caller.
 *
 * \c ABT_self_get_type() returns a type of the calling work unit through
 * \c type.  If the caller is a ULT, \c type is set to \c ABT_UNIT_TYPE_THREAD.
 * If the caller is a tasklet, \c type is set to \c ABT_UNIT_TYPE_TASK.
 * Otherwise (i.e., if the caller is an external thread), \c type is set to
 * \c ABT_UNIT_TYPE_EXT.
 *
 * @changev20
 * \DOC_DESC_V1X_NOEXT{\c ABT_ERR_INV_XSTREAM}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c type, \c ABT_UNIT_TYPE_EXT}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 * \DOC_V1X \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c type}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] type  work unit type
 * @return Error code
 */
int ABT_self_get_type(ABT_unit_type *type)
{
    ABTI_UB_ASSERT(type);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* By default, type is ABT_UNIT_TYPE_EXT in Argobots 1.x */
    *type = ABT_UNIT_TYPE_EXT;
    ABTI_SETUP_GLOBAL(NULL);
    /* Since ABT_ERR_INV_XSTREAM is a valid return, this should not be handled
     * by ABTI_SETUP_LOCAL_XSTREAM, which warns the user when --enable-debug=err
     * is set. */
    ABTI_xstream *p_local_xstream =
        ABTI_local_get_xstream_or_null(ABTI_local_get_local());
    if (!p_local_xstream) {
        return ABT_ERR_INV_XSTREAM;
    }
    *type = ABTI_thread_type_get_type(p_local_xstream->p_thread->type);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_xstream *p_local_xstream =
        ABTI_local_get_xstream_or_null(ABTI_local_get_local());
    if (p_local_xstream) {
        *type = ABTI_thread_type_get_type(p_local_xstream->p_thread->type);
    } else {
        *type = ABT_UNIT_TYPE_EXT;
    }
#endif
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Check if the caller is the primary ULT.
 *
 * \c ABT_self_is_primary() checks whether the caller is the primary ULT and
 * returns the result through \c is_primary.  If the caller is the primary ULT,
 * \c is_primary is set to \c ABT_TRUE.  Otherwise, \c is_primary is set to
 * \c ABT_FALSE.
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_INV_THREAD}
 *
 * \DOC_DESC_V1X_NOEXT{\c ABT_ERR_INV_XSTREAM}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c is_primary, \c ABT_FALSE}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 * \DOC_V1X \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_INV_THREAD_NY
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c is_primary}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] is_primary  result (\c ABT_TRUE: primary ULT, \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_self_is_primary(ABT_bool *is_primary)
{
    ABTI_UB_ASSERT(is_primary);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *is_primary = ABT_FALSE;
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_ythread *p_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(NULL, &p_ythread);
    *is_primary = (p_ythread->thread.type & ABTI_THREAD_TYPE_PRIMARY)
                      ? ABT_TRUE
                      : ABT_FALSE;
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_xstream *p_local_xstream =
        ABTI_local_get_xstream_or_null(ABTI_local_get_local());
    if (p_local_xstream) {
        *is_primary =
            (p_local_xstream->p_thread->type & ABTI_THREAD_TYPE_PRIMARY)
                ? ABT_TRUE
                : ABT_FALSE;
    } else {
        *is_primary = ABT_FALSE;
    }
#endif
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Check if the caller is running on the primary execution stream.
 *
 * \c ABT_self_on_primary_xstream() checks whether the caller is running on the
 * primary execution stream and returns the result through \c on_primary.  If
 * the caller is a work unit running on the primary execution stream,
 * \c on_primary is set to \c ABT_TRUE.  Otherwise, \c on_primary is set to
 * \c ABT_FALSE.
 *
 * @changev20
 * \DOC_DESC_V1X_NOEXT{\c ABT_ERR_INV_XSTREAM}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c on_primary, \c ABT_FALSE}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 * \DOC_V1X \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c on_primary}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] on_primary  result (\c ABT_TRUE: primary execution stream,
 *                                 \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_self_on_primary_xstream(ABT_bool *on_primary)
{
    ABTI_UB_ASSERT(on_primary);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *on_primary = ABT_FALSE;
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    *on_primary = (p_local_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY)
                      ? ABT_TRUE
                      : ABT_FALSE;
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_xstream *p_local_xstream =
        ABTI_local_get_xstream_or_null(ABTI_local_get_local());
    if (p_local_xstream) {
        *on_primary = (p_local_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY)
                          ? ABT_TRUE
                          : ABT_FALSE;
    } else {
        *on_primary = ABT_FALSE;
    }
#endif
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Get the last pool of the calling work unit.
 *
 * \c ABT_self_get_last_pool() returns the last pool associated with the calling
 * work unit through \c pool.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c pool}
 *
 * @param[out] pool  pool handle
 * @return  Error code
 */
int ABT_self_get_last_pool(ABT_pool *pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pool);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    ABTI_thread *p_self = p_local_xstream->p_thread;
    ABTI_ASSERT(p_self->p_pool);
    *pool = ABTI_pool_get_handle(p_self->p_pool);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Get ID of the last pool of the calling work unit.
 *
 * \c ABT_self_get_last_pool_id() returns the last pool's ID of the calling work
 * unit through \c pool_id.
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c pool_id, -1}
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
 * \DOC_UNDEFINED_NULL_PTR{\c pool_id}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] pool_id  pool ID
 * @return  Error code
 */
int ABT_self_get_last_pool_id(int *pool_id)
{
    ABTI_UB_ASSERT(pool_id);

    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *pool_id = -1;
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    ABTI_thread *p_self = p_local_xstream->p_thread;
    ABTI_ASSERT(p_self->p_pool);
    *pool_id = p_self->p_pool->id;
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    ABTI_thread *p_self = p_local_xstream->p_thread;
    ABTI_ASSERT(p_self->p_pool);
    *pool_id = p_self->p_pool->id;
#endif
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Set an associated pool for the calling work unit.
 *
 * \c ABT_self_set_associated_pool() changes the associated pool of the work
 * unit \c thread to the pool \c pool.  This routine does not yield the calling
 * work unit.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 *
 * @param[in] pool  pool handle
 * @return  Error code
 */
int ABT_self_set_associated_pool(ABT_pool pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_thread *p_self = p_local_xstream->p_thread;

    int abt_errno = ABTI_thread_set_associated_pool(p_global, p_self, p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Get a unit handle of the calling work unit.
 *
 * \c ABT_self_get_unit() returns the \c ABT_unit handle associated with the
 * calling work unit through \c unit.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c unit}
 *
 * @param[out] unit  work unit handle
 * @return  Error code
 */
int ABT_self_get_unit(ABT_unit *unit)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(unit);

    /* We don't allow an external thread to call this routine. */
    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    *unit = p_local_xstream->p_thread->unit;
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Yield the calling ULT to its parent ULT
 *
 * \c ABT_self_yield() yields the calling ULT and pushes the calling ULT to its
 * associated pool.  Its parent ULT will be resumed.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 *
 * @return Error code
 */
int ABT_self_yield(void)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_ythread);

    ABTI_ythread_yield(&p_local_xstream, p_ythread,
                       ABTI_YTHREAD_YIELD_KIND_USER, ABT_SYNC_EVENT_TYPE_USER,
                       NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Yield the calling ULT to another ULT.
 *
 * \c ABT_self_yield_to() yields the calling ULT and schedules the ULT
 * \c thread as a child thread of the calling ULT's parent.  The calling ULT is
 * pushed to its associated pool.  It is the user's responsibility to pop
 * \c thread from its associated pool before calling this routine.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_THREAD_CALLER{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_IN_POOL{\c thread,
 *                                  the pool associated with \c thread}
 * \DOC_UNDEFINED_WORK_UNIT_NOT_READY{\c thread}
 *
 * @param[in] thread  handle to the target thread
 * @return Error code
 */
int ABT_self_yield_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(p_cur_ythread != p_tar_ythread, ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_tar_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);

    /* Switch the context */
    ABTI_ythread_yield_to(&p_local_xstream, p_cur_ythread, p_tar_ythread,
                          ABTI_YTHREAD_YIELD_TO_KIND_USER,
                          ABT_SYNC_EVENT_TYPE_USER, NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Yield the calling ULT to another suspended ULT.
 *
 * \c ABT_self_resume_yield_to() yields the calling ULT, resumes the ULT
 * \c thread, and schedules \c thread as a child thread of the calling ULT's
 * parent.  The calling ULT is pushed to its associated pool.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_UNSUSPENDED{\c thread}
 *
 * @param[in] thread  handle to the target ULT
 * @return Error code
 */
int ABT_self_resume_yield_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_tar_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_UB_ASSERT(p_cur_ythread != p_tar_ythread);
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_tar_ythread->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);

    /* Switch the context */
    ABTI_ythread_resume_yield_to(&p_local_xstream, p_cur_ythread, p_tar_ythread,
                                 ABTI_YTHREAD_RESUME_YIELD_TO_KIND_USER,
                                 ABT_SYNC_EVENT_TYPE_USER, NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Suspend the calling ULT.
 *
 * \c ABT_self_suspend() suspends the execution of the calling ULT and switches
 * to its parent ULT.  The calling ULT is not pushed to its associated pool and
 * its state becomes blocked.  \c ABT_thread_resume() awakens the suspended ULT
 * and pushes it back to its associated pool.
 *
 * @changev11
 * \DOC_DESC_V10_ERROR_CODE_CHANGE{\c ABT_ERR_INV_THREAD,
 *                                 \c ABT_ERR_INV_XSTREAM,
 *                                 this routine is called by an external thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 *
 * @return Error code
 */
int ABT_self_suspend(void)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_self;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_self);

    ABTI_ythread_suspend(&p_local_xstream, p_self, ABT_SYNC_EVENT_TYPE_USER,
                         NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Suspend the calling ULT and jump to another ULT.
 *
 * \c ABT_self_suspend_to() suspends the execution of the calling ULT and
 * schedules the ULT \c thread as a child thread of the calling ULT's parent.
 * The calling ULT is not pushed to its associated pool and its state becomes
 * blocked.  \c ABT_thread_resume() awakens the suspended ULT and pushes it back
 * to its associated pool.  It is the user's responsibility to pop \c thread
 * from its associated pool before calling this routine.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_THREAD_CALLER{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_IN_POOL{\c thread,
 *                                  the pool associated with \c thread}
 * \DOC_UNDEFINED_WORK_UNIT_NOT_READY{\c thread}
 *
 * @param[in] thread  handle to the target thread
 * @return Error code
 */
int ABT_self_suspend_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(p_cur_ythread != p_tar_ythread, ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_tar_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);

    /* Switch the context */
    ABTI_ythread_suspend_to(&p_local_xstream, p_cur_ythread, p_tar_ythread,
                            ABT_SYNC_EVENT_TYPE_USER, NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Suspend the calling ULT and jump to another suspended ULT.
 *
 * \c ABT_self_resume_suspend_to() suspends the execution of the calling ULT,
 * resumes the ULT \c thread, and schedules \c thread as a child thread of the
 * calling ULT's parent.  The calling ULT is not pushed to its associated pool
 * and its state becomes blocked.  \c ABT_thread_resume() awakens the suspended
 * ULT and pushes it back to its associated pool.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_UNSUSPENDED{\c thread}
 *
 * @param[in] thread  handle to the target ULT
 * @return Error code
 */
int ABT_self_resume_suspend_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_tar_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_UB_ASSERT(p_cur_ythread != p_tar_ythread);
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_tar_ythread->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);

    /* Switch the context */
    ABTI_ythread_resume_suspend_to(&p_local_xstream, p_cur_ythread,
                                   p_tar_ythread, ABT_SYNC_EVENT_TYPE_USER,
                                   NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Terminate a calling ULT.
 *
 * \c ABT_self_exit() terminates the calling ULT.  This routine does not return
 * if it succeeds.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{the caller}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @return Error code
 */
int ABT_self_exit(void)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_ythread);
    ABTI_CHECK_TRUE(!(p_ythread->thread.type & ABTI_THREAD_TYPE_PRIMARY),
                    ABT_ERR_INV_THREAD);

    ABTI_ythread_exit(p_local_xstream, p_ythread);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Terminate the calling ULT and jump to another ULT.
 *
 * \c ABT_self_exit_to() terminates the calling ULT and schedules the ULT
 * \c thread as a child thread of the calling ULT's parent.   This routine does
 * not return if it succeeds.  It is the user's responsibility to pop \c thread
 * from its associated pool before calling this routine.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{the caller}
 * \DOC_ERROR_INV_THREAD_CALLER{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_IN_POOL{\c thread,
 *                                  the pool associated with \c thread}
 * \DOC_UNDEFINED_WORK_UNIT_NOT_READY{\c thread}
 *
 * @param[in] thread  handle to the target thread
 * @return Error code
 */
int ABT_self_exit_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(p_cur_ythread != p_tar_ythread, ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_tar_ythread->thread.type &
                      (ABTI_THREAD_TYPE_MAIN_SCHED | ABTI_THREAD_TYPE_PRIMARY)),
                    ABT_ERR_INV_THREAD);

    /* Switch the context */
    ABTI_ythread_exit_to(p_local_xstream, p_cur_ythread, p_tar_ythread);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Terminate the calling ULT and jump to another suspended ULT.
 *
 * \c ABT_self_resume_exit_to() terminates the calling ULT, resumes the ULT
 * \c thread, and schedules \c thread as a child thread of the calling ULT's
 * parent.  This routine does not return if it succeeds.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{the caller}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_UNSUSPENDED{\c thread}
 *
 * @param[in] thread  handle to the target ULT
 * @return Error code
 */
int ABT_self_resume_exit_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type &
                      (ABTI_THREAD_TYPE_PRIMARY | ABTI_THREAD_TYPE_MAIN_SCHED)),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_tar_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_UB_ASSERT(p_cur_ythread != p_tar_ythread);
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_tar_ythread->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);

    /* Switch the context */
    ABTI_ythread_resume_exit_to(p_local_xstream, p_cur_ythread, p_tar_ythread);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Execute a work unit on the calling ULT
 *
 * \c ABT_self_schedule() runs the work unit \c thread as a child work unit on
 * the calling ULT, which becomes a parent ULT.  If \c pool is not
 * \c ABT_POOL_NULL, this routine associates \c thread with the pool \c pool
 * before \c thread is scheduled.  The calling ULT will be resumed when a
 * child work unit finishes or yields.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WORK_UNIT_NOT_READY{\c thread}
 *
 * @param[in] thread  work unit handle
 * @param[in] pool    pool handle
 * @return Error code
 */
int ABT_self_schedule(ABT_thread thread, ABT_pool pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, NULL);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    if (p_pool) {
        int abt_errno =
            ABTI_thread_set_associated_pool(p_global, p_thread, p_pool);
        ABTI_CHECK_ERROR(abt_errno);
    }
    ABTI_ythread_schedule(p_global, &p_local_xstream, p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Set an argument for a work-unit function of the calling work unit
 *
 * \c ABT_self_set_arg() sets the argument \c arg for the caller's work-unit
 * function.
 *
 * @note
 * The new argument will be used if the calling work unit is revived.
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
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[in] arg  argument a work-unit function of the calling work unit
 * @return Error code
 */
int ABT_self_set_arg(void *arg)
{
    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    p_local_xstream->p_thread->p_arg = arg;
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Retrieve an argument for a work-unit function of the calling work
 *          unit
 *
 * \c ABT_self_get_arg() returns the argument that is passed to the caller's
 * work-unit function through \c arg.
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c arg, \c NULL}
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
 * \DOC_UNDEFINED_NULL_PTR{\c arg}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] arg  argument for the caller's function
 * @return Error code
 */
int ABT_self_get_arg(void **arg)
{
    ABTI_UB_ASSERT(arg);

    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *arg = NULL;
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    *arg = p_local_xstream->p_thread->p_arg;
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Retrieve a work-unit function of the calling work unit
 *
 * \c ABT_self_get_thread_func() returns the work-unit function of the calling
 * work unit through \c thread_func.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 *
 * @param[out] thread_func  the caller's function
 * @return Error code
 */
int ABT_self_get_thread_func(void (**thread_func)(void *))
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    *thread_func = p_local_xstream->p_thread->f_thread;
    return ABT_SUCCESS;
}

/**
 * @ingroup SELF
 * @brief   Check if the calling work unit is unnamed
 *
 * \c ABT_self_is_unnamed() checks if the calling work unit is unnamed and
 * returns the result through \c is_unnamed.  \c is_unnamed is set to
 * \c ABT_TRUE if the calling work unit is unnamed.  Otherwise, \c is_unnamed is
 * set to \c ABT_FALSE.
 *
 * @contexts
 * \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_unnamed}
 *
 * @param[out] is_unnamed  result (\c ABT_TRUE: unnamed, \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_self_is_unnamed(ABT_bool *is_unnamed)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_unnamed);

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);

    *is_unnamed = (p_local_xstream->p_thread->type & ABTI_THREAD_TYPE_NAMED)
                      ? ABT_FALSE
                      : ABT_TRUE;
    return ABT_SUCCESS;
}
