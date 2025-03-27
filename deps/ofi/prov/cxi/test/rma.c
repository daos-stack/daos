/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>

#include "cxip.h"
#include "cxip_test_common.h"

#define RMA_WIN_KEY 0x1f

TestSuite(rma, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rma, zero_byte_writev)
{
	int ret;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;

	mr_create(0, FI_REMOTE_WRITE | FI_REMOTE_READ, 0, &key_val,
		  &mem_window);

	ret = fi_writev(cxit_ep, NULL, NULL, 0, cxit_ep_fi_addr, 0, key_val,
			NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writev failed: %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	mr_destroy(&mem_window);
}

Test(rma, zero_byte_writemsg)
{
	int ret;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct fi_rma_iov rma[1] = {};

	mr_create(0, FI_REMOTE_WRITE | FI_REMOTE_READ, 0, &key_val,
		  &mem_window);

	rma[0].key = key_val;

	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	ret = fi_writemsg(cxit_ep, &msg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed: %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	mr_destroy(&mem_window);
}

Test(rma, zero_byte_readv)
{
	int ret;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;

	mr_create(0, FI_REMOTE_WRITE | FI_REMOTE_READ, 0, &key_val,
		  &mem_window);

	ret = fi_readv(cxit_ep, NULL, NULL, 0, cxit_ep_fi_addr, 0, key_val,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_readv failed: %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

	mr_destroy(&mem_window);
}

Test(rma, zero_byte_readmsg)
{
	int ret;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct fi_rma_iov rma[1] = {};

	mr_create(0, FI_REMOTE_WRITE | FI_REMOTE_READ, 0, &key_val,
		  &mem_window);

	rma[0].key = key_val;

	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	ret = fi_readmsg(cxit_ep, &msg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_readmsg failed: %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

	mr_destroy(&mem_window);
}

static void simple_write(void)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16 * 1024;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);
	}

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test fi_write simple case. Test IDC sizes to multi-packe sizes. */
Test(rma, simple_write)
{
	simple_write();
}

/* Test compatibility of client/provider keys */
Test(rma, key_compatibility)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16 * 1024;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fid_domain *domain2;
	struct fid_ep *ep2;
	struct fid_cq *tx_cq2;
	struct fid_cq *rx_cq2;
	struct fid_av *av2;
	struct cxip_addr ep2_addr;
	size_t addrlen = sizeof(ep2_addr);
	struct cxip_addr fake_addr = {.nic = 0xad, .pid = 0xbc};
	struct cxip_domain *dom;
	struct cxip_mr_key cxip_key;
	bool first_domain_prov_key;

	/* Create second RMA endpoint in the opposite client/provider
	 * mr_mode as the test default EP. When tested with
	 * CXIP_TEST_PROV_KEY=true is set, then the second EP is started
	 * in client key mode, if not set, then the second EP is started
	 * in provider key mode.
	 */
	if (cxit_fi->domain_attr->mr_mode & FI_MR_PROV_KEY) {
		first_domain_prov_key = true;
		cxit_fi->domain_attr->mr_mode &= ~FI_MR_PROV_KEY;
		cxit_fi->domain_attr->mr_key_size = sizeof(uint32_t);
	} else {
		first_domain_prov_key = false;
		cxit_fi->domain_attr->mr_mode |= FI_MR_PROV_KEY;
		cxit_fi->domain_attr->mr_key_size = sizeof(uint64_t);
	}
	ret = fi_domain(cxit_fabric, cxit_fi, &domain2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_domain 2nd domain");
	dom = container_of(domain2, struct cxip_domain,
			   util_domain.domain_fid);
	if (first_domain_prov_key)
		cr_assert(!dom->is_prov_key, "2nd domain not client key");
	else
		cr_assert(dom->is_prov_key, "2nd domain not provider key");

	ret = fi_endpoint(domain2, cxit_fi, &ep2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_endpoint 2nd endpoint");

	ret = fi_av_open(domain2, &cxit_av_attr, &av2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_av_open 2nd AV");
	ret = fi_ep_bind(ep2, &av2->fid, 0);
	cr_assert(ret == FI_SUCCESS, "fi_ep_bind 2nd AV");

	ret = fi_cq_open(domain2, &cxit_tx_cq_attr, &tx_cq2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cq_open 2nd TX CQ");
	ret = fi_ep_bind(ep2, &tx_cq2->fid, FI_TRANSMIT);
	cr_assert(ret == FI_SUCCESS, "fi_ep_bind 2nd TX CQ");

	ret = fi_cq_open(domain2, &cxit_rx_cq_attr, &rx_cq2, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cq_open 2nd RX CQ");
	ret = fi_ep_bind(ep2, &rx_cq2->fid, FI_RECV);
	cr_assert(ret == FI_SUCCESS, "fi_ep_bind 2nd RX CQ");

	ret = fi_enable(ep2);
	cr_assert(ret == FI_SUCCESS, "fi_enable 2nd EP");

	ret = fi_getname(&ep2->fid, &ep2_addr, &addrlen);
	cr_assert(ret == FI_SUCCESS, "fi_getname 2nd EP");

	/* Setup AV, adding fake, first EP then second EP */
	ret = fi_av_insert(av2, (void *)&fake_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1);
	ret = fi_av_insert(av2, (void *)&cxit_ep_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1, "fi_av_insert 1st EP into AV2");
	ret = fi_av_insert(av2, (void *)&ep2_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1, "fi_av_insert 2nd EP into AV2");

	/* Add second EP to default EP's AV */
	ret = fi_av_insert(cxit_av, (void *)&ep2_addr, 1, NULL, 0, NULL);
	cr_assert(ret == 1, "fi_av_insert 2nd EP into cxit_av");

	/* First EP creates a MR with a key of the type specified in
	 * the associated domain. The second EP will use this key
	 * which to initiate a transfer.
	 */
	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	cxip_key.raw = key_val;
	if (first_domain_prov_key)
		cr_assert(cxip_key.is_prov, "Key is not provider key");
	else
		cr_assert(!cxip_key.is_prov, "Key is not client key");

	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		ret = fi_write(ep2, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(tx_cq2, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);
	}

	ret = fi_close(&ep2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close EP2");
	ret = fi_close(&tx_cq2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close TX CQ2");
	ret = fi_close(&rx_cq2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close RX CQ2");
	ret = fi_close(&av2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close AV2");
	ret = fi_close(&domain2->fid);
	cr_assert(ret == FI_SUCCESS, "fi_close domain2");

	mr_destroy(&mem_window);
	free(send_buf);
}

void cxit_setup_rma_opt(void)
{
	cxit_setup_getinfo();

	/* Explicitly request unordered RMA */
	cxit_fi_hints->caps = FI_RMA;
	cxit_fi_hints->tx_attr->msg_order = 0;

	cxit_setup_rma();
}

TestSuite(rma_opt, .init = cxit_setup_rma_opt, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test an optimal fi_write. */
Test(rma_opt, opt_write)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16 * 1024;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	uint64_t res_start;
	uint64_t res_end;
	uint64_t hits_start;
	uint64_t hits_end;
	struct cxip_ep *cxi_ep;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &res_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	ret = cxit_dom_read_cntr(C_CNTR_LPE_PLEC_HITS,
				 &hits_start, NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create_ext(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, NULL,
		      &mem_window);

	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS, "ret is: %d\n", ret);

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);
	}

	mr_destroy(&mem_window);
	free(send_buf);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &res_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);
	cr_expect(res_end > res_start);

	ret = cxit_dom_read_cntr(C_CNTR_LPE_PLEC_HITS,
				 &hits_end, NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	cxi_ep = container_of(cxit_ep, struct cxip_ep, ep);
	if (!is_netsim(cxi_ep->ep_obj)) {
		cr_assert(hits_end > hits_start);
	} else {
		if (hits_end == hits_start)
			printf("PLEC Hits not registered (unsupported on netsim)\n");
	}
}

/* Test simple writes to a standard MR. */
Test(rma, simple_write_std_mr)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16 * 1024;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = 0xdef;
	struct fi_cq_tagged_entry cqe;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);
	}

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test fi_writev simple case */
Test(rma, simple_writev)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct iovec iov[1];

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	/* Send 8 bytes from send buffer data to RMA window 0 */
	ret = fi_writev(cxit_ep, iov, NULL, 1, cxit_ep_fi_addr, 0, key_val,
			NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writev failed %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);
}

void do_writemsg(uint64_t flags)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Send 8 bytes from send buffer data to RMA window 0 at FI address 0
	 * (self)
	 */
	ret = fi_writemsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	if (flags & FI_CXI_HRP)
		usleep(1000);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test fi_writemsg with flags */
Test(rma, writemsg)
{
	do_writemsg(0);
	do_writemsg(FI_FENCE);
}

void cxit_rma_setup_nofence(void)
{
	cxit_setup_getinfo();
	cxit_fi_hints->caps = CXIP_EP_PRI_CAPS;
	cxit_setup_rma();
}

/* Test RMA without FI_FENCE */
Test(rma_nofence, nofence,
     .init = cxit_rma_setup_nofence,
     .fini = cxit_teardown_rma)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	ret = fi_writemsg(cxit_ep, &msg, FI_FENCE);
	cr_assert(ret == -FI_EINVAL);

	ret = fi_readmsg(cxit_ep, &msg, FI_FENCE);
	cr_assert(ret == -FI_EINVAL);

	mr_destroy(&mem_window);
	free(send_buf);
}

void cxit_rma_setup_no_rma_events(void)
{
	cxit_setup_getinfo();

	cxit_fi_hints->caps = FI_RMA | FI_ATOMIC;
	cxit_setup_rma();
}

/* Test HRP Put */
Test(rma_opt, hrp,
     .init = cxit_rma_setup_no_rma_events,
     .fini = cxit_teardown_rma)
{
	int ret;
	uint64_t hrp_acks_start;
	uint64_t hrp_acks_end;
	struct cxip_ep *cxi_ep;

	/* HRP not supported in netsim */
	cxi_ep = container_of(cxit_ep, struct cxip_ep, ep);
	if (is_netsim(cxi_ep->ep_obj))
		return;

	ret = cxit_dom_read_cntr(C_CNTR_HNI_HRP_ACK,
				 &hrp_acks_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	do_writemsg(0);
	do_writemsg(FI_CXI_HRP);
	do_writemsg(0);

	for (int i = 0; i < 10; i++)
		do_writemsg(FI_CXI_HRP);

	ret = cxit_dom_read_cntr(C_CNTR_HNI_HRP_ACK,
				 &hrp_acks_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	cr_assert_eq(hrp_acks_end - hrp_acks_start, 11,
		     "unexpected hrp_acks count: %lu\n",
		     hrp_acks_end - hrp_acks_start);
}

/* Perform a write that uses a flushing ZBR at the target. */
Test(rma, flush)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	uint64_t flags = FI_DELIVERY_COMPLETE;
	uint64_t flushes_start;
	uint64_t flushes_end;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Send 8 bytes from send buffer data to RMA window 0 at FI address 0
	 * (self)
	 */
	ret = fi_writemsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);
	cr_assert(flushes_end > flushes_start);
}

/* Test fi_writemsg with FI_INJECT flag */
Test(rma, simple_writemsg_inject)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	uint64_t flags = FI_INJECT;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Send 8 bytes from send buffer data to RMA window 0 at FI address 0
	 * (self)
	 */
	ret = fi_writemsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);

	/* Try using standard MR */

	key_val = 1000;
	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);
	rma[0].key = key_val;

	/* Send 8 bytes from send buffer data to RMA window 0 at FI address 0
	 * (self)
	 */
	ret = fi_writemsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);

	free(send_buf);
}

