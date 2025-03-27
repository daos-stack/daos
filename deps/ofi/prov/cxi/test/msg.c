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

/* Test basic send/recv - expected or unexpected*/
static void ping(bool ux)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX if not testing unexpected behavior */
	if (!ux) {
		ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL,
			      FI_ADDR_UNSPEC, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);
	}
	/* Send 64 bytes to self */
	ret = fi_send(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Post RX if testing unexpected behavior */
	if (ux) {
		/* Make sure RX progress has occurred */
		fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

		/* Post RX buffer */
		ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL,
			      FI_ADDR_UNSPEC, NULL);
	}

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}
/* Test basic send/recv with data */
static void pingdata(void)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	uint64_t data = 0xabcdabcdabcdabcd;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_senddata(cxit_ep, send_buf, send_len, NULL, data,
			  cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV | FI_REMOTE_CQ_DATA),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == data, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}


static void vping(void)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct iovec riovec;
	struct iovec siovec;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	ret = fi_recvv(cxit_ep, &riovec, NULL, 1, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	ret = fi_sendv(cxit_ep, &siovec, NULL, 1, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test basic sendmsg/recvmsg */
static void msgping(void)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec siovec;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;

	ret = fi_sendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

TestSuite(msg, .init = cxit_setup_msg, .fini = cxit_teardown_msg,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic send/recv */
Test(msg, ping)
{
	ping(false);
}

Test(msg, ping_retry)
{
	ping(true);
}

/* Test basic send/recv with data */
Test(msg, pingdata)
{
	pingdata();
}

/* Test basic inject send */
Test(msg, inject_ping)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_inject(cxit_ep, send_buf, send_len, cxit_ep_fi_addr);
	cr_assert_eq(ret, FI_SUCCESS, "fi_inject failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_MSG | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Make sure a TX event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	free(send_buf);
	free(recv_buf);
}

/* Test basic injectdata */
Test(msg, injectdata_ping)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	uint64_t data = 0xabcdabcdabcdabcd;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_injectdata(cxit_ep, send_buf, send_len, data,
			    cxit_ep_fi_addr);
	cr_assert_eq(ret, FI_SUCCESS, "fi_inject failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len,
			  FI_MSG | FI_RECV | FI_REMOTE_CQ_DATA, NULL, data, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Make sure a TX event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	free(send_buf);
	free(recv_buf);
}

/* Test basic sendv/recvv */
Test(msg, vping)
{
	vping();
}

/* Test basic sendmsg/recvmsg */
Test(msg, msgping)
{
	msgping();
}

/* Test basic sendmsg/recvmsg with two EP bound to same CQ */
Test(msg, msgping_cq_share)
{
	int i, ret;
	uint8_t *recv_buf,
		*recv_buf2,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec riovec2;
	struct iovec siovec;
	struct fid_ep *fid_ep2;
	struct cxip_addr ep2_addr;
	fi_addr_t ep2_fi_addr;
	size_t addrlen = sizeof(cxit_ep_addr);
	int num_recv_comps = 0;

	/* Create a second EP bound to the same CQs as original */
	ret = fi_endpoint(cxit_domain, cxit_fi, &fid_ep2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_endpoint");
	cr_assert_not_null(fid_ep2);

	ret = fi_ep_bind(fid_ep2, &cxit_tx_cq->fid, cxit_tx_cq_bind_flags);
	cr_assert(!ret, "fe_ep_bind TX CQ to 2nd EP");
	ret = fi_ep_bind(fid_ep2, &cxit_rx_cq->fid, cxit_rx_cq_bind_flags);
	cr_assert(!ret, "fe_ep_bind RX CQ to 2nd EP");

	ret = fi_ep_bind(fid_ep2, &cxit_av->fid, 0);
	cr_assert(!ret, "fi_ep_bind AV to 2nd EP");

	ret = fi_enable(fid_ep2);
	cr_assert(ret == FI_SUCCESS, "fi_enable of 2nd EP");

	ret = fi_getname(&fid_ep2->fid, &ep2_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "fi_getname for 2nd EP");
	cr_assert(addrlen == sizeof(ep2_addr), "addr length");

	ret = fi_av_insert(cxit_av, (void *)&ep2_addr, 1,
			   &ep2_fi_addr, 0, NULL);
	cr_assert(ret == 1);

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	recv_buf2 = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf2);
	memset(recv_buf2, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer for first EP */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Post RX buffer for second EP */
	riovec2.iov_base = recv_buf2;
	riovec2.iov_len = recv_len;
	rmsg.msg_iov = &riovec2;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	ret = fi_recvmsg(fid_ep2, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;

	ret = fi_sendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Send 64 byte message to 2nd EP */
	smsg.addr = ep2_fi_addr;
	ret = fi_sendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send to EP2 failed %d", ret);

	/* Wait for async events from single CQ bound to multiple EP
	 * to verify receive notification for each EP occurs.
	 */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		if (ret == 1) {
			/* Validate RX event fields */
			cr_assert(rx_cqe.op_context == NULL,
				  "RX CQE Context mismatch");
			cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
				  "RX CQE flags mismatch");
			cr_assert(rx_cqe.len == send_len,
				  "Invalid RX CQE length");
			cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
			cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
			cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
			cr_assert(from == cxit_ep_fi_addr,
				  "Invalid source address");
			num_recv_comps++;
		}
	} while (num_recv_comps < 2);
	cr_assert_eq(num_recv_comps, 2, "Not all completions received");

	/* Wait for async events indicating data has been sent */
	for (i = 0; i < 2; i++) {
		ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		/* Validate TX event fields */
		cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
		cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
			  "TX CQE flags mismatch");
		cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
		cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
		cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
		cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");
	}

	/* Validate sent data to each receive buffer */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(recv_buf2[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf2[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	ret = fi_close(&fid_ep2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close endpoint2");

	free(send_buf);
	free(recv_buf);
	free(recv_buf2);
}

/* Test basic sendmsg/recvmsg with data */
Test(msg, msgping_wdata)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec siovec;
	uint64_t data = 0xabcdabcdabcdabcd;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;
	smsg.data = data;

	ret = fi_sendmsg(cxit_ep, &smsg, FI_REMOTE_CQ_DATA);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV | FI_REMOTE_CQ_DATA),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == data, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test basic injectmsg */
Test(msg, inject_msgping)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec siovec;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;

	ret = fi_sendmsg(cxit_ep, &smsg, FI_INJECT);
	cr_assert_eq(ret, FI_SUCCESS, "fi_sendmsg failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_MSG | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_MSG | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test send/recv sizes small to large */
static void sizes(void)
{
	int i, j, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64*1024; /* 128k fails */
	int send_len = 64*1024;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	bool sent;
	bool recved;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	for (i = 0; i <= recv_len; i = (i ? i << 1 : 1)) {
		recved = sent = false;

		/* Post RX buffer */
		ret = fi_recv(cxit_ep, i ? recv_buf : NULL, i, NULL,
			      FI_ADDR_UNSPEC, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

		/* Send to self */
		ret = fi_send(cxit_ep, i ? send_buf : NULL, i, NULL,
			      cxit_ep_fi_addr, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

		/* Gather both events, ensure progress on both sides. */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
			if (ret == 1) {
				cr_assert_eq(recved, false);
				recved = true;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
			if (ret == 1) {
				cr_assert_eq(sent, false);
				sent = true;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}
		} while (!(sent && recved));

		/* Validate RX event fields */
		cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
		cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
			  "RX CQE flags mismatch");
		cr_assert(rx_cqe.len == i, "Invalid RX CQE length");
		cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
		cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
		cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		/* Validate TX event fields */
		cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
		cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
			  "TX CQE flags mismatch");
		cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
		cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
		cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
		cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

		/* Validate sent data */
		for (j = 0; j < i; j++) {
			cr_expect_eq(recv_buf[j], send_buf[j],
				     "data mismatch, element[%d], exp=%d saw=%d, size:%d err=%d\n",
				     j, send_buf[j], recv_buf[j], i, err++);
		}
	}

	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test send/recv sizes small to large */
Test(msg, sizes)
{
	sizes();
}

/* Test send/recv sizes large to small (this exercises MR caching) */
Test(msg, sizes_desc)
{
	int i, j, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64*1024; /* 128k fails */
	int send_len = 64*1024;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	bool sent;
	bool recved;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	for (i = recv_len; i >= 1; i >>= 1) {
		recved = sent = false;

		/* Post RX buffer */
		ret = fi_recv(cxit_ep, recv_buf, i, NULL, FI_ADDR_UNSPEC,
			      NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

		/* Send 64 bytes to self */
		ret = fi_send(cxit_ep, send_buf, i, NULL, cxit_ep_fi_addr,
			      NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

		/* Gather both events, ensure progress on both sides. */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
			if (ret == 1) {
				cr_assert_eq(recved, false);
				recved = true;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
			if (ret == 1) {
				cr_assert_eq(sent, false);
				sent = true;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}
		} while (!(sent && recved));

		/* Validate RX event fields */
		cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
		cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
			  "RX CQE flags mismatch");
		cr_assert(rx_cqe.len == i, "Invalid RX CQE length");
		cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
		cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
		cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		/* Validate TX event fields */
		cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
		cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
			  "TX CQE flags mismatch");
		cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
		cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
		cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
		cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

		/* Validate sent data */
		for (j = 0; j < i; j++) {
			cr_expect_eq(recv_buf[j], send_buf[j],
				     "data mismatch, element[%d], exp=%d saw=%d, size:%d err=%d\n",
				     j, send_buf[j], recv_buf[j], i, err++);
		}
	}

	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test software posted receives greater than hardware limits */
Test(msg, sw_max_recv, .timeout = CXIT_DEFAULT_TIMEOUT)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	fi_addr_t from;
	char *rx_mode;

	/* Test is only valid in software only matching */
	rx_mode = getenv("FI_CXI_RX_MATCH_MODE");
	if (!rx_mode || strcmp(rx_mode, "software")) {
		cr_assert(1);
		return;
	}

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	/* Only 64K buffer IDs are available */
	for (i = 0; i < 68000; i++) {
		ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL,
			      FI_ADDR_UNSPEC, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);
	}

	/* Send 64 bytes to self */
	for (i = 0; i < 68000; i++) {
		ret = fi_send(cxit_ep, send_buf, send_len, NULL,
			      cxit_ep_fi_addr, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		/* Validate RX event fields */
		cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
		cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
			  "RX CQE flags mismatch");
		cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
		cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
		cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
		cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		/* Validate TX event fields */
		cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
		cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
			  "TX CQE flags mismatch");
		cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
		cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
		cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
		cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");
	}
}

/* Test send/recv interoperability with tagged messaging */
Test(msg, tagged_interop)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	uint8_t *trecv_buf,
		*tsend_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	trecv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(trecv_buf);
	memset(trecv_buf, 0, recv_len);

	tsend_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(tsend_buf);

	for (i = 0; i < send_len; i++)
		tsend_buf[i] = i + 0xc1;

	/* Post tagged RX buffer */
	ret = fi_trecv(cxit_ep, trecv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Post RX buffer */
	ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_send(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Send 64 byte tagged message to self */
	ret = fi_tsend(cxit_ep, tsend_buf, send_len, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_MSG | FI_RECV),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_MSG | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate RX event fields */
	cr_assert(rx_cqe.op_context == NULL, "RX CQE Context mismatch");
	cr_assert(rx_cqe.flags == (FI_TAGGED | FI_RECV),
		  "RX CQE flags mismatch");
	cr_assert(rx_cqe.len == send_len, "Invalid RX CQE length");
	cr_assert(rx_cqe.buf == 0, "Invalid RX CQE address");
	cr_assert(rx_cqe.data == 0, "Invalid RX CQE data");
	cr_assert(rx_cqe.tag == 0, "Invalid RX CQE tag");
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	/* Validate TX event fields */
	cr_assert(tx_cqe.op_context == NULL, "TX CQE Context mismatch");
	cr_assert(tx_cqe.flags == (FI_TAGGED | FI_SEND),
		  "TX CQE flags mismatch");
	cr_assert(tx_cqe.len == 0, "Invalid TX CQE length");
	cr_assert(tx_cqe.buf == 0, "Invalid TX CQE address");
	cr_assert(tx_cqe.data == 0, "Invalid TX CQE data");
	cr_assert(tx_cqe.tag == 0, "Invalid TX CQE tag");

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(trecv_buf[i], tsend_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, tsend_buf[i], trecv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(tsend_buf);
	free(trecv_buf);

	free(send_buf);
	free(recv_buf);
}

#define RECV_CTX ((void *)0xabc0000000000000)
#define SEND_CTX ((void *)0xdef0000000000000)

void do_multi_recv(uint8_t *send_buf, size_t send_len,
		   uint8_t *recv_buf, size_t recv_len,
		   bool send_first, size_t sends, size_t olen)
{
	int i, j, ret;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec siovec;
	uint64_t rxe_flags;
	uint64_t txe_flags;
	size_t sent = 0;
	size_t recved = 0;
	size_t err_recved = 0;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	struct fi_cq_err_entry err_cqe = {};
	size_t recved_len = 0;
	bool dequeued = false;

	if (!sends)
		sends = recv_len / send_len;

	memset(recv_buf, 0, recv_len);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = RECV_CTX;

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = SEND_CTX;

	if (send_first) {
		for (i = 0; i < sends; i++) {
			ret = fi_sendmsg(cxit_ep, &smsg, 0);
			cr_assert_eq(ret, FI_SUCCESS,
				     "fi_sendmsg failed %d", ret);
		}

		/* Progress send to ensure it arrives unexpected */
		i = 0;
		do {
			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
			if (ret == 1) {
				sent = true;
				break;
			}
			cr_assert_eq(ret, -FI_EAGAIN,
				     "send failed %d", ret);
		} while (i++ < 100000);
	}

	ret = fi_recvmsg(cxit_ep, &rmsg, FI_MULTI_RECV);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recvmsg failed %d", ret);

	if (!send_first) {
		sleep(1);
		for (i = 0; i < sends; i++) {
			ret = fi_sendmsg(cxit_ep, &smsg, 0);
			cr_assert_eq(ret, FI_SUCCESS,
				     "fi_sendmsg failed %d", ret);
		}
	}

	/* Gather both events, ensure progress on both sides. */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		if (ret == 1) {
			rxe_flags = FI_MSG | FI_RECV;

			validate_multi_recv_rx_event(&rx_cqe, RECV_CTX,
						     send_len, rxe_flags,
						     0, 0);
			cr_assert(from == cxit_ep_fi_addr,
				  "Invalid source address");

			if (rx_cqe.flags & FI_MULTI_RECV) {
				cr_assert(!dequeued);
				dequeued = true;
			}

			recved_len = rx_cqe.len;

			/* Validate sent data */
			uint8_t *rbuf = rx_cqe.buf;

			for (j = 0; j < recved_len; j++) {
				cr_expect_eq(rbuf[j], send_buf[j],
					     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
					     j, send_buf[j], rbuf[j],
					     err++);
				cr_assert(err < 10);
			}
			cr_assert_eq(err, 0, "Data errors seen\n");

			recved++;
		} else if (ret == -FI_EAVAIL) {
			ret = fi_cq_readerr(cxit_rx_cq, &err_cqe, 0);
			cr_assert_eq(ret, 1);

			recved_len = err_cqe.len;
			uint8_t *rbuf = recv_buf + ((sends-1) * send_len);

			/* The truncated transfer is always the last, which
			 * dequeued the multi-recv buffer.
			 */
			rxe_flags = FI_MSG | FI_RECV;

			cr_assert(err_cqe.op_context == RECV_CTX,
				  "Error RX CQE Context mismatch");
			cr_assert((err_cqe.flags & ~FI_MULTI_RECV) == rxe_flags,
				  "Error RX CQE flags mismatch");
			cr_assert(err_cqe.len == send_len - olen,
				  "Invalid Error RX CQE length, got: %ld exp: %ld",
				  err_cqe.len, recv_len);
			cr_assert(err_cqe.buf == rbuf,
				  "Invalid Error RX CQE address (%p %p)",
				  err_cqe.buf, rbuf);
			cr_assert(err_cqe.data == 0,
				  "Invalid Error RX CQE data");
			cr_assert(err_cqe.tag == 0,
				  "Invalid Error RX CQE tag");
			cr_assert(err_cqe.olen == olen,
				  "Invalid Error RX CQE olen, got: %ld exp: %ld",
				  err_cqe.olen, olen);
			cr_assert(err_cqe.err == FI_ETRUNC,
				  "Invalid Error RX CQE code\n");
			cr_assert(err_cqe.prov_errno == C_RC_OK,
				  "Invalid Error RX CQE errno");
			cr_assert(err_cqe.err_data == NULL);
			cr_assert(err_cqe.err_data_size == 0);

			if (err_cqe.flags & FI_MULTI_RECV) {
				cr_assert(!dequeued);
				dequeued = true;
			}

			/* Validate sent data */
			for (j = 0; j < recved_len; j++) {
				cr_expect_eq(rbuf[j], send_buf[j],
					     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
					     j, send_buf[j], rbuf[j],
					     err++);
				cr_assert(err < 10);
			}
			cr_assert_eq(err, 0, "Data errors seen\n");

			err_recved++;
		} else {
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d",
				     ret);
		}

		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		if (ret == 1) {
			txe_flags = FI_MSG | FI_SEND;
			sent++;
			validate_tx_event(&tx_cqe, txe_flags, SEND_CTX);
		} else {
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d",
				     ret);
		}
	} while (sent < sends || (recved + err_recved) < sends);
}

struct msg_multi_recv_params {
	size_t send_len;
	size_t recv_len;
	bool ux;
	size_t sends;
	size_t olen;
};

#define SHORT_SEND_LEN 128
#define SHORT_SENDS 200
#define LONG_SEND_LEN 4096
#define LONG_SENDS 20
#define SHORT_OLEN (3*1024)
#define LONG_OLEN 1024

static struct msg_multi_recv_params params[] = {
#if 1
	/* expected/unexp eager */
	{.send_len = SHORT_SEND_LEN,
	 .recv_len = SHORT_SENDS * SHORT_SEND_LEN,
	 .ux = false},
	{.send_len = SHORT_SEND_LEN,
	 .recv_len = SHORT_SENDS * SHORT_SEND_LEN,
	 .ux = true},

	/* exp/unexp long */
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN,
	 .ux = false},
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN,
	 .ux = true},
#endif

#if 1
	/* exp/unexp overflow */
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN + (LONG_SEND_LEN - LONG_OLEN),
	 .ux = false,
	 .sends = LONG_SENDS+1,
	 .olen = LONG_OLEN},
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN + (LONG_SEND_LEN - LONG_OLEN),
	 .ux = true,
	 .sends = LONG_SENDS+1,
	 .olen = LONG_OLEN},
#endif

#if 1
	/* exp/unexp overflow */
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN + (LONG_SEND_LEN - SHORT_OLEN),
	 .ux = false,
	 .sends = LONG_SENDS+1,
	 .olen = SHORT_OLEN},
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN + (LONG_SEND_LEN - SHORT_OLEN),
	 .ux = true,
	 .sends = LONG_SENDS+1,
	 .olen = SHORT_OLEN},
