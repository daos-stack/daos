/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#ifdef ABT_CONFIG_ENABLE_STACK_UNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
struct unwind_stack_t {
    FILE *fp;
};
static void ythread_unwind_stack(void *arg);
#endif

/*****************************************************************************/
/* Private APIs                                                              */
/*****************************************************************************/

static inline void ythread_callback_yield_impl(void *arg,
                                               ABT_pool_context context)
{
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    if (ABTI_thread_handle_request(&p_prev->thread, ABT_TRUE) &
        ABTI_THREAD_HANDLE_REQUEST_CANCELLED) {
        /* p_prev is terminated. */
    } else {
        /* Push p_prev back to the pool. */
        ABTI_pool_add_thread(&p_prev->thread, context);
    }
}

void ABTI_ythread_callback_yield_user_yield(void *arg)
{
    ythread_callback_yield_impl(arg, ABT_POOL_CONTEXT_OP_THREAD_YIELD);
}

void ABTI_ythread_callback_yield_loop(void *arg)
{
    ythread_callback_yield_impl(arg, ABT_POOL_CONTEXT_OP_THREAD_YIELD_LOOP);
}

void ABTI_ythread_callback_yield_user_yield_to(void *arg)
{
    ythread_callback_yield_impl(arg, ABT_POOL_CONTEXT_OP_THREAD_YIELD_TO);
}

void ABTI_ythread_callback_yield_create_to(void *arg)
{
    ythread_callback_yield_impl(arg, ABT_POOL_CONTEXT_OP_THREAD_CREATE_TO);
}

void ABTI_ythread_callback_yield_revive_to(void *arg)
{
    ythread_callback_yield_impl(arg, ABT_POOL_CONTEXT_OP_THREAD_REVIVE_TO);
}

/* Before yield_to, p_prev->thread.p_pool's num_blocked must be incremented to
 * avoid making a pool empty. */
void ABTI_ythread_callback_thread_yield_to(void *arg)
{
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    /* p_prev->thread.p_pool is loaded before ABTI_pool_add_thread() to keep
     * num_blocked consistent. Otherwise, other threads might pop p_prev
     * that has been pushed by ABTI_pool_add_thread() and change
     * p_prev->thread.p_pool by ABT_unit_set_associated_pool(). */
    ABTI_pool *p_pool = p_prev->thread.p_pool;
    if (ABTI_thread_handle_request(&p_prev->thread, ABT_TRUE) &
        ABTI_THREAD_HANDLE_REQUEST_CANCELLED) {
        /* p_prev is terminated. */
    } else {
        /* Push p_prev back to the pool. */
        ABTI_pool_add_thread(&p_prev->thread,
                             ABT_POOL_CONTEXT_OP_THREAD_YIELD_TO);
    }
    /* Decrease the number of blocked threads of the original pool (i.e., before
     * migration), which has been increased by p_prev to avoid making a pool
     * size 0. */
    ABTI_pool_dec_num_blocked(p_pool);
}

