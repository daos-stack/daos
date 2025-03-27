/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_event.h"
#include "mercury_poll.h"

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    hg_poll_set_t *poll_set;
    struct hg_poll_event events[2];
    unsigned int nevents = 0;
    bool signaled = false;
    int event_fd1, event_fd2, ret = EXIT_SUCCESS;

    poll_set = hg_poll_create();
    event_fd1 = hg_event_create();
    event_fd2 = hg_event_create();

    /* Add event descriptor */
    events[0].events = HG_POLLIN;
    events[1].events = HG_POLLIN;

    hg_poll_add(poll_set, event_fd1, &events[0]);
    hg_poll_add(poll_set, event_fd2, &events[1]);

    /* Set event */
    hg_event_set(event_fd1);

    /* Wait with timeout 0 */
    hg_poll_wait(poll_set, 0, 1, events, &nevents);
    if (nevents != 1) {
        /* We expect success */
        fprintf(stderr, "Error: should have progressed\n");
        ret = EXIT_FAILURE;
        goto done;
    }
    hg_event_get(event_fd1, &signaled);
    if (!signaled) {
        /* We expect success */
        fprintf(stderr, "Error: should have been signaled\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Reset progressed */
    nevents = 0;

    /* Wait with timeout 0 */
    hg_poll_wait(poll_set, 0, 1, events, &nevents);
    if (nevents) {
        /* We do not expect success */
        fprintf(stderr, "Error: should not have progressed (timeout 0)\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Reset progressed */
    nevents = 0;

    /* Wait with timeout */
    hg_poll_wait(poll_set, 100, 1, events, &nevents);
    if (nevents) {
        /* We do not expect success */
        fprintf(stderr, "Error: should not have progressed (timeout 100)\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Set event */
    hg_event_set(event_fd1);

    /* Reset progressed */
    nevents = 0;

    /* Wait with timeout */
    hg_poll_wait(poll_set, 1000, 1, events, &nevents);
    if (!nevents) {
        /* We expect success */
        fprintf(stderr, "Error: did not progress correctly\n");
        ret = EXIT_FAILURE;
        goto done;
    }
    hg_event_get(event_fd1, &signaled);
    if (!signaled) {
        /* We expect success */
        fprintf(stderr, "Error: should have been signaled\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Set event */
    hg_event_set(event_fd1);
    hg_event_set(event_fd2);

    /* Reset progressed */
    nevents = 0;

    /* Wait with timeout */
    hg_poll_wait(poll_set, 1000, 1, events, &nevents);
    if (nevents != 1) {
        /* We do not expect success */
        fprintf(stderr, "Error: should not have progressed first time\n");
        ret = EXIT_FAILURE;
        goto done;
    }
    hg_event_get(event_fd1, &signaled);
    if (!signaled) {
        /* We expect success */
        fprintf(stderr, "Error: should have been signaled\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Reset progressed */
    nevents = 0;

    /* Wait with timeout */
    hg_poll_wait(poll_set, 1000, 2, events, &nevents);
    if (nevents != 1) {
        /* We expect success */
        fprintf(stderr, "Error: did not progress second time\n");
        ret = EXIT_FAILURE;
        goto done;
    }
    hg_event_get(event_fd2, &signaled);
    if (!signaled) {
        /* We expect success */
        fprintf(stderr, "Error: should have been signaled\n");
        ret = EXIT_FAILURE;
        goto done;
    }

done:
    hg_poll_remove(poll_set, event_fd1);
    hg_poll_remove(poll_set, event_fd2);
    hg_poll_destroy(poll_set);
    hg_event_destroy(event_fd1);
    hg_event_destroy(event_fd2);

    return ret;
}
