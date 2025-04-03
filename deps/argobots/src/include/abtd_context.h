/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTD_CONTEXT_H_INCLUDED
#define ABTD_CONTEXT_H_INCLUDED

#include "abt_config.h"

#ifndef ABT_CONFIG_USE_FCONTEXT
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>
#endif

typedef struct ABTD_ythread_context ABTD_ythread_context;

typedef struct ABTD_ythread_context_atomic_ptr {
    ABTD_atomic_ptr val;
} ABTD_ythread_context_atomic_ptr;

static inline ABTD_ythread_context *
ABTD_atomic_relaxed_load_ythread_context_ptr(
    const ABTD_ythread_context_atomic_ptr *ptr)
{
    return (ABTD_ythread_context *)ABTD_atomic_relaxed_load_ptr(&ptr->val);
}

static inline ABTD_ythread_context *
ABTD_atomic_acquire_load_ythread_context_ptr(
    const ABTD_ythread_context_atomic_ptr *ptr)
{
    return (ABTD_ythread_context *)ABTD_atomic_acquire_load_ptr(&ptr->val);
}

static inline void ABTD_atomic_relaxed_store_ythread_context_ptr(
    ABTD_ythread_context_atomic_ptr *ptr, ABTD_ythread_context *p_ctx)
{
    ABTD_atomic_relaxed_store_ptr(&ptr->val, (void *)p_ctx);
}

static inline void ABTD_atomic_release_store_ythread_context_ptr(
    ABTD_ythread_context_atomic_ptr *ptr, ABTD_ythread_context *p_ctx)
{
    ABTD_atomic_release_store_ptr(&ptr->val, (void *)p_ctx);
}

void ABTD_ythread_func_wrapper(ABTD_ythread_context *p_arg);
void ABTD_ythread_print_context(ABTI_ythread *p_ythread, FILE *p_os,
                                int indent);

#ifdef ABT_CONFIG_USE_FCONTEXT
#include "abtd_fcontext.h"
#else
#include "abtd_ucontext.h"
#endif

#endif /* ABTD_CONTEXT_H_INCLUDED */
