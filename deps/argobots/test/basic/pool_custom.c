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
ABT_pool create_pool1(void);
ABT_pool create_pool2(void);
ABT_pool create_pool3(void);
ABT_pool create_pool4(void);

#define DEFAULT_NUM_XSTREAMS 3
#define DEFAULT_NUM_THREADS 200
#define NUM_POOLS 7

void thread_func(void *arg)
{
    int ret, i;
    for (i = 0; i < 10; i++) {
        if (i % 3 == 0) {
            ABT_pool target_pool = (ABT_pool)arg;
            /* Let's change the associated pool sometimes. */
            ret = ABT_self_set_associated_pool(target_pool);
            ATS_ERROR(ret, "ABT_self_set_associated_pool");
        }
        ret = ABT_thread_yield();
        ATS_ERROR(ret, "ABT_thread_yield");
    }
}

ABT_pool create_pool(int pool_type)
{
    ABT_pool newpool = ABT_POOL_NULL;
    if (pool_type == 0) {
        /* Built-in FIFO pool. */
        int ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                        ABT_FALSE, &newpool);
        ATS_ERROR(ret, "ABT_pool_create_basic");
    } else if (pool_type == 1) {
        /* Built-in FIFOWAIT pool. */
        int ret =
            ABT_pool_create_basic(ABT_POOL_FIFO_WAIT, ABT_POOL_ACCESS_MPMC,
                                  ABT_FALSE, &newpool);
        ATS_ERROR(ret, "ABT_pool_create_basic");
    } else if (pool_type == 2) {
        /* Built-in RANDWS pool. */
        int ret = ABT_pool_create_basic(ABT_POOL_RANDWS, ABT_POOL_ACCESS_MPMC,
                                        ABT_FALSE, &newpool);
        ATS_ERROR(ret, "ABT_pool_create_basic");
    } else if (pool_type == 3) {
        /* ABT_pool_def-based pool (pool 1). */
        newpool = create_pool1();
    } else if (pool_type == 4) {
        /* ABT_pool_def-based pool (pool 2). */
        newpool = create_pool2();
    } else if (pool_type == 5) {
        /* ABTI_pool_user_def-based pool (pool 3). */
        newpool = create_pool3();
    } else if (pool_type == 6) {
        /* ABTI_pool_user_def-based pool (poo; 4). */
        newpool = create_pool4();
    }
    return newpool;
}

int sched_init(ABT_sched sched, ABT_sched_config config)
{
    return ABT_SUCCESS;
}

void sched_run(ABT_sched sched)
{
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
    for (i = 0; i < NUM_POOLS; i++) {
        pools[i] = create_pool(i);
    }

    /* Create schedulers. */
    for (i = 0; i < num_xstreams; i++) {
        scheds[i] = create_sched(NUM_POOLS, pools);
    }

    /* Create secondary execution streams. */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_xstream_create(scheds[i], &xstreams[i]);
        ATS_ERROR(ret, "ABT_xstream_create");
    }
    /* Update the main scheduler of the primary execution stream. */
    ret = ABT_xstream_self(&xstreams[0]);
    ATS_ERROR(ret, "ABT_xstream_self");
    ret = ABT_xstream_set_main_sched(xstreams[0], scheds[0]);
    ATS_ERROR(ret, "ABT_xstream_set_main_sched");

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

    /* Move this thread to the main pool.  This is needed since the following
     * user-defined pool_free() checks whether the pool is empty or not. */
    ret = ABT_self_set_associated_pool(pools[0]);
    ATS_ERROR(ret, "ABT_self_set_associated_pool");

    /* Free schedulers of the secondary execution streams (since the scheduler
     * created by ABT_sched_create() are not automatically freed). */
    for (i = 1; i < num_xstreams; i++) {
        ret = ABT_sched_free(&scheds[i]);
        ATS_ERROR(ret, "ABT_sched_free");
    }
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
    int dummy[64]; /* If a built-in pool accesses this, SEGV should happen. */
    int pool_type;
    ABT_thread thread;
    struct unit_t *p_prev, *p_next;
} unit_t;

typedef struct queue_t {
    unit_t list;
    int size;
    int num_units;
    pthread_mutex_t lock;
} queue_t;

