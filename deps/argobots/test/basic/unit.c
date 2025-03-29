/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

/* Several types of pools are used.  This test checks if corresponding pool
 * operations are called properly. */

#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

void create_sched_def(ABT_sched_def *p_def);
void create_pool_def(ABT_pool_def *p_def);

#define DEFAULT_NUM_XSTREAMS 2
#define DEFAULT_NUM_THREADS 100
#define NUM_POOLS 2

void check_self_unit_mapping(void)
{
    int ret;
    ABT_thread self_thread1, self_thread2;
    ABT_unit self_unit1, self_unit2;
    ret = ABT_self_get_thread(&self_thread1);
    ATS_ERROR(ret, "ABT_self_get_thread");
    ret = ABT_self_get_unit(&self_unit1);
    ATS_ERROR(ret, "ABT_self_get_unit");
    ret = ABT_thread_get_unit(self_thread1, &self_unit2);
    ATS_ERROR(ret, "ABT_thread_get_unit");
    ret = ABT_unit_get_thread(self_unit1, &self_thread2);
    ATS_ERROR(ret, "ABT_unit_get_thread");
    assert(self_unit1 == self_unit2);
    assert(self_thread1 == self_thread2);
}

void thread_func(void *arg)
{
    int ret, i;
    for (i = 0; i < 10; i++) {
        if (i % 3 == 0) {
            check_self_unit_mapping();
            ABT_pool target_pool = (ABT_pool)arg;
            /* Let's change the associated pool sometimes. */
            ret = ABT_self_set_associated_pool(target_pool);
            ATS_ERROR(ret, "ABT_self_set_associated_pool");
        }
        check_self_unit_mapping();
        ret = ABT_thread_yield();
        ATS_ERROR(ret, "ABT_thread_yield");
    }
}

int sched_init(ABT_sched sched, ABT_sched_config config)
{
    return ABT_SUCCESS;
}

void sched_run(ABT_sched sched)
{
    check_self_unit_mapping();
    int ret;
    ABT_pool pools[NUM_POOLS];
    ret = ABT_sched_get_pools(sched, NUM_POOLS, 0, pools);
    ATS_ERROR(ret, "ABT_sched_get_pools");
    int work_count = 0;
    while (1) {
        ABT_unit unit;
        ABT_pool victim_pool = pools[work_count % NUM_POOLS];
        int no_run = (work_count % 3) == 0;

        ret = ABT_pool_pop(victim_pool, &unit);
        ATS_ERROR(ret, "ABT_pool_pop");
        if (unit != ABT_UNIT_NULL) {
            ABT_pool target_pool = pools[(work_count / 2) % NUM_POOLS];
            if (no_run) {
                /* Push back to the pool. */
                ret = ABT_pool_push(target_pool, unit);
                ATS_ERROR(ret, "ABT_pool_push");
            } else {
                ret = ABT_xstream_run_unit(unit, target_pool);
                ATS_ERROR(ret, "ABT_xstream_run_unit");
            }
        }
        if (work_count++ % 100 == 0) {
            ABT_bool stop;
            ret = ABT_sched_has_to_stop(sched, &stop);
            ATS_ERROR(ret, "ABT_sched_has_to_stop");
            if (stop == ABT_TRUE)
                break;
            ret = ABT_xstream_check_events(sched);
            ATS_ERROR(ret, "ABT_xstream_check_events");
        }
    }
}

int sched_free(ABT_sched sched)
{
    return ABT_SUCCESS;
}

void create_sched_def(ABT_sched_def *p_def)
{
    p_def->type = ABT_SCHED_TYPE_ULT;
    p_def->init = sched_init;
    p_def->run = sched_run;
    p_def->free = sched_free;
    p_def->get_migr_pool = NULL;
}

ABT_sched create_sched(int num_pools, ABT_pool *pools)
{
    int ret;
    ABT_sched sched;
    ABT_sched_def sched_def;
    create_sched_def(&sched_def);
    ret = ABT_sched_create(&sched_def, num_pools, pools, ABT_SCHED_CONFIG_NULL,
                           &sched);
    ATS_ERROR(ret, "ABT_sched_create");
    return sched;
}

