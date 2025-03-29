/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_init() and ABT_finalize(). */

void program(int must_succeed)
{
    int ret;

    /* ABT_get_wtime() */
    double wtime = ABT_get_wtime();
    (void)wtime;

    /* ABT_timer start-stop-read functions */
    ABT_timer timer = (ABT_timer)RAND_PTR;
    ret = ABT_timer_create(&timer);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        double t;
        ret = ABT_timer_start(timer);
        assert(ret == ABT_SUCCESS);
        ret = ABT_timer_stop(timer);
        assert(ret == ABT_SUCCESS);
        ret = ABT_timer_read(timer, &t);
        assert(ret == ABT_SUCCESS && t >= 0.0);
        ret = ABT_timer_stop_and_add(timer, &t);
        assert(ret == ABT_SUCCESS && t >= 0.0);
        ret = ABT_timer_stop_and_read(timer, &t);
        assert(ret == ABT_SUCCESS && t >= 0.0);

        ABT_timer timer2 = (ABT_timer)RAND_PTR;
        ret = ABT_timer_dup(timer, &timer2);
        assert(!must_succeed || ret == ABT_SUCCESS);
        if (ret == ABT_SUCCESS) {
            double t2;
            ret = ABT_timer_read(timer2, &t2);
            assert(ret == ABT_SUCCESS && t == t2);
            ret = ABT_timer_free(&timer2);
            assert(ret == ABT_SUCCESS && timer2 == ABT_TIMER_NULL);
        } else {
#ifdef ABT_ENABLE_VER_20_API
            assert(timer2 == (ABT_timer)RAND_PTR);
#else
            assert(timer2 == ABT_TIMER_NULL);
#endif
        }
        ret = ABT_timer_free(&timer);
        assert(ret == ABT_SUCCESS && timer == ABT_TIMER_NULL);
    } else {
#ifdef ABT_ENABLE_VER_20_API
        assert(timer == (ABT_timer)RAND_PTR);
#else
        assert(timer == ABT_TIMER_NULL);
#endif
    }

    /* ABT_timer_get_overhead() */
    double overhead = 999.9;
    ret = ABT_timer_get_overhead(&overhead);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        assert(overhead >= 0.0);
    } else {
        assert(overhead == 999.9);
    }
}

int main()
{
    setup_env();
    rtrace_init();

    if (use_rtrace()) {
        do {
            rtrace_start();
            program(0);
        } while (!rtrace_stop());
    }

    /* If no failure, it should succeed again. */
    program(1);

    rtrace_finalize();
    return 0;
}
