/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/* Must be in a critical section. */
ABTU_ret_err static int init_library(void);
ABTU_ret_err static int finailze_library(void);
static inline ABT_bool is_initialized_library(void);

/** @defgroup ENV Init & Finalize
 * This group is for initialization and finalization of the Argobots
 * environment.
 */

/* Global Data */
ABTI_global *gp_ABTI_global = NULL;

/* To indicate how many times ABT_init is called. */
static uint32_t g_ABTI_num_inits = 0;
/* A global lock protecting the initialization/finalization process */
static ABTD_spinlock g_ABTI_init_lock = ABTD_SPINLOCK_STATIC_INITIALIZER();
/* A flag whether Argobots has been initialized or not */
static ABTD_atomic_uint32 g_ABTI_initialized =
    ABTD_ATOMIC_UINT32_STATIC_INITIALIZER(0);

/**
 * @ingroup ENV
 * @brief   Initialize the Argobots execution environment.
 *
 * \c ABT_init() initializes the Argobots execution environment.  If Argobots
 * has not been initialized, the first caller of \c ABT_init() becomes the
 * primary ULT that is running on the primary execution stream.  If Argobots has
 * already been initialized, \c ABT_init() increments a reference counter
 * atomically.  This routine returns \c ABT_SUCCESS even if Argobots has already
 * been initialized.
 *
 * \DOC_DESC_ATOMICITY_ARGOBOTS_INIT
 *
 * Argobots must be finalized by \c ABT_finalize() after its use.  Argobots can
 * be initialized and finalized multiple times in a nested manner, but the
 * caller of \c ABT_finalize() must be the same as that of \c ABT_init() at the
 * same nesting level.
 *
 * \c ABT_init() is thread-safe, but calling \c ABT_init() concurrently is
 * discouraged because the user cannot know the calling order of \c ABT_init(),
 * which is needed to finalize Argobots correctly.  \c ABT_finalize() needs to
 * be called by the same caller as that of \c ABT_init() at the same nesting
 * level.
 *
 * @note
 * Argobots can be reinitialized after freeing Argobots.  That is, \c ABT_init()
 * can be called again after Argobots is finalized by \c ABT_finalize().
 *
 * This routine does not use the arguments \c argc and \c argv.
 *
 * @note
 * Although the arguments are \c argc and \c argv, the caller of \c ABT_init()
 * does not need to be an external thread that starts a program (e.g., a POSIX
 * thread that runs \c main()).
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @param[in] argc  unused parameter
 * @param[in] argv  unused parameter
 * @return Error code
 */
