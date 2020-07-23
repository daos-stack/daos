/*
 * (C) Copyright 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
