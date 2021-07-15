/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* Set array indices into my_proto_fmt_test_group arrays */
#define TEST_OPC_CHECKIN	CRT_PROTO_OPC(0x010000000, 0, 0)
#define TEST_OPC_PING_DELAY	CRT_PROTO_OPC(0x010000000, 0, 3)
#define TEST_OPC_SWIM_STATUS	CRT_PROTO_OPC(0x010000000, 0, 2)
#define TEST_OPC_SHUTDOWN	CRT_PROTO_OPC(0x010000000, 0, 1)
#define TEST_OPC_DISABLE_SWIM	CRT_PROTO_OPC(0x010000000, 0, 4)

/* input fields */
#define CRT_ISEQ_TEST_PING_DELAY				 \
	((int32_t)		(age)			CRT_VAR) \
	((int32_t)		(days)			CRT_VAR) \
	((d_string_t)		(name)			CRT_VAR) \
	((uint32_t)		(delay)			CRT_VAR)

/* output fields */
#define CRT_OSEQ_TEST_PING_DELAY				 \
	((int32_t)		(ret)			CRT_VAR) \
	((uint32_t)		(room_no)		CRT_VAR)

CRT_RPC_DECLARE(crt_test_ping_delay,
		CRT_ISEQ_TEST_PING_DELAY, CRT_OSEQ_TEST_PING_DELAY)
CRT_RPC_DEFINE(crt_test_ping_delay,
		CRT_ISEQ_TEST_PING_DELAY, CRT_OSEQ_TEST_PING_DELAY)
