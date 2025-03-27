/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

void ABTD_ythread_func_wrapper(ABTD_ythread_context *p_arg)
{
    ABTD_ythread_context *p_ctx = p_arg;
    ABTI_ythread *p_ythread = ABTI_ythread_context_get_ythread(p_ctx);
    p_ythread->thread.f_thread(p_ythread->thread.p_arg);

    ABTI_xstream *p_local_xstream = p_ythread->thread.p_last_xstream;
    ABTI_ythread_exit(p_local_xstream, p_ythread);
}

void ABTD_ythread_print_context(ABTI_ythread *p_ythread, FILE *p_os, int indent)
{
    ABTD_ythread_context *p_ctx = &p_ythread->ctx;
    fprintf(p_os, "%*sp_ctx     : %p\n", indent, "", (void *)p_ctx);
    fprintf(p_os, "%*sp_link    : %p\n", indent, "",
            (void *)ABTD_atomic_acquire_load_ythread_context_ptr(
                &p_ctx->p_link));
    fflush(p_os);
}