/* Test fi_inject_write simple case */
Test(rma, simple_inject_write)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	cr_assert(!fi_cntr_read(cxit_write_cntr));

	/* Test invalid inject length */
	ret = fi_inject_write(cxit_ep, send_buf,
			      cxit_fi->tx_attr->inject_size + 100,
			      cxit_ep_fi_addr, 0, key_val);
	cr_assert(ret == -FI_EMSGSIZE);

	/* Send 8 bytes from send buffer data to RMA window 0 */
	ret = fi_inject_write(cxit_ep, send_buf, send_len, cxit_ep_fi_addr, 0,
			      key_val);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_read(cxit_write_cntr) != 1)
		;

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&mem_window);
	free(send_buf);
}

static void simple_read(void)
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

	while (fi_cntr_read(cxit_read_cntr) != 1)
		;

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read() failed (%d)", ret);

	validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

	/* Validate sent data */
	for (int i = 0; i < local_len; i++)
		cr_expect_eq(local[i], remote.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     local[i], remote.mem[i]);

	mr_destroy(&remote);
	free(local);
}

/* Test fi_read simple case */
Test(rma, simple_read)
{
	simple_read();
}

/* Test fi_readv simple case */
Test(rma, simple_readv)
{
	int ret;
	uint8_t *local;
	int remote_len = 0x1000;
	int local_len = 8;
	uint64_t key_val = 0x2a;
	struct fi_cq_tagged_entry cqe;
	struct mem_region remote;
	struct iovec iov[1];

	local = calloc(1, local_len);
	cr_assert_not_null(local, "local alloc failed");

	mr_create(remote_len, FI_REMOTE_READ, 0x3c, &key_val, &remote);

	iov[0].iov_base = local;
	iov[0].iov_len = local_len;

	/* Get 8 bytes from the source buffer to the receive buffer */
	ret = fi_readv(cxit_ep, iov, NULL, 1, cxit_ep_fi_addr, 0, key_val,
		       NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_readv() failed (%d)", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read() failed (%d)", ret);

	validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

	/* Validate sent data */
	for (int i = 0; i < local_len; i++)
		cr_expect_eq(local[i], remote.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     local[i], remote.mem[i]);

	mr_destroy(&remote);
	free(local);
}

