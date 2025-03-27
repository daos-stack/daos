/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "abt.h"
#include "abttest.h"

typedef struct {
    const char *str;
    int code;
} error_pair_t;

int main(int argc, char *argv[])
{
    int ret, i;

    /* init and thread creation */
    ATS_read_args(argc, argv);

    error_pair_t error_pairs[] = {
        { "ABT_SUCCESS", ABT_SUCCESS },
        { "ABT_ERR_UNINITIALIZED", ABT_ERR_UNINITIALIZED },
        { "ABT_ERR_MEM", ABT_ERR_MEM },
        { "ABT_ERR_OTHER", ABT_ERR_OTHER },
        { "ABT_ERR_INV_XSTREAM", ABT_ERR_INV_XSTREAM },
        { "ABT_ERR_INV_XSTREAM_RANK", ABT_ERR_INV_XSTREAM_RANK },
        { "ABT_ERR_INV_XSTREAM_BARRIER", ABT_ERR_INV_XSTREAM_BARRIER },
        { "ABT_ERR_INV_SCHED", ABT_ERR_INV_SCHED },
        { "ABT_ERR_INV_SCHED_KIND", ABT_ERR_INV_SCHED_KIND },
        { "ABT_ERR_INV_SCHED_PREDEF", ABT_ERR_INV_SCHED_PREDEF },
        { "ABT_ERR_INV_SCHED_TYPE", ABT_ERR_INV_SCHED_TYPE },
        { "ABT_ERR_INV_SCHED_CONFIG", ABT_ERR_INV_SCHED_CONFIG },
        { "ABT_ERR_INV_POOL", ABT_ERR_INV_POOL },
        { "ABT_ERR_INV_POOL_KIND", ABT_ERR_INV_POOL_KIND },
        { "ABT_ERR_INV_POOL_ACCESS", ABT_ERR_INV_POOL_ACCESS },
        { "ABT_ERR_INV_POOL_CONFIG", ABT_ERR_INV_POOL_CONFIG },
        { "ABT_ERR_INV_POOL_USER_DEF", ABT_ERR_INV_POOL_USER_DEF },
        { "ABT_ERR_INV_UNIT", ABT_ERR_INV_UNIT },
        { "ABT_ERR_INV_THREAD", ABT_ERR_INV_THREAD },
        { "ABT_ERR_INV_THREAD_ATTR", ABT_ERR_INV_THREAD_ATTR },
        { "ABT_ERR_INV_TASK", ABT_ERR_INV_TASK },
        { "ABT_ERR_INV_KEY", ABT_ERR_INV_KEY },
        { "ABT_ERR_INV_MUTEX", ABT_ERR_INV_MUTEX },
        { "ABT_ERR_INV_MUTEX_ATTR", ABT_ERR_INV_MUTEX_ATTR },
        { "ABT_ERR_INV_COND", ABT_ERR_INV_COND },
        { "ABT_ERR_INV_RWLOCK", ABT_ERR_INV_RWLOCK },
        { "ABT_ERR_INV_EVENTUAL", ABT_ERR_INV_EVENTUAL },
        { "ABT_ERR_INV_FUTURE", ABT_ERR_INV_FUTURE },
        { "ABT_ERR_INV_BARRIER", ABT_ERR_INV_BARRIER },
        { "ABT_ERR_INV_TIMER", ABT_ERR_INV_TIMER },
        { "ABT_ERR_INV_QUERY_KIND", ABT_ERR_INV_QUERY_KIND },
        { "ABT_ERR_XSTREAM", ABT_ERR_XSTREAM },
        { "ABT_ERR_XSTREAM_STATE", ABT_ERR_XSTREAM_STATE },
        { "ABT_ERR_XSTREAM_BARRIER", ABT_ERR_XSTREAM_BARRIER },
        { "ABT_ERR_SCHED", ABT_ERR_SCHED },
        { "ABT_ERR_SCHED_CONFIG", ABT_ERR_SCHED_CONFIG },
        { "ABT_ERR_POOL", ABT_ERR_POOL },
        { "ABT_ERR_UNIT", ABT_ERR_UNIT },
        { "ABT_ERR_THREAD", ABT_ERR_THREAD },
        { "ABT_ERR_TASK", ABT_ERR_TASK },
        { "ABT_ERR_KEY", ABT_ERR_KEY },
        { "ABT_ERR_MUTEX", ABT_ERR_MUTEX },
        { "ABT_ERR_MUTEX_LOCKED", ABT_ERR_MUTEX_LOCKED },
        { "ABT_ERR_COND", ABT_ERR_COND },
        { "ABT_ERR_COND_TIMEDOUT", ABT_ERR_COND_TIMEDOUT },
        { "ABT_ERR_RWLOCK", ABT_ERR_RWLOCK },
        { "ABT_ERR_EVENTUAL", ABT_ERR_EVENTUAL },
        { "ABT_ERR_FUTURE", ABT_ERR_FUTURE },
        { "ABT_ERR_BARRIER", ABT_ERR_BARRIER },
        { "ABT_ERR_TIMER", ABT_ERR_TIMER },
        { "ABT_ERR_MIGRATION_TARGET", ABT_ERR_MIGRATION_TARGET },
        { "ABT_ERR_MIGRATION_NA", ABT_ERR_MIGRATION_NA },
        { "ABT_ERR_MISSING_JOIN", ABT_ERR_MISSING_JOIN },
        { "ABT_ERR_FEATURE_NA", ABT_ERR_FEATURE_NA },
        { "ABT_ERR_INV_TOOL_CONTEXT", ABT_ERR_INV_TOOL_CONTEXT },
        { "ABT_ERR_INV_ARG", ABT_ERR_INV_ARG },
        { "ABT_ERR_SYS", ABT_ERR_SYS },
        { "ABT_ERR_CPUID", ABT_ERR_CPUID },
    };

    for (i = 0; i < (int)(sizeof(error_pairs) / sizeof(error_pairs[0])); i++) {
        char str[256];
        ret = ABT_error_get_str(error_pairs[i].code, str, NULL);
        ATS_ERROR(ret, "ABT_error_get_str");
        assert(strcmp(error_pairs[i].str, str) == 0);
    }
    return ret;
}
