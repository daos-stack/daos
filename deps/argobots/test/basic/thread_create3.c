/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "abt.h"
#include "abttest.h"

/* This test checks if named/unnamed create/create_to can be mixed. */

#define DEFAULT_NUM_XSTREAMS 4
#define BINARY_DEPTH 8 /* 2 ** BINARY_DEPTH ULTs will be created */

static inline uint32_t get_rand(uint32_t seed)
{
    /* thread-safe pseudo random generator.  Randomness is not important. */
    return ((seed * 1103515245) + 12345) & 0x7fffffff;
}

void binary(void *arg)
{
    int i, ret;
    const intptr_t depth = (intptr_t)arg;
    if (depth <= 0) {
        /* Leaf node. */
        return;
    }
    const intptr_t child_depth = depth - 1;
    ABT_thread threads[2] = { ABT_THREAD_NULL, ABT_THREAD_NULL };
    for (i = 0; i < 2; i++) {
        ABT_pool pool;
        ret = ABT_self_get_last_pool(&pool);
        ATS_ERROR(ret, "ABT_self_get_last_pool");

        uint32_t rand_val = get_rand(((intptr_t)&i) + i) >> 3;
        ABT_thread *p_thread = (rand_val / 4 <= 1) ? &threads[i] : NULL;
        if (rand_val % 2 == 0) {
            ret = ABT_thread_create(pool, binary, (void *)child_depth,
                                    ABT_THREAD_ATTR_NULL, p_thread);
            ATS_ERROR(ret, "ABT_thread_create");
        } else {
            ret = ABT_thread_create_to(pool, binary, (void *)child_depth,
                                       ABT_THREAD_ATTR_NULL, p_thread);
            ATS_ERROR(ret, "ABT_thread_create_to");
        }
    }
    for (i = 0; i < 2; i++) {
        if (threads[i] != ABT_THREAD_NULL) {
            ret = ABT_thread_free(&threads[i]);
            ATS_ERROR(ret, "ABT_thread_free");
        }
    }
}

int main(int argc, char *argv[])
{
    int i, ret, num_xstreams = DEFAULT_NUM_XSTREAMS;
    if (argc > 1)
        num_xstreams = atoi(argv[1]);
    assert(num_xstreams >= 0);

    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_pool *pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
    ABT_sched *scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * num_xstreams);

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, num_xstreams + 1);

    /* Create pools. */
    for (i = 0; i < num_xstreams; i++) {
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_TRUE, &pools[i]);
        ATS_ERROR(ret, "ABT_pool_create_basic");
    }

    /* Create schedulers. */
    for (i = 0; i < num_xstreams; i++) {
        int j;
        ABT_pool *tmp = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
        for (j = 0; j < num_xstreams; j++) {
            tmp[j] = pools[(i + j) % num_xstreams];
        }
        ret = ABT_sched_create_basic(ABT_SCHED_DEFAULT, num_xstreams, tmp,
                                     ABT_SCHED_CONFIG_NULL, &scheds[i]);
        ATS_ERROR(ret, "ABT_sched_create_basic");
        free(tmp);
    }

    /* Set up the primary execution stream. */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    ret = ABT_xstream_set_main_sched(xstreams[0], scheds[0]);
    ATS_ERROR(ret, "ABT_xstream_set_main_sched");

    /* Create secondary execution streams. */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(scheds[i], &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }

    binary((void *)((intptr_t)BINARY_DEPTH));

    /* Join secondary execution streams. */
    for (i = 1; i < num_xstreams; i++) {
        while (1) {
            ABT_bool on_primary;
            ret = ABT_self_on_primary_xstream(&on_primary);
            ATS_ERROR(ret, "ABT_self_on_primary_xstream");
            if (on_primary) {
                break;
            }
            ret = ABT_self_yield();
            ATS_ERROR(ret, "ABT_self_yield");
        }
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);

    /* Free allocated memory. */
    free(xstreams);
    free(pools);
    free(scheds);

    return ret;
}
