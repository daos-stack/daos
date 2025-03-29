/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

/* Check if a user-defined pool is registered properly. */

#include <stdlib.h>
#include <pthread.h>
#include "abt.h"
#include "abttest.h"

typedef struct {
    /* create_unit */
    int create_unit_counter;
    ABT_thread create_unit_thread_in;
    ABT_unit create_unit_out;
    /* free_unit */
    int free_unit_counter;
    ABT_unit free_unit_in;
    /* is_empty */
    int is_empty_counter;
    ABT_bool is_empty_out;
    /* pop */
    int pop_counter;
    ABT_pool_context pop_context_in;
    ABT_thread pop_out;
    /* push */
    int push_counter;
    ABT_unit push_unit_in;
    ABT_pool_context push_context_in;
    /* init */
    int init_counter;
    ABT_pool_config init_config_in;
    /* free */
    int free_counter;
    /* get_size */
    int get_size_counter;
    size_t get_size_out;
    /* pop_wait */
    int pop_wait_counter;
    double pop_wait_time_secs_in;
    ABT_pool_context pop_wait_context_in;
    ABT_thread pop_wait_out;
    /* pop_many */
    int pop_many_counter;
    size_t pop_many_max_threads_in;
    ABT_pool_context pop_many_context_in;
    ABT_thread pop_many_threads_out[10];
    size_t pop_many_num_popped_out;
    /* push_many */
    int push_many_counter;
    ABT_unit push_many_units_in[10];
    size_t push_many_num_units_in;
    ABT_pool_context push_many_context_in;
    /* print_all */
    int print_all_counter;
    void *print_all_arg_in;
    void (*print_all_print_f_in)(void *, ABT_thread);
} expect_t;
expect_t g_expect;

ABT_pool create_pool(ABT_pool_config config);

void empty_func(void *arg)
{
    ; /* Empty */
}

void create_revive_to_func(void *arg)
{
    int ret;
    ABT_pool pool;
    ret = ABT_self_get_last_pool(&pool);
    ATS_ERROR(ret, "ABT_self_get_last_pool");

    ABT_unit self_unit;
    ABT_thread self_thread;
    ret = ABT_self_get_thread(&self_thread);
    ATS_ERROR(ret, "ABT_self_get_thread");
    ret = ABT_thread_get_unit(self_thread, &self_unit);
    ATS_ERROR(ret, "ABT_thread_get_unit");

    ABT_thread thread;
    int create_unit_counter = g_expect.create_unit_counter;
    int push_counter = g_expect.push_counter;
    g_expect.push_unit_in = self_unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_CREATE_TO;
    ret = ABT_thread_create_to(pool, empty_func, NULL, ABT_THREAD_ATTR_NULL,
                               &thread);
    ATS_ERROR(ret, "ABT_thread_create_to");
    assert(g_expect.create_unit_counter == create_unit_counter + 1);
    assert(g_expect.push_counter == push_counter + 1);

    ABT_unit unit;
    ret = ABT_thread_get_unit(thread, &unit);
    ATS_ERROR(ret, "ABT_thread_get_unit");
    assert(g_expect.create_unit_out == unit);

    ret = ABT_thread_join(thread);
    ATS_ERROR(ret, "ABT_thread_join");

    g_expect.push_unit_in = self_unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_REVIVE_TO;
    ret = ABT_thread_revive_to(pool, empty_func, NULL, &thread);
    ATS_ERROR(ret, "ABT_thread_revive_to");
    assert(g_expect.push_counter == push_counter + 2);

    int free_unit_counter = g_expect.free_unit_counter;
    g_expect.free_unit_in = unit;
    ret = ABT_thread_free(&thread);
    ATS_ERROR(ret, "ABT_thread_free");
    assert(g_expect.free_unit_counter == free_unit_counter + 1);
}

void suspend_func(void *arg)
{
    int ret;
    ret = ABT_self_suspend();
    ATS_ERROR(ret, "ABT_self_suspend");
    /* Resumed by ABT_self_resume() and scheduled by ABT_self_yield_to() */
    ret = ABT_self_suspend();
    ATS_ERROR(ret, "ABT_self_suspend");
    /* Resumed and scheduled by ABT_self_resume_yield_to() */
}

