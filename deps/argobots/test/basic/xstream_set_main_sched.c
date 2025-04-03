/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include "abt.h"
#include "abttest.h"

#define NUM_POOLS 3
#define NUM_ITERS 100
#define DEFAULT_NUM_XSTREAMS 4
#define DEFAULT_NUM_THREADS 10

int g_num_xstreams;
ABT_pool *g_pools;

typedef struct {
    int num_pools;
    ABT_pool *pools;
} sched_data_t;

int sched_init(ABT_sched sched, ABT_sched_config config)
{
    int ret;
    sched_data_t *p_data = (sched_data_t *)malloc(sizeof(sched_data_t));
    ret = ABT_sched_get_num_pools(sched, &p_data->num_pools);
    ATS_ERROR(ret, "ABT_sched_get_num_pools");
    assert(p_data->num_pools >= 0);
    p_data->pools = (ABT_pool *)malloc(sizeof(ABT_pool) * p_data->num_pools);
    ret = ABT_sched_get_pools(sched, p_data->num_pools, 0, p_data->pools);
    ATS_ERROR(ret, "ABT_sched_get_pools");
    ret = ABT_sched_set_data(sched, (void *)p_data);
    ATS_ERROR(ret, "ABT_sched_set_data");
    return ABT_SUCCESS;
}

void sched_run(ABT_sched sched)
{
    int ret;
    sched_data_t *p_data;
    ret = ABT_sched_get_data(sched, (void **)&p_data);
    ATS_ERROR(ret, "ABT_sched_get_data");
    int work_count = 0;
    while (1) {
        int i;
        for (i = 0; i < p_data->num_pools; i++) {
            if (work_count % 5 < 3) {
                ABT_unit unit;
                ret = ABT_pool_pop(p_data->pools[i], &unit);
                ATS_ERROR(ret, "ABT_pool_pop");
                if (unit != ABT_UNIT_NULL) {
                    if (work_count % 5 == 0) {
                        ABT_xstream_run_unit(unit, p_data->pools[i]);
                    } else if (work_count % 5 == 1) {
                        ABT_thread thread;
                        ret = ABT_unit_get_thread(unit, &thread);
                        ATS_ERROR(ret, "ABT_unit_get_thread");
                        ret = ABT_self_schedule(thread, ABT_POOL_NULL);
                        ATS_ERROR(ret, "ABT_self_schedule");
                    } else {
                        ABT_thread thread;
                        ret = ABT_unit_get_thread(unit, &thread);
                        ATS_ERROR(ret, "ABT_unit_get_thread");
                        ret = ABT_self_schedule(thread, p_data->pools[i]);
                        ATS_ERROR(ret, "ABT_self_schedule");
                    }
                }
            } else {
                /* work_count == 3 or 4 */
                ABT_thread thread;
                ret = ABT_pool_pop_thread(p_data->pools[i], &thread);
                ATS_ERROR(ret, "ABT_pool_pop_thread");
                if (thread != ABT_THREAD_NULL) {
                    if (work_count % 5 == 3) {
                        ret = ABT_self_schedule(thread, ABT_POOL_NULL);
                        ATS_ERROR(ret, "ABT_self_schedule");
                    } else {
                        ret = ABT_self_schedule(thread, p_data->pools[i]);
                        ATS_ERROR(ret, "ABT_self_schedule");
                    }
                }
            }
        }
        if (++work_count >= 16) {
            ret = ABT_xstream_check_events(sched);
            ATS_ERROR(ret, "ABT_xstream_check_events");
            ABT_bool stop;
            ret = ABT_sched_has_to_stop(sched, &stop);
            ATS_ERROR(ret, "ABT_sched_has_to_stop");
            if (stop == ABT_TRUE)
                break;
            work_count = 0;
        }
    }
}

int sched_free(ABT_sched sched)
{
    sched_data_t *p_data;
    int ret = ABT_sched_get_data(sched, (void **)&p_data);
    ATS_ERROR(ret, "ABT_sched_get_data");
    free(p_data->pools);
    free(p_data);
    return ABT_SUCCESS;
}

/* Change the scheduler. */
void change_main_sched(int num_pools, ABT_pool *pools, int is_basic)
{
    int ret;
    while (1) {
        int rank;
        ret = ABT_self_get_xstream_rank(&rank);
        ATS_ERROR(ret, "ABT_get_xstream_rank");
        if (rank != g_num_xstreams - 1)
            break;

        /* The last execution stream should keep the main scheduler that has all
         * the NUM_POOLS pools: this is necessary to keep all the pools not
         * automatically freed by the runtime. */
        ret = ABT_self_yield();
        ATS_ERROR(ret, "ABT_self_yield");
    }
    ABT_xstream self_xstream;
    ret = ABT_self_get_xstream(&self_xstream);
    ATS_ERROR(ret, "ABT_self_get_xstream");
    if (is_basic) {
        ret = ABT_xstream_set_main_sched_basic(self_xstream, ABT_SCHED_DEFAULT,
                                               num_pools, pools);
        ATS_ERROR(ret, "ABT_xstream_set_main_sched_basic");
    } else {
        /* Create a custom scheduler. */
        ABT_sched_def sched_def = { .type = ABT_SCHED_TYPE_ULT,
                                    .init = sched_init,
                                    .run = sched_run,
                                    .free = sched_free,
                                    .get_migr_pool = NULL };
        ABT_sched_config config;
        ret = ABT_sched_config_create(&config, ABT_sched_config_automatic, 1,
                                      ABT_sched_config_var_end);
        ATS_ERROR(ret, "ABT_sched_config_create");
        ABT_sched sched;
        ret = ABT_sched_create(&sched_def, num_pools, pools, config, &sched);
        ATS_ERROR(ret, "ABT_sched_create");
        ret = ABT_sched_config_free(&config);
        ATS_ERROR(ret, "ABT_sched_config_free");
        ret = ABT_xstream_set_main_sched(self_xstream, sched);
        ATS_ERROR(ret, "ABT_xstream_set_main_sched_basic");
        /* sched will be freed automatically. */
    }
}