static inline void queue_push(queue_t *p_queue, unit_t *p_unit)
{
    pthread_mutex_lock(&p_queue->lock);
    p_unit->p_next = &p_queue->list;
    p_unit->p_prev = p_queue->list.p_prev;
    p_queue->list.p_prev->p_next = p_unit;
    p_queue->list.p_prev = p_unit;
    p_queue->size++;
    pthread_mutex_unlock(&p_queue->lock);
}

static inline unit_t *queue_pop(queue_t *p_queue)
{
    pthread_mutex_lock(&p_queue->lock);
    if (p_queue->size == 0) {
        pthread_mutex_unlock(&p_queue->lock);
        /* Empty. */
        return NULL;
    } else {
        p_queue->size--;
        unit_t *p_ret = p_queue->list.p_next;
        p_queue->list.p_next = p_ret->p_next;
        p_queue->list.p_next->p_prev = &p_queue->list;
        pthread_mutex_unlock(&p_queue->lock);
        return p_ret;
    }
}

static inline unit_t *create_unit(queue_t *p_queue, ABT_thread thread,
                                  int pool_type)
{
    int i;
    unit_t *p_unit = (unit_t *)malloc(sizeof(unit_t));
    for (i = 0; i < 64; i++) {
        p_unit->dummy[i] = (int)0xbaadc0de; /* Canary. */
    }
    p_unit->thread = thread;
    p_unit->pool_type = pool_type;

    pthread_mutex_lock(&p_queue->lock);
    p_queue->num_units++;
    pthread_mutex_unlock(&p_queue->lock);
    return p_unit;
}

static inline void free_unit(queue_t *p_queue, unit_t *p_unit)
{
    int i;
    for (i = 0; i < 64; i++) {
        assert(p_unit->dummy[i] == (int)0xbaadc0de);
    }
    free(p_unit);
    pthread_mutex_lock(&p_queue->lock);
    p_queue->num_units--;
    assert(p_queue->num_units >= 0);
    pthread_mutex_unlock(&p_queue->lock);
}

/******************************************************************************/
/* Pool 1 */
/******************************************************************************/

queue_t pool1_queue;

ABT_unit pool1_unit_create_from_thread(ABT_thread thread)
{
    return (ABT_unit)create_unit(&pool1_queue, thread, 1);
}

void pool1_unit_free(ABT_unit *p_unit)
{
    free_unit(&pool1_queue, (unit_t *)(*p_unit));
}

int pool1_init(ABT_pool pool, ABT_pool_config config)
{
    pool1_queue.list.p_prev = &pool1_queue.list;
    pool1_queue.list.p_next = &pool1_queue.list;
    pool1_queue.size = 0;
    pool1_queue.num_units = 0;
    pthread_mutex_init(&pool1_queue.lock, NULL);
    return ABT_SUCCESS;
}

size_t pool1_get_size(ABT_pool pool)
{
    return pool1_queue.size;
}

void pool1_push(ABT_pool pool, ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    assert(p_unit->pool_type == 1);
    queue_push(&pool1_queue, p_unit);
}

ABT_unit pool1_pop(ABT_pool pool)
{
    unit_t *p_unit = queue_pop(&pool1_queue);
    return p_unit ? ((ABT_unit)p_unit) : ABT_UNIT_NULL;
}

int pool1_free(ABT_pool pool)
{
    assert(pool1_queue.size == 0);
    assert(pool1_queue.num_units == 0);
    pthread_mutex_destroy(&pool1_queue.lock);
    return ABT_SUCCESS;
}

ABT_pool create_pool1(void)
{
    ABT_pool_def def;
    def.access = ABT_POOL_ACCESS_MPMC;
    def.u_create_from_thread = pool1_unit_create_from_thread;
    def.u_free = pool1_unit_free;
    def.p_init = pool1_init;
    def.p_get_size = pool1_get_size;
    def.p_push = pool1_push;
    def.p_pop = pool1_pop;
    def.p_free = pool1_free;

    /* Optional. */
    def.u_is_in_pool = NULL;
#ifdef ABT_ENABLE_VER_20_API
    def.p_pop_wait = NULL;
#endif
    def.p_pop_timedwait = NULL;
    def.p_remove = NULL;
    def.p_print_all = NULL;

    ABT_pool newpool;
    int ret = ABT_pool_create(&def, ABT_POOL_CONFIG_NULL, &newpool);
    ATS_ERROR(ret, "ABT_pool_create");
    return newpool;
}