/* Test fi_readmsg simple case */
Test(rma, simple_readmsg)
{
	int ret;
	uint8_t *local;
	int remote_len = 0x1000;
	int local_len = 8;
	uint64_t key_val = 0x2a;
	struct fi_cq_tagged_entry cqe;
	struct mem_region remote;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	uint64_t flags = 0;

	local = calloc(1, local_len);
	cr_assert_not_null(local, "local alloc failed");

	mr_create(remote_len, FI_REMOTE_READ, 0xd9, &key_val, &remote);

	iov[0].iov_base = local;
	iov[0].iov_len = local_len;

	rma[0].addr = 0;
	rma[0].len = local_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Get 8 bytes from the source buffer to the receive buffer */
	ret = fi_readmsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_readv() failed (%d)", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read() failed (%d)", ret);

	validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

	/* Validate sent data */
	for (int i = 0; i < local_len; i++)
		cr_expect_eq(local[i], remote.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     local[i], remote.mem[i]);

	mr_destroy(&remote);
	free(local);
}

/* Test fi_readmsg failure cases */
Test(rma, readmsg_failures)
{
	int ret;
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	struct fi_msg_rma msg = {
		.msg_iov = iov,
		.rma_iov = rma,
		.iov_count = 1,
		.rma_iov_count = 1,
	};
	uint64_t flags = 0;

	/* Invalid msg value */
	ret = fi_readmsg(cxit_ep, NULL, flags);
	cr_assert_eq(ret, -FI_EINVAL, "NULL msg return %d", ret);

	msg.iov_count = cxit_fi->tx_attr->rma_iov_limit + 1;
	ret = fi_readmsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, -FI_EINVAL, "Invalid iov_count return %d", ret);

	msg.iov_count = cxit_fi->tx_attr->rma_iov_limit;
	flags = FI_DIRECTED_RECV; /* Invalid flag value */
	ret = fi_readmsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, -FI_EBADFLAGS, "Invalid flag unexpected return %d",
		     ret);
}

/* Test fi_writemsg failure cases */
Test(rma, writemsg_failures)
{
	int ret;
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	struct fi_msg_rma msg = {
		.msg_iov = iov,
		.rma_iov = rma,
		.iov_count = 1,
		.rma_iov_count = 1,
	};
	uint64_t flags = 0;
	size_t send_len = 10;
	char send_buf[send_len];

	/* Invalid msg value */
	ret = fi_writemsg(cxit_ep, NULL, flags);
	cr_assert_eq(ret, -FI_EINVAL, "NULL msg return %d", ret);

	msg.iov_count = cxit_fi->tx_attr->rma_iov_limit + 1;
	ret = fi_writemsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, -FI_EINVAL, "Invalid iov_count return %d", ret);

	msg.iov_count = cxit_fi->tx_attr->rma_iov_limit;
	flags = FI_DIRECTED_RECV; /* Invalid flag value */
	ret = fi_writemsg(cxit_ep, &msg, flags);
	cr_assert_eq(ret, -FI_EBADFLAGS, "Invalid flag return %d", ret);

	/* Invalid length */
	iov[0].iov_base = send_buf;
	iov[0].iov_len = cxit_fi->ep_attr->max_msg_size + 1;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = 0xa;
	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;

	ret = fi_writemsg(cxit_ep, &msg, 0);
	cr_assert_eq(ret, -FI_EMSGSIZE, "Invalid flag return %d", ret);

	/* Invalid inject length */
	iov[0].iov_len = C_MAX_IDC_PAYLOAD_RES+1;

	ret = fi_writemsg(cxit_ep, &msg, FI_INJECT);
	cr_assert_eq(ret, -FI_EMSGSIZE, "Invalid flag return %d", ret);
}

