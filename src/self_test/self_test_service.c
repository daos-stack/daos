/* Copyright (C) 2016 Intel Corporation
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
#include "self_test.h"

/* Global shutdown flag, used to terminate the progress thread */
static int g_shutdown_flag;

int ping_handler(crt_rpc_t *rpc_req)
{
	struct st_ping_res	*res = NULL;
	struct st_ping_args	*args = NULL;
	int			ret = 0;

	args = (struct st_ping_args *)crt_req_get(rpc_req);
	if (args == NULL) {
		C_ERROR("crt_req_get failed\n");
		return -EFAULT;
	}

	res = (struct st_ping_res *)crt_reply_get(rpc_req);
	if (res == NULL) {
		C_ERROR("could not get ping reply");
		return -CER_INVAL;
	}
	/*
	 * TODO: Improve this to allow sending more than a zero-byte response
	 * Tracked in CORFSHIP-317
	 */
	crt_iov_set(&res->resp_buf, args->ping_buf.iov_buf, 0);

	ret = crt_reply_send(rpc_req);
	if (ret != 0) {
		C_ERROR("crt_reply_send failed; ret = %d\n", ret);
		return ret;
	}

	return ret;
}

int shutdown_handler(crt_rpc_t *rpc_req)
{
	int ret;

	g_shutdown_flag = 1;

	ret = crt_reply_send(rpc_req);
	if (ret != 0) {
		C_ERROR("crt_reply_send failed; ret = %d\n", ret);
		return ret;
	}

	return ret;
}

static void *progress_fn(void *arg)
{
	int			ret;
	crt_context_t		*crt_ctx = NULL;

	crt_ctx = (crt_context_t *)arg;
	C_ASSERT(crt_ctx != NULL);

	while (!g_shutdown_flag) {
		ret = crt_progress(crt_ctx, 1, NULL, NULL);
		if (ret != 0 && ret != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed; ret = %d\n", ret);
			break;
		}
	};

	pthread_exit(NULL);
}

int main(void)
{
	crt_context_t	crt_ctx = NULL;
	char		my_group[] = "self_test_service";
	pthread_t	tid;

	int		ret;
	int		cleanup_ret;

	ret = crt_init(my_group, CRT_FLAG_BIT_SERVER);
	if (ret != 0) {
		C_ERROR("crt_init failed; ret = %d\n", ret);
		goto cleanup;
	}

	ret = crt_context_create(NULL, &crt_ctx);
	if (ret != 0) {
		C_ERROR("crt_context_create failed; ret = %d\n", ret);
		goto cleanup;
	}

	/* Register RPCs */
	ret = crt_rpc_srv_register(SELF_TEST_PING, &ST_PING_FORMAT,
				   ping_handler);
	if (ret != 0) {
		C_ERROR("ping srv registration failed; ret = %d\n", ret);
		goto cleanup;
	}
	ret = crt_rpc_srv_register(SELF_TEST_SHUTDOWN, NULL, shutdown_handler);
	if (ret != 0) {
		C_ERROR("shutdown srv registration failed; ret = %d\n", ret);
		goto cleanup;
	}

	g_shutdown_flag = 0;

	ret = pthread_create(&tid, NULL, progress_fn, crt_ctx);
	if (ret != 0) {
		ret = errno;
		C_ERROR("failed to create progress thread: %s\n",
			strerror(ret));
		goto cleanup;
	}

	ret = pthread_join(tid, NULL);
	if (ret)
		C_ERROR("Could not join progress thread");

cleanup:
	cleanup_ret = crt_context_destroy(crt_ctx, 0);
	if (cleanup_ret != 0) {
		C_ERROR("crt_context_destroy failed; ret = %d\n", cleanup_ret);
		/* Make sure first error is returned, if applicable */
		ret = ((ret == 0) ? cleanup_ret : ret);
	}

	cleanup_ret = crt_finalize();
	if (cleanup_ret != 0) {
		C_ERROR("crt_finalize failed; ret = %d\n", cleanup_ret);
		/* Make sure first error is returned, if applicable */
		ret = ((ret == 0) ? cleanup_ret : ret);
	}

	return ret;

}
