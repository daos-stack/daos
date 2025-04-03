/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

ABT_bool g_is_check_error, g_support_external_thread;

void task_hello(void *arg)
{
    int ret;
    int rank1, rank2;
    ABT_unit_id id1, id2;
    ABT_thread thread;
    ABT_xstream xstream;

    ret = ABT_self_get_xstream(&xstream);
    ATS_ERROR(ret, "ABT_self_get_xstream");

    ret = ABT_xstream_get_rank(xstream, &rank1);
    ATS_ERROR(ret, "ABT_xstream_get_rank");

    ret = ABT_self_get_thread(&thread);
    ATS_ERROR(ret, "ABT_self_get_thread");

    ret = ABT_thread_get_id(thread, &id1);
    ATS_ERROR(ret, "ABT_thread_get_id");

    /* Rank check */
    ret = ABT_xstream_self_rank(&rank2);
    ATS_ERROR(ret, "ABT_xstream_self_rank");
    assert(rank1 == rank2);

    ret = ABT_self_get_xstream_rank(&rank2);
    ATS_ERROR(ret, "ABT_self_get_xstream_rank");
    assert(rank1 == rank2);

    /* ID check */
    ret = ABT_self_get_task_id(&id2);
    ATS_ERROR(ret, "ABT_self_get_task_id");
    assert(id1 == id2);

    ret = ABT_self_get_thread_id(&id2);
    ATS_ERROR(ret, "ABT_self_get_thread_id");
    assert(id1 == id2);

    ret = ABT_task_self_id(&id2);
    ATS_ERROR(ret, "ABT_task_self_id");
    assert(id1 == id2);

#ifdef ABT_ENABLE_VER_20_API
    ret = ABT_thread_self_id(&id2);
    ATS_ERROR(ret, "ABT_thread_self_id");
    assert(id1 == id2);
#else
    if (g_is_check_error) {
        ret = ABT_thread_self_id(&id2);
        assert(ret != ABT_SUCCESS);
    }
#endif
}

void thread_hello(void *arg)
{
    int ret;
    int rank1, rank2;
    ABT_unit_id id1, id2;
    ABT_thread thread;
    ABT_xstream xstream;

    ret = ABT_self_get_xstream(&xstream);
    ATS_ERROR(ret, "ABT_self_get_xstream");

    ret = ABT_xstream_get_rank(xstream, &rank1);
    ATS_ERROR(ret, "ABT_xstream_get_rank");

    ret = ABT_self_get_thread(&thread);
    ATS_ERROR(ret, "ABT_self_get_thread");

    ret = ABT_thread_get_id(thread, &id1);
    ATS_ERROR(ret, "ABT_thread_get_id");

    /* Rank check */
    ret = ABT_xstream_self_rank(&rank2);
    ATS_ERROR(ret, "ABT_xstream_self_rank");
    assert(rank1 == rank2);

    ret = ABT_self_get_xstream_rank(&rank2);
    ATS_ERROR(ret, "ABT_self_get_xstream_rank");
    assert(rank1 == rank2);

    /* ID check */
    ret = ABT_self_get_task_id(&id2);
    ATS_ERROR(ret, "ABT_self_get_task_id");
    assert(id1 == id2);

    ret = ABT_self_get_thread_id(&id2);
    ATS_ERROR(ret, "ABT_self_get_thread_id");
    assert(id1 == id2);

    ret = ABT_thread_self_id(&id2);
    ATS_ERROR(ret, "ABT_thread_self_id");
    assert(id1 == id2);

#ifdef ABT_ENABLE_VER_20_API
    ret = ABT_task_self_id(&id2);
    ATS_ERROR(ret, "ABT_task_self_id");
    assert(id1 == id2);
#else
    if (g_is_check_error) {
        ret = ABT_task_self_id(&id2);
        assert(ret != ABT_SUCCESS);
    }
#endif

    /* Even after yield, the ID should be the same. */
    ret = ABT_thread_yield();
    ATS_ERROR(ret, "ABT_thread_yield");

    ret = ABT_self_get_task_id(&id2);
    ATS_ERROR(ret, "ABT_self_get_task_id");
    assert(id1 == id2);

    ret = ABT_self_get_thread_id(&id2);
    ATS_ERROR(ret, "ABT_self_get_thread_id");
    assert(id1 == id2);

    ret = ABT_thread_self_id(&id2);
    ATS_ERROR(ret, "ABT_thread_self_id");
    assert(id1 == id2);

#ifdef ABT_ENABLE_VER_20_API
    ret = ABT_task_self_id(&id2);
    ATS_ERROR(ret, "ABT_task_self_id");
    assert(id1 == id2);
#else
    if (g_is_check_error) {
        ret = ABT_task_self_id(&id2);
        assert(ret != ABT_SUCCESS);
    }
#endif
}