int main(int argc, char *argv[])
{
    int i, ret;
    int num_xstreams = DEFAULT_NUM_XSTREAMS;
    int num_threads = DEFAULT_NUM_THREADS;

    /* Initialize */
    ATS_read_args(argc, argv);
    if (argc > 1) {
        num_xstreams = ATS_get_arg_val(ATS_ARG_N_ES);
        num_threads = ATS_get_arg_val(ATS_ARG_N_ULT);
    }

    /* Allocate memory. */
    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_pool *pools = (ABT_pool *)malloc(sizeof(ABT_pool) * NUM_POOLS);
    ABT_sched *scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * num_xstreams);

    /* Initialize Argobots. */
    ATS_init(argc, argv, num_xstreams);

    /* Create pools. */
    /* pools[0]: the built-in FIFO pool. */
    ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_FALSE,
                                &pools[0]);
    ATS_ERROR(ret, "ABT_pool_create_basic");
    /* pools[1]: user-defined basic pool. */
    ABT_pool_def pool_def;
    create_pool_def(&pool_def);
    ret = ABT_pool_create(&pool_def, ABT_POOL_CONFIG_NULL, &pools[1]);
    ATS_ERROR(ret, "ABT_pool_create");

    /* Create schedulers. */
    for (i = 0; i < num_xstreams; i++) {
        scheds[i] = create_sched(NUM_POOLS, pools);
    }

    /* Create secondary execution streams. */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(scheds[i], &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }

    check_self_unit_mapping();

    /* Update the main scheduler of the primary execution stream. */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    ret = ABT_xstream_set_main_sched(xstreams[0], scheds[0]);
    ATS_ERROR(ret, "ABT_xstream_set_main_sched");

    check_self_unit_mapping();

    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_threads);
    /* Create threads. */
    for (i = 0; i < num_threads; i++) {
        ABT_pool target_pool = pools[i % NUM_POOLS];
        ABT_pool arg_pool = pools[(i / 2) % NUM_POOLS];
        ret = ABT_thread_create(target_pool, thread_func, (void *)arg_pool,
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        ATS_ERROR(ret, "ABT_thread_create");
    }

    /* Join and revive threads. */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_join(threads[i]);
        ATS_ERROR(ret, "ABT_thread_join");
        ABT_pool target_pool = pools[(i / 3) % NUM_POOLS];
        ABT_pool arg_pool = pools[(i / 4) % NUM_POOLS];
        ret = ABT_thread_revive(target_pool, thread_func, (void *)arg_pool,
                                &threads[i]);
        ATS_ERROR(ret, "ABT_thread_revive");
    }

    /* Free threads. */
    for (i = 0; i < num_threads; i++) {
        ret = ABT_thread_free(&threads[i]);
        ATS_ERROR(ret, "ABT_thread_free");
    }

    free(threads);

    check_self_unit_mapping();

    /* Join and free secondary execution streams. */
    for (i = 1; i < num_xstreams; i++) {
        while (1) {
            ABT_bool on_primary_xstream = ABT_FALSE;
            ret = ABT_self_on_primary_xstream(&on_primary_xstream);
            ATS_ERROR(ret, "ABT_self_on_primary_xstream");
            if (on_primary_xstream)
                break;
            ret = ABT_thread_yield();
            ATS_ERROR(ret, "ABT_thread_yield");
        }
        /* Yield myself until this thread is running on the primary execution
         * stream. */
        ret = ABT_xstream_free(&xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_free");
    }

    check_self_unit_mapping();

    /* Free schedulers of the secondary execution streams (since the scheduler
     * created by ABT_sched_create() are not automatically freed). */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_sched_free(&scheds[i]);
        ATS_ERROR(ret, "ABT_sched_free");
    }

    check_self_unit_mapping();

    /* The scheduler of the primary execution stream will be freed by
     * ABT_finalize().  Pools are associated with the scheduler of the primary
     * execution stream, so they will be freed by ABT_finallize(), too. */

    /* Finalize Argobots. */
    ret = ATS_finalize(0);

    /* Free allocated memory. */
    free(xstreams);
    free(pools);
    free(scheds);

    return ret;
}