void ABTI_ythread_callback_resume_yield_to(void *arg)
{
    ABTI_ythread_callback_resume_yield_to_arg *p_arg =
        (ABTI_ythread_callback_resume_yield_to_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_ythread *p_next = p_arg->p_next;
    if (ABTI_thread_handle_request(&p_prev->thread, ABT_TRUE) &
        ABTI_THREAD_HANDLE_REQUEST_CANCELLED) {
        /* p_prev is terminated. */
    } else {
        /* Push this thread back to the pool. */
        ABTI_pool_add_thread(&p_prev->thread,
                             ABT_POOL_CONTEXT_OP_THREAD_RESUME_YIELD_TO);
    }
    /* Decrease the number of blocked threads of p_next's pool. */
    ABTI_pool_dec_num_blocked(p_next->thread.p_pool);
}

void ABTI_ythread_callback_suspend(void *arg)
{
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    /* Increase the number of blocked threads of the original pool (i.e., before
     * migration) */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Request handling.  p_prev->thread.p_pool might be changed. */
    ABTI_thread_handle_request(&p_prev->thread, ABT_FALSE);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
}

void ABTI_ythread_callback_resume_suspend_to(void *arg)
{
    ABTI_ythread_callback_resume_suspend_to_arg *p_arg =
        (ABTI_ythread_callback_resume_suspend_to_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_ythread *p_next = p_arg->p_next;
    ABTI_pool *p_prev_pool = p_prev->thread.p_pool;
    ABTI_pool *p_next_pool = p_next->thread.p_pool;
    if (p_prev_pool != p_next_pool) {
        /* Increase the number of blocked threads of p_prev's pool */
        ABTI_pool_inc_num_blocked(p_prev_pool);
        /* Decrease the number of blocked threads of p_next's pool */
        ABTI_pool_dec_num_blocked(p_next_pool);
    }
    /* Request handling.  p_prev->thread.p_pool might be changed. */
    ABTI_thread_handle_request(&p_prev->thread, ABT_FALSE);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
}

void ABTI_ythread_callback_exit(void *arg)
{
    /* Terminate this thread. */
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    ABTI_thread_terminate(ABTI_global_get_global(),
                          p_prev->thread.p_last_xstream, &p_prev->thread);
}

void ABTI_ythread_callback_resume_exit_to(void *arg)
{
    ABTI_ythread_callback_resume_exit_to_arg *p_arg =
        (ABTI_ythread_callback_resume_exit_to_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_ythread *p_next = p_arg->p_next;
    /* Terminate this thread. */
    ABTI_thread_terminate(ABTI_global_get_global(),
                          p_prev->thread.p_last_xstream, &p_prev->thread);
    /* Decrease the number of blocked threads. */
    ABTI_pool_dec_num_blocked(p_next->thread.p_pool);
}

void ABTI_ythread_callback_suspend_unlock(void *arg)
{
    ABTI_ythread_callback_suspend_unlock_arg *p_arg =
        (ABTI_ythread_callback_suspend_unlock_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTD_spinlock *p_lock = p_arg->p_lock;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Request handling.  p_prev->thread.p_pool might be changed. */
    ABTI_thread_handle_request(&p_prev->thread, ABT_FALSE);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
    /* Release the lock. */
    ABTD_spinlock_release(p_lock);
}

void ABTI_ythread_callback_suspend_join(void *arg)
{
    ABTI_ythread_callback_suspend_join_arg *p_arg =
        (ABTI_ythread_callback_suspend_join_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_ythread *p_target = p_arg->p_target;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Request handling.  p_prev->thread.p_pool might be changed. */
    ABTI_thread_handle_request(&p_prev->thread, ABT_FALSE);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
    /* Set the link in the context of the target ULT. This p_link might be
     * read by p_target running on another ES in parallel, so release-store
     * is needed here. */
    ABTD_atomic_release_store_ythread_context_ptr(&p_target->ctx.p_link,
                                                  &p_prev->ctx);
}

void ABTI_ythread_callback_suspend_replace_sched(void *arg)
{
    ABTI_ythread_callback_suspend_replace_sched_arg *p_arg =
        (ABTI_ythread_callback_suspend_replace_sched_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_sched *p_main_sched = p_arg->p_main_sched;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Request handling.  p_prev->thread.p_pool might be changed. */
    ABTI_thread_handle_request(&p_prev->thread, ABT_FALSE);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
    /* Ask the current main scheduler to replace its scheduler */
    ABTI_sched_set_request(p_main_sched, ABTI_SCHED_REQ_REPLACE);
}

void ABTI_ythread_callback_orphan(void *arg)
{
    /* It's a special operation, so request handling is unnecessary. */
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    ABTI_thread_unset_associated_pool(ABTI_global_get_global(),
                                      &p_prev->thread);
}

ABTU_no_sanitize_address void ABTI_ythread_print_stack(ABTI_global *p_global,
                                                       ABTI_ythread *p_ythread,
                                                       FILE *p_os)
{
    ABTD_ythread_print_context(p_ythread, p_os, 0);
    fprintf(p_os,
            "stacktop  : %p\n"
            "stacksize : %" PRIu64 "\n",
            ABTD_ythread_context_get_stacktop(&p_ythread->ctx),
            (uint64_t)ABTD_ythread_context_get_stacksize(&p_ythread->ctx));

#ifdef ABT_CONFIG_ENABLE_STACK_UNWIND
    {
        /* Peeking a running context is specially forbidden.  Though it is
         * incomplete, let's quickly check if a thread is running. */
        ABT_thread_state state = (ABT_thread_state)ABTD_atomic_acquire_load_int(
            &p_ythread->thread.state);
        if (state == ABT_THREAD_STATE_READY ||
            state == ABT_THREAD_STATE_BLOCKED) {
            struct unwind_stack_t arg;
            arg.fp = p_os;
            ABT_bool succeeded =
                ABTI_ythread_context_peek(p_ythread, ythread_unwind_stack,
                                          &arg);
            if (!succeeded) {
                fprintf(p_os, "not executed yet.\n");
            }
        } else {
            fprintf(p_os, "failed to unwind a stack.\n");
        }
    }
#endif

    void *p_stacktop = ABTD_ythread_context_get_stacktop(&p_ythread->ctx);
    size_t i, j,
        stacksize = ABTD_ythread_context_get_stacksize(&p_ythread->ctx);
    if (stacksize == 0 || p_stacktop == NULL) {
        /* Some threads do not have p_stack (e.g., the main thread) */
        fprintf(p_os, "no stack\n");
        fflush(0);
        return;
    }
    if (p_global->print_raw_stack) {
        void *p_stack = (void *)(((char *)p_stacktop) - stacksize);
        char buffer[32];
        const size_t value_width = 8;
        const int num_bytes = sizeof(buffer);
        static const char zero[sizeof(buffer)];
        ABT_bool full_zeroes = ABT_FALSE, multi_lines = ABT_FALSE;

        for (i = 0; i < stacksize; i += num_bytes) {
            if (stacksize >= i + num_bytes) {
                memcpy(buffer, &((uint8_t *)p_stack)[i], num_bytes);
            } else {
                memset(buffer, 0, num_bytes);
                memcpy(buffer, &((uint8_t *)p_stack)[i], stacksize - i);
            }

            /* pack full lines of zeroes */
            if (!memcmp(zero, buffer, sizeof(buffer))) {
                if (!full_zeroes) {
                    full_zeroes = ABT_TRUE;
                } else {
                    multi_lines = ABT_TRUE;
                    continue;
                }
            } else {
                full_zeroes = ABT_FALSE;
                if (multi_lines) {
                    fprintf(p_os, "*\n");
                    multi_lines = ABT_FALSE;
                }
            }

            /* Print the stack address */
#if SIZEOF_VOID_P == 8
            fprintf(p_os, "%016" PRIxPTR ":",
                    (uintptr_t)(&((uint8_t *)p_stack)[i]));
#elif SIZEOF_VOID_P == 4
            fprintf(p_os, "%08" PRIxPTR ":",
                    (uintptr_t)(&((uint8_t *)p_stack)[i]));
#else
#error "unknown pointer size"
#endif
            /* Print the raw stack data */
            for (j = 0; j < num_bytes / value_width; j++) {
                if (value_width == 8) {
                    uint64_t val = ((uint64_t *)buffer)[j];
                    fprintf(p_os, " %016" PRIx64, val);
                } else if (value_width == 4) {
                    uint32_t val = ((uint32_t *)buffer)[j];
                    fprintf(p_os, " %08" PRIx32, val);
                } else if (value_width == 2) {
                    uint16_t val = ((uint16_t *)buffer)[j];
                    fprintf(p_os, " %04" PRIx16, val);
                } else {
                    uint8_t val = ((uint8_t *)buffer)[j];
                    fprintf(p_os, " %02" PRIx8, val);
                }
                if (j == (num_bytes / value_width) - 1)
                    fprintf(p_os, "\n");
            }
        }
    }
    fflush(p_os);
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

#ifdef ABT_CONFIG_ENABLE_STACK_UNWIND
ABTU_no_sanitize_address static int ythread_unwind_stack_impl(FILE *fp)
{
    unw_cursor_t cursor;
    unw_context_t uc;
    unw_word_t ip, sp;
    int ret, level = -1;

    ret = unw_getcontext(&uc);
    if (ret != 0)
        return ABT_ERR_OTHER;

    ret = unw_init_local(&cursor, &uc);
    if (ret != 0)
        return ABT_ERR_OTHER;

    while (unw_step(&cursor) > 0 && level < 50) {
        level++;

        ret = unw_get_reg(&cursor, UNW_REG_IP, &ip);
        if (ret != 0)
            return ABT_ERR_OTHER;

        ret = unw_get_reg(&cursor, UNW_REG_SP, &sp);
        if (ret != 0)
            return ABT_ERR_OTHER;

        char proc_name[256];
        unw_word_t offset;
        ret = unw_get_proc_name(&cursor, proc_name, 256, &offset);
        if (ret != 0)
            return ABT_ERR_OTHER;

        /* Print function stack. */
        fprintf(fp, "#%d %p in %s () <+%d> (%s = %p)\n", level,
                (void *)((uintptr_t)ip), proc_name, (int)offset,
                unw_regname(UNW_REG_SP), (void *)((uintptr_t)sp));
    }
    return ABT_SUCCESS;
}

static void ythread_unwind_stack(void *arg)
{
    struct unwind_stack_t *p_arg = (struct unwind_stack_t *)arg;
    if (ythread_unwind_stack_impl(p_arg->fp) != ABT_SUCCESS) {
        fprintf(p_arg->fp, "libunwind error\n");
    }
}

#endif /* ABT_CONFIG_ENABLE_STACK_UNWIND */
