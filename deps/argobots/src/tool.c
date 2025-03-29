/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
ABTU_ret_err static inline int tool_query(ABTI_tool_context *p_tctx,
                                          ABT_tool_query_kind query_kind,
                                          void *val);
#endif

/** @defgroup TOOL Tool
 * This group is for Tool.
 */

/**
 * @ingroup TOOL
 * @brief   Register a callback function for work-unit events.
 *
 * \c ABT_tool_register_thread_callback() registers the callback function
 * \c cb_func() for work-unit events.  The events are enabled if \c event_mask
 * have the corresponding bits.  The other events are disabled.  The routine
 * unregisters the callback function if \c cb_func is \c NULL.
 *
 * \c cb_func() is called with the following arguments:
 *
 * - The first argument: a work unit that triggers the event
 * - The second argument: an underlying execution stream
 * - The third argument: an event code (see \c ABT_TOOL_EVENT_THREAD)
 * - The fourth argument: a tool context for \c ABT_tool_query_thread()
 * - The fifth argument: \c user_arg passed to this routine
 *
 * If an event occurs on an external thread, \c ABT_XSTREAM_NULL is passed as
 * the second argument.  The returned tool context is valid only in the callback
 * function.
 *
 * An object referenced by the returned handle (e.g., a work unit handle) may be
 * in an intermediate state, so the user should not read any internal state of
 * such an object (e.g., by \c ABT_thread_get_state()) in \c cb_func().
 * Instead, the user should use \c ABT_tool_query_thread().  The caller of
 * \c cb_func() might be neither a work unit that triggers the event nor a work
 * unit that is running on the same execution stream.  A program that relies on
 * the caller of \c cb_func() is non-conforming.
 *
 * This routine can be called while other work-unit events are being triggered.
 * This routine atomically registers \c cb_func(), \c event_mask, and
 * \c user_arg at the same time.
 *
 * @note
 * Invoking an event in \c cb_func() may cause an infinite invocation of
 * \c cb_func().  It is the user's responsibility to take a proper measure to
 * avoid it.
 *
 * \DOC_DESC_ATOMICITY_TOOL_CALLBACK_REGISTRATION
 *
 * @note
 * Even after \c ABT_tool_register_thread_callback() returns, another event call
 * might not have finished.  If so, the previous \c cb_func() might be using the
 * previous \c user_arg.  Argobots does not provide a method to guarantee that
 * the previous \c cb_func() and \c user_arg get unused.  Hence, the user needs
 * to carefully maintain consistency before and after
 * \c ABT_tool_register_thread_callback().
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_FEATURE_NA{the tool feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_TOOL_CALLBACK{\c cb_func()}
 * \DOC_UNDEFINED_CHANGE_STATE{\c cb_func()}
 *
 * @param[in]  cb_func     callback function pointer
 * @param[in]  event_mask  event code mask
 * @param[in]  user_arg    user argument passed to \c cb_func
 * @return Error code
 */
int ABT_tool_register_thread_callback(ABT_tool_thread_callback_fn cb_func,
                                      uint64_t event_mask, void *user_arg)
{
    ABTI_UB_ASSERT(ABTI_initialized());

#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#else
    ABTI_global *p_global;
    ABTI_SETUP_GLOBAL(&p_global);

    if (cb_func == NULL)
        event_mask = ABT_TOOL_EVENT_THREAD_NONE;
    ABTI_tool_event_thread_update_callback(p_global, cb_func,
                                           event_mask &
                                               ABT_TOOL_EVENT_THREAD_ALL,
                                           user_arg);
    return ABT_SUCCESS;
#endif
}