void rmamsg_bounds(bool write, bool opt_mr)
{
	int ret;
	struct mem_region mem_window;
	uint64_t key_val = opt_mr ? RMA_WIN_KEY : 200;
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry err;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	size_t good_len = 4096;
	char *src_buf;

	/* Create over-sized send buffer for bounds checking */
	src_buf = calloc(1, good_len * 2);
	cr_assert_not_null(src_buf, "send_buf alloc failed");
	mr_create(good_len,
		  write ? FI_REMOTE_WRITE : FI_REMOTE_READ, 0xa0,
		  &key_val, &mem_window);
	memset(mem_window.mem, 0x33, good_len);

	/* Good length to verify operation */
	iov[0].iov_base = src_buf;
	iov[0].iov_len = good_len;

	rma[0].addr = 0;
	rma[0].len = good_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	if (write)
		ret = fi_writemsg(cxit_ep, &msg, FI_COMPLETION);
	else
		ret = fi_readmsg(cxit_ep, &msg, FI_COMPLETION);

	cr_assert_eq(ret, FI_SUCCESS, "Bad RMA API status %d", ret);
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, 1, "Unexpected RMA failure");

	/* Use a bad length to cause a bounds violation and
	 * verify failure is detected.
	 */
	iov[0].iov_len = good_len * 2;
	rma[0].len = good_len * 2;

	if (write)
		ret = fi_writemsg(cxit_ep, &msg, FI_COMPLETION);
	else
		ret = fi_readmsg(cxit_ep, &msg, FI_COMPLETION);

	cr_assert_eq(ret, FI_SUCCESS, "Bad RMA return status %d", ret);

	/* There should be a source error entry. */
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected RMA success");

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert(ret == 1);
	cr_assert_eq(err.err, FI_EIO, "Error return %d", err.err);

	mr_destroy(&mem_window);
	free(src_buf);
}

Test(rma, writemsg_bounds_opt)
{
	rmamsg_bounds(true, true);
}

Test(rma, writemsg_bounds_std)
{
	rmamsg_bounds(true, false);
}

Test(rma, readmsg_bounds_opt)
{
	rmamsg_bounds(false, true);
}

Test(rma, readmsg_bounds_std)
{
	rmamsg_bounds(false, false);
}

/* Test fi_readv failure cases */
Test(rma, readv_failures)
{
	int ret;
	struct iovec iov = {};

	 /* Invalid count value */
	ret = fi_readv(cxit_ep, &iov, NULL,
		       cxit_fi->tx_attr->rma_iov_limit + 1,
		       cxit_ep_fi_addr, 0, 0, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Invalid count return %d", ret);
}

/* Test fi_writev failure cases */
Test(rma, writev_failures)
{
	int ret;
	struct iovec iov = {};

	 /* Invalid count value */
	ret = fi_writev(cxit_ep, &iov, NULL,
			cxit_fi->tx_attr->rma_iov_limit + 1,
			cxit_ep_fi_addr, 0, 0, NULL);
	cr_assert_eq(ret, -FI_EINVAL, "Invalid count return %d", ret);
}

/* Perform an RMA write spanning a page */
Test(rma, write_spanning_page)
{
	int ret;
	uint8_t *send_buf;
	uint8_t *send_addr;
	int win_len = s_page_size * 2;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	send_addr = (uint8_t *)FLOOR(send_buf + s_page_size, s_page_size) - 4;
	memset(send_addr, 0xcc, send_len);

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);
	memset(mem_window.mem, 0x33, win_len);

	/* Send 8 bytes from send buffer data to RMA window 0 */
	ret = fi_write(cxit_ep, send_addr, send_len, NULL, cxit_ep_fi_addr, 0,
		       key_val, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_addr[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_addr[i]);

	mr_destroy(&mem_window);
	free(send_buf);
}

Test(rma, rma_cleanup)
{
	int ret;
	long i;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	int writes = 50;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	for (i = 0; i < win_len; i++)
		send_buf[i] = 0xb1 * i;

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	/* Send 8 bytes from send buffer data to RMA window 0 */
	for (i = 0; i < writes; i++) {
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
				cxit_ep_fi_addr, 0, key_val, (void *)i);
		cr_assert(ret == FI_SUCCESS);
	}

	mr_destroy(&mem_window);

	/* Exit without gathering events. */
}

void cxit_setup_rma_selective_completion(void)
{
	cxit_tx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;

	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->op_flags = FI_COMPLETION;
	cxit_setup_rma();
}

