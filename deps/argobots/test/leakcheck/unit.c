/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdlib.h>
#include <assert.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_pool. */

#define POOL_KIND_USER ((ABT_pool_kind)999)

typedef struct unit_t {
    ABT_thread thread;
} unit_t;

ABT_unit unit_create_from_thread(ABT_thread thread)
{
    unit_t *p_unit = (unit_t *)malloc(sizeof(unit_t));
    if (!p_unit) {
        return ABT_UNIT_NULL;
    }
    p_unit->thread = thread;
    return (ABT_unit)p_unit;
}

void unit_free(ABT_unit *p_unit)
{
    free(*p_unit);
}

ABT_unit pool_create_unit(ABT_pool pool, ABT_thread thread)
{
    return unit_create_from_thread(thread);
}

void pool_free_unit(ABT_pool pool, ABT_unit unit)
{
    unit_free(&unit);
}

typedef struct {
    int num_units;
    ABT_unit units[16];
} pool_data_t;

int pool_init(ABT_pool pool, ABT_pool_config config)
{
    (void)config;
    pool_data_t *pool_data = (pool_data_t *)malloc(sizeof(pool_data_t));
    assert(pool_data);
    pool_data->num_units = 0;
    int ret = ABT_pool_set_data(pool, pool_data);
    assert(ret == ABT_SUCCESS);
    return ABT_SUCCESS;
}

size_t pool_get_size(ABT_pool pool)
{
    pool_data_t *pool_data;
    int ret = ABT_pool_get_data(pool, (void **)&pool_data);
    assert(ret == ABT_SUCCESS);
    return pool_data->num_units;
}

ABT_bool pool_is_empty(ABT_pool pool)
{
    pool_data_t *pool_data;
    int ret = ABT_pool_get_data(pool, (void **)&pool_data);
    assert(ret == ABT_SUCCESS);
    return pool_data->num_units == 0 ? ABT_TRUE : ABT_FALSE;
}

void pool_push_old(ABT_pool pool, ABT_unit unit)
{
    /* Very simple: no lock, fixed size.  This implementation is for simplicity,
     * so don't use it in a real program unless you know what you are really
     * doing. */
    pool_data_t *pool_data;
    int ret = ABT_pool_get_data(pool, (void **)&pool_data);
    assert(ret == ABT_SUCCESS);
    pool_data->units[pool_data->num_units++] = unit;
}

void pool_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context)
{
    pool_push_old(pool, unit);
}

ABT_unit pool_pop_old(ABT_pool pool)
{
    pool_data_t *pool_data;
    int ret = ABT_pool_get_data(pool, (void **)&pool_data);
    assert(ret == ABT_SUCCESS);
    if (pool_data->num_units == 0)
        return ABT_UNIT_NULL;
    return pool_data->units[--pool_data->num_units];
}

ABT_thread pool_pop(ABT_pool pool, ABT_pool_context context)
{
    ABT_unit unit = pool_pop_old(pool);
    if (unit != ABT_UNIT_NULL) {
        return ((unit_t *)unit)->thread;
    } else {
        return ABT_THREAD_NULL;
    }
}

int pool_free_old(ABT_pool pool)
{
    pool_data_t *pool_data;
    int ret = ABT_pool_get_data(pool, (void **)&pool_data);
    assert(ret == ABT_SUCCESS);
    free(pool_data);
    return ABT_SUCCESS;
}

void pool_free(ABT_pool pool)
{
    pool_data_t *pool_data;
    int ret = ABT_pool_get_data(pool, (void **)&pool_data);
    assert(ret == ABT_SUCCESS);
    free(pool_data);
}

ABT_pool create_pool(void)
{
    int ret;
    ABT_pool pool;

    ABT_pool_user_def def = (ABT_pool_user_def)RAND_PTR;
    ret = ABT_pool_user_def_create(pool_create_unit, pool_free_unit,
                                   pool_is_empty, pool_pop, pool_push, &def);
    assert(ret == ABT_SUCCESS);
    ret = ABT_pool_user_def_set_init(def, pool_init);
    assert(ret == ABT_SUCCESS);
    ret = ABT_pool_user_def_set_free(def, pool_free);
    assert(ret == ABT_SUCCESS);

    ret = ABT_pool_create(def, ABT_POOL_CONFIG_NULL, &pool);
    assert(ret == ABT_SUCCESS);
    ret = ABT_pool_user_def_free(&def);
    assert(ret == ABT_SUCCESS && def == ABT_POOL_USER_DEF_NULL);
    return pool;
}

