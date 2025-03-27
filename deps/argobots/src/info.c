/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTU_ret_err static int info_print_thread_stacks_in_pool(ABTI_global *p_global,
                                                         FILE *fp,
                                                         ABTI_pool *p_pool);
static void info_trigger_print_all_thread_stacks(
    FILE *fp, double timeout, void (*cb_func)(ABT_bool, void *), void *arg);

/** @defgroup INFO  Information
 * This group is for getting runtime information of Argobots.  The routines in
 * this group are meant for debugging and diagnosing Argobots.
 */

/**
 * @ingroup INFO
 * @brief   Retrieve the configuration information.
 *
 * \c ABT_info_query_config() returns the configuration information associated
 * with the query kind \c query_kind through \c val.
 *
 * The retrieved information is selected via \c query_kind.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_DEBUG
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the debug mode.
 *   Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_PRINT_ERRNO
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to print an error number when an
 *   error occurs.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_LOG
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to print debug messages.
 *   Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_VALGRIND
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to be Valgrind friendly.
 *   Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_FALSE if Argobots is configured to ignore some error checks.
 *   Otherwise, \c val is set to \c ABT_TRUE
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_CHECK_POOL_PRODUCER
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_FALSE if Argobots is configured to ignore an access violation
 *   error regarding pool producers.  Otherwise, \c val is set to \c ABT_TRUE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_CHECK_POOL_CONSUMER
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_FALSE if Argobots is configured to ignore an access violation
 *   error regarding pool consumers.  Otherwise, \c val is set to \c ABT_TRUE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_PRESERVE_FPU
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to save floating-point registers
 *   on user-level context switching.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_THREAD_CANCEL
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the thread cancellation
 *   feature. Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_TASK_CANCEL
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the task cancellation
 *   feature. Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_MIGRATION
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the thread/task
 *   migration feature.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_STACKABLE_SCHED
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the stackable scheduler
 *   feature.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the external thread
 *   feature.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_SCHED_SLEEP
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the sleep feature for
 *   predefined schedulers.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_PRINT_CONFIG
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to print all the configuration
 *   settings in the top-level \c ABT_init().  Otherwise, \c val is set to
 *   \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_AFFINITY
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the affinity setting.
 *   Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_MAX_NUM_XSTREAMS
 *
 *   \c val must be a pointer to a variable of type \c unsigned \c int.  \c val
 *   is set to the maximum number of execution streams in Argobots.
 *
 * - \c ABT_INFO_QUERY_KIND_DEFAULT_THREAD_STACKSIZE
 *
 *   \c val must be a pointer to a variable of type \c size_t.  \c val is set to
 *   the default stack size of ULTs.
 *
 * - \c ABT_INFO_QUERY_KIND_DEFAULT_SCHED_STACKSIZE
 *
 *   \c val must be a pointer to a variable of type \c size_t.  \c val is set to
 *   the default stack size of ULT-type schedulers.
 *
 * - \c ABT_INFO_QUERY_KIND_DEFAULT_SCHED_EVENT_FREQ
 *
 *   \c val must be a pointer to a variable of type \c uint64_t.  \c val is set
 *   to the default event-checking frequency of predefined schedulers.
 *
 * - \c ABT_INFO_QUERY_KIND_DEFAULT_SCHED_SLEEP_NSEC
 *
 *   \c val must be a pointer to a variable of type \c uint64_t.  \c val is set
 *   to the default sleep time of predefined schedulers in nanoseconds.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_TOOL
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the tool feature.
 *   Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_FCONTEXT
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to use fcontext.  Otherwise,
 *   \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_DYNAMIC_PROMOTION
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the dynamic promotion
 *   optimization.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_STACK_UNWIND
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable the stack unwinding
 *   feature.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_STACK_OVERFLOW_CHECK
 *
 *   \c val must be a pointer to a variable of type \c int.  \c val is set to 1
 *   if Argobots is configured to use a stack canary to check stack overflow.
 *   \c val is set to 2 if Argobots is configured to use an mprotect-based stack
 *   guard but ignore an error of \c mprotect().  \c val is set to 3 if Argobots
 *   is configured to use an mprotect-based stack guard and assert an error of
 *   \c mprotect().  Otherwise, \c val is set to 0.
 *
 * - \c ABT_INFO_QUERY_KIND_WAIT_POLICY
 *
 *   \c val must be a pointer to a variable of type \c int.  \c val is set to 0
 *   if the wait policy of Argobots is passive.  \c val is set to 1 if the wait
 *   policy of Argobots is active.
 *
 * - \c ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC
 *
 *   \c val must be a pointer to a variable of type \c ABT_bool.  \c val is set
 *   to \c ABT_TRUE if Argobots is configured to enable lazy allocation for ULT
 *   stacks by default.  Otherwise, \c val is set to \c ABT_FALSE.
 *
 * @changev20
 * \DOC_DESC_V1X_RETURN_INFO_IF_POSSIBLE
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_INFO_QUERY_KIND{\c query_kind}
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 * \DOC_V20 \DOC_ERROR_UNINITIALIZED_INFO_QUERY
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c val}
 *
 * @param[in]  query_kind  query kind
 * @param[out] val         result
 * @return Error code
 */