#endif
};

ParameterizedTestParameters(msg, multi_recv)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct msg_multi_recv_params, params,
				   param_sz);
}

/* Test multi-recv messaging */
ParameterizedTest(struct msg_multi_recv_params *param, msg, multi_recv)
{
	void *recv_buf;
	void *send_buf;

	recv_buf = aligned_alloc(s_page_size, param->recv_len);
	cr_assert(recv_buf);

	send_buf = aligned_alloc(s_page_size, param->send_len);
	cr_assert(send_buf);

	do_multi_recv(send_buf, param->send_len, recv_buf,
		      param->recv_len, param->ux, param->sends,
		      param->olen);

	free(send_buf);
	free(recv_buf);
}

/* Test multi-recv cancel */
Test(msg, multi_recv_cancel)
{
	int i, ret;
	uint8_t *recv_buf;
	int recv_len = 0x1000;
	int recvs = 5;
	struct fi_cq_tagged_entry rx_cqe;
	struct fi_cq_err_entry err_cqe;
	struct fi_msg rmsg = {};
	struct iovec riovec;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	for (i = 0; i < recvs; i++) {
		ret = fi_recvmsg(cxit_ep, &rmsg, FI_MULTI_RECV);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);
	}

	for (i = 0; i < recvs; i++) {
		ret = fi_cancel(&cxit_ep->fid, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_cancel failed %d", ret);
	}

	for (i = 0; i < recvs; i++) {
		do {
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			if (ret == -FI_EAVAIL)
				break;

			cr_assert_eq(ret, -FI_EAGAIN,
				     "unexpected event %d", ret);
		} while (1);

		ret = fi_cq_readerr(cxit_rx_cq, &err_cqe, 0);
		cr_assert_eq(ret, 1);

		cr_assert(err_cqe.op_context == NULL,
			  "Error RX CQE Context mismatch");
		cr_assert(err_cqe.flags == (FI_MSG | FI_RECV | FI_MULTI_RECV),
			  "Error RX CQE flags mismatch");
		cr_assert(err_cqe.err == FI_ECANCELED,
			  "Invalid Error RX CQE code\n");
		cr_assert(err_cqe.prov_errno == 0,
			  "Invalid Error RX CQE errno");
	}
}

