/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../../crt_internal.h"
#include "../cart_proc.h"
#include <assert.h>
#include <stdio.h>

static int
request_header(crt_proc_t proc, void *arg)
{
	return crt_proc_req_hdr(proc, arg);
}
static int
reply_header(crt_proc_t proc, void *arg)
{
	return crt_proc_reply_hdr(proc, arg);
}

int
main(void)
{
	unsigned char      buffer[1024] = {0};
	struct crt_req_hdr request      = {.crh_common   = {.chc_hlc   = UINT64_C(0x0102030405060708),
							    .chc_rpcid = UINT64_C(0x1020304050607080)},
					   .crh_opc      = 0x23000001,
					   .crh_flags    = CRT_RPC_FLAG_COLL,
					   .crh_src_rank = 0x123456,
					   .crh_deadline_unix_sec = 1800000000},
			   decoded      = {0};
	size_t size, used;
	assert(crt_req_dst_set(&request, 0xabcdef, 0x42) == 0);
	assert(crt_native_cart_run(request_header, &request, CRT_PROC_ENCODE, buffer,
				   sizeof(buffer), NULL, NULL, &size) == 0);
	assert(size == CRT_NATIVE_PROC_HEADER + sizeof(request));
	unsigned char *header = buffer + CRT_NATIVE_PROC_HEADER;
	assert(header[0] == CRT_HDR_MAJOR && header[1] == CRT_HDR_MINOR && header[2] == 0 &&
	       header[3] == sizeof(request));
	assert(memcmp(header + 4, "\x01\x02\x03\x04\x05\x06\x07\x08", 8) == 0);
	assert(memcmp(header + 32, "\xab\xcd\xef\x42", 4) == 0);
	assert(crt_native_cart_run(request_header, &decoded, CRT_PROC_DECODE, buffer, size, NULL,
				   NULL, &used) == 0 &&
	       used == size);
	assert(memcmp(&request, &decoded, sizeof(request)) == 0);
	for (size_t n = 0; n < size; n++)
		assert(crt_native_cart_run(request_header, &decoded, CRT_PROC_DECODE, buffer, n,
					   NULL, NULL, &used) != 0);
	header[0]++;
	assert(crt_native_cart_run(request_header, &decoded, CRT_PROC_DECODE, buffer, size, NULL,
				   NULL, &used) == -DER_PROTO);
	header[0]--;
	/* Only a newer minor version may append unknown, bounded header fields. */
	header[3] = sizeof(request) + 4;
	assert(crt_native_cart_run(request_header, &decoded, CRT_PROC_DECODE, buffer, size + 4,
				   NULL, NULL, &used) == -DER_PROTO);
	header[1]++;
	assert(crt_native_cart_run(request_header, &decoded, CRT_PROC_DECODE, buffer, size + 4,
				   NULL, NULL, &used) == 0 &&
	       used == size + 4);
	assert(crt_native_cart_run(request_header, &decoded, CRT_PROC_DECODE, buffer, size, NULL,
				   NULL, &used) == -DER_PROTO);
	struct crt_reply_hdr reply = {.crh_common = request.crh_common, .crh_status = -DER_NOREPLY},
			     reply_decoded = {0};
	assert(crt_native_cart_run(reply_header, &reply, CRT_PROC_ENCODE, buffer, sizeof(buffer),
				   NULL, NULL, &size) == 0);
	assert(size == CRT_NATIVE_PROC_HEADER + sizeof(reply));
	assert(crt_native_cart_run(reply_header, &reply_decoded, CRT_PROC_DECODE, buffer, size,
				   NULL, NULL, &used) == 0);
	assert(memcmp(&reply, &reply_decoded, sizeof(reply)) == 0);
	assert(crt_req_dst_set(&request, 0x1000000, 0) == -DER_INVAL);
	assert(crt_req_dst_set(&request, 0, 256) == -DER_INVAL);
	puts("Native CaRT headers: canonical bytes, packed routing, request/reply round trips, "
	     "truncation and version checks passed");
	return 0;
}
