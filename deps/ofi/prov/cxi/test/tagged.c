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

TestSuite(tagged, .init = cxit_setup_tagged, .fini = cxit_teardown_tagged,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

static void ping(void)
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
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Try invalid lengths */
	ret = fi_tsend(cxit_ep, send_buf, cxit_fi->ep_attr->max_msg_size+1,
		       NULL, cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, -FI_EMSGSIZE, "fi_tsend failed %d", ret);

	free(send_buf);
	free(recv_buf);
}

/* Test basic send/recv w/data */
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
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_tsenddata(cxit_ep, send_buf, send_len, NULL, data,
			   cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsenddata failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len,
			  FI_TAGGED | FI_RECV | FI_REMOTE_CQ_DATA,
			  NULL, data, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

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
/* Test basic sendv/recvv */
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
	struct iovec siovec;
	struct iovec riovec;

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
	ret = fi_trecvv(cxit_ep, &riovec, NULL, 1, FI_ADDR_UNSPEC, 0, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvv failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	ret = fi_tsendv(cxit_ep, &siovec, NULL, 1, cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendv failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

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
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
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
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	ret = fi_tsendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

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

/* Test basic send/recv */
Test(tagged, ping)
{
	ping();
}

/* Test basic zero-byte send/recv */
Test(tagged, zbr)
{
	int ret;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	fi_addr_t from;

	ret = fi_trecv(cxit_ep, NULL, 0, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	ret = fi_tsend(cxit_ep, NULL, 0, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, 0, FI_TAGGED | FI_RECV, NULL, 0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Try an unexpected send */
	ret = fi_tsend(cxit_ep, NULL, 0, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	sleep(1);

	ret = fi_trecv(cxit_ep, NULL, 0, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, 0, FI_TAGGED | FI_RECV, NULL, 0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
}

static void simple_rdzv(bool check_invalid_length)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 8192;
	int send_len = 8192;
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
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Send 8192 bytes to self */
	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	if (check_invalid_length) {
		ret = fi_tsend(cxit_ep, send_buf,
			       cxit_fi->ep_attr->max_msg_size+1,
			       NULL, cxit_ep_fi_addr, 0, NULL);
		cr_assert_eq(ret, -FI_EMSGSIZE, "fi_tsend failed %d", ret);
	}

	free(send_buf);
	free(recv_buf);
}

/* Test basic rendezvous send */
Test(tagged, rdzv)
{
	simple_rdzv(true);
}

/* Verify unrestricted non-eager rendezvous get is used if requested */
Test(tagged, alt_read_rdzv)
{
	char *rdzv_proto;
	uint64_t end_pkt_cnt;
	uint64_t start_pkt_cnt;
	int ret;

	/* If not testing alt_read protocol skip */
	rdzv_proto = getenv("FI_CXI_RDZV_PROTO");
	if (!rdzv_proto || strcmp(rdzv_proto, "alt_read")) {
		cr_assert(1);
		return;
	}

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &start_pkt_cnt, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	simple_rdzv(false);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &end_pkt_cnt, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	/* Some number of non-eager data restricted get packets need
	 * have been sent.
	 */
	cr_assert(end_pkt_cnt > start_pkt_cnt,
		  "Incorrect number of restricted packets");
}

Test(tagged, zero_byte_tsend_trecv_iov)
{
	int ret;
	struct fi_cq_tagged_entry cqe;

	ret = fi_trecvv(cxit_ep, NULL, NULL, 0, cxit_ep_fi_addr, 0, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvv failed: %d", ret);

	ret = fi_tsendv(cxit_ep, NULL, NULL, 0, cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendv failed: %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
}

Test(tagged, zero_byte_tsend_trecv_msg)
{
	int ret;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};

	rmsg.addr = cxit_ep_fi_addr;

	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed: %d", ret);

	smsg.addr = cxit_ep_fi_addr;

	ret = fi_tsendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed: %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
}

#if ENABLE_DEBUG
/* Verify fallback to default rendezvous proto on H/W resource failure */
Test(tagged, fail_alt_read_rdzv)
{
	char *rdzv_proto;
	uint64_t end_pkt_cnt;
	uint64_t start_pkt_cnt;
	int ret;
	struct cxip_ep *ep = container_of(&cxit_ep->fid,
					  struct cxip_ep, ep.fid);
	struct cxip_txc_hpc *txc = container_of(ep->ep_obj->txc,
						struct cxip_txc_hpc, base);

	/* If not testing alt_read protocol skip */
	rdzv_proto = getenv("FI_CXI_RDZV_PROTO");
	if (!rdzv_proto || strcmp(rdzv_proto, "alt_read")) {
		cr_assert(1);
		return;
	}

	/* Force error on allocation of hardware resources required
	 * by alt_read rendezvous protocol.
	 */
	txc->force_err |= CXIP_TXC_FORCE_ERR_ALT_READ_PROTO_ALLOC;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &start_pkt_cnt, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	simple_rdzv(false);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &end_pkt_cnt, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	/* No restricted packets should have been sent */
	cr_assert(end_pkt_cnt == start_pkt_cnt,
		  "Incorrect number of restricted packets");
}
#endif /* ENABLE_DEBUG */

/* Test basic send/recv w/data */
Test(tagged, pingdata)
{
	pingdata();
}

/* Test basic inject send */
Test(tagged, inject_ping)
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
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_tinject(cxit_ep, send_buf, send_len, cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tinject failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
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

	/* Try invalid lengths */
	ret = fi_tinject(cxit_ep, send_buf, cxit_fi->tx_attr->inject_size+1,
			 cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, -FI_EMSGSIZE, "fi_tinject failed %d", ret);

	ret = fi_tinject(cxit_ep, send_buf, 4*1024*1024,
			 cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, -FI_EMSGSIZE, "fi_tinject failed %d", ret);

	ret = fi_tinject(cxit_ep, send_buf, cxit_fi->ep_attr->max_msg_size+1,
			 cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, -FI_EMSGSIZE, "fi_tinject failed %d", ret);

	free(send_buf);
	free(recv_buf);
}

/* Test basic injectdata */
Test(tagged, injectdata_ping)
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
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Send 64 bytes to self */
	ret = fi_tinjectdata(cxit_ep, send_buf, send_len, data,
			     cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tinject failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len,
			  FI_TAGGED | FI_RECV | FI_REMOTE_CQ_DATA,
			  NULL, data, 0);
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
Test(tagged, vping)
{
	vping();
}

/* Test basic sendmsg/recvmsg */
Test(tagged, msgping)
{
	msgping();
}

/* Test FI_FENCE */
Test(tagged, fence)
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
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
	struct iovec riovec;
	struct iovec siovec;

	recv_buf = aligned_alloc(s_page_size, s_page_size);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, s_page_size);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_FENCE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Test rendezvous fence */
	send_len = recv_len = s_page_size;
	siovec.iov_len = send_len;
	riovec.iov_len = recv_len;

	for (i = 0; i < send_len; i++) {
		recv_buf[i] = 0;
		send_buf[i] = i + 0xa0;
	}

	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_FENCE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);

		/* progress */
		fi_cq_read(cxit_tx_cq, &tx_cqe, 0);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

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

void cxit_tagged_setup_nofence(void)
{
	cxit_setup_getinfo();
	cxit_fi_hints->caps = CXIP_EP_PRI_CAPS;
	cxit_setup_rma();
}

/* Test messaging without FI_FENCE */
Test(tagged_nofence, nofence,
     .init = cxit_tagged_setup_nofence,
     .fini = cxit_teardown_rma)
{
	int ret;
	uint8_t *send_buf;
	int send_len = 64;
	struct fi_msg_tagged smsg = {};
	struct fi_msg msg = {};
	struct iovec siovec;

	send_buf = aligned_alloc(s_page_size, s_page_size);
	cr_assert(send_buf);

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_FENCE);
	cr_assert_eq(ret, -FI_EINVAL);

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	msg.msg_iov = &siovec;
	msg.iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.context = NULL;

	ret = fi_sendmsg(cxit_ep, &msg, FI_FENCE);
	cr_assert_eq(ret, -FI_EINVAL);

	free(send_buf);
}

/* Test basic sendmsg/recvmsg with data */
Test(tagged, msgping_wdata)
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
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
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
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;
	smsg.data = data;

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_REMOTE_CQ_DATA);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len,
			  FI_TAGGED | FI_RECV | FI_REMOTE_CQ_DATA, NULL,
			  data, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

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
Test(tagged, inject_msgping)
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
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
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
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	/* Send 64 bytes to self */
	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_INJECT);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

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

/* Test unexpected send/recv */
Test(tagged, ux_ping)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	fi_addr_t from;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Send 64 bytes to self */
	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Give some time for the message to move */
	sleep(1);

	/* Post RX buffer */
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert(ret == 1);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_assert(recv_buf[i] == send_buf[i],
			  "data mismatch, element: %d\n", i);
	}

	free(send_buf);
	free(recv_buf);
}

/* Issue a fi_trecvmsg with FI_PEEK and validate result */
ssize_t try_peek(fi_addr_t addr, uint64_t tag, uint64_t ignore,
		 ssize_t len, void *context, bool claim)
{
	struct fi_msg_tagged tmsg = {
		.msg_iov = NULL,
		.iov_count = 0,
		.addr = addr,
		.tag = tag,
		.ignore = ignore,
		.context = context,
		.data = 0
	};
	struct fi_cq_tagged_entry cqe = {};
	struct fi_cq_err_entry err_cqe = {};
	fi_addr_t from;
	ssize_t ret;

	do {
		fi_cq_read(cxit_tx_cq, NULL, 0);
		fi_cq_read(cxit_rx_cq, NULL, 0);
		ret = fi_trecvmsg(cxit_ep, &tmsg,
				  claim ? FI_CLAIM | FI_PEEK : FI_PEEK);
	} while (ret == -FI_EAGAIN);
	if (ret != FI_SUCCESS)
		return ret;

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &cqe, 1, &from);
		if (ret == 1) {
			validate_rx_event_mask(&cqe, context, len,
					       FI_TAGGED | FI_RECV, NULL, 0,
					       tag, ignore);
			cr_assert_eq(from, cxit_ep_fi_addr,
				     "Invalid source address");
			ret = FI_SUCCESS;
			break;
		} else if (ret == -FI_EAVAIL) {
			ret = fi_cq_readerr(cxit_rx_cq, &err_cqe, 0);
			cr_assert_eq(ret, 1);

			cr_assert(err_cqe.err == ENOMSG, "Bad CQE error %d",
				  err_cqe.err);
			cr_assert(err_cqe.buf == 0, "Invalid buffer");
			cr_assert(err_cqe.olen == 0, "Invalid length");
			cr_assert(err_cqe.tag == tag, "Invalid tag");
			cr_assert(err_cqe.err == FI_ENOMSG,
				  "Invalid error code %d", err_cqe.err);
			ret = err_cqe.err;
			break;
		}
	} while (ret == -FI_EAGAIN);

	return ret;
}

static int wait_peek(fi_addr_t addr, uint64_t tag, uint64_t ignore,
		     ssize_t len, void *context, bool claim)
{
	int ret;

	do {
		ret = try_peek(addr, tag, ignore, len, context, claim);
	} while (ret == FI_ENOMSG);

	return ret;
}

#define PEEK_TAG_BASE		0x0000a000
#define PEEK_MSG_LEN		64
#define PEEK_NUM_MSG		4
#define PEEK_NUM_FAKE_ADDRS	3

/* Test fi_trecvmsg using FI_PEEK flag to search unexpected message list.
 * Additional message sizes will be tested within the multitudes tests.
 */
Test(tagged, ux_peek)
{
	ssize_t ret;
	uint8_t *rx_buf;
	uint8_t *tx_buf;
	ssize_t	rx_len = PEEK_MSG_LEN;
	ssize_t tx_len = PEEK_MSG_LEN;
	struct fi_cq_tagged_entry cqe;
	struct fi_context rx_context[PEEK_NUM_MSG];
	struct fi_context tx_context[PEEK_NUM_MSG];
	struct fi_msg_tagged tmsg = {};
	struct iovec iovec;
	fi_addr_t from;
	int i, tx_comp;
	struct cxip_addr fake_ep_addrs[PEEK_NUM_FAKE_ADDRS];

	/* Add fake AV entries to test peek for non-matching valid address */
	for (i = 0; i < PEEK_NUM_FAKE_ADDRS; i++) {
		fake_ep_addrs[i].nic = i + 0x41c;
		fake_ep_addrs[i].pid = i + 0x21;
	}
	ret = fi_av_insert(cxit_av, (void *)fake_ep_addrs,
			   PEEK_NUM_FAKE_ADDRS, NULL, 0, NULL);
	cr_assert(ret == PEEK_NUM_FAKE_ADDRS);

	rx_buf = aligned_alloc(s_page_size, rx_len * PEEK_NUM_MSG);
	cr_assert(rx_buf);
	memset(rx_buf, 0, rx_len * PEEK_NUM_MSG);

	tx_buf = aligned_alloc(s_page_size, tx_len * PEEK_NUM_MSG);
	cr_assert(tx_buf);

	/* Send messages to build the unexpected list */
	for (i = 0; i < PEEK_NUM_MSG; i++) {
		memset(&tx_buf[i * tx_len], 0xa0 + i, tx_len);
		iovec.iov_base = &tx_buf[i * tx_len];
		iovec.iov_len = tx_len;

		tmsg.msg_iov = &iovec;
		tmsg.iov_count = 1;
		tmsg.addr = cxit_ep_fi_addr;
		tmsg.tag = PEEK_TAG_BASE + i;
		tmsg.ignore = 0;
		tmsg.context = &tx_context[i];

		ret = fi_tsendmsg(cxit_ep, &tmsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %" PRId64,
			     ret);
	}

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &cqe, 0);

	/* Any address with bad tag and no context */
	ret = try_peek(FI_ADDR_UNSPEC, PEEK_TAG_BASE + PEEK_NUM_MSG + 1, 0,
		       tx_len, NULL, false);
	cr_assert_eq(ret, FI_ENOMSG, "Peek with invalid tag");

	/* Any address with bad tag with context */
	ret = try_peek(FI_ADDR_UNSPEC, PEEK_TAG_BASE + PEEK_NUM_MSG + 1, 0,
		       tx_len, &rx_context[0], false);
	cr_assert_eq(ret, FI_ENOMSG, "Peek with invalid tag");

	/* Non matching valid source address with valid tag */
	ret = try_peek(3, PEEK_TAG_BASE, 0, tx_len, NULL, false);
	cr_assert_eq(ret, FI_ENOMSG, "Peek with wrong match address");

	/* Valid with any address and valid tag */
	ret = try_peek(FI_ADDR_UNSPEC, PEEK_TAG_BASE + 1, 0, tx_len,
		       NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "Peek with invalid tag");

	/* Valid with expected address and valid tag */
	ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + 1, 0, tx_len,
		       NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "Peek with bad address");

	/* Valid with any address and good tag when masked correctly */
	ret = try_peek(FI_ADDR_UNSPEC, PEEK_TAG_BASE + 0x20002,
		       0x0FFF0000UL, tx_len, NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "Peek tag ignore bits failed");

	/* Valid with expected address and good tag when masked correctly */
	ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + 0x20002,
		       0x0FFF0000UL, tx_len, NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "Peek tag ignore bits failed");

	/* Verify peek of all sends */
	for (i = 0; i < PEEK_NUM_MSG; i++) {
		ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0,
			       tx_len, &rx_context[i], false);
		cr_assert_eq(ret, FI_SUCCESS, "Peek valid tag not found");
	}

	/* Verify peek of all sends in reverse order */
	for (i = PEEK_NUM_MSG - 1; i >= 0; i--) {
		ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0,
			       tx_len, &rx_context[i], false);
		cr_assert_eq(ret, FI_SUCCESS, "Peek valid tag not found");
	}

	/* Receive all unexpected sends */
	for (i = 0; i < PEEK_NUM_MSG; i++) {
		iovec.iov_base = &rx_buf[i * rx_len];
		iovec.iov_len = rx_len;

		tmsg.msg_iov = &iovec;
		tmsg.iov_count = 1;
		tmsg.addr = cxit_ep_fi_addr;
		tmsg.tag = PEEK_TAG_BASE + i;
		tmsg.ignore = 0;
		tmsg.context = &rx_context[i];

		ret = fi_trecvmsg(cxit_ep, &tmsg, 0);
		cr_assert_eq(ret, FI_SUCCESS,
			     "fi_trecvmsg failed %" PRId64, ret);

		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &cqe, 1, &from);
		} while (ret == -FI_EAGAIN);

		cr_assert(ret == 1);
		cr_assert_eq(from, cxit_ep_fi_addr, "Invalid source address");
		validate_rx_event(&cqe, &rx_context[i], rx_len,
				  FI_TAGGED | FI_RECV, NULL, 0,
				  PEEK_TAG_BASE + i);
	}

	/* Verify received data */
	for (i = 0; i < PEEK_NUM_MSG; i++) {
		ret = memcmp(&tx_buf[i * tx_len], &rx_buf[i * rx_len], tx_len);
		cr_assert_eq(ret, 0, "RX buffer data mismatch for msg %d", i);
	}

	/* Verify received messages have been removed from unexpected list */
	for (i = 0; i < PEEK_NUM_MSG; i++) {
		ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0,
			       tx_len, &rx_context[i], false);
		cr_assert_eq(ret, FI_ENOMSG,
			     "Peek after receive did not fail %" PRId64, ret);
	}

	/* Wait for TX async events to complete, and validate */
	tx_comp = 0;
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		if (ret == 1) {
			validate_tx_event(&cqe, FI_TAGGED | FI_SEND,
					  &tx_context[tx_comp]);
			tx_comp++;
		}
		cr_assert(ret == 1 || ret == -FI_EAGAIN,
			  "Bad fi_cq_read return %" PRId64, ret);
	} while (tx_comp < PEEK_NUM_MSG);
	cr_assert_eq(tx_comp, PEEK_NUM_MSG,
		     "Peek tsendmsg only %d TX completions read", tx_comp);

	free(rx_buf);
	free(tx_buf);
}

