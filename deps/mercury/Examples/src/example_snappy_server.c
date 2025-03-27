/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "example_snappy.h"

#include <mercury.h>

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    const char *info_string = NULL;

    char self_addr_string[PATH_MAX];
    hg_addr_t self_addr;
    FILE *na_config = NULL;

    hg_class_t *hg_class;
    hg_context_t *hg_context;
    unsigned major;
    unsigned minor;
    unsigned patch;
    hg_return_t hg_ret;
    hg_size_t self_addr_string_size = PATH_MAX;

    HG_Version_get(&major, &minor, &patch);

    printf("Server running mercury version %u.%u.%u\n", major, minor, patch);

    /* Get info string */
    /* bmi+tcp://localhost:port */
    info_string = getenv("HG_PORT_NAME");
    if (!info_string) {
        fprintf(stderr, "HG_PORT_NAME environment variable must be set, "
                        "e.g.:\nHG_PORT_NAME=\"tcp://127.0.0.1:22222\"\n");
        exit(0);
    }

    HG_Set_log_level("warning");

    /* Initialize Mercury with the desired network abstraction class */
    hg_class = HG_Init(info_string, HG_TRUE);

    /* Get self addr to tell client about */
    HG_Addr_self(hg_class, &self_addr);
    HG_Addr_to_string(
        hg_class, self_addr_string, &self_addr_string_size, self_addr);
    HG_Addr_free(hg_class, self_addr);
    printf("Server address is: %s\n", self_addr_string);

    /* Write addr to a file */
    na_config = fopen(TEMP_DIRECTORY CONFIG_FILE_NAME, "w+");
    if (!na_config) {
        fprintf(stderr, "Could not open config file from: %s\n",
            TEMP_DIRECTORY CONFIG_FILE_NAME);
        exit(0);
    }
    fprintf(na_config, "%s\n", self_addr_string);
    fclose(na_config);

    /* Create HG context */
    hg_context = HG_Context_create(hg_class);

    /* Register RPC */
    snappy_compress_register(hg_class);

    /* Poke progress engine and check for events */
    do {
        unsigned int actual_count = 0;
        do {
            hg_ret = HG_Trigger(
                hg_context, 0 /* timeout */, 1 /* max count */, &actual_count);
        } while ((hg_ret == HG_SUCCESS) && actual_count);

        /* Do not try to make progress anymore if we're done */
        if (snappy_compress_done_target_g)
            break;

        hg_ret = HG_Progress(hg_context, HG_MAX_IDLE_TIME);

    } while (hg_ret == HG_SUCCESS);

    /* Finalize */
    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return EXIT_SUCCESS;
}
