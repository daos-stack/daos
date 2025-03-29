/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdlib.h>
#include <assert.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_xstream. */

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
    while (1) {
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

void program(int type, int must_succeed)
{
    int ret;
    rtrace_set_enabled(0);
    /* Checking ABT_init() should be done by other tests. */
    ret = ABT_init(0, 0);
    assert(ret == ABT_SUCCESS);
    rtrace_set_enabled(1);

    ABT_xstream xstream = (ABT_xstream)RAND_PTR;
    ABT_sched sched = ABT_SCHED_NULL;
    if (type == 0 || type == 2) {
        /* Use ABT_xstream_create() or use ABT_xstream_create_with_rank(). */
        ABT_pool pool = (ABT_pool)RAND_PTR;
        ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC,
                                    ABT_TRUE, &pool);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(pool == (ABT_pool)RAND_PTR);
#else
            assert(pool == ABT_POOL_NULL);
#endif
            goto FAILED;
        }
        sched = (ABT_sched)RAND_PTR;
        ABT_sched_def sched_def;
        sched_def.type = ABT_SCHED_TYPE_ULT;
        sched_def.init = sched_init;
        sched_def.run = sched_run;
        sched_def.free = sched_free;
        sched_def.get_migr_pool = NULL;
        ret = ABT_sched_create(&sched_def, 1, &pool, ABT_SCHED_CONFIG_NULL,
                               &sched);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(sched == (ABT_sched)RAND_PTR);
#else
            assert(sched == ABT_SCHED_NULL);
#endif
            /* Maybe the second time will succeed. */
            ret = ABT_sched_create(&sched_def, 1, &pool, ABT_SCHED_CONFIG_NULL,
                                   &sched);
            if (ret != ABT_SUCCESS) {
                /* Second time failed.  Give up. */
                ret = ABT_pool_free(&pool);
                assert(ret == ABT_SUCCESS && pool == ABT_POOL_NULL);
                goto FAILED;
            }
        }
        if (type == 0) {
            ret = ABT_xstream_create(sched, &xstream);
        } else {
            int new_rank = 39; /* Any number is fine. */
            ret = ABT_xstream_create_with_rank(sched, new_rank, &xstream);
        }
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(xstream == (ABT_xstream)RAND_PTR);
#else
            assert(xstream == ABT_XSTREAM_NULL);
#endif
            /* Maybe the second time will succeed. */
            if (type == 0) {
                ret = ABT_xstream_create(sched, &xstream);
            } else {
                int new_rank = 39; /* Any number is fine. */
                ret = ABT_xstream_create_with_rank(sched, new_rank, &xstream);
            }
            if (ret != ABT_SUCCESS) {
                ret = ABT_sched_free(&sched);
                assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
                /* ABT_sched_free() will free its automatic pool. */
                goto FAILED;
            }
        }
    } else if (type == 1) {
        /* Use ABT_xstream_create_basic(). */
        ABT_pool pools[1] = { ABT_POOL_NULL };
        ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, pools,
                                       ABT_SCHED_CONFIG_NULL, &xstream);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret != ABT_SUCCESS) {
#ifdef ABT_ENABLE_VER_20_API
            assert(xstream == (ABT_xstream)RAND_PTR);
#else
            assert(xstream == ABT_XSTREAM_NULL);
#endif
            /* Maybe the second time will succeed. */
            ret = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, pools,
                                           ABT_SCHED_CONFIG_NULL, &xstream);
            if (ret != ABT_SUCCESS) {
                goto FAILED;
            }
        }
    }
    ret = ABT_xstream_join(xstream);
    assert(ret == ABT_SUCCESS);
    ret = ABT_xstream_revive(xstream);
    assert(ret == ABT_SUCCESS);
    ret = ABT_xstream_free(&xstream);
    assert(ret == ABT_SUCCESS && xstream == ABT_XSTREAM_NULL);
    if (sched != ABT_SCHED_NULL) {
        ret = ABT_sched_free(&sched);
        assert(ret == ABT_SUCCESS && sched == ABT_SCHED_NULL);
    }

FAILED:
    ret = ABT_finalize();
    assert(ret == ABT_SUCCESS);
}

int main()
{
    setup_env();
    rtrace_init();

    int type;
    for (type = 0; type < 3; type++) {
        if (use_rtrace()) {
            do {
                rtrace_start();
                program(type, 0);
            } while (!rtrace_stop());
        }

        /* If no failure, it should succeed again. */
        program(type, 1);
    }

    rtrace_finalize();
    return 0;
}
