/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_YTHREAD_H_INCLUDED
#define ABTI_YTHREAD_H_INCLUDED

/* Inlined functions for yieldable threads */

static inline ABTI_ythread *ABTI_ythread_get_ptr(ABT_thread thread)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_ythread *p_ythread;
    if (thread == ABT_THREAD_NULL) {
        p_ythread = NULL;
    } else {
        p_ythread = (ABTI_ythread *)thread;
    }
    return p_ythread;
#else
    return (ABTI_ythread *)thread;
#endif
}

static inline ABT_thread ABTI_ythread_get_handle(ABTI_ythread *p_ythread)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_thread h_thread;
    if (p_ythread == NULL) {
        h_thread = ABT_THREAD_NULL;
    } else {
        h_thread = (ABT_thread)p_ythread;
    }
    return h_thread;
#else
    return (ABT_thread)p_ythread;
#endif
}

static inline void ABTI_ythread_resume_and_push(ABTI_local *p_local,
                                                ABTI_ythread *p_ythread)
{
    /* The ULT must be in BLOCKED state. */
    ABTI_ASSERT(ABTD_atomic_acquire_load_int(&p_ythread->thread.state) ==
                ABT_THREAD_STATE_BLOCKED);

    ABTI_event_ythread_resume(p_local, p_ythread,
                              ABTI_local_get_xstream_or_null(p_local)
                                  ? ABTI_local_get_xstream(p_local)->p_thread
                                  : NULL);
    /* p_ythread->thread.p_pool is loaded before ABTI_POOL_ADD_THREAD to keep
     * num_blocked consistent. Otherwise, other threads might pop p_ythread
     * that has been pushed in ABTI_POOL_ADD_THREAD and change
     * p_ythread->thread.p_pool by ABT_unit_set_associated_pool. */
    ABTI_pool *p_pool = p_ythread->thread.p_pool;

    /* Add the ULT to its associated pool */
    ABTI_pool_add_thread(&p_ythread->thread, ABT_POOL_CONTEXT_OP_THREAD_RESUME);

    /* Decrease the number of blocked threads */
    ABTI_pool_dec_num_blocked(p_pool);
}

static inline ABTI_ythread *
ABTI_ythread_context_get_ythread(ABTD_ythread_context *p_ctx)
{
    return (ABTI_ythread *)(((char *)p_ctx) - offsetof(ABTI_ythread, ctx));
}

ABTU_noreturn static inline void
ABTI_ythread_context_jump(ABTI_xstream *p_local_xstream, ABTI_ythread *p_new)
{
    if (ABTD_ythread_context_is_started(&p_new->ctx)) {
        ABTD_ythread_context_jump(&p_new->ctx);
    } else {
        if (!ABTD_ythread_context_has_stack(&p_new->ctx)) {
            int ret =
                ABTI_mem_alloc_ythread_mempool_stack(p_local_xstream, p_new);
            /* FIXME: this error should be propagated to the caller. */
            ABTI_ASSERT(ret == ABT_SUCCESS);
        }
        ABTD_ythread_context_start_and_jump(&p_new->ctx);
    }
    ABTU_unreachable();
}

static inline void ABTI_ythread_context_switch(ABTI_xstream *p_local_xstream,
                                               ABTI_ythread *p_old,
                                               ABTI_ythread *p_new)
{
    if (ABTD_ythread_context_is_started(&p_new->ctx)) {
        ABTD_ythread_context_switch(&p_old->ctx, &p_new->ctx);
    } else {
        if (!ABTD_ythread_context_has_stack(&p_new->ctx)) {
            int ret =
                ABTI_mem_alloc_ythread_mempool_stack(p_local_xstream, p_new);
            /* FIXME: this error should be propagated to the caller. */
            ABTI_ASSERT(ret == ABT_SUCCESS);
        }
        ABTD_ythread_context_start_and_switch(&p_old->ctx, &p_new->ctx);
    }
}

