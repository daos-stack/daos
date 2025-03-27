/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <abt.h>
#include <assert.h>
#include <stdlib.h>

#define RAND_PTR ((void *)(intptr_t)0x12345678)
static void setup_env(void)
{
    /* The following speeds up ABT_init(). */
    int ret;
    ret = setenv("ABT_MEM_MAX_NUM_DESCS", "4", 1);
    assert(ret == 0);
    ret = setenv("ABT_MEM_MAX_NUM_STACKS", "4", 1);
    assert(ret == 0);
}

static int use_rtrace(void)
{
    int ret;
#ifndef ABT_ENABLE_VER_20_API
    ret = ABT_init(0, NULL);
    assert(ret == ABT_SUCCESS);
#endif
    ABT_bool lazy_stack_alloc;
    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC,
                                (void *)&lazy_stack_alloc);
    assert(ret == ABT_SUCCESS);
#ifndef ABT_ENABLE_VER_20_API
    ret = ABT_finalize();
    assert(ret == ABT_SUCCESS);
#endif
    /* Currently the lazy stack allocation mechanism does not handle all the
     * memory leak cases properly. */
    return lazy_stack_alloc ? 0 : 1;
}
#endif /* UTIL_H_INCLUDED */
