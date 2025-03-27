/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_atomic_queue.h"

#include <stdio.h>
#include <stdlib.h>

struct my_entry {
    int value;
};

#define HG_TEST_QUEUE_SIZE 16

int
main(void)
{
    struct hg_atomic_queue *hg_atomic_queue;
    int ret = EXIT_SUCCESS;
    int value1 = 10, value2 = 20;
    struct my_entry my_entry1 = {.value = value1};
    struct my_entry my_entry2 = {.value = value2};
    struct my_entry *my_entry_ptr;

    hg_atomic_queue = hg_atomic_queue_alloc(HG_TEST_QUEUE_SIZE);
    if (!hg_atomic_queue) {
        fprintf(stderr, "Error: could not allocate queue\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    hg_atomic_queue_push(hg_atomic_queue, &my_entry1);
    hg_atomic_queue_push(hg_atomic_queue, &my_entry2);

    my_entry_ptr = hg_atomic_queue_pop_sc(hg_atomic_queue);
    if (my_entry_ptr == NULL) {
        fprintf(stderr, "NULL entry");
        ret = EXIT_FAILURE;
        goto done;
    }
    if (value1 != my_entry_ptr->value) {
        fprintf(stderr, "Error: values do not match, expected %d, got %d\n",
            value1, my_entry_ptr->value);
        ret = EXIT_FAILURE;
        goto done;
    }

    my_entry_ptr = hg_atomic_queue_pop_mc(hg_atomic_queue);
    if (my_entry_ptr == NULL) {
        fprintf(stderr, "NULL entry");
        ret = EXIT_FAILURE;
        goto done;
    }
    if (value2 != my_entry_ptr->value) {
        fprintf(stderr, "Error: values do not match, expected %d, got %d\n",
            value2, my_entry_ptr->value);
        ret = EXIT_FAILURE;
        goto done;
    }

done:
    hg_atomic_queue_free(hg_atomic_queue);
    return ret;
}