ABTU_noreturn static inline void
ABTI_ythread_context_jump_with_call(ABTI_xstream *p_local_xstream,
                                    ABTI_ythread *p_new, void (*f_cb)(void *),
                                    void *cb_arg)
{
    if (ABTD_ythread_context_is_started(&p_new->ctx)) {
        ABTD_ythread_context_jump_with_call(&p_new->ctx, f_cb, cb_arg);
    } else {
        if (!ABTD_ythread_context_has_stack(&p_new->ctx)) {
            int ret =
                ABTI_mem_alloc_ythread_mempool_stack(p_local_xstream, p_new);
            /* FIXME: this error should be propagated to the caller. */
            ABTI_ASSERT(ret == ABT_SUCCESS);
        }
        ABTD_ythread_context_start_and_jump_with_call(&p_new->ctx, f_cb,
                                                      cb_arg);
    }
    ABTU_unreachable();
}

static inline void
ABTI_ythread_context_switch_with_call(ABTI_xstream *p_local_xstream,
                                      ABTI_ythread *p_old, ABTI_ythread *p_new,
                                      void (*f_cb)(void *), void *cb_arg)
{
    if (ABTD_ythread_context_is_started(&p_new->ctx)) {
        ABTD_ythread_context_switch_with_call(&p_old->ctx, &p_new->ctx, f_cb,
                                              cb_arg);
    } else {
        if (!ABTD_ythread_context_has_stack(&p_new->ctx)) {
            int ret =
                ABTI_mem_alloc_ythread_mempool_stack(p_local_xstream, p_new);
            /* FIXME: this error should be propagated to the caller. */
            ABTI_ASSERT(ret == ABT_SUCCESS);
        }
        ABTD_ythread_context_start_and_switch_with_call(&p_old->ctx,
                                                        &p_new->ctx, f_cb,
                                                        cb_arg);
    }
}

static inline void
ABTI_ythread_switch_to_child_internal(ABTI_xstream **pp_local_xstream,
                                      ABTI_ythread *p_old, ABTI_ythread *p_new)
{
    p_new->thread.p_parent = &p_old->thread;
    ABTI_xstream *p_local_xstream = *pp_local_xstream;
    ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                          p_new->thread.p_parent);
    p_local_xstream->p_thread = &p_new->thread;
    p_new->thread.p_last_xstream = p_local_xstream;
    /* Context switch starts. */
    ABTI_ythread_context_switch(p_local_xstream, p_old, p_new);
    /* Context switch finishes. */
    *pp_local_xstream = p_old->thread.p_last_xstream;
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_sibling_internal(ABTI_xstream *p_local_xstream,
                                      ABTI_ythread *p_old, ABTI_ythread *p_new,
                                      void (*f_cb)(void *), void *cb_arg)
{
    p_new->thread.p_parent = p_old->thread.p_parent;
    ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                          p_new->thread.p_parent);
    p_local_xstream->p_thread = &p_new->thread;
    p_new->thread.p_last_xstream = p_local_xstream;
    ABTI_ythread_context_jump_with_call(p_local_xstream, p_new, f_cb, cb_arg);
    ABTU_unreachable();
}

static inline void ABTI_ythread_switch_to_sibling_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new,
    void (*f_cb)(void *), void *cb_arg)
{
    p_new->thread.p_parent = p_old->thread.p_parent;
    ABTI_xstream *p_local_xstream = *pp_local_xstream;
    ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                          p_new->thread.p_parent);
    p_local_xstream->p_thread = &p_new->thread;
    p_new->thread.p_last_xstream = p_local_xstream;
    /* Context switch starts. */
    ABTI_ythread_context_switch_with_call(p_local_xstream, p_old, p_new, f_cb,
                                          cb_arg);
    /* Context switch finishes. */
    *pp_local_xstream = p_old->thread.p_last_xstream;
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_parent_internal(ABTI_xstream *p_local_xstream,
                                     ABTI_ythread *p_old, void (*f_cb)(void *),
                                     void *cb_arg)
{
    ABTI_ythread *p_new = ABTI_thread_get_ythread(p_old->thread.p_parent);
    p_local_xstream->p_thread = &p_new->thread;
    ABTI_ASSERT(p_new->thread.p_last_xstream == p_local_xstream);
    ABTI_ythread_context_jump_with_call(p_local_xstream, p_new, f_cb, cb_arg);
    ABTU_unreachable();
}

