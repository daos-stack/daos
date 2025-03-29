/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

void task_hello(void *arg)
{
    ABT_xstream xstream, xstream2;
    ABT_thread thread, thread2;
    ABT_task task, task2;
    ABT_unit_type type;
    ABT_bool flag;
    int ret;

    ret = ABT_xstream_self(&xstream);
    ATS_ERROR(ret, "ABT_stream_self");

    ret = ABT_self_get_xstream(&xstream2);
    ATS_ERROR(ret, "ABT_self_get_xstream");
    assert(xstream == xstream2);

    ret = ABT_thread_self(&thread);
#ifdef ABT_ENABLE_VER_20_API
    ATS_ERROR(ret, "ABT_thread_self");
#else
    assert(ret == ABT_ERR_INV_THREAD && thread == ABT_THREAD_NULL);
#endif

    ret = ABT_self_get_thread(&thread2);
    ATS_ERROR(ret, "ABT_self_get_thread");

    ret = ABT_task_self(&task);
    ATS_ERROR(ret, "ABT_task_self");

    ret = ABT_self_get_task(&task2);
    ATS_ERROR(ret, "ABT_self_get_task");

    assert(task == task2 && task == thread2);

    ret = ABT_self_get_type(&type);
    assert(ret == ABT_SUCCESS && type == ABT_UNIT_TYPE_TASK);

    ret = ABT_self_is_primary(&flag);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_SUCCESS && flag == ABT_FALSE);
#else
    assert(ret == ABT_ERR_INV_THREAD && flag == ABT_FALSE);
#endif

    ret = ABT_self_on_primary_xstream(&flag);
    assert(ret == ABT_SUCCESS);

    ATS_printf(1, "TASK %d: running on the %s\n", (int)(size_t)arg,
               (flag == ABT_TRUE ? "primary ES" : "secondary ES"));
}

void thread_hello(void *arg)
{
    ABT_xstream xstream, xstream2;
    ABT_pool pool;
    ABT_thread thread, thread2;
    ABT_unit_id my_id;
    ABT_task task, task2;
    ABT_unit_type type;
    ABT_bool flag;
    int ret;

    ret = ABT_xstream_self(&xstream);
    ATS_ERROR(ret, "ABT_stream_self");

    ret = ABT_self_get_xstream(&xstream2);
    ATS_ERROR(ret, "ABT_self_get_xstream");
    assert(xstream == xstream2);

    ret = ABT_thread_self(&thread);
    ATS_ERROR(ret, "ABT_thread_self");

    ret = ABT_self_get_thread(&thread2);
    ATS_ERROR(ret, "ABT_self_get_thread");
    assert(thread == thread2);

    ret = ABT_thread_get_id(thread, &my_id);
    ATS_ERROR(ret, "ABT_thread_get_id");

    ret = ABT_task_self(&task);
#ifdef ABT_ENABLE_VER_20_API
    ATS_ERROR(ret, "ABT_task_self");
#else
    assert(ret == ABT_ERR_INV_TASK && task == ABT_TASK_NULL);
#endif

    ret = ABT_self_get_task(&task2);
    ATS_ERROR(ret, "ABT_self_get_task");
    assert(thread == task2);

    ret = ABT_self_get_type(&type);
    assert(ret == ABT_SUCCESS && type == ABT_UNIT_TYPE_THREAD);

    ret = ABT_self_is_primary(&flag);
    assert(ret == ABT_SUCCESS && flag == ABT_FALSE);

    ret = ABT_thread_is_primary(thread, &flag);
    assert(ret == ABT_SUCCESS && flag == ABT_FALSE);

    /* Get the first pool */
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");

    /* Create a task */
    ret = ABT_task_create(pool, task_hello, (void *)((intptr_t)my_id), NULL);
    ATS_ERROR(ret, "ABT_task_create");

    ret = ABT_self_on_primary_xstream(&flag);
    assert(ret == ABT_SUCCESS);

    ATS_printf(1, "ULT %lu running on the %s\n", my_id,
               (flag == ABT_TRUE ? "primary ES" : "secondary ES"));
}

void *pthread_hello(void *arg)
{
    ABT_xstream xstream;
    ABT_thread thread;
    ABT_task task;
    ABT_unit_type type;
    ABT_bool flag;
    int ret;

    /* Since Argobots has been initialized, we should get ABT_ERR_INV_XXX. */
    ret = ABT_xstream_self(&xstream);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_ERR_INV_XSTREAM);
#else
    assert(ret == ABT_ERR_INV_XSTREAM && xstream == ABT_XSTREAM_NULL);
#endif

    ret = ABT_self_get_xstream(&xstream);
    assert(ret == ABT_ERR_INV_XSTREAM);

    ret = ABT_thread_self(&thread);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_ERR_INV_XSTREAM);
#else
    assert(ret == ABT_ERR_INV_XSTREAM && thread == ABT_THREAD_NULL);