/* Test out-of-order multi-receive transaction completion */
Test(msg, multi_recv_ooo)
{
	int i, j, ret;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec siovec;
	uint64_t rxe_flags;
	int bytes_sent = 0;
	uint8_t *recv_buf;
	uint8_t *send_buf;
	size_t send_len = 8*1024;
	int sends = 10;
	size_t recv_len = send_len * 5 + 64 * 5;
	int sent = 0;
	int recved = 0;
	struct fi_cq_tagged_entry tx_cqe[sends];
	struct fi_cq_tagged_entry rx_cqe[sends];

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);
	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;

	ret = fi_recvmsg(cxit_ep, &rmsg, FI_MULTI_RECV);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	sleep(1);
	for (i = 0; i < sends; i++) {
		/* Interleave long and short sends. They will complete in a
		 * different order than they were sent or received.
		 */
		if (i % 2)
			siovec.iov_len = 64;
		else
			siovec.iov_len = 8*1024;

		ret = fi_sendmsg(cxit_ep, &smsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d",
			     ret);
	}

	for (i = 0; i < sends; i++) {
		/* Gather both events, ensure progress on both sides. */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe[recved], 1,
					     &from);
			if (ret == 1) {
				recved++;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe[sent], 1);
			if (ret == 1) {
				sent++;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}
		} while (!(sent == sends && recved == sends));
	}

	for (i = 0; i < sends; i++) {
		bytes_sent += rx_cqe[i].len;
		rxe_flags = FI_MSG | FI_RECV;
		if (bytes_sent > (recv_len - CXIP_EP_MIN_MULTI_RECV))
			rxe_flags |= FI_MULTI_RECV;

		cr_assert(rx_cqe[i].flags == rxe_flags, "CQE flags mismatch");
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		validate_tx_event(&tx_cqe[i], FI_MSG | FI_SEND, NULL);

		/* Validate sent data */
		uint8_t *rbuf = rx_cqe[i].buf;

		for (j = 0; j < rx_cqe[i].len; j++) {
			cr_expect_eq(rbuf[j], send_buf[j],
				  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				  j, send_buf[j], recv_buf[j], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");
	}

	free(send_buf);
	free(recv_buf);
}

Test(msg, fc_multi_recv, .timeout = 30)
{
	int i, j, k, ret, tx_ret;
	uint8_t *send_bufs;
	uint8_t *send_buf;
	int send_len = 64;
	uint8_t *recv_buf;
	int recv_len = 64;
	int mrecv_msgs = 10;
	struct fi_msg rmsg = {};
	struct iovec riovec;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	int nsends_concurrent = 3; /* must be less than the LE pool min. */
	int nsends = 20;
	int sends = 0;
	fi_addr_t from;

	cr_assert(!(nsends % mrecv_msgs));

	send_bufs = aligned_alloc(s_page_size, send_len * nsends_concurrent);
	cr_assert(send_bufs);

	recv_buf = aligned_alloc(s_page_size, recv_len * mrecv_msgs);
	cr_assert(recv_buf);

	for (i = 0; i < nsends_concurrent - 1; i++) {
		send_buf = send_bufs + (i % nsends_concurrent) * send_len;
		memset(send_buf, i, send_len);

		tx_ret = fi_send(cxit_ep, send_buf, send_len, NULL,
				 cxit_ep_fi_addr, NULL);
	}

	for (i = nsends_concurrent - 1; i < nsends; i++) {
		send_buf = send_bufs + (i % nsends_concurrent) * send_len;
		memset(send_buf, i, send_len);

		do {
			tx_ret = fi_send(cxit_ep, send_buf, send_len, NULL,
					 cxit_ep_fi_addr, NULL);

			/* Progress RX to avoid EQ drops */
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);

			/* Just progress */
			fi_cq_read(cxit_tx_cq, NULL, 0);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, FI_SUCCESS, "fi_tsend failed %d", tx_ret);

		do {
			tx_ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);

			/* Progress RX to avoid EQ drops */
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, 1, "fi_cq_read unexpected value %d",
			     tx_ret);

		validate_tx_event(&tx_cqe, FI_MSG | FI_SEND, NULL);

		if (!(++sends % 1000))
			printf("%u Sends complete.\n", sends);
	}

	for (i = 0; i < nsends_concurrent - 1; i++) {
		do {
			tx_ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);

			/* Progress RX to avoid EQ drops */
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, 1, "fi_cq_read unexpected value %d",
			     tx_ret);

		validate_tx_event(&tx_cqe, FI_MSG | FI_SEND, NULL);

		if (!(++sends % 1000))
			printf("%u Sends complete.\n", sends);
	}

	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len * mrecv_msgs;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	for (i = 0; i < nsends / mrecv_msgs; i++) {
		memset(recv_buf, 0, recv_len * mrecv_msgs);
		do {
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 0);
			assert(ret == FI_SUCCESS || ret == -FI_EAGAIN);

			ret = fi_recvmsg(cxit_ep, &rmsg, FI_MULTI_RECV);
			cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d",
				     ret);
		} while (ret == -FI_EAGAIN);

		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

		for (k = 0; k < mrecv_msgs; k++) {
			do {
				ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1,
						     &from);
			} while (ret == -FI_EAGAIN);

			cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d",
				     ret);

			validate_multi_recv_rx_event(&rx_cqe, NULL, recv_len,
						     FI_MSG | FI_RECV, 0, 0);
			cr_assert(from == cxit_ep_fi_addr,
				  "Invalid source address");
			bool last_msg = (k == (mrecv_msgs - 1));
			bool dequeued = rx_cqe.flags & FI_MULTI_RECV;

			cr_assert(!(last_msg ^ dequeued));

			for (j = 0; j < recv_len; j++) {
				cr_assert_eq(recv_buf[k * recv_len + j],
					     (uint8_t)i * mrecv_msgs + k,
					     "data mismatch, recv: %d,%d element[%d], exp=%d saw=%d\n",
					     i, k, j,
					     (uint8_t)i * mrecv_msgs + k,
					     recv_buf[k * recv_len + j]);
			}
		}
	}

	free(send_bufs);
	free(recv_buf);
}

