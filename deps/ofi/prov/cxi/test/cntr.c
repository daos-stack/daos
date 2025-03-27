/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <pthread.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(cntr, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .timeout = 5);

Test(cntr, mod)
{
	int ret;
	int i;
	uint64_t val = 0;
	uint64_t errval = 0;
	struct fid_cntr *tmp_cntr;
	struct fi_cntr_attr attr = {
		.wait_obj = FI_WAIT_NONE,
	};

	ret = fi_cntr_open(cxit_domain, &attr, &tmp_cntr, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cntr_open (send)");

	ret = fi_cntr_add(tmp_cntr, 1);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_read(tmp_cntr) != 1)
		sched_yield();

	/* fi_cntr_wait() is invalid with FI_WAIT_NONE */
	ret = fi_cntr_wait(tmp_cntr, 1, -1);
	cr_assert(ret == -FI_EINVAL);

	fi_close(&tmp_cntr->fid);

	cr_assert(!fi_cntr_read(cxit_write_cntr));

	/* Test invalid values */
	ret = fi_cntr_add(cxit_write_cntr, FI_CXI_CNTR_SUCCESS_MAX + 1);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_cntr_set(cxit_write_cntr, FI_CXI_CNTR_SUCCESS_MAX + 1);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_cntr_adderr(cxit_write_cntr, FI_CXI_CNTR_FAILURE_MAX + 1);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_cntr_seterr(cxit_write_cntr, FI_CXI_CNTR_FAILURE_MAX + 1);
	cr_assert(ret == -FI_EINVAL);

	for (i = 0; i < 10; i++) {
		val += 10;
		ret = fi_cntr_add(cxit_write_cntr, 10);
		cr_assert(ret == FI_SUCCESS);

		while (fi_cntr_read(cxit_write_cntr) != val)
			sched_yield();

		errval += 30;
		ret = fi_cntr_adderr(cxit_write_cntr, 30);
		cr_assert(ret == FI_SUCCESS);

		while (fi_cntr_readerr(cxit_write_cntr) != errval)
			sched_yield();

		val = 5;
		ret = fi_cntr_set(cxit_write_cntr, val);
		cr_assert(ret == FI_SUCCESS);

		while (fi_cntr_read(cxit_write_cntr) != val)
			sched_yield();

		errval = 15;
		ret = fi_cntr_seterr(cxit_write_cntr, errval);
		cr_assert(ret == FI_SUCCESS);

		while (fi_cntr_readerr(cxit_write_cntr) != errval)
			sched_yield();
	}
}

/* Test RMA with counters */
Test(cntr, write)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = 0x1f;
	struct fi_cq_tagged_entry cqe;
	int writes = 10;
	int i;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	for (i = 0; i < send_len; i++)
		send_buf[i] = 0xab + i;

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	cr_assert(!fi_cntr_read(cxit_write_cntr));

	for (i = 0; i < writes; i++) {
		int off = i * send_len;

		ret = fi_inject_write(cxit_ep, send_buf + off, send_len,
				      cxit_ep_fi_addr, off, key_val);
		cr_assert(ret == FI_SUCCESS);
	}

	while (fi_cntr_read(cxit_write_cntr) != writes)
		sched_yield();

	/* Validate sent data */
	for (int i = 0; i < writes * send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	/* Make sure no events were delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test all sizes of RMA transactions with counters */
Test(cntr, write_sizes)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16 * 1024;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = 0x1f;
	struct fi_cq_tagged_entry cqe;
	int writes = 0;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	cr_assert(!fi_cntr_read(cxit_write_cntr));

	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert_eq(ret, FI_SUCCESS, "ret=%d", ret);

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);
		writes++;

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);
	}

	while (fi_cntr_read(cxit_write_cntr) != writes)
		sched_yield();

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test fi_read with counters */
Test(cntr, read)
{
	int ret;
	uint8_t *local;
	int remote_len = 0x1000;
	int local_len = 8;
	uint64_t key_val = 0xa;
	struct fi_cq_tagged_entry cqe;
	struct mem_region remote;

	local = calloc(1, local_len);
	cr_assert_not_null(local, "local alloc failed");

	mr_create(remote_len, FI_REMOTE_READ, 0xc0, &key_val, &remote);

	cr_assert(!fi_cntr_read(cxit_read_cntr));

	/* Get 8 bytes from the source buffer to the receive buffer */
	ret = fi_read(cxit_ep, local, local_len, NULL, cxit_ep_fi_addr, 0,
		      key_val, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_read() failed (%d)", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read() failed (%d)", ret);

	validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

	/* Validate sent data */
	for (int i = 0; i < local_len; i++)
		cr_expect_eq(local[i], remote.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     local[i], remote.mem[i]);

	while (fi_cntr_read(cxit_read_cntr) != 1)
		sched_yield();

	mr_destroy(&remote);
	free(local);
}

