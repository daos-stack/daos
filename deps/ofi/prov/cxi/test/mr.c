/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2020 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(mr, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

Test(mr, opt_mrs, .timeout = 60)
{
	int opt_mr_cnt = 200;
	struct mem_region opt_mrs[opt_mr_cnt];
	int i;
	uint64_t key;

	for (i = 0; i < opt_mr_cnt; i++) {
		key = i;
		mr_create(0x1000, FI_REMOTE_WRITE, 0, &key, &opt_mrs[i]);
	}


	for (i = 0; i < opt_mr_cnt; i++)
		mr_destroy(&opt_mrs[i]);
}

Test(mr, invalid_fi_directed_recv_flag)
{
	int ret;
	struct fi_mr_attr attr = {};
	struct iovec iov = {};
	struct fid_mr *mr;

	iov.iov_len = sizeof(ret);
	iov.iov_base = (void *)&ret;

	attr.mr_iov = &iov;
	attr.iov_count = 1;
	attr.access = FI_REMOTE_READ | FI_REMOTE_WRITE;
	attr.requested_key = 0x123;

	ret = fi_mr_regattr(cxit_domain, &attr, FI_DIRECTED_RECV, &mr);
	cr_assert_eq(ret, -FI_EINVAL, "fi_mr_regattr failed: %d", ret);
}

Test(mr, std_mrs, .timeout = 600, .disabled = true)
{
	int std_mr_cnt = 16*1024;
	int mrs = 0;
	struct mem_region std_mrs[std_mr_cnt];
	int i;
	int ret;
	uint64_t key;

	for (i = 0; i < std_mr_cnt; i++) {
		mrs++;
		key = i + 200;
		ret = mr_create(8, FI_REMOTE_WRITE, 0, &key, &std_mrs[i]);
		if (ret) {
			printf("Standard MR limit: %d\n", mrs);
			break;
		}
	}

	/* It's difficult to predict available resources. An idle system
	 * currently supports at least 13955 total standard MRs. This is
	 * roughly:
	 * 16k total LEs -
	 * 1000 (reserved for services) -
	 * 1400 (reserved for other pools) =
	 * 13984
	 *
	 * An EP requires a few other LEs to implement messaging and other
	 * APIs.
	 */
	cr_assert(mrs >= 13955);

	/* Note: MR close is very slow in emulation due to
	 * cxil_invalidate_pte_le().
	 */
	for (i = 0; i < mrs; i++)
		mr_destroy(&std_mrs[i]);
}

Test(mr, opt_mr_recycle, .timeout = 600, .disabled = false)
{
	int mr_cnt = 2*1024+1; // more than the total number of  PTEs
	struct mem_region mr;
	int i;
	int ret;
	uint64_t key;

	for (i = 0; i < mr_cnt; i++) {
		key = 0;
		ret = mr_create(8, FI_REMOTE_WRITE, 0, &key, &mr);
		cr_assert_eq(ret, FI_SUCCESS, "Failed to allocate MR %d\n", i);

		mr_destroy(&mr);
	}
}

/* Perform zero-byte Puts to zero-byte standard and optimized MRs. Validate
 * remote counting events.
 */
Test(mr, mr_zero_len)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	int ret;
	uint64_t key;

	/* Optimized MR */
	key = 0;

	ret = mr_create(0, FI_REMOTE_WRITE, 0, &key, &mr);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_write(cxit_ep, NULL, 0, NULL,
		       cxit_ep_fi_addr, 0, key, NULL);
	cr_assert(ret == FI_SUCCESS, "write failure %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	while (fi_cntr_read(cxit_rem_cntr) != 1)
		;

	mr_destroy(&mr);

	/* Standard MR */
	/* TODO: For FI_MR_PROV_KEY we will need to fully
	 * allocate optimized
	 */
	key = 200;
	ret = mr_create(0, FI_REMOTE_WRITE, 0, &key, &mr);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_write(cxit_ep, NULL, 0, NULL,
		       cxit_ep_fi_addr, 0, key, NULL);
	cr_assert(ret == FI_SUCCESS, "ret: %d\n", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	while (fi_cntr_read(cxit_rem_cntr) != 2)
		;

	mr_destroy(&mr);
}

/* Validate that unique keys are enforced. */
Test(mr, mr_unique_key)
{
	char buf[256];
	struct fid_mr *mr1;
	struct fid_mr *mr2;
	int ret;

	/* MR keys are enforced by the domain. */
	if (cxit_prov_key) {
		assert(1);
		return;
	}

	ret = fi_mr_reg(cxit_domain, buf, 256, FI_REMOTE_WRITE, 0, 0, 0, &mr1,
			NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_reg(cxit_domain, buf, 256, FI_REMOTE_WRITE, 0, 0, 0, &mr2,
			NULL);
	cr_assert(ret == -FI_ENOKEY);

	ret = fi_close(&mr1->fid);
	cr_assert(ret == FI_SUCCESS);
}

/* Validate not recycling non-cached FI_MR_PROV_KEY */
Test(mr, mr_recycle)
{
	char buf[256];
	struct fid_mr *mr1;
	struct fid_mr *mr2;
	struct fid_mr *mr3;
	uint64_t rkey1 = 0;
	uint64_t rkey2 = 0;
	uint64_t rkey3 = 0;
	int ret;

	/* Must be non-cached FI_MR_PROV_KEY; we rely on the fact
	 * rma EP are setup with a remote counter and bind it
	 * to the EP which forces non-cached for the MR.
	 */
	if (!cxit_prov_key) {
		assert(1);
		return;
	}

	ret = fi_mr_reg(cxit_domain, buf, 256,
			FI_REMOTE_READ | FI_REMOTE_WRITE, 0, rkey1, 0,
			&mr1, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_bind(mr1, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind MR1 failed %d", ret);

	ret = fi_mr_bind(mr1, &cxit_rem_cntr->fid, FI_REMOTE_WRITE);
	cr_assert_eq(ret, FI_SUCCESS,
		     "fi_mr_bind MR1 counter failed %d", ret);

	ret = fi_mr_enable(mr1);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable MR1 failed %d", ret);

	rkey1 = fi_mr_key(mr1);
	cr_assert_neq(rkey1, FI_KEY_NOTAVAIL, "MR1 KEY invalid %lx", rkey1);

	ret = fi_mr_reg(cxit_domain, buf, 256,
			FI_REMOTE_READ | FI_REMOTE_WRITE, 0, rkey2, 0,
			&mr2, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_bind(mr2, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind MR2 failed %d", ret);

	ret = fi_mr_bind(mr2, &cxit_rem_cntr->fid, FI_REMOTE_WRITE);
	cr_assert_eq(ret, FI_SUCCESS,
		     "fi_mr_bind MR2 counter failed %d", ret);

	ret = fi_mr_enable(mr2);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable MR2 failed %d", ret);

	rkey2 = fi_mr_key(mr2);
	cr_assert_neq(rkey2, FI_KEY_NOTAVAIL, "MR2 KEY invalid %lx", rkey2);
	cr_assert_neq(rkey2, rkey1, "MR Keys not unique");

	ret = fi_close(&mr2->fid);
	cr_assert_eq(ret, FI_SUCCESS, "close of MR2 %d", ret);

	ret = fi_mr_reg(cxit_domain, buf, 256,
			FI_REMOTE_READ | FI_REMOTE_WRITE, 0, rkey3, 0,
			&mr3, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_bind(mr3, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind MR3 failed %d", ret);

	ret = fi_mr_bind(mr3, &cxit_rem_cntr->fid, FI_REMOTE_WRITE);
	cr_assert_eq(ret, FI_SUCCESS,
		     "fi_mr_bind MR3 counter failed %d", ret);

	ret = fi_mr_enable(mr3);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable MR3 failed %d", ret);

	rkey3 = fi_mr_key(mr3);
	cr_assert_neq(rkey3, FI_KEY_NOTAVAIL, "MR3 KEY invalid %lx", rkey3);

	cr_assert_neq(rkey3, rkey1, "MR3 Key not unique");
	cr_assert_neq(rkey3, rkey2, "MR2 Key recycled");

	ret = fi_close(&mr1->fid);
	cr_assert_eq(ret, FI_SUCCESS, "close of MR1 %d", ret);
	ret = fi_close(&mr3->fid);
	cr_assert_eq(ret, FI_SUCCESS, "close of MR3 %d", ret);
}

/* Validate that RKEY are not required for local MR */
Test(mr, mr_no_local_rkey)
{
	char buf[256];
	struct fid_mr *mr1;
	struct fid_mr *mr2;
	uint64_t rkey = 0;
	uint64_t no_rkey;
	int ret;

	ret = fi_mr_reg(cxit_domain, buf, 256, FI_READ | FI_WRITE, 0, rkey, 0,
			&mr1, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_bind(mr1, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind mr1 failed %d", ret);

	ret = fi_mr_enable(mr1);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable mr1 failed %d", ret);

	no_rkey = fi_mr_key(mr1);
	cr_assert_eq(no_rkey, FI_KEY_NOTAVAIL, "No RKEY check %ld", no_rkey);

	/* Verify second local MR with same client key value passed works */
	ret = fi_mr_reg(cxit_domain, buf, 256, FI_READ | FI_WRITE, 0, rkey, 0,
			&mr2, NULL);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_bind(mr2, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind mr2 failed %d", ret);

	ret = fi_mr_enable(mr2);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable mr2 failed %d", ret);

	no_rkey = fi_mr_key(mr2);
	cr_assert_eq(no_rkey, FI_KEY_NOTAVAIL, "No RKEY check %ld", no_rkey);

	ret = fi_close(&mr2->fid);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_close(&mr1->fid);
	cr_assert(ret == FI_SUCCESS);
}


/* Test creating and destroying an MR that is never bound to an EP. */
Test(mr, no_bind)
{
	int ret;
	size_t buf_len = 0x1000;
	void *buf;
	struct fid_mr *mr;

	buf = malloc(buf_len);
	cr_assert(buf);

	/* Optimized MR */

	ret = fi_mr_reg(cxit_domain, buf, buf_len, FI_REMOTE_WRITE,
			0, 0, 0, &mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS);

	fi_close(&mr->fid);

	/* Standard MR */

	ret = fi_mr_reg(cxit_domain, buf, buf_len, FI_REMOTE_WRITE,
			0, 200, 0, &mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS);

	fi_close(&mr->fid);

	free(buf);
}

TestSuite(mr_event, .init = cxit_setup_rma_mr_events,
	  .fini = cxit_teardown_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(mr_event, counts)
{
	int ret;
	struct fi_cq_tagged_entry cqe;
	struct fid_mr *mr;
	struct cxip_mr *cxip_mr;
	uint8_t *src_buf;
	uint8_t *tgt_buf;
	int src_len = 8;
	int tgt_len = 4096;
	uint64_t key_val = 200;
	uint64_t orig_cnt;
	int matches;
	int accesses;
	uint64_t operand1;
	uint64_t result1;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_ioc result_ioc;
	struct fi_rma_ioc rma_ioc;

	/* Need remote counters */
	cxit_create_rem_cntrs();

	src_buf = malloc(src_len);
	cr_assert_not_null(src_buf, "src_buf alloc failed");

	tgt_buf = calloc(1, tgt_len);
	cr_assert_not_null(tgt_buf, "tgt_buf alloc failed");

	/* Create MR */
	ret = fi_mr_reg(cxit_domain, tgt_buf, tgt_len,
			FI_REMOTE_WRITE | FI_REMOTE_READ, 0,
			key_val, 0, &mr, NULL);
	cr_assert(ret == FI_SUCCESS);

	cxip_mr = container_of(mr, struct cxip_mr, mr_fid);

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert(ret == FI_SUCCESS);

	cr_assert(cxit_rem_cntr != NULL);
	ret = fi_mr_bind(mr, &cxit_rem_cntr->fid, FI_REMOTE_WRITE);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_enable(mr);
	cr_assert(ret == FI_SUCCESS);

	if (cxit_fi->domain_attr->mr_mode & FI_MR_PROV_KEY)
		key_val = fi_mr_key(mr);

	/* Match counts do not apply to optimized MR */
	if (cxip_generic_is_mr_key_opt(key_val))
		goto done;

	orig_cnt = fi_cntr_read(cxit_rem_cntr);

	matches = ofi_atomic_get32(&cxip_mr->match_events);
	accesses = ofi_atomic_get32(&cxip_mr->access_events);

	ret = fi_write(cxit_ep, src_buf, src_len, NULL,
		       cxit_ep_fi_addr, 0, key_val, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate remote counter was incremented correctly */
	while (orig_cnt + 1 != fi_cntr_read(cxit_rem_cntr))
		;

	/* Validate match and access counts incremented */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) >= matches + 1,
		  "Match count not updated for RMA\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) >= accesses + 1,
		  "RMA access count not updated\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) ==
		  ofi_atomic_get32(&cxip_mr->access_events),
		  "RMA matches do not equal accesses");

	matches = ofi_atomic_get32(&cxip_mr->match_events);
	accesses = ofi_atomic_get32(&cxip_mr->access_events);

	ret = fi_atomic(cxit_ep, &operand1, 1, 0, cxit_ep_fi_addr, 0,
			key_val, FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* Validate remote counter was incremented correctly */
	while (orig_cnt + 2 != fi_cntr_read(cxit_rem_cntr))
		;

	/* Validate match and access counts incremented */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) >= matches + 1,
		  "Match count not updated for atomic");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) >= accesses + 1,
		  "Atomic access count not updated");
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) ==
		  ofi_atomic_get32(&cxip_mr->access_events),
		  "Atomic matches do not equal accesses");

	matches = ofi_atomic_get32(&cxip_mr->match_events);
	accesses = ofi_atomic_get32(&cxip_mr->access_events);

	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, NULL, &result1, NULL,
			      cxit_ep_fi_addr, 0, key_val, FI_UINT64,
			      FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Validate remote counter was incremented correctly */
	while (orig_cnt + 3 != fi_cntr_read(cxit_rem_cntr))
		;

	/* Validate match and access counts incremented */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) >= matches + 1,
		  "Fetch atomic match count not updated for atomic");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) >= accesses + 1,
		  "Fetch atomic access count not updated");
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) ==
		  ofi_atomic_get32(&cxip_mr->access_events),
		  "Fetch atomic matches do not equal accesses");

	matches = ofi_atomic_get32(&cxip_mr->match_events);
	accesses = ofi_atomic_get32(&cxip_mr->access_events);

	ioc.addr = &operand1;
	ioc.count = 1;
	result_ioc.addr = &result1;
	result_ioc.count = 1;
	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key_val;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	/* Do a fetch with a flush */
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_DELIVERY_COMPLETE);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Validate remote counter was incremented correctly,
	 * once for atomic and once for flush.
	 */
	while (orig_cnt + 5 != fi_cntr_read(cxit_rem_cntr))
		;

	/* Validate match and access counts incremented */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) >= matches + 1,
		  "Fetch atomic/flush match count not updated for atomic");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) >= accesses + 1,
		  "Fetch atomic/flush access count not updated");
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) ==
		  ofi_atomic_get32(&cxip_mr->access_events),
		  "Fetch atomic flush matches do not equal accesses");

