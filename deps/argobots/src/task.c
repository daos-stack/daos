/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTU_ret_err static int task_create(ABTI_global *p_global, ABTI_local *p_local,
                                    ABTI_pool *p_pool,
                                    void (*task_func)(void *), void *arg,
                                    ABTI_sched *p_sched, int refcount,
                                    ABTI_thread **pp_newtask);

/** @defgroup TASK Tasklet
 * This group is for Tasklet.  A tasklet is a work unit that cannot yield.
 */

/**
 * @ingroup TASK
 * @brief   Create a new tasklet.
 *
 * \c ABT_task_create() creates a new tasklet, associates it with the pool
 * \c pool, and returns its handle through \c newtask.  This routine pushes the
 * created tasklet to the pool \c pool.  The created tasklet calls
 * \c task_func() with \c arg when it is scheduled.
 *
 * If \c newtask is \c NULL, this routine creates an unnamed tasklet.  An
 * unnamed tasklet is automatically released on the completion of
 * \c task_func().  Otherwise, \c newtask must be explicitly freed by
 * \c ABT_thread_free().
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR_CONDITIONAL{\c newtask, \c ABT_TASK_NULL,
 *                                              \c newtask is not \c NULL}
 * @endchangev20
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
 * \DOC_UNDEFINED_NULL_PTR{\c task_func}
 *
 * @param[in]  pool       pool handle
 * @param[in]  task_func  function to be executed by a new tasklet
 * @param[in]  arg        argument for \c task_func()
 * @param[out] newtask    tasklet handle
 * @return Error code
 */
int ABT_task_create(ABT_pool pool, void (*task_func)(void *), void *arg,
                    ABT_task *newtask)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(task_func);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newtask to NULL on error. */
    if (newtask)
        *newtask = ABT_TASK_NULL;
#endif
    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_thread *p_newtask;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    int refcount = (newtask != NULL) ? 1 : 0;
    int abt_errno = task_create(p_global, p_local, p_pool, task_func, arg, NULL,
                                refcount, &p_newtask);
    ABTI_CHECK_ERROR(abt_errno);

    /* Return value */
    if (newtask)
        *newtask = ABTI_thread_get_handle(p_newtask);
    return ABT_SUCCESS;
}

/**
 * @ingroup TASK
 * @brief   Create a new tasklet associated with an execution stream.
 *
 * \c ABT_task_create_on_xstream() creates a new tasklet, associates it with the
 * first pool of the main scheduler of the execution stream \c xstream, and
 * returns its handle through \c newtask.  This routine pushes the created
 * tasklet to the pool \c pool.  The created tasklet calls \c task_func() with
 * \c arg when it is scheduled.
 *
 * If \c newtask is \c NULL, this routine creates an unnamed tasklet.  An
 * unnamed tasklet is automatically released on the completion of
 * \c task_func().  Otherwise, \c newtask must be explicitly freed by
 * \c ABT_thread_free().
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR_CONDITIONAL{\c newtask, \c ABT_TASK_NULL,
 *                                              \c newtask is not \c NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c task_func}
 *
 * @param[in]  xstream    execution stream handle
 * @param[in]  task_func  function to be executed by a new tasklet
 * @param[in]  arg        argument for \c task_func()
 * @param[out] newtask    tasklet handle
 * @return Error code
 */
int ABT_task_create_on_xstream(ABT_xstream xstream, void (*task_func)(void *),
                               void *arg, ABT_task *newtask)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(task_func);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newtask to NULL on error. */
    if (newtask)
        *newtask = ABT_TASK_NULL;
#endif
    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_thread *p_newtask;

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    /* TODO: need to consider the access type of target pool */
    ABTI_pool *p_pool = ABTI_xstream_get_main_pool(p_xstream);
    int refcount = (newtask != NULL) ? 1 : 0;
    int abt_errno = task_create(p_global, p_local, p_pool, task_func, arg, NULL,
                                refcount, &p_newtask);
    ABTI_CHECK_ERROR(abt_errno);

    /* Return value */
    if (newtask)
        *newtask = ABTI_thread_get_handle(p_newtask);
    return ABT_SUCCESS;
}

/**
 * @ingroup TASK
 * @brief   Revive a terminated work unit.
 *
 * The functionality of this routine is the same as \c ABT_thread_revive()
 * except for the error code.  \c ABT_task_revive() returns \c ABT_ERR_INV_TASK
 * in a case where \c ABT_thread_revive() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_revive()}
 */
int ABT_task_revive(ABT_pool pool, void (*task_func)(void *), void *arg,
                    ABT_task *task)
{
    int abt_errno = ABT_thread_revive(pool, task_func, arg, task);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Free a work unit.
 *
 * The functionality of this routine is the same as \c ABT_thread_free() except
 * for the value of \c task after this routine and the error code.
 * \c ABT_task_free() sets \c task to \c ABT_TASK_NULL instead of
 * \c ABT_THREAD_NULL.  \c ABT_task_free() returns \c ABT_ERR_INV_TASK in a case
 * where \c ABT_thread_free() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_free()}
 */