static void test_fc_multi_recv(size_t xfer_len, bool progress_before_post)
{
	int ret;
	char *recv_buf;
	char *send_buf;
	int i;
	struct fi_msg rmsg = {};
	struct iovec riovec;
	unsigned int send_events = 0;
	unsigned int recv_events = 0;
	struct fi_cq_tagged_entry cqe;
	size_t min_mrecv = 0;
	size_t opt_len = sizeof(size_t);
	bool unlinked = false;

	/* Needs to exceed available LEs. */
	unsigned int num_xfers = 100;

	ret = fi_setopt(&cxit_ep->fid, FI_OPT_ENDPOINT, FI_OPT_MIN_MULTI_RECV,
			&min_mrecv, opt_len);
	cr_assert(ret == FI_SUCCESS);

	recv_buf = calloc(num_xfers, xfer_len);
	cr_assert(recv_buf);

	send_buf = calloc(num_xfers, xfer_len);
	cr_assert(send_buf);

	for (i = 0; i < (num_xfers * xfer_len); i++)
		send_buf[i] = (char)(rand() % 256);

	/* Fire off all the unexpected sends expect 1. Last send will be sent
	 * expectedly to verify that hardware has updates the manage local LE
	 * start and length fields accordingly.
	 */
	for (i = 0; i < num_xfers - 1; i++) {
		do {
			ret = fi_send(cxit_ep, &send_buf[i * xfer_len],
				      xfer_len, NULL, cxit_ep_fi_addr, NULL);
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_rx_cq, &cqe, 0);
				fi_cq_read(cxit_tx_cq, &cqe, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert(ret == FI_SUCCESS);
	}

	/* Progress before post will cause all ULEs to be onloaded before the
	 * append occurs.
	 */
	if (progress_before_post)
		fi_cq_read(cxit_rx_cq, &cqe, 0);

	/* Append late multi-recv buffer. */
	riovec.iov_base = recv_buf;
	riovec.iov_len = num_xfers * xfer_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = cxit_ep_fi_addr;
	rmsg.context = NULL;

	do {
		ret = fi_recvmsg(cxit_ep, &rmsg, FI_MULTI_RECV);
		if (ret == -FI_EAGAIN) {
			fi_cq_read(cxit_tx_cq, NULL, 0);
			fi_cq_read(cxit_rx_cq, NULL, 0);
		}
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for all send events. Since this test can be run with or without
	 * flow control, progressing the RX CQ may be required.
	 */
	while (send_events != (num_xfers - 1)) {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN || ret == 1);
		if (ret == 1)
			send_events++;

		/* Progress RXC. */
		fi_cq_read(cxit_rx_cq, &cqe, 0);
	}

	/* Wait for all receive events. */
	while (recv_events != (num_xfers - 1)) {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN || ret == 1);
		if (ret == 1 && cqe.flags & FI_RECV)
			recv_events++;
	}

	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Make last send expected. This ensures that hardware and/or software
	 * has correctly updated the LE start and length fields correctly.
	 */
	do {
		ret = fi_send(cxit_ep, &send_buf[(num_xfers - 1) * xfer_len],
			      xfer_len, NULL, cxit_ep_fi_addr, NULL);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for all send events. Since this test can be run with or without
	 * flow control, progressing the RX CQ may be required.
	 */
	while (send_events != num_xfers) {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN || ret == 1);
		if (ret == 1)
			send_events++;

		/* Progress RXC. */
		fi_cq_read(cxit_rx_cq, &cqe, 0);
	}

	/* Process the last receive event and the multi-receive event signaling
	 * the provider is no longer using the buffer.
	 */
	while (recv_events != num_xfers && !unlinked) {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN || ret == 1);
		if (ret == 1) {
			if (cqe.flags & FI_RECV)
				recv_events++;
			if (cqe.flags & FI_MULTI_RECV)
				unlinked = true;
		}
	}

	/* Data integrity check. If hardware/software mismanaged the multi-recv
	 * start and/or length fields on the expected send, data will be
	 * corrupted.
	 */
	for (i = 0; i < (num_xfers * xfer_len); i++)
		cr_assert_eq(send_buf[i], recv_buf[i],
			     "Data miscompare: byte=%u", i);

	free(send_buf);
	free(recv_buf);
}

Test(msg, fc_multi_recv_rdzv, .timeout = 10)
{
	/* Transfer size needs to be large enough to trigger rendezvous. */
	test_fc_multi_recv(16384, false);
}

Test(msg, fc_multi_recv_rdzv_onload_ules, .timeout = 10)
{
	/* Transfer size needs to be large enough to trigger rendezvous. */
	test_fc_multi_recv(16384, true);
}

Test(msg, fc_no_eq_space_expected_multi_recv, .timeout = 10)
{
	test_fc_multi_recv(1, false);
}

Test(msg, fc_no_eq_space_expected_multi_recv_onload_ules, .timeout = 10)
{
	test_fc_multi_recv(1, false);
}

