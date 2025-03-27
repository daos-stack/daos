/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

typedef enum {
    THREAD_POOL_OP_NONE,
    THREAD_POOL_OP_PUSH,
    THREAD_POOL_OP_INIT,
} thread_pool_op_kind;
ABTU_ret_err static inline int
ythread_create(ABTI_global *p_global, ABTI_local *p_local, ABTI_pool *p_pool,
               void (*thread_func)(void *), void *arg, ABTI_thread_attr *p_attr,
               ABTI_thread_type thread_type, ABTI_sched *p_sched,
               thread_pool_op_kind pool_op, ABTI_ythread **pp_newthread);
ABTU_ret_err static inline int
thread_revive(ABTI_global *p_global, ABTI_local *p_local, ABTI_pool *p_pool,
              void (*thread_func)(void *), void *arg,
              thread_pool_op_kind pool_op, ABTI_thread *p_thread);
static inline void thread_join(ABTI_local **pp_local, ABTI_thread *p_thread);
static inline void thread_free(ABTI_global *p_global, ABTI_local *p_local,
                               ABTI_thread *p_thread, ABT_bool free_unit);
static void thread_root_func(void *arg);
static void thread_main_sched_func(void *arg);
#ifndef ABT_CONFIG_DISABLE_MIGRATION
ABTU_ret_err static int thread_migrate_to_pool(ABTI_global *p_global,
                                               ABTI_local *p_local,
                                               ABTI_thread *p_thread,
                                               ABTI_pool *p_pool);
#endif
static inline ABT_unit_id thread_get_new_id(void);

static void thread_key_destructor_stackable_sched(void *p_value);
static ABTI_key g_thread_sched_key =
    ABTI_KEY_STATIC_INITIALIZER(thread_key_destructor_stackable_sched,
                                ABTI_KEY_ID_STACKABLE_SCHED);
static void thread_key_destructor_migration(void *p_value);
static ABTI_key g_thread_mig_data_key =
    ABTI_KEY_STATIC_INITIALIZER(thread_key_destructor_migration,
                                ABTI_KEY_ID_MIGRATION);

/** @defgroup ULT User-level Thread (ULT)
 * This group is for User-level Thread (ULT).
 * A ULT is a work unit that can yield.
 */

/**
 * @ingroup ULT
 * @brief   Create a new ULT.
 *
 * \c ABT_thread_create() creates a new ULT, given by the attributes \c attr,
 * associates it with the pool \c pool, and returns its handle through
 * \c newthread.  This routine pushes the created ULT to \c pool.  The created
 * ULT calls \c thread_func() with \c arg when it is scheduled.
 *
 * \c attr can be created by \c ABT_thread_attr_create().  If the user passes
 * \c ABT_THREAD_ATTR_NULL for \c attr, the default ULT attribute is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_THREAD_ATTRIBUTE
 *
 * This routine copies \c attr, so the user can free \c attr after this routine
 * returns.
 *
 * If \c newthread is \c NULL, this routine creates an unnamed ULT.  An unnamed
 * ULT is automatically released on the completion of \c thread_func().
 * Otherwise, \c newthread must be explicitly freed by \c ABT_thread_free().
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR_CONDITIONAL{\c newthread,
 *                                              \c ABT_THREAD_NULL,
 *                                              \c newthread is not \c NULL}
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
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 *
 * @param[in]  pool         pool handle
 * @param[in]  thread_func  function to be executed by a new ULT
 * @param[in]  arg          argument for \c thread_func()
 * @param[in]  attr         ULT attribute
 * @param[out] newthread    ULT handle
 * @return Error code
 */