int ABT_task_free(ABT_task *task)
{
    int abt_errno = ABT_thread_free(task);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    if (!(ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS)) {
        *task = ABT_TASK_NULL;
    }
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Wait for a work unit to terminate.
 *
 * The functionality of this routine is the same as \c ABT_thread_join() except
 * for the error code.  \c ABT_task_join() returns \c ABT_ERR_INV_TASK in a case
 * where \c ABT_thread_join() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_join()}
 */
int ABT_task_join(ABT_task task)
{
    int abt_errno = ABT_thread_join(task);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Send a termination request to a work unit.
 *
 * The functionality of this routine is the same as \c ABT_thread_cancel()
 * except for the error code.  \c ABT_task_cancel() returns \c ABT_ERR_INV_TASK
 * in a case where \c ABT_thread_cancel() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_cancel()}
 */
int ABT_task_cancel(ABT_task task)
{
    int abt_errno = ABT_thread_cancel(task);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Get the calling work unit.
 *
 * \c ABT_task_self() returns the handle of the calling work unit through
 * \c task.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_thread()}
 *
 * @changev20
 * \DOC_DESC_V1X_NOYIELDABLE{\c ABT_ERR_INV_TASK}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c task, \c ABT_TASK_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_TASK \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_INV_TASK_Y
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c task}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] task  work unit handle
 * @return Error code
 */
int ABT_task_self(ABT_task *task)
{
    ABTI_UB_ASSERT(task);

    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *task = ABT_TASK_NULL;
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    ABTI_CHECK_TRUE(!(p_local_xstream->p_thread->type &
                      ABTI_THREAD_TYPE_YIELDABLE),
                    ABT_ERR_INV_TASK);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
#endif
    *task = ABTI_thread_get_handle(p_local_xstream->p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup TASK
 * @brief   Get ID of the calling work unit.
 *
 * \c ABT_task_self_id() returns the ID of the calling work unit through \c id.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_thread_id()}
 *
 * @changev20
 * \DOC_DESC_V1X_NOYIELDABLE{\c ABT_ERR_INV_TASK}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_TASK \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_INV_TASK_Y
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c id}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] id  ID of the calling work unit
 * @return Error code
 */
int ABT_task_self_id(ABT_unit_id *id)
{
    ABTI_UB_ASSERT(id);

    ABTI_xstream *p_local_xstream;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    ABTI_CHECK_TRUE(!(p_local_xstream->p_thread->type &
                      ABTI_THREAD_TYPE_YIELDABLE),
                    ABT_ERR_INV_TASK);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
#endif
    *id = ABTI_thread_get_id(p_local_xstream->p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup TASK
 * @brief   Get an execution stream associated with a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_get_last_xstream() except for the error code.
 * \c ABT_task_get_xstream() returns \c ABT_ERR_INV_TASK in a case where
 * \c ABT_thread_get_last_xstream() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_last_xstream()}
 */
int ABT_task_get_xstream(ABT_task task, ABT_xstream *xstream)
{
    int abt_errno = ABT_thread_get_last_xstream(task, xstream);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Get a state of a tasklet.
 *
 * \c ABT_task_get_state() returns the state of the tasklet \c task through
 * \c state.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_state()}
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TASK_HANDLE{\c thread}
 * \DOC_ERROR_INV_TASK_Y{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c state}
 *
 * @param[in]  task   tasklet handle
 * @param[out] state  state of \c task
 * @return Error code
 */
int ABT_task_get_state(ABT_task task, ABT_task_state *state)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(state);

    ABT_thread_state thread_state;
    int abt_errno = ABT_thread_get_state(task, &thread_state);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    if (thread_state == ABT_THREAD_STATE_READY) {
        *state = ABT_TASK_STATE_READY;
    } else if (thread_state == ABT_THREAD_STATE_TERMINATED) {
        *state = ABT_TASK_STATE_TERMINATED;
    } else {
        *state = ABT_TASK_STATE_RUNNING;
    }
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Get the last pool of a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_get_last_pool() except for the error code.
 * \c ABT_task_get_last_pool() returns \c ABT_ERR_INV_TASK in a case where
 * \c ABT_thread_get_last_pool() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_last_pool()}
 */
int ABT_task_get_last_pool(ABT_task task, ABT_pool *pool)
{
    int abt_errno = ABT_thread_get_last_pool(task, pool);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Get the last pool's ID of a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_get_last_pool_id() except for the error code.
 * \c ABT_task_get_last_pool_id() returns \c ABT_ERR_INV_TASK in a case where
 * \c ABT_thread_get_last_pool_id() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_last_pool_id()}
 */
int ABT_task_get_last_pool_id(ABT_task task, int *id)
{
    int abt_errno = ABT_thread_get_last_pool_id(task, id);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Set the migratability in a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_set_migratable() except for the error code.
 * \c ABT_task_set_migratable() returns \c ABT_ERR_INV_TASK in a case where
 * \c ABT_thread_set_migratable() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_set_migratable()}
 */
int ABT_task_set_migratable(ABT_task task, ABT_bool flag)
{
    int abt_errno = ABT_thread_set_migratable(task, flag);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Get the migratability of a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_is_migratable() except for the error code.
 * \c ABT_task_is_migratable() returns \c ABT_ERR_INV_TASK in a case where
 * \c ABT_thread_is_migratable() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_is_migratable()}
 */
