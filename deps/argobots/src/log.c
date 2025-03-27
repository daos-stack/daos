/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdarg.h>
#include "abti.h"

#ifdef ABT_CONFIG_USE_DEBUG_LOG

void ABTI_log_debug(const char *format, ...)
{
    ABTI_global *p_global = ABTI_global_get_global_or_null();
    if (!p_global || p_global->use_logging == ABT_FALSE)
        return;
    ABTI_local *p_local = ABTI_local_get_local_uninlined();

    const char *prefix_fmt = NULL, *prefix = NULL;
    char static_buffer[256], *newfmt;
    uint64_t tid;
    int rank;
    size_t newfmt_len;

    ABTI_xstream *p_local_xstream = ABTI_local_get_xstream_or_null(p_local);
    if (p_local_xstream) {
        ABTI_ythread *p_ythread =
            ABTI_thread_get_ythread(p_local_xstream->p_thread);
        if (p_ythread == NULL) {
            if (p_local_xstream->type != ABTI_XSTREAM_TYPE_PRIMARY) {
                prefix_fmt = "<U%" PRIu64 ":E%d> %s";
                rank = p_local_xstream->rank;
                tid = 0;
            } else {
                prefix = "<U0:E0> ";
                prefix_fmt = "%s%s";
            }
        } else {
            rank = p_local_xstream->rank;
            prefix_fmt = "<U%" PRIu64 ":E%d> %s";
            tid = ABTI_thread_get_id(&p_ythread->thread);
        }
    } else {
        prefix = "<EXT> ";
        prefix_fmt = "%s%s";
    }

    if (prefix == NULL) {
        /* Both tid and rank are less than 42 characters in total. */
        const int len_tid_rank = 50;
        newfmt_len = 6 + len_tid_rank + strlen(format);
        if (sizeof(static_buffer) >= newfmt_len + 1) {
            newfmt = static_buffer;
        } else {
            int abt_errno = ABTU_malloc(newfmt_len + 1, (void **)&newfmt);
            if (abt_errno != ABT_SUCCESS)
                return;
        }
        sprintf(newfmt, prefix_fmt, tid, rank, format);
    } else {
        newfmt_len = strlen(prefix) + strlen(format);
        if (sizeof(static_buffer) >= newfmt_len + 1) {
            newfmt = static_buffer;
        } else {
            int abt_errno = ABTU_malloc(newfmt_len + 1, (void **)&newfmt);
            if (abt_errno != ABT_SUCCESS)
                return;
        }
        sprintf(newfmt, prefix_fmt, prefix, format);
    }

#ifndef ABT_CONFIG_USE_DEBUG_LOG_DISCARD
    va_list list;
    va_start(list, format);
    vfprintf(stderr, newfmt, list);
    va_end(list);
    fflush(stderr);
#else
    /* Discard the log message.  This option is used to check if the logging
     * function works correct (i.e., without any SEGV) but a tester does not
     * need an actual log since the output can be extremely large. */
#endif
    if (newfmt != static_buffer) {
        ABTU_free(newfmt);
    }
}

void ABTI_log_debug_thread(const char *msg, ABTI_thread *p_thread)
{
    if (!p_thread) {
        /* Unknown thread. */
        ABTI_log_debug("%s [unknown ULT]\n", msg);
    } else if (p_thread->type & ABTI_THREAD_TYPE_ROOT) {
        /* This should not appear in a log. */
    } else if (p_thread->type & ABTI_THREAD_TYPE_PRIMARY) {
        /* Primary ULT. */
        ABTI_log_debug("%s U%" PRIu64 " (primary)\n", msg,
                       ABTI_thread_get_id(p_thread));
    } else if (p_thread->type & ABTI_THREAD_TYPE_MAIN_SCHED) {
        /* Main scheduler ULT. */
        ABTI_log_debug("%s U%" PRIu64 " (main sched)\n", msg,
                       ABTI_thread_get_id(p_thread));
    } else if (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) {
        /* Normal yieldable ULT. */
        ABTI_log_debug("%s U%" PRIu64 "\n", msg, ABTI_thread_get_id(p_thread));
    } else {
        /* Not yieldable. */
        ABTI_log_debug("%s T%" PRIu64 "\n", msg, ABTI_thread_get_id(p_thread));
    }
}

void ABTI_log_pool_push(ABTI_pool *p_pool, ABT_unit unit)
{
    ABTI_global *p_global = ABTI_global_get_global_or_null();
    if (!p_global || p_global->use_logging == ABT_FALSE)
        return;
    if (unit == ABT_UNIT_NULL)
        return;

    ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
    char unit_type = (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) ? 'U' : 'T';
    if (p_thread->p_last_xstream) {
        ABTI_log_debug("[%c%" PRIu64 ":E%d] pushed to P%" PRIu64 "\n",
                       unit_type, ABTI_thread_get_id(p_thread),
                       p_thread->p_last_xstream->rank, p_pool->id);
    } else {
        ABTI_log_debug("[%c%" PRIu64 "] pushed to P%" PRIu64 "\n", unit_type,
                       ABTI_thread_get_id(p_thread), p_pool->id);
    }
}

void ABTI_log_pool_remove(ABTI_pool *p_pool, ABT_unit unit)
{
    ABTI_global *p_global = ABTI_global_get_global_or_null();
    if (!p_global || p_global->use_logging == ABT_FALSE)
        return;
    if (unit == ABT_UNIT_NULL)
        return;

    ABTI_thread *p_thread = ABTI_unit_get_thread(p_global, unit);
    char unit_type = (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) ? 'U' : 'T';
    if (p_thread->p_last_xstream) {
        ABTI_log_debug("[%c%" PRIu64 ":E%d] removed from P%" PRIu64 "\n",
                       unit_type, ABTI_thread_get_id(p_thread),
                       p_thread->p_last_xstream->rank, p_pool->id);
    } else {
        ABTI_log_debug("[%c%" PRIu64 "] removed from P%" PRIu64 "\n", unit_type,
                       ABTI_thread_get_id(p_thread), p_pool->id);
    }
}

void ABTI_log_pool_pop(ABTI_pool *p_pool, ABT_thread thread)
{
    ABTI_global *p_global = ABTI_global_get_global_or_null();
    if (!p_global || p_global->use_logging == ABT_FALSE)
        return;
    if (thread == ABT_THREAD_NULL)
        return;

    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    char unit_type = (p_thread->type & ABTI_THREAD_TYPE_YIELDABLE) ? 'U' : 'T';
    if (p_thread->p_last_xstream) {
        ABTI_log_debug("[%c%" PRIu64 ":E%d] popped from P%" PRIu64 "\n",
                       unit_type, ABTI_thread_get_id(p_thread),
                       p_thread->p_last_xstream->rank, p_pool->id);
    } else {
        ABTI_log_debug("[%c%" PRIu64 "] popped from P%" PRIu64 "\n", unit_type,
                       ABTI_thread_get_id(p_thread), p_pool->id);
    }
}

void ABTI_log_pool_pop_many(ABTI_pool *p_pool, const ABT_thread *threads,
                            size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        ABTI_log_pool_pop(p_pool, threads[i]);
    }
}

void ABTI_log_pool_push_many(ABTI_pool *p_pool, const ABT_unit *units,
                             size_t num)
{
    size_t i;
    for (i = 0; i < num; i++) {
        ABTI_log_pool_push(p_pool, units[i]);
    }
}

#endif /* ABT_CONFIG_USE_DEBUG_LOG */