void yield_func(void *arg)
{
    int ret;
    ABT_pool pool;
    ret = ABT_self_get_last_pool(&pool);
    ATS_ERROR(ret, "ABT_self_get_last_pool");

    ABT_unit self_unit;
    ABT_thread self_thread;
    ret = ABT_self_get_thread(&self_thread);
    ATS_ERROR(ret, "ABT_self_get_thread");
    ret = ABT_thread_get_unit(self_thread, &self_unit);
    ATS_ERROR(ret, "ABT_thread_get_unit");

    int push_counter = g_expect.push_counter;
    g_expect.push_unit_in = self_unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_YIELD;
    ret = ABT_self_yield();
    ATS_ERROR(ret, "ABT_self_yield");
    assert(g_expect.push_counter == push_counter + 1);

    ABT_thread thread;
    int create_unit_counter = g_expect.create_unit_counter;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_CREATE;
    ret = ABT_thread_create(pool, suspend_func, NULL, ABT_THREAD_ATTR_NULL,
                            &thread);
    ATS_ERROR(ret, "ABT_thread_create");
    assert(g_expect.create_unit_counter == create_unit_counter + 1);
    assert(g_expect.push_counter == push_counter + 2);

    ABT_unit unit;
    ret = ABT_thread_get_unit(thread, &unit);
    ATS_ERROR(ret, "ABT_thread_get_unit");
    assert(g_expect.create_unit_out == unit);

    ABT_unit popped_unit;
    int pop_counter = g_expect.pop_counter;
    ret = ABT_pool_pop(pool, &popped_unit);
    ATS_ERROR(ret, "ABT_pool_pop");
    assert(g_expect.pop_counter == pop_counter + 1);
    assert(g_expect.pop_out == thread && popped_unit == unit);

    g_expect.push_unit_in = self_unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_YIELD_TO;
    ret = ABT_self_yield_to(thread);
    ATS_ERROR(ret, "ABT_self_yield_to");
    assert(g_expect.push_counter == push_counter + 3);

    g_expect.push_unit_in = unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_RESUME;
    ret = ABT_thread_resume(thread);
    ATS_ERROR(ret, "ABT_thread_resume");
    assert(g_expect.push_counter == push_counter + 4);

    pop_counter = g_expect.pop_counter;
    ret = ABT_pool_pop(pool, &popped_unit);
    ATS_ERROR(ret, "ABT_pool_pop");
    assert(g_expect.pop_counter == pop_counter + 1);
    assert(g_expect.pop_out == thread && popped_unit == unit);

    g_expect.push_unit_in = self_unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_YIELD_TO;
    ret = ABT_self_yield_to(thread);
    ATS_ERROR(ret, "ABT_self_yield_to");
    assert(g_expect.push_counter == push_counter + 5);

    g_expect.push_unit_in = self_unit;
    g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_RESUME_YIELD_TO;
    ret = ABT_self_resume_yield_to(thread);
    ATS_ERROR(ret, "ABT_self_resume_yield_to");
    assert(g_expect.push_counter == push_counter + 6);

    int free_unit_counter = g_expect.free_unit_counter;
    g_expect.free_unit_in = unit;
    ret = ABT_thread_free(&thread);
    ATS_ERROR(ret, "ABT_thread_free");
    assert(g_expect.free_unit_counter == free_unit_counter + 1);
}

void print_all_func(void *arg, ABT_thread thread)
{
    assert(0);
}