/* FI_PEEK with FI_CLAIM testing */
void test_ux_claim(int num_msgs, int msg_len)
{
	ssize_t ret;
	uint8_t *rx_buf;
	uint8_t *tx_buf;
	ssize_t	rx_len = msg_len;
	ssize_t tx_len = msg_len;
	struct fi_cq_tagged_entry cqe;
	struct fi_context *rx_context; /* [PEEK_NUM_MSG]; */
	struct fi_context *tx_context; /* [PEEK_NUM_MSG]; */
	struct fi_msg_tagged tmsg = {};
	struct iovec iovec;
	fi_addr_t from;
	int i, tx_comp;
	struct cxip_addr fake_ep_addrs[PEEK_NUM_FAKE_ADDRS];

	rx_context = calloc(num_msgs, sizeof(struct fi_context));
	cr_assert_not_null(rx_context);
	tx_context = calloc(num_msgs, sizeof(struct fi_context));
	cr_assert_not_null(tx_context);

	rx_buf = aligned_alloc(s_page_size, rx_len * num_msgs);
	cr_assert_not_null(rx_buf);
	memset(rx_buf, 0, rx_len * num_msgs);

	tx_buf = aligned_alloc(s_page_size, tx_len * num_msgs);
	cr_assert_not_null(tx_buf);

	/* Add fake AV entries to test peek for non-matching valid address */
	for (i = 0; i < PEEK_NUM_FAKE_ADDRS; i++) {
		fake_ep_addrs[i].nic = i + 0x41c;
		fake_ep_addrs[i].pid = i + 0x21;
	}
	ret = fi_av_insert(cxit_av, (void *)fake_ep_addrs,
			   PEEK_NUM_FAKE_ADDRS, NULL, 0, NULL);
	cr_assert(ret == PEEK_NUM_FAKE_ADDRS);

	/* Send messages to build the unexpected list */
	for (i = 0; i < num_msgs; i++) {
		memset(&tx_buf[i * tx_len], 0xa0 + i, tx_len);
		iovec.iov_base = &tx_buf[i * tx_len];
		iovec.iov_len = tx_len;

		tmsg.msg_iov = &iovec;
		tmsg.iov_count = 1;
		tmsg.addr = cxit_ep_fi_addr;
		tmsg.tag = PEEK_TAG_BASE + i;
		tmsg.ignore = 0;
		tmsg.context = &tx_context[i];

		ret = fi_tsendmsg(cxit_ep, &tmsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %" PRId64,
			     ret);
	}

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &cqe, 0);

	/* Any address with bad tag and FI_CLAIM with no context */
	ret = try_peek(FI_ADDR_UNSPEC, PEEK_TAG_BASE + num_msgs + 1, 0,
		       tx_len, NULL, true);
	cr_assert_eq(ret, -FI_EINVAL,
		     "FI_CLAIM with invalid tag and no context");

	/* Any address with bad tag and FI_CLAIM with context */
	ret = try_peek(FI_ADDR_UNSPEC, PEEK_TAG_BASE + num_msgs + 1, 0,
		       tx_len, &rx_context[0], true);
	cr_assert_eq(ret, FI_ENOMSG, "FI_CLAIM with invalid tag");

	/* Non matching valid source address with valid tag and context */
	ret = try_peek(3, PEEK_TAG_BASE, 0, tx_len, &rx_context[0], true);
	cr_assert_eq(ret, FI_ENOMSG, "FI_CLAIM with wrong match address");

	/* Verify peek of all sends */
	for (i = 0; i < num_msgs; i++) {
		ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0,
			       tx_len, &rx_context[i], false);
		cr_assert_eq(ret, FI_SUCCESS, "All unexpected tags not found");
	}

	/* Verify peek of all sends in reverse order with FI_CLAIM */
	for (i = num_msgs - 1; i >= 0; i--) {
		ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0,
			       tx_len, &rx_context[i], true);
		cr_assert_eq(ret, FI_SUCCESS,
			     "FI_PEEK | FI_CLAIM valid tag not found");
	}

	/* Verify peek of previously claimed messages fail */
	for (i = 0; i < num_msgs; i++) {
		ret = try_peek(cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0,
			       tx_len, &rx_context[i], false);
		cr_assert_eq(ret, FI_ENOMSG,
			     "Unexpected message not claimed found");
	}

	/* Receive all claimed unexpected messages */
	for (i = 0; i < num_msgs; i++) {
		iovec.iov_base = &rx_buf[i * rx_len];
		iovec.iov_len = rx_len;

		tmsg.msg_iov = &iovec;
		tmsg.iov_count = 1;
		tmsg.addr = cxit_ep_fi_addr;
		tmsg.tag = PEEK_TAG_BASE + i;
		tmsg.ignore = 0;
		tmsg.context = &rx_context[i];

		ret = fi_trecvmsg(cxit_ep, &tmsg, FI_CLAIM);
		cr_assert_eq(ret, FI_SUCCESS,
			     "fi_trecvmsg FI_CLAIM failed %" PRId64, ret);

		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &cqe, 1, &from);
		} while (ret == -FI_EAGAIN);

		cr_assert(ret == 1);
		cr_assert_eq(from, cxit_ep_fi_addr, "Invalid source address");
		validate_rx_event(&cqe, &rx_context[i], rx_len,
				  FI_TAGGED | FI_RECV, NULL, 0,
				  PEEK_TAG_BASE + i);
	}

	/* Verify received data */
	for (i = 0; i < num_msgs; i++) {
		ret = memcmp(&tx_buf[i * tx_len], &rx_buf[i * rx_len], tx_len);
		cr_assert_eq(ret, 0, "RX buffer data mismatch for msg %d", i);
	}

	/* Wait for TX async events to complete, and validate */
	tx_comp = 0;
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		if (ret == 1) {
			validate_tx_event(&cqe, FI_TAGGED | FI_SEND,
					  &tx_context[tx_comp]);
			tx_comp++;
		}
		cr_assert(ret == 1 || ret == -FI_EAGAIN,
			  "Bad fi_cq_read return %" PRId64, ret);
	} while (tx_comp < num_msgs);
	cr_assert_eq(tx_comp, num_msgs,
		     "Peek tsendmsg only %d TX completions read", tx_comp);

	free(rx_buf);
	free(tx_buf);
	free(rx_context);
	free(tx_context);
}

/* Test fi_trecvmsg using FI_PEEK and FI_CLAIM flags to search unexpected
 * message list and claim the message.
 */
Test(tagged, ux_claim)
{
	test_ux_claim(4, 1024);
}

Test(tagged, ux_claim_rdzv)
{
	test_ux_claim(4, 65536);
}

#define PEEK_ORDER_SEND_COUNT 5
#define PEEK_ORDER_TAG 0x1234ULL

static void verify_peek_claim_order_same_tag(size_t xfer_base_size, bool claim)
{
	void *buf;
	struct fi_context context;
	int i;
	int ret;
	struct fi_cq_tagged_entry cqe;
	fi_addr_t from;
	struct fi_msg_tagged tmsg = {};
	struct iovec iovec;
	size_t buf_size = xfer_base_size + (PEEK_ORDER_SEND_COUNT - 1);
	size_t xfer_size;

	buf = malloc(buf_size);
	cr_assert_not_null(buf);

	/* Issue sends unexpected to target. Same tagged is used with different
	 * transfer size. Transfer size identifies operation order.
	 */
	for (i = 0; i < PEEK_ORDER_SEND_COUNT; i++) {
		ret = fi_tsend(cxit_ep, buf, xfer_base_size + i, NULL,
			       cxit_ep_fi_addr, PEEK_ORDER_TAG, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed: %d", ret);
	}

	/* Receives should be processed in order. Order is incrementing receive
	 * size.
	 */
	iovec.iov_base = buf;
	iovec.iov_len = buf_size;

	tmsg.msg_iov = &iovec;
	tmsg.iov_count = 1;
	tmsg.addr = cxit_ep_fi_addr;
	tmsg.tag = PEEK_ORDER_TAG;
	tmsg.ignore = 0;
	tmsg.context = &context;

	for (i = 0; i < PEEK_ORDER_SEND_COUNT; i++) {
		xfer_size = xfer_base_size + i;

		ret = wait_peek(cxit_ep_fi_addr, PEEK_ORDER_TAG, 0,
				xfer_size, tmsg.context, claim);
		cr_assert_eq(ret, FI_SUCCESS, "try_peek failed: %d", ret);

		/* With claim, subsequent FI_PEEK without FI_CLAIM should always
		 * return next message.
		 */
		if (claim && i < (PEEK_ORDER_SEND_COUNT - 1)) {
			ret = wait_peek(cxit_ep_fi_addr, PEEK_ORDER_TAG, 0,
					xfer_size + 1, NULL, false);
			cr_assert_eq(ret, FI_SUCCESS, "try_peek failed: %d",
				     ret);
		}

		/* Recieve unexpected message. If message is FI_CLAIM,
		 * FI_CONTEXT buffer contains data to progress receive.
		 */
		ret = fi_trecvmsg(cxit_ep, &tmsg, claim ? FI_CLAIM : 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed: %d", ret);

		do {
			/* Process TX CQ (if needed). */
			fi_cq_read(cxit_tx_cq, NULL, 0);
			ret = fi_cq_readfrom(cxit_rx_cq, &cqe, 1, &from);
		} while (ret == -FI_EAGAIN);

		cr_assert_eq(ret, 1, "fi_cq_read failed: %d", ret);
		cr_assert_eq(from, cxit_ep_fi_addr,
			     "Invalid user id: expected=%#lx got=%#lx",
			     cxit_ep_fi_addr, from);
		validate_rx_event_mask(&cqe, tmsg.context, xfer_size,
				       FI_RECV | FI_TAGGED,
				       NULL, 0, PEEK_ORDER_TAG, 0);
	}

	free(buf);
}

Test(tagged, verify_peek_order_same_tag_idc)
{
	verify_peek_claim_order_same_tag(0, false);
}

Test(tagged, verify_peek_order_same_tag_eager)
{
	verify_peek_claim_order_same_tag(257, false);
}

Test(tagged, verify_peek_order_same_tag_rendezvous)
{
	verify_peek_claim_order_same_tag(1048576, false);
}

Test(tagged, verify_claim_order_same_tag_idc)
{
	verify_peek_claim_order_same_tag(0, true);
}

Test(tagged, verify_claim_order_same_tag_eager)
{
	verify_peek_claim_order_same_tag(257, true);
}

Test(tagged, verify_claim_order_same_tag_rendezvous)
{
	verify_peek_claim_order_same_tag(1048576, true);
}

/* Test MQD get of unexpected message list */
void verify_ux_dump(int num, ssize_t msg_len)
{
	ssize_t ret;
	size_t count;
	size_t ux_count;
	size_t ux_ret_count;
	struct fi_cq_tagged_entry *cq_entry;
	fi_addr_t *src_addr;
	uint8_t *tx_buf;
	ssize_t tx_len = msg_len;
	uint8_t *rx_buf;
	ssize_t	rx_len = msg_len;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_tagged tmsg = {};
	struct iovec iovec;
	int i;
	int tx_comp = 0;
	fi_addr_t from;

	rx_buf = aligned_alloc(s_page_size, rx_len * num);
	cr_assert(rx_buf);
	tx_buf = aligned_alloc(s_page_size, tx_len * num);
	cr_assert(tx_buf);

	/* Send messages to build the unexpected list */
	for (i = 0; i < num; i++) {
		memset(&tx_buf[i * tx_len], 0xa0 + i, tx_len);
		iovec.iov_base = &tx_buf[i * tx_len];
		iovec.iov_len = tx_len;

		tmsg.msg_iov = &iovec;
		tmsg.iov_count = 1;
		tmsg.addr = cxit_ep_fi_addr;
		tmsg.tag = PEEK_TAG_BASE + i;
		tmsg.ignore = 0;
		tmsg.context = NULL;

		ret = fi_tsendmsg(cxit_ep, &tmsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %" PRId64,
			     ret);
	}

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &cqe, 0);

	/* Call first to get number of UX entries */
	ux_ret_count = dom_ops->ep_get_unexp_msgs(cxit_ep, NULL, 0,
						  NULL, &ux_count);
	cr_assert_eq(ux_ret_count, 0, "Num entries returned");
	count = ux_count;

	cq_entry = calloc(ux_count, sizeof(*cq_entry));
	ux_ret_count = dom_ops->ep_get_unexp_msgs(cxit_ep, cq_entry, count,
						  NULL, &ux_count);
	cr_assert(ux_ret_count <= count, "Number UX returned <= count");
	cr_assert_eq(ux_ret_count, num, "Number UX returned wrong");

	for (i = 0; i < ux_ret_count; i++) {
		cr_assert(cq_entry[i].op_context == NULL, "Context");
		cr_assert(cq_entry[i].buf == NULL, "Buf");
		cr_assert(cq_entry[i].tag == PEEK_TAG_BASE + i, "Tag match");
		cr_assert(cq_entry[i].len == tx_len, "Length %ld",
			  cq_entry[i].len);
		cr_assert(cq_entry[i].flags & FI_TAGGED, "FI_TAGGED");
		cr_assert(!(cq_entry[i].flags & FI_REMOTE_CQ_DATA),
			  "FI_REMOTE_CQ_DATA");
	}

	/* Get entries with source address */
	src_addr = calloc(ux_count, sizeof(*src_addr));
	ux_ret_count = dom_ops->ep_get_unexp_msgs(cxit_ep, cq_entry, count,
						  src_addr, &ux_count);
	cr_assert(ux_ret_count <= count, "Number UX returned <= count");
	cr_assert_eq(ux_ret_count, num, "Number UX returned wrong");

	for (i = 0; i < ux_ret_count; i++)
		cr_assert_eq(src_addr[i], cxit_ep_fi_addr, "Source address");

	/* Receive all unexpected messages */
	for (i = 0; i < num; i++) {
		ret = fi_trecv(cxit_ep, &rx_buf[i * rx_len], rx_len, NULL,
			       cxit_ep_fi_addr, PEEK_TAG_BASE + i, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %ld", ret);

		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &cqe, 1, &from);
		} while (ret == -FI_EAGAIN);

		cr_assert(ret == 1);
		cr_assert_eq(from, cxit_ep_fi_addr, "Invalid source address");

		validate_rx_event(&cqe, NULL, rx_len,
				  FI_TAGGED | FI_RECV, NULL, 0,
				  PEEK_TAG_BASE + i);
	}

	/* Verify received data */
	for (i = 0; i < num; i++) {
		ret = memcmp(&tx_buf[i * tx_len], &rx_buf[i * rx_len], tx_len);
		cr_assert_eq(ret, 0, "RX buffer data mismatch for msg %d", i);
	}

	/* Wait for TX async events to complete, and validate */
	tx_comp = 0;
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		if (ret == 1)
			tx_comp++;
		cr_assert(ret == 1 || ret == -FI_EAGAIN,
			  "Bad fi_cq_read return %ld", ret);
	} while (tx_comp < num);
	cr_assert_eq(tx_comp, num,
		     "Peek tsendmsg only %d TX completions read", tx_comp);

	free(src_addr);
	free(cq_entry);
	free(tx_buf);
}

