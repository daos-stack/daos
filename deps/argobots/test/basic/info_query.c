/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "abt.h"
#include "abttest.h"

/* Check if ABT_info_query_config() returns a consistent result. */

enum val_type_kind {
    VAL_TYPE_ABT_BOOL,
    VAL_TYPE_INT,
    VAL_TYPE_UNSIGNED_INT,
    VAL_TYPE_UINT64_T,
    VAL_TYPE_SIZE_T,
};

typedef enum val_type_kind val_type_kind;

typedef struct info_query_t {
    ABT_info_query_kind query_kind;
    ABT_bool need_init; /* Experimental ver-2.0 API */
    val_type_kind type;

    int buffer_idx;
    uint64_t buffers[64];
    struct info_query_t *p_next;
} info_query_t;

info_query_t *gp_query = NULL;

void add_info_query_t(ABT_info_query_kind query_kind, ABT_bool need_init,
                      val_type_kind type)
{
    info_query_t *p_query = (info_query_t *)calloc(1, sizeof(info_query_t));
    p_query->query_kind = query_kind;
    p_query->need_init = need_init;
    p_query->type = type;
    p_query->p_next = gp_query;
    gp_query = p_query;
}

void info_query_all(ABT_bool init)
{
#ifndef ABT_ENABLE_VER_20_API
    /* Argobots 1.x does not allow calling ABT_info_query_config() when the
     * runtime is not initialized. */
    if (!init)
        return;
#endif
    info_query_t *p_query = gp_query;
    while (p_query) {
        if (!(p_query->need_init && !init)) {
            const int idx = p_query->buffer_idx++;
            int32_t *ptr = (int32_t *)(&p_query->buffers[1 + (idx)*3]);
            ptr[-1] = 0x77777777;
            ptr[0] = 0x77777777;
            ptr[1] = 0x77777777;
            ptr[2] = 0x77777777;
            int ret = ABT_info_query_config(p_query->query_kind, (void *)ptr);
            ATS_ERROR(ret, "ABT_info_query_config");
            /* Check the size */
            int nbytes = 0;
            if (p_query->type == VAL_TYPE_ABT_BOOL) {
                nbytes = sizeof(ABT_bool);
                ABT_bool val = *((ABT_bool *)ptr);
                assert(val == ABT_TRUE || val == ABT_FALSE);
            } else if (p_query->type == VAL_TYPE_INT) {
                nbytes = sizeof(int);
            } else if (p_query->type == VAL_TYPE_UNSIGNED_INT) {
                nbytes = sizeof(unsigned int);
            } else if (p_query->type == VAL_TYPE_UINT64_T) {
                nbytes = sizeof(uint64_t);
            } else if (p_query->type == VAL_TYPE_SIZE_T) {
                nbytes = sizeof(size_t);
            }
            /* ABT_info_query_config may not overwrite memories around ptr. */
            if (nbytes <= 4) {
                assert(ptr[-1] == 0x77777777);
                assert(ptr[1] == 0x77777777);
            } else if (nbytes == 8) {
                assert(ptr[-1] == 0x77777777);
                assert(ptr[2] == 0x77777777);
            } else {
                /* We do not consider such a system. */
                exit(77);
            }
            /* Check consistency. */
            if (idx > 0) {
                int32_t *prev_ptr =
                    (int32_t *)(&p_query->buffers[1 + (idx - 1) * 3]);
                /* The value must be the same as the previous one. */
                assert(ptr[-1] == prev_ptr[-1]);
                assert(ptr[0] == prev_ptr[0]);
                assert(ptr[1] == prev_ptr[1]);
                assert(ptr[2] == prev_ptr[2]);
            }
        }
        p_query = p_query->p_next;
    }
}

void info_query_free()
{
    info_query_t *p_query = gp_query;
    while (p_query) {
        info_query_t *p_next = p_query->p_next;
        free(p_query);
        p_query = p_next;
    }
}

int main(int argc, char *argv[])
{
    int ret;

    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_DEBUG, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_PRINT_ERRNO, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_LOG, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_VALGRIND, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_CHECK_POOL_PRODUCER, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_CHECK_POOL_CONSUMER, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_PRESERVE_FPU, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_THREAD_CANCEL, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_TASK_CANCEL, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_MIGRATION, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_STACKABLE_SCHED, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_SCHED_SLEEP, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_PRINT_CONFIG, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_AFFINITY, ABT_TRUE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_MAX_NUM_XSTREAMS, ABT_FALSE,
                     VAL_TYPE_UNSIGNED_INT);
    add_info_query_t(ABT_INFO_QUERY_KIND_DEFAULT_THREAD_STACKSIZE, ABT_FALSE,
                     VAL_TYPE_SIZE_T);
    add_info_query_t(ABT_INFO_QUERY_KIND_DEFAULT_SCHED_STACKSIZE, ABT_FALSE,
                     VAL_TYPE_SIZE_T);
    add_info_query_t(ABT_INFO_QUERY_KIND_DEFAULT_SCHED_EVENT_FREQ, ABT_FALSE,
                     VAL_TYPE_UINT64_T);
    add_info_query_t(ABT_INFO_QUERY_KIND_DEFAULT_SCHED_SLEEP_NSEC, ABT_FALSE,
                     VAL_TYPE_UINT64_T);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_TOOL, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_FCONTEXT, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_DYNAMIC_PROMOTION, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_STACK_UNWIND, ABT_FALSE,
                     VAL_TYPE_ABT_BOOL);
    add_info_query_t(ABT_INFO_QUERY_KIND_ENABLED_STACK_OVERFLOW_CHECK,
                     ABT_FALSE, VAL_TYPE_INT);
    add_info_query_t(ABT_INFO_QUERY_KIND_WAIT_POLICY, ABT_FALSE, VAL_TYPE_INT);

    info_query_all(0);
    ret = ABT_init(argc, argv);
    ATS_ERROR(ret, "ABT_init");

    info_query_all(1);
    info_query_all(1);
    ret = ABT_finalize();
    ATS_ERROR(ret, "ABT_finalize");

    info_query_all(0);
    info_query_free();
    return ret;
}