static void zero_byte_send_recv_iov(void)
{
	int ret;
	struct fi_cq_tagged_entry cqe;

	ret = fi_recvv(cxit_ep, NULL, NULL, 0, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recvv failed: %d", ret);

	ret = fi_sendv(cxit_ep, NULL, NULL, 0, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_sendv failed: %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
}

Test(msg, zero_byte_send_recv_iov)
{
	zero_byte_send_recv_iov();
}

static void zero_byte_send_recv_msg(void)
{
	int ret;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};

	rmsg.addr = cxit_ep_fi_addr;

	ret = fi_recvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recvmsg failed: %d", ret);

	smsg.addr = cxit_ep_fi_addr;

	ret = fi_sendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_sendmsg failed: %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
}

Test(msg, zero_byte_send_recv_msg)
{
	zero_byte_send_recv_msg();
}

/* Verify that FI_AV_USER_ID is returned from fi_cq_readfrom(). */
Test(msg, av_user_id)
{
	int ret;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	fi_addr_t from;
	fi_addr_t user_id = 0xdeadbeef;

	/* Need to remove loopback address from AV and reinsert with
	 * FI_AV_USER_ID.
	 */
	ret = fi_av_remove(cxit_av, &cxit_ep_fi_addr, 1, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_av_remove failed: %d", ret);

	cxit_ep_fi_addr = user_id;
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, &cxit_ep_fi_addr,
			   FI_AV_USER_ID, NULL);
	cr_assert_eq(ret, 1, "fi_av_insert failed: %d", ret);

	ret = fi_recv(cxit_ep, NULL, 0, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	ret = fi_send(cxit_ep, NULL, 0, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
	cr_assert_eq(from, user_id, "Invalid user id: expected=%#lx got=%#lx",
		     user_id, from);

	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
}
/* Note: the FI_PROTO_CXI_RNR message test suite uses rnr_msg
 * so that it will not be included in flow-control and software
 * EP tests, which it does not support.
 */
TestSuite(rnr_msg, .init = cxit_setup_rnr_msg_ep,
	  .fini = cxit_teardown_msg, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rnr_msg, ping)
{
	ping(false);
}

Test(rnr_msg, ping_retry)
{
	ping(true);
}

Test(rnr_msg, ping_retry_b2b)
{
	/* unexpected, RNR retries */
	ping(true);
	ping(true);
	/*expected, no RNR retries */
	ping(false);
	/* unexpected, RNR retries */
	ping(true);
}

Test(rnr_msg, pingdata)
{
	pingdata();
}

Test(rnr_msg, vping)
{
	vping();
}

Test(rnr_msg, msgping)
{
	msgping();
}

Test(rnr_msg, sizes)
{
	sizes();
}

Test(rnr_msg, zero_byte_send_recv_iov)
{
	zero_byte_send_recv_iov();
}

Test(rnr_msg, zero_byte_send_recv_msg)
{
	zero_byte_send_recv_msg();
}
/* CS - expected messages only */
static struct msg_multi_recv_params rnr_params[] = {
	/* expected eager */
	{.send_len = SHORT_SEND_LEN,
	 .recv_len = SHORT_SENDS * SHORT_SEND_LEN,
	 .ux = false},

	/* exp long */
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN,
	 .ux = false},

	/* exp overflow */
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN + (LONG_SEND_LEN - LONG_OLEN),
	 .ux = false,
	 .sends = LONG_SENDS+1,
	 .olen = LONG_OLEN},

	/* exp overflow */
	{.send_len = LONG_SEND_LEN,
	 .recv_len = LONG_SENDS*LONG_SEND_LEN + (LONG_SEND_LEN - SHORT_OLEN),
	 .ux = false,
	 .sends = LONG_SENDS+1,
	 .olen = SHORT_OLEN},
};

ParameterizedTestParameters(rnr_msg, multi_recv)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(rnr_params);
	return cr_make_param_array(struct msg_multi_recv_params, rnr_params,
				   param_sz);
}

/* Test multi-recv messaging */
ParameterizedTest(struct msg_multi_recv_params *param, rnr_msg, multi_recv)
{
	void *recv_buf;
	void *send_buf;


	recv_buf = aligned_alloc(s_page_size, param->recv_len);
	cr_assert(recv_buf);

	send_buf = aligned_alloc(s_page_size, param->send_len);
	cr_assert(send_buf);

	do_multi_recv(send_buf, param->send_len, recv_buf,
		      param->recv_len, param->ux, param->sends,
		      param->olen);

	free(send_buf);
	free(recv_buf);
}

Test(rnr_msg, timeout)
{
	int i, ret;
	uint8_t *send_buf;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_err_entry err_cqe = {};

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Send 64 bytes to self, no receive posted */
	ret = fi_send(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, -FI_EAVAIL, "fi_cq_read unexpected status %d", ret);

	/* Read the error data */
	ret = fi_cq_readerr(cxit_tx_cq, &err_cqe, 0);
	cr_assert_eq(ret, 1);

	cr_assert(err_cqe.err == FI_EIO,
		  "Invalid Error TX CQE err %d", err_cqe.err);
	cr_assert(err_cqe.prov_errno == C_RC_ENTRY_NOT_FOUND,
		  "Invalid Error TX CQE prov_errno %d", err_cqe.prov_errno);

	free(send_buf);
}

Test(rnr_msg, rnr_cancel)
{
	int i, ret;
	uint8_t *send_buf1;
	uint8_t *send_buf2;
	uint8_t *recv_buf;
	int send_len = 64;
	struct fi_context ctxt[2];
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	struct fi_cq_err_entry err_cqe = {};
	send_buf1 = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf1);
	send_buf2 = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf2);
	recv_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(recv_buf);

	for (i = 0; i < send_len; i++) {
		send_buf1[i] = i + 0xa0;
		send_buf2[i] = i + 0x05;
	}

	/* Post two sends of 64 bytes each using a unique context */
	ret = fi_send(cxit_ep, send_buf1, send_len, NULL, cxit_ep_fi_addr,
		      &ctxt[0]);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send one failed %d", ret);

	ret = fi_send(cxit_ep, send_buf2, send_len, NULL,
		      cxit_ep_fi_addr, &ctxt[1]);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send two failed %d", ret);

	/* Cancel the first send */
	ret = fi_cancel(&cxit_ep->fid, &ctxt[0]);
	cr_assert_eq(ret, FI_SUCCESS, "Request not found %d", ret);

	/* Give time for a retry to complete */
	usleep(100);

	/* Read the canceled TX completion status */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, -FI_EAVAIL, "fi_cq_read unexpected status %d", ret);

	/* Read the error data */
	ret = fi_cq_readerr(cxit_tx_cq, &err_cqe, 0);
	cr_assert_eq(ret, 1);

	cr_assert(err_cqe.err == FI_ECANCELED,
		  "Invalid Error TX CQE err %d", err_cqe.err);
	cr_assert(err_cqe.prov_errno == C_RC_ENTRY_NOT_FOUND,
		  "Invalid Error TX CQE prov_errno %d", err_cqe.prov_errno);

	/* Post a receive, the second request should land */
	ret = fi_recv(cxit_ep, recv_buf, send_len, NULL,
		      FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Wait for async event indicating data has been sent */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected status %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected status %d", ret);

	/* Second TX data should have landed */
	cr_expect_arr_eq(recv_buf, send_buf2, send_len);

	free(send_buf1);
	free(send_buf2);
	free(recv_buf);
}