/* Test send/recv counters */
Test(cntr, ping)
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

	cr_assert(!fi_cntr_read(cxit_send_cntr));
	cr_assert(!fi_cntr_read(cxit_recv_cntr));

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

	while (fi_cntr_read(cxit_send_cntr) != 1)
		sched_yield();

	while (fi_cntr_read(cxit_recv_cntr) != 1)
		sched_yield();

	free(send_buf);
	free(recv_buf);
}

int wait_for_cnt(struct fid_cntr *cntr, int cnt,
		 uint64_t (*cntr_read)(struct fid_cntr *cntr))
{
	uint64_t cntr_value;
	time_t timeout = time(NULL) + 3;

	while ((cntr_value = cntr_read(cntr)) != cnt) {
		if (time(NULL) > timeout) {
			printf("Timeout waiting for cnt:%d cntr_value:%lx\n",
			       cnt, cntr_value);
			return -1;
		}
		sched_yield();
	}

	return 0;
}

int wait_for_value(uint64_t compare_value, uint64_t *wb_buf)
{
	time_t timeout = time(NULL) + 2;

	while (compare_value != *wb_buf) {
		if (time(NULL) > timeout) {
			printf("Timeout waiting for compare_value:%lx wb:%lx\n",
			       compare_value, *wb_buf);
			return -1;
		}
		sched_yield();
	}

	return 0;
}

