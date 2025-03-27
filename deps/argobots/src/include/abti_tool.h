/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_TOOL_H_INCLUDED
#define ABTI_TOOL_H_INCLUDED

static inline ABT_thread ABTI_ythread_get_handle(ABTI_ythread *p_thread);
static inline ABT_task ABTI_thread_get_handle(ABTI_thread *p_task);

#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
static inline ABTI_tool_context *
ABTI_tool_context_get_ptr(ABT_tool_context tctx)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_tool_context *p_tctx;
    if (tctx == ABT_TOOL_CONTEXT_NULL) {
        p_tctx = NULL;
    } else {
        p_tctx = (ABTI_tool_context *)tctx;
    }
    return p_tctx;
#else
    return (ABTI_tool_context *)tctx;
#endif
}

static inline ABT_tool_context
ABTI_tool_context_get_handle(ABTI_tool_context *p_tctx)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_tool_context h_tctx;
    if (p_tctx == NULL) {
        h_tctx = ABT_TOOL_CONTEXT_NULL;
    } else {
        h_tctx = (ABT_tool_context)p_tctx;
    }
    return h_tctx;
#else
    return (ABT_tool_context)p_tctx;
#endif
}

#define ABTI_TOOL_EVENT_TAG_SIZE 20 /* bits */
#define ABTI_TOOL_EVENT_TAG_MASK                                               \
    ((((uint64_t)1 << (uint64_t)ABTI_TOOL_EVENT_TAG_SIZE) - 1)                 \
     << (uint64_t)(64 - 1 - ABTI_TOOL_EVENT_TAG_SIZE))
#define ABTI_TOOL_EVENT_TAG_INC                                                \
    ((uint64_t)1 << (uint64_t)(64 - 1 - ABTI_TOOL_EVENT_TAG_SIZE))
#define ABTI_TOOL_EVENT_TAG_DIRTY_BIT ((uint64_t)1 << (uint64_t)(64 - 1))

static inline void
ABTI_tool_event_thread_update_callback(ABTI_global *p_global,
                                       ABT_tool_thread_callback_fn cb_func,
                                       uint64_t event_mask, void *user_arg)
{
    /* The spinlock is needed to avoid data race between two writers. */
    ABTD_spinlock_acquire(&p_global->tool_writer_lock);

    /*
     * This atomic writing process is needed to avoid data race between a reader
     * and a writer.  We need to atomically update three values (callback, event
     * mask, and user_arg) in the following cases:
     *
     * A. ES-W writes the three values while ES-R is reading the three values
     * B. ES-W1 writes and then ES-W2 writes the three values while ES-R is
     *    reading the three values
     *
     * The reader will first read the event mask and then load the other two.
     * The reader then read the event mask again and see if it is 1. the same as
     * the previous and 2. clean.  If both are satisfied, acquire-release memory
     * order guarantees that the loaded values are ones updated by the same
     * ABTI_tool_event_thread_update_callback() call, unless the tag value wraps
     * around (which does not happen practically).
     */

    uint64_t current = ABTD_atomic_acquire_load_uint64(
        &p_global->tool_thread_event_mask_tagged);
    uint64_t new_tag =
        (current + ABTI_TOOL_EVENT_TAG_INC) & ABTI_TOOL_EVENT_TAG_MASK;
    uint64_t new_mask = new_tag | ((event_mask & ABT_TOOL_EVENT_THREAD_ALL) &
                                   ~ABTI_TOOL_EVENT_TAG_DIRTY_BIT);
    uint64_t dirty_mask = ABTI_TOOL_EVENT_TAG_DIRTY_BIT | new_mask;

    ABTD_atomic_release_store_uint64(&p_global->tool_thread_event_mask_tagged,
                                     dirty_mask);
    p_global->tool_thread_cb_f = cb_func;
    p_global->tool_thread_user_arg = user_arg;
    ABTD_atomic_release_store_uint64(&p_global->tool_thread_event_mask_tagged,
                                     new_mask);

    ABTD_spinlock_release(&p_global->tool_writer_lock);
}

static inline void
ABTI_tool_event_thread(ABTI_local *p_local, uint64_t event_code,
                       ABTI_thread *p_thread, ABTI_thread *p_caller,
                       ABTI_pool *p_pool, ABTI_thread *p_parent,
                       ABT_sync_event_type sync_event_type, void *p_sync_object)
{
    if (p_thread->type & ABTI_THREAD_TYPE_ROOT)
        return; /* A root thread should not be exposed to the user. */
    ABTI_global *p_global = gp_ABTI_global;
    while (1) {
        uint64_t current_mask = ABTD_atomic_acquire_load_uint64(
            &p_global->tool_thread_event_mask_tagged);
        if (current_mask & event_code) {
            ABT_tool_thread_callback_fn cb_func_thread =
                p_global->tool_thread_cb_f;
            void *user_arg_thread = p_global->tool_thread_user_arg;
            /* Double check the current event mask. */
            uint64_t current_mask2 = ABTD_atomic_acquire_load_uint64(
                &p_global->tool_thread_event_mask_tagged);
            if (ABTU_unlikely(current_mask != current_mask2 ||
                              (current_mask & ABTI_TOOL_EVENT_TAG_DIRTY_BIT)))
                continue;
            ABTI_tool_context tctx;
            tctx.p_pool = p_pool;
            tctx.p_parent = p_parent;
            tctx.p_caller = p_caller;
            tctx.sync_event_type = sync_event_type;
            tctx.p_sync_object = p_sync_object;

            ABTI_xstream *p_local_xstream =
                ABTI_local_get_xstream_or_null(p_local);
            ABT_xstream h_xstream =
                p_local_xstream ? ABTI_xstream_get_handle(p_local_xstream)
                                : ABT_XSTREAM_NULL;
            ABT_thread h_thread = ABTI_thread_get_handle(p_thread);
            ABT_tool_context h_tctx = ABTI_tool_context_get_handle(&tctx);
            cb_func_thread(h_thread, h_xstream, event_code, h_tctx,
                           user_arg_thread);
        }
        return;
    }
}

#endif /* !ABT_CONFIG_DISABLE_TOOL_INTERFACE */

#endif /* ABTI_TOOL_H_INCLUDED */
