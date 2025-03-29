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

#define POOL_KIND_USER ((ABT_pool_kind)998)
#define POOL_KIND_USER2 ((ABT_pool_kind)999)

ABT_unit unit_create_from_thread(ABT_thread thread)
{
    return (ABT_unit)thread;
}

void unit_free(ABT_unit *p_unit)
{
    (void)p_unit;
}

ABT_unit pool_create_unit(ABT_pool pool, ABT_thread thread)
{
    return (ABT_unit)thread;
}

void pool_free_unit(ABT_pool pool, ABT_unit unit)
{
    (void)unit;
}

typedef struct {
    int num_units;
    ABT_unit units[16];
} pool_data_t;

int pool_init(ABT_pool pool, ABT_pool_config config)
{
    (void)config;
    pool_data_t *pool_data = (pool_data_t *)malloc(sizeof(pool_data_t));
    if (!pool_data) {
        return ABT_ERR_MEM;
    }
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
        return (ABT_thread)unit;
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

ABT_pool create_pool(int automatic, int must_succeed)
{
    int ret;
    ABT_pool pool = (ABT_pool)RAND_PTR;

    ABT_pool_user_def def = (ABT_pool_user_def)RAND_PTR;
    ret = ABT_pool_user_def_create(pool_create_unit, pool_free_unit,
                                   pool_is_empty, pool_pop, pool_push, &def);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
        assert(def == (ABT_pool_user_def)RAND_PTR);
        return ABT_POOL_NULL;
    }
    ret = ABT_pool_user_def_set_init(def, pool_init);
    assert(ret == ABT_SUCCESS);
    ret = ABT_pool_user_def_set_free(def, pool_free);
    assert(ret == ABT_SUCCESS);

    ABT_pool_config config = (ABT_pool_config)RAND_PTR;
    if (automatic) {
        /* Create a configuration. */
        ret = ABT_pool_config_create(&config);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            assert(config == (ABT_pool_config)RAND_PTR);
            ret = ABT_pool_user_def_free(&def);
            assert(ret == ABT_SUCCESS && def == ABT_POOL_USER_DEF_NULL);
            return ABT_POOL_NULL;
        }
        const int automatic_val = 1;
        ret = ABT_pool_config_set(config, ABT_pool_config_automatic.key,
                                  ABT_pool_config_automatic.type,
                                  (const void *)&automatic_val);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            ret = ABT_pool_config_free(&config);
            assert(ret == ABT_SUCCESS && config == ABT_POOL_CONFIG_NULL);
            ret = ABT_pool_user_def_free(&def);
            assert(ret == ABT_SUCCESS && def == ABT_POOL_USER_DEF_NULL);
            return ABT_POOL_NULL;
        }
    } else {
        /* By default, a pool created by ABT_pool_create() is not automatically
         * freed. */
        config = ABT_POOL_CONFIG_NULL;
    }
    ret = ABT_pool_create(def, config, &pool);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(pool == (ABT_pool)RAND_PTR);
#else
        assert(pool == ABT_POOL_NULL);
#endif
        pool = ABT_POOL_NULL;
    }
    if (config != ABT_POOL_CONFIG_NULL) {
        ret = ABT_pool_config_free(&config);
        assert(ret == ABT_SUCCESS && config == ABT_POOL_CONFIG_NULL);
    }
    ret = ABT_pool_user_def_free(&def);
    assert(ret == ABT_SUCCESS && def == ABT_POOL_USER_DEF_NULL);
    return pool;
}

ABT_pool create_pool_old(int automatic, int must_succeed)
{
    int ret;
    ABT_pool pool = (ABT_pool)RAND_PTR;

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

    ABT_pool_config config = (ABT_pool_config)RAND_PTR;
    if (automatic) {
        /* Create a configuration. */
        ret = ABT_pool_config_create(&config);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            assert(config == (ABT_pool_config)RAND_PTR);
            return ABT_POOL_NULL;
        }
        const int automatic_val = 1;
        ret = ABT_pool_config_set(config, ABT_pool_config_automatic.key,
                                  ABT_pool_config_automatic.type,
                                  (const void *)&automatic_val);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            ret = ABT_pool_config_free(&config);
            assert(ret == ABT_SUCCESS && config == ABT_POOL_CONFIG_NULL);
            return ABT_POOL_NULL;
        }
    } else {
        /* By default, a pool created by ABT_pool_create() is not automatically
         * freed. */
        config = ABT_POOL_CONFIG_NULL;
    }
    ret = ABT_pool_create(&pool_def, config, &pool);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(pool == (ABT_pool)RAND_PTR);
#else
        assert(pool == ABT_POOL_NULL);
#endif
        pool = ABT_POOL_NULL;
    }
    if (config != ABT_POOL_CONFIG_NULL) {
        ret = ABT_pool_config_free(&config);
        assert(ret == ABT_SUCCESS && config == ABT_POOL_CONFIG_NULL);
    }
    return pool;
}

