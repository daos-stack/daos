/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdlib.h>
#include <assert.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_sched. */

#define SCHED_PREDEF_USER ((ABT_sched_predef)999)

int sched_init(ABT_sched sched, ABT_sched_config config)
{
    (void)config;
    void *p_data = malloc(128);
    if (!p_data)
        return ABT_ERR_MEM;
    ABT_sched_set_data(sched, (void *)p_data);
    return ABT_SUCCESS;
}

void sched_run(ABT_sched sched)
{
    ABT_pool pools[16];
    int ret, num_pools;
    ret = ABT_sched_get_num_pools(sched, &num_pools);
    assert(ret == ABT_SUCCESS);
    if (num_pools > 16)
        num_pools = 16;
    ret = ABT_sched_get_pools(sched, num_pools, 0, pools);
    assert(ret == ABT_SUCCESS);

    while (1) {
        int i;
        ABT_unit unit;
        for (i = 0; i < num_pools; i++) {
            ret = ABT_pool_pop(pools[0], &unit);
            assert(ret == ABT_SUCCESS);
            if (unit != ABT_UNIT_NULL) {
                ret = ABT_xstream_run_unit(unit, pools[0]);
                assert(ret == ABT_SUCCESS);
            }
        }
        ABT_bool stop;
        ABT_sched_has_to_stop(sched, &stop);
        if (stop == ABT_TRUE)
            break;
        ABT_xstream_check_events(sched);
    }
}

int sched_free(ABT_sched sched)
{
    void *p_data;
    ABT_sched_get_data(sched, (void **)&p_data);
    free(p_data);
    return ABT_SUCCESS;
}

ABT_sched create_sched(int automatic, int must_succeed)
{
    int ret;
    ABT_pool pool = (ABT_pool)RAND_PTR;
    ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE,
                                &pool);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(pool == (ABT_pool)RAND_PTR);
#else
        assert(pool == ABT_POOL_NULL);
#endif
        return ABT_SCHED_NULL;
    }

    ABT_sched_def sched_def;
    sched_def.type = ABT_SCHED_TYPE_ULT;
    sched_def.init = sched_init;
    sched_def.run = sched_run;
    sched_def.free = sched_free;
    sched_def.get_migr_pool = NULL;
    ABT_sched_config sched_config;
    if (automatic) {
        /* The default "automatic" configuration of ABT_sched_create() is
         * "false". */
        sched_config = (ABT_sched_config)RAND_PTR;
        ret = ABT_sched_config_create(&sched_config, ABT_sched_config_automatic,
                                      ABT_TRUE, ABT_sched_config_var_end);
        if (ret != ABT_SUCCESS) {
            assert(sched_config == (ABT_sched_config)RAND_PTR);
            ret = ABT_pool_free(&pool);
            assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
            return ABT_SCHED_NULL;
        }
    } else {
        sched_config = ABT_SCHED_CONFIG_NULL;
    }
    ABT_sched sched = (ABT_sched)RAND_PTR;
    ret = ABT_sched_create(&sched_def, 1, &pool, sched_config, &sched);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(sched == (ABT_sched)RAND_PTR);
#else
        assert(sched == ABT_SCHED_NULL);
#endif
        /* Maybe the second time will succeed. */
        ret = ABT_sched_create(&sched_def, 1, &pool, sched_config, &sched);
        if (ret != ABT_SUCCESS) {
            /* Second time failed.  Give up. */
            ret = ABT_pool_free(&pool);
            assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
            sched = ABT_SCHED_NULL;
        }
    }
    if (sched_config != ABT_SCHED_CONFIG_NULL) {
        ret = ABT_sched_config_free(&sched_config);
        assert(ret == ABT_SUCCESS && sched_config == ABT_SCHED_CONFIG_NULL);
    }
    return sched;
}

ABT_sched create_sched_basic(ABT_sched_predef predef, int automatic,
                             int must_succeed)
{
    int ret;
    ABT_pool pools[3] = { ABT_POOL_NULL, ABT_POOL_NULL, ABT_POOL_NULL };
    pools[1] = (ABT_pool)RAND_PTR;
    ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE,
                                &pools[1]);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(pools[1] == (ABT_pool)RAND_PTR);
#else
        assert(pools[1] == ABT_POOL_NULL);
#endif
        return ABT_SCHED_NULL;
    }
    /* The default "automatic" configuration of ABT_sched_create_basic() is
     * "true". */
    ABT_sched_config sched_config;
    if (!automatic) {
        sched_config = (ABT_sched_config)RAND_PTR;
        ret = ABT_sched_config_create(&sched_config, ABT_sched_config_automatic,
                                      ABT_FALSE, ABT_sched_config_var_end);
        if (ret != ABT_SUCCESS) {
            assert(sched_config == (ABT_sched_config)RAND_PTR);
            ret = ABT_pool_free(&pools[1]);
            assert(ret == ABT_SUCCESS && pools[1] == ABT_POOL_NULL);
            return ABT_SCHED_NULL;
        }
    } else {
        sched_config = ABT_SCHED_CONFIG_NULL;
    }
    ABT_sched sched = (ABT_sched)RAND_PTR;
    ret = ABT_sched_create_basic(predef, 3, pools, sched_config, &sched);

    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
        assert(sched == (ABT_sched)RAND_PTR);
