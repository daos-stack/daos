/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define TEST_OPC_CHECKIN	CRT_PROTO_OPC(0x010000000, 0, 0)
#define TEST_OPC_PING_DELAY	CRT_PROTO_OPC(0x010000000, 0, 2)
#define TEST_OPC_SHUTDOWN	CRT_PROTO_OPC(0x010000000, 0, 1)


#define CRT_ISEQ_TEST_PING_DELAY /* input fields */		 \
	((int32_t)		(age)			CRT_VAR) \
	((int32_t)		(days)			CRT_VAR) \
	((d_string_t)		(name)			CRT_VAR) \
	((uint32_t)		(delay)			CRT_VAR)

#define CRT_OSEQ_TEST_PING_DELAY /* output fields */		 \
	((int32_t)		(ret)			CRT_VAR) \
	((uint32_t)		(room_no)		CRT_VAR)

CRT_RPC_DECLARE(crt_test_ping_delay,
		CRT_ISEQ_TEST_PING_DELAY, CRT_OSEQ_TEST_PING_DELAY)
CRT_RPC_DEFINE(crt_test_ping_delay,
		CRT_ISEQ_TEST_PING_DELAY, CRT_OSEQ_TEST_PING_DELAY)