int ABT_task_is_migratable(ABT_task task, ABT_bool *flag)
{
    int abt_errno = ABT_thread_is_migratable(task, flag);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

#ifdef ABT_CONFIG_USE_DOXYGEN
/**
 * @ingroup TASK
 * @brief   Check if a work unit is unnamed
 *
 * The functionality of this routine is the same as \c ABT_thread_is_unnamed().
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_is_unnamed()}
 */
int ABT_task_is_unnamed(ABT_task task, ABT_bool *flag);
#endif

/**
 * @ingroup TASK
 * @brief   Compare two work-unit handles for equality.
 *
 * The functionality of this routine is the same as \c ABT_thread_equal().
 */
int ABT_task_equal(ABT_task task1, ABT_task task2, ABT_bool *result)
{
    return ABT_thread_equal(task1, task2, result);
}

/**
 * @ingroup TASK
 * @brief   Get ID of a work unit
 *
 * The functionality of this routine is the same as \c ABT_thread_get_id()
 * except for the error code.  \c ABT_task_get_id() returns \c ABT_ERR_INV_TASK
 * in a case where \c ABT_thread_get_id() returns \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_id()}
 */
int ABT_task_get_id(ABT_task task, ABT_unit_id *task_id)
{
    int abt_errno = ABT_thread_get_id(task, task_id);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

/**
 * @ingroup TASK
 * @brief   Retrieve an argument for a work-unit function of a work unit.
 *
 * The functionality of this routine is the same as \c ABT_thread_get_arg()
 * except for the error code.  \c ABT_task_get_arg() returns
 * \c ABT_ERR_INV_TASK in a case where \c ABT_thread_get_arg() returns
 * \c ABT_ERR_INV_THREAD.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_arg()}
 */
int ABT_task_get_arg(ABT_task task, void **arg)
{
    int abt_errno = ABT_thread_get_arg(task, arg);
    ABTI_CHECK_TRUE(abt_errno != ABT_ERR_INV_THREAD, ABT_ERR_INV_TASK);
    return abt_errno;
}

#ifdef ABT_CONFIG_USE_DOXYGEN
/**
 * @ingroup TASK
 * @brief   Set a value with a work-unit-specific key in a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_set_specific().
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_set_specific()}
 */
int ABT_task_set_specific(ABT_task task, ABT_key key, void *value);
#endif

#ifdef ABT_CONFIG_USE_DOXYGEN
/**
 * @ingroup TASK
 * @brief   Get a value associated with a work-unit-specific key in a work unit.
 *
 * The functionality of this routine is the same as
 * \c ABT_thread_get_specific().
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_thread_get_specific()}
 */
int ABT_task_get_specific(ABT_task task, ABT_key key, void **value);
#endif

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

ABTU_ret_err static int task_create(ABTI_global *p_global, ABTI_local *p_local,
                                    ABTI_pool *p_pool,
                                    void (*task_func)(void *), void *arg,
                                    ABTI_sched *p_sched, int refcount,
                                    ABTI_thread **pp_newtask)
{
    ABTI_thread *p_newtask;

    /* Allocate a task object */
    int abt_errno = ABTI_mem_alloc_nythread(p_local, &p_newtask);
    ABTI_CHECK_ERROR(abt_errno);
    abt_errno = ABTI_thread_init_pool(p_global, p_newtask, p_pool);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        ABTI_mem_free_thread(p_global, p_local, p_newtask);
        ABTI_HANDLE_ERROR(abt_errno);
    }

    p_newtask->p_last_xstream = NULL;
    p_newtask->p_parent = NULL;
    ABTD_atomic_relaxed_store_int(&p_newtask->state, ABT_THREAD_STATE_READY);
    ABTD_atomic_relaxed_store_uint32(&p_newtask->request, 0);
    p_newtask->f_thread = task_func;
    p_newtask->p_arg = arg;
    ABTD_atomic_relaxed_store_ptr(&p_newtask->p_keytable, NULL);
    p_newtask->id = ABTI_TASK_INIT_ID;

    /* Create a wrapper work unit */
    ABTI_thread_type thread_type =
        refcount ? (ABTI_THREAD_TYPE_THREAD | ABTI_THREAD_TYPE_NAMED)
                 : ABTI_THREAD_TYPE_THREAD;
#ifndef ABT_CONFIG_DISABLE_MIGRATION
    thread_type |= ABTI_THREAD_TYPE_MIGRATABLE;
#endif
    p_newtask->type |= thread_type;

    ABTI_event_thread_create(p_local, p_newtask,
                             ABTI_local_get_xstream_or_null(p_local)
                                 ? ABTI_local_get_xstream(p_local)->p_thread
                                 : NULL,
                             p_pool);

    /* Add this task to the scheduler's pool */
    ABTI_pool_push(p_pool, p_newtask->unit, ABT_POOL_CONTEXT_OP_THREAD_CREATE);

    /* Return value */
    *pp_newtask = p_newtask;

    return ABT_SUCCESS;
}