/**
 * @ingroup TOOL
 * @brief   Query information associated with a work-unit event.
 *
 * \c ABT_tool_query_thread() returns information associated with the tool
 * context \c context through \c val.  Because \c context is valid only in the
 * callback function, this function must be called in the callback function.
 *
 * When \c query_kind is \c ABT_TOOL_QUERY_KIND_POOL, \c val must be a pointer
 * to a variable of type \c ABT_pool.  This routine sets \c val to a handle of
 * a pool to which a work unit is or will be pushed.  This query is valid when
 * \c event is \c THREAD_CREATE, \c THREAD_REVIVE, \c THREAD_YIELD,
 * or \c THREAD_RESUME.
 *
 * When \c query_kind is \c ABT_TOOL_QUERY_KIND_STACK_DEPTH, \c val must be a
 * pointer to a variable of type \c int.  This routine sets \c val to the
 * current depth of stacked work units while the level of the work unit
 * associated with the main scheduler is zero.  For example, if the current
 * work unit is running directly on the main scheduler, the depth is \a 1.  This
 * query is valid when \c event is \c THREAD_RUN (the depth after the work unit
 * runs), \c THREAD_FINISH (the depth before the work unit finishes),
 * \c THREAD_YIELD (the depth before the work unit yields), or
 * \c THREAD_SUSPEND (the depth before the work unit suspends).
 *
 * When \c query_kind is \c ABT_TOOL_QUERY_KIND_CALLER_TYPE, \c val must be a
 * pointer to a variable of type \c ABT_exec_entity_type.  This routine sets
 * \c val to a type of an entity that incurs this event.  This query is valid
 * for all events.
 *
 * When \c query_kind is \c ABT_TOOL_QUERY_KIND_CALLER_HANDLE, \c val must be a
 * pointer to a variable of a handle type of an entity that incurs this event.
 * This routine sets \c val to a handle of an entity that incurs this event.
 * Specifically, this routine sets \c val to a work unit handle (\c ABT_thread)
 * if the caller type is \c ABT_EXEC_ENTITY_TYPE_THREAD.  If the caller is an
 * external thread, this routine sets \c val to \c NULL.  The query is valid for
 * all events except for \c THREAD_CANCEL.  Note that the caller is the previous
 * work unit running on the same execution stream when \c event is
 * \c THRAED_RUN.
 *
 * When \c query_kind is \c ABT_TOOL_QUERY_KIND_SYNC_OBJECT_TYPE, \c val must be
 * a pointer to a variable of type \c ABT_sync_event_type.  This routine sets
 * \c val to a type of the synchronization object that incurs this event.  This
 * query is valid when \c event is \c THREAD_YIELD or \c THREAD_SUSPEND.
 *
 * When \c query_kind is \c ABT_TOOL_QUERY_KIND_SYNC_OBJECT_HANDLE, \c val must
 * be a pointer to a variable of a handle type of the synchronization object
 * that incurs this event.  This routine sets \c val to a handle of the
 * synchronization object that incurs this event.  This query is valid when
 * \c event is \c THREAD_YIELD or \c THREAD_SUSPEND.
 *
 * Synchronization events, \c ABT_sync_event_type, and synchronization objects
 * are mapped as follows:
 *
 *  - \c ABT_SYNC_EVENT_TYPE_USER:
 *
 *    A user's explicit call (e.g., \c ABT_thread_yield()).  The synchronization
 *    object is none, so \c NULL is set to \c val if
 *    \c ABT_TOOL_QUERY_KIND_SYNC_OBJECT_HANDLE is passed.
 *
 *  - \c ABT_SYNC_EVENT_TYPE_XSTREAM_JOIN:
 *
 *    Waiting for completion of execution streams (e.g., \c ABT_xstream_join()).
 *    The synchronization object is an execution stream (\c ABT_xstream).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_THREAD_JOIN:
 *
 *    Waiting for completion of a work unit (e.g., \c ABT_thread_join() or
 *    \c ABT_task_join()).  The synchronization object is a work unit
 *    (\c ABT_thread).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_MUTEX:
 *
 *    Synchronization regarding a mutex (e.g., \c ABT_mutex_lock()).  The
 *    synchronization object is a mutex (\c ABT_mutex).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_COND:
 *
 *    Synchronization regarding a condition variable (e.g., \c ABT_cond_wait()).
 *    The synchronization object is a condition variable (\c ABT_cond).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_RWLOCK:
 *
 *    Synchronization regarding a readers-writer lock (e.g.,
 *    \c ABT_rwlock_rdlock()).  The synchronization object is a readers-writer
 *    lock (\c ABT_rwlock).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_EVENTUAL:
 *
 *    Synchronization regarding an eventual (e.g., \c ABT_eventual_wait()).  The
 *    synchronization object is an eventual (\c ABT_eventual).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_FUTURE:
 *
 *    Synchronization regarding a future (e.g., \c ABT_future_wait()).  The
 *    synchronization object is a future (\c ABT_future).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_BARRIER:
 *
 *    Synchronization regarding a barrier (e.g., \c ABT_barrier_wait()).  The
 *    synchronization object is a barrier (\c ABT_barrier).
 *
 *  - \c ABT_SYNC_EVENT_TYPE_OTHER:
 *
 *    Other synchronization (e.g., \c ABT_xstream_exit()).  The synchronization
 *    object is none, so \c NULL is set to \c val if
 *    \c ABT_TOOL_QUERY_KIND_SYNC_OBJECT_HANDLE is passed.
 *
 * An object referenced by the returned handle (e.g., the work unit handle) may
 * be in an intermediate state, so the user should not to read any internal
 * state of such an object (e.g., by \c ABT_thread_get_state()) in \c cb_func().
 *
 * @contexts
 * \DOC_CONTEXT_TOOL_CALLBACK \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_ARG_INV_TOOL_QUERY_KIND{\c query_kind, \c query}
 * \DOC_ERROR_FEATURE_NA{the tool feature}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c val}
 * \DOC_UNDEFINED_CHANGE_STATE{the callback function}
 * \DOC_UNDEFINED_TOOL_QUERY{\c context, \c event}
 *
 * @param[in]  context     tool context handle
 * @param[in]  event       event code passed to the callback function
 * @param[in]  query_kind  query kind
 * @param[out] val         pointer to storage where a returned value is saved
 * @return Error code
 */
