/*
 * Copyright (c) 2018-2021 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <pthread.h>

#include "cxip.h"
#include "cxip_test_common.h"

void *buf;

void do_tsend(size_t len)
{
	int ret;

	ret = fi_tsend(cxit_ep, buf, len, NULL, cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "ret is %d\n", ret);
}
void do_tsend_0() { do_tsend(0); }
void do_tsend_8() { do_tsend(8); }
void do_tsend_256() { do_tsend(256); }

void do_trecv(size_t len)
{
	int ret;

	ret = fi_trecv(cxit_ep, buf, len, NULL, FI_ADDR_UNSPEC, 0, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "ret is %d\n", ret);
}
void do_trecv_0() { do_trecv(0); }
void do_trecv_8() { do_trecv(8); }
void do_trecv_256() { do_trecv(256); }

void do_tsend_more(size_t len)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	struct fi_msg_tagged msg = {
		.msg_iov = &iov,
		.iov_count = 1,
		.addr = cxit_ep_fi_addr,
	};
	int ret;

	ret = fi_tsendmsg(cxit_ep, &msg, FI_MORE);
	cr_assert_eq(ret, FI_SUCCESS, "ret is %d\n", ret);
}
void do_tsend_more_8() { do_tsend_more(8); }
void do_tsend_more_256() { do_tsend_more(256); }

void do_trecv_more(size_t len)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	struct fi_msg_tagged msg = {
		.msg_iov = &iov,
		.iov_count = 1,
		.addr = FI_ADDR_UNSPEC,
	};
	int ret;

	ret = fi_trecvmsg(cxit_ep, &msg, FI_MORE);
	cr_assert_eq(ret, FI_SUCCESS, "ret is %d\n", ret);
}
void do_trecv_more_8() { do_trecv_more(8); }
void do_trecv_more_256() { do_trecv_more(256); }

TestSuite(latency, .init = cxit_setup_tagged, .fini = cxit_teardown_tagged,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

struct latency_params {
	char *api;
	void (*func)();
	bool flush_send;
};

ParameterizedTestParameters(latency, basic)
{
	size_t param_sz;

	static struct latency_params params[] = {
		{
			.api = "tsend (0-byte)",
			.func = do_tsend_0,
			.flush_send = false,
		},
		{
			.api = "trecv (0-byte)",
			.func = do_trecv_0,
			.flush_send = false,
		},
		{
			.api = "tsend (8-byte)",
			.func = do_tsend_8,
			.flush_send = false,
		},
		{
			.api = "trecv (8-byte)",
			.func = do_trecv_8,
			.flush_send = false,
		},
		{
			.api = "tsend (256-byte)",
			.func = do_tsend_256,
			.flush_send = false,
		},
		{
			.api = "trecv (256-byte)",
			.func = do_trecv_256,
			.flush_send = false,
		},
		{
			.api = "tsend_more (8b, no doorbell)",
			.func = do_tsend_more_8,
			.flush_send = true,
		},
		{
			.api = "trecv_more (8b, no doorbell)",
			.func = do_trecv_more_8,
			.flush_send = false,
		},
		{
			.api = "tsend_more (256b, no doorbell)",
			.func = do_tsend_more_256,
			.flush_send = true,
		},
		{
			.api = "trecv_more (256b, no doorbell)",
			.func = do_trecv_more_256,
			.flush_send = false,
		},
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct latency_params, params, param_sz);
}

/* Test API latency */
ParameterizedTest(struct latency_params *params, latency, basic)
{
	int warmup = 10;
	uint64_t loops = 200;
	int i;
	uint64_t start;
	uint64_t end;

	buf = malloc(0x1000);
	cr_assert(buf);

	for (i = 0; i < warmup; i++)
		params->func();

	start = ofi_gettime_ns();

	for (i = 0; i < loops; i++)
		params->func();

	end = ofi_gettime_ns();

	printf("%s latency: %lu ns\n", params->api, (end - start) / loops);

	/* Cleanup all outstanding more sends. */
	if (params->flush_send) {
		do_tsend_0();
		sleep(1);
		fi_cq_read(cxit_tx_cq, NULL, 0);
	}

	free(buf);
}
