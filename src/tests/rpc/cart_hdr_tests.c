/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* This file implements CaRT RPC header tests */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include "cart/crt_internal.h"

static hg_class_t *header_class;

static int
header_setup(void **state)
{
	(void)state;
	header_class = HG_Init("na+sm", HG_FALSE);
	assert_non_null(header_class);
	return 0;
}

static int
header_teardown(void **state)
{
	(void)state;
	assert_int_equal(HG_Finalize(header_class), HG_SUCCESS);
	return 0;
}

static const uint8_t request_bytes[CRT_REQ_HDR_V1_BYTES] = {
    0x01, 0x00, 0x2c, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x21, 0x22, 0x23, 0x24, 0x00, 0x00, 0x03, 0x00, 0x31, 0x32,
    0x33, 0x34, 0x51, 0x42, 0x43, 0x44, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64,
};

static hg_proc_t
header_proc(void *buffer, size_t bytes, hg_proc_op_t op)
{
	hg_proc_t proc;

	assert_int_equal(hg_proc_create_set(header_class, buffer, bytes, op, HG_NOHASH, &proc),
			 HG_SUCCESS);
	return proc;
}

static int
decode_request(void *buffer, size_t bytes, struct crt_req_hdr *hdr, size_t *used)
{
	hg_proc_t proc = header_proc(buffer, bytes, HG_DECODE);
	int       rc   = crt_proc_req_hdr(proc, hdr);

	if (used != NULL)
		*used = hg_proc_get_size_used(proc);
	assert_int_equal(hg_proc_free(proc), HG_SUCCESS);
	return rc;
}

static void
request_roundtrip(void **state)
{
	struct crt_req_hdr request = {
	    .crh_common =
		{
		    .chc_hlc   = 0x0807060504030201ULL,
		    .chc_rpcid = 0x1817161514131211ULL,
		},
	    .crh_opc               = 0x24232221,
	    .crh_flags             = CRT_RPC_FLAG_COLL | CRT_RPC_FLAG_PRIMARY_GRP,
	    .crh_src_rank          = 0x34333231,
	    .crh_dst               = {.packed = (0x444342U << 8) | 0x51U},
	    .crh_deadline_unix_sec = 0x64636261605f5e5dULL,
	};
	struct crt_req_hdr decoded = {0};
	uint8_t            buffer[CRT_REQ_HDR_V1_BYTES];
	hg_proc_t          proc = header_proc(buffer, sizeof(buffer), HG_ENCODE);
	size_t             used;

	(void)state;
	assert_int_equal(crt_proc_req_hdr(proc, &request), 0);
	assert_int_equal(hg_proc_get_size_used(proc), sizeof(buffer));
	assert_memory_equal(buffer, request_bytes, sizeof(buffer));
	assert_int_equal(hg_proc_free(proc), HG_SUCCESS);
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, &used), 0);
	assert_int_equal(used, sizeof(buffer));
	assert_int_equal(decoded.crh_common.chc_hlc, request.crh_common.chc_hlc);
	assert_int_equal(decoded.crh_common.chc_rpcid, request.crh_common.chc_rpcid);
	assert_int_equal(decoded.crh_opc, request.crh_opc);
	assert_int_equal(decoded.crh_flags, request.crh_flags);
	assert_int_equal(decoded.crh_src_rank, request.crh_src_rank);
	assert_int_equal(crt_req_dst_rank(&decoded), crt_req_dst_rank(&request));
	assert_int_equal(crt_req_dst_tag(&decoded), crt_req_dst_tag(&request));
	assert_int_equal(decoded.crh_deadline_unix_sec, request.crh_deadline_unix_sec);
}

static void
request_validation(void **state)
{
	struct crt_req_hdr decoded                          = {0};
	uint8_t            buffer[CRT_REQ_HDR_V1_BYTES + 4] = {0};
	size_t             bytes;
	size_t             used;

	(void)state;
	memcpy(buffer, request_bytes, sizeof(request_bytes));
	for (bytes = 0; bytes < sizeof(request_bytes); bytes++)
		assert_int_equal(decode_request(buffer, bytes, &decoded, NULL), -DER_PROTO);
	buffer[0] = CRT_HDR_MAJOR + 1;
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, NULL), -DER_PROTO);
	buffer[0] = CRT_HDR_MAJOR;
	buffer[2] = CRT_REQ_HDR_V1_BYTES - 4;
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, NULL), -DER_PROTO);
	buffer[2] = CRT_REQ_HDR_V1_BYTES + 4;
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, NULL), -DER_PROTO);
	memcpy(buffer, request_bytes, sizeof(request_bytes));
	buffer[1] = CRT_HDR_MINOR + 1;
	buffer[2] = CRT_REQ_HDR_V1_BYTES;
	assert_int_equal(decode_request(buffer, CRT_REQ_HDR_V1_BYTES, &decoded, &used), 0);
	assert_int_equal(used, CRT_REQ_HDR_V1_BYTES);
	memcpy(buffer, request_bytes, sizeof(request_bytes));
	buffer[1] = CRT_HDR_MINOR + 1;
	buffer[2] = CRT_REQ_HDR_V1_BYTES + 4;
	memset(buffer + CRT_REQ_HDR_V1_BYTES, 0xa5, 4);
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, &used), 0);
	assert_int_equal(used, sizeof(buffer));
	assert_int_equal(crt_req_dst_rank(&decoded), 0x444342U);
	assert_int_equal(crt_req_dst_tag(&decoded), 0x51U);
	memcpy(buffer, request_bytes, sizeof(request_bytes));
	buffer[2] = CRT_REQ_HDR_V1_BYTES + 4;
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, NULL), -DER_PROTO);
	buffer[2] = CRT_REQ_HDR_V1_BYTES + 1;
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, NULL), -DER_PROTO);
	buffer[2] = 4;
	buffer[3] = 2;
	assert_int_equal(decode_request(buffer, sizeof(buffer), &decoded, NULL), -DER_PROTO);
	memcpy(buffer, request_bytes, sizeof(request_bytes));
}