ABT_pool create_pool_old(void)
{
    int ret;
    ABT_pool pool;

    ABT_pool_def pool_def;
    pool_def.access = ABT_POOL_ACCESS_MPMC;
    pool_def.u_get_type = NULL;
    pool_def.u_get_thread = NULL;
    pool_def.u_get_task = NULL;
    pool_def.u_is_in_pool = NULL;
    pool_def.u_create_from_thread = unit_create_from_thread;
    pool_def.u_create_from_task = NULL;
    pool_def.u_free = unit_free;
    pool_def.p_init = pool_init;
    pool_def.p_get_size = pool_get_size;
    pool_def.p_push = pool_push_old;
    pool_def.p_pop = pool_pop_old;
#ifdef ABT_ENABLE_VER_20_API
    pool_def.p_pop_wait = NULL;
#endif
    pool_def.p_pop_timedwait = NULL;
    pool_def.p_remove = NULL;
    pool_def.p_free = pool_free_old;
    pool_def.p_print_all = NULL;

    ret = ABT_pool_create(&pool_def, ABT_POOL_CONFIG_NULL, &pool);
    assert(ret == ABT_SUCCESS);
    return pool;
}

void thread_func(void *arg)
{
    int ret;
    ABT_thread thread;
    ret = ABT_self_get_thread(&thread);
    assert(ret == ABT_SUCCESS);
    ret = ABT_thread_set_associated_pool(thread, (ABT_pool)arg);
    /* ABT_thread_set_associated_pool() might fail. */
    (void)ret;

    ret = ABT_self_set_associated_pool((ABT_pool)arg);
    /* ABT_self_set_associated_pool() might fail, too. */
    (void)ret;

    ret = ABT_thread_yield();
    assert(ret == ABT_SUCCESS);
}

void program(int use_predef, int must_succeed)
{
    int ret, i;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    /* Pool creation should be covered by other tests. */
    ABT_pool pools[2];
    if (use_predef == 0) {
        pools[0] = create_pool();
        pools[1] = create_pool();
    } else if (use_predef == 1) {
        pools[0] = create_pool();
        pools[1] = create_pool_old();
    } else if (use_predef == 2) {
        pools[0] = create_pool_old();
        pools[1] = create_pool_old();
    } else if (use_predef == 3) {
        pools[0] = create_pool();
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_FALSE, &pools[1]);
        assert(ret == ABT_SUCCESS);
    } else if (use_predef == 4) {
        pools[0] = create_pool_old();
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_FALSE, &pools[1]);
        assert(ret == ABT_SUCCESS);
    } else {
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_FALSE, &pools[0]);
        assert(ret == ABT_SUCCESS);
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_FALSE, &pools[1]);
        assert(ret == ABT_SUCCESS);
    }
    /* ABT_xstream_set_main_sched_basic() should be checked by other tests. */
    ABT_xstream self_xstream;
    ret = ABT_self_get_xstream(&self_xstream);
    assert(ret == ABT_SUCCESS);
    ret = ABT_xstream_set_main_sched_basic(self_xstream, ABT_SCHED_DEFAULT, 2,
                                           pools);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    ABT_thread threads[4];
    for (i = 0; i < 4; i++) {
        ABT_pool target_pool = pools[i % 2];
        ABT_pool mig_pool = pools[(i / 2) % 2];
        threads[i] = (ABT_thread)RAND_PTR;
        ret = ABT_thread_create(target_pool, thread_func, (void *)mig_pool,
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(threads[i] == (ABT_thread)RAND_PTR);
            threads[i] = ABT_THREAD_NULL;
#else
            assert(threads[i] == ABT_THREAD_NULL);
#endif
        }
    }
    /* Push and pop some threads. */
    for (i = 0; i < 4; i++) {
        ABT_pool target_pool = pools[i % 2];
        ABT_pool mig_pool = pools[(i / 2) % 2];
        ABT_unit unit;
        ret = ABT_pool_pop(target_pool, &unit);
        assert(ret == ABT_SUCCESS);
        if (unit != ABT_UNIT_NULL) {
            /* Push back to the pool. */
            ret = ABT_pool_push(mig_pool, unit);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret != ABT_SUCCESS) {
                /* If it is pushed to the same pool, the push operation may not
                 * fail though this behavior is not clearly mentioned in the
                 * specification. */
                assert(mig_pool != target_pool);
                ret = ABT_pool_push(target_pool, unit);
                assert(ret == ABT_SUCCESS);
            }
        }
    }
    /* Execute these threads. */
    for (i = 0; i < 4; i++) {
        if (threads[i] != ABT_THREAD_NULL) {
            ret = ABT_thread_free(&threads[i]);
            assert(ret == ABT_SUCCESS);
        }
    }
    ret = ABT_finalize();
    assert(ret == ABT_SUCCESS);
}

int main()
{
    setup_env();
    rtrace_init();

    int use_predef;
    for (use_predef = 0; use_predef <= 5; use_predef++) {

        if (use_rtrace()) {
            do {
                rtrace_start();
                program(use_predef, 0);
            } while (!rtrace_stop());
        }
        /* If no failure, it should succeed again. */
        program(use_predef, 1);
    }

    rtrace_finalize();
    return 0;
}
