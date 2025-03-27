/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_EVENT_H_INCLUDED
#define ABTI_EVENT_H_INCLUDED

#if !defined(ABT_CONFIG_DISABLE_TOOL_INTERFACE) ||                             \
    defined(ABT_CONFIG_USE_DEBUG_LOG)
#define ABTI_ENABLE_EVENT_INTERFACE 1
#else
#define ABTI_ENABLE_EVENT_INTERFACE 0
#endif

static inline void ABTI_event_thread_create_impl(ABTI_local *p_local,
                                                 ABTI_thread *p_thread,
                                                 ABTI_thread *p_caller,
                                                 ABTI_pool *p_pool)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("create", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(p_local, ABT_TOOL_EVENT_THREAD_CREATE, p_thread,
                           p_caller, p_pool, NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                           NULL);
#endif
}

static inline void ABTI_event_thread_join_impl(ABTI_local *p_local,
                                               ABTI_thread *p_thread,
                                               ABTI_thread *p_caller)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("join", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(p_local, ABT_TOOL_EVENT_THREAD_JOIN, p_thread,
                           p_caller, NULL, NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                           NULL);
#endif
}

static inline void ABTI_event_thread_free_impl(ABTI_local *p_local,
                                               ABTI_thread *p_thread,
                                               ABTI_thread *p_caller)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("free", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(p_local, ABT_TOOL_EVENT_THREAD_FREE, p_thread,
                           p_caller, NULL, NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                           NULL);
#endif
}

static inline void ABTI_event_thread_revive_impl(ABTI_local *p_local,
                                                 ABTI_thread *p_thread,
                                                 ABTI_thread *p_caller,
                                                 ABTI_pool *p_pool)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("revive", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(p_local, ABT_TOOL_EVENT_THREAD_REVIVE, p_thread,
                           p_caller, p_pool, NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                           NULL);
#endif
}

static inline void ABTI_event_thread_run_impl(ABTI_xstream *p_local_xstream,
                                              ABTI_thread *p_thread,
                                              ABTI_thread *p_prev,
                                              ABTI_thread *p_parent)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("run", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(ABTI_xstream_get_local(p_local_xstream),
                           ABT_TOOL_EVENT_THREAD_RUN, p_thread, p_prev, NULL,
                           p_parent, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
#endif
}

static inline void ABTI_event_thread_finish_impl(ABTI_xstream *p_local_xstream,
                                                 ABTI_thread *p_thread,
                                                 ABTI_thread *p_parent)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("finish", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(ABTI_xstream_get_local(p_local_xstream),
                           ABT_TOOL_EVENT_THREAD_FINISH, p_thread, NULL, NULL,
                           p_parent, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
#endif
}

static inline void ABTI_event_thread_cancel_impl(ABTI_xstream *p_local_xstream,
                                                 ABTI_thread *p_thread)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("cancel", p_thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(ABTI_xstream_get_local(p_local_xstream),
                           ABT_TOOL_EVENT_THREAD_CANCEL, p_thread, NULL, NULL,
                           NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
#endif
}

static inline void
ABTI_event_ythread_yield_impl(ABTI_xstream *p_local_xstream,
                              ABTI_ythread *p_ythread, ABTI_thread *p_parent,
                              ABT_sync_event_type sync_event_type, void *p_sync)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("yield", &p_ythread->thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(ABTI_xstream_get_local(p_local_xstream),
                           ABT_TOOL_EVENT_THREAD_YIELD, &p_ythread->thread,
                           NULL, p_ythread->thread.p_pool, p_parent,
                           sync_event_type, p_sync);
#endif
}

static inline void ABTI_event_ythread_suspend_impl(
    ABTI_xstream *p_local_xstream, ABTI_ythread *p_ythread,
    ABTI_thread *p_parent, ABT_sync_event_type sync_event_type, void *p_sync)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("suspend", &p_ythread->thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(ABTI_xstream_get_local(p_local_xstream),
                           ABT_TOOL_EVENT_THREAD_SUSPEND, &p_ythread->thread,
                           NULL, p_ythread->thread.p_pool, p_parent,
                           sync_event_type, p_sync);
#endif
}

static inline void ABTI_event_ythread_resume_impl(ABTI_local *p_local,
                                                  ABTI_ythread *p_ythread,
                                                  ABTI_thread *p_caller)
{
#ifdef ABT_CONFIG_USE_DEBUG_LOG
    ABTI_log_debug_thread("resume", &p_ythread->thread);
#endif
#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    ABTI_tool_event_thread(p_local, ABT_TOOL_EVENT_THREAD_RESUME,
                           &p_ythread->thread, p_caller,
                           p_ythread->thread.p_pool, NULL,
                           ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
#endif
}

#define ABTI_event_thread_create(p_local, p_thread, p_caller, p_pool)          \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_create_impl(p_local, p_thread, p_caller,         \
                                          p_pool);                             \
        }                                                                      \
    } while (0)

#define ABTI_event_thread_join(p_local, p_thread, p_caller)                    \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_join_impl(p_local, p_thread, p_caller);          \
        }                                                                      \
    } while (0)

#define ABTI_event_thread_free(p_local, p_thread, p_caller)                    \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_free_impl(p_local, p_thread, p_caller);          \
        }                                                                      \
    } while (0)

#define ABTI_event_thread_revive(p_local, p_thread, p_caller, p_pool)          \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_revive_impl(p_local, p_thread, p_caller,         \
                                          p_pool);                             \
        }                                                                      \
    } while (0)

#define ABTI_event_thread_run(p_local_xstream, p_thread, p_prev, p_parent)     \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_run_impl(p_local_xstream, p_thread, p_prev,      \
                                       p_parent);                              \
        }                                                                      \
    } while (0)

#define ABTI_event_thread_finish(p_local_xstream, p_thread, p_parent)          \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_finish_impl(p_local_xstream, p_thread,           \
                                          p_parent);                           \
        }                                                                      \
    } while (0)

#define ABTI_event_thread_cancel(p_local_xstream, p_thread)                    \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_thread_cancel_impl(p_local_xstream, p_thread);          \
        }                                                                      \
    } while (0)

#define ABTI_event_ythread_yield(p_local_xstream, p_ythread, p_parent,         \
                                 sync_event_type, p_sync)                      \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_ythread_yield_impl(p_local_xstream, p_ythread,          \
                                          p_parent, sync_event_type, p_sync);  \
        }                                                                      \
    } while (0)

#define ABTI_event_ythread_suspend(p_local_xstream, p_ythread, p_parent,       \
                                   sync_event_type, p_sync)                    \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_ythread_suspend_impl(p_local_xstream, p_ythread,        \
                                            p_parent, sync_event_type,         \
                                            p_sync);                           \
        }                                                                      \
    } while (0)

#define ABTI_event_ythread_resume(p_local, p_ythread, p_caller)                \
    do {                                                                       \
        if (ABTI_ENABLE_EVENT_INTERFACE) {                                     \
            ABTI_event_ythread_resume_impl(p_local, p_ythread, p_caller);      \
        }                                                                      \
    } while (0)

#endif /* ABTI_EVENT_H_INCLUDED */