int main(int argc, char *argv[])
{
    int ret;

    /* Initialize */
    ATS_read_args(argc, argv);
    /* Initialize Argobots. */
    ATS_init(argc, argv, 1);

    ABT_pool pool;

    /* ABT_pool_create() */
    {
        ABT_pool_config config;
        ret = ABT_pool_config_create(&config);
        ATS_ERROR(ret, "ABT_pool_config_create");
        g_expect.init_config_in = config;
        assert(g_expect.init_counter == 0);
        pool = create_pool(config);
        assert(g_expect.init_counter == 1);
        ret = ABT_pool_config_free(&config);
        ATS_ERROR(ret, "ABT_pool_config_free");
    }

    /* ABT_pool_is_empty() */
    {
        int is_empty_counter = g_expect.is_empty_counter;
        ABT_bool is_empty;
        ret = ABT_pool_is_empty(pool, &is_empty);
        ATS_ERROR(ret, "ABT_pool_is_empty");
        assert(g_expect.is_empty_counter == is_empty_counter + 1);
        assert(is_empty == ABT_TRUE && g_expect.is_empty_out == is_empty);
    }

    /* ABT_pool_get_size() */
    {
        int get_size_counter = g_expect.get_size_counter;
        size_t size;
        ret = ABT_pool_get_size(pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_size");
        assert(g_expect.get_size_counter == get_size_counter + 1);
        assert(size == 0 && g_expect.get_size_out == size);
    }

    /* ABT_pool_get_total_size() */
    {
        int get_size_counter = g_expect.get_size_counter;
        size_t size;
        ret = ABT_pool_get_total_size(pool, &size);
        ATS_ERROR(ret, "ABT_pool_get_total_size");
        assert(g_expect.get_size_counter == get_size_counter + 1);
        assert(size == 0 && g_expect.get_size_out == size);
    }

    /* ABT_pool_pop() */
    {
        int pop_counter = g_expect.pop_counter;
        ABT_unit unit;
        ret = ABT_pool_pop(pool, &unit);
        ATS_ERROR(ret, "ABT_pool_pop");
        assert(g_expect.pop_counter == pop_counter + 1);
        assert(unit == ABT_UNIT_NULL && g_expect.pop_out == ABT_THREAD_NULL);
    }

    /* ABT_pool_pop_wait() */
    {
        int pop_wait_counter = g_expect.pop_wait_counter;
        g_expect.pop_wait_time_secs_in = 1.0;
        ABT_unit unit;
        ret = ABT_pool_pop_wait(pool, &unit, 1.0);
        ATS_ERROR(ret, "ABT_pool_pop_wait");
        assert(g_expect.pop_wait_counter == pop_wait_counter + 1);
        assert(unit == ABT_UNIT_NULL &&
               g_expect.pop_wait_out == ABT_THREAD_NULL);
    }

    /* ABT_pool_pop_thread() */
    {
        int pop_counter = g_expect.pop_counter;
        ABT_thread thread;
        g_expect.pop_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_thread(pool, &thread);
        ATS_ERROR(ret, "ABT_pool_pop_thread");
        assert(g_expect.pop_counter == pop_counter + 1);
        assert(thread == ABT_THREAD_NULL &&
               g_expect.pop_out == ABT_THREAD_NULL);
    }

    /* ABT_pool_pop_thread_ex() */
    {
        int pop_counter = g_expect.pop_counter;
        ABT_thread thread;
        g_expect.pop_context_in = (ABT_pool_context)777;
        ret = ABT_pool_pop_thread_ex(pool, &thread, (ABT_pool_context)777);
        ATS_ERROR(ret, "ABT_pool_pop_thread_ex");
        assert(g_expect.pop_counter == pop_counter + 1);
        assert(thread == ABT_THREAD_NULL &&
               g_expect.pop_out == ABT_THREAD_NULL);
    }

    /* ABT_pool_pop_threads() */
    {
        int pop_many_counter = g_expect.pop_many_counter;
        ABT_thread threads[3];
        g_expect.pop_many_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        size_t num, len = sizeof(threads) / sizeof(threads[0]);
        ret = ABT_pool_pop_threads(pool, threads, len, &num);
        ATS_ERROR(ret, "ABT_pool_pop_threads");
        assert(g_expect.pop_many_counter == pop_many_counter + 1);
        assert(num == 0 && g_expect.pop_many_num_popped_out == 0);
    }

    /* ABT_pool_pop_threads_ex() */
    {
        int pop_many_counter = g_expect.pop_many_counter;
        ABT_thread threads[3];
        g_expect.pop_many_context_in = (ABT_pool_context)777;
        size_t num, len = sizeof(threads) / sizeof(threads[0]);
        ret = ABT_pool_pop_threads_ex(pool, threads, len, &num,
                                      (ABT_pool_context)777);
        ATS_ERROR(ret, "ABT_pool_pop_threads_ex");
        assert(g_expect.pop_many_counter == pop_many_counter + 1);
        assert(num == 0 && g_expect.pop_many_num_popped_out == 0);
    }

    /* ABT_pool_pop_wait_thread() */
    {
        int pop_wait_counter = g_expect.pop_wait_counter;
        ABT_thread thread;
        g_expect.pop_wait_time_secs_in = 1.0;
        g_expect.pop_wait_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_wait_thread(pool, &thread, 1.0);
        ATS_ERROR(ret, "ABT_pool_pop_wait_thread");
        assert(g_expect.pop_wait_counter == pop_wait_counter + 1);
        assert(thread == ABT_THREAD_NULL &&
               g_expect.pop_wait_out == ABT_THREAD_NULL);
    }

    /* ABT_pool_pop_wait_thread_ex() */
    {
        int pop_wait_counter = g_expect.pop_wait_counter;
        ABT_thread thread;
        g_expect.pop_wait_time_secs_in = 1.0;
        g_expect.pop_wait_context_in = (ABT_pool_context)777;
        ret = ABT_pool_pop_wait_thread_ex(pool, &thread, 1.0,
                                          (ABT_pool_context)777);
        ATS_ERROR(ret, "ABT_pool_pop_wait_thread_ex");
        assert(g_expect.pop_wait_counter == pop_wait_counter + 1);
        assert(thread == ABT_THREAD_NULL &&
               g_expect.pop_wait_out == ABT_THREAD_NULL);
    }

    /* ABT_pool_print_all_threads() */
    {
        int print_all_counter = g_expect.print_all_counter;
        g_expect.print_all_arg_in = (void *)&g_expect.print_all_arg_in;
        g_expect.print_all_print_f_in = print_all_func;
        ret = ABT_pool_print_all_threads(pool, g_expect.print_all_arg_in,
                                         g_expect.print_all_print_f_in);
        ATS_ERROR(ret, "ABT_pool_print_all_threads");
        assert(g_expect.print_all_counter == print_all_counter + 1);
    }

    /* ABT_pool_push(), ABT_thread_create(), and ABT_thread_revive() */
    {
        ABT_thread thread;
        int create_unit_counter = g_expect.create_unit_counter;
        int push_counter = g_expect.push_counter;
        g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_CREATE;
        ret = ABT_thread_create(pool, empty_func, NULL, ABT_THREAD_ATTR_NULL,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        assert(g_expect.create_unit_counter == create_unit_counter + 1);
        assert(g_expect.push_counter == push_counter + 1);

        ABT_unit unit;
        ret = ABT_thread_get_unit(thread, &unit);
        ATS_ERROR(ret, "ABT_thread_get_unit");
        assert(g_expect.create_unit_out == unit);

        ABT_unit popped_unit;
        int pop_counter = g_expect.pop_counter;
        ret = ABT_pool_pop(pool, &popped_unit);
        ATS_ERROR(ret, "ABT_pool_pop");
        assert(g_expect.pop_counter == pop_counter + 1);
        assert(unit == popped_unit && g_expect.pop_out == thread);

        g_expect.push_unit_in = unit;
        ret = ABT_pool_push(pool, unit);
        ATS_ERROR(ret, "ABT_pool_push");
        assert(g_expect.push_counter == push_counter + 2);

        ret = ABT_pool_pop(pool, &popped_unit);
        ATS_ERROR(ret, "ABT_pool_pop");
        assert(g_expect.pop_counter == pop_counter + 2);
        assert(unit == popped_unit && g_expect.pop_out == thread);

        ret = ABT_self_schedule(thread, pool);
        ATS_ERROR(ret, "ABT_self_schedule");

        ret = ABT_thread_join(thread);
        ATS_ERROR(ret, "ABT_thread_join");

        g_expect.push_unit_in = unit;
        g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_REVIVE;
        ret = ABT_thread_revive(pool, empty_func, NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_revive");
        assert(g_expect.push_counter == push_counter + 3);

        ret = ABT_pool_pop(pool, &popped_unit);
        ATS_ERROR(ret, "ABT_pool_pop");
        assert(g_expect.pop_counter == pop_counter + 3);
        assert(unit == popped_unit && g_expect.pop_out == thread);

        ret = ABT_self_schedule(thread, pool);
        ATS_ERROR(ret, "ABT_self_schedule");

        int free_unit_counter = g_expect.free_unit_counter;
        g_expect.free_unit_in = unit;
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        assert(g_expect.free_unit_counter == free_unit_counter + 1);
    }

    /* ABT_pool_push_thread(), ABT_pool_push_thread_ex(),
     * ABT_pool_push_threads(), and ABT_pool_push_threads_ex() */
    {
        ABT_thread thread;
        int create_unit_counter = g_expect.create_unit_counter;
        int push_counter = g_expect.push_counter;
        g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_CREATE;
        ret = ABT_thread_create(pool, empty_func, NULL, ABT_THREAD_ATTR_NULL,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        assert(g_expect.create_unit_counter == create_unit_counter + 1);
        assert(g_expect.push_counter == push_counter + 1);

        ABT_thread popped_thread;
        int pop_counter = g_expect.pop_counter;
        g_expect.pop_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_thread(pool, &popped_thread);
        ATS_ERROR(ret, "ABT_pool_pop_thread");
        assert(g_expect.pop_counter == pop_counter + 1);
        assert(thread == popped_thread);

        g_expect.push_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_push_thread(pool, thread);
        ATS_ERROR(ret, "ABT_pool_push_thread");
        assert(g_expect.push_counter == push_counter + 2);

        g_expect.pop_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_thread(pool, &popped_thread);
        ATS_ERROR(ret, "ABT_pool_pop_thread");
        assert(g_expect.pop_counter == pop_counter + 2);
        assert(thread == popped_thread);

        g_expect.push_context_in = (ABT_pool_context)777;
        ret = ABT_pool_push_thread_ex(pool, thread, (ABT_pool_context)777);
        ATS_ERROR(ret, "ABT_pool_push_thread_ex");
        assert(g_expect.push_counter == push_counter + 3);

        g_expect.pop_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_thread(pool, &popped_thread);
        ATS_ERROR(ret, "ABT_pool_pop_thread");
        assert(g_expect.pop_counter == pop_counter + 3);
        assert(thread == popped_thread);

        int push_many_counter = g_expect.push_many_counter;
        g_expect.push_many_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_push_threads(pool, &thread, 1);
        ATS_ERROR(ret, "ABT_pool_push_threads");
        assert(g_expect.push_many_counter == push_many_counter + 1);

        g_expect.pop_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_thread(pool, &popped_thread);
        ATS_ERROR(ret, "ABT_pool_pop_thread");
        assert(g_expect.pop_counter == pop_counter + 4);
        assert(thread == popped_thread);

        g_expect.push_many_context_in = (ABT_pool_context)777;
        ret = ABT_pool_push_threads_ex(pool, &thread, 1, (ABT_pool_context)777);
        ATS_ERROR(ret, "ABT_pool_push_threads_ex");
        assert(g_expect.push_many_counter == push_many_counter + 2);

        g_expect.pop_context_in = ABT_POOL_CONTEXT_OP_POOL_OTHER;
        ret = ABT_pool_pop_thread(pool, &popped_thread);
        ATS_ERROR(ret, "ABT_pool_pop_thread");
        assert(g_expect.pop_counter == pop_counter + 5);
        assert(thread == popped_thread);

        ret = ABT_self_schedule(thread, pool);
        ATS_ERROR(ret, "ABT_self_schedule");

        int free_unit_counter = g_expect.free_unit_counter;
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        assert(g_expect.free_unit_counter == free_unit_counter + 1);
    }

    /* ABT_thread_create_to() and ABT_thread_revive_to() */
    {
        ABT_thread thread;
        int create_unit_counter = g_expect.create_unit_counter;
        int push_counter = g_expect.push_counter;
        g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_CREATE;
        ret = ABT_thread_create(pool, create_revive_to_func, NULL,
                                ABT_THREAD_ATTR_NULL, &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        assert(g_expect.create_unit_counter == create_unit_counter + 1);
        assert(g_expect.push_counter == push_counter + 1);

        while (1) {
            ABT_unit unit;
            int pop_counter = g_expect.pop_counter;
            ret = ABT_pool_pop(pool, &unit);
            ATS_ERROR(ret, "ABT_pool_pop");
            assert(g_expect.pop_counter == pop_counter + 1);
            if (unit == ABT_UNIT_NULL) {
                assert(g_expect.pop_out == ABT_THREAD_NULL);
                break;
            } else {
                assert(g_expect.pop_out == thread);
            }
            ret = ABT_self_schedule(thread, pool);
            ATS_ERROR(ret, "ABT_self_schedule");
        }

        int free_unit_counter = g_expect.free_unit_counter;
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        assert(g_expect.free_unit_counter == free_unit_counter + 1);
    }

    /* ABT_self_yield(), ABT_self_yield_to(), ABT_thread_resume(), and
     * ABT_self_resume_yield_to() */
    {
        ABT_thread thread;
        int create_unit_counter = g_expect.create_unit_counter;
        int push_counter = g_expect.push_counter;
        g_expect.push_context_in = ABT_POOL_CONTEXT_OP_THREAD_CREATE;
        ret = ABT_thread_create(pool, yield_func, NULL, ABT_THREAD_ATTR_NULL,
                                &thread);
        ATS_ERROR(ret, "ABT_thread_create");
        assert(g_expect.create_unit_counter == create_unit_counter + 1);
        assert(g_expect.push_counter == push_counter + 1);

        while (1) {
            ABT_unit unit;
            int pop_counter = g_expect.pop_counter;
            ret = ABT_pool_pop(pool, &unit);
            ATS_ERROR(ret, "ABT_pool_pop");
            assert(g_expect.pop_counter == pop_counter + 1);
            if (unit == ABT_UNIT_NULL) {
                assert(g_expect.pop_out == ABT_THREAD_NULL);
                break;
            } else {
                assert(g_expect.pop_out == thread);
            }
            ret = ABT_self_schedule(thread, pool);
            ATS_ERROR(ret, "ABT_self_schedule");
        }

        int free_unit_counter = g_expect.free_unit_counter;
        ret = ABT_thread_free(&thread);
        ATS_ERROR(ret, "ABT_thread_free");
        assert(g_expect.free_unit_counter == free_unit_counter + 1);
    }

    /* ABT_pool_free() */
    {
        int free_counter = g_expect.free_counter;
        ABT_pool_free(&pool);
        assert(g_expect.free_counter == free_counter + 1);
    }

    /* Finalize Argobots. */
    ret = ATS_finalize(0);
    return ret;
}

/* Pool implementation. */

#define POOL_BUFFER_LEN 16
typedef struct {
    ABT_pool pool;
    int ptr;
    ABT_unit units[POOL_BUFFER_LEN];
} pool_t;
pool_t g_pool;

ABT_unit pool_create_unit(ABT_pool pool, ABT_thread thread)
{
    assert(g_pool.pool == pool);
    g_expect.create_unit_counter++;
    if (g_expect.create_unit_thread_in != NULL) {
        assert(g_expect.create_unit_thread_in == thread);
        g_expect.create_unit_thread_in = NULL;
    }
    g_expect.create_unit_out = (ABT_unit)thread;
    return (ABT_unit)thread;
}

void pool_free_unit(ABT_pool pool, ABT_unit unit)
{
    assert(g_pool.pool == pool);
    g_expect.free_unit_counter++;
    if (g_expect.free_unit_in != NULL) {
        assert(g_expect.free_unit_in == unit);
        g_expect.free_unit_in = NULL;
    }
}

ABT_bool pool_is_empty(ABT_pool pool)
{
    assert(g_pool.pool == pool);
    g_expect.is_empty_counter++;
    int i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        if (g_pool.units[i] != ABT_UNIT_NULL) {
            g_expect.is_empty_out = ABT_FALSE;
            return ABT_FALSE;
        }
    }
    g_expect.is_empty_out = ABT_TRUE;
    return ABT_TRUE;
}

ABT_thread pool_pop(ABT_pool pool, ABT_pool_context context)
{
    assert(g_pool.pool == pool);
    g_expect.pop_counter++;
    if (g_expect.pop_context_in != 0) {
        assert(g_expect.pop_context_in & context);
        g_expect.pop_context_in = 0;
    }

    int ptr = g_pool.ptr++, i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        int thread_i = (ptr + i) % POOL_BUFFER_LEN;
        if (g_pool.units[thread_i] != ABT_UNIT_NULL) {
            ABT_unit unit = g_pool.units[thread_i];
            g_pool.units[thread_i] = ABT_UNIT_NULL;
            g_expect.pop_out = (ABT_thread)unit;
            return (ABT_thread)unit;
        }
    }
    g_expect.pop_out = ABT_THREAD_NULL;
    return ABT_THREAD_NULL;
}

void pool_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context)
{
    assert(g_pool.pool == pool);
    g_expect.push_counter++;
    if (g_expect.push_unit_in != NULL) {
        assert(g_expect.push_unit_in == unit);
        g_expect.push_unit_in = NULL;
    }
    if (g_expect.push_context_in != 0) {
        assert(g_expect.push_context_in & context);
        g_expect.push_context_in = 0;
    }

    int ptr = g_pool.ptr++, i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        int thread_i = (ptr + i) % POOL_BUFFER_LEN;
        if (g_pool.units[thread_i] == ABT_UNIT_NULL) {
            g_pool.units[thread_i] = unit;
            return;
        }
    }
    /* This pool gets full. */
    assert(0);
}

int pool_init(ABT_pool pool, ABT_pool_config config)
{
    g_pool.pool = pool;
    g_expect.init_counter++;
    if (g_expect.init_config_in != NULL) {
        assert(g_expect.init_config_in == config);
        g_expect.init_config_in = NULL;
    }
    g_pool.ptr = 0;
    int i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        g_pool.units[i] = ABT_UNIT_NULL;
    }
    return ABT_SUCCESS;
}

