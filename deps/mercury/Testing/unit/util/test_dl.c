/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_test_util_config.h"

#include "mercury_dl.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HG_TEST_UTIL_MODULE_PREFIX "libhg_test_dl_module"

static int
hg_test_util_dl_filter(const struct dirent *entry)
{
    return !strncmp(entry->d_name, HG_TEST_UTIL_MODULE_PREFIX,
        strlen(HG_TEST_UTIL_MODULE_PREFIX));
}

int
main(void)
{
    char module_path[256];
    struct dirent **module_list;
    HG_DL_HANDLE handle = NULL;
    int *val_p;
    int (*func)(void);
    int n, rc;

    n = scandir(HG_TEST_UTIL_OUTPUT_DIRECTORY, &module_list,
        hg_test_util_dl_filter, alphasort);
    if (n < 1) {
        fprintf(stderr, "Error: could not find module\n");
        goto error;
    }

    rc = snprintf(module_path, sizeof(module_path), "%s/%s",
        HG_TEST_UTIL_OUTPUT_DIRECTORY, module_list[0]->d_name);

    while (n--)
        free(module_list[n]);
    free(module_list);

    if (rc < 0 || rc > (int) sizeof(module_path)) {
        fprintf(stderr, "Error: path truncated\n");
        goto error;
    }

    handle = hg_dl_open(module_path);
    if (handle == NULL) {
        fprintf(stderr, "Error: handle is NULL for path %s\n", module_path);
        goto error;
    }

    val_p = (int *) hg_dl_sym(handle, "hg_test_dl_module_var_g");
    if (val_p == NULL) {
        fprintf(stderr, "Error: could not lookup symbol\n");
        goto error;
    }
    if (*val_p != 1) {
        fprintf(stderr, "Error: invalid value: %d\n", *val_p);
        goto error;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    func = (int (*)(void)) hg_dl_sym(handle, "hg_test_dl_module_func");
#pragma GCC diagnostic pop
    if (func == NULL) {
        fprintf(stderr, "Error: could not lookup symbol\n");
        goto error;
    }
    if (func() != 1) {
        fprintf(stderr, "Error: invalid value: %d\n", func());
        goto error;
    }

    if (hg_dl_close(handle) != HG_UTIL_SUCCESS) {
        fprintf(stderr, "Error: could not close handle\n");
        goto error;
    }

    return EXIT_SUCCESS;

error:
    return EXIT_FAILURE;
}
