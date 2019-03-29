/**
 * (C) Copyright 2017-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#ifndef __IOF_FS_H__
#define __IOF_FS_H__

#include <stdbool.h>
#include <sched.h>
#include <cart/types.h>
#include "iof_atomic.h"
#include "dfuse_gah.h"

struct iof_service_group {
	crt_group_t		*dest_grp; /* Server group */
	crt_endpoint_t		psr_ep;    /* Server PSR endpoint */
	bool			enabled;   /* Indicates group is available */
};

/** Projection specific information held on the client.
 *
 * Shared between CNSS and IL.
 */
struct iof_projection {
	/** Server group info */
	struct iof_service_group	*grp;
	/** Protocol used for I/O RPCs */
	struct crt_proto_format		*io_proto;
	/** context to use */
	crt_context_t			crt_ctx;
	/** bulk threshold */
	uint32_t			max_iov_write;
	/** max write size */
	uint32_t			max_write;
	/** client projection id */
	int				cli_fs_id;
	/** Projection enabled flag */
	bool				enabled;
	/** True if there is a progress thread configured */
	bool				progress_thread;
};

/* Common data stored on open file handles */
struct iof_file_common {
	struct iof_projection	*projection;
	struct ios_gah		gah;
	crt_endpoint_t		ep;
};

/* Tracks remaining events for completion */
struct iof_tracker {
	ATOMIC int remaining;
};

/* Initialize number of events to track */
static inline void iof_tracker_init(struct iof_tracker *tracker,
				    int expected_count)
{
	atomic_store_release(&tracker->remaining, expected_count);
}

/* Signal an event */
static inline void iof_tracker_signal(struct iof_tracker *tracker)
{
	atomic_dec_release(&tracker->remaining);
}

/* Test if all events have signaled */
static inline bool iof_tracker_test(struct iof_tracker *tracker)
{
	if (atomic_load_consume(&tracker->remaining) == 0)
		return true;

	return false;
}

static inline void iof_tracker_wait(struct iof_tracker *tracker)
{
	while (!iof_tracker_test(tracker))
		sched_yield();
}

/* Progress until all events have signaled */
void iof_wait(crt_context_t, struct iof_tracker *);

/* Progress until all events have signaled */
static inline void iof_fs_wait(struct iof_projection *iof_state,
			       struct iof_tracker *tracker)
{
	/* If there is no progress thread then call progress from within
	 * this function, else just wait
	 */
	if (!iof_state->progress_thread) {
		iof_wait(iof_state->crt_ctx, tracker);
		return;
	}

	iof_tracker_wait(tracker);
}

#endif
