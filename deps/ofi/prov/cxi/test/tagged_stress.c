/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <pthread.h>

#include "cxip.h"
#include "cxip_test_common.h"

#define RECV_INIT 0x77
#define SEND_INIT ~RECV_INIT

TestSuite(tagged_stress, .init = cxit_setup_tagged,
	  .fini = cxit_teardown_tagged,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

static void do_msg(uint8_t *send_buf, size_t send_len, uint64_t send_tag,
	    uint8_t *recv_buf, size_t recv_len, uint64_t recv_tag,
	    uint64_t recv_ignore, bool send_first, size_t buf_size,
	    bool tagged, size_t ntrans)
{
	int i, j, ret;
	int err = 0;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	fi_addr_t from;
	int sent = false;
	int recved = false;
	struct fi_cq_err_entry err_cqe = {};

	memset(recv_buf, RECV_INIT, send_len * ntrans);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	if (send_first) {
		for (j = 0; j < ntrans; j++) {
			/* Send 64 bytes to self */
			if (tagged) {
				ret = fi_tsend(cxit_ep, send_buf, send_len,
					       NULL, cxit_ep_fi_addr, send_tag,
					       NULL);
				cr_assert_eq(ret, FI_SUCCESS,
					     "fi_tsend failed %d",
					     ret);
			} else {
				ret = fi_send(cxit_ep, send_buf, send_len,
					      NULL, cxit_ep_fi_addr, NULL);
				cr_assert_eq(ret, FI_SUCCESS,
					     "fi_send failed %d",
					     ret);
			}

			/* Progress send to ensure it arrives unexpected */
			i = 0;
			do {
				ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
				if (ret == 1) {
					sent++;
					break;
				}
				cr_assert_eq(ret, -FI_EAGAIN,
					     "send failed %d", ret);
			} while (i++ < 10000);
		}
	}

	/* Post RX buffer */

	for (j = 0; j < ntrans; j++) {
		if (tagged) {
			ret = fi_trecv(cxit_ep, recv_buf + j * send_len,
				       recv_len, NULL,
				       FI_ADDR_UNSPEC, recv_tag, recv_ignore,
				       NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d",
				     ret);
		} else {
			ret = fi_recv(cxit_ep, recv_buf + j * send_len,
				      recv_len, NULL,
				      FI_ADDR_UNSPEC, NULL);
			cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d",
				     ret);
		}
	}

	if (!send_first) {
		for (j = 0; j < ntrans; j++) {
			if (tagged) {
				ret = fi_tsend(cxit_ep, send_buf, send_len,
					       NULL, cxit_ep_fi_addr, send_tag,
					       NULL);
				cr_assert_eq(ret, FI_SUCCESS,
					     "fi_tsend failed %d",
					     ret);
			} else {
				ret = fi_send(cxit_ep, send_buf, send_len,
					      NULL,
					      cxit_ep_fi_addr, NULL);
				cr_assert_eq(ret, FI_SUCCESS,
					     "fi_send failed %d",
					     ret);
			}
		}
	}

	/* Gather both events, ensure progress on both sides. */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		if (ret == 1) {
			cr_assert_lt(recved, ntrans);
			recved++;
		} else if (ret == -FI_EAVAIL) {
			cr_assert_lt(recved, ntrans);

			ret = fi_cq_readerr(cxit_rx_cq, &err_cqe, 0);
			cr_assert_eq(ret, 1);
			recved++;
		} else {
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);
		}

		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		if (ret == 1) {
			cr_assert_lt(sent, ntrans);
			sent++;
		} else {
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);
		}
	} while (sent < ntrans || recved < ntrans);

	for (i = 0; i < ntrans; i++) {
		for (j = 0; j < send_len; j++) {
			uint8_t *r = recv_buf + i * send_len;

			cr_expect_eq(r[j], send_buf[j],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     j, send_buf[j], r[j], err++);
		}
		cr_assert_eq(err, 0, "trans[%d] Data errors seen\n", i);
	}
}

#define BUF_SIZE (128*1024)
#define SEND_MIN 64
#define TAG 0x333333333333

struct tagged_rx_params {
	size_t buf_size;
	size_t send_min;
	uint64_t send_tag;
	int recv_len_off;
	uint64_t recv_tag;
	uint64_t ignore;
	bool ux;
	bool tagged;
	size_t ntrans;
};

static struct tagged_rx_params params[] = {
	{.buf_size = BUF_SIZE, /* equal length */
	 .send_min = SEND_MIN,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = true,
	 .ntrans = 200},
	{.buf_size = BUF_SIZE, /* equal length UX */
	 .send_min = SEND_MIN,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = true,
	 .ntrans = 200},
};

ParameterizedTestParameters(tagged_stress, rx)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct tagged_rx_params, params,
				   param_sz);
}

ParameterizedTest(struct tagged_rx_params *param, tagged_stress, rx,
		  .timeout = 60*10, .disabled = true)
{
	uint8_t *recv_buf,
		*send_buf;
	size_t send_len;

	recv_buf = aligned_alloc(s_page_size, param->buf_size * param->ntrans);
	cr_assert(recv_buf);

	send_buf = aligned_alloc(s_page_size, param->buf_size * param->ntrans);
	cr_assert(send_buf);

	for (send_len = param->send_min;
	     send_len <= param->buf_size;
	     send_len <<= 1) {
		do_msg(send_buf, send_len, param->send_tag,
		       recv_buf, send_len + param->recv_len_off,
		       param->recv_tag, param->ignore, param->ux,
		       param->buf_size, param->tagged, param->ntrans);
		printf("send_len: %ld completed\n", send_len);
	}

	free(send_buf);
	free(recv_buf);
}
