/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_THREAD_H_INCLUDED
#define ABTI_THREAD_H_INCLUDED

static inline ABTI_thread *ABTI_thread_get_ptr(ABT_thread thread)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_thread *p_thread;
    if (thread == ABT_THREAD_NULL || thread == ABT_TASK_NULL) {
        p_thread = NULL;
    } else {
        p_thread = (ABTI_thread *)thread;
    }
    return p_thread;
#else
    return (ABTI_thread *)thread;
#endif
}

static inline ABT_thread ABTI_thread_get_handle(ABTI_thread *p_thread)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_thread h_thread;
    if (p_thread == NULL) {
        h_thread = ABT_THREAD_NULL;
    } else {
        h_thread = (ABT_thread)p_thread;
    }
    return h_thread;
#else
    return (ABT_thread)p_thread;
#endif
}

/* Inlined functions for User-level Thread (ULT) */

static inline ABT_unit_type ABTI_thread_type_get_type(ABTI_thread_type type)
{
    if (type & ABTI_THREAD_TYPE_YIELDABLE) {
        return ABT_UNIT_TYPE_THREAD;
    } else if (type == ABTI_THREAD_TYPE_EXT) {
        return ABT_UNIT_TYPE_EXT;
    } else {
        return ABT_UNIT_TYPE_TASK;
    }
}

static inline ABTI_ythread *ABTI_thread_get_ythread(ABTI_thread *p_thread)
{
    ABTI_STATIC_ASSERT(offsetof(ABTI_ythread, thread) == 0);
    return (ABTI_ythread *)p_thread;
}

static inline ABTI_ythread *
ABTI_thread_get_ythread_or_null(ABTI_thread *p_thread)
{
    if (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) {
        return ABTI_thread_get_ythread(p_thread);
    } else {
        return NULL;
    }
}

static inline void ABTI_thread_set_request(ABTI_thread *p_thread, uint32_t req)
{
    ABTD_atomic_fetch_or_uint32(&p_thread->request, req);
}

static inline void ABTI_thread_unset_request(ABTI_thread *p_thread,
                                             uint32_t req)
{
    ABTD_atomic_fetch_and_uint32(&p_thread->request, ~req);
}

#define ABTI_THREAD_HANDLE_REQUEST_NONE ((int)0x0)
#define ABTI_THREAD_HANDLE_REQUEST_CANCELLED ((int)0x1)
#define ABTI_THREAD_HANDLE_REQUEST_MIGRATED ((int)0x2)

static inline int ABTI_thread_handle_request(ABTI_thread *p_thread,
                                             ABT_bool allow_termination)
{
#if defined(ABT_CONFIG_DISABLE_CANCELLATION) &&                                \
    defined(ABT_CONFIG_DISABLE_MIGRATION)
    return ABTI_THREAD_HANDLE_REQUEST_NONE;
#else
    /* At least either cancellation or migration is enabled. */
    const uint32_t request =
        ABTD_atomic_acquire_load_uint32(&p_thread->request);

    /* Check cancellation request. */
#ifndef ABT_CONFIG_DISABLE_CANCELLATION
    if (allow_termination && ABTU_unlikely(request & ABTI_THREAD_REQ_CANCEL)) {
        ABTI_thread_handle_request_cancel(ABTI_global_get_global(),
                                          p_thread->p_last_xstream, p_thread);
        return ABTI_THREAD_HANDLE_REQUEST_CANCELLED;
    }
#endif /* !ABT_CONFIG_DISABLE_CANCELLATION */

    /* Check migration request. */
#ifndef ABT_CONFIG_DISABLE_MIGRATION
    if (ABTU_unlikely(request & ABTI_THREAD_REQ_MIGRATE)) {
        /* This is the case when the ULT requests migration of itself. */
        int abt_errno =
            ABTI_thread_handle_request_migrate(ABTI_global_get_global(),
                                               ABTI_xstream_get_local(
                                                   p_thread->p_last_xstream),
                                               p_thread);
        if (abt_errno == ABT_SUCCESS) {
            return ABTI_THREAD_HANDLE_REQUEST_MIGRATED;
        }
        /* Migration failed. */
    }
#endif /* !ABT_CONFIG_DISABLE_MIGRATION */

    return ABTI_THREAD_HANDLE_REQUEST_NONE;
#endif
}

ABTU_ret_err static inline int
ABTI_mem_alloc_ythread_mempool_stack(ABTI_xstream *p_local_xstream,
                                     ABTI_ythread *p_ythread);
static inline void
ABTI_mem_free_ythread_mempool_stack(ABTI_xstream *p_local_xstream,
                                    ABTI_ythread *p_ythread);

static inline void ABTI_thread_terminate(ABTI_global *p_global,
                                         ABTI_xstream *p_local_xstream,
                                         ABTI_thread *p_thread)
{
    const ABTI_thread_type thread_type = p_thread->type;
    if (thread_type & (ABTI_THREAD_TYPE_MEM_MEMPOOL_DESC_MEMPOOL_LAZY_STACK |
                       ABTI_THREAD_TYPE_MEM_MALLOC_DESC_MEMPOOL_LAZY_STACK)) {
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread(p_thread);
        if (ABTD_ythread_context_has_stack(&p_ythread->ctx)) {
            ABTI_mem_free_ythread_mempool_stack(p_local_xstream, p_ythread);
        }
    }
    if (!(thread_type & ABTI_THREAD_TYPE_NAMED)) {
        ABTD_atomic_release_store_int(&p_thread->state,
                                      ABT_THREAD_STATE_TERMINATED);
        ABTI_thread_free(p_global, ABTI_xstream_get_local(p_local_xstream),
                         p_thread);
    } else {
        /* NOTE: We set the ULT's state as TERMINATED after checking refcount
         * because the ULT can be freed on a different ES.  In other words, we
         * must not access any field of p_thead after changing the state to
         * TERMINATED. */
        ABTD_atomic_release_store_int(&p_thread->state,
                                      ABT_THREAD_STATE_TERMINATED);
    }
}

#endif /* ABTI_THREAD_H_INCLUDED */