/* Test selective completion behavior with RMA. */
Test(rma_sel, selective_completion,
     .init = cxit_setup_rma_selective_completion,
     .fini = cxit_teardown_rma)
{
	int ret;
	uint8_t *loc_buf;
	int win_len = 0x1000;
	int loc_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov;
	struct fi_rma_iov rma;
	int count = 0;

	loc_buf = calloc(1, win_len);
	cr_assert_not_null(loc_buf, "loc_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE | FI_REMOTE_READ, 0xa0, &key_val,
		  &mem_window);

	iov.iov_base = loc_buf;
	iov.iov_len = loc_len;

	rma.addr = 0;
	rma.key = key_val;

	msg.msg_iov = &iov;
	msg.iov_count = 1;
	msg.rma_iov = &rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Puts */

	/* Completion requested by default. */
	for (loc_len = 1; loc_len <= win_len; loc_len <<= 1) {
		ret = fi_write(cxit_ep, loc_buf, loc_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);
		count++;

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < loc_len; i++)
			cr_assert_eq(mem_window.mem[i], loc_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], loc_buf[i]);
	}

	/* Completion explicitly requested. */
	for (loc_len = 1; loc_len <= win_len; loc_len <<= 1) {
		iov.iov_len = loc_len;
		ret = fi_writemsg(cxit_ep, &msg, FI_COMPLETION);
		cr_assert(ret == FI_SUCCESS);
		count++;

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < loc_len; i++)
			cr_assert_eq(mem_window.mem[i], loc_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], loc_buf[i]);
	}

	/* Suppress completion. */
	for (loc_len = 1; loc_len <= win_len; loc_len <<= 1) {
		iov.iov_len = loc_len;
		ret = fi_writemsg(cxit_ep, &msg, 0);
		cr_assert(ret == FI_SUCCESS);
		count++;

		while (fi_cntr_read(cxit_write_cntr) != count)
			;

		/* Validate sent data */
		for (int i = 0; i < loc_len; i++)
			while (mem_window.mem[i] != loc_buf[i])
				sched_yield();

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	/* Inject never generates an event */
	loc_len = 8;
	ret = fi_inject_write(cxit_ep, loc_buf, loc_len, cxit_ep_fi_addr, 0,
			      key_val);
	cr_assert(ret == FI_SUCCESS);

	/* Validate sent data */
	for (int i = 0; i < loc_len; i++)
		while (mem_window.mem[i] != loc_buf[i])
			sched_yield();

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Gets */
	memset(loc_buf, 0, win_len);
	count = 0;

	/* Completion requested by default. */
	for (loc_len = 1; loc_len <= win_len; loc_len <<= 1) {
		memset(loc_buf, 0, loc_len);
		ret = fi_read(cxit_ep, loc_buf, loc_len, NULL,
			      cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);
		count++;

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

		/* Validate sent data */
		for (int i = 0; i < loc_len; i++)
			cr_assert_eq(mem_window.mem[i], loc_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], loc_buf[i]);
	}

	/* Completion explicitly requested. */
	for (loc_len = 1; loc_len <= win_len; loc_len <<= 1) {
		memset(loc_buf, 0, loc_len);
		iov.iov_len = loc_len;
		ret = fi_readmsg(cxit_ep, &msg, FI_COMPLETION);
		cr_assert(ret == FI_SUCCESS);
		count++;

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_READ, NULL);

		/* Validate sent data */
		for (int i = 0; i < loc_len; i++)
			cr_assert_eq(mem_window.mem[i], loc_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], loc_buf[i]);
	}

	/* Suppress completion. */
	for (loc_len = 1; loc_len <= win_len; loc_len <<= 1) {
		memset(loc_buf, 0, loc_len);
		iov.iov_len = loc_len;
		ret = fi_readmsg(cxit_ep, &msg, 0);
		cr_assert(ret == FI_SUCCESS);
		count++;

		while (fi_cntr_read(cxit_read_cntr) != count)
			;

		/* Validate sent data */
		for (int i = 0; i < loc_len; i++)
			cr_assert_eq(mem_window.mem[i], loc_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], loc_buf[i]);

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	mr_destroy(&mem_window);
	free(loc_buf);
}

void cxit_setup_rma_selective_completion_suppress(void)
{
	cxit_tx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;

	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->op_flags = 0;
	cxit_setup_rma();
}

/* Test selective completion behavior with RMA. */
Test(rma_sel, selective_completion_suppress,
     .init = cxit_setup_rma_selective_completion_suppress,
     .fini = cxit_teardown_rma)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov;
	struct fi_rma_iov rma;
	int write_count = 0;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	iov.iov_base = send_buf;
	iov.iov_len = send_len;

	rma.addr = 0;
	rma.key = key_val;

	msg.msg_iov = &iov;
	msg.iov_count = 1;
	msg.rma_iov = &rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Normal writes do not generate completions */
	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		memset(mem_window.mem, 0, send_len);
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);
		write_count++;

		while (fi_cntr_read(cxit_write_cntr) != write_count)
			;

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			while (mem_window.mem[i] != send_buf[i])
				sched_yield();

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	/* Request completions from fi_writemsg */
	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		memset(mem_window.mem, 0, send_len);
		iov.iov_len = send_len;
		ret = fi_writemsg(cxit_ep, &msg, FI_COMPLETION);
		cr_assert(ret == FI_SUCCESS);
		write_count++;

		/* Wait for async event indicating data has been sent */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);
	}

	/* Suppress completions using fi_writemsg */
	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		memset(mem_window.mem, 0, send_len);
		iov.iov_len = send_len;
		ret = fi_writemsg(cxit_ep, &msg, 0);
		cr_assert(ret == FI_SUCCESS);
		write_count++;

		while (fi_cntr_read(cxit_write_cntr) != write_count)
			;

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			while (mem_window.mem[i] != send_buf[i])
				sched_yield();

		/* Ensure no events were generated */
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert(ret == -FI_EAGAIN);
	}

	/* Inject never generates an event */
	send_len = 8;
	memset(mem_window.mem, 0, send_len);
	ret = fi_inject_write(cxit_ep, send_buf, send_len, cxit_ep_fi_addr, 0,
			      key_val);
	cr_assert(ret == FI_SUCCESS);
	write_count++;

	while (fi_cntr_read(cxit_write_cntr) != write_count)
		;

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		while (mem_window.mem[i] != send_buf[i])
			sched_yield();

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test remote counter events with RMA */
Test(rma, rem_cntr)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16 * 1024;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	int count = 0;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	for (send_len = 1; send_len <= win_len; send_len <<= 1) {
		ret = fi_write(cxit_ep, send_buf, send_len, NULL,
			       cxit_ep_fi_addr, 0, key_val, NULL);
		cr_assert(ret == FI_SUCCESS);

		/* Wait for remote counter event, then check data */
		count++;

		while (fi_cntr_read(cxit_rem_cntr) != count)
			;

		/* Validate sent data */
		for (int i = 0; i < send_len; i++)
			cr_assert_eq(mem_window.mem[i], send_buf[i],
				     "data mismatch, element: (%d) %02x != %02x\n", i,
				     mem_window.mem[i], send_buf[i]);

		/* Gather source completion after data */
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);
	}

	mr_destroy(&mem_window);
	free(send_buf);
}

