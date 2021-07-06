/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Common code for threaded_client/threaded_server testing multiple threads
 * using a single context
 */
#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <gurt/common.h>

static inline int drain_queue(crt_context_t ctx)
{
	int	rc;
	/* Drain the queue. Progress until 1 second timeout.  We need
	 * a more robust method
	 */
	do {
		rc = crt_progress(ctx, 1000000);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			printf("crt_progress failed rc: %d.\n", rc);
			return rc;
		}

		if (rc == -DER_TIMEDOUT)
			break;
	} while (1);

	printf("Done draining queue\n");
	return 0;
}

#endif /* __COMMON_H__ */
