/* Copyright (C) 2016-2017 Intel Corporation
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
#include <pthread.h>
#include <crt_internal.h>

/* Memory to re-use every call as response data */
static char			*reply_buf;
static uint32_t			 reply_buf_len;
static pthread_spinlock_t	 reply_lock;

/* TODO: This needs to be improved with the addition of sessions */
void crt_self_test_init(void)
{
	reply_buf = NULL;
	reply_buf_len = 0;
	pthread_spin_init(&reply_lock, PTHREAD_PROCESS_PRIVATE);
}

int crt_self_test_ping_handler(crt_rpc_t *rpc_req)
{

	void				*args = NULL;
	struct crt_st_ping_reply	*res = NULL;
	uint32_t			 reply_size = 0;
	int				 ret = 0;

	C_ASSERT(rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_BOTH_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_SEND_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_REPLY_EMPTY ||
		 rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_BOTH_NONEMPTY);

	/* All types except BOTH_EMPTY have some input data */
	if (rpc_req->cr_opc != CRT_OPC_SELF_TEST_PING_BOTH_EMPTY) {
		args = crt_req_get(rpc_req);
		C_ASSERT(args != NULL);
	}

	if (rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_SEND_EMPTY ||
	    rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_BOTH_NONEMPTY) {
		res = (struct crt_st_ping_reply *)
		      crt_reply_get(rpc_req);
		C_ASSERT(res != NULL);
	}

	/*
	 * Get the reply size based on the type of message and initialize
	 * the reply iovec to zero sized, just in case memory allocation fails
	 * later
	 */
	if (rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_SEND_EMPTY) {
		struct crt_st_ping_send_empty *typed_args =
			(struct crt_st_ping_send_empty *)args;

		reply_size = typed_args->reply_size;
		crt_iov_set(&res->resp_buf, NULL, 0);
	} else if (rpc_req->cr_opc == CRT_OPC_SELF_TEST_PING_BOTH_NONEMPTY) {
		crt_iov_set(&res->resp_buf, NULL, 0);
	}

	if (reply_size > CRT_SELF_TEST_MAX_MSG_SIZE) {
		C_ERROR("Got RPC with reply size %u > maximum allowed (%u)\n",
			reply_size, CRT_SELF_TEST_MAX_MSG_SIZE);

		goto send_rpc;
	}


	if (reply_size == 0)
		goto send_rpc;

	/*
	 * Allocate additional memory only when the reply size is larger than
	 * the currently allocated buffer. If the reply size is smaller, always
	 * prefer to re-use the existing memory, even if it is much too large
	 *
	 * Note there is no mechanism to free this memory once self-test
	 * completes. It isn't really leaked - a reference is still held here,
	 * but it just sits around waiting for another self-test message.
	 */
	pthread_spin_lock(&reply_lock);
	if (reply_size > reply_buf_len) {
		char *realloced_mem = C_REALLOC(reply_buf, reply_size);

		if (realloced_mem == NULL) {
			/*
			 * Memory allocation failed, but reply_buf is
			 * still valid at this point. However, if
			 * reallocation failed, system is probably
			 * getting close to out of memory. Better to
			 * release reply_buf since we have to fail
			 * this RPC anyway
			 */
			C_FREE(reply_buf, reply_buf_len);
			reply_buf = NULL;
			reply_buf_len = 0;

			C_ERROR("self-test: error allocating %d-byte reply\n",
				reply_size);

			goto unlock;
		}

		reply_buf = realloced_mem;
		reply_buf_len = reply_size;
	}

	crt_iov_set(&res->resp_buf, reply_buf, reply_size);
unlock:
	pthread_spin_unlock(&reply_lock);

send_rpc:
	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		C_ERROR("self-test: crt_reply_send failed; ret = %d\n", ret);

	return 0;
}
