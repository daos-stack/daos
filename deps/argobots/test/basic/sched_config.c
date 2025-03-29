/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

/* The purpose of this test is to check that ABT_sched_config_create,
 * ABT_sched_config_read, and ABT_sched_config_free behave as we want.
 */

#include <stdarg.h>
#include "abt.h"
#include "abttest.h"

ABT_sched_config_var param_a = { .idx = 0, .type = ABT_SCHED_CONFIG_INT };
ABT_sched_config_var param_b = { .idx = 1, .type = ABT_SCHED_CONFIG_DOUBLE };
ABT_bool g_check_error;

void check_val(ABT_sched_config config, const int *ans_a, const double *ans_b)
{
    int ret;
    int val_a = 1;
    double val_b = 2.0;
    /* Check ABT_sched_config_read. */
    if (param_b.idx <= 4) {
        if (param_b.idx == 1) {
            ret = ABT_sched_config_read(config, 2, &val_a, &val_b);
        } else if (param_b.idx == 2) {
            ret = ABT_sched_config_read(config, 3, &val_a, NULL, &val_b);
        } else if (param_b.idx == 3) {
            ret = ABT_sched_config_read(config, 4, &val_a, NULL, NULL, &val_b);
        } else if (param_b.idx == 4) {
            ret = ABT_sched_config_read(config, 5, &val_a, NULL, NULL, NULL,
                                        &val_b);
        } else {
            ret = ABT_ERR_OTHER;
        }
        ATS_ERROR(ret, "ABT_sched_config_read");
        if (ans_a) {
            assert(val_a == *ans_a);
        } else {
            assert(val_a == 1);
        }
        if (ans_b) {
            assert(val_b == *ans_b);
        } else {
            assert(val_b == 2.0);
        }
    }
    if (g_check_error || ans_a) {
        /* Check ABT_sched_config_get for param_a. */
        val_a = 1;
        ABT_sched_config_type type = (ABT_sched_config_type)77;
        ret = ABT_sched_config_get(config, param_a.idx, &type, &val_a);
        if (ans_a) {
            ATS_ERROR(ret, "ABT_sched_config_get");
            assert(val_a == *ans_a && type == param_a.type);
            ret = ABT_sched_config_get(config, param_a.idx, NULL, NULL);
            ATS_ERROR(ret, "ABT_sched_config_get");
        } else {
            assert(ret != ABT_SUCCESS && val_a == 1 &&
                   type == (ABT_sched_config_type)77);
        }
    }
    if (g_check_error || ans_b) {
        /* Check ABT_sched_config_get for param_b. */
        val_b = 1;
        ABT_sched_config_type type = (ABT_sched_config_type)77;
        ret = ABT_sched_config_get(config, param_b.idx, &type, &val_b);
        if (ans_b) {
            ATS_ERROR(ret, "ABT_sched_config_get");
            assert(val_b == *ans_b && type == param_b.type);
            ret = ABT_sched_config_get(config, param_b.idx, NULL, NULL);
            ATS_ERROR(ret, "ABT_sched_config_get");
        } else {
            assert(ret != ABT_SUCCESS && val_b == 1 &&
                   type == (ABT_sched_config_type)77);
        }
    }
}

int main(int argc, char *argv[])
{
    const int a = 5, a2 = 8;
    int ret, i, param_b_idx;
    const double b = 3.0, b2 = 7.0;
    ABT_sched_config config;

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 1);

    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR,
                                (void *)&g_check_error);
    ATS_ERROR(ret, "ABT_info_query_config");

    for (param_b_idx = 1; param_b_idx <= 9; param_b_idx++) {
        /* Change param_b.idx to check the internal hash table implementation.
         */
        param_b.idx = param_b_idx;

        /* {a, x} */
        ret = ABT_sched_config_create(&config, param_a, a,
                                      ABT_sched_config_var_end);
        ATS_ERROR(ret, "ABT_sched_config_create");
        check_val(config, &a, NULL);

        /* {a, x} -> {a2, x} */
        ret = ABT_sched_config_set(config, param_a.idx, param_a.type, &a2);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, &a2, NULL);

        /* {a2, x} -> {a2, b} */
        ret = ABT_sched_config_set(config, param_b.idx, param_b.type, &b);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, &a2, &b);

        /* {a2, x} -> {a2, b2} */
        ret = ABT_sched_config_set(config, param_b.idx, param_b.type, &b2);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, &a2, &b2);

        /* {a2, b2} -> {x, b2} */
        ret = ABT_sched_config_set(config, param_a.idx, param_a.type, NULL);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, NULL, &b2);

        /* {x, b2} -> {x, b2} */
        ret = ABT_sched_config_set(config, param_a.idx, param_a.type, NULL);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, NULL, &b2);

        /* {x, b2} -> {a, b2} */
        ret = ABT_sched_config_set(config, param_a.idx, param_a.type, &a);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, &a, &b2);

        /* {a, b2} -> {a, b} */
        ret = ABT_sched_config_set(config, param_b.idx, param_b.type, &b);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, &a, &b);

        /* {a, b} -> {a, x} */
        ret = ABT_sched_config_set(config, param_b.idx, param_b.type, NULL);
        ATS_ERROR(ret, "ABT_sched_config_set");
        check_val(config, &a, NULL);

        ret = ABT_sched_config_free(&config);
        ATS_ERROR(ret, "ABT_sched_config_free");

        /* {x, b} */
        ret = ABT_sched_config_create(&config, param_b, b,
                                      ABT_sched_config_var_end);
        ATS_ERROR(ret, "ABT_sched_config_create");
        check_val(config, NULL, &b);

        for (i = 0; i < 10; i++) {
            /* {x, b} -> {x, b} */
            ret = ABT_sched_config_set(config, param_b.idx, param_b.type, &b);
            ATS_ERROR(ret, "ABT_sched_config_set");
            check_val(config, NULL, &b);

            /* {x, b} -> {x, b2} */
            ret = ABT_sched_config_set(config, param_b.idx, param_b.type, &b2);
            ATS_ERROR(ret, "ABT_sched_config_set");
            check_val(config, NULL, &b2);

            /* {x, b2} -> {x, b} */
            ret = ABT_sched_config_set(config, param_b.idx, param_b.type, &b);
            ATS_ERROR(ret, "ABT_sched_config_set");
            check_val(config, NULL, &b);

            /* {x, b} -> {x, x} */
            ret = ABT_sched_config_set(config, param_b.idx, param_b.type, NULL);
            ATS_ERROR(ret, "ABT_sched_config_set");
            check_val(config, NULL, NULL);
        }
        ret = ABT_sched_config_free(&config);
        ATS_ERROR(ret, "ABT_sched_config_free");

        /* {a, b} */
        ret = ABT_sched_config_create(&config, param_a, a, param_b, b,
                                      ABT_sched_config_var_end);
        ATS_ERROR(ret, "ABT_sched_config_create");
        check_val(config, &a, &b);
        ret = ABT_sched_config_free(&config);
        ATS_ERROR(ret, "ABT_sched_config_free");

        /* {a, b} */
        ret = ABT_sched_config_create(&config, param_b, b, param_a, a,
                                      ABT_sched_config_var_end);
        ATS_ERROR(ret, "ABT_sched_config_create");
        check_val(config, &a, &b);
        ret = ABT_sched_config_free(&config);
        ATS_ERROR(ret, "ABT_sched_config_free");
    }
    /* Finalize */
    return ATS_finalize(0);
}