/******************************************************************************/
/* Pool 2 */
/******************************************************************************/

queue_t pool2_queue;

ABT_unit pool2_unit_create_from_thread(ABT_thread thread)
{
    return (ABT_unit)create_unit(&pool2_queue, thread, 2);
}

void pool2_unit_free(ABT_unit *p_unit)
{
    free_unit(&pool2_queue, (unit_t *)(*p_unit));
}

int pool2_init(ABT_pool pool, ABT_pool_config config)
{
    pool2_queue.list.p_prev = &pool2_queue.list;
    pool2_queue.list.p_next = &pool2_queue.list;
    pool2_queue.size = 0;
    pool2_queue.num_units = 0;
    pthread_mutex_init(&pool2_queue.lock, NULL);
    return ABT_SUCCESS;
}

size_t pool2_get_size(ABT_pool pool)
{
    return pool2_queue.size;
}

void pool2_push(ABT_pool pool, ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    assert(p_unit->pool_type == 2);
    queue_push(&pool2_queue, p_unit);
}

ABT_unit pool2_pop(ABT_pool pool)
{
    unit_t *p_unit = queue_pop(&pool2_queue);
    return p_unit ? ((ABT_unit)p_unit) : ABT_UNIT_NULL;
}

int pool2_free(ABT_pool pool)
{
    assert(pool2_queue.size == 0);
    assert(pool2_queue.num_units == 0);
    pthread_mutex_destroy(&pool2_queue.lock);
    return ABT_SUCCESS;
}

ABT_pool create_pool2(void)
{
    ABT_pool_def def;
    def.access = ABT_POOL_ACCESS_MPMC;
    def.u_create_from_thread = pool2_unit_create_from_thread;
    def.u_free = pool2_unit_free;
    def.p_init = pool2_init;
    def.p_get_size = pool2_get_size;
    def.p_push = pool2_push;
    def.p_pop = pool2_pop;
    def.p_free = pool2_free;

    /* Optional. */
    def.u_is_in_pool = NULL;
#ifdef ABT_ENABLE_VER_20_API
    def.p_pop_wait = NULL;
#endif
    def.p_pop_timedwait = NULL;
    def.p_remove = NULL;
    def.p_print_all = NULL;

    ABT_pool newpool;
    int ret = ABT_pool_create(&def, ABT_POOL_CONFIG_NULL, &newpool);
    ATS_ERROR(ret, "ABT_pool_create");
    return newpool;
}

/******************************************************************************/
/* Pool 3 */
/******************************************************************************/

ABT_pool g_pool3;
queue_t pool3_queue;

ABT_unit pool3_create_unit(ABT_pool pool, ABT_thread thread)
{
    assert(g_pool3 == pool);
    return (ABT_unit)create_unit(&pool3_queue, thread, 1);
}

void pool3_free_unit(ABT_pool pool, ABT_unit unit)
{
    assert(g_pool3 == pool);
    free_unit(&pool3_queue, (unit_t *)(unit));
}

int pool3_init(ABT_pool pool, ABT_pool_config config)
{
    g_pool3 = pool;
    pool3_queue.list.p_prev = &pool3_queue.list;
    pool3_queue.list.p_next = &pool3_queue.list;
    pool3_queue.size = 0;
    pool3_queue.num_units = 0;
    pthread_mutex_init(&pool3_queue.lock, NULL);
    return ABT_SUCCESS;
}

ABT_bool pool3_is_empty(ABT_pool pool)
{
    assert(g_pool3 == pool);
    return (pool3_queue.size == 0) ? ABT_TRUE : ABT_FALSE;
}

void pool3_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context)
{
    assert(g_pool3 == pool);
    unit_t *p_unit = (unit_t *)unit;
    assert(p_unit->pool_type == 1);
    queue_push(&pool3_queue, p_unit);
}

ABT_thread pool3_pop(ABT_pool pool, ABT_pool_context context)
{
    assert(g_pool3 == pool);
    unit_t *p_unit = queue_pop(&pool3_queue);
    return p_unit ? p_unit->thread : ABT_THREAD_NULL;
}