int ABT_thread_create(ABT_pool pool, void (*thread_func)(void *), void *arg,
                      ABT_thread_attr attr, ABT_thread *newthread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newthread to NULL on error. */
    if (newthread)
        *newthread = ABT_THREAD_NULL;
#endif
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_ythread *p_newthread;

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    ABTI_thread_type unit_type =
        (newthread != NULL)
            ? (ABTI_THREAD_TYPE_YIELDABLE | ABTI_THREAD_TYPE_NAMED)
            : ABTI_THREAD_TYPE_YIELDABLE;
    int abt_errno = ythread_create(p_global, p_local, p_pool, thread_func, arg,
                                   ABTI_thread_attr_get_ptr(attr), unit_type,
                                   NULL, THREAD_POOL_OP_PUSH, &p_newthread);
    ABTI_CHECK_ERROR(abt_errno);

    /* Return value */
    if (newthread)
        *newthread = ABTI_ythread_get_handle(p_newthread);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Create a new ULT and yield to it.
 *
 * \c ABT_thread_create_to() creates a new ULT, given by the attributes \c attr,
 * associates it with the pool \c pool, and returns its handle through
 * \c newthread.  Then, the calling ULT yields to the newly created ULT.  The
 * calling ULT is pushed to its associated pool.  The newly created ULT calls
 * \c thread_func() with \c arg.  If \c newthread is not NULL, \c newthread is
 * updated before the created ULT calls \c thread_func().
 *
 * \c attr can be created by \c ABT_thread_attr_create().  If the user passes
 * \c ABT_THREAD_ATTR_NULL for \c attr, the default ULT attribute is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_THREAD_ATTRIBUTE
 *
 * This routine copies \c attr, so the user can free \c attr after this routine
 * returns.
 *
 * If \c newthread is \c NULL, this routine creates an unnamed ULT.  An unnamed
 * ULT is automatically released on the completion of \c thread_func().
 * Otherwise, \c newthread must be explicitly freed by \c ABT_thread_free().
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 *
 * @param[in]  pool         pool handle
 * @param[in]  thread_func  function to be executed by a new ULT
 * @param[in]  arg          argument for \c thread_func()
 * @param[in]  attr         ULT attribute
 * @param[out] newthread    ULT handle
 * @return Error code
 */
int ABT_thread_create_to(ABT_pool pool, void (*thread_func)(void *), void *arg,
                         ABT_thread_attr attr, ABT_thread *newthread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread, *p_newthread;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_cur_ythread);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    ABTI_thread_type unit_type =
        (newthread != NULL)
            ? (ABTI_THREAD_TYPE_YIELDABLE | ABTI_THREAD_TYPE_NAMED)
            : ABTI_THREAD_TYPE_YIELDABLE;
    int abt_errno =
        ythread_create(p_global, ABTI_xstream_get_local(p_local_xstream),
                       p_pool, thread_func, arg, ABTI_thread_attr_get_ptr(attr),
                       unit_type, NULL, THREAD_POOL_OP_INIT, &p_newthread);
    ABTI_CHECK_ERROR(abt_errno);

    /* Set a return value before context switching. */
    if (newthread)
        *newthread = ABTI_ythread_get_handle(p_newthread);

    /* Yield to the target ULT. */
    ABTI_ythread_yield_to(&p_local_xstream, p_cur_ythread, p_newthread,
                          ABTI_YTHREAD_YIELD_TO_KIND_CREATE_TO,
                          ABT_SYNC_EVENT_TYPE_USER, NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Create a new ULT associated with an execution stream.
 *
 * \c ABT_thread_create_on_xstream() creates a new ULT, given by the attributes
 * \c attr, associates it with the first pool of the main scheduler of the
 * execution stream \c xstream, and returns its handle through \c newthread.
 * This routine pushes the created ULT to the pool \c pool.  The created ULT
 * calls \c thread_func() with \c arg when it is scheduled.
 *
 * \c attr can be created by \c ABT_thread_attr_create().  If the user passes
 * \c ABT_THREAD_ATTR_NULL for \c attr, the default ULT attribute is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_THREAD_ATTRIBUTE
 *
 * This routine copies \c attr, so the user can free \c attr after this routine
 * returns.
 *
 * If \c newthread is \c NULL, this routine creates an unnamed ULT.  An unnamed
 * ULT is automatically released on the completion of \c thread_func().
 * Otherwise, \c newthread must be explicitly freed by \c ABT_thread_free().
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR_CONDITIONAL{\c newthread,
 *                                              \c ABT_THREAD_NULL,
 *                                              \c newthread is not \c NULL}
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
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 *
 * @param[in]  xstream      execution stream handle
 * @param[in]  thread_func  function to be executed by a new ULT
 * @param[in]  arg          argument for \c thread_func()
 * @param[in]  attr         ULT attribute
 * @param[out] newthread    ULT handle
 * @return Error code
 */
int ABT_thread_create_on_xstream(ABT_xstream xstream,
                                 void (*thread_func)(void *), void *arg,
                                 ABT_thread_attr attr, ABT_thread *newthread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newthread to NULL on error. */
    if (newthread)
        *newthread = ABT_THREAD_NULL;
#endif
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_ythread *p_newthread;

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);

    /* TODO: need to consider the access type of target pool */
    ABTI_pool *p_pool = ABTI_xstream_get_main_pool(p_xstream);
    ABTI_thread_type unit_type =
        (newthread != NULL)
            ? (ABTI_THREAD_TYPE_YIELDABLE | ABTI_THREAD_TYPE_NAMED)
            : ABTI_THREAD_TYPE_YIELDABLE;
    int abt_errno = ythread_create(p_global, p_local, p_pool, thread_func, arg,
                                   ABTI_thread_attr_get_ptr(attr), unit_type,
                                   NULL, THREAD_POOL_OP_PUSH, &p_newthread);
    ABTI_CHECK_ERROR(abt_errno);

    /* Return value */
    if (newthread)
        *newthread = ABTI_ythread_get_handle(p_newthread);

    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Create a set of new ULTs.
 *
 * \c ABT_thread_create_many() creates a set of new ULTs, i.e., \c num_threads
 * ULTs, having the same ULT attribute \c attr and returns ULT handles through
 * \c newthread_list.  Each newly created ULT calls the corresponding function
 * of \c thread_func_list that has \c num_threads ULT functions with the
 * corresponding argument of \c arg_list that has \c num_threads argument
 * pointers an argument.  Each newly created ULT is pushed to the corresponding
 * pool of \c pool_list that has \c num_threads of pools handles.  That is, the
 * \a i th ULT is pushed to \a i th pool of \c pool_list and, when scheduled,
 * calls the \a i th function of \c thread_func_list with the \a i th argument
 * of \c arg_list.  This routine pushes newly created ULTs to pools \c pool.
 *
 * \c attr can be created by \c ABT_thread_attr_create().  If the user passes
 * \c ABT_THREAD_ATTR_NULL for \c attr, the default ULT attribute is used.
 *
 * @note
 * \DOC_NOTE_DEFAULT_THREAD_ATTRIBUTE\n
 * Since this routine uses the same ULT attribute for creating all ULTs, this
 * routine does not support a user-provided stack.
 *
 * If \c newthread_list is \c NULL, this routine creates unnamed ULTs.  An
 * unnamed ULT is automatically released on the completion of \c thread_func().
 * Otherwise, the creates ULTs must be explicitly freed by \c ABT_thread_free().
 *
 * This routine is deprecated because this routine does not provide a way for
 * the user to keep track of an error that happens during this routine.  The
 * user should call \c ABT_thread_create() multiple times instead.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_NO_ERROR_HANDLING
 *
 * @param[in]  num_threads       number of array elements
 * @param[in]  pool_list         array of pool handles
 * @param[in]  thread_func_list  array of ULT functions
 * @param[in]  arg_list          array of arguments for each ULT function
 * @param[in]  attr              ULT attribute
 * @param[out] newthread_list    array of ULT handles
 * @return Error code
 */
int ABT_thread_create_many(int num_threads, ABT_pool *pool_list,
                           void (**thread_func_list)(void *), void **arg_list,
                           ABT_thread_attr attr, ABT_thread *newthread_list)
{
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();
    int i;

    if (attr != ABT_THREAD_ATTR_NULL) {
        /* This implies that the stack is given by a user.  Since threads
         * cannot use the same stack region, this is illegal. */
        ABTI_CHECK_TRUE(ABTI_thread_attr_get_ptr(attr)->p_stack == NULL,
                        ABT_ERR_INV_THREAD_ATTR);
    }

    if (newthread_list == NULL) {
        for (i = 0; i < num_threads; i++) {
            ABTI_ythread *p_newthread;
            ABT_pool pool = pool_list[i];
            ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
            ABTI_CHECK_NULL_POOL_PTR(p_pool);

            void (*thread_f)(void *) = thread_func_list[i];
            void *arg = arg_list ? arg_list[i] : NULL;
            int abt_errno = ythread_create(p_global, p_local, p_pool, thread_f,
                                           arg, ABTI_thread_attr_get_ptr(attr),
                                           ABTI_THREAD_TYPE_YIELDABLE, NULL,
                                           THREAD_POOL_OP_PUSH, &p_newthread);
            ABTI_CHECK_ERROR(abt_errno);
        }
    } else {
        for (i = 0; i < num_threads; i++) {
            ABTI_ythread *p_newthread;
            ABT_pool pool = pool_list[i];
            ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
            ABTI_CHECK_NULL_POOL_PTR(p_pool);

            void (*thread_f)(void *) = thread_func_list[i];
            void *arg = arg_list ? arg_list[i] : NULL;
            int abt_errno =
                ythread_create(p_global, p_local, p_pool, thread_f, arg,
                               ABTI_thread_attr_get_ptr(attr),
                               ABTI_THREAD_TYPE_YIELDABLE |
                                   ABTI_THREAD_TYPE_NAMED,
                               NULL, THREAD_POOL_OP_PUSH, &p_newthread);
            newthread_list[i] = ABTI_ythread_get_handle(p_newthread);
            /* TODO: Release threads that have been already created. */
            ABTI_CHECK_ERROR(abt_errno);
        }
    }

    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Revive a terminated work unit.
 *
 * \c ABT_thread_revive() revives the work unit \c thread with the new work-unit
 * function \c thread_func() and its argument \c arg.  This routine does not
 * change the attributes of \c thread.  The revived work unit is pushed to the
 * pool \c pool.
 *
 * Although this routine takes a pointer of \c ABT_thread, the handle of
 * \c thread is not updated by this routine.
 *
 * \c thread must be a terminated work unit that has not been freed.  A work
 * unit that is blocked on by another caller may not be revived.
 *
 * @note
 * Because an unnamed work unit will be freed immediately after its termination,
 * an unnamed work unit cannot be revived.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_INV_THREAD_PTR{\c thread}
 * \DOC_ERROR_INV_THREAD_NOT_TERMINATED{\c thread}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_BLOCKED{\c thread, \c ABT_thread_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in]      pool         pool handle
 * @param[in]      thread_func  function to be executed by the work unit
 * @param[in]      arg          argument for \c thread_func()
 * @param[in,out]  thread       work unit handle
 * @return Error code
 */
int ABT_thread_revive(ABT_pool pool, void (*thread_func)(void *), void *arg,
                      ABT_thread *thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);
    ABTI_UB_ASSERT(thread);

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_thread *p_thread = ABTI_thread_get_ptr(*thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    ABTI_CHECK_TRUE(ABTD_atomic_relaxed_load_int(&p_thread->state) ==
                        ABT_THREAD_STATE_TERMINATED,
                    ABT_ERR_INV_THREAD);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    int abt_errno = thread_revive(p_global, p_local, p_pool, thread_func, arg,
                                  THREAD_POOL_OP_PUSH, p_thread);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Revive a terminated ULT and yield to it.
 *
 * \c ABT_thread_revive_to() revives the ULT \c thread with the new work-unit
 * function \c thread_func() and its argument \c arg.  The revived work unit is
 * associated with the pool \c pool.  Then, the calling ULT yields to the newly
 * created ULT.  The calling ULT is pushed to its associated pool.  This routine
 * does not change the attributes of \c thread.
 *
 * Although this routine takes a pointer of \c ABT_thread, the handle of
 * \c thread is not updated by this routine.
 *
 * \c thread must be a terminated ULT that has not been freed.  A ULT that is
 * blocked on by another caller may not be revived.
 *
 * @note
 * Because an unnamed ULT will be freed immediately after its termination, an
 * unnamed ULT cannot be revived.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_PTR{\c thread}
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_NOT_TERMINATED{\c thread}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_WORK_UNIT_BLOCKED{\c thread, \c ABT_thread_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in]      pool         pool handle
 * @param[in]      thread_func  function to be executed by the ULT
 * @param[in]      arg          argument for \c thread_func()
 * @param[in,out]  thread       ULT handle
 * @return Error code
 */
int ABT_thread_revive_to(ABT_pool pool, void (*thread_func)(void *), void *arg,
                         ABT_thread *thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);
    ABTI_UB_ASSERT(thread);

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_self, *p_target;
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_self);
    ABTI_CHECK_TRUE(!(p_self->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    {
        ABTI_thread *p_thread = ABTI_thread_get_ptr(*thread);
        ABTI_CHECK_NULL_THREAD_PTR(p_thread);
        ABTI_CHECK_TRUE(ABTD_atomic_relaxed_load_int(&p_thread->state) ==
                            ABT_THREAD_STATE_TERMINATED,
                        ABT_ERR_INV_THREAD);
        ABTI_CHECK_YIELDABLE(p_thread, &p_target, ABT_ERR_INV_THREAD);
    }

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);

    int abt_errno =
        thread_revive(p_global, ABTI_xstream_get_local(p_local_xstream), p_pool,
                      thread_func, arg, THREAD_POOL_OP_INIT, &p_target->thread);
    ABTI_CHECK_ERROR(abt_errno);

    /* Yield to the target ULT. */
    ABTI_ythread_yield_to(&p_local_xstream, p_self, p_target,
                          ABTI_YTHREAD_YIELD_TO_KIND_REVIVE_TO,
                          ABT_SYNC_EVENT_TYPE_USER, NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Free a work unit.
 *
 * \c ABT_thread_free() deallocates the resource used for the work unit
 * \c thread and sets \c thread to \c ABT_THREAD_NULL.  If \c thread is still
 * running, this routine will be blocked on \c thread until \c thread
 * terminates.
 *
 * @note
 * Because an unnamed work unit will be freed immediately after its termination,
 * an unnamed work unit cannot be freed by this routine.\n
 * This routine cannot free the calling work unit.\n
 * This routine cannot free the main ULT or the main scheduler's ULT.\n
 * Only one caller can join or free the same work unit.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_PTR{\c thread}
 * \DOC_ERROR_INV_THREAD_CALLER{\c thread}
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_BLOCKED{\c thread, \c ABT_thread_join() or
 *                                             \c ABT_thread_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c thread}
 *
 * @param[in,out] thread  work unit handle
 * @return Error code
 */
int ABT_thread_free(ABT_thread *thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread);

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();
    ABT_thread h_thread = *thread;

    ABTI_thread *p_thread = ABTI_thread_get_ptr(h_thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_CHECK_TRUE(!ABTI_local_get_xstream_or_null(p_local) ||
                        p_thread != ABTI_local_get_xstream(p_local)->p_thread,
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_thread->type &
                      (ABTI_THREAD_TYPE_PRIMARY | ABTI_THREAD_TYPE_MAIN_SCHED)),
                    ABT_ERR_INV_THREAD);

    /* Wait until the thread terminates */
    thread_join(&p_local, p_thread);
    /* Free the ABTI_thread structure */
    ABTI_thread_free(p_global, p_local, p_thread);

    /* Return value */
    *thread = ABT_THREAD_NULL;

    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Free a set of work units.
 *
 * \c ABT_thread_free_many() deallocates a set of work units listed in
 * \c thread_list that has \c num_threads work unit handles.  If any of work
 * units is still running, this routine will be blocked on the running work unit
 * until it terminates.  Each handle referenced by \c thread_list is set to
 * \c ABT_THRAED_NULL.
 *
 * This routine is deprecated because this routine does not provide a way for
 * the user to keep track of an error that happens during this routine.  The
 * user should call \c ABT_thread_free() multiple times instead.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{an element of \c thread_list}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_NO_ERROR_HANDLING
 *
 * @param[in]     num_threads  the number of array elements
 * @param[in,out] thread_list  array of work unit handles
 * @return Error code
 */
int ABT_thread_free_many(int num_threads, ABT_thread *thread_list)
{
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();
    int i;

    for (i = 0; i < num_threads; i++) {
        ABTI_thread *p_thread = ABTI_thread_get_ptr(thread_list[i]);
        thread_list[i] = ABT_THREAD_NULL;
        if (!p_thread)
            continue;
        /* TODO: check input */
        thread_join(&p_local, p_thread);
        ABTI_thread_free(p_global, p_local, p_thread);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Wait for a work unit to terminate.
 *
 * The caller of \c ABT_thread_join() waits for the work unit \c thread until
 * \c thread terminates.
 *
 * @note
 * Because an unnamed work unit will be freed immediately after its termination,
 * an unnamed work unit cannot be joined by this routine.\n
 * This routine cannot join the calling work unit.\n
 * This routine cannot join the main ULT or the main scheduler ULT.\n
 * Only one caller can join or free the same work unit.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_CALLER{\c thread}
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WORK_UNIT_BLOCKED{\c thread, \c ABT_thread_join() or
 *                                             \c ABT_thread_free()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in] thread  work unit handle
 * @return Error code
 */
int ABT_thread_join(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_CHECK_TRUE(!ABTI_local_get_xstream_or_null(p_local) ||
                        p_thread != ABTI_local_get_xstream(p_local)->p_thread,
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_thread->type &
                      (ABTI_THREAD_TYPE_PRIMARY | ABTI_THREAD_TYPE_MAIN_SCHED)),
                    ABT_ERR_INV_THREAD);

    thread_join(&p_local, p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Wait for a set of work units to terminate.
 *
 * The caller of \c ABT_thread_join_many() waits for all the work units in
 * \c thread_list that has \c num_threads work unit handles until all the work
 * units in \c thread_list terminate.
 *
 * This routine is deprecated because this routine does not provide a way for
 * the user to keep track of an error that happens during this routine.  The
 * user should call \c ABT_thread_join() multiple times instead.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{an element of \c thread_list}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_NO_ERROR_HANDLING
 *
 * @param[in] num_threads  the number of ULTs to join
 * @param[in] thread_list  array of target ULT handles
 * @return Error code
 */
int ABT_thread_join_many(int num_threads, ABT_thread *thread_list)
{
    ABTI_local *p_local = ABTI_local_get_local();
    int i;
    for (i = 0; i < num_threads; i++) {
        ABTI_thread *p_thread = ABTI_thread_get_ptr(thread_list[i]);
        if (!p_thread)
            continue;
        /* TODO: check input */
        thread_join(&p_local, p_thread);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Terminate a calling ULT.
 *
 * \c ABT_thread_exit() terminates the calling ULT.  This routine does not
 * return if it succeeds.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_exit()}
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_ERROR_INV_THREAD_NY
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{the caller}
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @return Error code
 */
int ABT_thread_exit(void)
{
    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_ythread;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
#endif
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_ythread);
    ABTI_CHECK_TRUE(!(p_ythread->thread.type & ABTI_THREAD_TYPE_PRIMARY),
                    ABT_ERR_INV_THREAD);

    ABTI_ythread_exit(p_local_xstream, p_ythread);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Send a cancellation request to a work unit.
 *
 * \c ABT_thread_cancel() sends a cancellation request to the work unit
 * \c thread.  \c thread may terminate before its thread function completes.
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_REQUEST
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_PRIMARY_ULT{the caller}
 * \DOC_ERROR_FEATURE_NA{the cancellation feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WORK_UNIT_NOT_RUNNING{\c thread}
 *
 * @param[in] thread  work unit handle
 * @return Error code
 */
int ABT_thread_cancel(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifdef ABT_CONFIG_DISABLE_CANCELLATION
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#else
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_CHECK_TRUE(!(p_thread->type & ABTI_THREAD_TYPE_PRIMARY),
                    ABT_ERR_INV_THREAD);

    /* Set the cancel request */
    ABTI_thread_set_request(p_thread, ABTI_THREAD_REQ_CANCEL);
    return ABT_SUCCESS;
#endif
}

/**
 * @ingroup ULT
 * @brief   Get the calling work unit.
 *
 * \c ABT_thread_self() returns the handle of the calling work unit through
 * \c thread.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_thread()}
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_INV_THREAD}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 *
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c thread, \c ABT_THREAD_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_INV_THREAD_NY
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c thread}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] thread  work unit handle
 * @return Error code
 */
int ABT_thread_self(ABT_thread *thread)
{
    ABTI_UB_ASSERT(thread);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *thread = ABT_THREAD_NULL;
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_ythread *p_self;
    ABTI_SETUP_LOCAL_YTHREAD(NULL, &p_self);
    *thread = ABTI_thread_get_handle(&p_self->thread);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    *thread = ABTI_thread_get_handle(p_local_xstream->p_thread);
#endif
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get ID of the calling work unit.
 *
 * \c ABT_thread_self_id() returns the ID of the calling work unit through
 * \c id.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_get_thread_id()}
 *
 * @changev20
 * \DOC_DESC_V1X_NOTASK{\c ABT_ERR_INV_THREAD}
 *
 * \DOC_DESC_V1X_RETURN_UNINITIALIZED
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT_NOEXT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_XSTREAM_EXT
 * \DOC_V1X \DOC_ERROR_INV_THREAD_NY
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c id}
 * \DOC_V20 \DOC_UNDEFINED_UNINIT
 *
 * @param[out] id  ID of the calling work unit
 * @return Error code
 */
int ABT_thread_self_id(ABT_unit_id *id)
{
    ABTI_UB_ASSERT(id);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_SETUP_GLOBAL(NULL);
    ABTI_ythread *p_self;
    ABTI_SETUP_LOCAL_YTHREAD(NULL, &p_self);
    *id = ABTI_thread_get_id(&p_self->thread);
#else
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM(&p_local_xstream);
    *id = ABTI_thread_get_id(p_local_xstream->p_thread);
#endif
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get an execution stream associated with a work unit.
 *
 * \c ABT_thread_get_last_xstream() returns the last execution stream associated
 * with the work unit \c thread through \c xstream.  If \c thread is not
 * associated with any execution stream, \c xstream is set to
 * \c ABT_XSTREAM_NULL.
 *
 * @note
 * The returned \c xstream may point to an invalid handle if the last execution
 * stream associated with \c thread has already been freed.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c xstream}
 *
 * @param[in]  thread   work unit handle
 * @param[out] xstream  execution stream handle
 * @return Error code
 */
int ABT_thread_get_last_xstream(ABT_thread thread, ABT_xstream *xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(xstream);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *xstream = ABTI_xstream_get_handle(p_thread->p_last_xstream);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get a state of a work unit.
 *
 * \c ABT_thread_get_state() returns the state of the work unit \c thread
 * through \c state.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_STATE
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @note
 * If \c thread is a tasklet, \c ABT_task_state is converted to the
 * corresponding \c ABT_thread_state.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c state}
 *
 * @param[in]  thread  work unit handle
 * @param[out] state   state of \c thread
 * @return Error code
 */
int ABT_thread_get_state(ABT_thread thread, ABT_thread_state *state)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(state);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *state = (ABT_thread_state)ABTD_atomic_acquire_load_int(&p_thread->state);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get the last pool of a work unit.
 *
 * \c ABT_thread_get_last_pool() returns the last pool associated with the work
 * unit \c thread through \c pool.  If \c thread is not associated with any
 * pool, \c pool is set to \c ABT_POOL_NULL.
 *
 * @note
 * The returned \c pool may point to an invalid handle if the last pool
 * associated with \c thread has already been freed.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c pool}
 *
 * @param[in]  thread  work unit handle
 * @param[out] pool    the last pool associated with \c thread
 * @return Error code
 */
int ABT_thread_get_last_pool(ABT_thread thread, ABT_pool *pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(pool);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *pool = ABTI_pool_get_handle(p_thread->p_pool);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get the last pool's ID of a work unit.
 *
 * \c ABT_thread_get_last_pool_id() returns the ID of the last pool associated
 * with the work unit \c thread through \c id.
 *
 * @note
 * The returned \c pool may point to an invalid handle if the last pool
 * associated with \c thread has already been freed.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c id}
 * \DOC_UNDEFINED_FREED{the last pool associated with \c thread}
 *
 * @param[in]  thread  work unit handle
 * @param[out] id      ID of the last pool associated with \c thread
 * @return Error code
 */
int ABT_thread_get_last_pool_id(ABT_thread thread, int *id)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(id);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    *id = (int)p_thread->p_pool->id;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get a unit handle of the target work unit.
 *
 * \c ABT_thread_get_unit() returns the \c ABT_unit handle associated with the
 * work unit \c thread through \c unit.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c unit}
 *
 * @param[in]  thread  work unit handle
 * @param[out] unit    work unit handle
 * @return Error code
 */
int ABT_thread_get_unit(ABT_thread thread, ABT_unit *unit)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(unit);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    *unit = p_thread->unit;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Set an associated pool for the target work unit.
 *
 * \c ABT_thread_set_associated_pool() changes the associated pool of the work
 * unit \c thread to the pool \c pool.  This routine must be called after
 * \c thread is popped from its original associated pool (i.e., \c thread must
 * not be in any pool), which is the pool where \c thread was residing.  This
 * routine does not push \c thread to \c pool.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_RESOURCE_UNIT_CREATE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_WORK_UNIT_IN_POOL{\c thread}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in] thread  work unit handle
 * @param[in] pool    pool handle
 * @return Error code
 */
int ABT_thread_set_associated_pool(ABT_thread thread, ABT_pool pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_global *p_global = ABTI_global_get_global();

    int abt_errno = ABTI_thread_set_associated_pool(p_global, p_thread, p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Yield the calling ULT to another ULT.
 *
 * \c ABT_thread_yield_to() yields the calling ULT and schedules the ULT
 * \c thread that is in its associated pool.  The calling ULT will be pushed to
 * its associated pool.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_yield_to()}
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{the caller}
 * \DOC_ERROR_INV_THREAD_CALLER{\c thread}
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{a pool associated with \c thread,
 *                                     functions that are necessary for this
 *                                     routine}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_UNDEFINED_WORK_UNIT_NOT_IN_POOL{\c thread,
 *                                      the pool associated with \c thread}
 * \DOC_UNDEFINED_WORK_UNIT_NOT_READY{\c thread}
 *
 * @param[in] thread  handle to the target thread
 * @return Error code
 */
int ABT_thread_yield_to(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_cur_ythread;
    p_local_xstream = ABTI_local_get_xstream_or_null(ABTI_local_get_local());
    if (ABTI_IS_EXT_THREAD_ENABLED && p_local_xstream == NULL) {
        return ABT_SUCCESS;
    } else {
        p_cur_ythread =
            ABTI_thread_get_ythread_or_null(p_local_xstream->p_thread);
        if (!p_cur_ythread)
            return ABT_SUCCESS;
    }

    ABTI_thread *p_tar_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_tar_thread);
    ABTI_ythread *p_tar_ythread = ABTI_thread_get_ythread_or_null(p_tar_thread);
    ABTI_CHECK_NULL_YTHREAD_PTR(p_tar_ythread);
    ABTI_CHECK_TRUE(p_cur_ythread != p_tar_ythread, ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_cur_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(p_tar_ythread->thread.p_pool->deprecated_def.u_is_in_pool,
                    ABT_ERR_POOL);
    ABTI_CHECK_TRUE(p_tar_ythread->thread.p_pool->deprecated_def.p_remove,
                    ABT_ERR_POOL);

    /* If the target thread is not in READY, we don't yield.  Note that ULT can
     * be regarded as 'ready' only if its state is READY and it has been
     * pushed into a pool. Since we set ULT's state to READY and then push it
     * into a pool, we check them in the reverse order, i.e., check if the ULT
     * is inside a pool and the its state. */
    if (!(p_tar_ythread->thread.p_pool->deprecated_def.u_is_in_pool(
              p_tar_ythread->thread.unit) == ABT_TRUE &&
          ABTD_atomic_acquire_load_int(&p_tar_ythread->thread.state) ==
              ABT_THREAD_STATE_READY)) {
        /* This is undefined behavior. */
        return ABT_SUCCESS;
    }

    /* Remove the target ULT from the pool */
    /* This is necessary to prevent the size of this pool from 0. */
    ABTI_pool_inc_num_blocked(p_cur_ythread->thread.p_pool);
    int abt_errno = ABTI_pool_remove(p_tar_ythread->thread.p_pool,
                                     p_tar_ythread->thread.unit);
    if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
        ABTI_pool_dec_num_blocked(p_cur_ythread->thread.p_pool);
        ABTI_HANDLE_ERROR(abt_errno);
    }

    /* We set the last ES */
    p_tar_ythread->thread.p_last_xstream = p_local_xstream;

    /* Switch the context */
    ABTI_ythread_thread_yield_to(&p_local_xstream, p_cur_ythread, p_tar_ythread,
                                 ABT_SYNC_EVENT_TYPE_USER, NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Yield the calling ULT to its parent ULT
 *
 * \c ABT_thread_yield() yields the calling ULT and pushes the calling ULT to
 * its associated pool.  Its parent ULT will be resumed.
 *
 * @note
 * \DOC_NOTE_REPLACEMENT{\c ABT_self_yield()}
 *
 * @changev20
 * \DOC_DESC_V1X_YIELD_TASK
 *
 * \DOC_DESC_V1X_YIELD_EXT
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_INIT_YIELDABLE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V20 \DOC_ERROR_INV_THREAD_NY
 * \DOC_V20 \DOC_ERROR_INV_XSTREAM_EXT
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{the caller}
 *
 * @return Error code
 */
int ABT_thread_yield(void)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_ythread;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    p_local_xstream = ABTI_local_get_xstream_or_null(ABTI_local_get_local());
    if (ABTI_IS_EXT_THREAD_ENABLED && ABTU_unlikely(p_local_xstream == NULL)) {
        return ABT_SUCCESS;
    } else {
        p_ythread = ABTI_thread_get_ythread_or_null(p_local_xstream->p_thread);
        if (ABTU_unlikely(!p_ythread)) {
            return ABT_SUCCESS;
        }
    }
#else
    ABTI_SETUP_LOCAL_YTHREAD(&p_local_xstream, &p_ythread);
#endif

    ABTI_ythread_yield(&p_local_xstream, p_ythread,
                       ABTI_YTHREAD_YIELD_KIND_USER, ABT_SYNC_EVENT_TYPE_USER,
                       NULL);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Resume a ULT.
 *
 * \c ABT_thread_resume() resumes the ULT \c thread blocked by
 * \c ABT_self_suspend() by making \c thread ready and pushing \c thread to
 * its associated pool.
 *
 * @changev20
 * \DOC_DESC_V1X_CRUDE_BLOCKED_CHECK{\c thread, \c ABT_ERR_THREAD}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_NY{\c thread}
 * \DOC_V1X \DOC_ERROR_THREAD_WORK_UNIT_UNSUSPENDED{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 * \DOC_V20 \DOC_UNDEFINED_WORK_UNIT_UNSUSPENDED{\c thread}
 *
 * @param[in] thread  ULT handle
 * @return Error code
 */
int ABT_thread_resume(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_ythread *p_ythread;
    ABTI_CHECK_YIELDABLE(p_thread, &p_ythread, ABT_ERR_INV_THREAD);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* The ULT must be in BLOCKED state. */
    ABTI_CHECK_TRUE(ABTD_atomic_acquire_load_int(&p_ythread->thread.state) ==
                        ABT_THREAD_STATE_BLOCKED,
                    ABT_ERR_THREAD);
#else
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_ythread->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);
#endif

    ABTI_ythread_resume_and_push(p_local, p_ythread);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Request a migration of a work unit to a specific execution stream.
 *
 * \c ABT_thread_migrate_to_xstream() requests a migration of the work unit
 * \c thread to any pool associated with the main scheduler of execution stream
 * \c xstream.  The previous migration request is overwritten by the new
 * migration request.  The requested work unit may be migrated before its
 * work-unit function completes.
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_REQUEST
 *
 * It is the user's responsibility to keep \c xstream, its main scheduler, and
 * its associated pools alive until the migration process completes or \c thread
 * is freed, whichever is earlier.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 *
 * \DOC_DESC_V10_CRUDE_TERMINATION_CHECK{\c thread, \c ABT_ERR_INV_THREAD}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_NOT_MIGRATABLE{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 * \DOC_ERROR_MIGRATION_TARGET{\c thread, any pool associated with the main
 *                                        scheduler of \c xstream}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_MIGRATION{\c thread, \c xstream\, the main scheduler of
 *                                            \c xstream\, or any pool
 *                                            associated with the main scheduler
 *                                            of \c xstream}
 * \DOC_UNDEFINED_WORK_UNIT_TERMINATED{\c thread}
 *
 * @param[in] thread   work unit handle
 * @param[in] xstream  execution stream handle
 * @return Error code
 */
int ABT_thread_migrate_to_xstream(ABT_thread thread, ABT_xstream xstream)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);
    ABTI_CHECK_TRUE(p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE,
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_thread->type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    /* Check if a thread is associated with a pool of the main scheduler. */
    ABTI_sched *p_sched = p_xstream->p_main_sched;
    if (ABTI_IS_ERROR_CHECK_ENABLED) {
        size_t p;
        for (p = 0; p < p_sched->num_pools; p++)
            ABTI_CHECK_TRUE(ABTI_pool_get_ptr(p_sched->pools[p]) !=
                                p_thread->p_pool,
                            ABT_ERR_MIGRATION_TARGET);
    }
    /* Get the target pool. */
    ABTI_pool *p_pool = NULL;
    int abt_errno;
    abt_errno =
        ABTI_sched_get_migration_pool(p_sched, p_thread->p_pool, &p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    /* Request a migration. */
    abt_errno = thread_migrate_to_pool(p_global, p_local, p_thread, p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_MIGRATION_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Request a migration of a work unit to a specific scheduler.
 *
 * \c ABT_thread_migrate_to_sched() requests a migration of the work unit
 * \c thread to any pool associated with the scheduler \c sched.  The previous
 * migration request is overwritten by the new migration request.  The requested
 * work unit may be migrated before its work-unit function completes.
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_REQUEST
 *
 * It is the user's responsibility to keep \c sched and its associated pools
 * alive until the migration process completes or \c thread is freed, whichever
 * is earlier.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 *
 * \DOC_DESC_V10_CRUDE_TERMINATION_CHECK{\c thread, \c ABT_ERR_INV_THREAD}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_NOT_MIGRATABLE{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 * \DOC_ERROR_MIGRATION_TARGET{\c thread, any pool associated with \c sched}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_MIGRATION{\c thread, \c sched or any pool associated
 *                                            with \c sched}
 * \DOC_UNDEFINED_WORK_UNIT_TERMINATED{\c thread}
 *
 * @param[in] thread  work unit handle
 * @param[in] sched   scheduler handle
 * @return Error code
 */
int ABT_thread_migrate_to_sched(ABT_thread thread, ABT_sched sched)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
    ABTI_CHECK_TRUE(p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE,
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_thread->type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    /* Check if a thread is associated with a pool of the main scheduler. */
    if (ABTI_IS_ERROR_CHECK_ENABLED) {
        size_t p;
        for (p = 0; p < p_sched->num_pools; p++)
            ABTI_CHECK_TRUE(ABTI_pool_get_ptr(p_sched->pools[p]) !=
                                p_thread->p_pool,
                            ABT_ERR_MIGRATION_TARGET);
    }
    /* Get the target pool. */
    ABTI_pool *p_pool;
    int abt_errno;
    abt_errno =
        ABTI_sched_get_migration_pool(p_sched, p_thread->p_pool, &p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    /* Request a migration. */
    abt_errno = thread_migrate_to_pool(p_global, p_local, p_thread, p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_MIGRATION_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Request a migration of a work unit to a specific pool.
 *
 * \c ABT_thread_migrate_to_pool() requests a migration of the work unit
 * \c thread to the pool \c pool.  The previous migration request will be
 * overwritten by the new migration request.  The requested work unit may be
 * migrated before its work-unit function completes.
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_REQUEST
 *
 * It is the user's responsibility to keep \c pool alive until the migration
 * process completes or \c thread is freed, whichever is earlier.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 *
 * \DOC_DESC_V10_CRUDE_TERMINATION_CHECK{\c thread, \c ABT_ERR_INV_THREAD}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_NOT_MIGRATABLE{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 * \DOC_ERROR_MIGRATION_TARGET{\c thread, \c pool}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_MIGRATION{\c thread, \c pool}
 * \DOC_UNDEFINED_WORK_UNIT_TERMINATED{\c thread}
 *
 * @param[in] thread  work unit handle
 * @param[in] pool    pool handle
 * @return Error code
 */
int ABT_thread_migrate_to_pool(ABT_thread thread, ABT_pool pool)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);
    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
    ABTI_CHECK_TRUE(p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE,
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_thread->type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(p_thread->p_pool != p_pool, ABT_ERR_MIGRATION_TARGET);
    /* Request a migration. */
    int abt_errno = thread_migrate_to_pool(p_global, p_local, p_thread, p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_MIGRATION_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Request a migration of a work unit to any available execution
 *          stream.
 *
 * \c ABT_thread_migrate() requests a migration of the work unit \c thread to
 * one of the execution streams.  The last execution stream of \c thread is not
 * chosen as the target execution stream.  The previous migration request will
 * be overwritten by the new migration request.  The requested work unit may be
 * migrated before its work-unit function completes.
 *
 * @note
 * \DOC_NOTE_TIMING_REQUEST
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_REQUEST
 *
 * It is the user's responsibility to keep all the execution streams, the main
 * schedulers, and their associated pools alive until the migration process
 * completes or \c thread is freed, whichever is earlier.
 *
 * This routine is deprecated because this routine is significantly restrictive.
 * The user should use other migration functions instead.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 *
 * \DOC_DESC_V10_CRUDE_TERMINATION_CHECK{\c thread, \c ABT_ERR_INV_THREAD}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_THREAD_NOT_MIGRATABLE{\c thread}
 * \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 * \DOC_ERROR_MIGRATION_NA_THREAD_MIGRATE
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_MIGRATION{\c thread, any execution stream\, any main
 *                                            scheduler of the execution
 *                                            streams\, or any pool associated
 *                                            with the main schedulers}
 * \DOC_UNDEFINED_WORK_UNIT_TERMINATED{\c thread}
 *
 * @param[in] thread  work unit handle
 * @return Error code
 */
int ABT_thread_migrate(ABT_thread thread)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    /* TODO: fix the bug(s) */
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_CHECK_TRUE(p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE,
                    ABT_ERR_INV_THREAD);
    ABTI_CHECK_TRUE(!(p_thread->type & ABTI_THREAD_TYPE_MAIN_SCHED),
                    ABT_ERR_INV_THREAD);

    /* Copy the target execution streams. */
    int i, num_xstreams, abt_errno;
    ABTI_xstream **xstreams;
    ABTD_spinlock_acquire(&p_global->xstream_list_lock);
    num_xstreams = p_global->num_xstreams;
    abt_errno =
        ABTU_malloc(sizeof(ABTI_xstream *) * num_xstreams, (void **)&xstreams);
    if (!(ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS)) {
        ABTI_xstream *p_xstream = p_global->p_xstream_head;
        i = 0;
        while (p_xstream) {
            xstreams[i++] = p_xstream;
            p_xstream = p_xstream->p_next;
        }
    }
    ABTD_spinlock_release(&p_global->xstream_list_lock);
    ABTI_CHECK_ERROR(abt_errno);

    /* Choose the destination xstream.  The user needs to maintain all the pools
     * and execution streams alive. */
    for (i = 0; i < num_xstreams; i++) {
        ABTI_xstream *p_xstream = xstreams[i];
        if (p_xstream == p_thread->p_last_xstream)
            continue;
        if (ABTD_atomic_acquire_load_int(&p_xstream->state) !=
            ABT_XSTREAM_STATE_RUNNING)
            continue;
        /* Check if a thread is associated with a pool of the main scheduler. */
        ABTI_sched *p_sched = p_xstream->p_main_sched;
        ABT_bool is_valid = ABT_TRUE;
        size_t p;
        for (p = 0; p < p_sched->num_pools; p++) {
            if (ABTI_pool_get_ptr(p_sched->pools[p]) != p_thread->p_pool) {
                is_valid = ABT_FALSE;
                break;
            }
        }
        if (!is_valid)
            continue;
        /* Get the target pool. */
        ABTI_pool *p_pool = NULL;
        abt_errno =
            ABTI_sched_get_migration_pool(p_sched, p_thread->p_pool, &p_pool);
        if (abt_errno != ABT_SUCCESS)
            continue;
        /* Request a migration. */
        abt_errno = thread_migrate_to_pool(p_global, p_local, p_thread, p_pool);
        if (abt_errno != ABT_SUCCESS)
            continue;
        /* Succeeds. Return. */
        ABTU_free(xstreams);
        return ABT_SUCCESS;
    }
    /* All attempts failed. */
    ABTU_free(xstreams);
    return ABT_ERR_MIGRATION_NA;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_MIGRATION_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Register a callback function in a work unit.
 *
 * \c ABT_thread_set_callback() registers the callback function \c cb_func() and
 * its argument \c cb_arg in the work unit \c thread.  If \c cb_func is not
 * \c NULL, \c cb_func() is called when \c thread is migrated.  The first
 * argument of \c cb_func() is the handle of a migrated work unit.  The second
 * argument is \c cb_arg passed to this routine.  The caller of the callback
 * function is undefined, so a program that relies on the caller is
 * non-conforming.  If \c cb_func is \c NULL, this routine unregisters a
 * callback function in \c thread.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_CHANGE_STATE{\c cb_func()}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in] thread   work unit handle
 * @param[in] cb_func  callback function
 * @param[in] cb_arg   argument for \c cb_func()
 * @return Error code
 */
int ABT_thread_set_callback(ABT_thread thread,
                            void (*cb_func)(ABT_thread thread, void *cb_arg),
                            void *cb_arg)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    ABTI_thread_mig_data *p_mig_data;
    int abt_errno =
        ABTI_thread_get_mig_data(p_global, p_local, p_thread, &p_mig_data);
    ABTI_CHECK_ERROR(abt_errno);

    p_mig_data->f_migration_cb = cb_func;
    p_mig_data->p_migration_cb_arg = cb_arg;
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Set the migratability in a work unit.
 *
 * \c ABT_thread_set_migratable() sets the migratability in the work unit
 * \c thread.  If \c migratable is \c ABT_TRUE, \c thread becomes migratable.
 * Otherwise, \c thread becomes unmigratable.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_SET_MIGRATABLE{\c thread, \c ABT_ERR_INV_THREAD}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 * \DOC_V20 \DOC_ERROR_INV_THREAD_PRIMARY_ULT{\c thread}
 * \DOC_V20 \DOC_ERROR_INV_THREAD_MAIN_SCHED_THREAD{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_BOOL{\c migratable}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in] thread      work unit handle
 * @param[in] migratable  migratability flag (\c ABT_TRUE: migratable,
 *                                            \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_thread_set_migratable(ABT_thread thread, ABT_bool migratable)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT_BOOL(migratable);

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    if (p_thread->type &
        (ABTI_THREAD_TYPE_PRIMARY | ABTI_THREAD_TYPE_MAIN_SCHED))
        return ABT_SUCCESS;
#else
    ABTI_CHECK_TRUE(!(p_thread->type &
                      (ABTI_THREAD_TYPE_PRIMARY | ABTI_THREAD_TYPE_MAIN_SCHED)),
                    ABT_ERR_INV_THREAD);
#endif

    if (migratable) {
        p_thread->type |= ABTI_THREAD_TYPE_MIGRATABLE;
    } else {
        p_thread->type &= ~ABTI_THREAD_TYPE_MIGRATABLE;
    }
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Get the migratability of a work unit.
 *
 * \c ABT_thread_is_migratable() returns the migratability of the work unit
 * \c thread through \c is_migratable.  If \c thread is migratable,
 * \c is_migratable is set to \c ABT_TRUE.  Otherwise, \c is_migratable is set
 * to \c ABT_FALSE.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_FEATURE_NA{the migration feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_migratable}
 *
 * @param[in]  thread         work unit handle
 * @param[out] is_migratable  result (\c ABT_TRUE: migratable,
 *                                    \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_thread_is_migratable(ABT_thread thread, ABT_bool *is_migratable)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_migratable);

#ifndef ABT_CONFIG_DISABLE_MIGRATION
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *is_migratable =
        (p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
#else
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#endif
}

/**
 * @ingroup ULT
 * @brief   Check if a work unit is the primary ULT.
 *
 * \c ABT_thread_is_primary() checks if the work unit \c thread is the primary
 * ULT and returns the result through \c is_primary.  If \c thread is the main
 * ULT, \c is_primary is set to \c ABT_TRUE.  Otherwise, \c is_primary is set to
 * \c ABT_FALSE.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_primary}
 *
 * @param[in]  thread      work unit handle
 * @param[out] is_primary  result (\c ABT_TRUE: primary ULT, \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_thread_is_primary(ABT_thread thread, ABT_bool *is_primary)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_primary);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *is_primary =
        (p_thread->type & ABTI_THREAD_TYPE_PRIMARY) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Check if a work unit is unnamed
 *
 * \c ABT_thread_is_primary() checks if the work unit \c thread is unnamed and
 * returns the result through \c is_unnamed.  If \c thread is unnamed,
 * \c is_unnamed is set to \c ABT_TRUE.  Otherwise, \c is_unnamed is set to
 * \c ABT_FALSE.
 *
 * @note
 * A handle of an unnamed work unit can be obtained by, for example, running
 * \c ABT_self_get_thread() on an unnamed work unit.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c is_unnamed}
 *
 * @param[in]  thread      work unit handle
 * @param[out] is_unnamed  result (\c ABT_TRUE: unnamed, \c ABT_FALSE: not)
 * @return Error code
 */
int ABT_thread_is_unnamed(ABT_thread thread, ABT_bool *is_unnamed)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(is_unnamed);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *is_unnamed =
        (p_thread->type & ABTI_THREAD_TYPE_NAMED) ? ABT_FALSE : ABT_TRUE;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Compare two work unit handles for equality.
 *
 * \c ABT_thread_equal() compares two work unit handles \c thread1 and
 * \c thread2 for equality.
 *
 * This routine is deprecated since its behavior is the same as comparing values
 * of \c ABT_thread handles except for handling \c ABT_THREAD_NULL and
 * \c ABT_TASK_NULL.
 * @code{.c}
 * if (thread1 == ABT_THREAD_NULL || thread1 == ABT_TASK_NULL) {
 *   *result = (thread2 == ABT_THREAD_NULL || thread2 == ABT_TASK_NULL)
 *             ? ABT_TRUE : ABT_FALSE;
 * } else {
 *   *result = (thread1 == thread2) ? ABT_TRUE : ABT_FALSE;
 * }
 * @endcode
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread1 or \c thread2}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c result}
 *
 * @param[in]  thread1  work unit handle 1
 * @param[in]  thread2  work unit handle 2
 * @param[out] result   result (\c ABT_TRUE: same, \c ABT_FALSE: not same)
 * @return Error code
 */
int ABT_thread_equal(ABT_thread thread1, ABT_thread thread2, ABT_bool *result)
{
    ABTI_UB_ASSERT(result);

    ABTI_thread *p_thread1 = ABTI_thread_get_ptr(thread1);
    ABTI_thread *p_thread2 = ABTI_thread_get_ptr(thread2);
    *result = (p_thread1 == p_thread2) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get a stack size of a work unit.
 *
 * \c ABT_thread_get_stacksize() returns the stack size of the work unit
 * \c thread in bytes through \c stacksize.  If \c thread does not have a stack
 * managed by the Argobots runtime (e.g., a tasklet or the primary ULT),
 * \c stacksize is set to 0.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c stacksize}
 *
 * @param[in]  thread     work unit handle
 * @param[out] stacksize  stack size in bytes
 * @return Error code
 */
int ABT_thread_get_stacksize(ABT_thread thread, size_t *stacksize)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(stacksize);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
    ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
    if (p_ythread) {
        *stacksize = ABTD_ythread_context_get_stacksize(&p_ythread->ctx);
    } else {
        *stacksize = 0;
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get ID of a work unit
 *
 * \c ABT_thread_get_id() returns the ID of the work unit \c thread through
 * \c thread_id.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread_id}
 *
 * @param[in]  thread     work unit handle
 * @param[out] thread_id  work unit ID
 * @return Error code
 */
int ABT_thread_get_id(ABT_thread thread, ABT_unit_id *thread_id)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_id);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *thread_id = ABTI_thread_get_id(p_thread);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Set an argument for a work-unit function of a work unit.
 *
 * \c ABT_thread_set_arg() sets the argument \c arg for the work-unit function
 * of the work unit \c thread.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c thread}
 *
 * @param[in] thread  work unit handle
 * @param[in] arg     argument for the work-unit function
 * @return Error code
 */
int ABT_thread_set_arg(ABT_thread thread, void *arg)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    p_thread->p_arg = arg;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Retrieve an argument for a work-unit function of a work unit.
 *
 * \c ABT_thread_get_arg() returns the argument for the work-unit function of
 * the work unit \c thread through \c arg.
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c arg}
 *
 * @param[in]  thread  work unit handle
 * @param[out] arg     argument for the work-unit function
 * @return Error code
 */
int ABT_thread_get_arg(ABT_thread thread, void **arg)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(arg);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *arg = p_thread->p_arg;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Retrieve a work-unit function of a work unit.
 *
 * \c ABT_thread_get_thread_func() returns the work-unit function of the work
 * unit \c thread through \c thread_func.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c thread_func}
 *
 * @param[in]  thread       work unit handle
 * @param[out] thread_func  work-unit function
 * @return Error code
 */
int ABT_thread_get_thread_func(ABT_thread thread, void (**thread_func)(void *))
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(thread_func);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    *thread_func = p_thread->f_thread;
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Set a value with a work-unit-specific data key in a work unit.
 *
 * \c ABT_thread_set_specific() associates the value \c value with the
 * work-unit-specific data key \c key in the work unit \c thread.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_KEY
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_KEY_HANDLE{\c key}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 *
 * @param[in] thread  work unit handle
 * @param[in] key     work-unit-specific data key handle
 * @param[in] value   value
 * @return Error code
 */
int ABT_thread_set_specific(ABT_thread thread, ABT_key key, void *value)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    ABTI_local *p_local = ABTI_local_get_local();

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    ABTI_key *p_key = ABTI_key_get_ptr(key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);

    /* Set the value. */
    int abt_errno =
        ABTI_ktable_set(p_global, p_local, &p_thread->p_keytable, p_key, value);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get a value associated with a work-unit-specific data key in a work
 *          unit.
 *
 * \c ABT_thread_get_specific() returns the value associated with the
 * work-unit-specific data key \c key in the work unit \c thread through
 * \c value.  If \c thread has never set a value for \c key, this routine sets
 * \c value to \c NULL.
 *
 * \DOC_DESC_ATOMICITY_WORK_UNIT_KEY
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_INV_KEY_HANDLE{\c key}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c value}
 *
 * @param[in]  thread  work unit handle
 * @param[in]  key     work-unit-specific data key handle
 * @param[out] value   value
 * @return Error code
 */
int ABT_thread_get_specific(ABT_thread thread, ABT_key key, void **value)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(value);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    ABTI_key *p_key = ABTI_key_get_ptr(key);
    ABTI_CHECK_NULL_KEY_PTR(p_key);

    /* Get the value. */
    *value = ABTI_ktable_get(&p_thread->p_keytable, p_key);
    return ABT_SUCCESS;
}

/**
 * @ingroup ULT
 * @brief   Get attributes of a work unit.
 *
 * \c ABT_thread_get_attr() returns a newly created attribute object that is
 * copied from the attributes of the work unit \c thread through \c attr.
 * Attribute values of \c attr may be different from those used on the creation
 * of \c thread.  Since this routine allocates a ULT attribute object, it is the
 * user's responsibility to free \c attr after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_TASKLET_ATTR{\c thread, \c attr, \c ABT_ERR_INV_THREAD}
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT_NOTASK \DOC_CONTEXT_NOCTXSWITCH
 * \DOC_V20 \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 * \DOC_ERROR_RESOURCE
 * \DOC_V1X \DOC_ERROR_INV_THREAD_NY{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c attr}
 *
 * @param[in]  thread  work unit handle
 * @param[out] attr    ULT attributes
 * @return Error code
 */
int ABT_thread_get_attr(ABT_thread thread, ABT_thread_attr *attr)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(attr);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);

    ABTI_thread_attr thread_attr, *p_attr;
    ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE(p_ythread, ABT_ERR_INV_THREAD);
#endif

    if (p_ythread) {
        void *p_stacktop = ABTD_ythread_context_get_stacktop(&p_ythread->ctx);
        size_t stacksize = ABTD_ythread_context_get_stacksize(&p_ythread->ctx);
        if (p_stacktop) {
            thread_attr.p_stack = (void *)(((char *)p_stacktop) - stacksize);
        } else {
            thread_attr.p_stack = NULL;
        }
        thread_attr.stacksize = stacksize;
    } else {
        thread_attr.p_stack = NULL;
        thread_attr.stacksize = 0;
    }
#ifndef ABT_CONFIG_DISABLE_MIGRATION
    thread_attr.migratable =
        (p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE) ? ABT_TRUE : ABT_FALSE;
    ABTI_thread_mig_data *p_mig_data =
        (ABTI_thread_mig_data *)ABTI_ktable_get(&p_thread->p_keytable,
                                                &g_thread_mig_data_key);
    if (p_mig_data) {
        thread_attr.f_cb = p_mig_data->f_migration_cb;
        thread_attr.p_cb_arg = p_mig_data->p_migration_cb_arg;
    } else {
        thread_attr.f_cb = NULL;
        thread_attr.p_cb_arg = NULL;
    }
#endif
    int abt_errno = ABTI_thread_attr_dup(&thread_attr, &p_attr);
    ABTI_CHECK_ERROR(abt_errno);

    *attr = ABTI_thread_attr_get_handle(p_attr);
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

ABTU_ret_err int ABTI_thread_revive(ABTI_global *p_global, ABTI_local *p_local,
                                    ABTI_pool *p_pool,
                                    void (*thread_func)(void *), void *arg,
                                    ABTI_thread *p_thread)
{
    ABTI_ASSERT(ABTD_atomic_relaxed_load_int(&p_thread->state) ==
                ABT_THREAD_STATE_TERMINATED);
    int abt_errno = thread_revive(p_global, p_local, p_pool, thread_func, arg,
                                  THREAD_POOL_OP_PUSH, p_thread);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

ABTU_ret_err int ABTI_ythread_create_primary(ABTI_global *p_global,
                                             ABTI_local *p_local,
                                             ABTI_xstream *p_xstream,
                                             ABTI_ythread **p_ythread)
{
    ABTI_thread_attr attr;
    ABTI_pool *p_pool;

    /* Get the first pool of ES */
    p_pool = ABTI_pool_get_ptr(p_xstream->p_main_sched->pools[0]);

    /* Allocate a ULT object */

    ABTI_thread_attr_init(&attr, NULL, 0, ABT_FALSE);

    /* Although this primary ULT is running now, we add this primary ULT to the
     * pool so that the scheduler can schedule the primary ULT when the primary
     * ULT is context switched to the scheduler for the first time. */
    int abt_errno =
        ythread_create(p_global, p_local, p_pool, NULL, NULL, &attr,
                       ABTI_THREAD_TYPE_YIELDABLE | ABTI_THREAD_TYPE_PRIMARY,
                       NULL, THREAD_POOL_OP_PUSH, p_ythread);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

ABTU_ret_err int ABTI_ythread_create_root(ABTI_global *p_global,
                                          ABTI_local *p_local,
                                          ABTI_xstream *p_xstream,
                                          ABTI_ythread **pp_root_ythread)
{
    ABTI_thread_attr attr;
    /* Create a ULT context */
    if (p_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY) {
        /* Create a thread with its stack */
        ABTI_thread_attr_init(&attr, NULL, p_global->sched_stacksize,
                              ABT_FALSE);
    } else {
        /* For secondary ESs, the stack of an OS thread is used. */
        ABTI_thread_attr_init(&attr, NULL, 0, ABT_FALSE);
    }
    const ABTI_thread_type thread_type = ABTI_THREAD_TYPE_YIELDABLE |
                                         ABTI_THREAD_TYPE_ROOT |
                                         ABTI_THREAD_TYPE_NAMED;
    ABTI_ythread *p_root_ythread;
    int abt_errno =
        ythread_create(p_global, p_local, NULL, thread_root_func, NULL, &attr,
                       thread_type, NULL, THREAD_POOL_OP_NONE, &p_root_ythread);
    ABTI_CHECK_ERROR(abt_errno);
    *pp_root_ythread = p_root_ythread;
    return ABT_SUCCESS;
}

ABTU_ret_err int ABTI_ythread_create_main_sched(ABTI_global *p_global,
                                                ABTI_local *p_local,
                                                ABTI_xstream *p_xstream,
                                                ABTI_sched *p_sched)
{
    ABTI_thread_attr attr;

    /* Allocate a ULT object and its stack */
    ABTI_thread_attr_init(&attr, NULL, p_global->sched_stacksize, ABT_FALSE);
    int abt_errno =
        ythread_create(p_global, p_local, p_xstream->p_root_pool,
                       thread_main_sched_func, NULL, &attr,
                       ABTI_THREAD_TYPE_YIELDABLE |
                           ABTI_THREAD_TYPE_MAIN_SCHED | ABTI_THREAD_TYPE_NAMED,
                       p_sched, THREAD_POOL_OP_PUSH, &p_sched->p_ythread);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/* This routine is to create a ULT for the scheduler. */
ABTU_ret_err int ABTI_ythread_create_sched(ABTI_global *p_global,
                                           ABTI_local *p_local,
                                           ABTI_pool *p_pool,
                                           ABTI_sched *p_sched)
{
    ABTI_thread_attr attr;

    /* Allocate a ULT object and its stack */
    ABTI_thread_attr_init(&attr, NULL, p_global->sched_stacksize, ABT_FALSE);
    int abt_errno = ythread_create(p_global, p_local, p_pool,
                                   (void (*)(void *))p_sched->run,
                                   (void *)ABTI_sched_get_handle(p_sched),
                                   &attr, ABTI_THREAD_TYPE_YIELDABLE, p_sched,
                                   THREAD_POOL_OP_PUSH, &p_sched->p_ythread);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

void ABTI_thread_join(ABTI_local **pp_local, ABTI_thread *p_thread)
{
    thread_join(pp_local, p_thread);
}

void ABTI_thread_free(ABTI_global *p_global, ABTI_local *p_local,
                      ABTI_thread *p_thread)
{
    thread_free(p_global, p_local, p_thread, ABT_TRUE);
}

void ABTI_ythread_free_primary(ABTI_global *p_global, ABTI_local *p_local,
                               ABTI_ythread *p_ythread)
{
    ABTI_thread *p_thread = &p_ythread->thread;
    thread_free(p_global, p_local, p_thread, ABT_FALSE);
}

void ABTI_ythread_free_root(ABTI_global *p_global, ABTI_local *p_local,
                            ABTI_ythread *p_ythread)
{
    thread_free(p_global, p_local, &p_ythread->thread, ABT_FALSE);
}

ABTU_ret_err int ABTI_thread_get_mig_data(ABTI_global *p_global,
                                          ABTI_local *p_local,
                                          ABTI_thread *p_thread,
                                          ABTI_thread_mig_data **pp_mig_data)
{
    ABTI_thread_mig_data *p_mig_data =
        (ABTI_thread_mig_data *)ABTI_ktable_get(&p_thread->p_keytable,
                                                &g_thread_mig_data_key);
    if (!p_mig_data) {
        int abt_errno;
        abt_errno =
            ABTU_calloc(1, sizeof(ABTI_thread_mig_data), (void **)&p_mig_data);
        ABTI_CHECK_ERROR(abt_errno);
        abt_errno = ABTI_ktable_set(p_global, p_local, &p_thread->p_keytable,
                                    &g_thread_mig_data_key, (void *)p_mig_data);
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            /* Failed to add p_mig_data to p_thread's keytable. */
            ABTU_free(p_mig_data);
            return abt_errno;
        }
    }
    *pp_mig_data = p_mig_data;
    return ABT_SUCCESS;
}

void ABTI_thread_handle_request_cancel(ABTI_global *p_global,
                                       ABTI_xstream *p_local_xstream,
                                       ABTI_thread *p_thread)
{
    ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
    if (p_ythread) {
        /* When we cancel a ULT, if other ULT is blocked to join the canceled
         * ULT, we have to wake up the joiner ULT. */
        ABTI_ythread_resume_joiner(p_local_xstream, p_ythread);
    }
    ABTI_event_thread_cancel(p_local_xstream, p_thread);
    ABTI_thread_terminate(p_global, p_local_xstream, p_thread);
}

ABTU_ret_err int ABTI_thread_handle_request_migrate(ABTI_global *p_global,
                                                    ABTI_local *p_local,
                                                    ABTI_thread *p_thread)
{
    int abt_errno;

    ABTI_thread_mig_data *p_mig_data;
    abt_errno =
        ABTI_thread_get_mig_data(p_global, p_local, p_thread, &p_mig_data);
    ABTI_CHECK_ERROR(abt_errno);

    /* Extracting an argument embedded in a migration request. */
    ABTI_pool *p_pool =
        ABTD_atomic_relaxed_load_ptr(&p_mig_data->p_migration_pool);

    /* Change the associated pool */
    abt_errno = ABTI_thread_set_associated_pool(p_global, p_thread, p_pool);
    ABTI_CHECK_ERROR(abt_errno);
    /* Call a callback function */
    if (p_mig_data->f_migration_cb) {
        ABT_thread thread = ABTI_thread_get_handle(p_thread);
        p_mig_data->f_migration_cb(thread, p_mig_data->p_migration_cb_arg);
    }
    /* Unset the migration request. */
    ABTI_thread_unset_request(p_thread, ABTI_THREAD_REQ_MIGRATE);
    return ABT_SUCCESS;
}

void ABTI_thread_print(ABTI_thread *p_thread, FILE *p_os, int indent)
{
    if (p_thread == NULL) {
        fprintf(p_os, "%*s== NULL thread ==\n", indent, "");
    } else {
        ABTI_xstream *p_xstream = p_thread->p_last_xstream;
        int xstream_rank = p_xstream ? p_xstream->rank : 0;
        const char *type, *yieldable, *state, *named, *migratable;

        if (p_thread->type & ABTI_THREAD_TYPE_PRIMARY) {
            type = "PRIMARY";
        } else if (p_thread->type & ABTI_THREAD_TYPE_MAIN_SCHED) {
            type = "MAIN_SCHED";
        } else if (p_thread->type & ABTI_THREAD_TYPE_ROOT) {
            type = "ROOT";
        } else {
            type = "USER";
        }
        if (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) {
            yieldable = "yes";
        } else {
            yieldable = "no";
        }
        if (p_thread->type & ABTI_THREAD_TYPE_NAMED) {
            named = "yes";
        } else {
            named = "no";
        }
        if (p_thread->type & ABTI_THREAD_TYPE_MIGRATABLE) {
            migratable = "yes";
        } else {
            migratable = "no";
        }
        switch (ABTD_atomic_acquire_load_int(&p_thread->state)) {
            case ABT_THREAD_STATE_READY:
                state = "READY";
                break;
            case ABT_THREAD_STATE_RUNNING:
                state = "RUNNING";
                break;
            case ABT_THREAD_STATE_BLOCKED:
                state = "BLOCKED";
                break;
            case ABT_THREAD_STATE_TERMINATED:
                state = "TERMINATED";
                break;
            default:
                state = "UNKNOWN";
                break;
        }
        ABTI_thread_mig_data *p_mig_data =
            (ABTI_thread_mig_data *)ABTI_ktable_get(&p_thread->p_keytable,
                                                    &g_thread_mig_data_key);
        void *p_migration_cb_arg =
            p_mig_data ? p_mig_data->p_migration_cb_arg : NULL;

        fprintf(p_os,
                "%*s== Thread (%p) ==\n"
                "%*sid         : %" PRIu64 "\n"
                "%*stype       : %s\n"
                "%*syieldable  : %s\n"
                "%*sstate      : %s\n"
                "%*slast_ES    : %p (%d)\n"
                "%*sparent     : %p\n"
                "%*sp_arg      : %p\n"
                "%*spool       : %p\n"
                "%*snamed      : %s\n"
                "%*smigratable : %s\n"
                "%*srequest    : 0x%x\n"
                "%*smig_cb_arg : %p\n"
                "%*skeytable   : %p\n",
                indent, "", (void *)p_thread, indent, "",
                ABTI_thread_get_id(p_thread), indent, "", type, indent, "",
                yieldable, indent, "", state, indent, "", (void *)p_xstream,
                xstream_rank, indent, "", (void *)p_thread->p_parent, indent,
                "", p_thread->p_arg, indent, "", (void *)p_thread->p_pool,
                indent, "", named, indent, "", migratable, indent, "",
                ABTD_atomic_acquire_load_uint32(&p_thread->request), indent, "",
                p_migration_cb_arg, indent, "",
                ABTD_atomic_acquire_load_ptr(&p_thread->p_keytable));

        if (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) {
            ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
            fprintf(p_os,
                    "%*sstacktop   : %p\n"
                    "%*sstacksize  : %zu\n",
                    indent, "",
                    ABTD_ythread_context_get_stacktop(&p_ythread->ctx), indent,
                    "", ABTD_ythread_context_get_stacksize(&p_ythread->ctx));
        }
    }
    fflush(p_os);
}

static ABTD_atomic_uint64 g_thread_id =
    ABTD_ATOMIC_UINT64_STATIC_INITIALIZER(0);
void ABTI_thread_reset_id(void)
{
    ABTD_atomic_release_store_uint64(&g_thread_id, 0);
}

ABT_unit_id ABTI_thread_get_id(ABTI_thread *p_thread)
{
    if (p_thread == NULL)
        return ABTI_THREAD_INIT_ID;

    if (p_thread->id == ABTI_THREAD_INIT_ID) {
        p_thread->id = thread_get_new_id();
    }
    return p_thread->id;
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

ABTU_ret_err static inline int
ythread_create(ABTI_global *p_global, ABTI_local *p_local, ABTI_pool *p_pool,
               void (*thread_func)(void *), void *arg, ABTI_thread_attr *p_attr,
               ABTI_thread_type thread_type, ABTI_sched *p_sched,
               thread_pool_op_kind pool_op, ABTI_ythread **pp_newthread)
{
    int abt_errno;
    ABTI_ythread *p_newthread;
    ABTI_ktable *p_keytable = NULL;

    /* Allocate a ULT object and its stack, then create a thread context. */
    if (!p_attr) {
        abt_errno =
            ABTI_mem_alloc_ythread_default(p_global, p_local, &p_newthread);
        ABTI_CHECK_ERROR(abt_errno);
#ifndef ABT_CONFIG_DISABLE_MIGRATION
        thread_type |= ABTI_THREAD_TYPE_MIGRATABLE;
#endif
    } else {
        /*
         * There are four memory management types for ULTs.
         * 1. A thread that uses a stack of a default size.
         *  -> size == p_global->thread_stacksize, p_stack == NULL
         * 2. A thread that uses a stack of a non-default size.
         *  -> size != 0, size != p_global->thread_stacksize, p_stack == NULL
         * 3. A thread that uses OS-level thread's stack (e.g., a primary ULT).
         *  -> size == 0, p_stack = NULL
         * 4. A thread that uses a user-allocated stack.
         *  -> p_stack != NULL
         * Only 1. is important for the performance.
         */
        if (ABTU_likely(p_attr->p_stack == NULL)) {
            const size_t default_stacksize = p_global->thread_stacksize;
            const size_t stacksize = p_attr->stacksize;
            if (ABTU_likely(stacksize == default_stacksize)) {
                /* 1. A thread that uses a stack of a default size. */
                abt_errno =
                    ABTI_mem_alloc_ythread_mempool_desc_stack(p_global, p_local,
                                                              stacksize,
                                                              &p_newthread);
            } else if (stacksize != 0) {
                /* 2. A thread that uses a stack of a non-default size. */
                abt_errno =
                    ABTI_mem_alloc_ythread_malloc_desc_stack(p_global,
                                                             stacksize,
                                                             &p_newthread);
            } else {
                /* 3. A thread that uses OS-level thread's stack */
                abt_errno =
                    ABTI_mem_alloc_ythread_mempool_desc(p_global, p_local, 0,
                                                        NULL, &p_newthread);
            }
            ABTI_CHECK_ERROR(abt_errno);
        } else {
            /* 4. A thread that uses a user-allocated stack. */
            void *p_stacktop =
                (void *)((char *)(p_attr->p_stack) + p_attr->stacksize);
            abt_errno =
                ABTI_mem_alloc_ythread_mempool_desc(p_global, p_local,
                                                    p_attr->stacksize,
                                                    p_stacktop, &p_newthread);
            ABTI_CHECK_ERROR(abt_errno);
        }
#ifndef ABT_CONFIG_DISABLE_MIGRATION
        thread_type |= p_attr->migratable ? ABTI_THREAD_TYPE_MIGRATABLE : 0;
        if (ABTU_unlikely(p_attr->f_cb)) {
            ABTI_thread_mig_data *p_mig_data;
            abt_errno = ABTU_calloc(1, sizeof(ABTI_thread_mig_data),
                                    (void **)&p_mig_data);
            if (ABTI_IS_ERROR_CHECK_ENABLED &&
                ABTU_unlikely(abt_errno != ABT_SUCCESS)) {
                ABTI_mem_free_thread(p_global, p_local, &p_newthread->thread);
                return abt_errno;
            }
            p_mig_data->f_migration_cb = p_attr->f_cb;
            p_mig_data->p_migration_cb_arg = p_attr->p_cb_arg;
            abt_errno = ABTI_ktable_set_unsafe(p_global, p_local, &p_keytable,
                                               &g_thread_mig_data_key,
                                               (void *)p_mig_data);
            if (ABTI_IS_ERROR_CHECK_ENABLED &&
                ABTU_unlikely(abt_errno != ABT_SUCCESS)) {
                if (p_keytable)
                    ABTI_ktable_free(p_global, p_local, p_keytable);
                ABTU_free(p_mig_data);
                ABTI_mem_free_thread(p_global, p_local, &p_newthread->thread);
                return abt_errno;
            }
        }
#endif
    }

    p_newthread->thread.f_thread = thread_func;
    p_newthread->thread.p_arg = arg;

    ABTD_atomic_release_store_int(&p_newthread->thread.state,
                                  ABT_THREAD_STATE_READY);
    ABTD_atomic_release_store_uint32(&p_newthread->thread.request, 0);
    p_newthread->thread.p_last_xstream = NULL;
    p_newthread->thread.p_parent = NULL;
    p_newthread->thread.type |= thread_type;
    p_newthread->thread.id = ABTI_THREAD_INIT_ID;
    if (p_sched && !(thread_type & (ABTI_THREAD_TYPE_PRIMARY |
                                    ABTI_THREAD_TYPE_MAIN_SCHED))) {
        /* Set a destructor for p_sched. */
        abt_errno = ABTI_ktable_set_unsafe(p_global, p_local, &p_keytable,
                                           &g_thread_sched_key, p_sched);
        if (ABTI_IS_ERROR_CHECK_ENABLED &&
            ABTU_unlikely(abt_errno != ABT_SUCCESS)) {
            if (p_keytable)
                ABTI_ktable_free(p_global, p_local, p_keytable);
            ABTI_mem_free_thread(p_global, p_local, &p_newthread->thread);
            return abt_errno;
        }
    }
    ABTD_atomic_relaxed_store_ptr(&p_newthread->thread.p_keytable, p_keytable);

    /* Create a wrapper unit */
    if (pool_op == THREAD_POOL_OP_PUSH || pool_op == THREAD_POOL_OP_INIT) {
        abt_errno =
            ABTI_thread_init_pool(p_global, &p_newthread->thread, p_pool);
        if (ABTI_IS_ERROR_CHECK_ENABLED &&
            ABTU_unlikely(abt_errno != ABT_SUCCESS)) {
            if (p_keytable)
                ABTI_ktable_free(p_global, p_local, p_keytable);
            ABTI_mem_free_thread(p_global, p_local, &p_newthread->thread);
            return abt_errno;
        }
        /* Invoke a thread creation event. */
        ABTI_event_thread_create(p_local, &p_newthread->thread,
                                 ABTI_local_get_xstream_or_null(p_local)
                                     ? ABTI_local_get_xstream(p_local)->p_thread
                                     : NULL,
                                 p_pool);
        if (pool_op == THREAD_POOL_OP_PUSH) {
            /* Add this thread to the pool */
            ABTI_pool_push(p_pool, p_newthread->thread.unit,
                           ABT_POOL_CONTEXT_OP_THREAD_CREATE);
        }
    } else {
        /* pool_op == THREAD_POOL_OP_NONE */
        p_newthread->thread.p_pool = p_pool;
        p_newthread->thread.unit = ABT_UNIT_NULL;
        /* Invoke a thread creation event. */
        ABTI_event_thread_create(p_local, &p_newthread->thread,
                                 ABTI_local_get_xstream_or_null(p_local)
                                     ? ABTI_local_get_xstream(p_local)->p_thread
                                     : NULL,
                                 NULL);
    }

    /* Return value */
    *pp_newthread = p_newthread;
    return ABT_SUCCESS;
}

ABTU_ret_err static inline int
thread_revive(ABTI_global *p_global, ABTI_local *p_local, ABTI_pool *p_pool,
              void (*thread_func)(void *), void *arg,
              thread_pool_op_kind pool_op, ABTI_thread *p_thread)
{
    ABTI_UB_ASSERT(ABTD_atomic_relaxed_load_int(&p_thread->state) ==
                   ABT_THREAD_STATE_TERMINATED);
    /* Set the new pool */
    int abt_errno = ABTI_thread_set_associated_pool(p_global, p_thread, p_pool);
    ABTI_CHECK_ERROR(abt_errno);

    p_thread->f_thread = thread_func;
    p_thread->p_arg = arg;

    ABTD_atomic_relaxed_store_int(&p_thread->state, ABT_THREAD_STATE_READY);
    ABTD_atomic_relaxed_store_uint32(&p_thread->request, 0);
    p_thread->p_last_xstream = NULL;
    p_thread->p_parent = NULL;

    ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
    if (p_ythread) {
        /* Create a ULT context */
        ABTD_ythread_context_reinit(&p_ythread->ctx);
    }

    /* Invoke a thread revive event. */
    ABTI_event_thread_revive(p_local, p_thread,
                             ABTI_local_get_xstream_or_null(p_local)
                                 ? ABTI_local_get_xstream(p_local)->p_thread
                                 : NULL,
                             p_pool);

    if (pool_op == THREAD_POOL_OP_PUSH) {
        /* Add this thread to the pool */
        ABTI_pool_push(p_pool, p_thread->unit,
                       ABT_POOL_CONTEXT_OP_THREAD_REVIVE);
    }
    return ABT_SUCCESS;
}

#ifndef ABT_CONFIG_DISABLE_MIGRATION
ABTU_ret_err static int thread_migrate_to_pool(ABTI_global *p_global,
                                               ABTI_local *p_local,
                                               ABTI_thread *p_thread,
                                               ABTI_pool *p_pool)
{
    /* Adding request to the thread.  p_migration_pool must be updated before
     * setting the request since the target thread would read p_migration_pool
     * after ABTI_THREAD_REQ_MIGRATE.  The update must be "atomic" (but does not
     * require acq-rel) since two threads can update the pointer value
     * simultaneously. */

    ABTI_thread_mig_data *p_mig_data;
    int abt_errno =
        ABTI_thread_get_mig_data(p_global, p_local, p_thread, &p_mig_data);
    ABTI_CHECK_ERROR(abt_errno);

    ABTD_atomic_relaxed_store_ptr(&p_mig_data->p_migration_pool,
                                  (void *)p_pool);
    ABTI_thread_set_request(p_thread, ABTI_THREAD_REQ_MIGRATE);
    return ABT_SUCCESS;
}
#endif

static inline void thread_free(ABTI_global *p_global, ABTI_local *p_local,
                               ABTI_thread *p_thread, ABT_bool free_unit)
{
    /* Invoke a thread freeing event. */
    ABTI_event_thread_free(p_local, p_thread,
                           ABTI_local_get_xstream_or_null(p_local)
                               ? ABTI_local_get_xstream(p_local)->p_thread
                               : NULL);

    /* Free the unit */
    if (free_unit) {
        ABTI_thread_unset_associated_pool(p_global, p_thread);
    }

    /* Free the key-value table */
    ABTI_ktable *p_ktable = ABTD_atomic_acquire_load_ptr(&p_thread->p_keytable);
    /* No parallel access to TLS is allowed. */
    ABTI_ASSERT(p_ktable != ABTI_KTABLE_LOCKED);
    if (p_ktable) {
        ABTI_ktable_free(p_global, p_local, p_ktable);
    }

    /* Free ABTI_thread (stack will also be freed) */
    ABTI_mem_free_thread(p_global, p_local, p_thread);
}

static void thread_key_destructor_stackable_sched(void *p_value)
{
    /* This destructor should be called in ABTI_ythread_free(), so it should not
     * free the thread again.  */
    ABTI_sched *p_sched = (ABTI_sched *)p_value;
    p_sched->used = ABTI_SCHED_NOT_USED;
    if (p_sched->automatic == ABT_TRUE) {
        ABTI_global *p_global = ABTI_global_get_global();
        p_sched->p_ythread = NULL;
        ABTI_sched_free(p_global, ABTI_local_get_local_uninlined(), p_sched,
                        ABT_FALSE);
    } else {
        /* If it is not automatic, p_ythread must be set to NULL to avoid double
         * free corruption. */
        p_sched->p_ythread = NULL;
    }
}

static void thread_key_destructor_migration(void *p_value)
{
    ABTI_thread_mig_data *p_mig_data = (ABTI_thread_mig_data *)p_value;
    ABTU_free(p_mig_data);
}

static void thread_join_busywait(ABTI_thread *p_thread)
{
    while (ABTD_atomic_acquire_load_int(&p_thread->state) !=
           ABT_THREAD_STATE_TERMINATED) {
        ABTD_atomic_pause();
    }
    ABTI_event_thread_join(NULL, p_thread, NULL);
}

#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
static void thread_join_futexwait(ABTI_thread *p_thread)
{
    ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
    if (p_ythread) {
        /* tell that this thread will join */
        uint32_t req = ABTD_atomic_fetch_or_uint32(&p_ythread->thread.request,
                                                   ABTI_THREAD_REQ_JOIN);
        if (!(req & ABTI_THREAD_REQ_JOIN)) {
            ABTD_futex_single futex;
            ABTD_futex_single_init(&futex);
            ABTI_ythread dummy_ythread;
            dummy_ythread.thread.type = ABTI_THREAD_TYPE_EXT;
            /* Just arbitrarily choose p_arg to store futex. */
            dummy_ythread.thread.p_arg = &futex;
            ABTD_atomic_release_store_ythread_context_ptr(&p_ythread->ctx
                                                               .p_link,
                                                          &dummy_ythread.ctx);
            ABTD_futex_suspend(&futex);
            /* Resumed. */
        } else {
            /* If request already has ABTI_THREAD_REQ_JOIN, p_ythread is
             * terminating.  We can't suspend in this case. */
        }
    }
    /* No matter whether this thread has been resumed or not, we need to busy-
     * wait to make sure that the thread's state gets terminated. */
    thread_join_busywait(p_thread);
}
#endif

static void thread_join_yield_thread(ABTI_xstream **pp_local_xstream,
                                     ABTI_ythread *p_self,
                                     ABTI_thread *p_thread)
{
    while (ABTD_atomic_acquire_load_int(&p_thread->state) !=
           ABT_THREAD_STATE_TERMINATED) {
        ABTI_ythread_yield(pp_local_xstream, p_self,
                           ABTI_YTHREAD_YIELD_KIND_YIELD_LOOP,
                           ABT_SYNC_EVENT_TYPE_THREAD_JOIN, (void *)p_thread);
    }
    ABTI_event_thread_join(ABTI_xstream_get_local(*pp_local_xstream), p_thread,
                           &p_self->thread);
}

static inline void thread_join(ABTI_local **pp_local, ABTI_thread *p_thread)
{
    if (ABTD_atomic_acquire_load_int(&p_thread->state) ==
        ABT_THREAD_STATE_TERMINATED) {
        ABTI_event_thread_join(*pp_local, p_thread,
                               ABTI_local_get_xstream_or_null(*pp_local)
                                   ? ABTI_local_get_xstream(*pp_local)->p_thread
                                   : NULL);
        return;
    }
    /* The primary ULT cannot be joined. */
    ABTI_ASSERT(!(p_thread->type & ABTI_THREAD_TYPE_PRIMARY));

    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(*pp_local);
    if (ABTI_IS_EXT_THREAD_ENABLED && !p_local_xstream) {
#ifdef ABT_CONFIG_ACTIVE_WAIT_POLICY
        thread_join_busywait(p_thread);
#else
        thread_join_futexwait(p_thread);
#endif
        return;
    }

    ABTI_thread *p_self_thread = p_local_xstream->p_thread;

    ABTI_ythread *p_self = ABTI_thread_get_ythread_or_null(p_self_thread);
    if (!p_self) {
#ifdef ABT_CONFIG_ACTIVE_WAIT_POLICY
        thread_join_busywait(p_thread);
#else
        thread_join_futexwait(p_thread);
#endif
        return;
    }

    /* The target ULT should be different. */
    ABTI_ASSERT(p_thread != p_self_thread);

    ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
    if (!p_ythread) {
        thread_join_yield_thread(&p_local_xstream, p_self, p_thread);
        *pp_local = ABTI_xstream_get_local(p_local_xstream);
        return;
    }

    /* Tell p_ythread that there has been a join request. */
    /* If request already has ABTI_THREAD_REQ_JOIN, p_ythread is
     * terminating. We can't block p_self in this case. */
    uint32_t req = ABTD_atomic_fetch_or_uint32(&p_ythread->thread.request,
                                               ABTI_THREAD_REQ_JOIN);
    if (req & ABTI_THREAD_REQ_JOIN) {
        /* Fall-back to the yield-based join. */
        thread_join_yield_thread(&p_local_xstream, p_self, &p_ythread->thread);
        *pp_local = ABTI_xstream_get_local(p_local_xstream);
    } else {
        /* Suspend the current ULT */
        ABTI_ythread_suspend_join(&p_local_xstream, p_self, p_ythread,
                                  ABT_SYNC_EVENT_TYPE_THREAD_JOIN,
                                  (void *)p_ythread);
        /* This thread is resumed by a target thread.  Since this ULT is resumed
         * before the target thread is fully terminated, let's wait for the
         * completion. */
        thread_join_yield_thread(&p_local_xstream, p_self, &p_ythread->thread);
        *pp_local = ABTI_xstream_get_local(p_local_xstream);
    }
}

static void thread_root_func(void *arg)
{
    /* root thread is working on a special context, so it should not rely on
     * functionality that needs yield. */
    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);
    ABTI_ASSERT(ABTD_atomic_relaxed_load_int(&p_local_xstream->state) ==
                ABT_XSTREAM_STATE_RUNNING);

    ABTI_ythread *p_root_ythread = p_local_xstream->p_root_ythread;
    p_local_xstream->p_thread = &p_root_ythread->thread;
    ABTI_pool *p_root_pool = p_local_xstream->p_root_pool;

    do {
        ABT_thread thread =
            ABTI_pool_pop(p_root_pool, ABT_POOL_CONTEXT_OWNER_PRIMARY);
        if (thread != ABT_THREAD_NULL) {
            ABTI_xstream *p_xstream = p_local_xstream;
            ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
            ABTI_ythread_schedule(p_global, &p_xstream, p_thread);
            /* The root thread must be executed on the same execution stream. */
            ABTI_ASSERT(p_xstream == p_local_xstream);
        }
    } while (ABTD_atomic_acquire_load_int(
                 &p_local_xstream->p_main_sched->p_ythread->thread.state) !=
             ABT_THREAD_STATE_TERMINATED);
    /* The main scheduler thread finishes. */

    /* Set the ES's state as TERMINATED */
    ABTD_atomic_release_store_int(&p_local_xstream->state,
                                  ABT_XSTREAM_STATE_TERMINATED);

    if (p_local_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY) {
        /* Let us jump back to the primary thread (then finalize Argobots) */
        ABTI_ythread_exit_to_primary(p_global, p_local_xstream, p_root_ythread);
    }
}

static void thread_main_sched_func(void *arg)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream(p_local);

    while (1) {
        /* Execute the run function of scheduler */
        ABTI_sched *p_sched = p_local_xstream->p_main_sched;
        ABTI_ASSERT(p_local_xstream->p_thread == &p_sched->p_ythread->thread);

        p_sched->run(ABTI_sched_get_handle(p_sched));
        /* The main scheduler's thread must be executed on the same execution
         * stream. */
        ABTI_ASSERT(p_local == ABTI_local_get_local_uninlined());

        /* We free the current main scheduler and replace it if requested. */
        if (ABTD_atomic_relaxed_load_uint32(&p_sched->request) &
            ABTI_SCHED_REQ_REPLACE) {
            ABTI_ythread *p_waiter = p_sched->p_replace_waiter;
            ABTI_sched *p_new_sched = p_sched->p_replace_sched;
            /* Set this scheduler as a main scheduler */
            p_new_sched->used = ABTI_SCHED_MAIN;
            /* Take the ULT of the current main scheduler and use it for the new
             * scheduler. */
            p_new_sched->p_ythread = p_sched->p_ythread;
            p_local_xstream->p_main_sched = p_new_sched;
            /* Now, we free the current main scheduler. p_sched->p_ythread must
             * be NULL to avoid freeing it in ABTI_sched_discard_and_free(). */
            p_sched->p_ythread = NULL;
            ABTI_sched_discard_and_free(ABTI_global_get_global(), p_local,
                                        p_sched, ABT_FALSE);
            /* We do not need to unset ABTI_SCHED_REQ_REPLACE since that p_sched
             * has already been replaced. */
            p_sched = p_new_sched;
            /* Resume the waiter. */
            ABTI_ythread_resume_and_push(p_local, p_waiter);
        }
        ABTI_ASSERT(p_sched == p_local_xstream->p_main_sched);
        uint32_t request = ABTD_atomic_acquire_load_uint32(
            &p_sched->p_ythread->thread.request);

        /* If there is an exit or a cancel request, the ES terminates
         * regardless of remaining work units. */
        if (request & ABTI_THREAD_REQ_CANCEL)
            break;

        /* When join is requested, the ES terminates after finishing
         * execution of all work units. */
        if ((ABTD_atomic_relaxed_load_uint32(&p_sched->request) &
             ABTI_SCHED_REQ_FINISH) &&
            !ABTI_sched_has_unit(p_sched)) {
            break;
        }
    }
    /* Finish this thread and goes back to the root thread. */
}

static inline ABT_unit_id thread_get_new_id(void)
{
    return (ABT_unit_id)ABTD_atomic_fetch_add_uint64(&g_thread_id, 1);
}