/* Test RMA FI_MORE */
Test(rma, more)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 16;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	int i;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");
	for (i = 0; i < win_len; i++)
		send_buf[i] = 0xa + i;

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	ret = fi_writemsg(cxit_ep, &msg, FI_MORE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Ensure no completion before the doorbell ring */
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN,
			     "write failed %d", ret);
	} while (i++ < 100000);

	iov[0].iov_base = send_buf + send_len;
	rma[0].addr += send_len;
	ret = fi_writemsg(cxit_ep, &msg, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Wait for two events. */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);
}

Test(rma, std_mr_inject)
{
	int ret;
	uint8_t *send_buf;
	int iters = 10;
	int send_len = 8;
	int win_len = send_len * iters;
	struct mem_region mem_window;
	uint64_t key_val = CXIP_PTL_IDX_MR_OPT_CNT;
	struct fi_cq_tagged_entry cqe;
	int i;

	send_buf = calloc(1, send_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0xa0, &key_val, &mem_window);

	cr_assert(!fi_cntr_read(cxit_write_cntr));

	for (i = 0; i < iters; i++) {
		/* Send 8 bytes from send buffer data to RMA window 0 */
		ret = fi_inject_write(cxit_ep, send_buf, send_len,
				      cxit_ep_fi_addr, i * send_len, key_val);
		cr_assert(ret == FI_SUCCESS);
	}

	/* Corrupt the user buffer to make sure the NIC is not using it for an
	 * inject.
	 */
	memset(send_buf, 0xff, send_len);

	while (fi_cntr_read(cxit_write_cntr) != iters)
		;

	/* Validate sent data */
	for (int i = 0; i < win_len; i++)
		cr_assert_eq(mem_window.mem[i], 0,
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&mem_window);
	free(send_buf);
}