void pool_free(ABT_pool pool)
{
    assert(g_pool.pool == pool);
    g_expect.free_counter++;
    int i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        assert(g_pool.units[i] == ABT_UNIT_NULL);
    }
    g_pool.pool = ABT_POOL_NULL;
}

size_t pool_get_size(ABT_pool pool)
{
    assert(g_pool.pool == pool);
    g_expect.get_size_counter++;
    int i;
    size_t size = 0;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        if (g_pool.units[i] != ABT_UNIT_NULL)
            size++;
    }
    g_expect.get_size_out = size;
    return size;
}

ABT_thread pool_pop_wait(ABT_pool pool, double time_secs,
                         ABT_pool_context context)
{
    assert(g_pool.pool == pool);
    g_expect.pop_wait_counter++;
    if (g_expect.pop_wait_context_in != 0) {
        assert(g_expect.pop_wait_context_in & context);
        g_expect.pop_wait_context_in = 0;
    }
    if (g_expect.pop_wait_time_secs_in != 0.0) {
        assert(g_expect.pop_wait_time_secs_in == time_secs);
        g_expect.pop_wait_time_secs_in = 0.0;
    }

    int ptr = g_pool.ptr++, i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        int thread_i = (ptr + i) % POOL_BUFFER_LEN;
        if (g_pool.units[thread_i] != ABT_UNIT_NULL) {
            ABT_unit unit = g_pool.units[thread_i];
            g_pool.units[thread_i] = ABT_UNIT_NULL;
            g_expect.pop_wait_out = (ABT_thread)unit;
            return (ABT_thread)unit;
        }
    }
    g_expect.pop_wait_out = ABT_THREAD_NULL;
    return ABT_THREAD_NULL;
}

