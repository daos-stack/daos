/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "abt.h"
#include "abttest.h"

#define DEFAULT_NUM_XSTREAMS 4
#define DEFAULT_NUM_THREADS 100

typedef struct {
    ABT_xstream xstream;
    ABT_thread suspend_thread;
    ABT_thread next_thread;
} xstream_info_t;

int g_num_xstreams = DEFAULT_NUM_XSTREAMS;
xstream_info_t *g_xstreams;
ABT_pool *g_pools;

void thread_func(void *arg)
{
    ABT_thread self;
    int ret, rank, i;
    ret = ABT_self_get_thread(&self);
    ATS_ERROR(ret, "ABT_self_get_thread");

    for (i = 0; i < 20; i++) {
        ret = ABT_self_get_xstream_rank(&rank);
        ATS_ERROR(ret, "ABT_self_get_xstream_rank");
        assert(g_xstreams[rank - 1].next_thread == ABT_THREAD_NULL ||
               g_xstreams[rank - 1].next_thread == self);
        if (g_xstreams[rank - 1].suspend_thread != ABT_THREAD_NULL) {
            /* If a threads is suspended, resume that thread. */
            ABT_thread target = g_xstreams[rank - 1].suspend_thread;
            if (i % 3 == 0) {
                /* Normal resume */
                g_xstreams[rank - 1].suspend_thread = ABT_THREAD_NULL;
                g_xstreams[rank - 1].next_thread = ABT_THREAD_NULL;
                ret = ABT_thread_resume(target);
                ATS_ERROR(ret, "ABT_thread_resume");
            } else if (i % 3 == 1) {
                /* resume_yield_to */
                g_xstreams[rank - 1].suspend_thread = ABT_THREAD_NULL;
                g_xstreams[rank - 1].next_thread = target;
                ret = ABT_self_resume_yield_to(target);
                ATS_ERROR(ret, "ABT_self_resume_yield_to");
            } else if (i % 3 == 2) {
                /* resume_suspend_to */
                g_xstreams[rank - 1].suspend_thread = self;
                g_xstreams[rank - 1].next_thread = target;
                ret = ABT_self_resume_suspend_to(target);
                ATS_ERROR(ret, "ABT_self_resume_suspend_to");
            }
        } else {
            /* Randomly get the next ULT. */
            int victim_id = i % g_num_xstreams;
            ABT_unit unit;
            ret = ABT_pool_pop(g_pools[victim_id], &unit);
            ATS_ERROR(ret, "ABT_pool_pop");
            if (unit != ABT_UNIT_NULL) {
                /* Suspend to that ULT. */
                ABT_thread target;
                ret = ABT_unit_get_thread(unit, &target);
                ATS_ERROR(ret, "ABT_unit_get_thread");
                g_xstreams[rank - 1].suspend_thread = self;
                g_xstreams[rank - 1].next_thread = target;
                ret = ABT_self_suspend_to(target);
                ATS_ERROR(ret, "ABT_self_suspend_to");
            } else {
                /* Failed to get the next ULT.  Let's just yield. */
                g_xstreams[rank - 1].suspend_thread = ABT_THREAD_NULL;
                g_xstreams[rank - 1].next_thread = ABT_THREAD_NULL;
                ret = ABT_self_yield();
                ATS_ERROR(ret, "ABT_self_yield");
            }
        }
    }
    /* Finish this ULT. */
    ret = ABT_self_get_xstream_rank(&rank);
    ATS_ERROR(ret, "ABT_self_get_xstream_rank");
    assert(g_xstreams[rank - 1].next_thread == ABT_THREAD_NULL ||
           g_xstreams[rank - 1].next_thread == self);
    /* A thread is waiting.  Let's resume-exit to that thread. */
    if (g_xstreams[rank - 1].suspend_thread != ABT_THREAD_NULL) {
        ABT_thread target = g_xstreams[rank - 1].suspend_thread;
        g_xstreams[rank - 1].next_thread = target;
        g_xstreams[rank - 1].suspend_thread = ABT_THREAD_NULL;
        ret = ABT_self_resume_exit_to(target);
        ATS_ERROR(ret, "ABT_self_resume_exit_to");
    } else {
        g_xstreams[rank - 1].next_thread = ABT_THREAD_NULL;
    }
}

int main(int argc, char *argv[])
{
    int i;
    int ret;
    int num_threads = DEFAULT_NUM_THREADS;
    if (argc > 1)
        g_num_xstreams = atoi(argv[1]);
    assert(g_num_xstreams >= 0);
    if (argc > 2)
        num_threads = atoi(argv[2]);
    assert(num_threads >= 0);

    g_xstreams =
        (xstream_info_t *)malloc(sizeof(xstream_info_t) * g_num_xstreams);
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);
    g_pools = (ABT_pool *)malloc(sizeof(ABT_pool) * g_num_xstreams);
    ABT_sched *scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * g_num_xstreams);

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, g_num_xstreams + 1);

    /* Create pools. */
    for (i = 0; i < g_num_xstreams; i++) {
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_TRUE, &g_pools[i]);
        ATS_ERROR(ret, "ABT_pool_create_basic");
    }

    /* Create schedulers. */
    for (i = 0; i < g_num_xstreams; i++) {
        int j;
        ABT_pool *tmp = (ABT_pool *)malloc(sizeof(ABT_pool) * g_num_xstreams);
        for (j = 0; j < g_num_xstreams; j++) {
            tmp[j] = g_pools[(i + j) % g_num_xstreams];
        }
        ret = ABT_sched_create_basic(ABT_SCHED_DEFAULT, g_num_xstreams, tmp,
                                     ABT_SCHED_CONFIG_NULL, &scheds[i]);
        ATS_ERROR(ret, "ABT_sched_create_basic");
        free(tmp);
    }

    /* Create secondary execution streams. */
    for (i = 0; i < g_num_xstreams; i++) {
        g_xstreams[i].next_thread = ABT_THREAD_NULL;
        g_xstreams[i].suspend_thread = ABT_THREAD_NULL;
        ret = ABT_xstream_create(scheds[i], &g_xstreams[i].xstream);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");
    }

    /* Create named threads */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_create(g_pools[i % g_num_xstreams], thread_func, NULL,
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }

    /* Create unnamed threads */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_create(g_pools[i % g_num_xstreams], thread_func, NULL,
                                ABT_THREAD_ATTR_NULL, NULL);
        ATS_ERROR(ret, "ABT_thread_create");
    }

    /* Join named threads */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }

    /* Join secondary execution streams. */
    for (i = 0; i < g_num_xstreams; i++) {
        ret = ABT_xstream_free(&g_xstreams[i].xstream);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);

    /* Free allocated memory. */
    free(g_xstreams);
    free(threads);
    free(g_pools);
    free(scheds);

    return ret;
}