/* Test many CS retries in flight */
Test(rnr_msg, multi_recv_retries)
{
	int i, j, ret;
	int err = 0;
	fi_addr_t from;
	struct fi_msg rmsg = {};
	struct fi_msg smsg = {};
	struct iovec riovec;
	struct iovec siovec;
	uint64_t rxe_flags;
	int bytes_sent = 0;
	uint8_t *recv_buf;
	uint8_t *send_buf;
	size_t send_len = 8*1024;
	int sends = 10;
	size_t recv_len = send_len * 5 + 64 * 5;
	int sent = 0;
	int recved = 0;
	struct fi_cq_tagged_entry tx_cqe[sends];
	struct fi_cq_tagged_entry rx_cqe[sends];

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);
	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post all TX before posting a buffer, these
	 * will all go into the CS retry flow as they
	 * are unexpected.
	 */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;

	for (i = 0; i < sends; i++) {
		/* Interleave long and short sends. They will complete in a
		 * different order than they were sent or received.
		 */
		if (i % 2)
			siovec.iov_len = 64;
		else
			siovec.iov_len = 8*1024;

		ret = fi_sendmsg(cxit_ep, &smsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d",
			     ret);
	}

	/* Post an RX multi-recv buffer to receive all the sends */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.context = NULL;

	/* Force more RNR Acks and retries */
	usleep(100);
	ret = fi_cq_read(cxit_tx_cq, &tx_cqe[0], 0);
	usleep(100);

	/* Start accepting sends */
	ret = fi_recvmsg(cxit_ep, &rmsg, FI_MULTI_RECV);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	for (i = 0; i < sends; i++) {
		/* Gather both events, ensure progress on both sides. */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe[recved], 1,
					     &from);
			if (ret == 1) {
				recved++;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe[sent], 1);
			if (ret == 1) {
				sent++;
			} else {
				cr_assert_eq(ret, -FI_EAGAIN,
					     "fi_cq_read unexpected value %d",
					     ret);
			}
		} while (!(sent == sends && recved == sends));
	}

	/* All TX and RX completions have been received */

	for (i = 0; i < sends; i++) {
		bytes_sent += rx_cqe[i].len;
		rxe_flags = FI_MSG | FI_RECV;
		if (bytes_sent > (recv_len - CXIP_EP_MIN_MULTI_RECV))
			rxe_flags |= FI_MULTI_RECV;

		cr_assert(rx_cqe[i].flags == rxe_flags, "CQE flags mismatch");
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		validate_tx_event(&tx_cqe[i], FI_MSG | FI_SEND, NULL);

		/* Validate sent data */
		uint8_t *rbuf = rx_cqe[i].buf;

		for (j = 0; j < rx_cqe[i].len; j++) {
			cr_expect_eq(rbuf[j], send_buf[j],
				  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				  j, send_buf[j], recv_buf[j], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");
	}

	free(send_buf);
	free(recv_buf);
}


/* Verify that FI_AV_USER_ID is returned from fi_cq_readfrom(). */
Test(msg, av_user_id_domain_cap)
{
	int ret;
	struct fid_cq *cq;
	struct fid_av *av;
	struct fid_ep *ep;
	struct fi_cq_attr cxit_tx_cq_attr = {
		.format = FI_CQ_FORMAT_TAGGED,
	};
	struct fi_cq_tagged_entry cqe;
	fi_addr_t from;
	fi_addr_t dest_ep;
	fi_addr_t user_id = 0xdeadbeef;
	char addr[256];
	size_t addr_size = sizeof(addr);
	struct fi_av_attr av_attr = {
		.flags = FI_AV_USER_ID,
	};

	ret = fi_cq_open(cxit_domain, &cxit_tx_cq_attr, &cq, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cq_open failed: %d", ret);

	ret = fi_av_open(cxit_domain, &av_attr, &av, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_av_open failed: %d", ret);

	ret = fi_endpoint(cxit_domain, cxit_fi, &ep, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_endpoint failed: %d", ret);

	ret = fi_ep_bind(ep, &cq->fid, FI_TRANSMIT | FI_RECV);
	cr_assert_eq(ret, FI_SUCCESS, "fi_ep_bind failed: %d", ret);

	ret = fi_ep_bind(ep, &av->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_ep_bind failed: %d", ret);

	ret = fi_enable(ep);
	cr_assert_eq(ret, FI_SUCCESS, "fi_enable failed: %d", ret);

	ret = fi_getname(&ep->fid, addr, &addr_size);
	cr_assert_eq(ret, FI_SUCCESS, "fi_getname failed: %d", ret);

	ret = fi_av_insert(av, addr, 1, &dest_ep, 0, NULL);
	cr_assert_eq(ret, 1, "fi_av_insert failed: %d", ret);

	ret = fi_av_set_user_id(av, dest_ep, user_id, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_av_set_user_id failed: %d", ret);

	ret = fi_recv(ep, NULL, 0, NULL, dest_ep, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	ret = fi_send(ep, NULL, 0, NULL, dest_ep, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	do {
		ret = fi_cq_readfrom(cq, &cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	if (cqe.flags & FI_SEND) {
		do {
			ret = fi_cq_readfrom(cq, &cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
	}

	cr_assert_eq(from, user_id, "Invalid user id: expected=%#lx got=%#lx",
		     user_id, from);

	ret = fi_close(&ep->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close failed %d", ret);

	ret = fi_close(&av->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close failed %d", ret);

	ret = fi_close(&cq->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close failed %d", ret);
}

TestSuite(hybrid_preemptive, .timeout = CXIT_DEFAULT_TIMEOUT);

#define RX_SIZE 2U

Test(hybrid_preemptive, posted_recv_preemptive)
{
	int ret;
	int i;

	ret = setenv("FI_CXI_HYBRID_POSTED_RECV_PREEMPTIVE", "1", 1);
	cr_assert(ret == 0);

	ret = setenv("FI_CXI_RX_MATCH_MODE", "hybrid", 1);
	cr_assert(ret == 0);

	cxit_fi_hints = cxit_allocinfo();
	cr_assert(cxit_fi_hints);

	cxit_fi_hints->rx_attr->size = RX_SIZE;

	cxit_setup_msg();

	/* Posting more receives than RX_SIZE should cause transition to
	 * SW EP.
	 */
	for (i = 0; i < RX_SIZE + 1; i++) {
		ret = fi_recv(cxit_ep, NULL, 0, NULL, FI_ADDR_UNSPEC, NULL);

		if (i < RX_SIZE)
			cr_assert(ret == FI_SUCCESS);
		else
			cr_assert(ret == -FI_EAGAIN);
	}

	while (ret == -FI_EAGAIN) {
		fi_cq_read(cxit_rx_cq, NULL, 0);
		ret = fi_recv(cxit_ep, NULL, 0, NULL, FI_ADDR_UNSPEC, NULL);
	}

	cr_assert(ret == FI_SUCCESS);

	cxit_teardown_msg();
}

Test(hybrid_preemptive, unexpected_msg_preemptive)
{
	int ret;
	int i;
	struct cxip_ep *cxip_ep;

	ret = setenv("FI_CXI_HYBRID_UNEXPECTED_MSG_PREEMPTIVE", "1", 1);
	cr_assert(ret == 0);

	ret = setenv("FI_CXI_RX_MATCH_MODE", "hybrid", 1);
	cr_assert(ret == 0);

	cxit_fi_hints = cxit_allocinfo();
	cr_assert(cxit_fi_hints);

	cxit_fi_hints->rx_attr->size = RX_SIZE;

	cxit_setup_msg();

	cxip_ep = container_of(&cxit_ep->fid, struct cxip_ep, ep.fid);

	/* Posting more unexpected messages than RX_SIZE should cause
	 * transition to SW EP.
	 */
	for (i = 0; i < RX_SIZE + 1; i++) {
		ret = fi_send(cxit_ep, NULL, 0, NULL, cxit_ep_fi_addr, NULL);
		cr_assert(ret == FI_SUCCESS);
	}

	while (cxip_ep->ep_obj->rxc->state != RXC_ENABLED_SOFTWARE)
		fi_cq_read(cxit_rx_cq, NULL, 0);

	cr_assert(ret == FI_SUCCESS);

	cxit_teardown_msg();
}

static void msg_hybrid_mr_desc_test_runner(bool multirecv,
					   bool cq_events)
{
	struct mem_region send_window;
	struct mem_region recv_window;
	uint64_t send_key = 0x2;
	uint64_t recv_key = 0x1;
	int iters = 10;
	int send_len = 1024;
	int recv_len = multirecv ? iters * send_len + 20 : send_len;
	int recv_msg_len = send_len;
	int send_win_len = send_len * iters;
	int recv_win_len = multirecv ? recv_len : recv_len * iters;
	uint64_t recv_flags = cq_events ? FI_COMPLETION : 0;
	uint64_t send_flags = cq_events ? FI_COMPLETION | FI_TRANSMIT_COMPLETE :
					  FI_TRANSMIT_COMPLETE;
	uint64_t max_rnr_wait_us = 0;
	struct iovec riovec;
	struct iovec siovec;
	struct fi_msg msg = {};
	struct fi_cq_tagged_entry cqe;
	int ret;
	int i;
	void *send_desc[1];
	void *recv_desc[1];

	ret = mr_create(send_win_len, FI_READ | FI_WRITE, 0xa, &send_key,
			&send_window);
	cr_assert(ret == FI_SUCCESS);

	send_desc[0] = fi_mr_desc(send_window.mr);
	cr_assert(send_desc[0] != NULL);

	ret = mr_create(recv_win_len, FI_READ | FI_WRITE, 0x3, &recv_key,
			&recv_window);
	cr_assert(ret == FI_SUCCESS);
	recv_desc[0] = fi_mr_desc(recv_window.mr);
	cr_assert(recv_desc[0] != NULL);

	msg.iov_count = 1;
	msg.addr = FI_ADDR_UNSPEC;
	msg.context = NULL;
	msg.desc = recv_desc;
	msg.msg_iov = &riovec;

	/* Always pre-post receives */
	if (multirecv) {
		riovec.iov_base = recv_window.mem;
		riovec.iov_len = recv_win_len;
		recv_flags |= FI_MULTI_RECV;
		ret = fi_recvmsg(cxit_ep, &msg, recv_flags);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);
	} else {
		for (i = 0; i < iters; i++) {
			riovec.iov_base = recv_window.mem + recv_len * i;
			riovec.iov_len = recv_len;
			ret = fi_recvmsg(cxit_ep, &msg, recv_flags);
			cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);
		}
	}

	/* If not using completions to avoid internal completion
	 * set MAX RNR time to 0
	 */
	if (!cq_events) {
		ret = fi_set_val(&cxit_ep->fid,
				 FI_OPT_CXI_SET_RNR_MAX_RETRY_TIME,
				(void *) &max_rnr_wait_us);
		cr_assert(ret == FI_SUCCESS, "Set max RNR = 0 failed %d", ret);
	}

	/* Send messages */
	msg.addr = cxit_ep_fi_addr;
	msg.iov_count = 1;
	msg.context = NULL;
	msg.desc = send_desc;
	msg.msg_iov = &siovec;

	for (i = 0; i < iters; i++) {
		siovec.iov_base = send_window.mem + send_len * i;
		siovec.iov_len = send_len;
		ret = fi_sendmsg(cxit_ep, &msg, send_flags);
		cr_assert_eq(ret, FI_SUCCESS, "fi_sendmsg failed %d", ret);
	}

	/* Await Send completions or counter updates */
	if (cq_events) {
		for (i = 0; i < iters; i++) {
			ret = cxit_await_completion(cxit_tx_cq, &cqe);
			cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

			validate_tx_event(&cqe, FI_MSG | FI_SEND, NULL);
		}
	} else {
		ret = fi_cntr_wait(cxit_send_cntr, iters, 1000);
		cr_assert(ret == FI_SUCCESS);
	}

	/* Make sure only expected completions were generated */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Await Receive completions or counter updates */
	if (cq_events) {
		for (i = 0; i < iters; i++) {
			ret = cxit_await_completion(cxit_rx_cq, &cqe);
			cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

			recv_flags = FI_MSG | FI_RECV;
			if (multirecv) {
				/* We've sized so last message will unlink */
				if (i == iters - 1)
					recv_flags |= FI_MULTI_RECV;
				validate_rx_event(&cqe, NULL, recv_msg_len,
						  recv_flags,
						  recv_window.mem +
						  recv_msg_len * i, 0, 0);
			} else {
				validate_rx_event(&cqe, NULL, recv_msg_len,
						  recv_flags, NULL, 0, 0);
			}
		}
	} else {
		ret = fi_cntr_wait(cxit_recv_cntr, iters, 1000);
		cr_assert(ret == FI_SUCCESS, "Recv cntr wait returned %d", ret);

		/* With FI_MULTI_RECV, a single completion associated with
		 * the buffer un-link should be reported.
		 */
		if (multirecv) {
			ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
			cr_assert(ret == 1);
			cr_assert(cqe.flags & FI_MULTI_RECV,
				  "No FI_MULTI_RECV, flags 0x%lX", cqe.flags);
			cr_assert(!(cqe.flags & FI_RECV), "FI_RECV flag set");
			cr_assert(cqe.buf == NULL,
				  "Unexpected cqe.buf value %p", cqe.buf);
			cr_assert(cqe.len == 0,
				  "Unexpected cqe.len value %ld", cqe.len);
		}
	}

	/* Make sure only expected completions were generated */
	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	for (i = 0; i < send_win_len; i++)
		cr_assert_eq(send_window.mem[i], recv_window.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     send_window.mem[i], recv_window.mem[i]);

	mr_destroy(&send_window);
	mr_destroy(&recv_window);
}

TestSuite(rnr_msg_hybrid_mr_desc, .init = cxit_setup_rma_rnr_hybrid_mr_desc,
	  .fini = cxit_teardown_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rnr_msg_hybrid_mr_desc, non_multirecv_comp)
{
	msg_hybrid_mr_desc_test_runner(false, true);
}

Test(rnr_msg_hybrid_mr_desc, multirecv_comp)
{
	msg_hybrid_mr_desc_test_runner(true, true);
}

Test(rnr_msg_hybrid_mr_desc, non_multirecv_non_comp)
{
	msg_hybrid_mr_desc_test_runner(false, false);
}

Test(rnr_msg_hybrid_mr_desc, multirecv_non_comp)
{
	msg_hybrid_mr_desc_test_runner(true, false);
}

/* Verify non-descriptor traffic works */
Test(rnr_msg_hybrid_mr_desc, sizes_comp)
{
	uint64_t flags;
	int ret;

	/* Turn on completions notifications */
	flags = FI_SEND;
	ret = fi_control(&cxit_ep->fid, FI_GETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_GETOPSFLAG TX ret %d",
		     ret);
	flags |= FI_SEND | FI_COMPLETION | FI_TRANSMIT_COMPLETE;
	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_SETOPSFLAG TX ret %d",
		     ret);

	flags = FI_RECV;
	ret = fi_control(&cxit_ep->fid, FI_GETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_GETOPSFLAG RX ret %d",
		     ret);
	flags |= FI_RECV | FI_COMPLETION;
	ret = fi_control(&cxit_ep->fid, FI_SETOPSFLAG, (void *)&flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_control FI_SETOPSFLAG RX ret %d",
		     ret);

	sizes();
}

static void msg_hybrid_append_test_runner(bool recv_truncation,
					  bool byte_counts,
					  bool cq_events)
{
	struct cxip_ep *cxip_ep = container_of(&cxit_ep->fid, struct cxip_ep,
					       ep.fid);
	struct mem_region send_window;
	struct mem_region recv_window;
	uint64_t send_key = 0x2;
	uint64_t recv_key = 0x1;
	int iters = 10;
	int send_len = 1024;
	int recv_len = recv_truncation ? (iters - 2) * send_len :
					 iters * send_len;
	int trunc_byte_len = recv_len;
	int send_win_len = send_len * iters;
	int recv_win_len = recv_len;
	uint64_t recv_flags = cq_events ? FI_COMPLETION : 0;
	uint64_t send_flags = cq_events ? FI_COMPLETION | FI_TRANSMIT_COMPLETE :
					  FI_TRANSMIT_COMPLETE;
	uint64_t recv_cnt;
	uint64_t max_rnr_wait_us = 0;
	size_t min_multi_recv = 0;
	size_t opt_len = sizeof(size_t);
	struct iovec riovec;
	struct iovec siovec;
	struct fi_msg msg = {};
	struct fi_context ctxt[1];
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry err_cqe = {};
	int ret;
	int i;
	void *send_desc[1];
	void *recv_desc[1];

	ret = mr_create(send_win_len, FI_READ | FI_WRITE, 0xa, &send_key,
			&send_window);
	cr_assert(ret == FI_SUCCESS);

	send_desc[0] = fi_mr_desc(send_window.mr);
	cr_assert(send_desc[0] != NULL);

	ret = mr_create(recv_win_len, FI_READ | FI_WRITE, 0x3, &recv_key,
			&recv_window);
	cr_assert(ret == FI_SUCCESS);
	recv_desc[0] = fi_mr_desc(recv_window.mr);
	cr_assert(recv_desc[0] != NULL);

	/* Update min_multi_recv to ensure append buffer does not unlink */
	ret = fi_setopt(&cxit_ep->fid, FI_OPT_ENDPOINT, FI_OPT_MIN_MULTI_RECV,
			&min_multi_recv, opt_len);
	cr_assert(ret == FI_SUCCESS);

	msg.iov_count = 1;
	msg.addr = FI_ADDR_UNSPEC;
	msg.context = &ctxt[0];
	msg.desc = recv_desc;
	msg.msg_iov = &riovec;
	riovec.iov_base = recv_window.mem;
	riovec.iov_len = recv_win_len;
	recv_flags |= FI_MULTI_RECV;
	ret = fi_recvmsg(cxit_ep, &msg, recv_flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Set MAX RNR time to 0 so that message will not be retried */
	ret = fi_set_val(&cxit_ep->fid,
			 FI_OPT_CXI_SET_RNR_MAX_RETRY_TIME,
			 (void *) &max_rnr_wait_us);
	cr_assert(ret == FI_SUCCESS, "Set max RNR = 0 failed %d", ret);

	/* Send messages */
	msg.addr = cxit_ep_fi_addr;
	msg.iov_count = 1;
	msg.context = NULL;
	msg.desc = send_desc;
	msg.msg_iov = &siovec;

	for (i = 0; i < iters; i++) {
		siovec.iov_base = send_window.mem + send_len * i;
		siovec.iov_len = send_len;
		ret = fi_sendmsg(cxit_ep, &msg, send_flags);
		cr_assert_eq(ret, FI_SUCCESS, "fi_sendmsg failed %d", ret);
	}

	/* Await Send completions or counter updates */
	if (cq_events) {
		int write_len = 0;
		uint64_t flags = FI_MSG | FI_SEND;

		for (i = 0; i < iters; i++) {
			ret = cxit_await_completion(cxit_tx_cq, &cqe);
			cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

			write_len += send_len;
			if (cxip_ep->ep_obj->txc->trunc_ok) {
				if (write_len > trunc_byte_len)
					flags |= FI_CXI_TRUNC;
			}
			validate_tx_event(&cqe, flags, NULL);
		}

		/* Validate that only non-truncated counts were updated */
		ret = fi_cntr_wait(cxit_send_cntr,
				   byte_counts ? trunc_byte_len : iters, 1000);
		cr_assert(ret == FI_SUCCESS, "Bad count %ld",
			  fi_cntr_read(cxit_send_cntr));
	} else {
		ret = fi_cntr_wait(cxit_send_cntr,
				   byte_counts ? trunc_byte_len : iters, 1000);
		cr_assert(ret == FI_SUCCESS, "Bad count %ld",
			  fi_cntr_read(cxit_send_cntr));
	}

	/* Make sure only expected completions were generated */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Await Receive completions or counter updates */
	if (cq_events) {
		int received_len = 0;
		int expected_len;

		for (i = 0; i < iters; i++) {

			ret = cxit_await_completion(cxit_rx_cq, &cqe);
			cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

			recv_flags = FI_MSG | FI_RECV;

			if (trunc_byte_len - received_len >= send_len) {
				expected_len = send_len;
			} else {
				expected_len = trunc_byte_len - received_len;
				recv_flags |= FI_CXI_TRUNC;
			}

			validate_rx_event(&cqe, &ctxt[0], expected_len,
					  recv_flags,
					  recv_window.mem +
					  received_len, 0, 0);
			received_len += expected_len;
		}

		/* Validate that only bytes received were updated */
		ret = fi_cntr_wait(cxit_recv_cntr, byte_counts ?
				   trunc_byte_len : iters, 1000);
		cr_assert(ret == FI_SUCCESS, "Bad return %d count %ld",
			  ret, fi_cntr_read(cxit_recv_cntr));

	} else {
		ret = fi_cntr_wait(cxit_recv_cntr, byte_counts ?
				   trunc_byte_len : iters, 1000);
		cr_assert(ret == FI_SUCCESS, "Bad return %d count %ld",
			  ret, fi_cntr_read(cxit_recv_cntr));

		/* Verify that the truncated messages updated the success
		 * event count.
		 */
		if (recv_truncation & !byte_counts) {
			recv_cnt = fi_cntr_read(cxit_recv_cntr);
			cr_assert(recv_cnt == iters,
				  "Truncation receive count %ld is wrong",
				  recv_cnt);
		}
	}

	/* Verify no completions have been written */
	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Cancel append FI_MULIT_RECV buffer */
	ret = fi_cancel(&cxit_ep->fid, &ctxt[0]);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cancel failed %d", ret);

	/* Get cancelled entry */
	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == -FI_EAVAIL, "Did not get cancel status\n");

	ret = fi_cq_readerr(cxit_rx_cq, &err_cqe, 0);
	cr_assert_eq(ret, 1, "Did not get cancel error CQE\n");

	cr_assert(err_cqe.op_context == &ctxt[0],
		  "Error CQE coontext mismatch\n");
	cr_assert(err_cqe.flags == (FI_MSG | FI_RECV | FI_MULTI_RECV),
		  "Error CQE flags mismatch\n");
	cr_assert(err_cqe.err == FI_ECANCELED,
		  "Error CQE error code mismatch\n");
	cr_assert(err_cqe.prov_errno == 0,
		  "Error CQE provider error code mismatch\n");

	/* Make sure only expected completions were generated */
	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	for (i = 0; i < recv_win_len; i++)
		cr_assert_eq(send_window.mem[i], recv_window.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     send_window.mem[i], recv_window.mem[i]);

	mr_destroy(&send_window);
	mr_destroy(&recv_window);
}

TestSuite(rnr_msg_append_hybrid_mr_desc,
	  .init = cxit_setup_rma_rnr_hybrid_mr_desc,
	  .fini = cxit_teardown_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rnr_msg_append_hybrid_mr_desc, no_trunc_count_events_non_comp)
{
	msg_hybrid_append_test_runner(false, false, false);
}

Test(rnr_msg_append_hybrid_mr_desc, no_trunc_count_events_comp)
{
	msg_hybrid_append_test_runner(false, false, true);
}

Test(rnr_msg_append_hybrid_mr_desc, trunc_count_events_non_comp)
{
	msg_hybrid_append_test_runner(true, false, false);
}

Test(rnr_msg_append_hybrid_mr_desc, trunc_count_events_comp)
{
	struct cxip_ep *cxip_ep = container_of(&cxit_ep->fid, struct cxip_ep,
					       ep.fid);

	/* This test requires that experimental truncation a success
	 * is enabled.
	 */
	cxip_ep->ep_obj->rxc->trunc_ok = true;

	msg_hybrid_append_test_runner(true, false, true);
}

TestSuite(rnr_msg_append_hybrid_mr_desc_byte_cntr,
	  .init = cxit_setup_rma_rnr_hybrid_mr_desc_byte_cntr,
	  .fini = cxit_teardown_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rnr_msg_append_hybrid_mr_desc_byte_cntr, no_trunc_count_bytes_non_comp)
{
	msg_hybrid_append_test_runner(false, true, false);
}

Test(rnr_msg_append_hybrid_mr_desc_byte_cntr, no_trunc_count_bytes_comp)
{
	msg_hybrid_append_test_runner(false, true, true);
}

Test(rnr_msg_append_hybrid_mr_desc_byte_cntr, trunc_count_bytes_non_comp)
{
	msg_hybrid_append_test_runner(true, true, false);
}

Test(rnr_msg_append_hybrid_mr_desc_byte_cntr, trunc_count_bytes_comp)
{
	struct cxip_ep *cxip_ep = container_of(&cxit_ep->fid, struct cxip_ep,
					       ep.fid);

	/* This test requires that experimental truncation a success
	 * is enabled.
	 */
	cxip_ep->ep_obj->rxc->trunc_ok = true;

	msg_hybrid_append_test_runner(true, true, true);
}