static void deferred_rma_test(enum fi_op_type op)
{
	int ret;
	uint8_t *send_buf;
	struct mem_region mem_window;
	struct iovec iov = {};
	struct fi_rma_iov rma_iov = {};
	struct fi_op_rma rma = {};
	struct fi_deferred_work work = {};
	struct fid_cntr *trig_cntr = cxit_write_cntr;

	size_t xfer_size = 8;
	uint64_t trig_thresh = 1;
	uint64_t key = 0xbeef;

	uint64_t cxi_value;
	struct fi_cxi_cntr_ops *cntr_ops;
	struct cxip_cntr *cxi_cntr;

	ret = fi_open_ops(&trig_cntr->fid, FI_CXI_COUNTER_OPS, 0,
			  (void **)&cntr_ops, NULL);
	cr_assert(ret == FI_SUCCESS);
	cxi_cntr = container_of(&trig_cntr->fid, struct cxip_cntr,
				cntr_fid.fid);
	cr_assert_not_null(cxi_cntr, "cxi_cntr is null");

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
	rma.flags = FI_CXI_CNTR_WB;

	work.threshold = trig_thresh;
	work.triggering_cntr = trig_cntr;
	work.completion_cntr = trig_cntr;
	work.op_type = op;
	work.op.rma = &rma;

	ret = fi_control(&cxit_domain->fid, FI_QUEUE_WORK, &work);
	cr_assert_eq(ret, FI_SUCCESS, "FI_QUEUE_WORK failed %d", ret);

	ret = fi_cntr_add(trig_cntr, work.threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	ret = fi_cxi_gen_cntr_success(trig_thresh + 1, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_value(cxi_value, (uint64_t *)cxi_cntr->wb);

	mr_destroy(&mem_window);
	free(send_buf);
}

Test(cntr, deferred_wb_rma_write)
{
	deferred_rma_test(FI_OP_WRITE);
}

Test(cntr, deferred_wb_rma_read)
{
	deferred_rma_test(FI_OP_READ);
}

Test(cntr, op_cntr_wb1)
{
	int ret;
	struct fid_cntr *cntr;
	uint64_t trig_thresh = 1;
	uint64_t cxi_value;
	struct cxip_cntr *cxi_cntr;

	ret = fi_cntr_open(cxit_domain, NULL, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);

	cxi_cntr = container_of(&cntr->fid, struct cxip_cntr, cntr_fid.fid);

	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);

	ret = fi_cntr_add(cntr, trig_thresh);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	fi_cntr_read(cntr);

	ret = fi_cxi_gen_cntr_success(trig_thresh, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_value(cxi_value, (uint64_t *)cxi_cntr->wb);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close cntr");
}

Test(cntr, op_cntr_wb2)
{
	int ret;
	void *mmio_addr;
	size_t mmio_len;
	uint64_t cxi_value;
	uint64_t threshold = 1;
	struct fid_cntr *cntr;
	struct cxip_cntr *cxi_cntr;
	struct fi_cxi_cntr_ops *cntr_ops;
	struct c_ct_writeback *wb_buf = NULL;
	int wb_len = sizeof(*wb_buf);

	ret = fi_cntr_open(cxit_domain, NULL, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_open_ops(&cntr->fid, FI_CXI_COUNTER_OPS, 0,
			  (void **)&cntr_ops, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);

	cxi_cntr = container_of(&cntr->fid, struct cxip_cntr, cntr_fid.fid);

	ret = fi_cntr_add(cntr, threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);

	ret = cntr_ops->get_mmio_addr(&cntr->fid, &mmio_addr, &mmio_len);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_cxi_gen_cntr_success(threshold, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	fi_cntr_read(cntr);
	ret = wait_for_value(cxi_value, (uint64_t *)cxi_cntr->wb);
	cr_assert(ret == 0);

	cr_assert(fi_cxi_cntr_wb_read(cxi_cntr->wb) == threshold);

	fi_cxi_cntr_set(mmio_addr, 0);
	fi_cxi_gen_cntr_success(0, &cxi_value);
	ret = wait_for_value(cxi_value, (uint64_t *)cxi_cntr->wb);
	cr_assert(ret == 0);

	threshold = 10;
	ret = fi_cntr_add(cntr, threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);
	ret = fi_cxi_gen_cntr_success(threshold, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, threshold, fi_cntr_read);
	cr_assert(ret == 0);

	fi_cxi_cntr_set(mmio_addr, 0);
	fi_cxi_gen_cntr_success(0, &cxi_value);
	ret = wait_for_value(cxi_value, (uint64_t *)cxi_cntr->wb);
	cr_assert(ret == 0);

	/* Change to a new writeback buffer */
	wb_buf = aligned_alloc(s_page_size, wb_len);
	cr_assert_not_null(wb_buf, "wb_buf alloc failed");
	ret = cntr_ops->set_wb_buffer(&cntr->fid, wb_buf, wb_len);
	cr_assert(ret == FI_SUCCESS);

	/* Use the new wb buffer */
	threshold = 20;
	ret = fi_cntr_add(cntr, threshold);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cntr_add failed %d", ret);
	ret = fi_cxi_gen_cntr_success(threshold, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, threshold, fi_cntr_read);
	cr_assert(ret == 0);

	// Use instead of fi_cxi_cntr_set()
	*(uint64_t*)(fi_cxi_get_cntr_reset_addr(mmio_addr)) = 0;
	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close cntr");

	free(wb_buf);
}

Test(cntr, counter_ops)
{
	int ret;
	int cnt;
	uint64_t *addr;
	uint64_t cxi_value;
	struct fid_cntr *cntr;
	struct fi_cxi_cntr_ops *cntr_ops;
	struct cxip_cntr *cxi_cntr;

	struct c_ct_writeback *wb_buf = NULL;
	int wb_len = sizeof(*wb_buf);
	void *mmio_addr;
	size_t mmio_len;

	ret = fi_cntr_open(cxit_domain, NULL, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_open_ops(&cntr->fid, FI_CXI_COUNTER_OPS, 0,
			  (void **)&cntr_ops, NULL);
	cr_assert(ret == FI_SUCCESS);

	cxi_cntr = container_of(&cntr->fid, struct cxip_cntr, cntr_fid.fid);

	wb_buf = aligned_alloc(s_page_size, wb_len);
	cr_assert_not_null(wb_buf, "wb_buf alloc failed");

	ret = cntr_ops->set_wb_buffer(&cntr->fid, wb_buf, wb_len);
	cr_assert(ret == FI_SUCCESS);

	/* enables counter */
	ret = fi_cntr_set(cntr, 0);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);

	ret = cntr_ops->get_mmio_addr(&cntr->fid, &mmio_addr, &mmio_len);
	cr_assert(ret == FI_SUCCESS);

	cr_assert(fi_cxi_cntr_wb_read(cxi_cntr->wb) == 0);

	cnt = 10;
	ret = fi_cntr_add(cntr, cnt);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, cnt, fi_cntr_read);
	cr_assert(ret == 0);
	cr_assert(fi_cxi_cntr_wb_read(wb_buf) == cnt);

	fi_cxi_cntr_set(mmio_addr, 0);
	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);
	cr_assert(fi_cntr_read(cntr) == 0, "read:%ld", fi_cntr_read(cntr));

	ret = fi_cxi_cntr_set(mmio_addr, 15);
	cr_assert(ret != FI_SUCCESS, "fi_cxi_cntr_set should fail:%d", ret);

	cnt = 5;
	ret = fi_cntr_add(cntr, cnt);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, cnt, fi_cntr_read);
	cr_assert(ret == 0);
	cr_assert(fi_cxi_cntr_wb_read(wb_buf) == cnt);

	fi_cxi_cntr_set(mmio_addr, 0);
	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);
	cr_assert(fi_cntr_read(cntr) == 0, "read:%ld", fi_cntr_read(cntr));

	fi_cxi_cntr_seterr(mmio_addr, 0);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, 0, fi_cntr_readerr);
	cr_assert(ret == 0);

	cnt = 1;
	ret = fi_cxi_cntr_adderr(mmio_addr, cnt);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, cnt, fi_cntr_readerr);
	cr_assert(ret == 0);
	cr_assert(fi_cntr_readerr(cntr) == cnt);
	cr_assert(fi_cxi_cntr_wb_readerr(wb_buf) == cnt);

	fi_cxi_cntr_set(mmio_addr, 0);
	cr_assert(ret == FI_SUCCESS);

	fi_cxi_cntr_seterr(mmio_addr, 0);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, 0, fi_cntr_readerr);
	cr_assert(ret == 0);

	cnt = 50;
	ret = fi_cxi_cntr_add(mmio_addr, cnt);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_cnt(cntr, cnt, fi_cntr_read);
	cr_assert(ret == 0);
	cr_assert(fi_cntr_read(cntr) == cnt, "cntr:%ld", fi_cntr_read(cntr));

	fi_cxi_cntr_set(mmio_addr, 0);
	cr_assert(ret == FI_SUCCESS);
	ret = fi_cxi_gen_cntr_success(0, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_value(cxi_value, (uint64_t *)wb_buf);
	cr_assert(ret == 0);

	// Use instead of fi_cxi_cntr_set()
	*(uint64_t*)(fi_cxi_get_cntr_reset_addr(mmio_addr)) = 0;
	ret = wait_for_cnt(cntr, 0, fi_cntr_read);
	cr_assert(ret == 0);

	cnt = 12;
	*(uint64_t*)(fi_cxi_get_cntr_adderr_addr(mmio_addr)) = cnt;
	/* Error transition from 0 causes a writeback */
	while(fi_cxi_cntr_wb_readerr(wb_buf) != cnt)
		sched_yield();

	cr_assert(fi_cxi_cntr_wb_readerr(wb_buf) == cnt);

	addr = fi_cxi_get_cntr_reseterr_addr(mmio_addr);
	*addr = 0;
	ret = fi_cxi_gen_cntr_success(0, &cxi_value);
	cr_assert(ret == FI_SUCCESS);
	ret = wait_for_value(cxi_value, (uint64_t *)wb_buf);
	cr_assert(ret == FI_SUCCESS);

	cr_assert(fi_cntr_readerr(cntr) == 0);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close cntr");

	free(wb_buf);
}