void pool_pop_many(ABT_pool pool, ABT_thread *threads, size_t max_threads,
                   size_t *num_popped, ABT_pool_context context)
{
    assert(g_pool.pool == pool);
    g_expect.pop_many_counter++;
    if (g_expect.pop_many_context_in != 0) {
        assert(g_expect.pop_many_context_in & context);
        g_expect.pop_many_context_in = 0;
    }
    if (g_expect.pop_many_max_threads_in != 0) {
        assert(g_expect.pop_many_max_threads_in == max_threads);
        g_expect.pop_many_max_threads_in = 0;
    }

    *num_popped = 0;
    int ptr = g_pool.ptr++, i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        if (*num_popped >= max_threads)
            break;
        int thread_i = (ptr + i) % POOL_BUFFER_LEN;
        if (g_pool.units[thread_i] != ABT_UNIT_NULL) {
            ABT_unit unit = g_pool.units[thread_i];
            g_pool.units[thread_i] = ABT_UNIT_NULL;
            threads[*num_popped] = (ABT_thread)unit;
            g_expect.pop_many_threads_out[*num_popped] = (ABT_thread)unit;
            (*num_popped)++;
        }
    }
    g_expect.pop_many_num_popped_out = *num_popped;
}

void pool_push_many(ABT_pool pool, const ABT_unit *units, size_t num_units,
                    ABT_pool_context context)
{
    assert(g_pool.pool == pool);
    g_expect.push_many_counter++;
    if (g_expect.push_many_context_in != 0) {
        assert(g_expect.push_many_context_in & context);
        g_expect.push_many_context_in = 0;
    }
    if (g_expect.push_many_num_units_in != 0) {
        assert(g_expect.push_many_num_units_in == num_units);
        g_expect.push_many_num_units_in = 0;
    }
    size_t unit_i = 0;
    for (unit_i = 0; unit_i < num_units; unit_i++) {
        if (g_expect.push_many_units_in[unit_i] != NULL)
            assert(g_expect.push_many_units_in[unit_i] == units[unit_i]);
    }

    unit_i = 0;
    int ptr = g_pool.ptr++, i;
    for (i = 0; i < POOL_BUFFER_LEN; i++) {
        if (unit_i == num_units)
            break;
        int thread_i = (ptr + i) % POOL_BUFFER_LEN;
        if (g_pool.units[thread_i] == ABT_UNIT_NULL) {
            g_pool.units[thread_i] = units[unit_i];
            g_expect.push_many_units_in[i] = units[i];
            unit_i++;
        }
    }
    assert(unit_i == num_units);
}