int ABT_info_query_config(ABT_info_query_kind query_kind, void *val)
{
    ABTI_UB_ASSERT(val);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x always requires an init check. */
    ABTI_SETUP_GLOBAL(NULL);
#endif
    switch (query_kind) {
        case ABT_INFO_QUERY_KIND_ENABLED_DEBUG: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((ABT_bool *)val) = p_global->use_debug;
            } else {
                *((ABT_bool *)val) = ABTD_env_get_use_debug();
            }
        } break;
        case ABT_INFO_QUERY_KIND_ENABLED_PRINT_ERRNO:
#ifdef ABT_CONFIG_PRINT_ABT_ERRNO
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_LOG: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((ABT_bool *)val) = p_global->use_logging;
            } else {
                *((ABT_bool *)val) = ABTD_env_get_use_logging();
            }
        } break;
        case ABT_INFO_QUERY_KIND_ENABLED_VALGRIND:
#ifdef HAVE_VALGRIND_SUPPORT
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR:
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_CHECK_POOL_PRODUCER:
            *((ABT_bool *)val) = ABT_FALSE;
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_CHECK_POOL_CONSUMER:
            *((ABT_bool *)val) = ABT_FALSE;
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_PRESERVE_FPU:
#if !defined(ABTD_FCONTEXT_PRESERVE_FPU) && defined(ABT_CONFIG_USE_FCONTEXT)
            *((ABT_bool *)val) = ABT_FALSE;
#else
            /* If ucontext is used, FPU is preserved. */
            *((ABT_bool *)val) = ABT_TRUE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_THREAD_CANCEL:
#ifndef ABT_CONFIG_DISABLE_CANCELLATION
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_TASK_CANCEL:
#ifndef ABT_CONFIG_DISABLE_CANCELLATION
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_MIGRATION:
#ifndef ABT_CONFIG_DISABLE_MIGRATION
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_STACKABLE_SCHED:
            *((ABT_bool *)val) = ABT_TRUE;
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD:
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_SCHED_SLEEP:
#ifdef ABT_CONFIG_USE_SCHED_SLEEP
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_PRINT_CONFIG: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((ABT_bool *)val) = p_global->print_config;
            } else {
                *((ABT_bool *)val) = ABTD_env_get_print_config();
            }
        } break;
        case ABT_INFO_QUERY_KIND_ENABLED_AFFINITY: {
            ABTI_global *p_global;
            /* This check needs runtime check in ABT_init(). */
            ABTI_SETUP_GLOBAL(&p_global);
            *((ABT_bool *)val) = p_global->set_affinity;
        } break;
        case ABT_INFO_QUERY_KIND_MAX_NUM_XSTREAMS: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((unsigned int *)val) = p_global->max_xstreams;
            } else {
                *((unsigned int *)val) = ABTD_env_get_max_xstreams();
            }
        } break;
        case ABT_INFO_QUERY_KIND_DEFAULT_THREAD_STACKSIZE: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((size_t *)val) = p_global->thread_stacksize;
            } else {
                *((size_t *)val) = ABTD_env_get_thread_stacksize();
            }
        } break;
        case ABT_INFO_QUERY_KIND_DEFAULT_SCHED_STACKSIZE: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((size_t *)val) = p_global->sched_stacksize;
            } else {
                *((size_t *)val) = ABTD_env_get_sched_stacksize();
            }
        } break;
        case ABT_INFO_QUERY_KIND_DEFAULT_SCHED_EVENT_FREQ: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((uint64_t *)val) = p_global->sched_event_freq;
            } else {
                *((uint64_t *)val) = ABTD_env_get_sched_event_freq();
            }
        } break;
        case ABT_INFO_QUERY_KIND_DEFAULT_SCHED_SLEEP_NSEC: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                *((uint64_t *)val) = p_global->sched_sleep_nsec;
            } else {
                *((uint64_t *)val) = ABTD_env_get_sched_sleep_nsec();
            }
        } break;
        case ABT_INFO_QUERY_KIND_ENABLED_TOOL:
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_FCONTEXT:
#ifdef ABT_CONFIG_USE_FCONTEXT
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_DYNAMIC_PROMOTION:
            *((ABT_bool *)val) = ABT_FALSE;
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_STACK_UNWIND:
#ifdef ABT_CONFIG_ENABLE_STACK_UNWIND
            *((ABT_bool *)val) = ABT_TRUE;
