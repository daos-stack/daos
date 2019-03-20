/* Copyright (C) 2016-2018 Intel Corporation
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

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <gurt/dlog.h>
#include <gurt/common.h>
#include "ctrl_common.h"

bool ctrl_info_init(struct ctrl_info *ctrl_info)
{
	int rc;

	memset(ctrl_info, 0, sizeof(*ctrl_info));
	rc = D_MUTEX_INIT(&ctrl_info->lock, NULL);
	if (rc != -DER_SUCCESS)
		return false;
	rc = pthread_cond_init(&ctrl_info->cond, NULL);
	if (rc != 0) {
		pthread_mutex_destroy(&ctrl_info->lock);
		return false;
	}
	return true;
}

void wait_for_shutdown(struct ctrl_info *ctrl_info)
{
	D_MUTEX_LOCK(&ctrl_info->lock);
	while (!ctrl_info->shutting_down)
		pthread_cond_wait(&ctrl_info->cond, &ctrl_info->lock);
	D_MUTEX_UNLOCK(&ctrl_info->lock);
	IOF_LOG_INFO("Shutdown signal received");
}

static int iof_uint_read(char *buf, size_t buflen, void *arg)
{
	uint *value = (uint *)arg;

	snprintf(buf, buflen, "%u", *value);
	return 0;
}

static uint64_t shutdown_read_cb(void *arg)
{
	struct ctrl_info *ctrl_info = (struct ctrl_info *)arg;

	return ctrl_info->shutting_down;
}

static int shutdown_write_cb(uint64_t value, void *arg)
{
	struct ctrl_info *ctrl_info = (struct ctrl_info *)arg;

	if (value != 1)
		return EINVAL;

	/* If a shutdown has already been triggered then reject future
	 * requests
	 */
	if (ctrl_info->shutting_down)
		return EINVAL;

	if (!ctrl_info->shutting_down) {
		IOF_LOG_INFO("Shutting down");
		ctrl_fs_disable(); /* disables new opens on ctrl files */
		D_MUTEX_LOCK(&ctrl_info->lock);
		ctrl_info->shutting_down = 1;
		pthread_cond_signal(&ctrl_info->cond);
		D_MUTEX_UNLOCK(&ctrl_info->lock);
	}

	return 0;
}

static int write_log_write_cb(const char *buf, void *arg)
{
	/* Printing as %s in order to prevent interpreting buf symbols*/
	d_log(D_LOGFAC | DLOG_INFO, "%s\n", buf);

	return 0;
}

static int dump_log_write_cb(const char *buf, void *arg)
{
	/* Printing as %s in order to prevent interpreting buf symbols*/
	d_log(D_LOGFAC | DLOG_INFO, "%s\n", buf);

	return cnss_dump_log(arg);
}

#define MAX_MASK_LEN 256
static int log_mask_cb(const char *mask,  void *cb_arg)
{
	char newmask[MAX_MASK_LEN];
	char *pos;
	size_t len;

	if (strcmp(mask, "\n") == 0 || strlen(mask) == 0) {
		IOF_LOG_INFO("No log mask specified, resetting to ERR");
		strcpy(newmask, "ERR");
	} else {
		/* strip '\n' */
		pos = strchr(mask, '\n');
		if (pos != NULL)
			len = ((uintptr_t)pos - (uintptr_t)mask);
		else
			len = strlen(mask);

		if (len > MAX_MASK_LEN - 1)
			len = MAX_MASK_LEN - 1;

		strncpy(newmask, mask, len);
		newmask[len] = 0;

		IOF_LOG_INFO("Setting log mask to %s", newmask);
	}

	d_log_setmasks(newmask, strlen(newmask));

	return 0;
}

#define CHECK_RET(ret, label, msg)		         \
	do {					         \
		if (ret != 0) {			         \
			IOF_LOG_ERROR(msg);	         \
			shutdown_write_cb(1, ctrl_info); \
			goto label;		         \
		}				         \
	} while (0)

int register_cnss_controls(struct ctrl_info *ctrl_info)
{
	char *crt_protocol;
	int ret = 0;

	ret = ctrl_register_variable(NULL, "active", iof_uint_read,
				     NULL, NULL, &ctrl_info->active);
	CHECK_RET(ret, exit, "Could not register 'active' ctrl");

	ret = ctrl_register_uint64_variable(NULL, "shutdown",
					    shutdown_read_cb,
					    shutdown_write_cb,
					    (void *)ctrl_info);
	CHECK_RET(ret, exit, "Could not register shutdown ctrl");

	ret = ctrl_register_variable(NULL, "dump_log",
				     NULL /* read_cb */,
				     dump_log_write_cb, /* write_cb */
				     NULL, /* destroy_cb */
				     (void *)ctrl_info);
	CHECK_RET(ret, exit, "Could not register dump_log ctrl");

	ret = ctrl_register_variable(NULL, "write_log",
				     NULL /* read_cb */,
				     write_log_write_cb, /* write_cb */
				     NULL, /* destroy_cb */
				     NULL);
	CHECK_RET(ret, exit, "Could not register write_log ctrl");

	ret = ctrl_register_variable(NULL, "log_mask",
				     NULL /* read_cb */,
				     log_mask_cb /* write_cb */,
				     NULL /* destroy_cb */, NULL);
	CHECK_RET(ret, exit, "Could not register log_mask ctrl");

	ret = ctrl_register_constant_int64(NULL, "cnss_id", getpid());
	CHECK_RET(ret, exit, "Could not register cnss_id");

	crt_protocol = getenv("CRT_PHY_ADDR_STR");
	if (crt_protocol) { /* Only register if set */
		ret = ctrl_register_constant(NULL, "crt_protocol",
					     crt_protocol);
		CHECK_RET(ret, exit, "Could not register crt_protocol");
	}

exit:
	return ret;
}
