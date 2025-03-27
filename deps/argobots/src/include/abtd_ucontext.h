/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTD_UCONTEXT_H_INCLUDED
#define ABTD_UCONTEXT_H_INCLUDED

struct ABTD_ythread_context {
    void *p_ctx;                            /* actual context of fcontext, or a
                                             * pointer to uctx */
    ABTD_ythread_context_atomic_ptr p_link; /* pointer to scheduler context */
    ucontext_t uctx;                        /* ucontext pointed by p_ctx */
    void *p_stacktop;                       /* Stack pointer (top). */
    size_t stacksize;                       /* Stack size. */
    /* Call-back functions. */
    void (*f_cb)(void *);
    void *cb_arg;
    /* Peek functions. */
    void (*peek_func)(void *);
    void *peek_arg;
    ucontext_t *p_peek_uctx;
    ABT_bool is_peeked;
};

static inline void ABTDI_ucontext_check_peeked(ABTD_ythread_context *p_self)
{
    /* Check if this thread is called only for peeked */
    while (ABTU_unlikely(p_self->is_peeked)) {
        p_self->peek_func(p_self->peek_arg);
        /* Reset the flag. */
        p_self->is_peeked = ABT_FALSE;
        int ret = swapcontext(&p_self->uctx, p_self->p_peek_uctx);
        ABTI_ASSERT(ret == 0); /* Fatal. */
        if (p_self->f_cb) {
            p_self->f_cb(p_self->cb_arg);
            p_self->f_cb = NULL;
        }
    }
}

static void ABTD_ucontext_wrapper(int arg1, int arg2)
{
    ABTD_ythread_context *p_self;
#if SIZEOF_VOID_P == 8
    p_self = (ABTD_ythread_context *)(((uintptr_t)((uint32_t)arg1) << 32) |
                                      ((uintptr_t)((uint32_t)arg2)));
#elif SIZEOF_VOID_P == 4
    p_self = (ABTD_ythread_context *)((uintptr_t)arg1);
#else
#error "Unknown pointer size."
#endif

    if (p_self->f_cb) {
        p_self->f_cb(p_self->cb_arg);
        p_self->f_cb = NULL;
    }
    ABTDI_ucontext_check_peeked(p_self);
    ABTD_ythread_func_wrapper(p_self);
    ABTU_unreachable();
}

static inline void ABTD_ythread_context_init(ABTD_ythread_context *p_ctx,
                                             void *p_stacktop, size_t stacksize)
{
    p_ctx->p_ctx = NULL;
    p_ctx->p_stacktop = p_stacktop;
    p_ctx->stacksize = stacksize;
    int ret = getcontext(&p_ctx->uctx);
    ABTI_ASSERT(ret == 0); /* getcontext() should not return an error. */
    ABTD_atomic_relaxed_store_ythread_context_ptr(&p_ctx->p_link, NULL);
}

static inline void ABTD_ythread_context_init_lazy(ABTD_ythread_context *p_ctx,
                                                  size_t stacksize)
{
    ABTD_ythread_context_init(p_ctx, NULL, stacksize);
}

static inline void
ABTD_ythread_context_lazy_set_stack(ABTD_ythread_context *p_ctx,
                                    void *p_stacktop)
{
    p_ctx->p_stacktop = p_stacktop;
}

static inline void
ABTD_ythread_context_lazy_unset_stack(ABTD_ythread_context *p_ctx)
{
    p_ctx->p_stacktop = NULL;
}

static inline void ABTD_ythread_context_reinit(ABTD_ythread_context *p_ctx)
{
    p_ctx->p_ctx = NULL;
    int ret = getcontext(&p_ctx->uctx);
    ABTI_ASSERT(ret == 0); /* getcontext() should not return an error. */
    ABTD_atomic_relaxed_store_ythread_context_ptr(&p_ctx->p_link, NULL);
}

static inline void *
ABTD_ythread_context_get_stacktop(ABTD_ythread_context *p_ctx)
{
    return p_ctx->p_stacktop;
}

static inline ABT_bool
ABTD_ythread_context_has_stack(const ABTD_ythread_context *p_ctx)
{
    return p_ctx->p_stacktop ? ABT_TRUE : ABT_FALSE;
}

static inline size_t
ABTD_ythread_context_get_stacksize(ABTD_ythread_context *p_ctx)
{
    return p_ctx->stacksize;
}

static inline void ABTDI_ythread_context_make(ABTD_ythread_context *p_ctx)
{
    p_ctx->p_ctx = &p_ctx->uctx;
    /* uc_link is not used. */
    p_ctx->uctx.uc_link = NULL;
    void *p_stack = (void *)(((char *)p_ctx->p_stacktop) - p_ctx->stacksize);
    p_ctx->uctx.uc_stack.ss_sp = p_stack;
    p_ctx->uctx.uc_stack.ss_size = p_ctx->stacksize;

#if SIZEOF_VOID_P == 8
    int arg_upper = (int)(((uintptr_t)p_ctx) >> 32);
    int arg_lower = (int)(((uintptr_t)p_ctx) >> 0);
    makecontext(&p_ctx->uctx, (void (*)())ABTD_ucontext_wrapper, 2, arg_upper,
                arg_lower);
#elif SIZEOF_VOID_P == 4
    int arg = (int)((uintptr_t)p_ctx);
    makecontext(&p_ctx->uctx, (void (*)())ABTD_ucontext_wrapper, 1, arg);
#else
#error "Unknown pointer size."
#endif
    p_ctx->is_peeked = ABT_FALSE;
}