done:
	fi_close(&mr->fid);

	free(tgt_buf);
	free(src_buf);
}

Test(mr_event, not_found_counts)
{
	int ret;
	struct fi_cq_err_entry err;
	struct fi_cq_tagged_entry cqe;
	struct fid_mr *mr;
	struct cxip_mr *cxip_mr;
	uint8_t *src_buf;
	uint8_t *tgt_buf;
	int src_len = 8;
	int tgt_len = 4096;
	uint64_t key_val = 200;
	int matches;
	int accesses;
	uint64_t operand1;
	uint64_t result1;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_ioc result_ioc;
	struct fi_rma_ioc rma_ioc;

	src_buf = malloc(src_len);
	cr_assert_not_null(src_buf, "src_buf alloc failed");

	tgt_buf = calloc(1, tgt_len);
	cr_assert_not_null(tgt_buf, "tgt_buf alloc failed");

	/* Create MR */
	ret = fi_mr_reg(cxit_domain, tgt_buf, tgt_len,
			FI_REMOTE_WRITE | FI_REMOTE_READ, 0,
			key_val, 0, &mr, NULL);
	cr_assert(ret == FI_SUCCESS);

	cxip_mr = container_of(mr, struct cxip_mr, mr_fid);

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_enable(mr);
	cr_assert(ret == FI_SUCCESS);

	if (cxit_fi->domain_attr->mr_mode & FI_MR_PROV_KEY)
		key_val = fi_mr_key(mr);

	/* Match counts do not apply to optimized MR */
	if (cxip_generic_is_mr_key_opt(key_val))
		goto done;

	/* Use invalid key so that remote MR is not found */
	key_val++;

	matches = ofi_atomic_get32(&cxip_mr->match_events);
	accesses = ofi_atomic_get32(&cxip_mr->access_events);

	ret = fi_write(cxit_ep, src_buf, src_len, NULL,
		       cxit_ep_fi_addr, 0, key_val, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected RMA success %d", ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts did not increment */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count updated for RMA\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count updated for RMA\n");

	ret = fi_atomic(cxit_ep, &operand1, 1, 0, cxit_ep_fi_addr, 0,
			key_val, FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic success %d", ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts did not increment */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count updated for atomic\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count updated for atomic\n");

	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, NULL, &result1, NULL,
			      cxit_ep_fi_addr, 0, key_val, FI_UINT64,
			      FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic fetch success %d",
		     ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts did not increment */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count updated for atomic fetch\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count updated for atomic fetch\n");

	ioc.addr = &operand1;
	ioc.count = 1;
	result_ioc.addr = &result1;
	result_ioc.count = 1;
	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key_val;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	/* Do a fetch with a flush */
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_DELIVERY_COMPLETE);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic flush success %d",
		     ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts did not increment */
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count updated for atomic flush\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count updated for atomic flush\n");