static void rma_invalid_target_mr_key(uint64_t rkey)
{
	int ret;
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry err;

	/* Zero byte write to invalid MR key. */
	ret = fi_inject_write(cxit_ep, NULL, 0, cxit_ep_fi_addr, 0, rkey);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_readerr(cxit_write_cntr) != 1)
		;

	/* No target event should be generated. */
	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* There should be an source error entry. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAVAIL);

	/* Expect a source error. */
	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert(ret == 1);

	/* Expect no other events. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);
}

Test(rma, invalid_target_std_mr_key)
{
	rma_invalid_target_mr_key(0x1234);
}

Test(rma, invalid_target_opt_mr_key)
{
	rma_invalid_target_mr_key(0x10);
}

Test(rma, invalid_source_mr_key)
{
	int ret;

	ret = fi_inject_write(cxit_ep, NULL, 0, cxit_ep_fi_addr, 0,
			      0x100000001);
	cr_assert(ret == -FI_EKEYREJECTED);
}

static void rma_invalid_read_target_mr_key(uint64_t rkey)
{
	int ret;
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry err;

	/* Zero byte read to invalid MR key. */
	ret = fi_read(cxit_ep, NULL, 0, NULL, cxit_ep_fi_addr, 0, rkey, NULL);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_readerr(cxit_read_cntr) != 1)
		;

	/* No target event should be generated. */
	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* There should be an source error entry. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAVAIL);

	/* Expect a source error. */
	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert(ret == 1);

	/* Expect no other events. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);
}

Test(rma, invalid_read_target_std_mr_key)
{
	rma_invalid_read_target_mr_key(0x1234);
}

Test(rma, invalid_read_target_opt_mr_key)
{
	rma_invalid_read_target_mr_key(0x10);
}

static void rma_hybrid_mr_desc_test_runner(bool write, bool cq_events)
{
	struct mem_region source_window;
	struct mem_region remote_window;
	int iters = 10;
	int send_len = 1024;
	int win_len = send_len * iters;
	uint64_t source_key = 0x2;
	uint64_t remote_key = 0x1;
	int ret;
	int i;
	struct iovec msg_iov = {};
	struct fi_rma_iov rma_iov = {};
	struct fi_msg_rma msg_rma = {};
	void *desc[1];
	struct fi_cq_tagged_entry cqe;
	uint64_t rma_flags = cq_events ? FI_TRANSMIT_COMPLETE | FI_COMPLETION :
		FI_TRANSMIT_COMPLETE;
	uint64_t cqe_flags = write ? FI_RMA | FI_WRITE : FI_RMA | FI_READ;
	struct fid_cntr *cntr = write ? cxit_write_cntr : cxit_read_cntr;

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &source_key,
			&source_window);
	cr_assert(ret == FI_SUCCESS);

	desc[0] = fi_mr_desc(source_window.mr);
	cr_assert(desc[0] != NULL);

	ret = mr_create(win_len, FI_REMOTE_READ | FI_REMOTE_WRITE, 0x3,
			&remote_key, &remote_window);
	cr_assert(ret == FI_SUCCESS);

	msg_rma.msg_iov = &msg_iov;
	msg_rma.desc = desc;
	msg_rma.iov_count = 1;
	msg_rma.addr = cxit_ep_fi_addr;
	msg_rma.rma_iov = &rma_iov;
	msg_rma.rma_iov_count = 1;

	for (i = 0; i < iters; i++) {
		msg_iov.iov_base = source_window.mem + (i * send_len);
		msg_iov.iov_len = send_len;

		rma_iov.addr = i * send_len;
		rma_iov.key = remote_key;
		rma_iov.len = send_len;

		if (write)
			ret = fi_writemsg(cxit_ep, &msg_rma, rma_flags);
		else
			ret = fi_readmsg(cxit_ep, &msg_rma, rma_flags);
		cr_assert_eq(ret, FI_SUCCESS, "Bad rc=%d\n", ret);
	}

	ret = fi_cntr_wait(cntr, iters, 1000);
	cr_assert(ret == FI_SUCCESS);

	if (cq_events) {
		for (i = 0; i < iters; i++) {
			ret = cxit_await_completion(cxit_tx_cq, &cqe);
			cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

			validate_tx_event(&cqe, cqe_flags, NULL);
		}
	}

	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	for (i = 0; i < win_len; i++)
		cr_assert_eq(source_window.mem[i], remote_window.mem[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     source_window.mem[i], remote_window.mem[i]);

	mr_destroy(&source_window);
	mr_destroy(&remote_window);
}

TestSuite(rma_hybrid_mr_desc, .init = cxit_setup_rma_hybrid_mr_desc,
	  .fini = cxit_teardown_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rma_hybrid_mr_desc, non_inject_selective_completion_write)
{
	rma_hybrid_mr_desc_test_runner(true, false);
}

Test(rma_hybrid_mr_desc, selective_completion_read)
{
	rma_hybrid_mr_desc_test_runner(false, false);
}

Test(rma_hybrid_mr_desc, non_inject_completion_write)
{
	rma_hybrid_mr_desc_test_runner(true, true);
}

Test(rma_hybrid_mr_desc, completion_read)
{
	rma_hybrid_mr_desc_test_runner(false, true);
}

static void rma_hybrid_invalid_addr_mr_desc_test_runner(bool write,
							bool cq_events)
{
	struct mem_region source_window;
	struct mem_region remote_window;
	int send_len = 1024;
	uint64_t source_key = 0x2;
	uint64_t remote_key = 0x1;
	int ret;
	struct iovec msg_iov = {};
	struct fi_rma_iov rma_iov = {};
	struct fi_msg_rma msg_rma = {};
	void *desc[1];
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry err;
	uint64_t rma_flags = cq_events ? FI_TRANSMIT_COMPLETE | FI_COMPLETION :
		FI_TRANSMIT_COMPLETE;
	struct fid_cntr *cntr = write ? cxit_write_cntr : cxit_read_cntr;

	ret = mr_create(send_len, FI_READ | FI_WRITE, 0xa, &source_key,
			&source_window);
	cr_assert(ret == FI_SUCCESS);

	desc[0] = fi_mr_desc(source_window.mr);
	cr_assert(desc[0] != NULL);

	ret = mr_create(send_len, FI_REMOTE_READ | FI_REMOTE_WRITE, 0x3,
			&remote_key, &remote_window);
	cr_assert(ret == FI_SUCCESS);

	msg_rma.msg_iov = &msg_iov;
	msg_rma.desc = desc;
	msg_rma.iov_count = 1;
	msg_rma.addr = cxit_ep_fi_addr;
	msg_rma.rma_iov = &rma_iov;
	msg_rma.rma_iov_count = 1;

	/* Generate invalid memory address. */
	msg_iov.iov_base = source_window.mem + 0xfffffffff;
	msg_iov.iov_len = send_len;

	rma_iov.key = remote_key;
	rma_iov.len = send_len;

	if (write)
		ret = fi_writemsg(cxit_ep, &msg_rma, rma_flags);
	else
		ret = fi_readmsg(cxit_ep, &msg_rma, rma_flags);
	cr_assert_eq(ret, FI_SUCCESS, "Bad rc=%d\n", ret);

	while (fi_cntr_readerr(cntr) != 1)
		;

	/* No target event should be generated. */
	ret = fi_cq_read(cxit_rx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* There should be an source error entry. */
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == -FI_EAVAIL);

	/* Expect a source error. */
	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert(ret == 1);

	/* Expect no other events. */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&source_window);
	mr_destroy(&remote_window);
}

Test(rma_hybrid_mr_desc, invalid_addr_non_inject_selective_completion_write)
{
	rma_hybrid_invalid_addr_mr_desc_test_runner(true, false);
}

Test(rma_hybrid_mr_desc, invalid_addr_selective_completion_read)
{
	rma_hybrid_invalid_addr_mr_desc_test_runner(false, false);
}

Test(rma_hybrid_mr_desc, invalid_addr_non_inject_completion_write)
{
	rma_hybrid_invalid_addr_mr_desc_test_runner(true, true);
}

Test(rma_hybrid_mr_desc, invalid_addr_completion_read)
{
	rma_hybrid_invalid_addr_mr_desc_test_runner(false, true);
}

void cxit_rma_setup_tx_alias_no_fence(void)
{
	int ret;
	uint64_t order = FI_ORDER_RMA_WAW;

	cxit_setup_getinfo();
	cxit_fi_hints->caps = CXIP_EP_PRI_CAPS;
	cxit_setup_tx_alias_rma_dc();

	/* Set WAW ordering */
	ret = fi_set_val(&cxit_tx_alias_ep->fid, FI_OPT_CXI_SET_MSG_ORDER,
			 (void *)&order);
	cr_assert_eq(ret, FI_SUCCESS, "fi_set_val(FI_OPT_SET_MSG_ORDER)");
}

/* RMA TX Alias capability */
TestSuite(rma_tx_alias, .init = cxit_rma_setup_tx_alias_no_fence,
	  .fini = cxit_teardown_tx_alias_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rma_tx_alias, flush)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	uint64_t flags = FI_DELIVERY_COMPLETE;
	uint64_t flushes_start;
	uint64_t flushes_end;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Send 8 bytes from send buffer data to RMA window 0 at FI address 0
	 * (self)
	 */
	ret = fi_writemsg(cxit_tx_alias_ep, &msg, flags);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg failed %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);
	cr_assert(flushes_end > flushes_start);
}