static inline ABT_bool
ABTD_ythread_context_is_started(const ABTD_ythread_context *p_ctx)
{
    return p_ctx->p_ctx ? ABT_TRUE : ABT_FALSE;
}

static inline void ABTD_ythread_context_switch(ABTD_ythread_context *p_old,
                                               ABTD_ythread_context *p_new)
{
    ABTI_UB_ASSERT(ABTD_ythread_context_is_started(p_new));
    p_old->is_peeked = ABT_FALSE;
    p_old->p_ctx = &p_old->uctx;
    p_new->f_cb = NULL;
    int ret = swapcontext(&p_old->uctx, &p_new->uctx);
    /* Fatal.  This out-of-stack error is not recoverable. */
    ABTI_ASSERT(ret == 0);
    if (p_old->f_cb) {
        p_old->f_cb(p_old->cb_arg);
        p_old->f_cb = NULL;
    }
    ABTDI_ucontext_check_peeked(p_old);
}

static inline void
ABTD_ythread_context_start_and_switch(ABTD_ythread_context *p_old,
                                      ABTD_ythread_context *p_new)
{
    ABTI_UB_ASSERT(!ABTD_ythread_context_is_started(p_new));
    ABTDI_ythread_context_make(p_new);
    ABTD_ythread_context_switch(p_old, p_new);
}

ABTU_noreturn static inline void
ABTD_ythread_context_jump(ABTD_ythread_context *p_new)
{
    ABTI_UB_ASSERT(ABTD_ythread_context_is_started(p_new));
    int ret = setcontext(&p_new->uctx);
    ABTI_ASSERT(ret == 0); /* setcontext() should not return an error. */
    ABTU_unreachable();
}

ABTU_noreturn static inline void
ABTD_ythread_context_start_and_jump(ABTD_ythread_context *p_new)
{
    ABTI_UB_ASSERT(!ABTD_ythread_context_is_started(p_new));
    ABTDI_ythread_context_make(p_new);
    ABTD_ythread_context_jump(p_new);
    ABTU_unreachable();
}

static inline void
ABTD_ythread_context_switch_with_call(ABTD_ythread_context *p_old,
                                      ABTD_ythread_context *p_new,
                                      void (*f_cb)(void *), void *cb_arg)
{
    ABTI_UB_ASSERT(ABTD_ythread_context_is_started(p_new));
    p_old->is_peeked = ABT_FALSE;
    p_old->p_ctx = &p_old->uctx;
    p_new->f_cb = f_cb;
    p_new->cb_arg = cb_arg;
    int ret = swapcontext(&p_old->uctx, &p_new->uctx);
    /* Fatal.  This out-of-stack error is not recoverable. */
    ABTI_ASSERT(ret == 0);
    if (p_old->f_cb) {
        p_old->f_cb(p_old->cb_arg);
        p_old->f_cb = NULL;
    }
    ABTDI_ucontext_check_peeked(p_old);
}

static inline void ABTD_ythread_context_start_and_switch_with_call(
    ABTD_ythread_context *p_old, ABTD_ythread_context *p_new,
    void (*f_cb)(void *), void *cb_arg)
{
    ABTI_UB_ASSERT(!ABTD_ythread_context_is_started(p_new));
    ABTDI_ythread_context_make(p_new);
    ABTD_ythread_context_switch_with_call(p_old, p_new, f_cb, cb_arg);
}

ABTU_noreturn static inline void
ABTD_ythread_context_jump_with_call(ABTD_ythread_context *p_new,
                                    void (*f_cb)(void *), void *cb_arg)
{
    ABTI_UB_ASSERT(ABTD_ythread_context_is_started(p_new));
    p_new->f_cb = f_cb;
    p_new->cb_arg = cb_arg;
    int ret = setcontext(&p_new->uctx);
    ABTI_ASSERT(ret == 0); /* setcontext() should not return an error. */
    ABTU_unreachable();
}

ABTU_noreturn static inline void ABTD_ythread_context_start_and_jump_with_call(
    ABTD_ythread_context *p_new, void (*f_cb)(void *), void *cb_arg)
{
    ABTI_UB_ASSERT(!ABTD_ythread_context_is_started(p_new));
    ABTDI_ythread_context_make(p_new);
    ABTD_ythread_context_jump_with_call(p_new, f_cb, cb_arg);
    ABTU_unreachable();
}

static inline ABT_bool ABTD_ythread_context_peek(ABTD_ythread_context *p_ctx,
                                                 void (*peek_func)(void *),
                                                 void *arg)
{
    if (p_ctx->p_ctx) {
        ucontext_t self_uctx;
        p_ctx->peek_arg = arg;
        p_ctx->peek_func = peek_func;
        p_ctx->p_peek_uctx = &self_uctx;
        p_ctx->is_peeked = ABT_TRUE;
        int ret = swapcontext(&self_uctx, &p_ctx->uctx);
        if (p_ctx->f_cb) {
            p_ctx->f_cb(p_ctx->cb_arg);
            p_ctx->f_cb = NULL;
        }
        ABTI_ASSERT(ret == 0);
        return ABT_TRUE;
    } else {
        return ABT_FALSE;
    }
}

#endif /* ABTD_UCONTEXT_H_INCLUDED */