done:
	fi_close(&mr->fid);

	free(tgt_buf);
	free(src_buf);
}

Test(mr_event, bounds_err_counts)
{
	int ret;
	struct fi_cq_err_entry err;
	struct fi_cq_tagged_entry cqe;
	struct fid_mr *mr;
	struct cxip_mr *cxip_mr;
	uint8_t *src_buf;
	uint8_t *tgt_buf;
	int src_len = 16;
	int tgt_len = 8;
	uint64_t key_val = 200;  /* Force client key to be standard MR */
	int matches;
	int accesses;
	uint64_t operand1;
	uint64_t result1;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_ioc result_ioc;
	struct fi_rma_ioc rma_ioc;
	struct cxip_ep *cxi_ep;

	src_buf = malloc(src_len);
	cr_assert_not_null(src_buf, "src_buf alloc failed");

	tgt_buf = calloc(1, tgt_len);
	cr_assert_not_null(tgt_buf, "tgt_buf alloc failed");

	/* Create MR */
	ret = fi_mr_reg(cxit_domain, tgt_buf, tgt_len,
			FI_REMOTE_WRITE | FI_REMOTE_READ, 0,
			key_val, 0, &mr, NULL);
	cr_assert(ret == FI_SUCCESS);

	cxip_mr = container_of(mr, struct cxip_mr, mr_fid);

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_enable(mr);
	cr_assert(ret == FI_SUCCESS);

	if (cxit_fi->domain_attr->mr_mode & FI_MR_PROV_KEY)
		key_val = fi_mr_key(mr);

	/* Match counts do not apply to optimized MR */
	if (cxip_generic_is_mr_key_opt(key_val))
		goto done;

	/* Netsim does not generate EVENT_MATCH for bounds,
	 * while hardware does. TODO: Fix this in netsim.
	 */
	cxi_ep = container_of(cxit_ep, struct cxip_ep, ep);

	matches = ofi_atomic_get32(&cxip_mr->match_events);
	accesses = ofi_atomic_get32(&cxip_mr->access_events);

	/* src len is greater than remote MR len */
	ret = fi_write(cxit_ep, src_buf, src_len, NULL,
		       cxit_ep_fi_addr, 0, key_val, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected RMA success %d", ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts increment */
	if (!is_netsim(cxi_ep->ep_obj)) {
		matches++;
		accesses++;
	}
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count mismatch for RMA\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count mismatch for RMA\n");

	/* Remote offset of 8 is greater than remote MR bounds */
	ret = fi_atomic(cxit_ep, &operand1, 1, NULL, cxit_ep_fi_addr, 8,
			key_val, FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic success %d", ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts increment */
	if (!is_netsim(cxi_ep->ep_obj)) {
		matches++;
		accesses++;
	}
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count mismatch for atomic\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count mismatch for atomic\n");

	/* Remote offset of 8 is greater than remote MR bounds */
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, NULL, &result1, NULL,
			      cxit_ep_fi_addr, 8, key_val, FI_UINT64,
			      FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic fetch success %d",
		     ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* Validate match and access counts increment */
	if (!is_netsim(cxi_ep->ep_obj)) {
		matches++;
		accesses++;
	}
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches,
		  "Match count mismatch atomic fetch\n");
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses,
		  "Access count mismatch for atomic fetch\n");

	ioc.addr = &operand1;
	ioc.count = 1;
	result_ioc.addr = &result1;
	result_ioc.count = 1;

	/* Remote offset of 8 is greater than remote MR bounds */
	rma_ioc.addr = 8;
	rma_ioc.count = 1;
	rma_ioc.key = key_val;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	/* Do a fetch with a flush */
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_DELIVERY_COMPLETE);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic flush success %d",
		     ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "Unexpected fi_cq_readerr return %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value %d", err.err);

	/* For an atomic flush with FI_DELIVERY_COMPLETE using an
	 * out-of-bounds offset we expect both the atomic and zero
	 * by flush to generate events.
	 */
	if (!is_netsim(cxi_ep->ep_obj)) {
		matches++;
		accesses++;
	}
	cr_assert(ofi_atomic_get32(&cxip_mr->match_events) == matches + 1,
		  "Match count != %d for flush with atomic error",
		  matches + 1);
	cr_assert(ofi_atomic_get32(&cxip_mr->access_events) == accesses + 1,
		  "Access count != %d for flush with atomic error",
		  accesses + 1);

