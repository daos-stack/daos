/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_request.h"

#include <stdio.h>
#include <stdlib.h>

static hg_request_t *request;

static int progressed = 0;
static int triggered = 0;

static void
user_cb(void)
{
    int *user_data = (int *) hg_request_get_data(request);
    *user_data = 1;

    hg_request_complete(request);
}

static int
progress(unsigned int timeout, void *arg)
{
    /*
    printf("Doing progress\n");
    */
    (void) timeout;
    (void) arg;
    if (!progressed)
        progressed = 1;

    return HG_UTIL_SUCCESS;
}

static int
trigger(unsigned int timeout, unsigned int *flag, void *arg)
{
    /*
    printf("Calling trigger\n");
    */
    (void) timeout;
    (void) arg;
    if (progressed && !triggered) {
        user_cb();
        *flag = 1;
        triggered = 1;
    } else {
        *flag = 0;
    }

    return HG_UTIL_SUCCESS;
}

int
main(int argc, char *argv[])
{
    hg_request_class_t *request_class;
    unsigned int flag;
    unsigned int timeout = 1000; /* ms */
    int user_data = 0;
    int ret = EXIT_SUCCESS;

    (void) argc;
    (void) argv;
    request_class = hg_request_init(progress, trigger, NULL);
    request = hg_request_create(request_class);
    hg_request_set_data(request, &user_data);
    hg_request_wait(request, timeout, &flag);

    if (!user_data || !flag) {
        fprintf(stderr, "User data is %d\n", user_data);
        ret = EXIT_FAILURE;
    } else {
        /*
        printf("User data is %d\n", user_data);
        */
    }

    hg_request_destroy(request);
    hg_request_finalize(request_class, NULL);

    return ret;
}