int ABT_init(int argc, char **argv)
{
    ABTI_UNUSED(argc);
    ABTI_UNUSED(argv);

    int abt_errno;
    /* Take a global lock protecting the initialization/finalization process. */
    ABTD_spinlock_acquire(&g_ABTI_init_lock);
    /* If Argobots has already been initialized, just return */
    if (g_ABTI_num_inits > 0) {
        g_ABTI_num_inits++;
        abt_errno = ABT_SUCCESS;
    } else {
        abt_errno = init_library();
        if (abt_errno == ABT_SUCCESS)
            g_ABTI_num_inits++;
    }
    /* Unlock a global lock */
    ABTD_spinlock_release(&g_ABTI_init_lock);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ENV
 * @brief   Finalize the Argobots execution environment.
 *
 * If \c ABT_finalize() is called at the first nesting level, \c ABT_finalize()
 * deallocates the resource used for the Argobots execution environment and sets
 * the state of Argobots to uninitialized.  If \c ABT_finalize() is not called
 * at the first nesting level, \c ABT_finalize() decrements the reference
 * counter atomically.
 *
 * \DOC_DESC_ATOMICITY_ARGOBOTS_INIT
 *
 * Argobots can be initialized and finalized multiple times in a nested manner,
 * but the caller of \c ABT_finalize() must be the same as that of
 * \c ABT_init() at the same nesting level.  Specifically, if \c ABT_finalize()
 * is called at the first nesting level, the caller must be the primary ULT that
 * is running on the primary execution stream.
 *
 * \c ABT_finalize() is thread-safe, but calling \c ABT_finalize() concurrently
 * is discouraged because the user cannot guarantee the calling order of
 * \c ABT_finalize() although \c ABT_finalize() needs to be called by the same
 * caller as that of \c ABT_init() at the same nesting level.
 *
 * @note
 * The current specification does not define which routine can be safely called
 * during the finalization phase.  For example, the specification does not
 * specify which routine can be safely used in the user-defined scheduler
 * finalization function \c free() while the main scheduler of the primary
 * execution stream is freed during \c ABT_finalize().  The detailed behavior of
 * this routine will be clarified in the future.
 *
 * @contexts
 * \DOC_CONTEXT_FINALIZE \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_FINALIZE
 *
 * @return Error code
 */
int ABT_finalize(void)
{
    ABTI_UB_ASSERT(ABTI_initialized());

    /* Take a global lock protecting the initialization/finalization process. */
    ABTD_spinlock_acquire(&g_ABTI_init_lock);
    int abt_errno = finailze_library();
    /* Unlock a global lock */
    ABTD_spinlock_release(&g_ABTI_init_lock);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
}

/**
 * @ingroup ENV
 * @brief   Check if the Argobots execution environment has been initialized.
 *
 * \c ABT_initialized() returns \c ABT_SUCCESS if the Argobots execution
 * environment has been initialized.  Otherwise, it returns
 * \c ABT_ERR_UNINITIALIZED.
 *
 * \DOC_DESC_ATOMICITY_ARGOBOTS_INIT
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH \DOC_CONTEXT_NOTE_SIGNAL_SAFE
 *
 * @errors
 * \DOC_ERROR_SUCCESS_INITIALIZED
 * \DOC_ERROR_SUCCESS_UNINITIALIZED
 *
 * @return Error code
 */
int ABT_initialized(void)
{
    if (is_initialized_library()) {
        return ABT_SUCCESS;
    } else {
        return ABT_ERR_UNINITIALIZED;
    }
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

/* This function must be async-signal-safe. */
ABT_bool ABTI_initialized(void)
{
    return is_initialized_library();
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

ABTU_ret_err static int init_library(void)
{
    int abt_errno, init_stage = 0;

    ABTI_global *p_global;
    ABTI_xstream *p_local_xstream;

    abt_errno = ABTU_malloc(sizeof(ABTI_global), (void **)&p_global);
    ABTI_CHECK_ERROR(abt_errno);
    ABTI_global_set_global(p_global);

    /* Initialize the system environment */
    ABTD_env_init(p_global);

    /* Initialize memory pool */
    abt_errno = ABTI_mem_init(p_global);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 1;

    /* Initialize IDs */
    ABTI_thread_reset_id();
    ABTI_sched_reset_id();
    ABTI_pool_reset_id();

#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    /* Initialize the tool interface */
    ABTD_spinlock_clear(&p_global->tool_writer_lock);
    p_global->tool_thread_cb_f = NULL;
    p_global->tool_thread_user_arg = NULL;
    ABTD_atomic_relaxed_store_uint64(&p_global->tool_thread_event_mask_tagged,
                                     0);
#endif
    /* Initialize a unit-to-thread hash table. */
    ABTI_unit_init_hash_table(p_global);

    /* Initialize the ES list */
    p_global->p_xstream_head = NULL;
    p_global->num_xstreams = 0;

    /* Initialize a spinlock */
    ABTD_spinlock_clear(&p_global->xstream_list_lock);

    /* Create the primary ES */
    abt_errno = ABTI_xstream_create_primary(p_global, &p_local_xstream);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 2;

    /* Init the ES local data */
    ABTI_local_set_xstream(p_local_xstream);

    /* Create the primary ULT */
    ABTI_ythread *p_primary_ythread;
    abt_errno =
        ABTI_ythread_create_primary(p_global,
                                    ABTI_xstream_get_local(p_local_xstream),
                                    p_local_xstream, &p_primary_ythread);
    if (abt_errno != ABT_SUCCESS)
        goto FAILED;
    init_stage = 3;

    /* Set as if p_local_xstream is currently running the primary ULT. */
    ABTD_atomic_relaxed_store_int(&p_primary_ythread->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    p_primary_ythread->thread.p_last_xstream = p_local_xstream;
    p_global->p_primary_ythread = p_primary_ythread;
    p_local_xstream->p_thread = &p_primary_ythread->thread;

    /* Start the primary ES */
    ABTI_xstream_start_primary(p_global, &p_local_xstream, p_local_xstream,
                               p_primary_ythread);

    if (p_global->print_config == ABT_TRUE) {
        ABTI_info_print_config(p_global, stdout);
    }
    ABTD_atomic_release_store_uint32(&g_ABTI_initialized, 1);
    return ABT_SUCCESS;
FAILED:
    if (init_stage >= 2) {
        ABTI_xstream_free(p_global, ABTI_xstream_get_local(p_local_xstream),
                          p_local_xstream, ABT_TRUE);
        ABTI_local_set_xstream(NULL);
    }
    if (init_stage >= 1) {
        ABTI_mem_finalize(p_global);
    }
    ABTD_affinity_finalize(p_global);
    ABTU_free(p_global);
    ABTI_global_set_global(NULL);
    ABTI_HANDLE_ERROR(abt_errno);
}

ABTU_ret_err static int finailze_library(void)
{
    ABTI_local *p_local = ABTI_local_get_local();

    /* If Argobots is not initialized, just return */
    ABTI_CHECK_TRUE(g_ABTI_num_inits > 0, ABT_ERR_UNINITIALIZED);
    /* If Argobots is still referenced by others, just return */
    if (--g_ABTI_num_inits != 0) {
        return ABT_SUCCESS;
    }

    ABTI_global *p_global = ABTI_global_get_global();
    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
    /* If called by an external thread, return an error. */
    ABTI_CHECK_TRUE(!ABTI_IS_EXT_THREAD_ENABLED || p_local_xstream,
                    ABT_ERR_INV_XSTREAM);

    ABTI_CHECK_TRUE_MSG(p_local_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY,
                        ABT_ERR_INV_XSTREAM,
                        "ABT_finalize must be called by the primary ES.");

    ABTI_thread *p_self = p_local_xstream->p_thread;
    ABTI_CHECK_TRUE_MSG(p_self->type & ABTI_THREAD_TYPE_PRIMARY,
                        ABT_ERR_INV_THREAD,
                        "ABT_finalize must be called by the primary ULT.");
    ABTI_ythread *p_ythread;
    ABTI_CHECK_YIELDABLE(p_self, &p_ythread, ABT_ERR_INV_THREAD);

#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    /* Turns off the tool interface */
    ABTI_tool_event_thread_update_callback(p_global, NULL,
                                           ABT_TOOL_EVENT_THREAD_NONE, NULL);
#endif

    /* Finish the main scheduler of this local xstream. */
    ABTI_sched_finish(p_local_xstream->p_main_sched);
    /* p_self cannot join the main scheduler since p_self needs to be orphaned.
     * Let's wait till the main scheduler finishes.  This thread will be
     * scheduled when the main root thread finishes. */
    ABTI_ythread_yield_orphan(&p_local_xstream, p_ythread,
                              ABT_SYNC_EVENT_TYPE_OTHER, NULL);
    ABTI_ASSERT(p_local_xstream == ABTI_local_get_xstream(p_local));
    ABTI_ASSERT(p_local_xstream->p_thread == p_self);

    /* Remove the primary ULT */
    p_local_xstream->p_thread = NULL;
    ABTI_ythread_free_primary(p_global, ABTI_xstream_get_local(p_local_xstream),
                              p_ythread);

    /* Free the primary ES */
    ABTI_xstream_free(p_global, ABTI_xstream_get_local(p_local_xstream),
                      p_local_xstream, ABT_TRUE);

    /* Finalize the ES local data */
    ABTI_local_set_xstream(NULL);

    /* Free the ES array */
    ABTI_ASSERT(p_global->p_xstream_head == NULL);

    /* Finalize the memory pool */
    ABTI_mem_finalize(p_global);

    /* Restore the affinity */
    ABTD_affinity_finalize(p_global);

    /* Free a unit-to-thread hash table. */
    ABTI_unit_finalize_hash_table(p_global);

    /* Free the ABTI_global structure */
    ABTU_free(p_global);
    ABTI_global_set_global(NULL);
    ABTD_atomic_release_store_uint32(&g_ABTI_initialized, 0);
    return ABT_SUCCESS;
}

/* This function must be async-signal-safe. */
static inline ABT_bool is_initialized_library(void)
{
    return (ABTD_atomic_acquire_load_uint32(&g_ABTI_initialized) != 0)
               ? ABT_TRUE
               : ABT_FALSE;
}
