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

#ifndef __CRT_SELF_TEST_H__
#define __CRT_SELF_TEST_H__

#include <crt_api.h>

#define CRT_SELF_TEST_MAX_MSG_SIZE	0x40000000

/*
 * Logic table for self-test message opcodes:
 *
 * send_size == 0 && return_size == 0
 *      opcode: CRT_OPC_SELF_TEST_BOTH_EMPTY
 *      send struct: NULL
 *      return struct: NULL
 *
 * send_size == 0 && return_size > 0
 *      opcode: CRT_OPC_SELF_TEST_SEND_EMPTY_REPLY_IOV
 *      send struct: uint32_t
 *      return struct: iovec
 *
 * send_size > 0 && return_size == 0
 *      opcode: CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY
 *      send struct: iovec payload
 *      return struct: NULL
 *
 * send_size > 0 && return_size > 0
 *      opcode: CRT_OPC_SELF_TEST_BOTH_IOV
 *      send struct: uint32_t reply_size, iovec payload
 *      return struct: iovec of specified size
 */

struct crt_st_session_params {
	uint32_t send_size;
	uint32_t reply_size;
	uint32_t num_buffers;
};

/*
 * Note that for these non-empty send structures the session_id is always
 * the first value. This allows the session to be retrieved without knowing
 * what the rest of the structure contains
 */

struct crt_st_send_id_iov {
	int32_t session_id;
	crt_iov_t buf;
};

void crt_self_test_init(void);
int crt_self_test_msg_handler(crt_rpc_t *rpc_req);
int crt_self_test_open_session_handler(crt_rpc_t *rpc_req);
int crt_self_test_close_session_handler(crt_rpc_t *rpc_req);

#endif /* __CRT_SELF_TEST_H__ */
