/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include "abt.h"
#include "abttest.h"

#define NUM_REPETITIONS 1000

int g_should_not_run_before_yield_flag = 0;
void should_not_run_before_yield(void *arg)
{
    g_should_not_run_before_yield_flag++;
}

int g_set_barrier = 0;
void set_barrier(void *arg)
{
    ABT_barrier barrier = (ABT_barrier)arg;
    g_set_barrier = 1;
    ABT_barrier_wait(barrier);
}

int main(int argc, char *argv[])
{
    int ret, i, j, lock_i, unlock_i;
    ABT_thread evil_thread;
    ABT_xstream xstream;
    ABT_pool pool;

    ATS_init(argc, argv, 1);

    ret = ABT_xstream_self(&xstream);
    ATS_ERROR(ret, "ABT_xstream_self");
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    ATS_ERROR(ret, "ABT_xstream_get_main_pools");

    /* Case: barrier (num_waiters = 1) */
    {
        ABT_barrier barrier;

        ret = ABT_barrier_create(1, &barrier);
        ATS_ERROR(ret, "ABT_barrier_create");
        ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                ABT_THREAD_ATTR_NULL, &evil_thread);
        ATS_ERROR(ret, "ABT_thread_create");

        /* No yield is allowed. */
        g_should_not_run_before_yield_flag = 0;
        for (i = 0; i < NUM_REPETITIONS; i++) {
            ret = ABT_barrier_wait(barrier);
            ATS_ERROR(ret, "ABT_barrier_wait");
        }
        assert(g_should_not_run_before_yield_flag == 0);

        ret = ABT_thread_free(&evil_thread);
        ATS_ERROR(ret, "ABT_thread_free");
        ret = ABT_barrier_free(&barrier);
        ATS_ERROR(ret, "ABT_barrier_free");
    }

    /* Case: barrier (num_waiters = 2) */
    {
        ABT_barrier barrier;
        ABT_thread barrier_thread;

        ret = ABT_barrier_create(2, &barrier);
        ATS_ERROR(ret, "ABT_barrier_create");

        for (i = 0; i < NUM_REPETITIONS; i++) {
            ret = ABT_thread_create(pool, set_barrier, (void *)barrier,
                                    ABT_THREAD_ATTR_NULL, &barrier_thread);
            ATS_ERROR(ret, "ABT_thread_create");
            g_set_barrier = 0;
            while (g_set_barrier == 0) {
                ret = ABT_thread_yield();
                ATS_ERROR(ret, "ABT_thread_yield");
            }
            ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                    ABT_THREAD_ATTR_NULL, &evil_thread);
            ATS_ERROR(ret, "ABT_thread_create");

            /* No yield is allowed. */
            g_should_not_run_before_yield_flag = 0;
            ret = ABT_barrier_wait(barrier);
            ATS_ERROR(ret, "ABT_barrier_wait");
            assert(g_should_not_run_before_yield_flag == 0);

            ret = ABT_thread_free(&barrier_thread);
            ATS_ERROR(ret, "ABT_thread_free");
            ret = ABT_thread_free(&evil_thread);
            ATS_ERROR(ret, "ABT_thread_free");
        }

        ret = ABT_barrier_free(&barrier);
        ATS_ERROR(ret, "ABT_barrier_free");
    }

    /* Case: future */
    {
        ABT_future future;

        ret = ABT_future_create(1, NULL, &future);
        ATS_ERROR(ret, "ABT_future_create");
        ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                ABT_THREAD_ATTR_NULL, &evil_thread);
        ATS_ERROR(ret, "ABT_thread_create");

        /* No yield is allowed. */
        g_should_not_run_before_yield_flag = 0;
        ret = ABT_future_set(future, NULL);
        ATS_ERROR(ret, "ABT_future_set");
        for (i = 0; i < NUM_REPETITIONS; i++) {
            ret = ABT_future_wait(future);
            ATS_ERROR(ret, "ABT_future_wait");
        }
        assert(g_should_not_run_before_yield_flag == 0);

        ret = ABT_thread_free(&evil_thread);
        ATS_ERROR(ret, "ABT_thread_free");
        ret = ABT_future_free(&future);
        ATS_ERROR(ret, "ABT_future_free");
    }

    /* Case: eventual */
    {
        ABT_eventual eventual;

        ret = ABT_eventual_create(0, &eventual);
        ATS_ERROR(ret, "ABT_eventual_create");
        ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                ABT_THREAD_ATTR_NULL, &evil_thread);
        ATS_ERROR(ret, "ABT_thread_create");

        /* No yield is allowed. */
        g_should_not_run_before_yield_flag = 0;
        ret = ABT_eventual_set(eventual, NULL, 0);
        ATS_ERROR(ret, "ABT_eventual_set");
        for (i = 0; i < NUM_REPETITIONS; i++) {
            ret = ABT_eventual_wait(eventual, NULL);
            ATS_ERROR(ret, "ABT_eventual_wait");
        }
        assert(g_should_not_run_before_yield_flag == 0);

        ret = ABT_thread_free(&evil_thread);
        ATS_ERROR(ret, "ABT_thread_free");
        ret = ABT_eventual_free(&eventual);
        ATS_ERROR(ret, "ABT_eventual_free");
    }

    typedef int (*mutex_lock_f)(ABT_mutex);
    typedef int (*mutex_unlock_f)(ABT_mutex);

    mutex_lock_f mutex_lock_fs[] = { ABT_mutex_lock, ABT_mutex_lock_low,
                                     ABT_mutex_lock_high, ABT_mutex_spinlock,
                                     ABT_mutex_trylock };
    mutex_unlock_f mutex_unlock_fs[] = { ABT_mutex_unlock, ABT_mutex_unlock_se,
                                         ABT_mutex_unlock_de };

    /* Case: mutex (non-recursive) */
    for (lock_i = 0;
         lock_i < (int)(sizeof(mutex_lock_fs) / sizeof(mutex_lock_fs[0]));
         lock_i++) {
        for (unlock_i = 0; unlock_i < (int)(sizeof(mutex_unlock_fs) /
                                            sizeof(mutex_unlock_fs[0]));
             unlock_i++) {
            ABT_mutex mutex;

            ret = ABT_mutex_create(&mutex);
            ATS_ERROR(ret, "ABT_mutex_create");
            ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                    ABT_THREAD_ATTR_NULL, &evil_thread);
            ATS_ERROR(ret, "ABT_thread_create");

            /* No yield is allowed. */
            g_should_not_run_before_yield_flag = 0;
            for (i = 0; i < NUM_REPETITIONS; i++) {
                ret = mutex_lock_fs[lock_i](mutex);
                ATS_ERROR(ret, "ABT_mutex_lock_xxx");
                ret = mutex_unlock_fs[unlock_i](mutex);
                ATS_ERROR(ret, "ABT_mutex_unlock_xxx");
            }
            assert(g_should_not_run_before_yield_flag == 0);

            ret = ABT_thread_free(&evil_thread);
            ATS_ERROR(ret, "ABT_thread_free");
            ret = ABT_mutex_free(&mutex);
            ATS_ERROR(ret, "ABT_mutex_free");
        }
    }

    /* Case: mutex (recursive) */
    for (lock_i = 0;
         lock_i < (int)(sizeof(mutex_lock_fs) / sizeof(mutex_lock_fs[0]));
         lock_i++) {
        for (unlock_i = 0; unlock_i < (int)(sizeof(mutex_unlock_fs) /
                                            sizeof(mutex_unlock_fs[0]));
             unlock_i++) {
            ABT_mutex mutex;
            ABT_mutex_attr mutex_attr;

            ret = ABT_mutex_attr_create(&mutex_attr);
            ATS_ERROR(ret, "ABT_mutex_attr_create");
            ret = ABT_mutex_attr_set_recursive(mutex_attr, ABT_TRUE);
            ATS_ERROR(ret, "ABT_mutex_attr_set_recursive");
            ret = ABT_mutex_create_with_attr(mutex_attr, &mutex);
            ATS_ERROR(ret, "ABT_mutex_create_with_attr");
            ret = ABT_mutex_attr_free(&mutex_attr);
            ATS_ERROR(ret, "ABT_mutex_attr_free");
            ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                    ABT_THREAD_ATTR_NULL, &evil_thread);
            ATS_ERROR(ret, "ABT_thread_create");

            /* No yield is allowed. */
            g_should_not_run_before_yield_flag = 0;
            for (i = 0; i < NUM_REPETITIONS; i++) {
                /* This lock is recursive. */
                for (j = 0; j < 10; j++) {
                    ret = mutex_lock_fs[lock_i](mutex);
                    ATS_ERROR(ret, "ABT_mutex_lock_xxx");
                }
                for (j = 0; j < 10; j++) {
                    ret = mutex_unlock_fs[unlock_i](mutex);
                    ATS_ERROR(ret, "ABT_mutex_unlock_xxx");
                }
            }
            assert(g_should_not_run_before_yield_flag == 0);

            ret = ABT_thread_free(&evil_thread);
            ATS_ERROR(ret, "ABT_thread_free");
            ret = ABT_mutex_free(&mutex);
            ATS_ERROR(ret, "ABT_mutex_free");
        }
    }
    /* Case: rwlock (readers' lock) */
    {
        ABT_rwlock rwlock;

        ret = ABT_rwlock_create(&rwlock);
        ATS_ERROR(ret, "ABT_rwlock_create");
        ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                ABT_THREAD_ATTR_NULL, &evil_thread);
        ATS_ERROR(ret, "ABT_thread_create");

        /* No yield is allowed. */
        g_should_not_run_before_yield_flag = 0;
        for (i = 0; i < NUM_REPETITIONS; i++) {
            for (j = 0; j < 10; j++) {
                ret = ABT_rwlock_rdlock(rwlock);
                ATS_ERROR(ret, "ABT_rwlock_rdlock");
            }
            for (j = 0; j < 10; j++) {
                ret = ABT_rwlock_unlock(rwlock);
                ATS_ERROR(ret, "ABT_rwlock_unlock");
            }
        }
        assert(g_should_not_run_before_yield_flag == 0);

        ret = ABT_thread_free(&evil_thread);
        ATS_ERROR(ret, "ABT_thread_free");
        ret = ABT_rwlock_free(&rwlock);
        ATS_ERROR(ret, "ABT_rwlock_free");
    }

    /* Case: rwlock (writer's lock) */
    {
        ABT_rwlock rwlock;

        ret = ABT_rwlock_create(&rwlock);
        ATS_ERROR(ret, "ABT_rwlock_create");
        ret = ABT_thread_create(pool, should_not_run_before_yield, NULL,
                                ABT_THREAD_ATTR_NULL, &evil_thread);
        ATS_ERROR(ret, "ABT_thread_create");

        /* No yield is allowed. */
        g_should_not_run_before_yield_flag = 0;
        for (i = 0; i < NUM_REPETITIONS; i++) {
            ret = ABT_rwlock_wrlock(rwlock);
            ATS_ERROR(ret, "ABT_rwlock_rdlock");
            ret = ABT_rwlock_unlock(rwlock);
            ATS_ERROR(ret, "ABT_rwlock_unlock");
        }
        assert(g_should_not_run_before_yield_flag == 0);

        ret = ABT_thread_free(&evil_thread);
        ATS_ERROR(ret, "ABT_thread_free");
        ret = ABT_rwlock_free(&rwlock);
        ATS_ERROR(ret, "ABT_rwlock_free");
    }

    /* Finalize */
    ret = ATS_finalize(0);
    return ret;
}