void pool_print_all(ABT_pool pool, void *arg,
                    void (*print_f)(void *, ABT_thread))
{
    assert(g_pool.pool == pool);
    g_expect.print_all_counter++;
    if (g_expect.print_all_arg_in != NULL) {
        assert(g_expect.print_all_arg_in == arg);
        g_expect.print_all_arg_in = NULL;
    }
    if (g_expect.print_all_print_f_in != NULL) {
        assert(g_expect.print_all_print_f_in == print_f);
        g_expect.print_all_print_f_in = NULL;
    }
}

ABT_pool create_pool(ABT_pool_config config)
{
    /* Pool definition */
    int ret;
    ABT_pool_user_def def;
    ret = ABT_pool_user_def_create(pool_create_unit, pool_free_unit,
                                   pool_is_empty, pool_pop, pool_push, &def);
    ATS_ERROR(ret, "ABT_pool_user_def_create");
    ret = ABT_pool_user_def_set_init(def, pool_init);
    ATS_ERROR(ret, "ABT_pool_user_def_set_init");
    ret = ABT_pool_user_def_set_free(def, pool_free);
    ATS_ERROR(ret, "ABT_pool_user_def_set_free");
    ret = ABT_pool_user_def_set_get_size(def, pool_get_size);
    ATS_ERROR(ret, "ABT_pool_user_def_set_get_size");
    ret = ABT_pool_user_def_set_pop_wait(def, pool_pop_wait);
    ATS_ERROR(ret, "ABT_pool_user_def_set_pop_wait");
    ret = ABT_pool_user_def_set_pop_many(def, pool_pop_many);
    ATS_ERROR(ret, "ABT_pool_user_def_set_pop_many");
    ret = ABT_pool_user_def_set_push_many(def, pool_push_many);
    ATS_ERROR(ret, "ABT_pool_user_def_set_push_many");
    ret = ABT_pool_user_def_set_print_all(def, pool_print_all);
    ATS_ERROR(ret, "ABT_pool_user_def_set_print_all");

    ABT_pool newpool;
    ret = ABT_pool_create(def, config, &newpool);
    ATS_ERROR(ret, "ABT_pool_create");
    ret = ABT_pool_user_def_free(&def);
    ATS_ERROR(ret, "ABT_pool_user_def_free");
    return newpool;
}