Test(tagged, ux_dump_eager)
{
	verify_ux_dump(4, 512);
}

Test(tagged, ux_dump_rdzv)
{
	verify_ux_dump(4, 16384);
}

/* Test DIRECTED_RECV send/recv */
void directed_recv(bool logical)
{
	int i, ret;
	uint8_t *recv_buf,
		*fake_recv_buf,
		*send_buf;
	int recv_len = 0x1000;
	int send_len = 0x1000;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
#define N_FAKE_ADDRS 3
	struct cxip_addr fake_ep_addrs[N_FAKE_ADDRS+1];
	fi_addr_t from;

	if (logical)
		cxit_av_attr.flags = FI_SYMMETRIC;
	cxit_setup_enabled_ep();

	/* Create multiple logical names for the local EP address */
	for (i = 0; i < N_FAKE_ADDRS; i++) {
		fake_ep_addrs[i].nic = i + 0x41c;
		fake_ep_addrs[i].pid = i + 0x21;
	}

	ret = fi_av_insert(cxit_av, (void *)fake_ep_addrs, 3, NULL, 0, NULL);
	cr_assert(ret == 3);

	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);

	recv_buf = calloc(recv_len, 1);
	cr_assert(recv_buf);

	fake_recv_buf = calloc(recv_len, 1);
	cr_assert(fake_recv_buf);

	send_buf = malloc(send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Post an RX buffer matching each EP name that won't be targeted */
	for (i = 0; i < N_FAKE_ADDRS; i++) {
		ret = fi_trecv(cxit_ep, fake_recv_buf, recv_len, NULL, i, 0, 0,
			       NULL);
		cr_assert(ret == FI_SUCCESS);
	}

	/* Post short RX buffer matching EP name 3 */
	ret = fi_trecv(cxit_ep, recv_buf, 64, NULL, 3, 0, 0, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Post long RX buffer matching EP name 3 */
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, 3, 0, 0, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Send short message to self (FI address 3)  */
	send_len = 64;

	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, 3, 0, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == 3, "Invalid source address, exp: 3 got: %lu", from);

	/* Wait for async event indicating data has been sent */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			     i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(fake_recv_buf[i], 0,
			     "fake data corrupted, element[%d] err=%d\n",
			     i, err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Send long message to self (FI address 3)  */
	memset(recv_buf, 0, recv_len);
	send_len = 0x1000;

	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, 3, 0, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == 3, "Invalid source address, exp: 3 got: %lu", from);

	/* Wait for async event indicating data has been sent */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			     i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(fake_recv_buf[i], 0,
			     "fake data corrupted, element[%d] err=%d\n",
			     i, err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Send long UX message to self (FI address 3)  */
	memset(recv_buf, 0, recv_len);
	send_len = 0x1000;

	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, 3, 0, NULL);
	cr_assert(ret == FI_SUCCESS);

	sleep(1);

	/* Post long RX buffer matching EP name 3 */
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, 3, 0, 0, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);

		/* Progress */
		fi_cq_read(cxit_tx_cq, &tx_cqe, 0);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == 3, "Invalid source address, exp: 3 got: %lu", from);

	/* Wait for async event indicating data has been sent */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			     i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(fake_recv_buf[i], 0,
			     "fake data corrupted, element[%d] err=%d\n",
			     i, err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(fake_recv_buf);
	free(recv_buf);

	cxit_teardown_tagged();
}

Test(tagged_directed, directed)
{
	directed_recv(false);
}

Test(tagged_directed, directed_logical)
{
	directed_recv(true);
}

/* Test unexpected send/recv */
#define RDZV_TAG (46)

struct tagged_thread_args {
	uint8_t *buf;
	size_t len;
	struct fi_cq_tagged_entry *cqe;
	fi_addr_t src_addr;
	size_t io_num;
	size_t tag;
	void *context;
};

static void *tsend_worker(void *data)
{
	int ret;
	struct tagged_thread_args *args;
	uint64_t tag;

	args = (struct tagged_thread_args *)data;
	tag = args->tag;

	/* Send 64 bytes to FI address 0 (self) */
	ret = fi_tsend(cxit_ep, args->buf, args->len, NULL, cxit_ep_fi_addr,
		       tag, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "%s %ld: unexpected ret %d", __func__,
		     args->io_num, ret);

	/* Wait for async event indicating data has been sent */
	do {
		ret = fi_cq_read(cxit_tx_cq, args->cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "%s %ld: unexpected ret %d", __func__,
		     args->io_num, ret);

	pthread_exit(NULL);
}

static void *trecv_worker(void *data)
{
	int ret;
	struct tagged_thread_args *args;
	uint64_t tag;

	args = (struct tagged_thread_args *)data;
	tag = args->tag;

	/* Post RX buffer */
	ret = fi_trecv(cxit_ep, args->buf, args->len, NULL, FI_ADDR_UNSPEC, tag,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "%s %ld: unexpected ret %d", __func__,
		     args->io_num, ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, args->cqe, 1, &args->src_addr);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "%s %ld: unexpected ret %d", __func__,
		     args->io_num, ret);

	pthread_exit(NULL);
}

Test(tagged, ux_sw_rdzv)
{
	size_t i;
	int ret;
	uint8_t *recv_buf, *send_buf;
	size_t buf_len = 2 * 1024 * 1024;
	int recv_len = 4 * 1024;
	int send_len = 4 * 1024;
	struct fi_cq_tagged_entry rx_cqe, tx_cqe;
	pthread_t threads[2];
	struct tagged_thread_args args[2];
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	recv_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, buf_len);

	send_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(send_buf);

	for (i = 0; i < buf_len; i++)
		send_buf[i] = i + 0xa0;

	args[0].buf = send_buf;
	args[0].len = send_len;
	args[0].cqe = &tx_cqe;
	args[0].io_num = 0;
	args[0].tag = RDZV_TAG;
	args[1].buf = recv_buf;
	args[1].len = recv_len;
	args[1].cqe = &rx_cqe;
	args[1].io_num = 1;
	args[1].tag = RDZV_TAG;

	/* Give some time for the message to move */
	cr_assert_arr_neq(recv_buf, send_buf, buf_len);

	/* start tsend thread */
	ret = pthread_create(&threads[0], &attr, tsend_worker,
			     (void *)&args[0]);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	sleep(1);

	/* start trecv thread */
	ret = pthread_create(&threads[1], &attr, trecv_worker,
			     (void *)&args[1]);
	cr_assert_eq(ret, 0, "Recv thread create failed %d", ret);

	/* Wait for the threads to complete */
	ret = pthread_join(threads[0], NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);
	ret = pthread_join(threads[1], NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	pthread_attr_destroy(&attr);

	/* Validate sent data */
	cr_expect_arr_eq(recv_buf, send_buf, recv_len);
	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);
	validate_rx_event(&rx_cqe, NULL, recv_len, FI_TAGGED | FI_RECV, NULL,
			  0, args[0].tag);
	cr_assert_eq(args[1].src_addr, cxit_ep_fi_addr,
		     "Invalid source address");

	free(send_buf);
	free(recv_buf);
}

Test(tagged, expected_sw_rdzv)
{
	size_t i;
	int ret;
	uint8_t *recv_buf, *send_buf;
	size_t buf_len = 2 * 1024 * 1024;
	int recv_len = 4 * 1024;
	int send_len = 4 * 1024;
	struct fi_cq_tagged_entry rx_cqe, tx_cqe;
	pthread_t threads[2];
	struct tagged_thread_args args[2];
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	recv_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, buf_len);

	send_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(send_buf);

	for (i = 0; i < buf_len; i++)
		send_buf[i] = i + 0xa0;

	args[0].buf = send_buf;
	args[0].len = send_len;
	args[0].cqe = &tx_cqe;
	args[0].io_num = 0;
	args[0].tag = RDZV_TAG;
	args[1].buf = recv_buf;
	args[1].len = recv_len;
	args[1].cqe = &rx_cqe;
	args[1].io_num = 1;
	args[1].tag = RDZV_TAG;

	/* Give some time for the message to move */
	cr_assert_arr_neq(recv_buf, send_buf, buf_len);

	/* Start trecv thread first so the buffer is ready when the data is sent
	 */
	ret = pthread_create(&threads[1], &attr, trecv_worker,
			     (void *)&args[1]);
	cr_assert_eq(ret, 0, "Recv thread create failed %d", ret);

	sleep(1);

	/* Start tsend thread to send the data into the ready buffer */
	ret = pthread_create(&threads[0], &attr, tsend_worker,
			     (void *)&args[0]);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	/* Wait for the threads to complete */
	ret = pthread_join(threads[0], NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);
	ret = pthread_join(threads[1], NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	pthread_attr_destroy(&attr);

	/* Validate sent data */
	cr_expect_arr_eq(recv_buf, send_buf, recv_len);
	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);
	validate_rx_event(&rx_cqe, NULL, recv_len, FI_TAGGED | FI_RECV, NULL,
			  0, args[0].tag);
	cr_assert_eq(args[1].src_addr, cxit_ep_fi_addr,
		     "Invalid source address");

	free(send_buf);
	free(recv_buf);
}

#define NUM_IOS (12)

struct tagged_event_args {
	struct fid_cq *cq;
	struct fi_cq_tagged_entry *cqe;
	size_t io_num;
};

static void *tagged_evt_worker(void *data)
{
	int ret;
	struct tagged_event_args *args;

	args = (struct tagged_event_args *)data;

	for (size_t i = 0; i < args->io_num; i++) {
		/* Wait for async event indicating data has been sent */
		do {
			ret = fi_cq_read(args->cq, &args->cqe[i], 1);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "%ld: unexpected ret %d", i,
			     ret);
	}

	pthread_exit(NULL);
}