Test(rma_tx_alias, weak_fence)
{
	int ret;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int send_len = 8;
	int i;
	struct mem_region mem_window;
	uint64_t key_val = RMA_WIN_KEY;
	struct fi_cq_tagged_entry cqe;
	struct fi_msg_rma msg = {};
	struct iovec iov[1];
	struct fi_rma_iov rma[1];
	uint64_t flags = FI_DELIVERY_COMPLETE;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	for (i = 0; i < send_len*2; i++)
		send_buf[i] = i;

	mr_create(win_len, FI_REMOTE_WRITE, 0x44, &key_val, &mem_window);

	iov[0].iov_base = send_buf;
	iov[0].iov_len = send_len;

	rma[0].addr = 0;
	rma[0].len = send_len;
	rma[0].key = key_val;

	msg.msg_iov = iov;
	msg.iov_count = 1;
	msg.rma_iov = rma;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;

	/* Verify FI_FENCE can not be done with original EP */
	ret = fi_writemsg(cxit_ep, &msg, flags | FI_FENCE);
	cr_assert_eq(ret, -FI_EINVAL, "fi_writemsg FI_FENCE ret %d", ret);

	/* Verify FI_CXI_WEAK_FENCE can be done with original EP */
	ret = fi_writemsg(cxit_ep, &msg, flags | FI_CXI_WEAK_FENCE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg FI_WEAK_FENCE ret %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	/* Verifiy FI_CXI_WEAK_FENCE can be done with alias EP */
	rma[0].addr = send_len;
	iov[0].iov_base = send_buf + send_len;
	ret = fi_writemsg(cxit_tx_alias_ep, &msg, flags | FI_CXI_WEAK_FENCE);
	cr_assert_eq(ret, FI_SUCCESS, "fi_writemsg FI_WEAK_FENCE ret %d", ret);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < send_len * 2; i++)
		cr_assert_eq(mem_window.mem[i], send_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     mem_window.mem[i], send_buf[i]);

	mr_destroy(&mem_window);
	free(send_buf);
}

TestSuite(rma_mr_event, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test that use of stale MR keys cannot access cached memory */
Test(rma_mr_event, stale_key)
{
	int ret;
	long i;
	struct fi_cq_err_entry err;
	struct fi_cq_tagged_entry cqe;
	struct fid_mr *mr;
	struct cxip_mr *cxip_mr;
	uint8_t *src_buf;
	uint8_t *src_buf2;
	uint8_t *tgt_buf;
	int src_len = 8;
	int tgt_len = 4096;
	uint64_t key_val = 200;

	src_buf = malloc(src_len);
	cr_assert_not_null(src_buf, "src_buf alloc failed");
	src_buf2 = malloc(src_len);
	cr_assert_not_null(src_buf2, "src_buf2 alloc failed");
	tgt_buf = calloc(1, tgt_len);
	cr_assert_not_null(tgt_buf, "tgt_buf alloc failed");

	for (i = 0; i < src_len; i++) {
		src_buf[i] = 0xb1 * i;
		src_buf2[i] = 0xa1 * i;
	}

	/* Create MR */
	ret = fi_mr_reg(cxit_domain, tgt_buf, tgt_len, FI_REMOTE_WRITE, 0,
			key_val, 0, &mr, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* We known cached FI_MR_PROV_KEY cannot support this
	 * level of robustness, so just skip FI_MR_PROV_KEY
	 * unless FI_CXI_MR_MATCH_EVENTS is enabled.
	 */
	cxip_mr = container_of(mr, struct cxip_mr, mr_fid);
	if (cxit_fi->domain_attr->mr_mode & FI_MR_PROV_KEY &&
	    !cxip_mr->count_events) {
		fi_close(&mr->fid);
		goto done;
	}

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert(ret == FI_SUCCESS);

	ret = fi_mr_enable(mr);
	cr_assert(ret == FI_SUCCESS);

	if (cxit_fi->domain_attr->mr_mode & FI_MR_PROV_KEY)
		key_val = fi_mr_key(mr);

	ret = fi_write(cxit_ep, src_buf, src_len, NULL,
		       cxit_ep_fi_addr, 0, key_val, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	/* Validate sent data */
	for (int i = 0; i < src_len; i++)
		cr_assert_eq(tgt_buf[i], src_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     tgt_buf[i], src_buf[i]);

	/* Close MR but leave memory backing it allocated/cached */
	fi_close(&mr->fid);

	/* Try to access using stale key */
	ret = fi_write(cxit_ep, src_buf2, src_len, NULL,
		       cxit_ep_fi_addr, 0, key_val, NULL);
	cr_assert(ret == FI_SUCCESS);

	/* Wait for async event indicating data has been sent */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected RMA success %d", ret);

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1);
	cr_assert_eq(err.err, FI_EIO, "Error return %d", err.err);

	/* Verfiy data was not modified with src_buf2 data */
	for (int i = 0; i < src_len; i++)
		cr_assert_eq(tgt_buf[i], src_buf[i],
			     "data mismatch, element: (%d) %02x != %02x\n", i,
			     tgt_buf[i], src_buf[i]);

done:
	free(tgt_buf);
	free(src_buf);
	free(src_buf2);
}

/* Note: the FI_PROTO_CXI_RNR should not alter the standard RMA
 * and Atomic functions. Perform a limited set of writes and reads to
 * verify this.
 */
TestSuite(rnr_rma, .init = cxit_setup_rnr_msg_ep,
	  .fini = cxit_teardown_msg, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(rnr_rma, simple_write)
{
	simple_write();
}

Test(rnr_rma, simple_read)
{
	simple_read();
}