Test(cntr, cntr_wait_timeout)
{
	struct fid_cntr *cntr;
	struct fi_cntr_attr attr = {
		.wait_obj = FI_WAIT_UNSPEC,
	};
	int timeout = 2999;
	uint64_t thresh = 0x1234;
	int ret;

	ret = fi_cntr_open(cxit_domain, &attr, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_cntr_wait(cntr, thresh, timeout);
	cr_assert(ret == -FI_ETIMEDOUT);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS);
}

Test(cntr, cntr_wait)
{
	struct fid_cntr *cntr;
	struct fi_cntr_attr attr = {
		.wait_obj = FI_WAIT_UNSPEC,
	};
	void *mmio_addr;
	size_t mmio_len;
	struct fi_cxi_cntr_ops *cntr_ops;
	int timeout = 2000;
	uint64_t thresh = 0x1234;
	int ret;

	ret = fi_cntr_open(cxit_domain, &attr, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_open_ops(&cntr->fid, FI_CXI_COUNTER_OPS, 0,
			  (void **)&cntr_ops, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = cntr_ops->get_mmio_addr(&cntr->fid, &mmio_addr, &mmio_len);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_cntr_wait(cntr, thresh, timeout);
	cr_assert(ret == -FI_ETIMEDOUT);

	fi_cxi_cntr_add(mmio_addr, thresh);

	ret = fi_cntr_wait(cntr, thresh, timeout);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS);
}

Test(cntr, cntr_wait_bad_threshold)
{
	struct fid_cntr *cntr;
	struct fi_cntr_attr attr = {
		.wait_obj = FI_WAIT_UNSPEC,
	};
	int timeout = 2000;
	uint64_t thresh = (1ULL << 49);
	int ret;

	ret = fi_cntr_open(cxit_domain, &attr, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_cntr_wait(cntr, thresh, timeout);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS);
}

struct cntr_waiter_args {
	struct fid_cntr *cntr;
	int timeout;
	uint64_t thresh;
	uint64_t error_count;
	uint64_t success_count;
};

static void *cntr_waiter(void *data)
{
	struct cntr_waiter_args *args = data;
	uint64_t error;
	uint64_t success;
	int ret;

	ret = fi_cntr_wait(args->cntr, args->thresh, args->timeout);
	if (args->error_count && args->thresh > args->success_count) {
		cr_assert(ret == -FI_EAVAIL, "fi_cntr_wait ret %d", ret);
		error = fi_cntr_readerr(args->cntr);
		cr_assert(error == args->error_count,
			  "Unexpected counter error count %lu", error);
	} else if (args->thresh <= args->success_count) {
		cr_assert(ret == FI_SUCCESS, "fi_cntr_wait ret %d", ret);
	} else {
		cr_assert(ret == -FI_ETIMEDOUT,
			  "fi_cntr_wait ret %d", ret);
	}

	if (args->success_count) {
		success = fi_cntr_read(args->cntr);
		cr_assert(success == args->success_count,
			  "Unexpected counter success count %lu", success);
	}

	pthread_exit(NULL);
}

static void cntr_wait_success_and_error_runner(struct cntr_waiter_args *args)
{
	struct fid_cntr *cntr;
	struct fi_cntr_attr cntr_attr = {
		.wait_obj = FI_WAIT_UNSPEC,
	};
	pthread_t thread;
	pthread_attr_t attr;
	int ret;

	ret = fi_cntr_open(cxit_domain, &cntr_attr, &cntr, NULL);
	cr_assert(ret == FI_SUCCESS);
	args->cntr = cntr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	ret = pthread_create(&thread, &attr, cntr_waiter, (void *)args);
	cr_assert_eq(ret, 0, "Counter waiter create failed %d", ret);

	/* make sure wait thread is in fi_cntr_wait() */
	usleep(1000);

	if (args->success_count) {
		ret = fi_cntr_set(cntr, args->success_count);
		cr_assert(ret == FI_SUCCESS, "fi_cntr_seterr ret %d", ret);
	}

	if (args->error_count) {
		ret = fi_cntr_seterr(cntr, args->error_count);
		cr_assert(ret == FI_SUCCESS, "fi_cntr_seterr ret %d", ret);
	}

	ret = pthread_join(thread, NULL);
	cr_assert_eq(ret, 0, "Counter waiter join failed %d", ret);

	ret = fi_close(&cntr->fid);
	cr_assert(ret == FI_SUCCESS);
}

Test(cntr, cntr_wait_error_increment)
{
	struct cntr_waiter_args args = {
		.timeout = 2000,
		.thresh = 2,
		.error_count = 1,
		.success_count = 0,
	};

	cntr_wait_success_and_error_runner(&args);
}

Test(cntr, cntr_wait_success_and_error_increment)
{
	struct cntr_waiter_args args = {
		.timeout = 2000,
		.thresh = 3,
		.error_count = 1,
		.success_count = 2,
	};

	cntr_wait_success_and_error_runner(&args);
}

Test(cntr, cntr_wait_success_increment_timeout)
{
	struct cntr_waiter_args args = {
		.timeout = 1000,
		.thresh = 3,
		.error_count = 0,
		.success_count = 2,
	};

	cntr_wait_success_and_error_runner(&args);
}

Test(cntr, cntr_wait_success_increment)
{
	struct cntr_waiter_args args = {
		.timeout = 1000,
		.thresh = 3,
		.error_count = 0,
		.success_count = 4,
	};

	cntr_wait_success_and_error_runner(&args);
}
