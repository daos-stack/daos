/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "../wire.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
	uint8_t                  frame[CRT_NATIVE_HEADER_SIZE + 3] = {0};
	struct crt_native_header h =
				     {
					 .type         = CRT_NATIVE_REQUEST,
					 .request_id   = UINT64_C(0x0102030405060708),
					 .opcode       = 0x10203040,
					 .payload_size = 3,
				     },
				 decoded;

	assert(crt_native_encode(&h, frame, sizeof(frame)) == 0);
	assert(memcmp(frame + 8, "\x01\x02\x03\x04\x05\x06\x07\x08", 8) == 0);
	assert(crt_native_decode(frame, sizeof(frame), &decoded) == 0);
	assert(decoded.request_id == h.request_id && decoded.opcode == h.opcode &&
	       decoded.type == h.type && decoded.payload_size == h.payload_size);
	for (size_t n = 0; n < sizeof(frame); n++)
		assert(crt_native_decode(frame, n, &decoded) != 0);
	frame[4]++;
	assert(crt_native_decode(frame, sizeof(frame), &decoded) == EPROTO);
	frame[4]--;
	frame[28] = 1;
	assert(crt_native_decode(frame, sizeof(frame), &decoded) == EPROTO);
	frame[28] = 0;
	h.status  = 1;
	assert(crt_native_encode(&h, frame, sizeof(frame)) == EINVAL);
	h.type = CRT_NATIVE_REPLY;
	assert(crt_native_encode(&h, frame, sizeof(frame)) == 0);
	assert(crt_native_decode(frame, sizeof(frame), &decoded) == 0 && decoded.status == 1);
	h.request_id = 0;
	assert(crt_native_encode(&h, frame, sizeof(frame)) == EINVAL);
	puts("native RPC envelope roundtrip, byte order and malformed frame tests passed");
	return 0;
}
