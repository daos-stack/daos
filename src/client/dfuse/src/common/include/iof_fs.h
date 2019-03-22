/* Copyright (C) 2017-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __IOF_FS_H__
#define __IOF_FS_H__

#include <stdbool.h>
#include <uuid/uuid.h> /* Required by cart/types.h.  See CART-279 */
#include <sched.h>
#include <cart/types.h>
#include <iof_atomic.h>
#include <ios_gah.h>

struct iof_service_group {
	crt_group_t		*dest_grp; /* Server group */
	crt_endpoint_t		psr_ep;    /* Server PSR endpoint */
	ATOMIC uint32_t		pri_srv_rank;  /* Primary Service Rank */
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

int iof_lm_attach(crt_group_t *group, crt_context_t crt_ctx);

#endif
