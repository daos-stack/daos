/* Copyright (C) 2017-2018 Intel Corporation
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
#include <cart/api.h>
#include "log.h"
#include "iof_fs.h"

static int iof_check_complete(void *arg)
{
	struct iof_tracker *tracker = arg;

	return iof_tracker_test(tracker);
}

/* Progress until all callbacks are invoked */
void iof_wait(crt_context_t crt_ctx, struct iof_tracker *tracker)
{
	int			rc;

	for (;;) {
		rc = crt_progress(crt_ctx, 1000 * 1000, iof_check_complete,
				  tracker);

		if (iof_tracker_test(tracker))
			return;

		/* TODO: Determine the best course of action on error.  In an
		 * audit of cart code, it seems like this would only happen
		 * under somewhat catostrophic circumstances.
		 */
		if (rc != 0 && rc != -DER_TIMEDOUT)
			IOF_LOG_ERROR("crt_progress failed rc: %d", rc);
	}
}

struct attach_info {
	struct iof_tracker	tracker;
	int			rc;
};

static void iof_lm_attach_cb(const struct crt_lm_attach_cb_info *cb_info)
{
	struct attach_info	*attach_info = cb_info->lac_arg;

	attach_info->rc = cb_info->lac_rc;
	iof_tracker_signal(&attach_info->tracker);
}

int iof_lm_attach(crt_group_t *group, crt_context_t crt_ctx)
{
	struct attach_info	attach_info;
	int			ret;

	iof_tracker_init(&attach_info.tracker, 1);
	ret = crt_lm_attach(group, iof_lm_attach_cb, &attach_info);
	if (ret) {
		IOF_LOG_ERROR("crt_lm_attach failed with ret = %d", ret);
		return ret;
	}

	/* If crt_ctx != NULL, another progress thread is required */
	if (crt_ctx)
		iof_wait(crt_ctx, &attach_info.tracker);
	else
		iof_tracker_wait(&attach_info.tracker);

	if (attach_info.rc != 0)
		IOF_LOG_ERROR("crt_lm_attach failed with ret = %d",
			      attach_info.rc);

	return attach_info.rc;
}