ABT_pool create_pool_basic(ABT_pool_kind kind, int automatic, int must_succeed)
{
    int ret;
    ABT_pool pool = (ABT_pool)RAND_PTR;
    ret = ABT_pool_create_basic(kind, ABT_POOL_ACCESS_MPMC,
                                automatic ? ABT_TRUE : ABT_FALSE, &pool);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(pool == (ABT_pool)RAND_PTR);
#else
        assert(pool == ABT_POOL_NULL);
#endif
        return ABT_POOL_NULL;
    }
    return pool;
}

void program(ABT_pool_kind kind, int automatic, int type, int must_succeed)
{
    int ret;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    ABT_pool pool;
    if (kind == POOL_KIND_USER) {
        pool = create_pool(automatic, must_succeed);
    } else if (kind == POOL_KIND_USER2) {
        pool = create_pool_old(automatic, must_succeed);
    } else {
        pool = create_pool_basic(kind, automatic, must_succeed);
    }

    if (pool == ABT_POOL_NULL) {
        /* Allocation failed, so do nothing. */
    } else if (type == 0) {
        /* Just free.  We must free an automatic one too. */
        ret = ABT_pool_free(&pool);
        assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
    } else if (type == 1) {
        /* Use it for ABT_sched_create_basic(). */
        ABT_sched sched = (ABT_sched)RAND_PTR;
        ret = ABT_sched_create_basic(ABT_SCHED_DEFAULT, 1, &pool,
                                     ABT_SCHED_CONFIG_NULL, &sched);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(sched == (ABT_sched)RAND_PTR);
#else
            assert(sched == ABT_SCHED_NULL);
#endif
            /* Maybe the second time will succeed. */
            ret = ABT_sched_create_basic(ABT_SCHED_DEFAULT, 1, &pool,
                                         ABT_SCHED_CONFIG_NULL, &sched);
            if (ret != ABT_SUCCESS) {
                /* Second time failed.  Give up. */
                ret = ABT_pool_free(&pool);
                assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
                sched = ABT_SCHED_NULL;
            }
        }
        if (sched != ABT_SCHED_NULL) {
            ret = ABT_sched_free(&sched);
            assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
            if (!automatic) {
                ret = ABT_pool_free(&pool);
                assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
            }
        }
    } else if (type == 2) {
        /* Use it for ABT_xstream_create_basic(). */
        ABT_xstream xstream = (ABT_xstream)RAND_PTR;
        ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &pool,
                                       ABT_SCHED_CONFIG_NULL, &xstream);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(xstream == (ABT_xstream)RAND_PTR);
#else
            assert(xstream == ABT_XSTREAM_NULL);
#endif
            /* Maybe the second time will succeed. */
            ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &pool,
                                           ABT_SCHED_CONFIG_NULL, &xstream);
            if (ret != ABT_SUCCESS) {
                /* Second time failed.  Give up. */
                ret = ABT_pool_free(&pool);
                assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
                xstream = ABT_XSTREAM_NULL;
            }
        }
        if (xstream != ABT_XSTREAM_NULL) {
            ret = ABT_xstream_free(&xstream);
            assert(ret == ABT_SUCCESS && xstream == ABT_XSTREAM_NULL);
            if (!automatic) {
                ret = ABT_pool_free(&pool);
                assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
            }
        }
    } else if (type == 3) {
        /* Use it for ABT_xstream_set_main_sched_basic() */
        ABT_xstream self_xstream;
        ret = ABT_self_get_xstream(&self_xstream);
        assert(ret == ABT_SUCCESS);
        ret = ABT_xstream_set_main_sched_basic(self_xstream, ABT_SCHED_DEFAULT,
                                               1, &pool);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            ret = ABT_pool_free(&pool);
            assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
        }
    }
    ret = ABT_finalize();
    assert(ret == ABT_SUCCESS);
}

int main()
{
    setup_env();
    rtrace_init();

    int i, automatic, type;
    ABT_pool_kind kinds[] = { ABT_POOL_FIFO, POOL_KIND_USER, POOL_KIND_USER2 };
    /* Checking all takes too much time. */
    for (i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); i++) {
        for (automatic = 0; automatic <= 1; automatic++) {
            for (type = 0; type < 4; type++) {
                if (use_rtrace()) {
                    do {
                        rtrace_start();
                        program(kinds[i], automatic, type, 0);
                    } while (!rtrace_stop());
                }

                /* If no failure, it should succeed again. */
                program(kinds[i], automatic, type, 1);
            }
        }
    }

    ABT_pool_kind extra_kinds[] = { ABT_POOL_FIFO_WAIT, ABT_POOL_RANDWS };
    for (i = 0; i < (int)(sizeof(extra_kinds) / sizeof(extra_kinds[0])); i++) {
        for (automatic = 0; automatic <= 1; automatic++) {
            for (type = 0; type < 1; type++) {
                if (use_rtrace()) {
                    do {
                        rtrace_start();
                        program(extra_kinds[i], automatic, type, 0);
                    } while (!rtrace_stop());
                }

                /* If no failure, it should succeed again. */
                program(extra_kinds[i], automatic, type, 1);
            }
        }
    }

    rtrace_finalize();
    return 0;
}
