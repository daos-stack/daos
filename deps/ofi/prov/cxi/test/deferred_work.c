/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2020 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/wait.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <pthread.h>

#include "cxip.h"
#include "cxip_test_common.h"

static void poll_counter_assert(struct fid_cntr *cntr, uint64_t expected_value,
				unsigned int timeout)
{
	int ret;
	struct timespec cur = {};
	struct timespec end;
	uint64_t value;

	ret = clock_gettime(CLOCK_MONOTONIC, &end);
	cr_assert_eq(ret, 0);

	end.tv_sec += timeout;

	while (true) {
		ret = clock_gettime(CLOCK_MONOTONIC, &cur);
		cr_assert_eq(ret, 0);

		value = fi_cntr_read(cntr);
		if (value == expected_value)
			break;

		if (cur.tv_sec > end.tv_sec) {
			// cr_fail doesn't work so fake it
			cr_assert_eq(value, expected_value,
				     "Counter failed to reach expected value: expected=%lu, got=%lu\n",
				     expected_value, value);
			break;
		}

		/* Progress TX side for rendezvous tests */
		fi_cq_read(cxit_tx_cq, NULL, 0);
	}
}

void deferred_msg_op_test(bool comp_event, size_t xfer_size,
			  uint64_t trig_thresh, bool is_tagged, uint64_t tag)
{
	int i;
	int ret;
	uint8_t *recv_buf;
	uint8_t *send_buf;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	struct fi_op_tagged tagged = {};
	struct fi_deferred_work work = {};
	uint64_t expected_rx_flags =
		is_tagged ? FI_TAGGED | FI_RECV : FI_MSG | FI_RECV;
	uint64_t expected_rx_tag = is_tagged ? tag : 0;
	uint64_t expected_tx_flags =
		is_tagged ? FI_TAGGED | FI_SEND : FI_MSG | FI_SEND;

	recv_buf = calloc(1, xfer_size);
	cr_assert(recv_buf);

	send_buf = calloc(1, xfer_size);
	cr_assert(send_buf);

	for (i = 0; i < xfer_size; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	if (is_tagged)
		ret = fi_trecv(cxit_ep, recv_buf, xfer_size, NULL,
			       FI_ADDR_UNSPEC, tag, 0, NULL);
	else
		ret = fi_recv(cxit_ep, recv_buf, xfer_size, NULL,
			      FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send deferred op to self */
	iov.iov_base = send_buf;
	iov.iov_len = xfer_size;

	work.threshold = trig_thresh;
	work.triggering_cntr = cxit_send_cntr;
	work.completion_cntr = cxit_send_cntr;

	if (is_tagged) {
		tagged.ep = cxit_ep;
		tagged.msg.msg_iov = &iov;
		tagged.msg.iov_count = 1;
		tagged.msg.addr = cxit_ep_fi_addr;
		tagged.msg.tag = tag;
		tagged.flags = comp_event ? FI_COMPLETION : 0;

		work.op_type = FI_OP_TSEND;
		work.op.tagged = &tagged;
	} else {
		msg.ep = cxit_ep;
		msg.msg.msg_iov = &iov;
		msg.msg.iov_count = 1;
		msg.msg.addr = cxit_ep_fi_addr;
		msg.flags = comp_event ? FI_COMPLETION : 0;

		work.op_type = FI_OP_SEND;
		work.op.msg = &msg;
	}

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	/* Verify no target event has occurred. */
	ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	ret = fi_cntr_add(cxit_send_cntr, work.threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	/* Wait for async event indicating data has been received */
	do {
		ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

	validate_rx_event(&rx_cqe, NULL, xfer_size, expected_rx_flags, NULL, 0,
			  expected_rx_tag);
	cr_assert(from == cxit_ep_fi_addr, "Invalid source address");

	if (comp_event) {
		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &tx_cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_tx_event(&tx_cqe, expected_tx_flags, NULL);
	} else {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d",
			     ret);
	}

	/* Validate sent data */
	for (i = 0; i < xfer_size; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	poll_counter_assert(cxit_send_cntr, work.threshold + 1, 5);

	free(send_buf);
	free(recv_buf);
}


TestSuite(deferred_work, .init = cxit_setup_msg, .fini = cxit_teardown_msg,
	  .timeout = CXIT_DEFAULT_TIMEOUT);


Test(deferred_work, eager_message_comp_event)
{
	deferred_msg_op_test(true, 1024, 123546, false, 0);
}

Test(deferred_work, rendezvous_message_comp_event)
{
	deferred_msg_op_test(true, 1024 * 1024, 123546, false, 0);
}

Test(deferred_work, eager_message_no_comp_event)
{
	deferred_msg_op_test(false, 1024, 123546, false, 0);
}

Test(deferred_work, rendezvous_message_no_comp_event, .timeout=60)
{
	deferred_msg_op_test(false, 1024 * 1024, 123546, false, 0);
}

Test(deferred_work, tagged_eager_message_comp_event)
{
	deferred_msg_op_test(true, 1024, 123546, true, 987654321);
}

Test(deferred_work, tagged_rendezvous_message_comp_event)
{
	deferred_msg_op_test(true, 1024 * 1024, 123546, true, 987654321);
}

Test(deferred_work, tagged_eager_message_no_comp_event)
{
	deferred_msg_op_test(false, 1024, 123546, true, 987654321);
}

Test(deferred_work, tagged_rendezvous_message_no_comp_event, .timeout=60)
{
	deferred_msg_op_test(false, 1024 * 1024, 123546, true, 987654321);
}

Test(deferred_work, flush_work)
{
	int i;
	int ret;
	uint8_t *recv_buf;
	uint8_t *send_buf;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	struct fi_deferred_work msg_work = {};
	unsigned int trig_thresh;
	size_t xfer_size = 1;
	uint64_t key = 0xbeef;
	struct mem_region mem_window;
	struct fi_rma_iov rma_iov = {};
	struct fi_op_rma rma = {};
	struct fi_deferred_work rma_work = {};
	struct fi_ioc ioc = {};
	struct fi_rma_ioc rma_ioc = {};
	struct fi_op_atomic amo = {};
	struct fi_deferred_work amo_work = {};
	struct fi_op_cntr op_cntr = {};
	struct fi_deferred_work cntr_work = {};

	recv_buf = calloc(1, xfer_size);
	cr_assert(recv_buf);

	send_buf = calloc(1, xfer_size);
	cr_assert(send_buf);

	ret = mr_create(xfer_size, FI_REMOTE_WRITE | FI_REMOTE_READ, 0xa0, &key,
			&mem_window);
	cr_assert_eq(ret, FI_SUCCESS, "mr_create failed %d", ret);

	for (i = 0; i < xfer_size; i++)
		send_buf[i] = i + 0xa0;

	/* Post RX buffer */
	ret = fi_recv(cxit_ep, recv_buf, xfer_size, NULL, FI_ADDR_UNSPEC, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed %d", ret);

	/* Send deferred 64 bytes to self */
	msg.ep = cxit_ep;
	iov.iov_base = send_buf;
	iov.iov_len = xfer_size;
	msg.msg.msg_iov = &iov;
	msg.msg.iov_count = 1;
	msg.msg.addr = cxit_ep_fi_addr;
	msg.flags = FI_COMPLETION;

	msg_work.triggering_cntr = cxit_send_cntr;
	msg_work.completion_cntr = cxit_send_cntr;
	msg_work.op_type = FI_OP_SEND;
	msg_work.op.msg = &msg;

	/* Deferred RMA op to be cancelled. */
	rma_iov.key = key;

	rma.ep = cxit_ep;
	rma.msg.msg_iov = &iov;
	rma.msg.iov_count = 1;
	rma.msg.addr = cxit_ep_fi_addr;
	rma.msg.rma_iov = &rma_iov;
	rma.msg.rma_iov_count = 1;
	rma.flags = FI_COMPLETION;

	rma_work.triggering_cntr = cxit_send_cntr;
	rma_work.completion_cntr = cxit_send_cntr;
	rma_work.op_type = FI_OP_READ;
	rma_work.op.rma = &rma;

	/* Deferred AMO op to be cancelled. */
	ioc.addr = &send_buf;
	ioc.count = 1;

	rma_ioc.key = key;
	rma_ioc.count = 1;

	amo.ep = cxit_ep;

	amo.msg.msg_iov = &ioc;
	amo.msg.iov_count = 1;
	amo.msg.addr = cxit_ep_fi_addr;
	amo.msg.rma_iov = &rma_ioc;
	amo.msg.rma_iov_count = 1;
	amo.msg.datatype = FI_UINT8;
	amo.msg.op = FI_SUM;

	amo_work.triggering_cntr = cxit_send_cntr;
	amo_work.completion_cntr = cxit_send_cntr;
	amo_work.op_type = FI_OP_ATOMIC;
	amo_work.op.atomic = &amo;

	/* Deferred counter op. */
	op_cntr.cntr = cxit_send_cntr;
	op_cntr.value = 13546;

	cntr_work.op_type = FI_OP_CNTR_SET;
	cntr_work.triggering_cntr = cxit_send_cntr;
	cntr_work.op.cntr = &op_cntr;

	/* Queue up multiple trigger requests to be cancelled. */
	for (i = 0, trig_thresh = 12345; i < 12; i++, trig_thresh++) {
		struct fi_deferred_work *work;

		if (i < 3)
			work = &msg_work;
		else if (i < 6)
			work = &rma_work;
		else if (i < 9)
			work = &cntr_work;
		else
			work = &amo_work;

		work->threshold = trig_thresh;

		ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, work);
		cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);
	}

	/* Verify no source or target event has occurred. */
	ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	/* Flush all work requests. */
	ret = fi_control(&cxit_domain->fid, FI_FLUSH_WORK, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "FI_FLUSH_WORK failed %d", ret);

	ret = fi_cntr_add(cxit_send_cntr, trig_thresh);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	/* Verify no source or target event has occurred. */
	ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	poll_counter_assert(cxit_send_cntr, trig_thresh, 5);

	free(send_buf);
	free(recv_buf);
	mr_destroy(&mem_window);
}

static void deferred_rma_test(enum fi_op_type op, size_t xfer_size,
			      uint64_t trig_thresh, uint64_t key,
			      bool comp_event)
{
	int ret;
	struct mem_region mem_window;
	struct fi_cq_tagged_entry cqe;
	struct iovec iov = {};
	struct fi_rma_iov rma_iov = {};
	struct fi_op_rma rma = {};
	struct fi_deferred_work work = {};
	struct fid_cntr *trig_cntr = cxit_write_cntr;
	struct fid_cntr *comp_cntr = cxit_read_cntr;
	uint8_t *send_buf;
	uint64_t expected_flags =
		op == FI_OP_WRITE ? FI_RMA | FI_WRITE : FI_RMA | FI_READ;

	send_buf = calloc(1, xfer_size);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(xfer_size, FI_REMOTE_WRITE | FI_REMOTE_READ, 0xa0, &key,
		  &mem_window);

	iov.iov_base = send_buf;
	iov.iov_len = xfer_size;

	rma_iov.key = key;

	rma.ep = cxit_ep;
	rma.msg.msg_iov = &iov;
	rma.msg.iov_count = 1;
	rma.msg.addr = cxit_ep_fi_addr;
	rma.msg.rma_iov = &rma_iov;
	rma.msg.rma_iov_count = 1;
	rma.flags = comp_event ? FI_COMPLETION : 0;

	work.threshold = trig_thresh;
	work.triggering_cntr = trig_cntr;
	work.completion_cntr = comp_cntr;
	work.op_type = op;
	work.op.rma = &rma;

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	/* Verify no target event has occurred. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	ret = fi_cntr_add(trig_cntr, work.threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	if (comp_event) {
		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_tx_event(&cqe, expected_flags, NULL);
	} else {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d",
			     ret);
	}

	poll_counter_assert(trig_cntr, work.threshold, 5);
	poll_counter_assert(comp_cntr, 1, 5);

	/* Validate RMA data */
	for (size_t i = 0; i < xfer_size; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%ld) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);
}

Test(deferred_work, rma_write)
{
	deferred_rma_test(FI_OP_WRITE, 12345, 54321, 0xbeef, true);
}

Test(deferred_work, rma_write_no_event)
{
	deferred_rma_test(FI_OP_WRITE, 12345, 54321, 0xbeef, false);
}

Test(deferred_work, rma_read)
{
	deferred_rma_test(FI_OP_READ, 12345, 54321, 0xbeef, true);
}

Test(deferred_work, rma_read_no_event)
{
	deferred_rma_test(FI_OP_READ, 12345, 54321, 0xbeef, false);
}

static void deferred_amo_test(bool comp_event, bool fetch, bool comp)
{
	int ret;
	struct mem_region mem_window;
	struct fi_cq_tagged_entry cqe;
	struct fi_ioc iov = {};
	struct fi_ioc fetch_iov = {};
	struct fi_ioc comp_iov = {};
	struct fi_rma_ioc rma_iov = {};
	struct fi_op_atomic amo = {};
	struct fi_op_fetch_atomic fetch_amo = {};
	struct fi_op_compare_atomic comp_amo = {};
	struct fi_msg_atomic *amo_msg;
	struct fi_deferred_work work = {};
	struct fid_cntr *trig_cntr = cxit_write_cntr;
	struct fid_cntr *comp_cntr = cxit_read_cntr;
	uint64_t expected_flags;
	uint64_t source_buf = 1;
	uint64_t *target_buf;
	uint64_t result;
	uint64_t key = 0xbbb;
	uint64_t trig_thresh = 12345;
	uint64_t init_target_value = 0x7FFFFFFFFFFFFFFF;
	uint64_t fetch_result = 0;
	uint64_t compare_value = init_target_value;

	ret = mr_create(sizeof(*target_buf), FI_REMOTE_WRITE | FI_REMOTE_READ,
			0, &key, &mem_window);
	assert(ret == FI_SUCCESS);

	target_buf = (uint64_t *)mem_window.mem;
	*target_buf = init_target_value;

	result = source_buf + *target_buf;

	iov.addr = &source_buf;
	iov.count = 1;

	rma_iov.key = key;
	rma_iov.count = 1;

	if (fetch) {
		amo_msg = &fetch_amo.msg;
		fetch_amo.ep = cxit_ep;
		fetch_amo.flags = comp_event ? FI_COMPLETION : 0;
		work.op_type = FI_OP_FETCH_ATOMIC;
		work.op.fetch_atomic = &fetch_amo;
		expected_flags = FI_ATOMIC | FI_READ;

		fetch_iov.addr = &fetch_result;
		fetch_iov.count = 1;

		fetch_amo.fetch.msg_iov = &fetch_iov;
		fetch_amo.fetch.iov_count = 1;
	} else if (comp) {
		amo_msg = &comp_amo.msg;
		comp_amo.ep = cxit_ep;
		comp_amo.flags = comp_event ? FI_COMPLETION : 0;
		work.op_type = FI_OP_COMPARE_ATOMIC;
		work.op.compare_atomic = &comp_amo;
		expected_flags = FI_ATOMIC | FI_READ;

		fetch_iov.addr = &fetch_result;
		fetch_iov.count = 1;

		comp_iov.addr = &compare_value;
		comp_iov.count = 1;

		comp_amo.fetch.msg_iov = &fetch_iov;
		comp_amo.fetch.iov_count = 1;
		comp_amo.compare.msg_iov = &comp_iov;
		comp_amo.compare.iov_count = 1;
	} else {
		amo_msg = &amo.msg;
		amo.ep = cxit_ep;
		amo.flags = comp_event ? FI_COMPLETION : 0;
		work.op_type = FI_OP_ATOMIC;
		work.op.atomic = &amo;
		expected_flags = FI_ATOMIC | FI_WRITE;
	}

	amo_msg->msg_iov = &iov;
	amo_msg->iov_count = 1;
	amo_msg->addr = cxit_ep_fi_addr;
	amo_msg->rma_iov = &rma_iov;
	amo_msg->rma_iov_count = 1;
	amo_msg->datatype = FI_UINT64;
	amo_msg->op = comp ? FI_CSWAP : FI_SUM;

	work.threshold = trig_thresh;
	work.triggering_cntr = trig_cntr;
	work.completion_cntr = comp_cntr;

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	/* Verify no target event has occurred. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	ret = fi_cntr_add(trig_cntr, work.threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	if (comp_event) {
		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_tx_event(&cqe, expected_flags, NULL);
	} else {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d",
			     ret);
	}

	poll_counter_assert(trig_cntr, work.threshold, 5);
	poll_counter_assert(comp_cntr, 1, 5);

	/* Validate AMO data */
	if (comp)
		cr_assert_eq(*target_buf, source_buf, "Invalid target result");
	else
		cr_assert_eq(*target_buf, result, "Invalid target result");

	if (fetch || comp)
		cr_assert_eq(fetch_result, init_target_value,
			     "Invalid fetch result expected=%lu got=%lu",
			     init_target_value, fetch_result);

	mr_destroy(&mem_window);
}

Test(deferred_work, amo_no_event)
{
	deferred_amo_test(false, false, false);
}

Test(deferred_work, amo_event)
{
	deferred_amo_test(true, false, false);
}

Test(deferred_work, fetch_amo_no_event)
{
	deferred_amo_test(false, true, false);
}

Test(deferred_work, fetch_amo_event)
{
	deferred_amo_test(true, true, false);
}

Test(deferred_work, compare_amo_no_event)
{
	deferred_amo_test(false, false, true);
}

Test(deferred_work, compare_amo_event)
{
	deferred_amo_test(true, false, true);
}

static void deferred_cntr(bool is_inc)
{
	struct fi_cntr_attr attr = {};
	struct fid_cntr *cntr;
	struct fid_cntr *trig_cntr = cxit_write_cntr;
	int ret;
	uint64_t value = 123456;
	uint64_t thresh = 1234;
	struct fi_op_cntr op_cntr = {};
	struct fi_deferred_work work = {};

	ret = fi_cntr_open(cxit_domain, &attr, &cntr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_open failed %d", ret);

	/* Ensure success value is non-zero to ensure success and increment
	 * work.
	 */
	ret = fi_cntr_add(cntr, 1);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	op_cntr.cntr = cntr;
	op_cntr.value = value;

	work.op_type = is_inc ? FI_OP_CNTR_ADD : FI_OP_CNTR_SET;
	work.triggering_cntr = trig_cntr;
	work.threshold = thresh;
	work.op.cntr = &op_cntr;

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	/* Trigger the operation. */
	ret = fi_cntr_add(trig_cntr, work.threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	poll_counter_assert(trig_cntr, work.threshold, 5);
	poll_counter_assert(cntr, is_inc ? 1 + value : value, 5);

	ret = fi_close(&cntr->fid);
	cr_assert_eq(ret, FI_SUCCESS, "fi_close failed %d", ret);
}

Test(deferred_work, cntr_add)
{
	deferred_cntr(true);
}

Test(deferred_work, cntr_set)
{
	deferred_cntr(false);
}

static void deferred_recv_op_test(bool comp_event, size_t xfer_size,
				  uint64_t trig_thresh, bool is_tagged,
				  uint64_t tag)
{
	int i;
	int ret;
	uint8_t *recv_buf;
	uint8_t *send_buf;
	struct fi_cq_tagged_entry tx_cqe;
	struct fi_cq_tagged_entry rx_cqe;
	int err = 0;
	fi_addr_t from;
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	struct fi_op_tagged tagged = {};
	struct fi_deferred_work work = {};
	uint64_t expected_rx_flags =
		is_tagged ? FI_TAGGED | FI_RECV : FI_MSG | FI_RECV;
	uint64_t expected_rx_tag = is_tagged ? tag : 0;
	uint64_t expected_tx_flags =
		is_tagged ? FI_TAGGED | FI_SEND : FI_MSG | FI_SEND;
	struct fi_cntr_attr attr = {};
	struct fid_cntr *recv_cntr;

	ret = fi_cntr_open(cxit_domain, &attr, &recv_cntr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_open failed %d", ret);

	recv_buf = calloc(1, xfer_size);
	cr_assert(recv_buf);

	send_buf = calloc(1, xfer_size);
	cr_assert(send_buf);

	for (i = 0; i < xfer_size; i++)
		send_buf[i] = i + 0xa0;

	/* Recv deferred op */
	iov.iov_base = recv_buf;
	iov.iov_len = xfer_size;

	work.threshold = trig_thresh;
	work.triggering_cntr = recv_cntr;
	work.completion_cntr = recv_cntr;

	if (is_tagged) {
		tagged.ep = cxit_ep;
		tagged.msg.msg_iov = &iov;
		tagged.msg.iov_count = 1;
		tagged.msg.tag = tag;
		tagged.msg.addr = cxit_ep_fi_addr;
		tagged.flags = comp_event ? FI_COMPLETION : 0;

		work.op_type = FI_OP_TRECV;
		work.op.tagged = &tagged;
	} else {
		msg.ep = cxit_ep;
		msg.msg.msg_iov = &iov;
		msg.msg.iov_count = 1;
		msg.msg.addr = cxit_ep_fi_addr;
		msg.flags = comp_event ? FI_COMPLETION : 0;

		work.op_type = FI_OP_RECV;
		work.op.msg = &msg;
	}

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	if (is_tagged)
		ret = fi_tsend(cxit_ep, send_buf, xfer_size, NULL,
			       cxit_ep_fi_addr, tag, NULL);
	else
		ret = fi_send(cxit_ep, send_buf, xfer_size, NULL,
			      cxit_ep_fi_addr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_send failed %d", ret);

	/* Wait for async send event. In software endpoint mode, RX CQ needs to
	 * be progress to progress TX CQ.
	 */
	do {
		ret = fi_cq_read(cxit_tx_cq, &tx_cqe, 1);
		if (ret == -FI_EAGAIN)
			fi_cq_read(cxit_rx_cq, &rx_cqe, 0);
	} while (ret == -FI_EAGAIN);

	validate_tx_event(&tx_cqe, expected_tx_flags, NULL);

	/* Verify optional receive event. */
	if (comp_event) {
		/* Wait for async event indicating data has been sent */
		do {
			ret = fi_cq_readfrom(cxit_rx_cq, &rx_cqe, 1, &from);
		} while (ret == -FI_EAGAIN);
		cr_assert_eq(ret, 1, "fi_cq_read unexpected value %d", ret);

		validate_rx_event(&rx_cqe, NULL, xfer_size, expected_rx_flags,
				  NULL, 0, expected_rx_tag);
		cr_assert(from == cxit_ep_fi_addr, "Invalid source address");
	} else {
		ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d",
			     ret);
	}

	/* Validate sent data */
	for (i = 0; i < xfer_size; i++) {
		cr_expect_eq(recv_buf[i], send_buf[i],
			  "data mismatch, element[%d], exp=%d saw=%d, err=%d\n",
			  i, send_buf[i], recv_buf[i], err++);
	}
	cr_assert_eq(err, 0, "Data errors seen\n");

	/* Need to progress recv the transaction to increment the counter. */
	ret = fi_cq_read(cxit_rx_cq, &rx_cqe, 1);
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_read unexpected value %d", ret);

	poll_counter_assert(recv_cntr, 1, 5);

	free(send_buf);
	free(recv_buf);
	fi_close(&recv_cntr->fid);
}

Test(deferred_work, recv_eager_message_comp_event)
{
	deferred_recv_op_test(true, 1024, 0, false, 0);
}

Test(deferred_work, recv_rendezvous_message_comp_event)
{
	deferred_recv_op_test(true, 1024 * 1024, 0, false, 0);
}

Test(deferred_work, recv_eager_message_no_comp_event)
{
	deferred_recv_op_test(false, 1024, 0, false, 0);
}

Test(deferred_work, recv_rendezvous_message_no_comp_event, .timeout=60)
{
	deferred_recv_op_test(false, 1024 * 1024, 0, false, 0);
}

Test(deferred_work, recv_tagged_eager_message_comp_event)
{
	deferred_recv_op_test(true, 1024, 0, true, 987654321);
}

Test(deferred_work, recv_tagged_rendezvous_message_comp_event)
{
	deferred_recv_op_test(true, 1024 * 1024, 0, true, 987654321);
}

Test(deferred_work, recv_tagged_eager_message_no_comp_event)
{
	deferred_recv_op_test(false, 1024, 0, true, 987654321);
}

Test(deferred_work, recv_tagged_rendezvous_message_no_comp_event, .timeout=60)
{
	deferred_recv_op_test(false, 1024 * 1024, 0, true, 987654321);
}

static void deferred_recv_non_zero_thresh(bool is_tagged)
{
	int ret;
	uint8_t *recv_buf;
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	struct fi_op_tagged tagged = {};
	struct fi_deferred_work work = {};
	struct fi_cntr_attr attr = {};
	struct fid_cntr *recv_cntr;

	ret = fi_cntr_open(cxit_domain, &attr, &recv_cntr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_open failed %d", ret);

	recv_buf = calloc(1, 5);
	cr_assert(recv_buf);

	/* Recv deferred op to self */
	iov.iov_base = recv_buf;
	iov.iov_len = 5;

	work.threshold = 5;
	work.triggering_cntr = recv_cntr;
	work.completion_cntr = recv_cntr;

	if (is_tagged) {
		tagged.ep = cxit_ep;
		tagged.msg.msg_iov = &iov;
		tagged.msg.iov_count = 1;
		tagged.msg.tag = 456;
		tagged.msg.addr = cxit_ep_fi_addr;
		tagged.flags = FI_COMPLETION;

		work.op_type = FI_OP_TRECV;
		work.op.tagged = &tagged;
	} else {
		msg.ep = cxit_ep;
		msg.msg.msg_iov = &iov;
		msg.msg.iov_count = 1;
		msg.msg.addr = cxit_ep_fi_addr;
		msg.flags = FI_COMPLETION;

		work.op_type = FI_OP_RECV;
		work.op.msg = &msg;
	}

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_neq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	free(recv_buf);
	fi_close(&recv_cntr->fid);
}

Test(deferred_work, recv_non_zero_thresh)
{
	deferred_recv_non_zero_thresh(false);
}

Test(deferred_work, recv_tagged_non_zero_thresh)
{
	deferred_recv_non_zero_thresh(true);
}

/* FI_INJECT with deferred work queue processing is not supported. */
void deferred_msg_inject_test(bool is_tagged)
{
	int ret;
	uint8_t *send_buf;
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	struct fi_op_tagged tagged = {};
	struct fi_deferred_work work = {};

	send_buf = calloc(1, 20);
	cr_assert(send_buf);

	/* Send deferred op to self */
	iov.iov_base = send_buf;
	iov.iov_len = 20;

	work.threshold = 5;
	work.triggering_cntr = cxit_send_cntr;
	work.completion_cntr = cxit_send_cntr;

	if (is_tagged) {
		tagged.ep = cxit_ep;
		tagged.msg.msg_iov = &iov;
		tagged.msg.iov_count = 1;
		tagged.msg.addr = cxit_ep_fi_addr;
		tagged.msg.tag = 0x0123;
		tagged.flags = FI_INJECT | FI_COMPLETION;

		work.op_type = FI_OP_TSEND;
		work.op.tagged = &tagged;
	} else {
		msg.ep = cxit_ep;
		msg.msg.msg_iov = &iov;
		msg.msg.iov_count = 1;
		msg.msg.addr = cxit_ep_fi_addr;
		msg.flags = FI_INJECT | FI_COMPLETION;

		work.op_type = FI_OP_SEND;
		work.op.msg = &msg;
	}

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, -FI_EINVAL, "FI_INJECT did not fail %d", ret);

	free(send_buf);
}

Test(deferred_work, tsend_inject)
{
	deferred_msg_inject_test(true);
}

Test(deferred_work, send_inject)
{
	deferred_msg_inject_test(false);
}

#define TLE_RESERVED 8U

static int alloc_service(struct cxil_dev *dev, unsigned int tle_count)
{
	struct cxi_svc_fail_info fail_info = {};
	struct cxi_svc_desc svc_desc = {
		.enable = 1,
		.limits = {
			.type[CXI_RSRC_TYPE_PTE] = {
				.max = 100,
				.res = 100,
			},
			.type[CXI_RSRC_TYPE_TXQ] = {
				.max = 100,
				.res = 100,
			},
			.type[CXI_RSRC_TYPE_TGQ] = {
				.max = 100,
				.res = 100,
			},
			.type[CXI_RSRC_TYPE_EQ] = {
				.max = 100,
				.res = 100,
			},
			.type[CXI_RSRC_TYPE_CT] = {
				.max = 100,
				.res = 100,
			},
			.type[CXI_RSRC_TYPE_LE] = {
				.max = 100,
				.res = 100,
			},
			.type[CXI_RSRC_TYPE_TLE] = {
				.max = tle_count + TLE_RESERVED,
				.res = tle_count + TLE_RESERVED,
			},
			.type[CXI_RSRC_TYPE_AC] = {
				.max = 8,
				.res = 8,
			},
		},
	};
	int ret;

	ret = cxil_alloc_svc(dev, &svc_desc, &fail_info);
	cr_assert_gt(ret, 0,
		     "cxil_alloc_svc(): Failed. Expected Success! rc:%d", ret);

	return ret;
}

struct deferred_work_resources {
	struct fi_info *hints;
	struct fi_info *info;
	struct fid_fabric *fab;
	struct fid_domain *dom;
	struct fid_cq *cq;
	struct fid_cntr *cntr;
	struct fid_av *av;
	struct fid_ep *ep;
	fi_addr_t loopback;
	struct cxil_dev *dev;
	int service_id;
};

#define test_assert(test, fmt, ...)					\
	do {								\
		if (!(test)) {						\
			fprintf(stderr, "%s:%d: " fmt "\n",		\
				__func__, __LINE__, ##__VA_ARGS__);	\
			abort();					\
		}							\
	} while (0)

static void
deferred_work_resources_teardown(struct deferred_work_resources *res)
{
	test_assert((fi_close(&res->ep->fid) == FI_SUCCESS), "fi_close failed");
	test_assert((fi_close(&res->cntr->fid) == FI_SUCCESS), "fi_close failed");
	test_assert((fi_close(&res->cq->fid) == FI_SUCCESS), "fi_close failed");
	test_assert((fi_close(&res->av->fid) == FI_SUCCESS), "fi_close failed");
	test_assert((fi_close(&res->dom->fid) == FI_SUCCESS), "fi_close failed");
	test_assert((fi_close(&res->fab->fid) == FI_SUCCESS), "fi_close failed");
	fi_freeinfo(res->info);
	fi_freeinfo(res->hints);
}

static bool triggered_ops_limited()
{
	static bool first = true;
	static bool limited = false;

	if (!first)
		return limited;

	char *s = getenv("FI_CXI_ENABLE_TRIG_OP_LIMIT");

	if (!s)           /* variable not set/found */
		goto not_limited;

	char *endptr;
	int i = strtol(s, &endptr, 10);

	if (endptr == s)  /* no parsable integers */
		goto not_limited;
	if (!i)           /* set to 0 */
		goto not_limited;

	/* Some non-zero integer was parsed.
	 * It still could be 10zebras, but we will count it.
	 */

	limited = true;

 not_limited:

	first = false;

	return limited;
}

static void deferred_work_resources_init(struct deferred_work_resources *res,
					 int service_id)
{
	int ret;
	struct cxi_auth_key auth_key = {
		.vni = 1,
	};
	struct fi_av_attr av_attr = {};

	auth_key.svc_id = service_id;

	res->hints = fi_allocinfo();
	test_assert(res->hints, "fi_allocinfo failed");

	res->hints->fabric_attr->prov_name = strdup("cxi");
	test_assert(res->hints->fabric_attr->prov_name, "strdup failed");

	res->hints->domain_attr->mr_mode =
		FI_MR_ENDPOINT | FI_MR_ALLOCATED | FI_MR_PROV_KEY;
	res->hints->tx_attr->op_flags = FI_TRANSMIT_COMPLETE;

	ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
			 "cxi0", NULL, FI_SOURCE, res->hints,
			 &res->info);
	test_assert(ret == FI_SUCCESS, "fi_getinfo failed: %d\n", ret);

	ret = fi_fabric(res->info->fabric_attr, &res->fab, NULL);
	test_assert(ret == FI_SUCCESS, "fi_fabric failed: %d\n", ret);

	res->info->domain_attr->auth_key = (void *)&auth_key;
	res->info->domain_attr->auth_key_size = sizeof(auth_key);

	ret = fi_domain(res->fab, res->info, &res->dom, NULL);
	test_assert(ret == FI_SUCCESS, "fi_domain failed: %d\n", ret);

	res->info->domain_attr->auth_key = NULL;
	res->info->domain_attr->auth_key_size = 0;

	ret = fi_av_open(res->dom, &av_attr, &res->av, NULL);
	test_assert(ret == FI_SUCCESS, "fi_av_open failed: %d\n", ret);

	ret = fi_cq_open(res->dom, NULL, &res->cq, NULL);
	test_assert(ret == FI_SUCCESS, "fi_cq_open failed: %d\n", ret);

	ret = fi_cntr_open(res->dom, NULL, &res->cntr, NULL);
	test_assert(ret == FI_SUCCESS, "fi_cntr_open failed: %d\n", ret);

	ret = fi_endpoint(res->dom, res->info, &res->ep, NULL);
	test_assert(ret == FI_SUCCESS, "fi_endpoint failed: %d\n", ret);

	ret = fi_ep_bind(res->ep, &res->cq->fid,
			 FI_TRANSMIT | FI_RECV | FI_SELECTIVE_COMPLETION);
	test_assert(ret == FI_SUCCESS, "fi_ep_bind failed: %d\n", ret);

	ret = fi_ep_bind(res->ep, &res->cntr->fid,
			 FI_SEND | FI_RECV | FI_READ | FI_WRITE);
	test_assert(ret == FI_SUCCESS, "fi_ep_bind failed: %d\n", ret);

	ret = fi_ep_bind(res->ep, &res->av->fid, 0);
	test_assert(ret == FI_SUCCESS, "fi_ep_bind failed: %d\n", ret);

	ret = fi_enable(res->ep);
	test_assert(ret == FI_SUCCESS, "fi_enable failed: %d\n", ret);

	ret = fi_av_insert(res->av, res->info->src_addr, 1, &res->loopback, 0,
			   NULL);
	test_assert(ret == 1, "fi_av_insert failed: %d\n", ret);
}

TestSuite(deferred_work_trig_op_limit, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(deferred_work_trig_op_limit, enforce_limit_single_thread)
{
	struct deferred_work_resources res = {};
	unsigned int trig_op_count = 64;
	unsigned int threshold = 1000;
	char send_buf[256];
	char recv_buf[256];
	int ret;
	int i;
	struct fi_deferred_work work = {};
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	bool limited = triggered_ops_limited();

	ret = cxil_open_device(0, &res.dev);
	cr_assert_eq(ret, 0, "cxil_open_device failed: %d\n", ret);

	res.service_id = alloc_service(res.dev, trig_op_count);
	cr_assert_gt(res.service_id, 0, "alloc_service() failed: %d\n",
		     res.service_id);

	deferred_work_resources_init(&res, res.service_id);

	for (i = 0; i < trig_op_count; i++) {
		ret = fi_recv(res.ep, recv_buf, sizeof(recv_buf), NULL,
			      res.loopback, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "fi_recv failed: %d\n", ret);
	}

	iov.iov_base = send_buf;
	iov.iov_len = sizeof(send_buf);

	work.threshold = threshold;
	work.triggering_cntr = res.cntr;
	work.completion_cntr = res.cntr;

	msg.ep = res.ep;
	msg.msg.msg_iov = &iov;
	msg.msg.iov_count = 1;
	msg.msg.addr = res.loopback;
	msg.flags = FI_TRANSMIT_COMPLETE;

	work.op_type = FI_OP_SEND;
	work.op.msg = &msg;

	for (i = 0; i < trig_op_count; i++) {
		ret = fi_control(&res.dom->fid, FI_QUEUE_WORK, &work);
		cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK iter %d failed %d", i, ret);
	}

	ret = fi_control(&res.dom->fid, FI_QUEUE_WORK, &work);
	if (limited)
		cr_assert_eq(ret, -FI_ENOSPC, "FI_QUEUE_WORK failed %d", ret);
	else
		cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	cr_assert((fi_control(&res.dom->fid, FI_FLUSH_WORK, NULL) == FI_SUCCESS));

	for (i = 0; i < trig_op_count; i++) {
		ret = fi_control(&res.dom->fid, FI_QUEUE_WORK, &work);
		cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK iter %d failed %d", i, ret);
	}

	cr_assert((fi_control(&res.dom->fid, FI_FLUSH_WORK, NULL) == FI_SUCCESS));

	deferred_work_resources_teardown(&res);

	cr_assert((cxil_destroy_svc(res.dev, res.service_id) == 0));
	cxil_close_device(res.dev);
}

static void run_multi_process_dwq_test(int service_id)
{
	struct deferred_work_resources res = {};
	int count = 4;
	unsigned int threshold = 1000;
	char send_buf[256];
	int ret;
	int i;
	struct fi_deferred_work work = {};
	struct iovec iov = {};
	struct fi_op_msg msg = {};
	bool limited = triggered_ops_limited();

	deferred_work_resources_init(&res, service_id);

	iov.iov_base = send_buf;
	iov.iov_len = sizeof(send_buf);

	work.threshold = threshold;
	work.triggering_cntr = res.cntr;
	work.completion_cntr = res.cntr;

	msg.ep = res.ep;
	msg.msg.msg_iov = &iov;
	msg.msg.iov_count = 1;
	msg.msg.addr = res.loopback;
	msg.flags = FI_TRANSMIT_COMPLETE;

	work.op_type = FI_OP_SEND;
	work.op.msg = &msg;

	/* Continue trying to queue multiple TLEs and free them. */
	for (i = 0; i < count; i++) {
		while (true) {
			ret = fi_control(&res.dom->fid, FI_QUEUE_WORK, &work);
			test_assert(((ret == FI_SUCCESS) && limited) || (ret  == -FI_ENOSPC),
				    "FI_QUEUE_WORK failed %d", ret);

			if (ret == -FI_ENOSPC)
				break;
		}

		test_assert((fi_control(&res.dom->fid, FI_FLUSH_WORK, NULL) == FI_SUCCESS),
			    "FI_FLUSH_WORK failed");
	}

	deferred_work_resources_teardown(&res);

	exit(EXIT_SUCCESS);
}

#define TLE_POOLS 4U

Test(deferred_work_trig_op_limit, enforce_limit_multi_process)
{
	struct deferred_work_resources res = {};
	int trig_op_count = 100;
	int ret;
	union c_cq_sts_max_tle_in_use max_in_use = {};
	pid_t pid = -1;
	int status;
	int i;
	bool found_max_in_use = false;
	int num_forks = 5;
	bool limited = triggered_ops_limited();

	ret = cxil_open_device(0, &res.dev);
	cr_assert_eq(ret, 0, "cxil_open_device failed: %d\n", ret);

	ret = cxil_map_csr(res.dev);
	cr_assert_eq(ret, 0, "cxil_map_csr failed: %d\n", ret);

	res.service_id = alloc_service(res.dev, trig_op_count);
	cr_assert_gt(res.service_id, 0, "alloc_service() failed: %d\n",
		     res.service_id);

	for (i = 0; i < TLE_POOLS; i++) {
		ret = cxil_write_csr(res.dev, C_CQ_STS_MAX_TLE_IN_USE(i),
				     &max_in_use, sizeof(max_in_use));
		cr_assert_eq(ret, 0, "cxil_write_csr failed: %d\n", ret);
	}

	for (i = 0; i < num_forks; i++) {
		pid = fork();
		if (pid == 0)
			run_multi_process_dwq_test(res.service_id);
	}

	wait(&status);

	for (i = 0; i < TLE_POOLS; i++) {
		ret = cxil_read_csr(res.dev, C_CQ_STS_MAX_TLE_IN_USE(i),
				    &max_in_use, sizeof(max_in_use));
		cr_assert_eq(ret, 0, "cxil_read_csr failed: %d\n", ret);

		fprintf(stderr, "%d max_in_use.max = %d\n", i, max_in_use.max);

		if (max_in_use.max >= trig_op_count && max_in_use.max < (trig_op_count + 8)) {
			found_max_in_use = true;
			break;
		}
	}
	if (limited)
		cr_assert_eq(found_max_in_use, true, "Triggered op limit exceeded\n");

	while ((ret = cxil_destroy_svc(res.dev, res.service_id)) == -EBUSY) {}
	cr_assert(ret == 0, "cxil_destroy_svc failed: %d\n", ret);

	cxil_close_device(res.dev);
}