void pool3_free(ABT_pool pool)
{
    assert(g_pool3 == pool);
    assert(pool3_queue.size == 0);
    assert(pool3_queue.num_units == 0);
    pthread_mutex_destroy(&pool3_queue.lock);
}

ABT_pool create_pool3(void)
{
    /* Pool definition */
    int ret;
    ABT_pool_user_def def;
    ret = ABT_pool_user_def_create(pool3_create_unit, pool3_free_unit,
                                   pool3_is_empty, pool3_pop, pool3_push, &def);
    ATS_ERROR(ret, "ABT_pool_user_def_create");
    ret = ABT_pool_user_def_set_init(def, pool3_init);
    ATS_ERROR(ret, "ABT_pool_user_def_set_init");
    ret = ABT_pool_user_def_set_free(def, pool3_free);
    ATS_ERROR(ret, "ABT_pool_user_def_set_free");

    ABT_pool newpool;
    ret = ABT_pool_create(def, ABT_POOL_CONFIG_NULL, &newpool);
    ATS_ERROR(ret, "ABT_pool_create");
    ret = ABT_pool_user_def_free(&def);
    ATS_ERROR(ret, "ABT_pool_user_def_free");
    return newpool;
}

/******************************************************************************/
/* Pool 4 */
/******************************************************************************/

ABT_pool g_pool4;
queue_t pool4_queue;

ABT_unit pool4_create_unit(ABT_pool pool, ABT_thread thread)
{
    assert(g_pool4 == pool);
    return (ABT_unit)create_unit(&pool4_queue, thread, 1);
}

void pool4_free_unit(ABT_pool pool, ABT_unit unit)
{
    assert(g_pool4 == pool);
    free_unit(&pool4_queue, (unit_t *)(unit));
}

int pool4_init(ABT_pool pool, ABT_pool_config config)
{
    g_pool4 = pool;
    pool4_queue.list.p_prev = &pool4_queue.list;
    pool4_queue.list.p_next = &pool4_queue.list;
    pool4_queue.size = 0;
    pool4_queue.num_units = 0;
    pthread_mutex_init(&pool4_queue.lock, NULL);
    return ABT_SUCCESS;
}

ABT_bool pool4_is_empty(ABT_pool pool)
{
    assert(g_pool4 == pool);
    return (pool4_queue.size == 0) ? ABT_TRUE : ABT_FALSE;
}

void pool4_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context)
{
    assert(g_pool4 == pool);
    unit_t *p_unit = (unit_t *)unit;
    assert(p_unit->pool_type == 1);
    queue_push(&pool4_queue, p_unit);
}

ABT_thread pool4_pop(ABT_pool pool, ABT_pool_context context)
{
    assert(g_pool4 == pool);
    unit_t *p_unit = queue_pop(&pool4_queue);
    return p_unit ? p_unit->thread : ABT_THREAD_NULL;
}

void pool4_free(ABT_pool pool)
{
    assert(g_pool4 == pool);
    assert(pool4_queue.size == 0);
    assert(pool4_queue.num_units == 0);
    pthread_mutex_destroy(&pool4_queue.lock);
}

ABT_pool create_pool4(void)
{
    /* Pool definition */
    int ret;
    ABT_pool_user_def def;
    ret = ABT_pool_user_def_create(pool4_create_unit, pool4_free_unit,
                                   pool4_is_empty, pool4_pop, pool4_push, &def);
    ATS_ERROR(ret, "ABT_pool_user_def_create");
    ret = ABT_pool_user_def_set_init(def, pool4_init);
    ATS_ERROR(ret, "ABT_pool_user_def_set_init");
    ret = ABT_pool_user_def_set_free(def, pool4_free);
    ATS_ERROR(ret, "ABT_pool_user_def_set_free");

    ABT_pool newpool;
    ret = ABT_pool_create(def, ABT_POOL_CONFIG_NULL, &newpool);
    ATS_ERROR(ret, "ABT_pool_create");
    ret = ABT_pool_user_def_free(&def);
    ATS_ERROR(ret, "ABT_pool_user_def_free");
    return newpool;
}