done:
	fi_close(&mr->fid);

	free(tgt_buf);
	free(src_buf);
}

/*
 * With FI_MR_PROV_KEY, test if all PID IDX mapping resources required by
 * optimized MR are consumed, that falling back to standard MR is done.
 * This test should run with and without MR cache disabled.
 */
TestSuite(mr_resources, .init = cxit_setup_domain, .fini = cxit_teardown_domain,
	  .timeout = 120);

#define NUM_MR_TEST_EP	15
#define NUM_MR_PER_EP	86

Test(mr_resources, opt_fallback)
{
	struct fid_domain *dom[NUM_MR_TEST_EP];
	struct fid_ep *ep[NUM_MR_TEST_EP];
	struct fid_av *av[NUM_MR_TEST_EP];
	struct fid_cq *cq[NUM_MR_TEST_EP];
	struct fid_mr **mr;
	char buf[256];
	int ret;
	int num_dom;
	int num_mr;
	int tot_mr;

	if (!cxit_prov_key)
		return;

	mr = calloc(NUM_MR_TEST_EP * NUM_MR_PER_EP,
		    sizeof(struct fid_mr *));
	cr_assert(mr != NULL, "calloc");

	for (num_dom = 0, tot_mr = 0; num_dom < NUM_MR_TEST_EP; num_dom++) {

		ret = fi_domain(cxit_fabric, cxit_fi, &dom[num_dom], NULL);
		cr_assert(ret == FI_SUCCESS, "fi_domain");

		ret = fi_endpoint(dom[num_dom], cxit_fi, &ep[num_dom], NULL);
		cr_assert(ret == FI_SUCCESS, "fi_endpoint");

		ret = fi_av_open(dom[num_dom], &cxit_av_attr,
				 &av[num_dom], NULL);
		cr_assert(ret == FI_SUCCESS, "fi_av_open");

		ret = fi_ep_bind(ep[num_dom], &av[num_dom]->fid, 0);
		cr_assert(ret == FI_SUCCESS, "fi_ep_bind AV");

		ret = fi_cq_open(dom[num_dom], &cxit_tx_cq_attr,
				 &cq[num_dom], NULL);
		cr_assert(ret == FI_SUCCESS, "fi_cq_open");

		ret = fi_ep_bind(ep[num_dom], &cq[num_dom]->fid,
				 FI_TRANSMIT);
		cr_assert(ret == FI_SUCCESS, "fi_ep_bind TX CQ");

		ret = fi_ep_bind(ep[num_dom], &cq[num_dom]->fid,
				 FI_RECV);
		cr_assert(ret == FI_SUCCESS, "fi_ep_bind RX CQ");

		ret = fi_enable(ep[num_dom]);
		cr_assert(ret == FI_SUCCESS, "fi_enable");

		/* Create only optimized MR for this EP */
		for (num_mr = 0; num_mr < NUM_MR_PER_EP; num_mr++, tot_mr++) {

			ret = fi_mr_reg(dom[num_dom], buf, 256,
					FI_REMOTE_WRITE | FI_REMOTE_READ,
					0, 0, 0, &mr[tot_mr], NULL);
			cr_assert(ret == FI_SUCCESS, "fi_mr_reg");

			ret = fi_mr_bind(mr[tot_mr], &ep[num_dom]->fid, 0);
			cr_assert(ret == FI_SUCCESS, "fi_mr_bind");

			ret = fi_mr_enable(mr[tot_mr]);
			cr_assert(ret == FI_SUCCESS, "fi_mr_enable");
		}
	}

	/*
	 * Validate that sufficient MR were created to exhaust the PID IDX
	 * mappings of 2560. There are two mappings required for each MR
	 * and 4 PID IDX mappings required by each endpoint created.
	 */
	cr_assert(4 * num_dom + tot_mr * 2 >= 2560, "Number of MR created");

	for (num_mr = 0; num_mr < tot_mr; num_mr++) {
		ret = fi_close(&mr[num_mr]->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close MR");
	}

	for (num_dom = 0; num_dom < NUM_MR_TEST_EP; num_dom++) {
		ret = fi_close(&ep[num_dom]->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close EP");

		ret = fi_close(&cq[num_dom]->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close CQ");

		ret = fi_close(&av[num_dom]->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close AV");

		ret = fi_close(&dom[num_dom]->fid);
		cr_assert(ret == FI_SUCCESS, "fi_close Domain");
	}

	free(mr);
}