static inline void
ABTI_ythread_switch_to_parent_internal(ABTI_xstream **pp_local_xstream,
                                       ABTI_ythread *p_old,
                                       void (*f_cb)(void *), void *cb_arg)
{
    ABTI_ythread *p_new = ABTI_thread_get_ythread(p_old->thread.p_parent);
    ABTI_xstream *p_local_xstream = *pp_local_xstream;
    p_local_xstream->p_thread = &p_new->thread;
    ABTI_ASSERT(p_new->thread.p_last_xstream == p_local_xstream);
    /* Context switch starts. */
    ABTI_ythread_context_switch_with_call(p_local_xstream, p_old, p_new, f_cb,
                                          cb_arg);
    /* Context switch finishes. */
    *pp_local_xstream = p_old->thread.p_last_xstream;
}

static inline ABT_bool ABTI_ythread_context_peek(ABTI_ythread *p_ythread,
                                                 void (*f_peek)(void *),
                                                 void *arg)
{
    return ABTD_ythread_context_peek(&p_ythread->ctx, f_peek, arg);
}

static inline void ABTI_ythread_run_child(ABTI_xstream **pp_local_xstream,
                                          ABTI_ythread *p_self,
                                          ABTI_ythread *p_child)
{
    ABTD_atomic_release_store_int(&p_child->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    ABTI_ythread_switch_to_child_internal(pp_local_xstream, p_self, p_child);
}

typedef enum {
    ABTI_YTHREAD_YIELD_KIND_USER,
    ABTI_YTHREAD_YIELD_KIND_YIELD_LOOP,
} ABTI_ythread_yield_kind;

typedef enum {
    ABTI_YTHREAD_YIELD_TO_KIND_USER,
    ABTI_YTHREAD_YIELD_TO_KIND_CREATE_TO,
    ABTI_YTHREAD_YIELD_TO_KIND_REVIVE_TO,
} ABTI_ythread_yield_to_kind;

void ABTI_ythread_callback_yield_user_yield(void *arg);
void ABTI_ythread_callback_yield_loop(void *arg);
void ABTI_ythread_callback_yield_user_yield_to(void *arg);
void ABTI_ythread_callback_yield_create_to(void *arg);
void ABTI_ythread_callback_yield_revive_to(void *arg);

static inline void ABTI_ythread_yield(ABTI_xstream **pp_local_xstream,
                                      ABTI_ythread *p_self,
                                      ABTI_ythread_yield_kind kind,
                                      ABT_sync_event_type sync_event_type,
                                      void *p_sync)
{
    ABTI_event_ythread_yield(*pp_local_xstream, p_self, p_self->thread.p_parent,
                             sync_event_type, p_sync);
    if (kind == ABTI_YTHREAD_YIELD_KIND_USER) {
        ABTI_ythread_switch_to_parent_internal(
            pp_local_xstream, p_self, ABTI_ythread_callback_yield_user_yield,
            (void *)p_self);
    } else {
        ABTI_UB_ASSERT(kind == ABTI_YTHREAD_YIELD_KIND_YIELD_LOOP);
        ABTI_ythread_switch_to_parent_internal(pp_local_xstream, p_self,
                                               ABTI_ythread_callback_yield_loop,
                                               (void *)p_self);
    }
}

static inline void
ABTI_ythread_yield_to(ABTI_xstream **pp_local_xstream, ABTI_ythread *p_self,
                      ABTI_ythread *p_target, ABTI_ythread_yield_to_kind kind,
                      ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_event_ythread_yield(*pp_local_xstream, p_self, p_self->thread.p_parent,
                             sync_event_type, p_sync);
    ABTD_atomic_release_store_int(&p_target->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    if (kind == ABTI_YTHREAD_YIELD_TO_KIND_USER) {
        ABTI_ythread_switch_to_sibling_internal(
            pp_local_xstream, p_self, p_target,
            ABTI_ythread_callback_yield_user_yield_to, (void *)p_self);
    } else if (kind == ABTI_YTHREAD_YIELD_TO_KIND_CREATE_TO) {
        ABTI_ythread_switch_to_sibling_internal(
            pp_local_xstream, p_self, p_target,
            ABTI_ythread_callback_yield_create_to, (void *)p_self);
    } else {
        ABTI_UB_ASSERT(kind == ABTI_YTHREAD_YIELD_TO_KIND_REVIVE_TO);
        ABTI_ythread_switch_to_sibling_internal(
            pp_local_xstream, p_self, p_target,
            ABTI_ythread_callback_yield_revive_to, (void *)p_self);
    }
}

/* Old interface used for ABT_thread_yield_to() */
void ABTI_ythread_callback_thread_yield_to(void *arg);

static inline void
ABTI_ythread_thread_yield_to(ABTI_xstream **pp_local_xstream,
                             ABTI_ythread *p_self, ABTI_ythread *p_target,
                             ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_event_ythread_yield(*pp_local_xstream, p_self, p_self->thread.p_parent,
                             sync_event_type, p_sync);
    ABTD_atomic_release_store_int(&p_target->thread.state,
                                  ABT_THREAD_STATE_RUNNING);

    ABTI_ythread_switch_to_sibling_internal(
        pp_local_xstream, p_self, p_target,
        ABTI_ythread_callback_thread_yield_to, (void *)p_self);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_ythread *p_next;
} ABTI_ythread_callback_resume_yield_to_arg;

void ABTI_ythread_callback_resume_yield_to(void *arg);

typedef enum {
    ABTI_YTHREAD_RESUME_YIELD_TO_KIND_USER,
} ABTI_ythread_resume_yield_to_kind;

static inline void
ABTI_ythread_resume_yield_to(ABTI_xstream **pp_local_xstream,
                             ABTI_ythread *p_self, ABTI_ythread *p_target,
                             ABTI_ythread_resume_yield_to_kind kind,
                             ABT_sync_event_type sync_event_type, void *p_sync)
{
    /* The ULT must be in BLOCKED state. */
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_target->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);

    ABTI_event_ythread_resume(ABTI_xstream_get_local(*pp_local_xstream),
                              p_target, &p_self->thread);
    ABTI_event_ythread_yield(*pp_local_xstream, p_self, p_self->thread.p_parent,
                             sync_event_type, p_sync);
    ABTD_atomic_release_store_int(&p_target->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    ABTI_UB_ASSERT(kind == ABTI_YTHREAD_RESUME_YIELD_TO_KIND_USER);
    ABTI_ythread_callback_resume_yield_to_arg arg = { p_self, p_target };
    ABTI_ythread_switch_to_sibling_internal(
        pp_local_xstream, p_self, p_target,
        ABTI_ythread_callback_resume_yield_to, (void *)&arg);
}

void ABTI_ythread_callback_suspend(void *arg);

static inline void ABTI_ythread_suspend(ABTI_xstream **pp_local_xstream,
                                        ABTI_ythread *p_self,
                                        ABT_sync_event_type sync_event_type,
                                        void *p_sync)
{
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTI_ythread_switch_to_parent_internal(pp_local_xstream, p_self,
                                           ABTI_ythread_callback_suspend,
                                           (void *)p_self);
}

static inline void ABTI_ythread_suspend_to(ABTI_xstream **pp_local_xstream,
                                           ABTI_ythread *p_self,
                                           ABTI_ythread *p_target,
                                           ABT_sync_event_type sync_event_type,
                                           void *p_sync)
{
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTI_ythread_switch_to_sibling_internal(pp_local_xstream, p_self, p_target,
                                            ABTI_ythread_callback_suspend,
                                            (void *)p_self);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_ythread *p_next;
} ABTI_ythread_callback_resume_suspend_to_arg;

void ABTI_ythread_callback_resume_suspend_to(void *arg);

static inline void ABTI_ythread_resume_suspend_to(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_self,
    ABTI_ythread *p_target, ABT_sync_event_type sync_event_type, void *p_sync)
{
    /* The ULT must be in BLOCKED state. */
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_target->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);

    ABTI_event_ythread_resume(ABTI_xstream_get_local(*pp_local_xstream),
                              p_target, &p_self->thread);
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTD_atomic_release_store_int(&p_target->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    ABTI_ythread_callback_resume_suspend_to_arg arg = { p_self, p_target };
    ABTI_ythread_switch_to_sibling_internal(
        pp_local_xstream, p_self, p_target,
        ABTI_ythread_callback_resume_suspend_to, (void *)&arg);
}

void ABTI_ythread_callback_exit(void *arg);

static inline ABTI_ythread *
ABTI_ythread_atomic_get_joiner(ABTI_ythread *p_ythread)
{
    ABTD_ythread_context *p_ctx = &p_ythread->ctx;
    ABTD_ythread_context *p_link =
        ABTD_atomic_acquire_load_ythread_context_ptr(&p_ctx->p_link);
    if (!p_link) {
        uint32_t req = ABTD_atomic_fetch_or_uint32(&p_ythread->thread.request,
                                                   ABTI_THREAD_REQ_JOIN);
        if (!(req & ABTI_THREAD_REQ_JOIN)) {
            /* This case means there is no join request. */
            return NULL;
        } else {
            /* This case means a join request is issued and the joiner is
             * setting p_link.  Wait for it. */
            do {
                p_link = ABTD_atomic_acquire_load_ythread_context_ptr(
                    &p_ctx->p_link);
            } while (!p_link);
            return ABTI_ythread_context_get_ythread(p_link);
        }
    } else {
        /* There is a join request. */
        return ABTI_ythread_context_get_ythread(p_link);
    }
}

static inline void ABTI_ythread_resume_joiner(ABTI_xstream *p_local_xstream,
                                              ABTI_ythread *p_ythread)
{
    ABTI_ythread *p_joiner = ABTI_ythread_atomic_get_joiner(p_ythread);
    if (p_joiner) {
#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
        if (p_joiner->thread.type == ABTI_THREAD_TYPE_EXT) {
            /* p_joiner is a non-yieldable thread (i.e., external thread). Wake
             * up the waiter via the futex.  Note that p_arg is used to store
             * futex (see thread_join_futexwait()). */
            ABTD_futex_single *p_futex =
                (ABTD_futex_single *)p_joiner->thread.p_arg;
            ABTD_futex_resume(p_futex);
            return;
        }
#endif
        /* p_joiner is a yieldable thread */
        ABTI_ythread_resume_and_push(ABTI_xstream_get_local(p_local_xstream),
                                     p_joiner);
    }
}

ABTU_noreturn static inline void
ABTI_ythread_exit(ABTI_xstream *p_local_xstream, ABTI_ythread *p_self)
{
    ABTI_event_thread_finish(p_local_xstream, &p_self->thread,
                             p_self->thread.p_parent);
    ABTI_ythread *p_joiner = ABTI_ythread_atomic_get_joiner(p_self);
    if (p_joiner) {
#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
        if (p_joiner->thread.type == ABTI_THREAD_TYPE_EXT) {
            /* p_joiner is a non-yieldable thread (i.e., external thread). Wake
             * up the waiter via the futex.  Note that p_arg is used to store
             * futex (see thread_join_futexwait()). */
            ABTD_futex_single *p_futex =
                (ABTD_futex_single *)p_joiner->thread.p_arg;
            ABTD_futex_resume(p_futex);
        } else
#endif
            if (p_self->thread.p_last_xstream ==
                    p_joiner->thread.p_last_xstream &&
                !(p_self->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED)) {
            /* Only when the current ULT is on the same ES as p_joiner's, we can
             * jump to the joiner ULT.  Note that a parent ULT cannot be a
             * joiner. */
            ABTI_pool_dec_num_blocked(p_joiner->thread.p_pool);
            ABTI_event_ythread_resume(ABTI_xstream_get_local(p_local_xstream),
                                      p_joiner, &p_self->thread);
            ABTD_atomic_release_store_int(&p_joiner->thread.state,
                                          ABT_THREAD_STATE_RUNNING);
            ABTI_ythread_jump_to_sibling_internal(p_local_xstream, p_self,
                                                  p_joiner,
                                                  ABTI_ythread_callback_exit,
                                                  (void *)p_self);
            ABTU_unreachable();
        } else {
            /* If the current ULT's associated ES is different from p_joiner's,
             * we can't directly jump to p_joiner.  Instead, we wake up p_joiner
             * here so that p_joiner's scheduler can resume it.  Note that the
             * main scheduler needs to jump back to the root scheduler, so the
             * main scheduler needs to take this path. */
            ABTI_ythread_resume_and_push(ABTI_xstream_get_local(
                                             p_local_xstream),
                                         p_joiner);
        }
    }
    /* The waiter has been resumed.  Let's switch to the parent. */
    ABTI_ythread_jump_to_parent_internal(p_local_xstream, p_self,
                                         ABTI_ythread_callback_exit,
                                         (void *)p_self);
    ABTU_unreachable();
}

ABTU_noreturn static inline void
ABTI_ythread_exit_to(ABTI_xstream *p_local_xstream, ABTI_ythread *p_self,
                     ABTI_ythread *p_target)
{
    /* If other ULT is blocked to join the canceled ULT, we have to wake up the
     * joiner ULT.  However, unlike the case when the ULT has finished its
     * execution and calls ythread_terminate/exit, this caller of this function
     * wants to jump to p_target.  Therefore, we should not context switch to
     * the joiner ULT. */
    ABTI_ythread_resume_joiner(p_local_xstream, p_self);
    ABTI_event_thread_finish(p_local_xstream, &p_self->thread,
                             p_self->thread.p_parent);
    ABTD_atomic_release_store_int(&p_target->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    ABTI_ythread_jump_to_sibling_internal(p_local_xstream, p_self, p_target,
                                          ABTI_ythread_callback_exit,
                                          (void *)p_self);
    ABTU_unreachable();
}

ABTU_noreturn static inline void ABTI_ythread_exit_to_primary(
    ABTI_global *p_global, ABTI_xstream *p_local_xstream, ABTI_ythread *p_self)
{
    /* No need to call a callback function. */
    ABTI_ythread *p_primary = p_global->p_primary_ythread;
    p_local_xstream->p_thread = &p_primary->thread;
    p_primary->thread.p_last_xstream = p_local_xstream;
    ABTD_atomic_release_store_int(&p_primary->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    ABTI_ythread_context_jump_with_call(p_local_xstream, p_primary,
                                        ABTI_ythread_callback_exit, p_self);
    ABTU_unreachable();
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_ythread *p_next;
} ABTI_ythread_callback_resume_exit_to_arg;

void ABTI_ythread_callback_resume_exit_to(void *arg);

ABTU_noreturn static inline void
ABTI_ythread_resume_exit_to(ABTI_xstream *p_local_xstream, ABTI_ythread *p_self,
                            ABTI_ythread *p_target)
{
    /* The ULT must be in BLOCKED state. */
    ABTI_UB_ASSERT(ABTD_atomic_acquire_load_int(&p_target->thread.state) ==
                   ABT_THREAD_STATE_BLOCKED);

    ABTI_event_ythread_resume(ABTI_xstream_get_local(p_local_xstream), p_target,
                              &p_self->thread);
    /* Wake up a joiner ULT attached to p_self. */
    ABTI_ythread_resume_joiner(p_local_xstream, p_self);
    ABTI_event_thread_finish(p_local_xstream, &p_self->thread,
                             p_self->thread.p_parent);
    ABTD_atomic_release_store_int(&p_target->thread.state,
                                  ABT_THREAD_STATE_RUNNING);
    ABTI_ythread_callback_resume_exit_to_arg arg = { p_self, p_target };
    ABTI_ythread_jump_to_sibling_internal(p_local_xstream, p_self, p_target,
                                          ABTI_ythread_callback_resume_exit_to,
                                          (void *)&arg);
    ABTU_unreachable();
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTD_spinlock *p_lock;
} ABTI_ythread_callback_suspend_unlock_arg;

void ABTI_ythread_callback_suspend_unlock(void *arg);

static inline void
ABTI_ythread_suspend_unlock(ABTI_xstream **pp_local_xstream,
                            ABTI_ythread *p_self, ABTD_spinlock *p_lock,
                            ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTI_ythread_callback_suspend_unlock_arg arg = { p_self, p_lock };
    ABTI_ythread_switch_to_parent_internal(pp_local_xstream, p_self,
                                           ABTI_ythread_callback_suspend_unlock,
                                           (void *)&arg);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_ythread *p_target;
} ABTI_ythread_callback_suspend_join_arg;

void ABTI_ythread_callback_suspend_join(void *arg);

static inline void
ABTI_ythread_suspend_join(ABTI_xstream **pp_local_xstream, ABTI_ythread *p_self,
                          ABTI_ythread *p_target,
                          ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTI_ythread_callback_suspend_join_arg arg = { p_self, p_target };
    ABTI_ythread_switch_to_parent_internal(pp_local_xstream, p_self,
                                           ABTI_ythread_callback_suspend_join,
                                           (void *)&arg);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_sched *p_main_sched;
} ABTI_ythread_callback_suspend_replace_sched_arg;

void ABTI_ythread_callback_suspend_replace_sched(void *arg);

static inline void ABTI_ythread_suspend_replace_sched(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_self,
    ABTI_sched *p_main_sched, ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTI_ythread_callback_suspend_replace_sched_arg arg = { p_self,
                                                            p_main_sched };
    ABTI_ythread_switch_to_parent_internal(
        pp_local_xstream, p_self, ABTI_ythread_callback_suspend_replace_sched,
        (void *)&arg);
}

void ABTI_ythread_callback_orphan(void *arg);

static inline void
ABTI_ythread_yield_orphan(ABTI_xstream **pp_local_xstream, ABTI_ythread *p_self,
                          ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_event_ythread_suspend(*pp_local_xstream, p_self,
                               p_self->thread.p_parent, sync_event_type,
                               p_sync);
    ABTI_ythread_switch_to_parent_internal(pp_local_xstream, p_self,
                                           ABTI_ythread_callback_orphan,
                                           (void *)p_self);
}
static inline void ABTI_ythread_schedule(ABTI_global *p_global,
                                         ABTI_xstream **pp_local_xstream,
                                         ABTI_thread *p_thread)
{
    ABTI_xstream *p_local_xstream = *pp_local_xstream;
    const int request_op = ABTI_thread_handle_request(p_thread, ABT_TRUE);
    if (ABTU_likely(request_op == ABTI_THREAD_HANDLE_REQUEST_NONE)) {
        /* Execute p_thread. */
        ABTI_ythread *p_ythread = ABTI_thread_get_ythread_or_null(p_thread);
        if (p_ythread) {
            /* p_thread is yieldable.  Let's switch the context.  Since the
             * argument is pp_local_xstream, p_local_xstream->p_thread must be
             * yieldable. */
            ABTI_ythread *p_self =
                ABTI_thread_get_ythread(p_local_xstream->p_thread);
            ABTI_ythread_run_child(pp_local_xstream, p_self, p_ythread);
            /* The previous ULT (p_ythread) may not be the same as one to which
             * the context has been switched. */
        } else {
            /* p_thread is not yieldable. */
            /* Change the task state */
            ABTD_atomic_release_store_int(&p_thread->state,
                                          ABT_THREAD_STATE_RUNNING);

            /* Set the associated ES */
            p_thread->p_last_xstream = p_local_xstream;

            /* Execute the task function */
            ABTI_thread *p_sched_thread = p_local_xstream->p_thread;
            p_local_xstream->p_thread = p_thread;
            p_thread->p_parent = p_sched_thread;

            /* Execute the task function */
            ABTI_event_thread_run(p_local_xstream, p_thread, p_sched_thread,
                                  p_sched_thread);
            p_thread->f_thread(p_thread->p_arg);
            ABTI_event_thread_finish(p_local_xstream, p_thread, p_sched_thread);

            /* Set the current running scheduler's thread */
            p_local_xstream->p_thread = p_sched_thread;

            /* Terminate the tasklet */
            ABTI_thread_terminate(p_global, p_local_xstream, p_thread);
        }
    } else if (request_op == ABTI_THREAD_HANDLE_REQUEST_CANCELLED) {
        /* If p_thread is cancelled, there's nothing to do. */
    } else if (request_op == ABTI_THREAD_HANDLE_REQUEST_MIGRATED) {
        /* If p_thread is migrated, let's push p_thread back to its pool. */
        ABTI_pool_add_thread(p_thread, ABT_POOL_CONTEXT_OP_THREAD_MIGRATE);
    }
}

#endif /* ABTI_YTHREAD_H_INCLUDED */