void *pthread_hello(void *arg)
{
    if (g_support_external_thread && g_is_check_error) {
        int ret;
        int rank;
        ABT_thread_id id;
        /* Rank check */
        ret = ABT_xstream_self_rank(&rank);
        assert(ret != ABT_SUCCESS);

        ret = ABT_self_get_xstream_rank(&rank);
        assert(ret != ABT_SUCCESS);

        /* ID check */
        ret = ABT_self_get_thread_id(&id);
        assert(ret != ABT_SUCCESS);

        ret = ABT_self_get_task_id(&id);
        assert(ret != ABT_SUCCESS);

        ret = ABT_thread_self_id(&id);
        assert(ret != ABT_SUCCESS);

        ret = ABT_task_self_id(&id);
        assert(ret != ABT_SUCCESS);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    ABT_xstream xstreams[2];
    ABT_pool pools[2];
    ABT_thread threads[2];
    pthread_t pthread;
    int i, ret;

#ifndef ABT_ENABLE_VER_20_API
    ret = ABT_init(0, NULL);
    ATS_ERROR(ret, "ABT_init");
#endif
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR,
                                (void *)&g_is_check_error);
    ATS_ERROR(ret, "ABT_info_query_config");
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_EXTERNAL_THREAD,
                                (void *)&g_support_external_thread);
    ATS_ERROR(ret, "ABT_info_query_config");
#ifndef ABT_ENABLE_VER_20_API
    ret = ABT_finalize();
    ATS_ERROR(ret, "ABT_finalize");
#endif

#ifndef ABT_ENABLE_VER_20_API
    /* Check error handling. */
    if (g_is_check_error && g_support_external_thread) {
        int rank;
        ABT_thread_id id;
        ret = ABT_xstream_self_rank(&rank);
        assert(ret != ABT_SUCCESS);
        ret = ABT_thread_self_id(&id);
        assert(ret != ABT_SUCCESS);
        ret = ABT_task_self_id(&id);
        assert(ret != ABT_SUCCESS);
    }
#endif

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 2);

    /* Execution Streams */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");

    ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[1]);
    ATS_ERROR(ret, "ABT_xstream_create");

    /* Create ULTs */
    for (i = 0; i < 2; i++) {
        ret = ABT_xstream_get_main_pools(xstreams[i], 1, &pools[i]);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");

        ret = ABT_thread_create(pools[i], thread_hello, NULL,
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }
    /* Call it here. */
    thread_hello(NULL);

    /* Create a pthread */
    ret = pthread_create(&pthread, NULL, pthread_hello, NULL);
    assert(ret == 0);

    /* Join and free ULTs */
    for (i = 0; i < 2; i++) {
        ret = ABT_thread_join(threads[i]);
        ATS_ERROR(ret, "ABT_thread_join");
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }
    ret = ABT_xstream_join(xstreams[1]);
    ATS_ERROR(ret, "ABT_xstream_join");
    ret = ABT_xstream_free(&xstreams[1]);
    ATS_ERROR(ret, "ABT_xstream_free");
    ret = pthread_join(pthread, NULL);
    assert(ret == 0);

    /* Finalize */
    ret = ATS_finalize(0);
    ATS_ERROR(ret, "ATS_finalize");

#ifndef ABT_ENABLE_VER_20_API
    /* Check error handling. */
    if (g_is_check_error && g_support_external_thread) {
        int rank;
        ABT_thread_id id;
        ret = ABT_xstream_self_rank(&rank);
        assert(ret != ABT_SUCCESS);
        ret = ABT_thread_self_id(&id);
        assert(ret != ABT_SUCCESS);
        ret = ABT_task_self_id(&id);
        assert(ret != ABT_SUCCESS);
    }
#endif
    return 0;
}