#else
            *((ABT_bool *)val) = ABT_FALSE;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_STACK_OVERFLOW_CHECK: {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (p_global) {
                if (p_global->stack_guard_kind == ABTI_STACK_GUARD_MPROTECT) {
                    *((int *)val) = 2;
                } else if (p_global->stack_guard_kind ==
                           ABTI_STACK_GUARD_MPROTECT_STRICT) {
                    *((int *)val) = 3;
                } else {
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
                    *((int *)val) = 1;
#else
                    *((int *)val) = 0;
#endif
                }
            } else {
                ABT_bool is_strict;
                if (ABTD_env_get_stack_guard_mprotect(&is_strict)) {
                    if (is_strict) {
                        *((int *)val) = 3;
                    } else {
                        *((int *)val) = 2;
                    }
                } else {
#if ABT_CONFIG_STACK_CHECK_TYPE == ABTI_STACK_CHECK_TYPE_CANARY
                    *((int *)val) = 1;
#else
                    *((int *)val) = 0;
#endif
                }
            }
        } break;
        case ABT_INFO_QUERY_KIND_WAIT_POLICY:
#if ABT_CONFIG_ACTIVE_WAIT_POLICY
            *((int *)val) = 1;
#else
            *((int *)val) = 0;
#endif
            break;
        case ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC:
#ifdef ABT_CONFIG_DISABLE_LAZY_STACK_ALLOC
            *((ABT_bool *)val) = ABT_FALSE;
#else
            *((ABT_bool *)val) = ABT_TRUE;
#endif
            break;
        default:
            ABTI_HANDLE_ERROR(ABT_ERR_INV_QUERY_KIND);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the runtime information of Argobots.
 *
 * \c ABT_info_print_config() writes the runtime information of Argobots to the
 * output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_RUNTIME_INFO
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp  output stream
 * @return Error code
 */