int ABT_tool_query_thread(ABT_tool_context context, uint64_t event,
                          ABT_tool_query_kind query_kind, void *val)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(val);

#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_HANDLE_ERROR(ABT_ERR_FEATURE_NA);
#else
    ABTI_tool_context *p_tctx = ABTI_tool_context_get_ptr(context);
    ABTI_CHECK_NULL_TOOL_CONTEXT_PTR(p_tctx);

    int abt_errno = tool_query(p_tctx, query_kind, val);
    ABTI_CHECK_ERROR(abt_errno);
    return ABT_SUCCESS;
#endif
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
ABTU_ret_err static inline int
tool_query(ABTI_tool_context *p_tctx, ABT_tool_query_kind query_kind, void *val)
{
    switch (query_kind) {
        case ABT_TOOL_QUERY_KIND_POOL:
            *(ABT_pool *)val = ABTI_pool_get_handle(p_tctx->p_pool);
            break;
        case ABT_TOOL_QUERY_KIND_STACK_DEPTH:
            if (!p_tctx->p_parent) {
                *(int *)val = 0;
            } else {
                int depth = 0;
                ABTI_thread *p_cur = p_tctx->p_parent;
                while (p_cur) {
                    depth++;
                    p_cur = p_cur->p_parent;
                }
                /* We do not count the root thread, so -1. */
                *(int *)val = depth - 1;
            }
            break;
        case ABT_TOOL_QUERY_KIND_CALLER_TYPE:
            if (!p_tctx->p_caller) {
                *(ABT_exec_entity_type *)val = ABT_EXEC_ENTITY_TYPE_EXT;
            } else {
                *(ABT_exec_entity_type *)val = ABT_EXEC_ENTITY_TYPE_THREAD;
            }
            break;
        case ABT_TOOL_QUERY_KIND_CALLER_HANDLE:
            if (!p_tctx->p_caller) {
                *(void **)val = NULL;
            } else {
                *(ABT_thread *)val = ABTI_thread_get_handle(p_tctx->p_caller);
            }
            break;
        case ABT_TOOL_QUERY_KIND_SYNC_OBJECT_TYPE:
            *(ABT_sync_event_type *)val = p_tctx->sync_event_type;
            break;
        case ABT_TOOL_QUERY_KIND_SYNC_OBJECT_HANDLE:
            switch (p_tctx->sync_event_type) {
                case ABT_SYNC_EVENT_TYPE_XSTREAM_JOIN:
                    *(ABT_xstream *)val = ABTI_xstream_get_handle(
                        (ABTI_xstream *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_THREAD_JOIN:
                    *(ABT_thread *)val = ABTI_thread_get_handle(
                        (ABTI_thread *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_MUTEX:
                    *(ABT_mutex *)val = ABTI_mutex_get_handle(
                        (ABTI_mutex *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_COND:
                    *(ABT_cond *)val = ABTI_cond_get_handle(
                        (ABTI_cond *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_RWLOCK:
                    *(ABT_rwlock *)val = ABTI_rwlock_get_handle(
                        (ABTI_rwlock *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_EVENTUAL:
                    *(ABT_eventual *)val = ABTI_eventual_get_handle(
                        (ABTI_eventual *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_FUTURE:
                    *(ABT_future *)val = ABTI_future_get_handle(
                        (ABTI_future *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_BARRIER:
                    *(ABT_barrier *)val = ABTI_barrier_get_handle(
                        (ABTI_barrier *)p_tctx->p_sync_object);
                    break;
                default:
                    *(void **)val = NULL;
            }
            break;
        default:
            ABTI_HANDLE_ERROR(ABT_ERR_OTHER);
    }
    return ABT_SUCCESS;
}
#endif
