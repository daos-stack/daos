/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_hash_table.h"

#include <stdio.h>
#include <stdlib.h>

static int
int_equal(hg_hash_table_key_t vlocation1, hg_hash_table_key_t vlocation2)
{
    return *((int *) vlocation1) == *((int *) vlocation2);
}

static unsigned int
int_hash(hg_hash_table_key_t vlocation)
{
    return *((unsigned int *) vlocation);
}

static void
int_hash_key_free(hg_hash_table_key_t key)
{
    free((int *) key);
}

static void
int_hash_value_free(hg_hash_table_value_t value)
{
    free((int *) value);
}

/*---------------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
    hg_hash_table_t *hash_table = NULL;
    hg_hash_table_iter_t hash_table_iter;

    int *key1, *key2;
    int *value1, *value2;
    int ret = EXIT_SUCCESS;

    (void) argc;
    (void) argv;

    hash_table = hg_hash_table_new(int_hash, int_equal);
    hg_hash_table_register_free_functions(
        hash_table, int_hash_key_free, int_hash_value_free);

    key1 = (int *) malloc(sizeof(int));
    if (key1 == NULL) {
        ret = EXIT_FAILURE;
        goto done;
    }

    key2 = (int *) malloc(sizeof(int));
    if (key2 == NULL) {
        free(key1);
        ret = EXIT_FAILURE;
        goto done;
    }

    value1 = (int *) malloc(sizeof(int));
    if (value1 == NULL) {
        free(key1);
        free(key2);
        ret = EXIT_FAILURE;
        goto done;
    }

    value2 = (int *) malloc(sizeof(int));
    if (value2 == NULL) {
        free(key1);
        free(key2);
        free(value1);
        ret = EXIT_FAILURE;
        goto done;
    }

    *key1 = 1;
    *key2 = 2;

    *value1 = 10;
    *value2 = 20;

    hg_hash_table_insert(hash_table, key1, value1);
    hg_hash_table_insert(hash_table, key2, value2);

    if (2 != hg_hash_table_num_entries(hash_table)) {
        fprintf(stderr, "Error: was expecting 2 entries, got %u\n",
            hg_hash_table_num_entries(hash_table));
        ret = EXIT_FAILURE;
        goto done;
    }

    if (*value1 != *((int *) hg_hash_table_lookup(hash_table, key1))) {
        fprintf(stderr, "Error: values do not match\n");
        ret = EXIT_FAILURE;
        goto done;
    }
    hg_hash_table_remove(hash_table, key1);

    if (1 != hg_hash_table_num_entries(hash_table)) {
        fprintf(stderr, "Error: was expecting 1 entry, got %u\n",
            hg_hash_table_num_entries(hash_table));
        ret = EXIT_FAILURE;
        goto done;
    }

    hg_hash_table_iterate(hash_table, &hash_table_iter);
    if (!hg_hash_table_iter_has_more(&hash_table_iter)) {
        fprintf(stderr, "Error: there should be more values\n");
        ret = EXIT_FAILURE;
        goto done;
    }
    if (*value2 != *((int *) hg_hash_table_iter_next(&hash_table_iter))) {
        fprintf(stderr, "Error: values do not match\n");
        ret = EXIT_FAILURE;
        goto done;
    }

done:
    hg_hash_table_free(hash_table);
    return ret;
}