int ABT_info_print_config(FILE *fp)
{
    ABTI_UB_ASSERT(fp);

    ABTI_global *p_global;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x always requires an init check. */
    ABTI_SETUP_GLOBAL(&p_global);
#else
    p_global = ABTI_global_get_global_or_null();
    if (!p_global) {
        fprintf(fp, "Argobots is not initialized.\n");
        fflush(fp);
        return ABT_SUCCESS;
    }
#endif
    ABTI_info_print_config(p_global, fp);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of all execution streams.
 *
 * \c ABT_info_print_all_xstreams() writes the information of all execution
 * streams to the output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_RUNTIME_INFO
 * @endchangev20
 *
 * @contexts
 * \DOC_V1X \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH\n
 * \DOC_V20 \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_UNINITIALIZED
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp  output stream
 * @return Error code
 */
int ABT_info_print_all_xstreams(FILE *fp)
{
    ABTI_UB_ASSERT(fp);

    ABTI_global *p_global;
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x always requires an init check. */
    ABTI_SETUP_GLOBAL(&p_global);
#else
    p_global = ABTI_global_get_global_or_null();
    if (!p_global) {
        fprintf(fp, "Argobots is not initialized.\n");
        fflush(fp);
        return ABT_SUCCESS;
    }
#endif

    ABTD_spinlock_acquire(&p_global->xstream_list_lock);

    fprintf(fp, "# of created ESs: %d\n", p_global->num_xstreams);

    ABTI_xstream *p_xstream = p_global->p_xstream_head;
    while (p_xstream) {
        ABTI_xstream_print(p_xstream, fp, 0, ABT_FALSE);
        p_xstream = p_xstream->p_next;
    }

    ABTD_spinlock_release(&p_global->xstream_list_lock);

    fflush(fp);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of an execution stream.
 *
 * \c ABT_info_print_xstream() writes the information of the execution stream
 * \c xstream to the output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c xstream, \c ABT_XSTREAM_NULL,
 *                                 \c ABT_ERR_INV_XSTREAM}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_XSTREAM_HANDLE{\c xstream}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp       output stream
 * @param[in] xstream  execution stream handle
 * @return Error code
 */
int ABT_info_print_xstream(FILE *fp, ABT_xstream xstream)
{
    ABTI_UB_ASSERT(fp);

    ABTI_xstream *p_xstream = ABTI_xstream_get_ptr(xstream);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_XSTREAM_PTR(p_xstream);
#endif
    ABTI_xstream_print(p_xstream, fp, 0, ABT_FALSE);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of a scheduler.
 *
 * \c ABT_info_print_sched() writes the information of the scheduler \c sched to
 * the output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c sched, \c ABT_SCHED_NULL,
 *                                 \c ABT_ERR_INV_SCHED}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_SCHED_HANDLE{\c sched}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp     output stream
 * @param[in] sched  scheduler handle
 * @return Error code
 */
int ABT_info_print_sched(FILE *fp, ABT_sched sched)
{
    ABTI_UB_ASSERT(fp);

    ABTI_sched *p_sched = ABTI_sched_get_ptr(sched);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_SCHED_PTR(p_sched);
#endif
    ABTI_sched_print(p_sched, fp, 0, ABT_TRUE);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of a pool.
 *
 * \c ABT_info_print_pool() writes the information of the pool \c pool to the
 * output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c pool, \c ABT_POOL_NULL,
 *                                 \c ABT_ERR_INV_POOL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp    output stream
 * @param[in] pool  pool handle
 * @return Error code
 */
int ABT_info_print_pool(FILE *fp, ABT_pool pool)
{
    ABTI_UB_ASSERT(fp);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
#endif
    ABTI_pool_print(p_pool, fp, 0);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of a work unit.
 *
 * \c ABT_info_print_thread() writes the information of the work unit \c thread
 * to the output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c thread, \c ABT_THREAD_NULL,
 *                                 \c ABT_TASK_NULL, \c ABT_ERR_INV_THREAD}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp      output stream
 * @param[in] thread  work unit handle
 * @return Error code
 */
int ABT_info_print_thread(FILE *fp, ABT_thread thread)
{
    ABTI_UB_ASSERT(fp);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
#endif
    ABTI_thread_print(p_thread, fp, 0);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of a ULT attribute.
 *
 * \c ABT_info_print_thread_attr() writes the information of the ULT attribute
 * \c attr to the output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c attr, \c ABT_THREAD_ATTR_NULL,
 *                                 \c ABT_ERR_INV_THREAD_ATTR}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_THREAD_ATTR_HANDLE{\c attr}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp    output stream
 * @param[in] attr  ULT attribute handle
 * @return Error code
 */
int ABT_info_print_thread_attr(FILE *fp, ABT_thread_attr attr)
{
    ABTI_UB_ASSERT(fp);

    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_THREAD_ATTR_PTR(p_attr);
#endif
    ABTI_thread_attr_print(p_attr, fp, 0);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print the information of a work unit.
 *
 * \c ABT_info_print_task() writes the information of the work unit \c task to
 * the output stream \c fp.  This routine is deprecated because its
 * functionality is the same as that of \c ABT_info_print_thread().
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_THREAD{\c task}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c task, \c ABT_THREAD_NULL,
 *                                 \c ABT_TASK_NULL, \c ABT_ERR_INV_TASK}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_TASK_HANDLE{\c task}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp    output stream
 * @param[in] task  work unit handle
 * @return Error code
 */
int ABT_info_print_task(FILE *fp, ABT_task task)
{
    ABTI_UB_ASSERT(fp);

    ABTI_thread *p_thread = ABTI_thread_get_ptr(task);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_TASK_PTR(p_thread);
#endif
    ABTI_thread_print(p_thread, fp, 0);
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print stack of a work unit.
 *
 * \c ABT_info_print_thread_stack() prints the information of the stack of the
 * work unit \c thread to the output stream \c fp.
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev11
 * \DOC_DESC_V10_ACCEPT_TASK{\c thread}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c thread, \c ABT_THREAD_NULL,
 *                                 \c ABT_TASK_NULL, \c ABT_ERR_INV_THREAD}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_V1X \DOC_ERROR_INV_THREAD_HANDLE{\c thread}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 * \DOC_UNDEFINED_WORK_UNIT_RUNNING{\c thread}
 *
 * @param[in] fp      output stream
 * @param[in] thread  work unit handle
 * @return Error code
 */
int ABT_info_print_thread_stack(FILE *fp, ABT_thread thread)
{
    ABTI_UB_ASSERT(fp);
    /* We can check if thread is running or not in ABTI_UB_ASSERT(), but as this
     * info function is basically used for debugging, printing a corrupted stack
     * or even crashing a program would be fine. */

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_THREAD_PTR(p_thread);
#endif
    if (!p_thread) {
        fprintf(fp, "no stack\n");
        fflush(0);
    } else {
        if (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) {
            ABTI_global *p_global = ABTI_global_get_global_or_null();
            if (!p_global) {
                fprintf(fp, "Argobots is not initialized.\n");
                fflush(0);
            } else {
                ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
                ABTI_ythread_print_stack(p_global, p_ythread, fp);
            }
        } else {
            fprintf(fp, "no stack\n");
            fflush(0);
        }
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print stacks of all work units in a pool.
 *
 * \c ABT_info_print_thread_stacks_in_pool() prints the information of stacks of
 * all work units in the pool \c pool to the output stream \c fp.  \c pool must
 * support \c p_print_all().
 *
 * @note
 * \DOC_NOTE_INFO_PRINT
 *
 * @changev20
 * \DOC_DESC_V1X_PRINT_HANDLE_INFO{\c pool, \c ABT_POOL_NULL,
 *                                 \c ABT_ERR_INV_POOL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_POOL_UNSUPPORTED_FEATURE{\c pool, \c p_print_all()}
 * \DOC_V1X \DOC_ERROR_INV_POOL_HANDLE{\c pool}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 *
 * @param[in] fp    output stream
 * @param[in] pool  pool handle
 * @return Error code
 */
int ABT_info_print_thread_stacks_in_pool(FILE *fp, ABT_pool pool)
{
    ABTI_UB_ASSERT(fp);

    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x requires a NULL-handle check. */
    ABTI_CHECK_NULL_POOL_PTR(p_pool);
#endif
    ABTI_global *p_global = ABTI_global_get_global_or_null();
    if (!p_global) {
        fprintf(fp, "Argobots is not initialized.\n");
        fflush(0);
    } else {
        int abt_errno = info_print_thread_stacks_in_pool(p_global, fp, p_pool);
        ABTI_CHECK_ERROR(abt_errno);
    }
    return ABT_SUCCESS;
}

/**
 * @ingroup INFO
 * @brief   Print stacks of work units in pools associated with all the main
 *          schedulers.
 *
 * \c ABT_info_trigger_print_all_thread_stacks() tries to print the information
 * of stacks of all work units stored in pools associated with all the main
 * schedulers to the output stream \c fp.  This routine itself does not print
 * the information; this routine immediately returns after updating a flag.
 * The stack information is printed when all execution streams stop at
 * \c ABT_xstream_check_events().
 *
 * If \c timeout is negative, the stack information is printed only after all
 * the execution streams stop at \c ABT_xstream_check_events().  If \c timeout
 * is nonnegative, one of the execution streams that stop at
 * \c ABT_xstream_check_events() starts to print the stack information even if
 * some of the execution streams do not stop at \c ABT_xstream_check_events()
 * within a certain time period specified by \c timeout in seconds.  In this
 * case, this routine might not work correctly and at worst crashes a program.
 * The stack information is never printed if no execution stream executes
 * \c ABT_xstream_check_events().
 *
 * The callback function \c cb_func() is called after completing printing stacks
 * unless it is registered.  The first argument of \c cb_func() is set to
 * \c ABT_TRUE if \c timeout is nonnegative and not all the execution streams
 * stop within \c timeout.  Otherwise, the first argument is set to
 * \c ABT_FALSE.  The second argument of \c cb_func() is the user-defined data
 * \c arg passed to this routine.  The caller of \c cb_func() is undefined, so a
 * program that relies on the caller of \c cb_func() is non-conforming.  Neither
 * signal-safety nor thread-safety is required for \c cb_func().
 *
 * The following work units are not captured by this routine:
 * - Work units that are suspending (e.g., by \c ABT_thread_suspend()).
 * - Work units in pools that are not associated with main schedulers.
 *
 * Calling \c ABT_info_trigger_print_all_thread_stacks() multiple times updates
 * old values.  The values are atomically updated.
 *
 * @note
 * This routine prints the information in a best-effort basis.  Specifically,
 * this routine does not return an error regarding \c fp to either the caller of
 * this routine or \c cb_func().\n
 * If the timeout mechanism is used, the program may crash, so this
 * functionality should be used only for debugging and diagnosis.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH \DOC_CONTEXT_NOTE_SIGNAL_SAFE
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c fp}
 * \DOC_UNDEFINED_NULL_PTR{\c cb_func}
 * \DOC_UNDEFINED_SYS_FILE{\c fp}
 * \DOC_UNDEFINED_CHANGE_STATE{\c cb_func()}
 *
 * @param[in] fp       output stream
 * @param[in] timeout  timeout in seconds
 * @param[in] cb_func  callback function
 * @param[in] arg      argument passed to \c cb_func()
 * @return Error code
 */
int ABT_info_trigger_print_all_thread_stacks(FILE *fp, double timeout,
                                             void (*cb_func)(ABT_bool, void *),
                                             void *arg)
{
    /* assert() is not signal safe, so we cannot validate variables. */
    info_trigger_print_all_thread_stacks(fp, timeout, cb_func, arg);
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

ABTU_ret_err static int print_all_thread_stacks(ABTI_global *p_global,
                                                FILE *fp);

#define PRINT_STACK_FLAG_UNSET 0
#define PRINT_STACK_FLAG_INITIALIZE 1
#define PRINT_STACK_FLAG_WAIT 2
#define PRINT_STACK_FLAG_FINALIZE 3

static ABTD_atomic_int print_stack_flag =
    ABTD_ATOMIC_INT_STATIC_INITIALIZER(PRINT_STACK_FLAG_UNSET);
static FILE *print_stack_fp = NULL;
static double print_stack_timeout = 0.0;
static void (*print_cb_func)(ABT_bool, void *) = NULL;
static void *print_arg = NULL;
static ABTD_atomic_int print_stack_barrier =
    ABTD_ATOMIC_INT_STATIC_INITIALIZER(0);

void ABTI_info_check_print_all_thread_stacks(void)
{
    if (ABTD_atomic_acquire_load_int(&print_stack_flag) !=
        PRINT_STACK_FLAG_WAIT)
        return;

    /* Wait for the other execution streams using a barrier mechanism. */
    int self_value = ABTD_atomic_fetch_add_int(&print_stack_barrier, 1);
    if (self_value == 0) {
        ABTI_global *p_global = ABTI_global_get_global();
        /* This ES becomes the main ES. */
        double start_time = ABTI_get_wtime();
        ABT_bool force_print = ABT_FALSE;

        /* xstreams_lock is acquired to avoid dynamic ES creation while
         * printing data. */
        ABTD_spinlock_acquire(&p_global->xstream_list_lock);
        while (1) {
            if (ABTD_atomic_acquire_load_int(&print_stack_barrier) >=
                p_global->num_xstreams) {
                break;
            }
            if (print_stack_timeout >= 0.0 &&
                (ABTI_get_wtime() - start_time) >= print_stack_timeout) {
                force_print = ABT_TRUE;
                break;
            }
            ABTD_spinlock_release(&p_global->xstream_list_lock);
            ABTD_atomic_pause();
            ABTD_spinlock_acquire(&p_global->xstream_list_lock);
        }
        /* All the available ESs are (supposed to be) stopped. We *assume* that
         * no ES is calling and will call Argobots functions except this
         * function while printing stack information. */
        if (force_print) {
            fprintf(print_stack_fp,
                    "ABT_info_trigger_print_all_thread_stacks: "
                    "timeout (only %d ESs stop)\n",
                    ABTD_atomic_acquire_load_int(&print_stack_barrier));
        }
        int abt_errno = print_all_thread_stacks(p_global, print_stack_fp);
        if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
            fprintf(print_stack_fp, "ABT_info_trigger_print_all_thread_stacks: "
                                    "failed because of an internal error.\n");
        }
        fflush(print_stack_fp);
        /* Release the lock that protects ES data. */
        ABTD_spinlock_release(&p_global->xstream_list_lock);
        if (print_cb_func)
            print_cb_func(force_print, print_arg);
        /* Update print_stack_flag to 3. */
        ABTD_atomic_release_store_int(&print_stack_flag,
                                      PRINT_STACK_FLAG_FINALIZE);
    } else {
        /* Wait for the main ES's work. */
        while (ABTD_atomic_acquire_load_int(&print_stack_flag) !=
               PRINT_STACK_FLAG_FINALIZE)
            ABTD_atomic_pause();
    }
    ABTI_ASSERT(ABTD_atomic_acquire_load_int(&print_stack_flag) ==
                PRINT_STACK_FLAG_FINALIZE);

    /* Decrement the barrier value. */
    int dec_value = ABTD_atomic_fetch_sub_int(&print_stack_barrier, 1);
    if (dec_value == 0) {
        /* The last execution stream resets the flag. */
        ABTD_atomic_release_store_int(&print_stack_flag,
                                      PRINT_STACK_FLAG_UNSET);
    }
}

void ABTI_info_print_config(ABTI_global *p_global, FILE *fp)
{
    fprintf(fp, "Argobots Configuration:\n");
    fprintf(fp, " - version: " ABT_VERSION "\n");
    fprintf(fp, " - # of cores: %d\n", p_global->num_cores);
    fprintf(fp, " - cache line size: %u B\n", ABT_CONFIG_STATIC_CACHELINE_SIZE);
    fprintf(fp, " - huge page size: %zu B\n", p_global->huge_page_size);
    fprintf(fp, " - max. # of ESs: %d\n", p_global->max_xstreams);
    fprintf(fp, " - cur. # of ESs: %d\n", p_global->num_xstreams);
    fprintf(fp, " - ES affinity: %s\n",
            (p_global->set_affinity == ABT_TRUE) ? "on" : "off");
    fprintf(fp, " - logging: %s\n",
            (p_global->use_logging == ABT_TRUE) ? "on" : "off");
    fprintf(fp, " - debug output: %s\n",
            (p_global->use_debug == ABT_TRUE) ? "on" : "off");
    fprintf(fp, " - print errno: "
#ifdef ABT_CONFIG_PRINT_ABT_ERRNO
                "on"
#else
                "off"
#endif
                "\n");
    fprintf(fp, " - valgrind support: "
#ifdef HAVE_VALGRIND_SUPPORT
                "yes"
#else
                "no"
#endif
                "\n");
    fprintf(fp, " - thread cancellation: "
#ifndef ABT_CONFIG_DISABLE_CANCELLATION
                "enabled"
#else
                "disabled"
#endif
                "\n");
    fprintf(fp, " - thread migration: "
#ifndef ABT_CONFIG_DISABLE_MIGRATION
                "enabled"
#else
                "disabled"
#endif
                "\n");
    fprintf(fp, " - external thread: "
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
                "enabled"
#else
                "disabled"
#endif
                "\n");
    fprintf(fp, " - error check: "
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
                "enabled"
#else
                "disable"
#endif
                "\n");
    fprintf(fp, " - tool interface: "
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
                "yes"
#else
                "no"
#endif
                "\n");
    fprintf(fp, " - wait policy: "
#ifdef ABT_CONFIG_ACTIVE_WAIT_POLICY
                "active"
#else
                "passive"
#endif
                "\n");
    fprintf(fp, " - context-switch: "
#ifdef ABT_CONFIG_USE_FCONTEXT
                "fcontext"
#ifndef ABTD_FCONTEXT_PRESERVE_FPU
                " (no FPU save)"
#endif
#else  /* ABT_CONFIG_USE_FCONTEXT */
                "ucontext"
#endif /* !ABT_CONFIG_USE_FCONTEXT */
                "\n");

    fprintf(fp, " - key table entries: %" PRIu32 "\n",
            p_global->key_table_size);
    fprintf(fp, " - default ULT stack size: %zu KB\n",
            p_global->thread_stacksize / 1024);
    fprintf(fp, " - default scheduler stack size: %zu KB\n",
            p_global->sched_stacksize / 1024);
    fprintf(fp, " - default scheduler event check frequency: %u\n",
            p_global->sched_event_freq);
    fprintf(fp, " - default scheduler sleep: "
#ifdef ABT_CONFIG_USE_SCHED_SLEEP
                "on"
#else
                "off"
#endif
                "\n");
    fprintf(fp, " - default scheduler sleep duration : %" PRIu64 " [ns]\n",
            p_global->sched_sleep_nsec);

    fprintf(fp, " - timer function: "
#if defined(ABT_CONFIG_USE_CLOCK_GETTIME)
                "clock_gettime"
#elif defined(ABT_CONFIG_USE_MACH_ABSOLUTE_TIME)
                "mach_absolute_time"
#elif defined(ABT_CONFIG_USE_GETTIMEOFDAY)
                "gettimeofday"
#endif
                "\n");

#ifdef ABT_CONFIG_USE_MEM_POOL
    fprintf(fp, "Memory Pool:\n");
    fprintf(fp, " - page size for allocation: %zu KB\n",
            p_global->mem_page_size / 1024);
    fprintf(fp, " - stack page size: %zu KB\n", p_global->mem_sp_size / 1024);
    fprintf(fp, " - max. # of stacks per ES: %u\n", p_global->mem_max_stacks);
    fprintf(fp, " - max. # of descs per ES: %u\n", p_global->mem_max_descs);
    switch (p_global->mem_lp_alloc) {
        case ABTI_MEM_LP_MALLOC:
            fprintf(fp, " - large page allocation: malloc\n");
            break;
        case ABTI_MEM_LP_MMAP_RP:
            fprintf(fp, " - large page allocation: mmap regular pages\n");
            break;
        case ABTI_MEM_LP_MMAP_HP_RP:
            fprintf(fp, " - large page allocation: mmap huge pages + "
                        "regular pages\n");
            break;
        case ABTI_MEM_LP_MMAP_HP_THP:
            fprintf(fp, " - large page allocation: mmap huge pages + THPs\n");
            break;
        case ABTI_MEM_LP_THP:
            fprintf(fp, " - large page allocation: THPs\n");
            break;
    }
#endif /* ABT_CONFIG_USE_MEM_POOL */

    fflush(fp);
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

struct info_print_unit_arg_t {
    ABTI_global *p_global;
    FILE *fp;
};

struct info_pool_set_t {
    ABT_pool *pools;
    size_t num;
    size_t len;
};

static void info_print_unit(void *arg, ABT_thread thread)
{
    /* This function may not have any side effect on unit because it is passed
     * to p_print_all. */
    struct info_print_unit_arg_t *p_arg;
    p_arg = (struct info_print_unit_arg_t *)arg;
    FILE *fp = p_arg->fp;
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);

    if (!p_thread) {
        fprintf(fp, "=== unknown (%p) ===\n", (void *)thread);
    } else if (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) {
        fprintf(fp, "=== ULT (%p) ===\n", (void *)thread);
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        ABT_unit_id thread_id = ABTI_thread_get_id(&p_ythread->thread);
        fprintf(fp,
                "id        : %" PRIu64 "\n"
                "ctx       : %p\n",
                (uint64_t)thread_id, (void *)&p_ythread->ctx);
        ABTI_ythread_print_stack(p_arg->p_global, p_ythread, fp);
    } else {
        fprintf(fp, "=== tasklet (%p) ===\n", (void *)thread);
    }
}

ABTU_ret_err static int info_print_thread_stacks_in_pool(ABTI_global *p_global,
                                                         FILE *fp,
                                                         ABTI_pool *p_pool)
{
    if (p_pool == NULL) {
        fprintf(fp, "== NULL pool ==\n");
        fflush(fp);
        return ABT_SUCCESS;
    }
    ABTI_CHECK_TRUE(p_pool->optional_def.p_print_all, ABT_ERR_POOL);

    ABT_pool pool = ABTI_pool_get_handle(p_pool);

    fprintf(fp, "== pool (%p) ==\n", (void *)p_pool);
    struct info_print_unit_arg_t arg;
    arg.p_global = p_global;
    arg.fp = fp;
    p_pool->optional_def.p_print_all(pool, &arg, info_print_unit);
    fflush(fp);
    return ABT_SUCCESS;
}

ABTU_ret_err static inline int
info_initialize_pool_set(struct info_pool_set_t *p_set)
{
    size_t default_len = 16;
    int abt_errno =
        ABTU_malloc(sizeof(ABT_pool) * default_len, (void **)&p_set->pools);
    ABTI_CHECK_ERROR(abt_errno);
    p_set->num = 0;
    p_set->len = default_len;
    return ABT_SUCCESS;
}

static inline void info_finalize_pool_set(struct info_pool_set_t *p_set)
{
    ABTU_free(p_set->pools);
}

ABTU_ret_err static inline int info_add_pool_set(ABT_pool pool,
                                                 struct info_pool_set_t *p_set)
{
    size_t i;
    for (i = 0; i < p_set->num; i++) {
        if (p_set->pools[i] == pool)
            return ABT_SUCCESS;
    }
    /* Add pool to p_set. */
    if (p_set->num == p_set->len) {
        size_t new_len = p_set->len * 2;
        int abt_errno =
            ABTU_realloc(sizeof(ABT_pool) * p_set->len,
                         sizeof(ABT_pool) * new_len, (void **)&p_set->pools);
        ABTI_CHECK_ERROR(abt_errno);
        p_set->len = new_len;
    }
    p_set->pools[p_set->num++] = pool;
    return ABT_SUCCESS;
}

static void info_trigger_print_all_thread_stacks(
    FILE *fp, double timeout, void (*cb_func)(ABT_bool, void *), void *arg)
{
    /* This function is signal-safe, so it may not call other functions unless
     * you really know what the called functions do. */
    if (ABTD_atomic_acquire_load_int(&print_stack_flag) ==
        PRINT_STACK_FLAG_UNSET) {
        if (ABTD_atomic_bool_cas_strong_int(&print_stack_flag,
                                            PRINT_STACK_FLAG_UNSET,
                                            PRINT_STACK_FLAG_INITIALIZE)) {
            /* Save fp and timeout. */
            print_stack_fp = fp;
            print_stack_timeout = timeout;
            print_cb_func = cb_func;
            print_arg = arg;
            /* Here print_stack_barrier must be 0. */
            ABTI_ASSERT(ABTD_atomic_acquire_load_int(&print_stack_barrier) ==
                        0);
            ABTD_atomic_release_store_int(&print_stack_flag,
                                          PRINT_STACK_FLAG_WAIT);
        }
    }
}

ABTU_ret_err static int print_all_thread_stacks(ABTI_global *p_global, FILE *fp)
{
    size_t i;
    int abt_errno;
    struct info_pool_set_t pool_set;
    struct tm *tm = NULL;
    time_t seconds;

    abt_errno = info_initialize_pool_set(&pool_set);
    ABTI_CHECK_ERROR(abt_errno);
    ABTI_xstream *p_xstream = p_global->p_xstream_head;

    seconds = (time_t)ABT_get_wtime();
    tm = localtime(&seconds);
    ABTI_ASSERT(tm != NULL);
    fprintf(fp, "Start of ULT stacks dump %04d/%02d/%02d-%02d:%02d:%02d\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
            tm->tm_min, tm->tm_sec);

    while (p_xstream) {
        ABTI_sched *p_main_sched = p_xstream->p_main_sched;
        fprintf(fp, "= xstream[%d] (%p) =\n", p_xstream->rank,
                (void *)p_xstream);
        fprintf(fp, "main_sched : %p\n", (void *)p_main_sched);
        if (!p_main_sched)
            continue;
        for (i = 0; i < p_main_sched->num_pools; i++) {
            ABT_pool pool = p_main_sched->pools[i];
            ABTI_ASSERT(pool != ABT_POOL_NULL);
            fprintf(fp, "  pools[%zu] : %p\n", i,
                    (void *)ABTI_pool_get_ptr(pool));
            abt_errno = info_add_pool_set(pool, &pool_set);
            if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
                info_finalize_pool_set(&pool_set);
                ABTI_HANDLE_ERROR(abt_errno);
            }
        }
        p_xstream = p_xstream->p_next;
    }
    for (i = 0; i < pool_set.num; i++) {
        ABT_pool pool = pool_set.pools[i];
        ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
        abt_errno = info_print_thread_stacks_in_pool(p_global, fp, p_pool);
        if (abt_errno != ABT_SUCCESS)
            fprintf(fp, "  Failed to print (errno = %d).\n", abt_errno);
    }

    seconds = (time_t)ABT_get_wtime();
    tm = localtime(&seconds);
    ABTI_ASSERT(tm != NULL);
    fprintf(fp, "End of ULT stacks dump %04d/%02d/%02d-%02d:%02d:%02d\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
            tm->tm_min, tm->tm_sec);

    info_finalize_pool_set(&pool_set);
    return ABT_SUCCESS;
}