static void
reply_roundtrip(void **state)
{
	const int32_t        statuses[] = {0, -DER_TIMEDOUT, INT32_MIN, INT32_MAX, -1};
	const uint8_t        prefix[]   = {1, 0, 24, 0};
	struct crt_reply_hdr reply      = {
		 .crh_common =
                {
			 .chc_hlc   = 0x0807060504030201ULL,
			 .chc_rpcid = 0x1817161514131211ULL,
                },
        };
	struct crt_reply_hdr decoded;
	uint8_t              buffer[CRT_REPLY_HDR_V1_BYTES];
	hg_proc_t            proc;
	size_t               index;
	size_t               bytes;

	(void)state;
	for (index = 0; index < ARRAY_SIZE(statuses); index++) {
		reply.crh_status = statuses[index];
		proc             = header_proc(buffer, sizeof(buffer), HG_ENCODE);
		assert_int_equal(crt_proc_reply_hdr(proc, &reply), 0);
		assert_int_equal(hg_proc_get_size_used(proc), sizeof(buffer));
		assert_memory_equal(buffer, prefix, sizeof(prefix));
		assert_memory_equal(buffer + 4, request_bytes + 4, 16);
		for (bytes = 0; bytes < 4; bytes++)
			assert_int_equal(buffer[20 + bytes],
					 (uint8_t)((uint32_t)statuses[index] >> (bytes * 8)));
		assert_int_equal(hg_proc_free(proc), HG_SUCCESS);
		proc = header_proc(buffer, sizeof(buffer), HG_DECODE);
		assert_int_equal(crt_proc_reply_hdr(proc, &decoded), 0);
		assert_int_equal(decoded.crh_status, statuses[index]);
		assert_int_equal(decoded.crh_common.chc_hlc, reply.crh_common.chc_hlc);
		assert_int_equal(decoded.crh_common.chc_rpcid, reply.crh_common.chc_rpcid);
		assert_int_equal(hg_proc_free(proc), HG_SUCCESS);
	}
	for (bytes = 0; bytes < sizeof(buffer); bytes++) {
		proc = header_proc(buffer, bytes, HG_DECODE);
		assert_int_equal(crt_proc_reply_hdr(proc, &decoded), -DER_PROTO);
		assert_int_equal(hg_proc_free(proc), HG_SUCCESS);
	}
}

static void
deadline_conversion(void **state)
{
	uint64_t now_sec = crt_realtime_sec();

	(void)state;
	assert_int_equal(crt_deadline_to_timeout(0), -1);
	assert_int_equal(crt_deadline_to_timeout(now_sec - 1), -1);
	assert_int_equal(crt_deadline_to_timeout(UINT64_MAX), UINT32_MAX);
	assert_true(crt_timeout_to_deadline(2) >= now_sec + 3);
}

static void
destination_rank_tag(void **state)
{
	struct crt_req_hdr hdr = {0};

	(void)state;
	assert_int_equal(crt_req_dst_set(&hdr, 0x00ffffffU, 0xffU), 0);
	assert_int_equal(crt_req_dst_rank(&hdr), 0x00ffffffU);
	assert_int_equal(crt_req_dst_tag(&hdr), 0xffU);
	assert_int_equal(crt_req_dst_set(&hdr, 0x01000000U, 0), -DER_INVAL);
	assert_int_equal(crt_req_dst_set(&hdr, 0, 0x100U), -DER_INVAL);
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(request_roundtrip),    cmocka_unit_test(request_validation),
	    cmocka_unit_test(reply_roundtrip),      cmocka_unit_test(deadline_conversion),
	    cmocka_unit_test(destination_rank_tag),
	};

	return cmocka_run_group_tests(tests, header_setup, header_teardown);
}