void thread_func(void *arg)
{
    int i;
    ABT_pool *pools = (ABT_pool *)malloc(sizeof(ABT_pool) * NUM_POOLS);
    for (i = 0; i < NUM_ITERS; i++) {
        int num_pools = 1;
        pools[0] = g_pools[i % NUM_POOLS];
        int is_basic = (i / NUM_POOLS) % 2;
        if (((i / NUM_POOLS / 2) % 2) && NUM_POOLS > 1) {
            num_pools++;
            pools[1] = g_pools[(i + 1) % NUM_POOLS];
        }
        if (((i / NUM_POOLS / 4) % 2) && NUM_POOLS > 2) {
            num_pools++;
            pools[2] = g_pools[(i + 2) % NUM_POOLS];
        }
        change_main_sched(num_pools, pools, is_basic);
        /* Sometimes we can yield. */
        if ((i / NUM_POOLS / 8) % 2 == 0) {
            int ret = ABT_self_yield();
            ATS_ERROR(ret, "ABT_self_yield");
        }
    }
    /* Before finishing this thread, we should guarantee that each pool is
     * checked by at least one scheduler. */
    change_main_sched(NUM_POOLS, g_pools, 1);
    free(pools);
}

int main(int argc, char *argv[])
{
    int num_threads;
    int i, ret;

    /* Initialize */
    ATS_read_args(argc, argv);
    if (argc < 2) {
        g_num_xstreams = DEFAULT_NUM_XSTREAMS;
        num_threads = DEFAULT_NUM_THREADS;
    } else {
        g_num_xstreams = ATS_get_arg_val(ATS_ARG_N_ES);
        num_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
    }
    if (g_num_xstreams < 2)
        g_num_xstreams = 2; /* The last execution stream is to keep pools. */

    /* Allocate memory. */
    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * g_num_xstreams);
    g_pools = (ABT_pool *)malloc(sizeof(ABT_pool) * NUM_POOLS);
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);

    /* Initialize Argobots. */
    ATS_init(argc, argv, g_num_xstreams);

    ret = ABT_self_get_xstream(&xstreams[0]);
    ATS_ERROR(ret, "ABT_self_get_xstream");

    /* Set up pools. */
    /* pools[0]: the original main pool. */
    ret = ABT_xstream_get_main_pools(xstreams[0], 1, &g_pools[0]);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");
    /* pools[1:NUM_POOLS]: the built-in FIFO pools. */
    for (i = 1; i < NUM_POOLS; i++) {
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_TRUE, &g_pools[i]);
        ATS_ERROR(ret, "ABT_pool_create_basic");
    }

    /* Create Execution Streams */
    for (i = 1; i < g_num_xstreams; i++) {
        ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, NUM_POOLS, g_pools,
                                       ABT_SCHED_CONFIG_NULL, &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create_basic");
    }
    /* Change the scheduler of the primary execution stream. */
    change_main_sched(NUM_POOLS, g_pools, NUM_POOLS);

    /* Create ULTs that randomly change the main scheduler. */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_create(g_pools[i % NUM_POOLS], thread_func, NULL,
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }

    thread_func(NULL);

    /* Join and free ULTs */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }

    /* Yield myself until this thread is running on the primary execution
     * stream. */
    i = 0;
    while (1) {
        ABT_bool on_primary_xstream = ABT_FALSE;
        ret = ABT_self_on_primary_xstream(&on_primary_xstream);
        ATS_ERROR(ret, "ABT_self_on_primary_xstream");
        if (on_primary_xstream)
            break;
        ret = ABT_self_set_associated_pool(g_pools[i]);
        ATS_ERROR(ret, "ABT_self_set_associated_pool");
        ret = ABT_self_yield();
        ATS_ERROR(ret, "ABT_self_yield");
        i = (i + 1) % NUM_POOLS;
    }

    /* Before freeing the other execution stream, we should guarantee that all
     * pools are associated with the primary execution stream's scheduler. */
    change_main_sched(NUM_POOLS, g_pools, 1);

    /* Join and free execution streams */
    for (i = 1; i < g_num_xstreams; i++) {
        while (1) {
            ABT_bool on_primary_xstream = ABT_FALSE;
            ret = ABT_self_on_primary_xstream(&on_primary_xstream);
            ATS_ERROR(ret, "ABT_self_on_primary_xstream");
            if (on_primary_xstream)
                break;
            ret = ABT_self_yield();
            ATS_ERROR(ret, "ABT_self_yield");
        }
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);

    free(xstreams);
    free(g_pools);
    free(threads);

    return ret;
}
