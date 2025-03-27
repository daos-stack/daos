/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static void *xstream_context_thread_func(void *arg)
{
    ABTD_xstream_context *p_ctx = (ABTD_xstream_context *)arg;
    void *(*thread_f)(void *) = p_ctx->thread_f;
    void *p_arg = p_ctx->p_arg;
    ABTI_ASSERT(p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_RUNNING);
    while (1) {
        /* Execute a main execution stream function. */
        thread_f(p_arg);
        /* This thread has finished. */
        ABT_bool restart;
        pthread_mutex_lock(&p_ctx->state_lock);
        /* If another execution stream is waiting for this thread completion,
         * let's wake it up. */
        if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_REQ_JOIN) {
            pthread_cond_signal(&p_ctx->state_cond);
        }
        p_ctx->state = ABTD_XSTREAM_CONTEXT_STATE_WAITING;
        /* Wait for a request from ABTD_xstream_context_free() or
         * ABTD_xstream_context_restart().
         * The following loop is to deal with spurious wakeup. */
        do {
            pthread_cond_wait(&p_ctx->state_cond, &p_ctx->state_lock);
        } while (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_WAITING);
        if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_REQ_TERMINATE) {
            /* ABTD_xstream_context_free() terminates this thread. */
            restart = ABT_FALSE;
        } else {
            /* ABTD_xstream_context_restart() restarts this thread */
            ABTI_ASSERT(p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_RUNNING ||
                        p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_REQ_JOIN);
            restart = ABT_TRUE;
        }
        pthread_mutex_unlock(&p_ctx->state_lock);
        if (!restart)
            break;
    }
    return NULL;
}

ABTU_ret_err int ABTD_xstream_context_create(void *(*f_xstream)(void *),
                                             void *p_arg,
                                             ABTD_xstream_context *p_ctx)
{
    p_ctx->thread_f = f_xstream;
    p_ctx->p_arg = p_arg;
    /* Coverity thinks p_ctx->state must be updated with a lock since it is
     * updated with a lock in the other places.  This assumption is wrong.  The
     * following suppresses a false positive. */
    /* coverity[missing_lock] */
    p_ctx->state = ABTD_XSTREAM_CONTEXT_STATE_RUNNING;
    int ret, init_stage = 0;
    ret = pthread_mutex_init(&p_ctx->state_lock, NULL);
    if (ret != 0)
        goto FAILED;
    init_stage = 1;

    ret = pthread_cond_init(&p_ctx->state_cond, NULL);
    if (ret != 0)
        goto FAILED;
    init_stage = 2;

    ret = pthread_create(&p_ctx->native_thread, NULL,
                         xstream_context_thread_func, p_ctx);
    if (ret != 0)
        goto FAILED;
    init_stage = 3;

    return ABT_SUCCESS;
FAILED:
    if (init_stage >= 2) {
        ret = pthread_cond_destroy(&p_ctx->state_cond);
        ABTI_ASSERT(ret == 0);
    }
    if (init_stage >= 1) {
        ret = pthread_mutex_destroy(&p_ctx->state_lock);
        ABTI_ASSERT(ret == 0);
    }
    p_ctx->state = ABTD_XSTREAM_CONTEXT_STATE_UNINIT;
    ABTI_HANDLE_ERROR(ABT_ERR_SYS);
}

void ABTD_xstream_context_free(ABTD_xstream_context *p_ctx)
{
    /* Request termination */
    if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_UNINIT) {
        /* Do nothing. */
    } else {
        pthread_mutex_lock(&p_ctx->state_lock);
        ABTI_ASSERT(p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_WAITING);
        p_ctx->state = ABTD_XSTREAM_CONTEXT_STATE_REQ_TERMINATE;
        pthread_cond_signal(&p_ctx->state_cond);
        pthread_mutex_unlock(&p_ctx->state_lock);
        /* Join the target thread. */
        int ret;
        ret = pthread_join(p_ctx->native_thread, NULL);
        ABTI_ASSERT(ret == 0);
        ret = pthread_cond_destroy(&p_ctx->state_cond);
        ABTI_ASSERT(ret == 0);
        ret = pthread_mutex_destroy(&p_ctx->state_lock);
        ABTI_ASSERT(ret == 0);
    }
}

void ABTD_xstream_context_join(ABTD_xstream_context *p_ctx)
{
    /* If not finished, sleep this thread. */
    pthread_mutex_lock(&p_ctx->state_lock);
    if (p_ctx->state != ABTD_XSTREAM_CONTEXT_STATE_WAITING) {
        ABTI_ASSERT(p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_RUNNING);
        p_ctx->state = ABTD_XSTREAM_CONTEXT_STATE_REQ_JOIN;
        /* The following loop is to deal with spurious wakeup. */
        do {
            pthread_cond_wait(&p_ctx->state_cond, &p_ctx->state_lock);
        } while (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_REQ_JOIN);
    }
    ABTI_ASSERT(p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_WAITING);
    pthread_mutex_unlock(&p_ctx->state_lock);
}

void ABTD_xstream_context_revive(ABTD_xstream_context *p_ctx)
{
    /* Request restart */
    pthread_mutex_lock(&p_ctx->state_lock);
    ABTI_ASSERT(p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_WAITING);
    p_ctx->state = ABTD_XSTREAM_CONTEXT_STATE_RUNNING;
    pthread_cond_signal(&p_ctx->state_cond);
    pthread_mutex_unlock(&p_ctx->state_lock);
}

void ABTD_xstream_context_set_self(ABTD_xstream_context *p_ctx)
{
    p_ctx->native_thread = pthread_self();
}

void ABTD_xstream_context_print(ABTD_xstream_context *p_ctx, FILE *p_os,
                                int indent)
{
    if (p_ctx == NULL) {
        fprintf(p_os, "%*s== NULL XSTREAM CONTEXT ==\n", indent, "");
    } else {
        const char *state;
        if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_RUNNING) {
            state = "RUNNING";
        } else if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_WAITING) {
            state = "WAITING";
        } else if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_REQ_JOIN) {
            state = "REQ_JOIN";
        } else if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_REQ_TERMINATE) {
            state = "REQ_TERMINATE";
        } else if (p_ctx->state == ABTD_XSTREAM_CONTEXT_STATE_UNINIT) {
            state = "UNINIT";
        } else {
            state = "UNKNOWN";
        }
        fprintf(p_os,
                "%*s== XSTREAM CONTEXT (%p) ==\n"
                "%*sstate : %s\n",
                indent, "", (void *)p_ctx, indent, "", state);
    }
    fflush(p_os);
}