Test(tagged, multitudes_sw_rdzv, .timeout=60)
{
	int ret;
	size_t buf_len = 4 * 1024;
	struct fi_cq_tagged_entry rx_cqe[NUM_IOS];
	struct fi_cq_tagged_entry tx_cqe[NUM_IOS];
	struct tagged_thread_args tx_args[NUM_IOS];
	struct tagged_thread_args rx_args[NUM_IOS];
	pthread_t tx_thread;
	pthread_t rx_thread;
	pthread_attr_t attr;
	struct tagged_event_args tx_evt_args = {
		.cq = cxit_tx_cq,
		.cqe = tx_cqe,
		.io_num = NUM_IOS,
	};
	struct tagged_event_args rx_evt_args = {
		.cq = cxit_rx_cq,
		.cqe = rx_cqe,
		.io_num = NUM_IOS,
	};

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	/* Issue the Sends */
	for (size_t tx_io = 0; tx_io < NUM_IOS; tx_io++) {
		tx_args[tx_io].len = buf_len;
		tx_args[tx_io].tag = tx_io;
		tx_args[tx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(tx_args[tx_io].buf);
		for (size_t i = 0; i < buf_len; i++)
			tx_args[tx_io].buf[i] = i + 0xa0 + tx_io;

		ret = fi_tsend(cxit_ep, tx_args[tx_io].buf, tx_args[tx_io].len,
			       NULL, cxit_ep_fi_addr, tx_args[tx_io].tag,
			       NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend %ld: unexpected ret %d",
			     tx_io, ret);
	}

	/* Start processing Send events */
	ret = pthread_create(&tx_thread, &attr, tagged_evt_worker,
				(void *)&tx_evt_args);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	/* Peek for each tag on UX list */
	for (size_t rx_io = 0; rx_io < NUM_IOS; rx_io++) {
		ret = try_peek(FI_ADDR_UNSPEC, rx_io, 0, buf_len, NULL, false);
		cr_assert_eq(ret, FI_SUCCESS, "peek of UX message failed");
	}

	/* Issue the Receives */
	for (size_t rx_io = 0; rx_io < NUM_IOS; rx_io++) {
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		ret = fi_trecv(cxit_ep, rx_args[rx_io].buf, rx_args[rx_io].len,
			       NULL, FI_ADDR_UNSPEC, rx_args[rx_io].tag,
			       0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Start processing Receive events */
	ret = pthread_create(&rx_thread, &attr, tagged_evt_worker,
			     (void *)&rx_evt_args);
	cr_assert_eq(ret, 0, "Receive thread create failed %d", ret);

	/* Wait for the RX/TX event threads to complete */
	ret = pthread_join(tx_thread, NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);

	ret = pthread_join(rx_thread, NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	/* Validate results */
	for (size_t io = 0; io < NUM_IOS; io++) {
		/* Validate sent data */
		cr_expect_arr_eq(rx_args[io].buf, tx_args[io].buf, buf_len);
		validate_tx_event(&tx_cqe[io], FI_TAGGED | FI_SEND, NULL);
		validate_rx_event(&rx_cqe[io], NULL, buf_len,
				  FI_TAGGED | FI_RECV, NULL,
				  0, tx_args[rx_cqe[io].tag].tag);

		free(tx_args[io].buf);
		free(rx_args[io].buf);
	}

	pthread_attr_destroy(&attr);
}

struct multitudes_params {
	size_t length;
	size_t num_ios;
	bool peek;
	bool claim;
};

/* This is a parameterized test to execute an arbitrary set of tagged send/recv
 * operations. The test is configurable in two parameters, the length value is
 * the size of the data to be transferred. The num_ios will set the number of
 * matching send/recv that are launched in each test.
 *
 * The test will first execute the fi_tsend() for `num_ios` number of buffers.
 * A background thread is launched to start processing the Cassini events for
 * the Send operations. The test will then pause for 1 second. After the pause,
 * the test will optionally use fi_trecvmsg() to FI_PEEK the unexpected list
 * and verify the send messages are on the unexpected list. Then the
 * test will execute the fi_trecv() to receive the buffers that were
 * previously sent. Another background thread is then launched to process the
 * receive events. When all send and receive operations have completed, the
 * threads exit and the results are compared to ensure the expected data was
 * returned.
 *
 * Based on the test's length parameter it will change the processing of the
 * send and receive operation. 2kiB and below lengths will cause the eager
 * data path to be used. Larger than 2kiB buffers will use the SW Rendezvous
 * data path to be used.
 */
void do_multitudes(struct multitudes_params *param)
{
	int ret;
	size_t buf_len = param->length;
	struct fi_cq_tagged_entry *rx_cqe;
	struct fi_cq_tagged_entry *tx_cqe;
	struct tagged_thread_args *tx_args;
	struct tagged_thread_args *rx_args;
	struct fi_context *rx_ctxts;
	struct iovec iovec;
	struct fi_msg_tagged tmsg = {};
	pthread_t tx_thread;
	pthread_t rx_thread;
	pthread_attr_t attr;
	struct tagged_event_args tx_evt_args = {
		.cq = cxit_tx_cq,
		.io_num = param->num_ios,
	};
	struct tagged_event_args rx_evt_args = {
		.cq = cxit_rx_cq,
		.io_num = param->num_ios,
	};
	char *rx_mode;
	bool claim = param->claim;

	/* TODO: Remove after HW FI_CLAIM support is implemented */
	rx_mode = getenv("FI_CXI_RX_MATCH_MODE");
	if (claim && (!rx_mode || strcmp(rx_mode, "software"))) {
		cr_assert(1);
		return;
	}

	tx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(tx_cqe);

	rx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(rx_cqe);

	tx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(tx_args);

	rx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(rx_args);

	rx_ctxts = calloc(param->num_ios, sizeof(struct fi_context));
	cr_assert_not_null(rx_ctxts);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	tx_evt_args.cqe = tx_cqe;
	rx_evt_args.cqe = rx_cqe;

	/* Issue the Sends */
	for (size_t tx_io = 0; tx_io < param->num_ios; tx_io++) {
		tx_args[tx_io].len = buf_len;
		tx_args[tx_io].tag = tx_io;
		tx_args[tx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(tx_args[tx_io].buf);
		for (size_t i = 0; i < buf_len; i++)
			tx_args[tx_io].buf[i] = i + 0xa0 + tx_io;

		do {
			ret = fi_tsend(cxit_ep, tx_args[tx_io].buf,
				       tx_args[tx_io].len, NULL,
				       cxit_ep_fi_addr, tx_args[tx_io].tag,
				       NULL);
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, NULL, 0);
				fi_cq_read(cxit_rx_cq, NULL, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend %ld: unexpected ret %d",
			     tx_io, ret);
	}

	/* Start processing Send events */
	ret = pthread_create(&tx_thread, &attr, tagged_evt_worker,
				(void *)&tx_evt_args);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	/* Optional peek to see if all send tags are found on ux list */
	if (param->peek) {
		for (size_t rx_io = 0; rx_io < param->num_ios; rx_io++) {
			if (claim)
				rx_args[rx_io].context = &rx_ctxts[rx_io];

			ret = try_peek(FI_ADDR_UNSPEC, rx_io, 0, buf_len,
				       claim ? &rx_ctxts[rx_io] : NULL, claim);
			cr_assert_eq(ret, FI_SUCCESS,
				     "peek of UX message failed");
		}
	}

	/* Issue the Receives */
	for (size_t rx_io = 0; rx_io < param->num_ios; rx_io++) {
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		do {
			if (claim) {
				iovec.iov_base = rx_args[rx_io].buf;
				iovec.iov_len = rx_args[rx_io].len;

				tmsg.msg_iov = &iovec;
				tmsg.iov_count = 1;
				tmsg.addr = FI_ADDR_UNSPEC;
				tmsg.tag = rx_args[rx_io].tag;
				tmsg.ignore = 0;
				tmsg.context = &rx_ctxts[rx_io];

				ret = fi_trecvmsg(cxit_ep, &tmsg, FI_CLAIM);
			} else {
				ret = fi_trecv(cxit_ep, rx_args[rx_io].buf,
					       rx_args[rx_io].len, NULL,
					       FI_ADDR_UNSPEC,
					       rx_args[rx_io].tag, 0, NULL);
			}
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Start processing Receive events */
	ret = pthread_create(&rx_thread, &attr, tagged_evt_worker,
			     (void *)&rx_evt_args);
	cr_assert_eq(ret, 0, "Receive thread create failed %d", ret);

	/* Wait for the RX/TX event threads to complete */
	ret = pthread_join(tx_thread, NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);

	ret = pthread_join(rx_thread, NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	/* Validate results */
	for (size_t io = 0; io < param->num_ios; io++) {
		/* Validate sent data */
		cr_expect_arr_eq(rx_args[io].buf, tx_args[io].buf, buf_len);

		validate_tx_event(&tx_cqe[io], FI_TAGGED | FI_SEND, NULL);
		validate_rx_event(&rx_cqe[io], claim ?
				  rx_args[rx_cqe[io].tag].context : NULL,
				  buf_len, FI_TAGGED | FI_RECV, NULL,
				  0, tx_args[rx_cqe[io].tag].tag);
		free(tx_args[io].buf);
		free(rx_args[io].buf);
	}

	pthread_attr_destroy(&attr);
	free(rx_cqe);
	free(tx_cqe);
	free(tx_args);
	free(rx_args);
	free(rx_ctxts);
}

ParameterizedTestParameters(tagged, multitudes)
{
	size_t param_sz;

	static struct multitudes_params params[] = {
		{.length = 1024,	/* Eager */
		 .num_ios = 10,
		 .peek = true},
		{.length = 2 * 1024,	/* Eager */
		 .num_ios = 15,
		 .peek = true},
		{.length = 4 * 1024,	/* Rendezvous */
		 .num_ios = 12,
		 .peek = true},
		{.length = 128 * 1024,	/* Rendezvous */
		 .num_ios = 25,
		 .peek = true},
		{.length = 1024,	/* Eager */
		 .num_ios = 10,
		 .peek = true,
		 .claim = true,
		},
		{.length = 2 * 1024,	/* Eager */
		 .num_ios = 15,
		 .peek = true,
		 .claim = true,
		},
		{.length = 4 * 1024,	/* Rendezvous */
		 .num_ios = 12,
		 .peek = true,
		 .claim = true,
		},
		{.length = 128 * 1024,	/* Rendezvous */
		 .num_ios = 25,
		 .peek = true,
		 .claim = true,
		},
		{.length = 8 * 1024,	/* Rendezvous ID > 8 bits */
		 .num_ios = 350,
		 .peek = true,
		 .claim = false,
		},
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct multitudes_params, params,
				   param_sz);
}

ParameterizedTest(struct multitudes_params *param, tagged, multitudes, .timeout=60)
{
	do_multitudes(param);
}

/* Use multitudes test to force transition from hardware
 * matching to software matching. LE_POOL resources should
 * be set to 60.
 */
ParameterizedTestParameters(tagged, hw2sw_multitudes)
{
	size_t param_sz;

	static struct multitudes_params params[] = {
		{.length = 1024,	/* Eager */
		 .num_ios = 100,
		 .peek = true
		},
		{.length = 2 * 2048,	/* Rendezvous */
		 .num_ios = 100,
		 .peek = true
		},
		{.length = 8 * 2048,	/* Rendezvous */
		 .num_ios = 100,
		 .peek = true
		},
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct multitudes_params, params,
				   param_sz);
}

/* This test will only require HW to SW matching transition if the
 * LE pool resources have been limited (60) and if running in HW offloaded
 * mode with RDZV offloaded and the eager long protocol is not used.
 */
ParameterizedTest(struct multitudes_params *param, tagged, hw2sw_multitudes,
		.timeout=60, .disabled=false)
{
	do_multitudes(param);
}

/* This will only test hybrid matching transition when LE resources
 * are restricted to no more than 60.
 */
Test(tagged, hw2sw_hybrid_matching, .timeout=60)
{
	int ret;
	size_t buf_len = 4096;
	struct fi_cq_tagged_entry *rx_cqe;
	struct fi_cq_tagged_entry *tx_cqe;
	struct tagged_thread_args *tx_args;
	struct tagged_thread_args *rx_args;
	pthread_t tx_thread;
	pthread_t rx_thread;
	pthread_attr_t attr;
	struct tagged_event_args tx_evt_args = {
		.cq = cxit_tx_cq,
		.io_num = 100,
	};
	struct tagged_event_args rx_evt_args = {
		.cq = cxit_rx_cq,
		.io_num = 100,
	};

	tx_cqe = calloc(100, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(tx_cqe);

	rx_cqe = calloc(100, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(rx_cqe);

	tx_args = calloc(100, sizeof(struct tagged_thread_args));
	cr_assert_not_null(tx_args);

	rx_args = calloc(100, sizeof(struct tagged_thread_args));
	cr_assert_not_null(rx_args);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	tx_evt_args.cqe = tx_cqe;
	rx_evt_args.cqe = rx_cqe;

	/* Issue 25 receives for tags 25-49 to pre-load priority list */
	for (size_t rx_io = 25; rx_io < 50; rx_io++) {
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		do {
			ret = fi_trecv(cxit_ep, rx_args[rx_io].buf,
				       rx_args[rx_io].len, NULL,
				       FI_ADDR_UNSPEC, rx_args[rx_io].tag,
				       0, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Start processing Receive events */
	ret = pthread_create(&rx_thread, &attr, tagged_evt_worker,
			     (void *)&rx_evt_args);
	cr_assert_eq(ret, 0, "Receive thread create failed %d", ret);

	/* Issue all of the Sends exhausting resources */
	for (size_t tx_io = 0; tx_io < 100; tx_io++) {
		tx_args[tx_io].len = buf_len;
		tx_args[tx_io].tag = tx_io;
		tx_args[tx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(tx_args[tx_io].buf);
		for (size_t i = 0; i < buf_len; i++)
			tx_args[tx_io].buf[i] = i + 0xa0 + tx_io;

		do {
			ret = fi_tsend(cxit_ep, tx_args[tx_io].buf,
				       tx_args[tx_io].len, NULL,
				       cxit_ep_fi_addr, tx_args[tx_io].tag,
				       NULL);
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, NULL, 0);
				fi_cq_read(cxit_rx_cq, NULL, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend %ld: unexpected ret %d",
			     tx_io, ret);
	}

	/* Start processing Send events */
	ret = pthread_create(&tx_thread, &attr, tagged_evt_worker,
				(void *)&tx_evt_args);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	/* Issue the remainder of the receives */
	for (size_t rx_io = 0; rx_io < 100; rx_io++) {
		if (rx_io >= 25 && rx_io < 50)
			continue;
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		do {
			ret = fi_trecv(cxit_ep, rx_args[rx_io].buf,
				       rx_args[rx_io].len, NULL,
				       FI_ADDR_UNSPEC, rx_args[rx_io].tag,
				       0, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Wait for the RX/TX event threads to complete */
	ret = pthread_join(tx_thread, NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);

	ret = pthread_join(rx_thread, NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	/* Validate results */
	for (size_t io = 0; io < 100; io++) {
		/* Validate sent data */
		cr_expect_arr_eq(rx_args[io].buf, tx_args[io].buf, buf_len);

		validate_tx_event(&tx_cqe[io], FI_TAGGED | FI_SEND, NULL);

		validate_rx_event(&rx_cqe[io], NULL, buf_len,
				  FI_TAGGED | FI_RECV, NULL,
				  0, tx_args[rx_cqe[io].tag].tag);

		free(tx_args[io].buf);
		free(rx_args[io].buf);
	}

	pthread_attr_destroy(&attr);
	free(rx_cqe);
	free(tx_cqe);
	free(tx_args);
	free(rx_args);
}

#define RECV_INIT 0x77
#define SEND_INIT ~RECV_INIT

void do_msg(uint8_t *send_buf, size_t send_len, uint64_t send_tag,
	    uint8_t *recv_buf, size_t recv_len, uint64_t recv_tag,
	    uint64_t recv_ignore, bool send_first, size_t buf_size,
	    bool tagged, bool wdata, uint64_t data, bool match_complete)
{
	int i, ret;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	bool sent = false,
	     recved = false,
	     truncated = false;
	struct fi_cq_err_entry err_cqe = {};
	size_t recved_len;
	static int send_cnt;
	static int recv_cnt;
	static int recv_errcnt;

	struct fi_msg_tagged tsmsg = {};
	struct fi_msg smsg = {};
	struct iovec siovec;
	uint64_t send_flags = 0;

	memset(recv_buf, RECV_INIT, buf_size);

	for (i = 0; i < buf_size; i++) {
		if (i < send_len)
			send_buf[i] = i + 0xa0;
		else
			send_buf[i] = SEND_INIT;
	}

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;

	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.context = NULL;
	smsg.data = data;

	tsmsg.msg_iov = &siovec;
	tsmsg.iov_count = 1;
	tsmsg.addr = cxit_ep_fi_addr;
	tsmsg.tag = send_tag;
	tsmsg.ignore = 0;
	tsmsg.context = NULL;
	tsmsg.data = data;

	/* FI_REMOTE_CQ_DATA flag is not strictly necessary. */
	if (wdata)
		send_flags |= FI_REMOTE_CQ_DATA;
	if (match_complete)
		send_flags |= FI_MATCH_COMPLETE;

	if (send_first) {
		if (tagged) {
			ret = fi_tsendmsg(cxit_ep, &tsmsg, send_flags);
			cr_assert_eq(ret, FI_SUCCESS,
				     "fi_tsendmsg failed %d", ret);
		} else {
			ret = fi_sendmsg(cxit_ep, &smsg, send_flags);
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

	/* Post RX buffer */

	if (tagged) {
		ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
			       FI_ADDR_UNSPEC, recv_tag, recv_ignore, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	} else {
		ret = fi_recv(cxit_ep, recv_buf, recv_len, NULL,
			      FI_ADDR_UNSPEC, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);
	}

	if (!send_first) {
		if (tagged) {
			ret = fi_tsendmsg(cxit_ep, &tsmsg, send_flags);
			cr_assert_eq(ret, FI_SUCCESS,
				     "fi_tsendmsg failed %d", ret);
		} else {
			ret = fi_sendmsg(cxit_ep, &smsg, send_flags);
			cr_assert_eq(ret, FI_SUCCESS,
				     "fi_sendmsg failed %d", ret);
		}
	}

	/* Gather both events, ensure progress on both sides. */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		if (ret == 1) {
			cr_assert_eq(recved, false);
			recved = true;
		} else if (ret == -FI_EAVAIL) {
			cr_assert_eq(recved, false);
			recved = true;
			truncated = true;

			ret = fi_cq_readerr(cxit_rx_cq, &err_cqe, 0);
			cr_assert_eq(ret, 1);
		} else {
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);
		}

		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		if (ret == 1) {
			cr_assert_eq(sent, false);
			sent = true;
		} else {
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d", ret);
		}
	} while (!(sent && recved));

	if (truncated) {
		cr_assert(err_cqe.op_context == NULL,
			  "Error RX CQE Context mismatch");
		cr_assert(err_cqe.flags ==
			  ((tagged ? FI_TAGGED : FI_MSG) | FI_RECV |
			   (wdata ? FI_REMOTE_CQ_DATA : 0UL)),
			  "Error RX CQE flags mismatch");
		cr_assert(err_cqe.len == recv_len,
			  "Invalid Error RX CQE length, got: %ld exp: %ld",
			  err_cqe.len, recv_len);
		cr_assert(err_cqe.buf == 0, "Invalid Error RX CQE address");
		cr_assert(err_cqe.data == (wdata ? data : 0UL),
			  "Invalid Error RX CQE data");
		cr_assert(err_cqe.tag == send_tag, "Invalid Error RX CQE tag");
		cr_assert(err_cqe.olen == (send_len - recv_len),
			  "Invalid Error RX CQE olen, got: %ld exp: %ld",
			  err_cqe.olen, send_len - recv_len);
		cr_assert(err_cqe.err == FI_ETRUNC,
			  "Invalid Error RX CQE code\n");
		cr_assert(err_cqe.prov_errno == C_RC_OK,
			  "Invalid Error RX CQE errno");
		cr_assert(err_cqe.err_data == NULL);
		cr_assert(err_cqe.err_data_size == 0);
		recved_len = err_cqe.len;
	} else {
		validate_rx_event(&rx_cqe, NULL, send_len,
				  (tagged ? FI_TAGGED : FI_MSG) | FI_RECV
				  | (wdata ? FI_REMOTE_CQ_DATA : 0UL),
				  NULL, wdata ? data : 0UL, send_tag);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");
		recved_len = rx_cqe.len;
	}

	validate_tx_event(&tx_cqe, (tagged ? FI_TAGGED : FI_MSG) | FI_SEND,
			  NULL);

	/* Validate sent data */
	for (i = 0; i < buf_size; i++) {
		uint8_t cmp = RECV_INIT;
		if (i < recved_len)
			cmp = send_buf[i];

		cr_expect_eq(recv_buf[i], cmp,
			     "data mismatch, len: %ld, element[%d], exp=0x%x saw=0x%x, err=%d\n",
			     recv_len, i, cmp, recv_buf[i], err++);
		if (err >= 10)
			break;
	}
	cr_assert_eq(err, 0, "%d data errors seen\n", err);

	/* Check counters */
	send_cnt++;

	if (truncated)
		recv_errcnt++;
	else
		recv_cnt++;

	while (fi_cntr_read(cxit_send_cntr) != send_cnt)
		;
	while (fi_cntr_read(cxit_recv_cntr) != recv_cnt)
		;
	while (fi_cntr_readerr(cxit_recv_cntr) != recv_errcnt)
		;

	/* Error count is 7 bits */
	if (recv_errcnt == 127) {
		recv_errcnt = 0;
		fi_cntr_seterr(cxit_recv_cntr, 0);
	}
}

#define BUF_SIZE (8*1024)
#define SEND_MIN 64
#define SEND_INC 64
#define TAG 0x333333333333
#define IGNORE_ALL (-1ULL & CXIP_TAG_MASK)
#define HDR_DATA 0xabcdabcdabcdabcd

struct tagged_rx_params {
	size_t buf_size;
	size_t send_min;
	size_t send_inc;
	uint64_t send_tag;
	int recv_len_off;
	uint64_t recv_tag;
	uint64_t ignore;
	bool ux;
	bool tagged;
	bool wdata;
	uint64_t data;
};

static struct tagged_rx_params params[] = {
	{.buf_size = BUF_SIZE, /* equal length no data */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = true},

	/* Use CQ data */

	{.buf_size = BUF_SIZE, /* truncate */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = -8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* truncate UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = -8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* truncate ignore */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = TAG,
	 .recv_len_off = -8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* truncate ignore UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = TAG,
	 .recv_len_off = -8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = true,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length ignore */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = TAG,
	 .recv_len_off = 0,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length ignore UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = TAG,
	 .recv_len_off = 0,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = true,
	 .tagged = true},
	{.buf_size = BUF_SIZE, /* excess */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess ignore */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = TAG,
	 .recv_len_off = 8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess ignore UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = TAG,
	 .recv_len_off = 8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = true,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},

	/* Un-tagged variants */

	{.buf_size = BUF_SIZE, /* equal length no data */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = false},

	/* Use CQ data */

	{.buf_size = BUF_SIZE, /* truncate */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = -8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* truncate UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = -8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* truncate ignore */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = -8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = false,
	 .tagged = true,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* truncate ignore UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = -8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = true,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length ignore */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = false,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* equal length ignore UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 0,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = true,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = false,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 8,
	 .recv_tag = 0,
	 .ignore = 0,
	 .ux = true,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess ignore */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = false,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
	{.buf_size = BUF_SIZE, /* excess ignore UX */
	 .send_min = SEND_MIN,
	 .send_inc = SEND_INC,
	 .send_tag = 0,
	 .recv_len_off = 8,
	 .recv_tag = ~TAG & CXIP_TAG_MASK,
	 .ignore = IGNORE_ALL,
	 .ux = true,
	 .tagged = false,
	 .wdata = true,
	 .data = HDR_DATA},
};

ParameterizedTestParameters(tagged, rx)
{
	size_t param_sz;

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct tagged_rx_params, params,
				   param_sz);
}

ParameterizedTest(struct tagged_rx_params *param, tagged, rx, .timeout=30)
{
	uint8_t *recv_buf,
		*send_buf;
	size_t send_len;

	recv_buf = aligned_alloc(s_page_size, param->buf_size);
	cr_assert(recv_buf);

	send_buf = aligned_alloc(s_page_size, param->buf_size);
	cr_assert(send_buf);

	for (send_len = param->send_min;
	     send_len <= param->buf_size;
	     send_len += param->send_inc) {
		do_msg(send_buf, send_len, param->send_tag,
		       recv_buf, send_len + param->recv_len_off,
		       param->recv_tag, param->ignore, param->ux,
		       param->buf_size, param->tagged,
		       param->wdata, param->data, false);
		do_msg(send_buf, send_len, param->send_tag,
		       recv_buf, send_len + param->recv_len_off,
		       param->recv_tag, param->ignore, param->ux,
		       param->buf_size, param->tagged,
		       param->wdata, param->data, true);
	}

	free(send_buf);
	free(recv_buf);
}

#define GB 1024*1024*1024
Test(tagged, rput_abort, .disabled=true)
{
	size_t recv_len = GB;
	size_t send_len = GB;
	void *recv_buf;
	void *send_buf;
	int ret;
	uint64_t val __attribute__((unused));

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
		       FI_ADDR_UNSPEC, 0, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	sleep(1);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	ret = fi_tsend(cxit_ep, send_buf, send_len,
		       NULL, cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS,
		     "fi_tsend failed %d", ret);

	sleep(1);
	val = *(uint64_t *)0;
}


Test(tagged, oflow_replenish, .timeout=180)
{
	uint8_t *recv_buf,
		*send_buf;
	size_t send_len = 1024;
	int i;

	recv_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(recv_buf);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < 6*1024+1; i++) {
		do_msg(send_buf, send_len, 0,
		       recv_buf, send_len, 0, 0,
		       true, send_len, true, false, 0, false);
	}

	free(send_buf);
	free(recv_buf);
}

/* Test outstanding send cleanup */
Test(tagged, cleanup_sends)
{
	int i, ret;
	uint8_t *send_buf;
	int send_len = 64;
	int sends = 5;

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	/* Send 64 bytes to self */
	for (i = 0; i < sends; i++) {
		ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);
	}

	/* Close Endpoint with outstanding Sends */
}

/* Test UX cleanup */
Test(tagged, ux_cleanup)
{
	int i, ret;
	uint8_t *send_buf;
	int send_len = 64;
	struct fi_cq_tagged_entry cqe;
	int sends = 5;

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	/* Send 64 bytes to self */
	for (i = 0; i < sends; i++) {
		ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
	}

	validate_tx_event(&cqe, FI_TAGGED | FI_SEND, NULL);

	/* Wait for async event indicating data has been received */
	for (i = 0 ; i < 1000; i++)
		fi_cq_readfrom(cxit_rx_cq, &cqe, 1, NULL);

	free(send_buf);

	/* Close Endpoint with UX sends on the RX Queue */
}

/* Test outstanding recv cleanup */
Test(tagged, cleanup_recvs)
{
	int i, ret;
	uint8_t *recv_buf;
	int recv_len = 64;
	int recvs = 5;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	for (i = 0; i < recvs; i++) {
		ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
			       FI_ADDR_UNSPEC, 0x0, 0x0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);
	}

	/* Close Endpoint with outstanding Receives */
}

/* Test outstanding recv cancel */
Test(tagged, cancel_recvs)
{
	int i, ret;
	uint8_t *recv_buf;
	int recv_len = 64;
	int recvs = 5;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	for (i = 0; i < recvs; i++) {
		ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
			       FI_ADDR_UNSPEC, 0x0, 0x0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);
	}

	for (i = 0; i < recvs; i++) {
		ret = fi_cancel(&cxit_ep->fid, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_cancel failed %d", ret);
	}
}

/* Test cancel with no matching recv */
Test(tagged, cancel_nomatch)
{
	int ret;

	ret = fi_cancel(&cxit_ep->fid, NULL);
	cr_assert_eq(ret, -FI_ENOENT, "fi_cancel failed to fail %d", ret);
}

/* Test outstanding recv cancel events */
Test(tagged, cancel_recvs_sync)
{
	int i, ret;
	uint8_t *recv_buf;
	int recv_len = 64;
	int recvs = 5;
	struct fi_cq_tagged_entry rx_cqe;
	struct fi_cq_err_entry err_cqe;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	for (i = 0; i < recvs; i++) {
		ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
			       FI_ADDR_UNSPEC, 0x0, 0x0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);
	}

	for (i = 0; i < recvs; i++) {
		ret = fi_cancel(&cxit_ep->fid, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_cancel failed %d", ret);
	}

	ret = fi_cancel(&cxit_ep->fid, NULL);
	cr_assert_eq(ret, -FI_ENOENT, "fi_cancel failed to fail %d", ret);

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
		cr_assert(err_cqe.flags == (FI_TAGGED | FI_RECV),
			  "Error RX CQE flags mismatch");
		cr_assert(err_cqe.err == FI_ECANCELED,
			  "Invalid Error RX CQE code\n");
		cr_assert(err_cqe.prov_errno == 0,
			  "Invalid Error RX CQE errno");
	}
}

void cxit_setup_selective_completion(void)
{
	cxit_tx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;
	cxit_rx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;

	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->op_flags = FI_COMPLETION;
	cxit_fi_hints->rx_attr->op_flags = FI_COMPLETION;
	cxit_setup_tagged();
}

/* Test selective completion behavior with RMA. */
Test(tagged_sel, selective_completion,
     .init = cxit_setup_selective_completion,
     .fini = cxit_teardown_tagged)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int buf_len = 0x1000;
	int send_len;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg_tagged smsg = {};
	struct fi_msg_tagged rmsg = {};
	struct iovec siovec;
	struct iovec riovec;
	int recv_cnt = 0;

	recv_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(recv_buf);

	riovec.iov_base = recv_buf;
	riovec.iov_len = buf_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	send_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(send_buf);

	siovec.iov_base = send_buf;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	/* Normal writes generate completions */
	for (send_len = 1; send_len <= buf_len; send_len <<= 1) {
		bool sent = false;
		bool rcved = false;

		memset(recv_buf, 0, send_len);
		for (i = 0; i < send_len; i++)
			send_buf[i] = i + 0xa0;

		/* Post RX buffer */
		ret = fi_trecv(cxit_ep, recv_buf, send_len, NULL,
			       FI_ADDR_UNSPEC, 0, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
		recv_cnt++;

		/* Send to self */
		ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

		/* Wait for async events indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
			if (ret == 1)
				rcved = true;

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
			if (ret == 1)
				sent = true;
		} while (!(sent && rcved));

		validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 0);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");
		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");
	}

	/* Request completions from fi_writemsg */
	for (send_len = 1; send_len <= buf_len; send_len <<= 1) {
		bool sent = false;
		bool rcved = false;

		memset(recv_buf, 0, send_len);
		for (i = 0; i < send_len; i++)
			send_buf[i] = i + 0xa0;

		/* Post RX buffer */
		ret = fi_trecvmsg(cxit_ep, &rmsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);
		recv_cnt++;

		/* Send to self */
		siovec.iov_len = send_len;
		ret = fi_tsendmsg(cxit_ep, &smsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

		/* Wait for async events indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
			if (ret == 1)
				rcved = true;

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
			if (ret == 1)
				sent = true;
		} while (!(sent && rcved));

		validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 0);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");
		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");
	}

	/* Suppress completions using fi_writemsg */
	for (send_len = 1; send_len <= buf_len; send_len <<= 1) {
		memset(recv_buf, 0, send_len);
		for (i = 0; i < send_len; i++)
			send_buf[i] = i + 0xa0;

		/* Post RX buffer */
		riovec.iov_len = send_len;
		ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);
		recv_cnt++;

		/* Send to self */
		siovec.iov_len = send_len;
		ret = fi_tsendmsg(cxit_ep, &smsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

		/* Wait for async events indicating data has been received */
		while (fi_cntr_read(cxit_recv_cntr) != recv_cnt)
			;

		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);

		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	/* Inject never generates an event */

	send_len = 8;
	/* Post RX buffer */
	ret = fi_trecv(cxit_ep, recv_buf, send_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	recv_cnt++;

	/* Send 64 bytes to self */
	ret = fi_tinject(cxit_ep, send_buf, send_len, cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
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

void cxit_setup_selective_completion_suppress(void)
{
	cxit_tx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;
	cxit_rx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;

	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->op_flags = 0;
	cxit_fi_hints->rx_attr->op_flags = 0;
	cxit_setup_tagged();
}

/* Test selective completion behavior with RMA. */
Test(tagged_sel, selective_completion_suppress,
     .init = cxit_setup_selective_completion_suppress,
     .fini = cxit_teardown_tagged)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int buf_len = 0x1000;
	int send_len;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg_tagged smsg = {};
	struct fi_msg_tagged rmsg = {};
	struct iovec siovec;
	struct iovec riovec;
	int recv_cnt = 0;

	recv_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(recv_buf);

	riovec.iov_base = recv_buf;
	riovec.iov_len = buf_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	send_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert(send_buf);

	siovec.iov_base = send_buf;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	/* Normal writes do not generate completions */
	for (send_len = 1; send_len <= buf_len; send_len <<= 1) {
		memset(recv_buf, 0, send_len);
		for (i = 0; i < send_len; i++)
			send_buf[i] = i + 0xa0;

		/* Post RX buffer */
		ret = fi_trecv(cxit_ep, recv_buf, send_len, NULL,
			       FI_ADDR_UNSPEC, 0, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
		recv_cnt++;

		/* Send to self */
		ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

		/* Wait for async events indicating data has been received */
		while (fi_cntr_read(cxit_recv_cntr) != recv_cnt)
			;

		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);

		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	/* Request completions from fi_writemsg */
	for (send_len = 1; send_len <= buf_len; send_len <<= 1) {
		bool sent = false;
		bool rcved = false;

		memset(recv_buf, 0, send_len);
		for (i = 0; i < send_len; i++)
			send_buf[i] = i + 0xa0;

		/* Post RX buffer */
		riovec.iov_len = send_len;
		ret = fi_trecvmsg(cxit_ep, &rmsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);
		recv_cnt++;

		/* Send to self */
		siovec.iov_len = send_len;
		ret = fi_tsendmsg(cxit_ep, &smsg, FI_COMPLETION);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

		/* Wait for async events indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
			if (ret == 1)
				rcved = true;

			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
			if (ret == 1)
				sent = true;
		} while (!(sent && rcved));

		validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 0);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");
		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");
	}

	/* Suppress completions using fi_writemsg */
	for (send_len = 1; send_len <= buf_len; send_len <<= 1) {
		memset(recv_buf, 0, send_len);
		for (i = 0; i < send_len; i++)
			send_buf[i] = i + 0xa0;

		/* Post RX buffer */
		riovec.iov_len = send_len;
		ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);
		recv_cnt++;

		/* Send to self */
		siovec.iov_len = send_len;
		ret = fi_tsendmsg(cxit_ep, &smsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

		/* Wait for async events indicating data has been received */
		while (fi_cntr_read(cxit_recv_cntr) != recv_cnt)
			;


		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				     "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				     i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);

		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	/* Inject never generates an event */

	send_len = 8;
	/* Post RX buffer */
	ret = fi_trecv(cxit_ep, recv_buf, send_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	recv_cnt++;

	/* Send 64 bytes to self */
	ret = fi_tinject(cxit_ep, send_buf, send_len, cxit_ep_fi_addr, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Wait for async events indicating data has been received */
	while (fi_cntr_read(cxit_recv_cntr) != recv_cnt)
		;

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

	ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	free(send_buf);
	free(recv_buf);
}

/* Test match complete */
Test(tagged, match_comp)
{
	int i, j, ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
	struct iovec riovec;
	struct iovec siovec;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	for (j = 0; j < 100; j++) {
		ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

		ret = fi_tsendmsg(cxit_ep, &smsg, FI_MATCH_COMPLETE);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 0);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

		/* Validate sent data */
		for (i = 0; i < send_len; i++) {
			cr_expect_eq(recv_buf[i], send_buf[i],
				  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
				  i, send_buf[i], recv_buf[i], err++);
		}
		cr_assert_eq(err, 0, "Data errors seen\n");

		/* UX */

		ret = fi_tsendmsg(cxit_ep, &smsg, FI_MATCH_COMPLETE);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

		/* Ensure no TX event is generated */
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert(ret == -FI_EAGAIN);

		ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 0);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);
	}

	free(send_buf);
	free(recv_buf);
}

/* Test eager Send with FI_MORE */
Test(tagged, esend_more)
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
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
	struct iovec riovec;
	struct iovec siovec;

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

	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	/* Post two Receives */
	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	riovec.iov_base = recv_buf2;
	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_MORE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Ensure no completion before the doorbell ring */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN,
			     "write failed %d", ret);
	} while (i++ < 100000);

	ret = fi_tsendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Gather 2 Receive events */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Gather 2 Send events */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(recv_buf2[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf2[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test rendezvous Send with FI_MORE */
Test(tagged, rsend_more)
{
	int i, ret;
	uint8_t *recv_buf,
		*recv_buf2,
		*send_buf;
	int recv_len = 0x1000;
	int send_len = 0x1000;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
	struct iovec riovec;
	struct iovec siovec;

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

	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	/* Post two Receives */
	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	riovec.iov_base = recv_buf2;
	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	ret = fi_tsendmsg(cxit_ep, &smsg, FI_MORE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Ensure no completion before the doorbell ring */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN,
			     "write failed %d", ret);
	} while (i++ < 100000);

	ret = fi_tsendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Gather 2 Receive events */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Gather 2 Send events */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(recv_buf2[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf2[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test Receive with FI_MORE */
Test(tagged, recv_more)
{
	int i, ret;
	uint8_t *recv_buf,
		*recv_buf2,
		*send_buf;
	int recv_len = 0x2000;
	int send_len = 0x2000;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct fi_msg_tagged rmsg = {};
	struct fi_msg_tagged smsg = {};
	struct iovec riovec;
	struct iovec siovec;
	struct cxip_ep *ep = container_of(cxit_ep, struct cxip_ep, ep.fid);

	/* FI_MORE has no meaning if receives are not offloaded */
	if (!ep->ep_obj->rxc->msg_offload) {
		cr_assert(1);
		return;
	}

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

	riovec.iov_base = recv_buf;
	riovec.iov_len = recv_len;
	rmsg.msg_iov = &riovec;
	rmsg.iov_count = 1;
	rmsg.addr = FI_ADDR_UNSPEC;
	rmsg.tag = 0;
	rmsg.ignore = 0;
	rmsg.context = NULL;

	siovec.iov_base = send_buf;
	siovec.iov_len = send_len;
	smsg.msg_iov = &siovec;
	smsg.iov_count = 1;
	smsg.addr = cxit_ep_fi_addr;
	smsg.tag = 0;
	smsg.ignore = 0;
	smsg.context = NULL;

	/* Perform 2 Sends */
	ret = fi_tsendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	ret = fi_tsendmsg(cxit_ep, &smsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsendmsg failed %d", ret);

	/* Post two Receives */
	ret = fi_trecvmsg(cxit_ep, &rmsg, FI_MORE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	/* Ensure no completion before the doorbell ring */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN,
			     "write failed %d", ret);
	} while (i++ < 100000);

	riovec.iov_base = recv_buf2;
	ret = fi_trecvmsg(cxit_ep, &rmsg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecvmsg failed %d", ret);

	/* Gather 2 Receive events */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Gather 2 Send events */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	/* Validate sent data */
	for (i = 0; i < send_len; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
		cr_expect_eq(recv_buf2[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf2[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	free(send_buf);
	free(recv_buf);
}

/* Test flow control.
 *
 * Perform enough Sends to overwhelm target LEs. Flow control recovery is
 * transparent.
 *
 * Post matching Receives and check data to validate correct ordering amid flow
 * control recovery.
 */
Test(tagged, fc, .timeout = 180)
{
	int i, j, ret, tx_ret;
	uint8_t *send_bufs;
	uint8_t *send_buf;
	int send_len = 64;
	uint8_t *recv_buf;
	int recv_len = 64;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	int nsends_concurrent = 3; /* must be less than the LE pool min. */
	int nsends = 14000;
	int sends = 0;
	uint64_t tag = 0xbeef;
	fi_addr_t from;

	send_bufs = aligned_alloc(s_page_size, send_len * nsends_concurrent);
	cr_assert(send_bufs);

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	for (i = 0; i < nsends_concurrent - 1; i++) {
		send_buf = send_bufs + (i % nsends_concurrent) * send_len;
		memset(send_buf, i, send_len);

		tx_ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, tag, NULL);
	}

	for (i = nsends_concurrent - 1; i < nsends; i++) {
		send_buf = send_bufs + (i % nsends_concurrent) * send_len;
		memset(send_buf, i, send_len);

		do {
			tx_ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
				       cxit_ep_fi_addr, tag, NULL);

			/* Progress RX to avoid EQ drops */
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d",
				     ret);

			/* Just progress */
			fi_cq_read(cxit_tx_cq, NULL, 0);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, FI_SUCCESS, "fi_tsend failed %d", tx_ret);

		do {
			tx_ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);

			/* Progress RX to avoid EQ drops */
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d",
				     ret);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, 1, "fi_cq_read unexpected value %d",
			     tx_ret);

		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

		if (!(++sends % 1000))
			printf("%u Sends complete.\n", sends);
	}

	for (i = 0; i < nsends_concurrent - 1; i++) {
		do {
			tx_ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);

			/* Progress RX to avoid EQ drops */
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			cr_assert_eq(ret, -FI_EAGAIN,
				     "fi_cq_read unexpected value %d",
				     ret);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, 1, "fi_cq_read unexpected value %d",
			     tx_ret);

		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

		if (!(++sends % 1000))
			printf("%u Sends complete.\n", sends);
	}

	for (i = 0; i < nsends; i++) {
		do {
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			assert(ret == -FI_EAGAIN);

			ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
				       FI_ADDR_UNSPEC, tag, 0, NULL);
		} while (ret == -FI_EAGAIN);

		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);

		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_rx_event(&rx_cqe, NULL, recv_len, FI_TAGGED | FI_RECV,
				  NULL, 0, tag);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

		for (j = 0; j < recv_len; j++) {
			cr_assert_eq(recv_buf[j], (uint8_t)i,
				     "data mismatch, recv: %d element[%d], exp=%d saw=%d\n",
				     i, j, (uint8_t)i, recv_buf[j]);
		}
	}

	free(send_bufs);
	free(recv_buf);
}

#define FC_TRANS 100

static void *fc_sender(void *data)
{
	int i, tx_ret;
	uint8_t *send_buf;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe;

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < FC_TRANS; i++) {
		memset(send_buf, i, send_len);

		/* Send 64 bytes to self */
		do {
			tx_ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
				       cxit_ep_fi_addr, 0xa, NULL);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, FI_SUCCESS, "fi_tsend failed %d", tx_ret);

		do {
			tx_ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		} while (tx_ret == -FI_EAGAIN);

		cr_assert_eq(tx_ret, 1, "fi_cq_read unexpected value %d",
			     tx_ret);

		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);
	}

	free(send_buf);

	pthread_exit(NULL);
}

static void *fc_recver(void *data)
{
	int i, j, ret;
	uint8_t *recv_buf;
	int recv_len = 64;
	struct fi_cq_tagged_entry rx_cqe;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	for (i = 0; i < 5; i++) {
		sleep(1);

		/* Progress RX to avoid EQ drops */
		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN,
			     "fi_cq_read unexpected value %d",
			     ret);
	}

	for (i = 0; i < FC_TRANS; i++) {
		memset(recv_buf, 0, recv_len);

		/* Send 64 bytes to self */

		do {
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
			assert(ret == -FI_EAGAIN);

			ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
				       FI_ADDR_UNSPEC, 0xa, 0, NULL);
		} while (ret == -FI_EAGAIN);

		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

		do {
			ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
		} while (ret == -FI_EAGAIN);

		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_rx_event(&rx_cqe, NULL, recv_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 0xa);

		for (j = 0; j < recv_len; j++) {
			cr_assert_eq(recv_buf[j], i,
				     "data mismatch, element[%d], exp=%d saw=%d\n",
				     j, i, recv_buf[j]);
		}
	}

	free(recv_buf);

	pthread_exit(NULL);
}

/*
 * Multi-threaded flow control test.
 *
 * Run sender and receiver threads. Start sender first to allow it to overwhelm
 * target LEs (set artificially low). Software matching is exercised while the
 * receiver catches up. Matching is a hybrid of SW/HW as threads race to
 * finish.
 *
 * Run with driver le_pool_max set below FC_TRANS.
 */
Test(tagged, fc_mt)
{
	pthread_t send_thread;
	pthread_t recv_thread;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	ret = pthread_create(&send_thread, &attr, fc_sender, NULL);
	cr_assert_eq(ret, 0);

	ret = pthread_create(&recv_thread, &attr, fc_recver, NULL);
	cr_assert_eq(ret, 0);

	ret = pthread_join(recv_thread, NULL);
	cr_assert_eq(ret, 0);

	ret = pthread_join(send_thread, NULL);
	cr_assert_eq(ret, 0);

	pthread_attr_destroy(&attr);
}

/* Post a bunch of receives to cause append failures. */
Test(tagged, fc_too_many_recv_early_close)
{
	void *recv_buf;
	size_t recv_len = 1;
	int i;
	int ret;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);

	for (i = 0; i < 50; i++) {
		do {
			ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
				       FI_ADDR_UNSPEC, 0xa, 0, NULL);
			/* Just progress */
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);

		assert(ret == FI_SUCCESS);
	}

	/* Early endpoint close. */
	ret = fi_close(&cxit_ep->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close endpoint");
	cxit_ep = NULL;

	free(recv_buf);
}

#define RDZV_FC_ITERS 100
#define RDZV_FC_BATCH 5

static void *rdzv_fc_sender(void *data)
{
	int i, j, tx_ret;
	int send_id;
	uint8_t *send_bufs;
	uint8_t *send_buf;
	long send_len = (long)data;
	struct fi_cq_tagged_entry tx_cqe;
	int batch_size = RDZV_FC_BATCH;

	send_bufs = aligned_alloc(s_page_size, send_len * batch_size);
	cr_assert(send_bufs);

	for (i = 0; i < RDZV_FC_ITERS; i++) {
		for (j = 0; j < batch_size; j++) {
			send_id = i * batch_size + j;
			send_buf = &send_bufs[j * send_len];
			memset(send_buf, send_id, send_len);

			do {
				tx_ret = fi_tsend(cxit_ep, send_buf, send_len,
						  NULL, cxit_ep_fi_addr,
						  send_id, NULL);

				if (tx_ret == -FI_EAGAIN) {
					fi_cq_read(cxit_tx_cq, &tx_cqe, 0);
					sched_yield();
				}
			} while (tx_ret == -FI_EAGAIN);

			cr_assert_eq(tx_ret, FI_SUCCESS, "fi_tsend failed %d",
				     tx_ret);
		}

		for (j = 0; j < batch_size; j++) {
			do {
				tx_ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);

				if (tx_ret == -FI_EAGAIN)
					sched_yield();
			} while (tx_ret == -FI_EAGAIN);

			cr_assert_eq(tx_ret, 1,
				     "fi_cq_read unexpected value %d",
				     tx_ret);

			validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);
		}
	}

	free(send_bufs);

	pthread_exit(NULL);
}

static void *rdzv_fc_recver(void *data)
{
	int i, j, k, ret;
	int recv_id;
	uint8_t *recv_bufs;
	uint8_t *recv_buf;
	long recv_len = (long)data;
	struct fi_cq_tagged_entry rx_cqe;
	int batch_size = RDZV_FC_BATCH;

	recv_bufs = aligned_alloc(s_page_size, recv_len * batch_size);
	cr_assert(recv_bufs);

	/* Let Sender get ahead and land some UX messages */
	sleep(1);

	for (i = 0; i < RDZV_FC_ITERS; i++) {

		for (j = 0; j < batch_size; j++) {
			recv_id = i * batch_size + j;
			recv_buf = &recv_bufs[j * recv_len];
			memset(recv_buf, -1, recv_len);

			do {
				ret = fi_trecv(cxit_ep, recv_buf, recv_len,
					       NULL, FI_ADDR_UNSPEC, recv_id,
					       0, NULL);

				if (ret == -FI_EAGAIN) {
					fi_cq_read(cxit_rx_cq, &rx_cqe, 0);
					sched_yield();
				}
			} while (ret == -FI_EAGAIN);

			cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d",
				     ret);

			do {
				ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);

				if (ret == -FI_EAGAIN)
					sched_yield();
			} while (ret == -FI_EAGAIN);

			cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d",
				     ret);

			validate_rx_event(&rx_cqe, NULL, recv_len,
					  FI_TAGGED | FI_RECV,
					  NULL, 0, rx_cqe.tag);

			recv_id = rx_cqe.tag % batch_size;
			recv_buf = &recv_bufs[recv_id * recv_len];
			for (k = 0; k < recv_len; k++) {
				cr_assert_eq(recv_buf[k], (uint8_t)rx_cqe.tag,
					     "data mismatch, element[%d], exp=%d saw=%d\n",
					     k, (uint8_t)rx_cqe.tag,
					     recv_buf[k]);
			}
		}
	}

	free(recv_bufs);

	pthread_exit(NULL);
}

/*
 * Rendezvous Send multi-threaded flow control test.
 *
 * Run with driver le_pool_max set just above RDZV_FC_BATCH.
 */
Test(tagged, rdzv_fc_mt, .timeout = 60)
{
	pthread_t send_thread;
	pthread_t recv_thread;
	pthread_attr_t attr;
	int ret;
	long xfer_len;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	for (xfer_len = 64; xfer_len <= 4*1024; xfer_len <<= 2) {
		ret = pthread_create(&send_thread, &attr, rdzv_fc_sender,
				     (void *)xfer_len);
		cr_assert_eq(ret, 0);

		ret = pthread_create(&recv_thread, &attr, rdzv_fc_recver,
				     (void *)xfer_len);
		cr_assert_eq(ret, 0);

		ret = pthread_join(recv_thread, NULL);
		cr_assert_eq(ret, 0);

		ret = pthread_join(send_thread, NULL);
		cr_assert_eq(ret, 0);

		printf("%ld byte Sends complete\n", xfer_len);
	}

	pthread_attr_destroy(&attr);
}

Test(tagged, NC2192)
{
	int i, ret;
	uint8_t *recv_buf,
		*send_buf;
	int send_len = CXIP_RDZV_THRESHOLD - 1;
	int recv_len = send_len;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	fi_addr_t from;
	int sends = (CXIP_OFLOW_BUF_SIZE - CXIP_RDZV_THRESHOLD) / send_len + 1;

	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	/* Consume 1 oflow byte */
	ret = fi_tsend(cxit_ep, send_buf, 1, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert(ret == FI_SUCCESS);

	for (i = 0; i < sends; i++) {
		do {
			ret = fi_tsend(cxit_ep, send_buf, send_len, NULL,
				       cxit_ep_fi_addr, 1, NULL);
			/* progress */
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, &tx_cqe, 0);
				fi_cq_read(cxit_rx_cq, &rx_cqe, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert(ret == FI_SUCCESS);
	}


	/* Force processing in software mode */
	fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	for (i = 0; i < sends + 1; i++) {
		fi_cq_read(cxit_tx_cq, &tx_cqe, 0);
		fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

		ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
		cr_assert(ret == 1);

		validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);
	}

	for (i = 0; i < sends; i++) {
		do {
			ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
				       FI_ADDR_UNSPEC, 1, 0, NULL);
			/* progress */
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, &tx_cqe, 0);
				fi_cq_read(cxit_rx_cq, &rx_cqe, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert(ret == FI_SUCCESS);
	}

	for (i = 0; i < sends; i++) {
		/* Wait for async event indicating data has been received */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
		cr_assert(ret == 1);

		validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV,
				  NULL, 0, 1);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");
	}

	/* Match the 1 byte Send */
	do {
		ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL,
			       FI_ADDR_UNSPEC, 0, 0, NULL);
		/* progress */
		if (ret == -FI_EAGAIN)
			fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	} while (ret == -FI_EAGAIN);
	cr_assert(ret == FI_SUCCESS);

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == 1);

	validate_rx_event(&rx_cqe, NULL, 1, FI_TAGGED | FI_RECV, NULL, 0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	free(send_buf);
	free(recv_buf);
}

TestSuite(tagged_tclass, .init = cxit_setup_tx_alias_tagged,
	  .fini = cxit_teardown_tx_alias_tagged,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Simple send using both the EP and alias EP with new TC */
Test(tagged_tclass, ping)
{
	int ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	uint32_t tclass = FI_TC_LOW_LATENCY;
	fi_addr_t from;

	recv_buf = aligned_alloc(s_page_size, recv_len * 2);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len * 2);

	send_buf = aligned_alloc(s_page_size, send_len * 2);
	cr_assert(send_buf);

	/* Post RX buffers */
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	ret = fi_trecv(cxit_ep, recv_buf + recv_len, recv_len, NULL,
		       FI_ADDR_UNSPEC, 0, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);

	/* Update EP alias traffic class */
	ret = fi_set_val(&cxit_tx_alias_ep->fid, FI_OPT_CXI_SET_TCLASS,
			 (void *)&tclass);
	cr_assert_eq(ret, FI_SUCCESS, "fi_set_val failed %d for tc %d\n",
		     ret, tclass);


	/* Send 64 bytes to self */
	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, cxit_ep_fi_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	ret = fi_tsend(cxit_tx_alias_ep, send_buf + send_len, send_len, NULL,
		       cxit_ep_fi_addr, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend for alias failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, send_len, FI_TAGGED | FI_RECV, NULL,
			  0, 0);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);
	validate_tx_event(&tx_cqe, FI_TAGGED | FI_SEND, NULL);

	free(send_buf);
	free(recv_buf);
}

/* Various tagged protocols using both the original endpoint
 * and an alias endpoint modified to use the specified tclass.
 *
 * Note that receive order is not expected between the original
 * and alias EP; tags are used for getting completions.
 */
struct multi_tc_params {
	size_t length;
	size_t num_ios;
	uint32_t tclass;
	uint32_t alias_mask;
	bool peek;
};

void do_multi_tc(struct multi_tc_params *param)
{
	int ret;
	size_t buf_len = param->length;
	struct fid_ep *ep;
	struct fi_cq_tagged_entry *rx_cqe;
	struct fi_cq_tagged_entry *tx_cqe;
	struct tagged_thread_args *tx_args;
	struct tagged_thread_args *rx_args;
	pthread_t tx_thread;
	pthread_t rx_thread;
	pthread_attr_t attr;
	struct tagged_event_args tx_evt_args = {
		.cq = cxit_tx_cq,
		.io_num = param->num_ios,
	};
	struct tagged_event_args rx_evt_args = {
		.cq = cxit_rx_cq,
		.io_num = param->num_ios,
	};

	tx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(tx_cqe);

	rx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(rx_cqe);

	tx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(tx_args);

	rx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(rx_args);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	tx_evt_args.cqe = tx_cqe;
	rx_evt_args.cqe = rx_cqe;

	/* Set alias EP traffic class */
	ret = fi_set_val(&cxit_tx_alias_ep->fid, FI_OPT_CXI_SET_TCLASS,
			 &param->tclass);
	cr_assert_eq(ret, FI_SUCCESS, "fi_set_val traffic class");

	/* Issue the Sends */
	for (size_t tx_io = 0; tx_io < param->num_ios; tx_io++) {
		tx_args[tx_io].len = buf_len;
		tx_args[tx_io].tag = tx_io;
		tx_args[tx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(tx_args[tx_io].buf);
		for (size_t i = 0; i < buf_len; i++)
			tx_args[tx_io].buf[i] = i + 0xa0 + tx_io;

		ep = tx_io & param->alias_mask ? cxit_tx_alias_ep : cxit_ep;
		do {
			ret = fi_tsend(ep, tx_args[tx_io].buf,
				       tx_args[tx_io].len, NULL,
				       cxit_ep_fi_addr, tx_args[tx_io].tag,
				       NULL);
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, NULL, 0);
				fi_cq_read(cxit_rx_cq, NULL, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend %ld: unexpected ret %d",
			     tx_io, ret);
	}

	/* Start processing Send events */
	ret = pthread_create(&tx_thread, &attr, tagged_evt_worker,
				(void *)&tx_evt_args);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	sleep(1);

	/* Force onloading of UX entries if operating in SW EP mode */
	fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	/* Optional peek to see if all send tags are found on ux list */
	if (param->peek) {
		for (size_t rx_io = 0; rx_io < param->num_ios; rx_io++) {
			ret = try_peek(FI_ADDR_UNSPEC, rx_io, 0, buf_len,
				       NULL, false);
			cr_assert_eq(ret, FI_SUCCESS,
				     "peek of UX message failed");
		}
	}

	/* Issue the Receives */
	for (size_t rx_io = 0; rx_io < param->num_ios; rx_io++) {
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		do {
			ret = fi_trecv(cxit_ep, rx_args[rx_io].buf,
				       rx_args[rx_io].len, NULL,
				       FI_ADDR_UNSPEC, rx_args[rx_io].tag,
				       0, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Start processing Receive events */
	ret = pthread_create(&rx_thread, &attr, tagged_evt_worker,
			     (void *)&rx_evt_args);
	cr_assert_eq(ret, 0, "Receive thread create failed %d", ret);

	/* Wait for the RX/TX event threads to complete */
	ret = pthread_join(tx_thread, NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);

	ret = pthread_join(rx_thread, NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	/* Validate results */
	for (size_t io = 0; io < param->num_ios; io++) {
		/* Validate sent data */
		cr_expect_arr_eq(rx_args[io].buf, tx_args[io].buf, buf_len);

		validate_tx_event(&tx_cqe[io], FI_TAGGED | FI_SEND, NULL);

		validate_rx_event(&rx_cqe[io], NULL, buf_len,
				  FI_TAGGED | FI_RECV, NULL,
				  0, tx_args[rx_cqe[io].tag].tag);

		free(tx_args[io].buf);
		free(rx_args[io].buf);
	}

	pthread_attr_destroy(&attr);
	free(rx_cqe);
	free(tx_cqe);
	free(tx_args);
	free(rx_args);
}

ParameterizedTestParameters(tagged_tclass, multi_tc)
{
	size_t param_sz;

	static struct multi_tc_params params[] = {
		{.length = 64,		/* Eager IDC */
		 .num_ios = 10,
		 .tclass = FI_TC_LOW_LATENCY,
		 .peek = true,
		 .alias_mask = 0x1},
		{.length = 64,		/* Eager IDC */
		 .num_ios = 10,
		 .tclass = FI_TC_LOW_LATENCY,
		 .peek = true,
		 .alias_mask = 0x3},
		{.length = 2 * 1024,	/* Eager */
		 .num_ios = 15,
		 .tclass = FI_TC_LOW_LATENCY,
		 .peek = true,
		 .alias_mask = 0x1},
		{.length = 4 * 1024,	/* Rendezvous */
		 .num_ios = 12,
		 .tclass = FI_TC_LOW_LATENCY,
		 .peek = true,
		 .alias_mask = 0x1},
		{.length = 128 * 1024,	/* Rendezvous */
		 .num_ios = 25,
		 .tclass = FI_TC_LOW_LATENCY,
		 .peek = true,
		 .alias_mask = 0x1},
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct multi_tc_params, params,
				   param_sz);
}

ParameterizedTest(struct multi_tc_params *param, tagged_tclass, multi_tc,
		  .timeout = 60)
{
	do_multi_tc(param);
}

TestSuite(tagged_src_err, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(tagged_src_err, cap_not_requested)
{
	struct fi_info *info;
	int ret;

	/* No hints, both FI_SOURCE and FI_SOURCE_ERR should be removed
	 * since they are secondary capabilities that impact performance.
	 */
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, NULL,
			 &info);
	cr_assert(ret == FI_SUCCESS);
	cr_assert_eq(info->caps & FI_SOURCE, 0, "FI_SOURCE");
	cr_assert_eq(info->caps & FI_SOURCE_ERR, 0, "FI_SOURCE_ERR");
	fi_freeinfo(info);

	cxit_setup_getinfo();
	cxit_fi_hints->caps = 0;

	/* No caps, both FI_SOURCE and FI_SOURCE_ERR should not be set since
	 * they are secondary capabilities and they impact performance.
	 */
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	cr_assert_eq(info->caps & FI_SOURCE, 0, "FI_SOURCE");
	cr_assert_eq(info->caps & FI_SOURCE_ERR, 0, "FI_SOURCE_ERR");

	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(tagged_src_err, hints_check)
{
	struct fi_info *info;
	int ret;

	/* If only FI_SOURCE then FI_SOURCE_ERR should not be set */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG | FI_SOURCE;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	cr_assert_eq(info->caps & FI_SOURCE, FI_SOURCE, "FI_SOURCE");
	cr_assert_eq(info->caps & FI_SOURCE_ERR, 0, "FI_SOURCE_ERR");

	fi_freeinfo(info);
	cxit_teardown_getinfo();

	/* Validate FI_SOURCE are set if FI_SOURCE_ERR specified in hints */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG | FI_SOURCE | FI_SOURCE_ERR;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	cr_assert_eq(info->caps & FI_SOURCE, FI_SOURCE, "FI_SOURCE");
	cr_assert_eq(info->caps & FI_SOURCE_ERR, FI_SOURCE_ERR,
		     "FI_SOURCE_ERR");
	fi_freeinfo(info);
	cxit_teardown_getinfo();

	/* Verify that if hints are specified, but do not include FI_SOURCE
	 * FI_SOURCE_ERR in capabilities they are not returned.
	 */
	cxit_setup_getinfo();
	cxit_fi_hints->caps = FI_MSG;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == FI_SUCCESS);

	cr_assert_eq(info->caps & FI_SOURCE, 0, "FI_SOURCE");
	cr_assert_eq(info->caps & FI_SOURCE_ERR, 0, "FI_SOURCE_ERR");
	fi_freeinfo(info);
	cxit_teardown_getinfo();
}

Test(tagged_src_err, invalid_use)
{
	struct fi_info *info;
	int ret;

	cxit_setup_getinfo();

	/* If no FI_SOURCE then FI_SOURCE_ERR is not allowed */
	cxit_fi_hints->caps = FI_MSG | FI_SOURCE_ERR;
	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 cxit_node, cxit_service, cxit_flags, cxit_fi_hints,
			 &info);
	cr_assert(ret == -FI_ENODATA);

	cxit_teardown_getinfo();
}

Test(tagged_src_err, addr)
{
	struct fid_ep *fid_ep;
	struct fid_eq *fid_eq;
	struct fi_eq_attr eq_attr = {
		.size = 32,
		.flags = FI_WRITE,
		.wait_obj = FI_WAIT_NONE
	};
	struct fid_cq *fid_tx_cq;
	struct fid_cq *fid_rx_cq;
	struct fid_av *fid_av;
	struct cxip_addr ep_addr;
	fi_addr_t fi_dest_ep_addr;
	fi_addr_t fi_src_err_ep_addr;
	size_t addr_len = sizeof(ep_addr);
	int ret;
	uint8_t *recv_buf,
		*send_buf;
	int recv_len = 64;
	int send_len = 64;
	struct fi_cq_tagged_entry tx_cqe,
				  rx_cqe;
	struct fi_cq_err_entry err_entry;
	int i;

	/* Create first EP - adds itself to the AV */
	cxit_setup_enabled_ep();
	ret = fi_av_insert(cxit_av, (void *)&cxit_ep_addr, 1, NULL, 0, NULL);
	cr_assert_eq(ret, 1, "First EP AV insert of self %d\n", ret);

	/* Create second EP and resources */
	cr_assert_eq(cxit_fi->caps &
		     (FI_TAGGED | FI_SOURCE | FI_SOURCE_ERR | FI_DIRECTED_RECV),
		     (FI_TAGGED | FI_SOURCE | FI_SOURCE_ERR | FI_DIRECTED_RECV),
		     "info->caps");
	ret = fi_endpoint(cxit_domain, cxit_fi, &fid_ep, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP %d", ret);
	ret = fi_eq_open(cxit_fabric, &eq_attr, &fid_eq, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP EQ %d", ret);
	ret = fi_ep_bind(fid_ep, &fid_eq->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "Second PE EQ bind %d", ret);
	ret = fi_cq_open(cxit_domain, &cxit_tx_cq_attr, &fid_tx_cq, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP TXCQ %d", ret);
	ret = fi_cq_open(cxit_domain, &cxit_rx_cq_attr, &fid_rx_cq, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP RXCQ %d", ret);
	ret = fi_ep_bind(fid_ep, &fid_tx_cq->fid, FI_TRANSMIT);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP bind TXCQ %d", ret);
	ret = fi_ep_bind(fid_ep, &fid_rx_cq->fid, FI_RECV);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP bind RXCQ %d", ret);

	/* Needs it's own AV */
	ret = fi_av_open(cxit_domain, &cxit_av_attr, &fid_av, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "Second AV %d\n", ret);
	ret = fi_ep_bind(fid_ep, &fid_av->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "Secnd AV bind %d\n", ret);

	ret = fi_enable(fid_ep);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP enable %d\n", ret);
	ret = fi_getname(&fid_ep->fid, &ep_addr, &addr_len);
	cr_assert_eq(ret, FI_SUCCESS, "Second EP getname %d\n", ret);

	/* Insert second EP address into to both AV, but do not insert
	 * the first EP address into the the second EP AV.
	 */
	ret = fi_av_insert(fid_av, (void *)&ep_addr, 1, 0,
			   0, NULL);
	cr_assert_eq(ret, 1, "Second EP AV insert local %d\n", ret);

	ret = fi_av_insert(cxit_av, (void *)&ep_addr, 1, &fi_dest_ep_addr,
			   0, NULL);
	cr_assert_eq(ret, 1, "Fisrt EP AV insert second EP %d\n", ret);

	/* Setup buffers */
	recv_buf = aligned_alloc(s_page_size, recv_len);
	cr_assert(recv_buf);
	memset(recv_buf, 0, recv_len);

	send_buf = aligned_alloc(s_page_size, send_len);
	cr_assert(send_buf);

	for (i = 0; i < send_len; i++)
		send_buf[i] = i + 0xa0;

	/* Test address not found EP1->EP2 */
	ret = fi_trecv(fid_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	sleep(1);

	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, fi_dest_ep_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Receive should get an -FI_EAVAIL with source error info */
	ret = cxit_await_completion(fid_rx_cq, &rx_cqe);
	cr_assert_eq(ret, -FI_EAVAIL);
	err_entry.err_data_size = sizeof(uint32_t);
	err_entry.err_data = malloc(sizeof(uint32_t));
	cr_assert(err_entry.err_data);

	ret = fi_cq_readerr(fid_rx_cq, &err_entry, 0);
	cr_assert_eq(ret, 1, "Readerr CQ %d\n", ret);

	/* Insert address from FI_SOURCE_ERR into AV */
	ret = fi_av_insert(fid_av, (void *)err_entry.err_data, 1,
			   &fi_src_err_ep_addr, 0, NULL);

	cr_assert_eq(ret, 1, "Second EP AV add src address %d\n", ret);

	/* Wait for TX */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "Send completion %d\n", ret);

	/* First EP address should now be found EP1->EP2 */
	ret = fi_trecv(fid_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	sleep(1);

	ret = fi_tsend(cxit_ep, send_buf, send_len, NULL, fi_dest_ep_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Receive should complete successfully */
	ret = cxit_await_completion(fid_rx_cq, &rx_cqe);
	cr_assert_eq(ret, 1);

	/* Wait for TX */
	ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "Send completion %d\n", ret);

	/* Validate that the inserted address may be used in send,
	 * i.e. EP2 can now send to EP1.
	 */
	ret = fi_trecv(cxit_ep, recv_buf, recv_len, NULL, FI_ADDR_UNSPEC, 0,
		       0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv failed %d", ret);
	sleep(1);

	ret = fi_tsend(fid_ep, send_buf, send_len, NULL, fi_src_err_ep_addr, 0,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend failed %d", ret);

	/* Receive should complete successfully */
	ret = cxit_await_completion(cxit_rx_cq, &rx_cqe);
	cr_assert_eq(ret, 1);

	/* Wait for TX */
	ret = cxit_await_completion(fid_tx_cq, &tx_cqe);
	cr_assert_eq(ret, 1, "Send completion %d\n", ret);

	/* Cleanup Second EP */
	fi_close(&fid_ep->fid);
	fi_close(&fid_av->fid);
	fi_close(&fid_tx_cq->fid);
	fi_close(&fid_rx_cq->fid);

	/* Cleanup First EP */
	cxit_teardown_tagged();
	cxit_teardown_getinfo();

	free(err_entry.err_data);
}

TestSuite(tagged_cq_wait, .init = cxit_setup_rma_fd,
	  .fini = cxit_teardown_rma_fd,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

struct fd_params {
	size_t length;
	size_t num_ios;
	int timeout;
	bool poll;
	bool ux_msg;
};

struct tagged_cq_wait_event_args {
	struct fid_cq *cq;
	struct fi_cq_tagged_entry *cqe;
	size_t io_num;
	int timeout;
	bool poll;
};

static void *tagged_cq_wait_evt_worker(void *data)
{
	int ret;
	struct tagged_cq_wait_event_args *args;
	struct fid *fids[1];
	int cq_fd;
	size_t completions = 0;

	args = (struct tagged_cq_wait_event_args *)data;

	if (args->poll) {
		ret = fi_control(&args->cq->fid, FI_GETWAIT, &cq_fd);
		cr_assert_eq(ret, FI_SUCCESS, "Get CQ wait FD %d", ret);
		fids[0] = &args->cq->fid;
	}

	while (completions < args->io_num) {
		if (args->poll) {
			ret = fi_trywait(cxit_fabric, fids, 1);
			if (ret == FI_SUCCESS) {
				struct pollfd fds;

				fds.fd = cq_fd;
				fds.events = POLLIN;

				ret = poll(&fds, 1, args->timeout);
				cr_assert_neq(ret, 0, "Poll timed out");
				cr_assert_eq(ret, 1, "Poll error");
			}
			ret = fi_cq_read(args->cq,
					 &args->cqe[completions], 1);
			if (ret == 1)
				completions++;
		} else {
			ret = fi_cq_sread(args->cq, &args->cqe[completions],
					  1, NULL, args->timeout);
			cr_assert_eq(ret, 1, "Completion not received\n");
			completions++;
		}
	}

	pthread_exit(NULL);
}

static void cq_wait_post_sends(struct tagged_thread_args *tx_args,
			       struct fd_params *param)
{
	int ret;
	size_t buf_len = param->length;

	/* Issue the Sends */
	for (size_t tx_io = 0; tx_io < param->num_ios; tx_io++) {
		tx_args[tx_io].len = buf_len;
		tx_args[tx_io].tag = tx_io;
		tx_args[tx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(tx_args[tx_io].buf);
		for (size_t i = 0; i < buf_len; i++)
			tx_args[tx_io].buf[i] = i + 0xa0 + tx_io;

		do {
			ret = fi_tsend(cxit_ep, tx_args[tx_io].buf,
				       tx_args[tx_io].len, NULL,
				       cxit_ep_fi_addr, tx_args[tx_io].tag,
				       NULL);
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, NULL, 0);
				fi_cq_read(cxit_rx_cq, NULL, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend %ld: unexpected ret %d",
			     tx_io, ret);
	}
}

void do_cq_wait(struct fd_params *param)
{
	int ret;
	size_t buf_len = param->length;
	struct fi_cq_tagged_entry *rx_cqe;
	struct fi_cq_tagged_entry *tx_cqe;
	struct tagged_thread_args *tx_args;
	struct tagged_thread_args *rx_args;
	pthread_t tx_thread;
	pthread_t rx_thread;
	pthread_attr_t attr;
	struct tagged_cq_wait_event_args tx_evt_args = {
		.cq = cxit_tx_cq,
		.io_num = param->num_ios,
		.timeout = param->timeout,
		.poll = param->poll,
	};
	struct tagged_cq_wait_event_args rx_evt_args = {
		.cq = cxit_rx_cq,
		.io_num = param->num_ios,
		.timeout = param->timeout,
		.poll = param->poll,
	};

	tx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(tx_cqe);

	rx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(rx_cqe);

	tx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(tx_args);

	rx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(rx_args);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	tx_evt_args.cqe = tx_cqe;
	rx_evt_args.cqe = rx_cqe;

	/* Sends first if testing unexpected message operation */
	if (param->ux_msg) {
		cq_wait_post_sends(tx_args, param);

		/* Start processing Send events */
		ret = pthread_create(&tx_thread, &attr,
				     tagged_cq_wait_evt_worker,
				     (void *)&tx_evt_args);
		cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

		/* Force onloading of UX entries if operating in SW EP mode */
		fi_cq_read(cxit_rx_cq, &rx_cqe, 0);
	}

	/* Issue the Receives */
	for (size_t rx_io = 0; rx_io < param->num_ios; rx_io++) {
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		do {
			ret = fi_trecv(cxit_ep, rx_args[rx_io].buf,
				       rx_args[rx_io].len, NULL,
				       FI_ADDR_UNSPEC, rx_args[rx_io].tag,
				       0, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Start processing Receive events */
	ret = pthread_create(&rx_thread, &attr, tagged_cq_wait_evt_worker,
			     (void *)&rx_evt_args);
	cr_assert_eq(ret, 0, "Receive thread create failed %d", ret);

	/* Sends last for expected messaging */
	if (!param->ux_msg) {
		/* Make sure receive has blocked */
		sleep(1);
		cq_wait_post_sends(tx_args, param);

		/* Start processing Send events */
		ret = pthread_create(&tx_thread, &attr,
				     tagged_cq_wait_evt_worker,
				     (void *)&tx_evt_args);
	}

	/* Wait for the RX/TX event threads to complete */
	ret = pthread_join(tx_thread, NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);

	ret = pthread_join(rx_thread, NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	/* Validate results */
	for (size_t io = 0; io < param->num_ios; io++) {
		/* Validate sent data */
		cr_expect_arr_eq(rx_args[io].buf, tx_args[io].buf, buf_len);

		validate_tx_event(&tx_cqe[io], FI_TAGGED | FI_SEND, NULL);

		validate_rx_event(&rx_cqe[io], NULL, buf_len,
				  FI_TAGGED | FI_RECV, NULL,
				  0, tx_args[rx_cqe[io].tag].tag);

		free(tx_args[io].buf);
		free(rx_args[io].buf);
	}

	pthread_attr_destroy(&attr);
	free(rx_cqe);
	free(tx_cqe);
	free(tx_args);
	free(rx_args);
}

ParameterizedTestParameters(tagged_cq_wait, wait_fd)
{
	size_t param_sz;

	static struct fd_params params[] = {
		{.length = 1024,
		 .num_ios = 4,
		 .timeout = 5000,
		 .poll = true},
		{.length = 8192,
		 .num_ios = 4,
		 .timeout = 5000,
		 .poll = true},
		{.length = 1024,
		 .num_ios = 4,
		 .timeout = 5000,
		 .poll = false},
		{.length = 8192,
		 .num_ios = 4,
		 .timeout = 5000,
		 .poll = false},
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct fd_params, params,
				   param_sz);
}

ParameterizedTest(struct fd_params *param, tagged_cq_wait, wait_fd,
		  .timeout = 60)
{
	do_cq_wait(param);
}

TestSuite(tagged_tx_size, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(tagged_tx_size, force_progress)
{
	struct fi_cq_tagged_entry rx_cqe;
	struct fi_cq_tagged_entry tx_cqe;
	fi_addr_t from;
	char *send_buf;
	char *recv_buf;
	size_t buf_len;
	int ret;
	int tx_posted;
	int rx_posted;
	int i;

	/* Limit the TX queue size to 32 */
	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->size = 32;
	cxit_setup_rma();

	cr_assert_eq(cxit_fi_hints->tx_attr->size,
		     cxit_fi->tx_attr->size, "tx_attr->size");

	/* Send unexpected rendezvous messages so that completions
	 * will not occur and verify we get resource management
	 * at tx_attr->size.
	 */
	buf_len = 32 * 1024;
	send_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert_not_null(send_buf);
	recv_buf = aligned_alloc(s_page_size, buf_len);
	cr_assert_not_null(recv_buf);

	ret = 0;
	for (tx_posted = 0; tx_posted < cxit_fi->tx_attr->size + 1;
	     tx_posted++) {
		ret = fi_tsend(cxit_ep, send_buf, buf_len, NULL,
			       cxit_ep_fi_addr, 0, NULL);
		if (ret == -FI_EAGAIN)
			break;
	}
	cr_assert_eq(ret, -FI_EAGAIN, "-FI_EAGAIN expected");
	cr_assert(tx_posted <= cxit_fi->tx_attr->size,
		  "Too many I/O initiated\n");

	/* Post the receives and get RX completions */
	ret = 0;
	for (rx_posted = 0; rx_posted < tx_posted; rx_posted++) {
		do {
			ret = fi_trecv(cxit_ep, recv_buf, buf_len, NULL,
				       FI_ADDR_UNSPEC, 0, 0, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS,
			     "fi_trecv %d: unexpected ret %d", rx_posted, ret);
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
	}

	/* Get TX completions */
	ret = 0;
	for (i = 0; i < tx_posted; i++) {
		do {
			ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		} while (ret == -FI_EAGAIN);
	}
	cr_assert_eq(ret, 1, "bad completion status");
	cr_assert_eq(i, tx_posted, "bad TX completion count");

	cxit_teardown_rma();

	free(send_buf);
	free(recv_buf);
}

/* Note: the FI_PROTO_CXI_RNR tagged message test suite uses rnr_tagged
 * so that it will not be included in flow-control and software EP tests,
 * which it does not support.
 */
TestSuite(rnr_tagged, .init = cxit_setup_rnr_msg_ep,
	  .fini = cxit_teardown_msg, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rnr_tagged, ping)
{
	ping();
}

Test(rnr_tagged, pingdata)
{
	pingdata();
}

Test(rnr_tagged, vping)
{
	vping();
}

Test(rnr_tagged, msgping)
{
	msgping();
}

Test(rnr_tagged, peek)
{
	int ret;
	ssize_t len = 4096;
	uint8_t *send_buf;
	uint8_t *recv_buf;
	uint64_t tag = 11;
	struct fi_cq_tagged_entry rx_cqe;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_context rx_ctxt;

	send_buf = aligned_alloc(s_page_size, len);
	cr_assert_not_null(send_buf);
	recv_buf = aligned_alloc(s_page_size, len);
	cr_assert_not_null(recv_buf);

	memset(send_buf, 0xa5, len);

	/* Issue the Send it will be in retransmits */
	ret = fi_tsend(cxit_ep, send_buf, len, NULL, cxit_ep_fi_addr,
		       tag, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_tsend unexpected ret %d", ret);

	/* Just to make sure nothing completed unexpectedly in all modes */
	fi_cq_read(cxit_rx_cq, &rx_cqe, 0);

	/* Issue a FI_PEEK, should return -FI_ENOMSG since there is not
	 * an unexpected list.
	 */
	ret = try_peek(FI_ADDR_UNSPEC, tag, 0, len, NULL, false);
	cr_assert_eq(ret, FI_ENOMSG, "peek of CS message succeeded");

	/* Issue a FI_PEEK | FI_CLAIM, should return -FI_ENOMSG since
	 * there is not an unexpected list.
	 */
	ret = try_peek(FI_ADDR_UNSPEC, tag, 0, len, &rx_ctxt, true);
	cr_assert_eq(ret, FI_ENOMSG,
		     "peek with claim of CS message succeeded");

	/* Issue the Receive to recvieve the message that is being RNR
	 * retried.
	 */
	ret = fi_trecv(cxit_ep, recv_buf, len, NULL, FI_ADDR_UNSPEC,
			       tag, 0, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_trecv unexpected ret %d", ret);

	do {
		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "RX CQ error\n");

	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "TX CQ error\n");

	/* Validate results */
	cr_expect_arr_eq(send_buf, recv_buf, len);

	free(send_buf);
	free(recv_buf);
}

/* CS multiple tagged received test. The last_to_first option is
 * used to verify that a RNR for a specific tagged message will not
 * keep other tagged messages from matching.
 */
struct rnr_multitudes_params {
	size_t length;
	size_t num_ios;
	bool last_to_first;
};

void do_rnr_multitudes(struct rnr_multitudes_params *param)
{
	int ret;
	size_t rx_io;
	int i;
	size_t tx_io;
	size_t buf_len = param->length;
	struct fi_cq_tagged_entry *rx_cqe;
	struct fi_cq_tagged_entry *tx_cqe;
	struct tagged_thread_args *tx_args;
	struct tagged_thread_args *rx_args;
	struct fi_context *rx_ctxts;
	pthread_t tx_thread;
	pthread_t rx_thread;
	pthread_attr_t attr;
	struct tagged_event_args tx_evt_args = {
		.cq = cxit_tx_cq,
		.io_num = param->num_ios,
	};
	struct tagged_event_args rx_evt_args = {
		.cq = cxit_rx_cq,
		.io_num = param->num_ios,
	};

	tx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(tx_cqe);

	rx_cqe = calloc(param->num_ios, sizeof(struct fi_cq_tagged_entry));
	cr_assert_not_null(rx_cqe);

	tx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(tx_args);

	rx_args = calloc(param->num_ios, sizeof(struct tagged_thread_args));
	cr_assert_not_null(rx_args);

	rx_ctxts = calloc(param->num_ios, sizeof(struct fi_context));
	cr_assert_not_null(rx_ctxts);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	tx_evt_args.cqe = tx_cqe;
	rx_evt_args.cqe = rx_cqe;

	/* Issue the Sends */
	for (tx_io = 0; tx_io < param->num_ios; tx_io++) {
		tx_args[tx_io].len = buf_len;
		tx_args[tx_io].tag = tx_io;
		tx_args[tx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(tx_args[tx_io].buf);
		for (size_t i = 0; i < buf_len; i++)
			tx_args[tx_io].buf[i] = i + 0xa0 + tx_io;

		do {
			ret = fi_tsend(cxit_ep, tx_args[tx_io].buf,
				       tx_args[tx_io].len, NULL,
				       cxit_ep_fi_addr, tx_args[tx_io].tag,
				       NULL);
			if (ret == -FI_EAGAIN) {
				fi_cq_read(cxit_tx_cq, NULL, 0);
				fi_cq_read(cxit_rx_cq, NULL, 0);
			}
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_tsend %ld: unexpected ret %d",
			     tx_io, ret);
	}

	/* Start processing Send events progressing retry of RNR */
	ret = pthread_create(&tx_thread, &attr, tagged_evt_worker,
				(void *)&tx_evt_args);
	cr_assert_eq(ret, 0, "Send thread create failed %d", ret);

	/* Issue the Receives */
	for (i = 0; i < param->num_ios; i++) {
		rx_io = param->last_to_first ?
				param->num_ios - 1  - i : i;
		rx_args[rx_io].len = buf_len;
		rx_args[rx_io].tag = rx_io;
		rx_args[rx_io].buf = aligned_alloc(s_page_size, buf_len);
		cr_assert_not_null(rx_args[rx_io].buf);
		memset(rx_args[rx_io].buf, 0, buf_len);

		do {
			ret = fi_trecv(cxit_ep, rx_args[rx_io].buf,
				       rx_args[rx_io].len, NULL, FI_ADDR_UNSPEC,
				       rx_args[rx_io].tag, 0, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_rx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, FI_SUCCESS, "fi_trecv %ld: unexpected ret %d",
			     rx_io, ret);
	}

	/* Start processing Receive events */
	ret = pthread_create(&rx_thread, &attr, tagged_evt_worker,
			     (void *)&rx_evt_args);
	cr_assert_eq(ret, 0, "Receive thread create failed %d", ret);

	/* Wait for the RX/TX event threads to complete */
	ret = pthread_join(tx_thread, NULL);
	cr_assert_eq(ret, 0, "Send thread join failed %d", ret);

	ret = pthread_join(rx_thread, NULL);
	cr_assert_eq(ret, 0, "Recv thread join failed %d", ret);

	/* Validate results */
	for (size_t io = 0; io < param->num_ios; io++) {
		/* Validate sent data */
		cr_expect_arr_eq(rx_args[io].buf, tx_args[io].buf, buf_len);

		validate_tx_event(&tx_cqe[io], FI_TAGGED | FI_SEND, NULL);
		validate_rx_event(&rx_cqe[io], NULL,
				  buf_len, FI_TAGGED | FI_RECV, NULL,
				  0, tx_args[rx_cqe[io].tag].tag);
		free(tx_args[io].buf);
		free(rx_args[io].buf);
	}

	pthread_attr_destroy(&attr);
	free(rx_cqe);
	free(tx_cqe);
	free(tx_args);
	free(rx_args);
	free(rx_ctxts);
}


ParameterizedTestParameters(rnr_tagged, rnr_multitudes)
{
	size_t param_sz;

	static struct rnr_multitudes_params rnr_params[] = {
		{.length = 1024,
		 .num_ios = 10,
		 .last_to_first = false},
		{.length = 1024,
		 .num_ios = 10,
		 .last_to_first = true},
		{.length = 8192,
		 .num_ios = 10,
		 .last_to_first = false},
		{.length = 8192,
		 .num_ios = 10,
		 .last_to_first = true},
	};

	param_sz = ARRAY_SIZE(rnr_params);
	return cr_make_param_array(struct rnr_multitudes_params, rnr_params,
				   param_sz);
}

ParameterizedTest(struct rnr_multitudes_params *param, rnr_tagged,
		  rnr_multitudes, .timeout = 60)
{
	do_rnr_multitudes(param);
}