#else
        assert(sched == ABT_SCHED_NULL);
#endif
        /* Maybe the second time will succeed. */
        ret = ABT_sched_create_basic(predef, 3, pools, sched_config, &sched);
        if (ret != ABT_SUCCESS) {
            /* Second time failed.  Give up. */
            ret = ABT_pool_free(&pools[1]);
            assert(ret == ABT_SUCCESS && pools[1] == ABT_POOL_NULL);
            sched = ABT_SCHED_NULL;
        }
    }
    if (sched_config != ABT_SCHED_CONFIG_NULL) {
        ret = ABT_sched_config_free(&sched_config);
        assert(ret == ABT_SUCCESS && sched_config == ABT_SCHED_CONFIG_NULL);
    }
    return sched;
}

void program(ABT_sched_predef predef, int automatic, int type, int must_succeed)
{
    int ret;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    if (type == 0)
        rtrace_set_enabled(1);

    ABT_sched sched;
    if (predef == SCHED_PREDEF_USER) {
        sched = create_sched(automatic, must_succeed);
    } else {
        sched = create_sched_basic(predef, automatic, must_succeed);
    }
    if (type != 0)
        rtrace_set_enabled(1);

    if (sched == ABT_SCHED_NULL) {
        /* Allocation failed, so do nothing. */
    } else if (type == 0) {
        /* Just free.  We must free even an automatic one. */
        ret = ABT_sched_free(&sched);
        assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
    } else if (type == 1) {
        /* Use it for ABT_xstream_create(). */
        ABT_xstream xstream = (ABT_xstream)RAND_PTR;
        ret = ABT_xstream_create(sched, &xstream);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            ret = ABT_xstream_free(&xstream);
            assert(ret == ABT_SUCCESS && xstream == ABT_XSTREAM_NULL);
            if (!automatic) {
                ret = ABT_sched_free(&sched);
                assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
            }
        } else {
            ret = ABT_sched_free(&sched);
            assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
        }
    } else if (type == 2) {
        /* Stackable scheduler. */
        if (automatic) {
            /* Try to use it's own pool. */
            ABT_xstream self_xstream;
            ret = ABT_self_get_xstream(&self_xstream);
            assert(ret == ABT_SUCCESS);
            ABT_pool pool;
            ret = ABT_xstream_get_main_pools(self_xstream, 1, &pool);
            assert(ret == ABT_SUCCESS);
            ret = ABT_pool_add_sched(pool, sched);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret != ABT_SUCCESS) {
                /* The second attempt might succeed. */
                ret = ABT_pool_add_sched(pool, sched);
                /* For some reasons, it fails, but then let's use another ES.*/
            }
            if (ret == ABT_SUCCESS) {
                ret = ABT_sched_finish(sched);
                assert(ret == ABT_SUCCESS);
                sched = ABT_SCHED_NULL;
            }
        }
        if (sched != ABT_SCHED_NULL) {
            /* Need to use another ES's pool since otherwise there's no way to
             * know if sched has been finished. */
            ABT_pool pools[2] = { ABT_POOL_NULL, ABT_POOL_NULL };
            ABT_xstream xstream;
            ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 2, pools,
                                           ABT_SCHED_CONFIG_NULL, &xstream);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret != ABT_SUCCESS) {
                ret = ABT_sched_free(&sched);
                assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
            } else {
                ABT_pool pool;
                ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
                assert(ret == ABT_SUCCESS);
                ret = ABT_pool_add_sched(pool, sched);
                assert(!must_succeed || ret == ABT_SUCCESS);
                if (ret != ABT_SUCCESS) {
                    /* The second attempt might succeed. */
                    ret = ABT_pool_add_sched(pool, sched);
                    if (ret != ABT_SUCCESS) {
                        ret = ABT_sched_free(&sched);
                        assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
                    }
                }
                if (sched != ABT_SCHED_NULL) {
                    /* Finish that scheduler. */
                    ret = ABT_sched_finish(sched);
                    assert(ret == ABT_SUCCESS);
                }
                ret = ABT_xstream_free(&xstream);
                assert(ret == ABT_SUCCESS && xstream == ABT_XSTREAM_NULL);
                if (!automatic && sched != ABT_SCHED_NULL) {
                    ret = ABT_sched_free(&sched);
                    assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
                }
            }
        }
    } else if (type == 3) {
        /* ABT_xstream_set_main_sched() (primary xstream).  Automatic pool
         * should be freed in ABT_finialze(). */
        ABT_xstream self_xstream;
        ret = ABT_self_get_xstream(&self_xstream);
        assert(ret == ABT_SUCCESS);
        ret = ABT_xstream_set_main_sched(self_xstream, sched);
        assert(ret == ABT_SUCCESS);
    } else if (type == 4) {
        /* ABT_xstream_set_main_sched() (secondary xstream). */
        ABT_pool pools[2] = { ABT_POOL_NULL, ABT_POOL_NULL };
        ABT_xstream xstream;
        ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 2, pools,
                                       ABT_SCHED_CONFIG_NULL, &xstream);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            ret = ABT_sched_free(&sched);
            assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
        } else {
            /* Terminate xstream. */
            ret = ABT_xstream_join(xstream);
            assert(ret == ABT_SUCCESS);
            ret = ABT_xstream_set_main_sched(xstream, sched);
            assert(ret == ABT_SUCCESS);
            /* Finish that execution stream. */
            ret = ABT_xstream_revive(xstream);
            assert(ret == ABT_SUCCESS);
            ret = ABT_xstream_free(&xstream);
            assert(ret == ABT_SUCCESS && xstream == ABT_XSTREAM_NULL);
            if (!automatic) {
                ret = ABT_sched_free(&sched);
                assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
            }
        }
    } else if (type == 5) {
        /* Replaced by ABT_xstream_set_main_sched() (primary xstream) */
        ABT_xstream self_xstream;
        ret = ABT_self_get_xstream(&self_xstream);
        assert(ret == ABT_SUCCESS);
        ret = ABT_xstream_set_main_sched(self_xstream, sched);
        assert(ret == ABT_SUCCESS);
        ABT_pool pools[2] = { ABT_POOL_NULL, ABT_POOL_NULL };
        ret = ABT_xstream_set_main_sched_basic(self_xstream, ABT_SCHED_DEFAULT,
                                               2, pools);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS && !automatic) {
            ret = ABT_sched_free(&sched);
            assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
        }
    } else if (type == 6) {
        /* Replaced by ABT_xstream_set_main_sched() (secondary xstream) */
        ABT_xstream xstream;
        ret = ABT_xstream_create(sched, &xstream);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
            ret = ABT_sched_free(&sched);
            assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
        } else {
            /* Terminate xstream. */
            ret = ABT_xstream_join(xstream);
            assert(ret == ABT_SUCCESS);
            ABT_pool pools[2] = { ABT_POOL_NULL, ABT_POOL_NULL };
            ret = ABT_xstream_set_main_sched_basic(xstream, ABT_SCHED_DEFAULT,
                                                   2, pools);
            assert(!must_succeed || ret == ABT_SUCCESS);
            if (ret == ABT_SUCCESS && !automatic) {
                ret = ABT_sched_free(&sched);
                assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
            }
            /* Finish that execution stream. */
            ret = ABT_xstream_revive(xstream);
            assert(ret == ABT_SUCCESS);
            ret = ABT_xstream_free(&xstream);
            assert(ret == ABT_SUCCESS && xstream == ABT_XSTREAM_NULL);
            if (!automatic && sched != ABT_SCHED_NULL) {
                ret = ABT_sched_free(&sched);
                assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
            }
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
#ifdef COMPLETE_CHECK
    ABT_sched_predef predefs[] = { ABT_SCHED_DEFAULT,    ABT_SCHED_BASIC,
                                   ABT_SCHED_PRIO,       ABT_SCHED_RANDWS,
                                   ABT_SCHED_BASIC_WAIT, SCHED_PREDEF_USER };
#else
    ABT_sched_predef predefs[] = { ABT_SCHED_DEFAULT, SCHED_PREDEF_USER };
#endif
    /* Checking all takes too much time. */
    for (i = 0; i < (int)(sizeof(predefs) / sizeof(predefs[0])); i++) {
        for (automatic = 0; automatic <= 1; automatic++) {
            for (type = 0; type < 7; type++) {

                if (use_rtrace()) {
                    do {
                        rtrace_start();
                        program(predefs[i], automatic, type, 0);
                    } while (!rtrace_stop());
                }

                /* If no failure, it should succeed again. */
                program(predefs[i], automatic, type, 1);
            }
        }
    }
#ifndef COMPLETE_CHECK
    ABT_sched_predef extra_predefs[] = { ABT_SCHED_BASIC, ABT_SCHED_PRIO,
                                         ABT_SCHED_RANDWS,
                                         ABT_SCHED_BASIC_WAIT };
    for (i = 0; i < (int)(sizeof(extra_predefs) / sizeof(extra_predefs[0]));
         i++) {
        for (automatic = 0; automatic <= 1; automatic++) {
            for (type = 0; type < 2; type++) { /* Only check 0 and 1. */

                if (use_rtrace()) {
                    do {
                        rtrace_start();
                        program(extra_predefs[i], automatic, type, 0);
                    } while (!rtrace_stop());
                }

                /* If no failure, it should succeed again. */
                program(extra_predefs[i], automatic, type, 1);
            }
        }
    }
#endif

    rtrace_finalize();
    return 0;
}
