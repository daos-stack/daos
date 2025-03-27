/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdarg.h>
#include "abt.h"
#include "abttest.h"

ABT_pool_config_var param_a = { .key = 0, .type = ABT_POOL_CONFIG_INT };
ABT_pool_config_var param_b = { .key = 1, .type = ABT_POOL_CONFIG_DOUBLE };
ABT_bool g_check_error;

void check_val(ABT_pool_config config, const int *ans_a, const double *ans_b)
{
    int ret;
    if (g_check_error || ans_a) {
        /* Check ABT_pool_config_get for param_a. */
        int val_a = 1;
        ABT_pool_config_type type = (ABT_pool_config_type)77;
        ret = ABT_pool_config_get(config, param_a.key, &type, &val_a);
        if (ans_a) {
            ATS_ERROR(ret, "ABT_pool_config_get");
            assert(val_a == *ans_a && type == param_a.type);
            ret = ABT_pool_config_get(config, param_a.key, NULL, NULL);
            ATS_ERROR(ret, "ABT_pool_config_get");
        } else {
            assert(ret != ABT_SUCCESS && val_a == 1 &&
                   type == (ABT_pool_config_type)77);
        }
    }
    if (g_check_error || ans_b) {
        /* Check ABT_pool_config_get for param_b. */
        double val_b = 1;
        ABT_pool_config_type type = (ABT_pool_config_type)77;
        ret = ABT_pool_config_get(config, param_b.key, &type, &val_b);
        if (ans_b) {
            ATS_ERROR(ret, "ABT_pool_config_get");
            assert(val_b == *ans_b && type == param_b.type);
            ret = ABT_pool_config_get(config, param_b.key, NULL, NULL);
            ATS_ERROR(ret, "ABT_pool_config_get");
        } else {
            assert(ret != ABT_SUCCESS && val_b == 1 &&
                   type == (ABT_pool_config_type)77);
        }
    }
}

int main(int argc, char *argv[])
{
    const int a = 5, a2 = 8;
    int ret, i, param_b_key;
    const double b = 3.0, b2 = 7.0;
    ABT_pool_config config;

    /* Initialize */
    ATS_read_args(argc, argv);
    ATS_init(argc, argv, 1);

    ret = ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_CHECK_ERROR,
                                (void *)&g_check_error);
    ATS_ERROR(ret, "ABT_info_query_config");

    for (param_b_key = 1; param_b_key <= 9; param_b_key++) {
        /* Change param_b.key to check the internal hash table implementation.
         */
        param_b.key = param_b_key;

        /* {x, x} */
        ret = ABT_pool_config_create(&config);
        ATS_ERROR(ret, "ABT_pool_config_create");
        check_val(config, NULL, NULL);

        /* {x, x} -> {a, x} */
        ret = ABT_pool_config_set(config, param_a.key, param_a.type, &a);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a, NULL);

        /* {a, x} -> {a2, x} */
        ret = ABT_pool_config_set(config, param_a.key, param_a.type, &a2);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a2, NULL);

        /* {a2, x} -> {a2, b} */
        ret = ABT_pool_config_set(config, param_b.key, param_b.type, &b);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a2, &b);

        /* {a2, x} -> {a2, b2} */
        ret = ABT_pool_config_set(config, param_b.key, param_b.type, &b2);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a2, &b2);

        /* {a2, b2} -> {x, b2} */
        ret = ABT_pool_config_set(config, param_a.key, param_a.type, NULL);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, NULL, &b2);

        /* {x, b2} -> {x, b2} */
        ret = ABT_pool_config_set(config, param_a.key, param_a.type, NULL);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, NULL, &b2);

        /* {x, b2} -> {a, b2} */
        ret = ABT_pool_config_set(config, param_a.key, param_a.type, &a);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a, &b2);

        /* {a, b2} -> {a, b} */
        ret = ABT_pool_config_set(config, param_b.key, param_b.type, &b);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a, &b);

        /* {a, b} -> {a, x} */
        ret = ABT_pool_config_set(config, param_b.key, param_b.type, NULL);
        ATS_ERROR(ret, "ABT_pool_config_set");
        check_val(config, &a, NULL);

        ret = ABT_pool_config_free(&config);
        ATS_ERROR(ret, "ABT_pool_config_free");

        /* {x, x} */
        ret = ABT_pool_config_create(&config);
        ATS_ERROR(ret, "ABT_pool_config_create");
        check_val(config, NULL, NULL);

        for (i = 0; i < 10; i++) {
            /* {x, x} -> {x, b} */
            ret = ABT_pool_config_set(config, param_b.key, param_b.type, &b);
            ATS_ERROR(ret, "ABT_pool_config_set");
            check_val(config, NULL, &b);

            /* {x, b} -> {x, b2} */
            ret = ABT_pool_config_set(config, param_b.key, param_b.type, &b2);
            ATS_ERROR(ret, "ABT_pool_config_set");
            check_val(config, NULL, &b2);

            /* {x, b2} -> {x, b} */
            ret = ABT_pool_config_set(config, param_b.key, param_b.type, &b);
            ATS_ERROR(ret, "ABT_pool_config_set");
            check_val(config, NULL, &b);

            /* {x, b} -> {x, x} */
            ret = ABT_pool_config_set(config, param_b.key, param_b.type, NULL);
            ATS_ERROR(ret, "ABT_pool_config_set");
            check_val(config, NULL, NULL);
        }
        ret = ABT_pool_config_free(&config);
        ATS_ERROR(ret, "ABT_pool_config_free");
    }
    /* Finalize */
    return ATS_finalize(0);
}
