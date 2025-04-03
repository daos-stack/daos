/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>
#include <abt.h>
#include "rtrace.h"
#include "util.h"

/* Check ABT_init() and ABT_finalize(). */

void program(int must_succeed)
{
    int ret;
    ret = ABT_init(0, 0);
    assert(!must_succeed || ret == ABT_SUCCESS);
    if (ret == ABT_SUCCESS) {
        ret = ABT_finalize();
        /* This ABT_finalize() should not fail. */
        assert(ret == ABT_SUCCESS);
    }
}

char *legal_affinity_strs[] = { "++1", "1:2,{1:2}" };

char *illegal_affinity_strs[] = { "{}", "1:1:1:1", "{1:2:3}:2:" };

int main()
{
    int i;
    setup_env();
    rtrace_init();

    for (i = 0; i < (int)(sizeof(legal_affinity_strs) /
                          sizeof(legal_affinity_strs[0]));
         i++) {
        int ret = setenv("ABT_SET_AFFINITY", legal_affinity_strs[i], 1);
        assert(ret == 0);
        if (use_rtrace()) {
            do {
                rtrace_start();
                program(0);
            } while (!rtrace_stop());
        }
        /* If no failure, it should succeed again. */
        program(1);
    }
    /* Currently Argobots silently ignores an illegal affinity string. */
    for (i = 0; i < (int)(sizeof(illegal_affinity_strs) /
                          sizeof(illegal_affinity_strs[0]));
         i++) {
        int ret = setenv("ABT_SET_AFFINITY", illegal_affinity_strs[i], 1);
        assert(ret == 0);
        if (use_rtrace()) {
            do {
                rtrace_start();
                program(0);
            } while (!rtrace_stop());
        }
        /* If no failure, it should succeed again. */
        program(1);
    }

    rtrace_finalize();
    return 0;
}
