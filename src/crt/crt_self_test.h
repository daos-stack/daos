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

#define CRT_SELF_TEST_MAX_MSG_SIZE (0x40000000)

/*
 * Logic table for self-test opcodes:
 *
 * send_size == 0 && return_size == 0
 *      opcode: CRT_OPC_SELF_TEST_PING_BOTH_EMPTY
 *      send struct: NULL
 *      return struct: NULL
 *
 * send_size == 0 && return_size > 0
 *      opcode: CRT_OPC_SELF_TEST_PING_SEND_EMPTY
 *      send struct: uint32_t
 *      return struct: iovec
 *
 * send_size > 0 && return_size == 0
 *      opcode: CRT_OPC_SELF_TEST_PING_REPLY_EMPTY
 *      send struct: iovec payload
 *      return struct: NULL
 *
 * send_size > 0 && return_size > 0
 *      opcode: CRT_OPC_SELF_TEST_PING_BOTH_NONEMPTY
 *      send struct: uint32_t reply_size, iovec payload
 *      return struct: iovec of specified size
 */

/* RPC arguments */
struct crt_st_ping_send_empty {
	uint32_t reply_size;
};

struct crt_st_ping_send_reply_empty {
	crt_iov_t ping_buf;
};

struct crt_st_ping_send_nonempty {
	crt_iov_t ping_buf;
	uint32_t reply_size;
};

struct crt_st_ping_reply {
	crt_iov_t resp_buf;
};

void crt_self_test_init(void);
int crt_self_test_ping_handler(crt_rpc_t *rpc_req);

#endif /* __CRT_SELF_TEST_H__ */