/******************************************************************************/

typedef struct unit_t {
    ABT_thread thread;
    struct unit_t *p_prev, *p_next;
} unit_t;

typedef struct pool_t {
    unit_t list;
    int size;
    pthread_mutex_t lock;
} pool_t;

ABT_unit pool_unit_create_from_thread(ABT_thread thread)
{
    unit_t *p_unit = (unit_t *)malloc(sizeof(unit_t));
    p_unit->thread = thread;
    return (ABT_unit)p_unit;
}

void pool_unit_free(ABT_unit *p_unit)
{
    free(*p_unit);
}

int pool_init(ABT_pool pool, ABT_pool_config config)
{
    pool_t *p_pool = (pool_t *)malloc(sizeof(pool_t));
    p_pool->list.p_prev = &p_pool->list;
    p_pool->list.p_next = &p_pool->list;
    p_pool->size = 0;
    pthread_mutex_init(&p_pool->lock, NULL);
    int ret = ABT_pool_set_data(pool, (void *)p_pool);
    ATS_ERROR(ret, "ABT_pool_set_data");
    return ABT_SUCCESS;
}

size_t pool_get_size(ABT_pool pool)
{
    pool_t *p_pool;
    int ret = ABT_pool_get_data(pool, (void **)&p_pool);
    ATS_ERROR(ret, "ABT_pool_get_data");
    return p_pool->size;
}

void pool_push(ABT_pool pool, ABT_unit unit)
{
    pool_t *p_pool;
    int ret = ABT_pool_get_data(pool, (void **)&p_pool);
    ATS_ERROR(ret, "ABT_pool_get_data");

    unit_t *p_unit = (unit_t *)unit;
    pthread_mutex_lock(&p_pool->lock);
    p_unit->p_next = &p_pool->list;
    p_unit->p_prev = p_pool->list.p_prev;
    p_pool->list.p_prev->p_next = p_unit;
    p_pool->list.p_prev = p_unit;
    p_pool->size++;
    pthread_mutex_unlock(&p_pool->lock);
}

ABT_unit pool_pop(ABT_pool pool)
{
    pool_t *p_pool;
    int ret = ABT_pool_get_data(pool, (void **)&p_pool);
    ATS_ERROR(ret, "ABT_pool_get_data");

    pthread_mutex_lock(&p_pool->lock);
    if (p_pool->size == 0) {
        pthread_mutex_unlock(&p_pool->lock);
        /* Empty. */
        return ABT_UNIT_NULL;
    } else {
        p_pool->size--;
        unit_t *p_ret = p_pool->list.p_next;
        p_pool->list.p_next = p_ret->p_next;
        p_pool->list.p_next->p_prev = &p_pool->list;
        pthread_mutex_unlock(&p_pool->lock);
        return (ABT_unit)p_ret;
    }
}

int pool_free(ABT_pool pool)
{
    pool_t *p_pool;
    int ret = ABT_pool_get_data(pool, (void **)&p_pool);
    ATS_ERROR(ret, "ABT_pool_get_data");

    pthread_mutex_destroy(&p_pool->lock);
    free(p_pool);
    return ABT_SUCCESS;
}

void create_pool_def(ABT_pool_def *p_def)
{
    p_def->access = ABT_POOL_ACCESS_MPMC;
    p_def->u_create_from_thread = pool_unit_create_from_thread;
    p_def->u_free = pool_unit_free;
    p_def->p_init = pool_init;
    p_def->p_get_size = pool_get_size;
    p_def->p_push = pool_push;
    p_def->p_pop = pool_pop;
    p_def->p_free = pool_free;

    /* Optional. */
    p_def->u_is_in_pool = NULL;
#ifdef ABT_ENABLE_VER_20_API
    p_def->p_pop_wait = NULL;
#endif
    p_def->p_pop_timedwait = NULL;
    p_def->p_remove = NULL;
    p_def->p_print_all = NULL;
}
