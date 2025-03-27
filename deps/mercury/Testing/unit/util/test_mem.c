/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_mem.h"

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    size_t page_size;
    void *ptr;

    page_size = (size_t) hg_mem_get_page_size();
    if (page_size == 0) {
        fprintf(stderr, "Error: page size is 0\n");
        goto error;
    }

    ptr = hg_mem_aligned_alloc(page_size, page_size * 4);
    if (ptr == NULL) {
        fprintf(stderr, "Error: could not allocate %zu bytes\n", page_size * 4);
        goto error;
    }
    hg_mem_aligned_free(ptr);

    page_size = (size_t) hg_mem_get_hugepage_size();
    if (page_size == 0) {
        fprintf(stderr, "Warning: hugepage size is 0\n");
    } else {
        ptr = hg_mem_huge_alloc(page_size * 4);
        if (ptr != NULL)
            (void) hg_mem_huge_free(ptr, page_size * 4);
    }

    return EXIT_SUCCESS;

error:
    return EXIT_FAILURE;
}