#endif

    ret = ABT_self_get_thread(&thread);
    assert(ret == ABT_ERR_INV_XSTREAM);

    ret = ABT_task_self(&task);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_ERR_INV_XSTREAM);
#else
    assert(ret == ABT_ERR_INV_XSTREAM && task == ABT_TASK_NULL);
#endif

    ret = ABT_self_get_task(&task);
    assert(ret == ABT_ERR_INV_XSTREAM);

    ret = ABT_self_get_type(&type);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_SUCCESS && type == ABT_UNIT_TYPE_EXT);
#else
    assert(ret == ABT_ERR_INV_XSTREAM && type == ABT_UNIT_TYPE_EXT);
#endif

    ret = ABT_self_is_primary(&flag);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_SUCCESS && flag == ABT_FALSE);
#else
    assert(ret == ABT_ERR_INV_XSTREAM && flag == ABT_FALSE);
#endif

    ret = ABT_self_on_primary_xstream(&flag);
#ifdef ABT_ENABLE_VER_20_API
    assert(ret == ABT_SUCCESS && flag == ABT_FALSE);
#else
    assert(ret == ABT_ERR_INV_XSTREAM && flag == ABT_FALSE);
#endif

    ATS_printf(1, "pthread: external thread\n");

    return NULL;
}

int main(int argc, char *argv[])
{
    ABT_xstream xstreams[2];
    ABT_pool pools[2];
    ABT_thread threads[2];
    ABT_thread my_thread;
    ABT_unit_id my_thread_id;
    ABT_task my_task;
    ABT_unit_type type;
    ABT_bool flag;
    pthread_t pthread;
    int i, ret;

#ifndef ABT_ENABLE_VER_20_API
    /* Self test: we should get ABT_ERR_UNITIALIZED */
    ret = ABT_xstream_self(&xstreams[0]);
    assert(ret == ABT_ERR_UNINITIALIZED && xstreams[0] == ABT_XSTREAM_NULL);

    ret = ABT_thread_self(&my_thread);
    assert(ret == ABT_ERR_UNINITIALIZED && my_thread == ABT_THREAD_NULL);

    ret = ABT_task_self(&my_task);
    assert(ret == ABT_ERR_UNINITIALIZED && my_task == ABT_TASK_NULL);

    ret = ABT_self_get_type(&type);
    assert(ret == ABT_ERR_UNINITIALIZED && type == ABT_UNIT_TYPE_EXT);

    ret = ABT_self_is_primary(&flag);
    assert(ret == ABT_ERR_UNINITIALIZED);

    ret = ABT_self_on_primary_xstream(&flag);
    assert(ret == ABT_ERR_UNINITIALIZED);
#endif

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 2);

    /* Execution Streams */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");

    ret = ABT_xstream_create(ABT_SCHED_NULL, &xstreams[1]);
    ATS_ERROR(ret, "ABT_xstream_create");

    /* Test self routines */
    ret = ABT_thread_self(&my_thread);
    ATS_ERROR(ret, "ABT_thread_self");
    ABT_thread_get_id(my_thread, &my_thread_id);
    ATS_printf(1, "ID: %lu\n", my_thread_id);

    ret = ABT_task_self(&my_task);
#ifdef ABT_ENABLE_VER_20_API
    ATS_ERROR(ret, "ABT_task_self");
#else
    assert(ret == ABT_ERR_INV_TASK && my_task == ABT_TASK_NULL);
#endif

    ABT_xstream xstream_tmp;
    ret = ABT_self_get_xstream(&xstream_tmp);
    ATS_ERROR(ret, "ABT_self_get_xstream");
    assert(xstream_tmp == xstreams[0]);

    ABT_thread thread_tmp;
    ret = ABT_self_get_thread(&thread_tmp);
    ATS_ERROR(ret, "ABT_self_get_thread");
    assert(thread_tmp == my_thread);

    ABT_task task_tmp;
    ret = ABT_self_get_task(&task_tmp);
    ATS_ERROR(ret, "ABT_self_get_task");
    assert(task_tmp == my_thread);

    ret = ABT_self_get_type(&type);
    assert(ret == ABT_SUCCESS && type == ABT_UNIT_TYPE_THREAD);

    ret = ABT_self_is_primary(&flag);
    assert(ret == ABT_SUCCESS && flag == ABT_TRUE);

    ret = ABT_thread_is_primary(my_thread, &flag);
    assert(ret == ABT_SUCCESS && flag == ABT_TRUE);

    ret = ABT_self_on_primary_xstream(&flag);
    assert(ret == ABT_SUCCESS && flag == ABT_TRUE);

    /* Create ULTs */
    for (i = 0; i < 2; i++) {
        ret = ABT_xstream_get_main_pools(xstreams[i], 1, &pools[i]);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");

        ret = ABT_thread_create(pools[i], thread_hello, NULL,
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }

    /* Create a pthread */
    ret = pthread_create(&pthread, NULL, pthread_hello, NULL);
    assert(ret == 0);

    /* Join & Free */
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
    return ATS_finalize(0);
}
