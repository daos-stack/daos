/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include <ofi.h>

#include "cxip.h"
#include "cxip_test_common.h"

#define	AMO_DISABLED	false

#define RMA_WIN_LEN	64
#define RMA_WIN_KEY	2
#define RMA_WIN_ACCESS	(FI_REMOTE_READ | FI_REMOTE_WRITE)
#define MR_KEY_STD	200

/* Create MR -- works like a "remote_calloc()" */
static void *_cxit_create_mr(struct mem_region *mr, uint64_t *key)
{
	int ret;

	mr->mem = calloc(1, RMA_WIN_LEN);
	cr_assert_not_null(mr->mem);

	ret = fi_mr_reg(cxit_domain, mr->mem, RMA_WIN_LEN, RMA_WIN_ACCESS, 0,
			*key, 0, &mr->mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed %d", ret);

	ret = fi_mr_bind(mr->mr, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind(ep) failed %d", ret);

	if (cxit_fi->caps & FI_RMA_EVENT && cxit_rem_cntr) {
		ret = fi_mr_bind(mr->mr, &cxit_rem_cntr->fid, FI_REMOTE_WRITE);
		cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind(cntr) failed %d",
			     ret);
	}

	ret = fi_mr_enable(mr->mr);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable failed %d", ret);

	*key = fi_mr_key(mr->mr);

	return mr->mem;
}

/* Destroy MR -- works like a "remote_free()" */
static void _cxit_destroy_mr(struct mem_region *mr)
{
	fi_close(&mr->mr->fid);

	free(mr->mem);
}

/* Test failures associated with bad call parameters.
 */
TestSuite(atomic_invalid, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .disabled = AMO_DISABLED, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(atomic_invalid, invalid_amo)
{
	uint64_t operand1 = 0;
	struct fi_ioc iov = {
		.addr = &operand1,
		.count = 1
	};
	int ret;

	ret = fi_atomic(cxit_ep, &operand1, 1, 0, cxit_ep_fi_addr, 0, 0,
			FI_UINT64, FI_ATOMIC_OP_LAST, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomic(cxit_ep, &operand1, 1, 0, cxit_ep_fi_addr, 0, 0,
			FI_UINT64, -1, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomic(cxit_ep, &operand1, 1, 0, cxit_ep_fi_addr, 0, 0,
			FI_DATATYPE_LAST, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomic(cxit_ep, &operand1, 1, 0, cxit_ep_fi_addr, 0, 0,
			-1, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomic(cxit_ep, &operand1, 0, 0, cxit_ep_fi_addr, 0, 0,
			FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomic(cxit_ep, &operand1, 2, 0, cxit_ep_fi_addr, 0, 0,
			FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomic(cxit_ep, 0, 1, 0, cxit_ep_fi_addr, 0, 0,
			FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);

	ret = fi_atomicv(cxit_ep, &iov, 0, 0, cxit_ep_fi_addr, 0, 0,
			 FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomicv(cxit_ep, &iov, 0, 2, cxit_ep_fi_addr, 0, 0,
			 FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	iov.count = 0;
	ret = fi_atomicv(cxit_ep, &iov, 0, 1, cxit_ep_fi_addr, 0, 0,
			 FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	iov.count = 2;
	ret = fi_atomicv(cxit_ep, &iov, 0, 1, cxit_ep_fi_addr, 0, 0,
			 FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_atomicv(cxit_ep, 0, 0, 1, cxit_ep_fi_addr, 0, 0,
			 FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
}

Test(atomic_invalid, invalid_fetch)
{
	uint64_t operand1 = 0;
	uint64_t result = 0;
	struct fi_ioc iov = {
		.addr = &operand1,
		.count = 1
	};
	struct fi_ioc riov = {
		.addr = &result,
		.count = 1
	};
	int ret;

	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, FI_UINT64,
			      FI_ATOMIC_OP_LAST, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, FI_UINT64, -1, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, FI_DATATYPE_LAST, FI_SUM,
			      0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, -1, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0, 0, 0,
			      cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, &operand1, 0, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, &operand1, 2, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomic(cxit_ep, 0, 1, 0, &result, 0,
			      cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);


	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, &riov, 0, 0,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, &riov, 0, 2,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, 0, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 0, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 2, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_fetch_atomicv(cxit_ep, 0, 0, 1, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	riov.count = 0;
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	riov.count = 2;
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	riov.count = 1;
	iov.count = 0;
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	iov.count = 2;
	ret = fi_fetch_atomicv(cxit_ep, &iov, 0, 1, &riov, 0, 1,
			       cxit_ep_fi_addr, 0, 0, FI_UINT64, FI_SUM, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	iov.count = 1;
	cr_assert_eq(ret, -FI_EINVAL);
}

Test(atomic_invalid, invalid_swap)
{
	uint64_t operand1 = 0;
	uint64_t compare = 0;
	uint64_t result = 0;
	struct fi_ioc iov = {
		.addr = &operand1,
		.count = 1
	};
	struct fi_ioc ciov = {
		.addr = &compare,
		.count = 1
	};
	struct fi_ioc riov = {
		.addr = &result,
		.count = 1
	};
	int ret;

	ret = fi_compare_atomic(cxit_ep,
				&operand1, 1, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_ATOMIC_OP_LAST, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 1, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, -1, 0);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 1, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_DATATYPE_LAST, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 1, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				-1, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 1, 0,
				&compare, 0,
				0, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 1, 0,
				0, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 2, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				&operand1, 0, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomic(cxit_ep,
				0, 1, 0,
				&compare, 0,
				&result, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);

	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 2,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 0,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 2,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 0,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 2,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 0,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	riov.count = 2;
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	riov.count = 0;
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	riov.count = 1;
	ciov.count = 2;
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ciov.count = 0;
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	ciov.count = 1;
	iov.count = 2;
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	iov.count = 0;
	ret = fi_compare_atomicv(cxit_ep,
				&iov, 0, 1,
				&ciov, 0, 1,
				&riov, 0, 1,
				cxit_ep_fi_addr, 0, 0,
				FI_UINT64, FI_CSWAP_NE, NULL);
	cr_assert_eq(ret, -FI_EINVAL);
	iov.count = 1;
}

/* Test simple operations: AMO SUM UINT64_T, FAMO SUM UINT64_T, and CAMO SWAP_NE
 * UINT64_T. If this doesn't work, nothing else will.
 */
TestSuite(atomic, .init = cxit_setup_rma, .fini = cxit_teardown_rma,
	  .disabled = AMO_DISABLED, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(atomic, simple_amo)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote;
	uint64_t *rma;
	int ret;
	int i;
	uint64_t key;

	/* Test standard and optimized MRs. */
	for (i = 0; i < 2; i++) {
		key = 199 + i;

		rma = _cxit_create_mr(&mr, &key);
		exp_remote = 0;
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);

		operand1 = 1;
		exp_remote += operand1;
		ret = fi_atomic(cxit_ep, &operand1, 1, 0,
				cxit_ep_fi_addr, 0, key,
				FI_UINT64, FI_SUM, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);

		operand1 = 3;
		exp_remote += operand1;
		ret = fi_atomic(cxit_ep, &operand1, 1, 0,
				cxit_ep_fi_addr, 0, key,
				FI_UINT64, FI_SUM, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);

		operand1 = 9;
		exp_remote += operand1;
		ret = fi_atomic(cxit_ep, &operand1, 1, 0,
				cxit_ep_fi_addr, 0, key,
				FI_UINT64, FI_SUM, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);

		_cxit_destroy_mr(&mr);
	}
}

/* Test atomic inject interface */
Test(atomic, simple_inject)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote = 0;
	uint64_t *rma;
	int ret;
	int count = 0;
	uint64_t key = RMA_WIN_KEY;

	rma = _cxit_create_mr(&mr, &key);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;
	exp_remote += operand1;
	ret = fi_inject_atomic(cxit_ep, &operand1, 1,
			       cxit_ep_fi_addr, 0, key,
			       FI_UINT64, FI_SUM);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 3;
	exp_remote += operand1;
	ret = fi_inject_atomic(cxit_ep, &operand1, 1,
			       cxit_ep_fi_addr, 0, key,
			       FI_UINT64, FI_SUM);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 9;
	exp_remote += operand1;
	ret = fi_inject_atomic(cxit_ep, &operand1, 1,
			       cxit_ep_fi_addr, 0, key,
			       FI_UINT64, FI_SUM);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure no events were delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	_cxit_destroy_mr(&mr);

	/* Try using standard MR */

	exp_remote = 0;
	key = 1000;
	rma = _cxit_create_mr(&mr, &key);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;
	exp_remote += operand1;
	ret = fi_inject_atomic(cxit_ep, &operand1, 1,
			       cxit_ep_fi_addr, 0, key,
			       FI_UINT64, FI_SUM);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure no events were delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	_cxit_destroy_mr(&mr);
}

Test(atomic, simple_fetch)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote;
	uint64_t exp_result;
	uint64_t *rma;
	uint64_t *loc;
	int ret;
	int i;
	uint64_t key;

	for (i = 0; i < 2; i++) {
		key = 199 + i;

		rma = _cxit_create_mr(&mr, &key);
		exp_remote = 0;
		exp_result = 0;
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);

		loc = calloc(1, RMA_WIN_LEN);
		cr_assert_not_null(loc);

		fi_cntr_set(cxit_read_cntr, 0);
		while (fi_cntr_read(cxit_read_cntr));

		operand1 = 1;
		*loc = -1;
		exp_result = exp_remote;
		exp_remote += operand1;
		ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0,
				      loc, 0,
				      cxit_ep_fi_addr, 0, key,
				      FI_UINT64, FI_SUM, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Add Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		operand1 = 3;
		*loc = -1;
		exp_result = exp_remote;
		exp_remote += operand1;
		ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0,
				      loc, 0,
				      cxit_ep_fi_addr, 0, key,
				      FI_UINT64, FI_SUM, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Add Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		operand1 = 9;
		*loc = -1;
		exp_result = exp_remote;
		exp_remote += operand1;
		ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0,
				      loc, 0,
				      cxit_ep_fi_addr, 0, key,
				      FI_UINT64, FI_SUM, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Add Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		while (fi_cntr_read(cxit_read_cntr) != 3)
			;

		free(loc);
		_cxit_destroy_mr(&mr);
	}
}

Test(atomic, simple_fetch_read)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t exp_remote;
	uint64_t exp_result;
	uint64_t *rma;
	uint64_t *loc;
	int ret;
	int i;
	uint64_t key;

	for (i = 0; i < 2; i++) {
		key = 199 + i;

		rma = _cxit_create_mr(&mr, &key);
		exp_remote = 0;
		exp_result = 0;
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);
		loc = calloc(1, RMA_WIN_LEN);
		cr_assert_not_null(loc);

		fi_cntr_set(cxit_read_cntr, 0);
		while (fi_cntr_read(cxit_read_cntr))
			;
		*rma = 1;
		*loc = -1;
		exp_remote = *rma;
		exp_result = exp_remote;

		ret = fi_fetch_atomic(cxit_ep, NULL, 1, NULL,
				      loc, 0,
				      cxit_ep_fi_addr, 0, key,
				      FI_UINT64, FI_ATOMIC_READ, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Read Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		*rma = 10;
		*loc = -1;
		exp_remote = *rma;
		exp_result = exp_remote;

		ret = fi_fetch_atomic(cxit_ep, NULL, 1, NULL,
				      loc, 0,
				      cxit_ep_fi_addr, 0, key,
				      FI_UINT64, FI_ATOMIC_READ, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Read Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		*rma = 0x0123456789abcdef;
		*loc = -1;
		exp_remote = *rma;
		exp_result = exp_remote;

		ret = fi_fetch_atomic(cxit_ep, NULL, 1, NULL,
				      loc, 0,
				      cxit_ep_fi_addr, 0, key,
				      FI_UINT64, FI_ATOMIC_READ, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Read Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		while (fi_cntr_read(cxit_read_cntr) != 3)
			;

		free(loc);
		_cxit_destroy_mr(&mr);
	}
}

Test(atomic, simple_swap)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t compare;
	uint64_t exp_remote;
	uint64_t exp_result;
	uint64_t *rma;
	uint64_t *loc;
	int ret;
	int i;
	uint64_t key;

	for (i = 0; i < 2; i++) {
		key = 199 + i;

		rma = _cxit_create_mr(&mr, &key);
			exp_remote = 0;
			exp_result = 0;
		cr_assert_eq(*rma, exp_remote,
			     "Result = %ld, expected = %ld",
			     *rma, exp_remote);

		loc = calloc(1, RMA_WIN_LEN);
		cr_assert_not_null(loc);

		*rma = 0;	/* remote == 0 */
		operand1 = 1;	/* change remote to 1 */
		compare = 2;	/* if remote != 2 (true) */
		*loc = -1;	/* initialize result */
		exp_remote = 1;	/* expect remote == 1 */
		exp_result = 0;	/* expect result == 0 */
		ret = fi_compare_atomic(cxit_ep,
					&operand1, 1, 0,
					&compare, 0,
					loc, 0,
					cxit_ep_fi_addr, 0, key,
					FI_UINT64, FI_CSWAP_NE, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Add Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		*rma = 2;	/* remote == 2 */
		operand1 = 1;	/* change remote to 1 */
		compare = 2;	/* if remote != 2 (false) */
		*loc = -1;	/* initialize result */
		exp_remote = 2;	/* expect remote == 2 (no op) */
		exp_result = 2;	/* expect result == 2 (does return value) */
		ret = fi_compare_atomic(cxit_ep,
					&operand1, 1, 0,
					&compare, 0,
					loc, 0,
					cxit_ep_fi_addr, 0, key,
					FI_UINT64, FI_CSWAP_NE, NULL);
		cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
		cr_assert_eq(*rma, exp_remote,
			     "Add Result = %ld, expected = %ld",
			     *rma, exp_remote);
		cr_assert_eq(*loc, exp_result,
			     "Fetch Result = %016lx, expected = %016lx",
			     *loc, exp_result);

		free(loc);
		_cxit_destroy_mr(&mr);
	}
}

/* Perform a full combinatorial test suite.
 */
#define	MAX_TEST_SIZE	16

/**
 * Compare a seen value with an expected value, with 'len' valid bytes. This
 * checks the seen buffer all the way to MAX_TEST_SIZE, and looks for a
 * predefined value in every byte, to ensure that there is no overflow.
 * The seen buffer will always be either the rma or the loc buffer, which have
 * 64 bytes of space in them.
 *
 * Summation of real and complex types is trickier. Every decimal constant is
 * internally represented by a binary approximation, and summation can
 * accumulate errors. With only a single sum with two arguments, the error could
 * be +1 or -1 in the LSBit.
 *
 * @param saw 'seen' buffer
 * @param exp 'expected' value
 * @param len number of valid bytes
 *
 * @return bool true if successful, false if comparison fails
 */
static bool _compare(void *saw, void *exp, int len,
		     enum fi_op op, enum fi_datatype dt)
{
	uint8_t *bval = saw;
	uint8_t *bexp = exp;
	uint64_t uval = 0;
	uint64_t uexp = 0;
	int i;

	/* Test MS pad bits */
	for (i = MAX_TEST_SIZE-1; i >= len; i--) {
		if (bval[i] != bexp[i])
			return false;
	}
	if (op == FI_SUM) {
		switch (dt) {
		case FI_FLOAT:
		case FI_DOUBLE:
			/* Copy to UINT64, adjust diff (-1,1) to (0,2) */
			memcpy(&uval, bval, len);
			memcpy(&uexp, bexp, len);
			if ((uval - uexp) + 1 > 2)
				return false;
			return true;
		case FI_FLOAT_COMPLEX:
		case FI_DOUBLE_COMPLEX:
			/* Do real and imag parts separately */
			memcpy(&uval, bval, len/2);
			memcpy(&uexp, bexp, len/2);
			if (uval - uexp + 1 > 2)
				return false;
			memcpy(&uval, bval+len/2, len/2);
			memcpy(&uexp, bexp+len/2, len/2);
			if (uval - uexp + 1 > 2)
				return false;
			return true;
		default:
			break;
		}
	}
	/* Test LS value bits */
	for (i = len-1; i >= 0; i--) {
		if (bval[i] != bexp[i])
			return false;
	}
	return true;
}

/**
 * Generates a useful error message.
 *
 * @param op opcode
 * @param dt dtcode
 * @param saw 'seen' buffer
 * @param exp 'expected' value
 * @param len number of valid bytes
 * @param buf buffer to fill with message
 * @param siz buffer size
 *
 * @return const char* returns the buf pointer
 */
static const char *_errmsg(enum fi_op op, enum fi_datatype dt,
			   void *saw, void *exp, int len,
			   char *buf, size_t siz)
{
	char *p = &buf[0];
	char *e = &buf[siz];
	uint8_t *bsaw = saw;
	uint8_t *bexp = exp;
	int i;

	p += snprintf(p, e-p, "%d:%d: saw=", op, dt);
	for (i = MAX_TEST_SIZE-1; i >= 0; i--)
		p += snprintf(p, e-p, "%02x%s", bsaw[i], i == len ? "/" : "");
	p += snprintf(p, e-p, " exp=");
	for (i = MAX_TEST_SIZE-1; i >= 0; i--)
		p += snprintf(p, e-p, "%02x%s", bexp[i], i == len ? "/" : "");
	return buf;
}

/**
 * The general AMO test.
 *
 * @param index value used to help identify the test if error
 * @param dt FI datatype
 * @param op FI operation
 * @param err 0 if success expected, 1 if failure expected
 * @param operand1 operation data value pointer
 * @param compare operation compare value pointer
 * @param loc operation result (local) buffer pointer
 * @param loc_init operation result initialization value pointer
 * @param rma operation rma (remote) buffer pointer
 * @param rma_init operation rma initialization value pointer
 * @param rma_expect operation rma (remote) expected value pointer
 */
static void _test_amo(int index, enum fi_datatype dt, enum fi_op op, int err,
		      void *operand1,
		      void *compare,
		      void *loc, void *loc_init,
		      void *rma, void *rma_init, void *rma_expect,
		      uint64_t key)
{
	struct fi_cq_tagged_entry cqe;
	char msgbuf[128];
	char opstr[64];
	char dtstr[64];
	uint8_t rexp[MAX_TEST_SIZE];
	uint8_t lexp[MAX_TEST_SIZE];
	void *rma_exp = rexp;
	void *loc_exp = lexp;
	int len = ofi_datatype_size(dt);
	int ret;

	strcpy(opstr, fi_tostr(&op, FI_TYPE_ATOMIC_OP));
	strcpy(dtstr, fi_tostr(&dt, FI_TYPE_ATOMIC_TYPE));

	cr_log_info("Testing %s %s (%d)\n", opstr, dtstr, len);

	memset(rma, -1, MAX_TEST_SIZE);
	memset(rma_exp, -1, MAX_TEST_SIZE);
	memcpy(rma, rma_init, len);
	memcpy(rma_exp, rma_expect, len);

	if (loc && loc_init) {
		memset(loc, -1, MAX_TEST_SIZE);
		memset(loc_exp, -1, MAX_TEST_SIZE);
		memcpy(loc, loc_init, len);
		memcpy(loc_exp, rma_init, len);
	}
	if (compare && loc) {
		/* This is a compare command */
		ret = fi_compare_atomic(cxit_ep, operand1, 1, 0,
					compare, 0, loc, 0,
					cxit_ep_fi_addr, 0, key, dt,
					op, NULL);
	} else if (loc) {
		/* This is a fetch command */
		ret = fi_fetch_atomic(cxit_ep, operand1, 1, 0, loc, 0,
				      cxit_ep_fi_addr, 0, key, dt, op,
				      NULL);
	} else {
		/* This is a simple command */
		ret = fi_atomic(cxit_ep, operand1, 1, 0,
				cxit_ep_fi_addr, 0, key, dt, op, NULL);
	}

	if (err) {
		/* Expected an error. Tests only invoke "unsupported" failures,
		 * so any other error is fatal. Success is also fatal if we
		 * expect a failure.
		 */
		cr_assert_eq(ret, -FI_EOPNOTSUPP,
			     "rtn #%d:%d:%d saw=%d exp=%d\n",
			     index, op, dt, ret, -FI_EOPNOTSUPP);
		return;
	}


	/* If we weren't expecting an error, any error is fatal  */
	cr_assert_eq(ret, 0,
		     "rtn #%d:%d:%d saw=%d exp=%d\n",
		     index, op, dt, ret, err);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | (loc ? FI_READ : FI_WRITE), NULL);

	/* We expect the RMA effect to be as predicted */
	cr_expect(_compare(rma, rma_exp, len, op, dt),
		  "rma #%d:%s\n", index,
		  _errmsg(op, dt, rma, rma_exp, len, msgbuf,
			  sizeof(msgbuf)));

	/* We expect the local result to be as predicted, if there is one */
	if (loc && loc_init) {
		cr_expect(_compare(loc, loc_exp, len, op, dt),
			  "loc #%d:%s\n", index,
			  _errmsg(op, dt, loc, loc_exp, len, msgbuf,
				  sizeof(msgbuf)));
	}
}

/* Every parameter list can create an OR of the following values, to indicate
 * what forms should be attempted.
 */
#define	_AMO	1
#define	_FAMO	2
#define	_CAMO	4

/* The INT tests test 8, 16, 32, and 64 bits for each line item.
 */
struct test_int_parms {
	int opmask;
	int index;
	enum fi_op op;
	int err;
	uint64_t comp;
	uint64_t o1;
	uint64_t rini;
	uint64_t rexp;
	uint64_t key;
};

static struct test_int_parms int_parms[] = {
	{ _AMO|_FAMO, 11, FI_MIN,  0, 0, 123, 120, 120 },
	{ _AMO|_FAMO, 12, FI_MIN,  0, 0, 120, 123, 120 },
	{ _AMO|_FAMO, 21, FI_MAX,  0, 0, 123, 120, 123 },
	{ _AMO|_FAMO, 22, FI_MAX,  0, 0, 120, 123, 123 },
	{ _AMO|_FAMO, 31, FI_SUM,  0, 0,   1,   0,   1 },
	{ _AMO|_FAMO, 32, FI_SUM,  0, 0,   1,  10,  11 },
	{ _AMO|_FAMO, 33, FI_SUM,  0, 0,   2,  -1,   1 },
	{ _AMO|_FAMO, 41, FI_LOR,  0, 0,   0,   0,   0 },
	{ _AMO|_FAMO, 42, FI_LOR,  0, 0, 128,   0,   1 },
	{ _AMO|_FAMO, 43, FI_LOR,  0, 0,   0, 128,   1 },
	{ _AMO|_FAMO, 44, FI_LOR,  0, 0,  64, 128,   1 },
	{ _AMO|_FAMO, 51, FI_LAND, 0, 0,   0,   0,   0 },
	{ _AMO|_FAMO, 52, FI_LAND, 0, 0, 128,   0,   0 },
	{ _AMO|_FAMO, 53, FI_LAND, 0, 0,   0, 128,   0 },
	{ _AMO|_FAMO, 54, FI_LAND, 0, 0,  64, 128,   1 },
	{ _AMO|_FAMO, 61, FI_LXOR, 0, 0,   0,   0,   0 },
	{ _AMO|_FAMO, 62, FI_LXOR, 0, 0, 128,   0,   1 },
	{ _AMO|_FAMO, 63, FI_LXOR, 0, 0,   0, 128,   1 },
	{ _AMO|_FAMO, 64, FI_LXOR, 0, 0,  64, 128,   0 },
	{ _AMO|_FAMO, 71, FI_BOR,  0, 0,
		0xf0e1f2e3f4e5f6e7,
		0x1818181818181818,
		0xf8f9fafbfcfdfeff },
	{ _AMO|_FAMO, 81, FI_BAND, 0, 0,
		0xf0e1f2e3f4e5f6e7,
		0x1818181818181818,
		0x1000100010001000 },
	{ _AMO|_FAMO, 91, FI_BXOR, 0, 0,
		0xf0e1f2e3f4e5f6e7,
		0x1818181818181818,
		0xe8f9eafbecfdeeff },
	{ _AMO|_FAMO, 101, FI_ATOMIC_WRITE, 0, 0,
		0x1234123412341234,
		0xabcdabcdabcdabcd,
		0x1234123412341234 },
	{ _AMO|_FAMO, 102, FI_ATOMIC_WRITE, 0, 0,
		0x1234123412341234,
		0x1234123412341234,
		0x1234123412341234 },
	{ _FAMO, 111, FI_ATOMIC_READ, 0, 0,
		0x1010101010101010,
		0x4321432143214321,
		0x4321432143214321 },
	{ _AMO, 112, FI_ATOMIC_READ, 1 },
	{ _CAMO, 121, FI_CSWAP,     0, 120, 123, 100, 100 },
	{ _CAMO, 122, FI_CSWAP,     0, 100, 123, 100, 123 },
	{ _CAMO, 131, FI_CSWAP_NE,  0, 120, 123, 100, 123 },
	{ _CAMO, 132, FI_CSWAP_NE,  0, 100, 123, 100, 100 },
	{ _CAMO, 141, FI_CSWAP_LE,  0, 101, 123, 100, 100 },
	{ _CAMO, 142, FI_CSWAP_LE,  0, 100, 123, 100, 123 },
	{ _CAMO, 143, FI_CSWAP_LE,  0,  99, 123, 100, 123 },
	{ _CAMO, 151, FI_CSWAP_LT,  0, 101, 123, 100, 100 },
	{ _CAMO, 152, FI_CSWAP_LT,  0, 100, 123, 100, 100 },
	{ _CAMO, 153, FI_CSWAP_LT,  0,  99, 123, 100, 123 },
	{ _CAMO, 161, FI_CSWAP_GE,  0, 101, 123, 100, 123 },
	{ _CAMO, 162, FI_CSWAP_GE,  0, 100, 123, 100, 123 },
	{ _CAMO, 163, FI_CSWAP_GE,  0,  99, 123, 100, 100 },
	{ _CAMO, 171, FI_CSWAP_GT,  0, 101, 123, 100, 123 },
	{ _CAMO, 173, FI_CSWAP_GT,  0, 100, 123, 100, 100 },
	{ _CAMO, 173, FI_CSWAP_GT,  0,  99, 123, 100, 100 },
	{ _CAMO, 181, FI_MSWAP,     0,
		0xf0f0f0f0f0f0f0f0,
		0xaaaaaaaaaaaaaaaa,
		0x1111111111111111,
		0xa1a1a1a1a1a1a1a1
	},
};

ParameterizedTestParameters(atomic, test_int)
{
	struct test_int_parms *params;
	int tests = ARRAY_SIZE(int_parms);
	int i;

	params = malloc(sizeof(int_parms) * 2);

	memcpy(params, int_parms, sizeof(int_parms));
	memcpy((uint8_t *)params + sizeof(int_parms), int_parms,
	       sizeof(int_parms));

	/* Make duplicate tests that use a standard MR key */
	for (i = 0; i < tests; i++) {
		params[tests + i].key = MR_KEY_STD;
		params[tests + i].index += 1000;
	}

	return cr_make_param_array(struct test_int_parms, params,
				   tests * 2);
}

ParameterizedTest(struct test_int_parms *p, atomic, test_int)
{
	struct mem_region mr;
	enum fi_datatype dt;
	uint64_t *rma;
	uint64_t *loc;
	uint64_t lini = -1;

	rma = _cxit_create_mr(&mr, &p->key);

	loc = calloc(1, RMA_WIN_LEN);
	cr_assert_not_null(loc);

	if (p->opmask & _AMO) {
		for (dt = FI_INT8; dt <= FI_UINT64; dt++) {
			_test_amo(p->index, dt, p->op, p->err, &p->o1,
				  0, 0, 0,
				  rma, &p->rini, &p->rexp,
				  p->key);
		}
	}

	if (p->opmask & _FAMO) {
		for (dt = FI_INT8; dt <= FI_UINT64; dt++) {
			_test_amo(p->index, dt, p->op, p->err, &p->o1,
				  0, loc, &lini, rma, &p->rini, &p->rexp,
				  p->key);
		}
	}

	if (p->opmask & _CAMO) {
		for (dt = FI_INT8; dt <= FI_UINT64; dt++) {
			_test_amo(p->index, dt, p->op, p->err, &p->o1,
				  &p->comp, loc, &lini, rma, &p->rini,
				  &p->rexp,
				  p->key);
		}
	}

	free(loc);
	_cxit_destroy_mr(&mr);
}

/* The FLT tests only test the float type.
 */
struct test_flt_parms {
	int opmask;
	int index;
	enum fi_op op;
	int err;
	float comp;
	float o1;
	float rini;
	float rexp;
	uint64_t key;
};

static struct test_flt_parms flt_parms[] = {
	{ _AMO|_FAMO, 11, FI_MIN,  0, 0.0f, 12.3f, 12.0f, 12.0f },
	{ _AMO|_FAMO, 12, FI_MIN,  0, 0.0f, 12.0f, 12.3f, 12.0f },
	{ _AMO|_FAMO, 21, FI_MAX,  0, 0.0f, 12.3f, 12.0f, 12.3f },
	{ _AMO|_FAMO, 22, FI_MAX,  0, 0.0f, 12.0f, 12.3f, 12.3f },
	{ _AMO|_FAMO, 31, FI_SUM,  0, 0.0f,  1.1f,  1.2f, (1.1f + 1.2f) },
	{ _AMO|_FAMO, 32, FI_SUM,  0, 0.0f,  0.4f,  1.7f, (0.4f + 1.7f) },
	{ _AMO|_FAMO, 41, FI_LOR,  1 },
	{ _AMO|_FAMO, 51, FI_LAND, 1 },
	{ _AMO|_FAMO, 61, FI_LXOR, 1 },
	{ _AMO|_FAMO, 71, FI_BOR,  1 },
	{ _AMO|_FAMO, 81, FI_BAND, 1 },
	{ _AMO|_FAMO, 91, FI_BXOR, 1 },
	{ _AMO|_FAMO, 101, FI_ATOMIC_WRITE, 0, 0.0f, 10.2f, 96.6f, 10.2f },
	{ _FAMO, 111, FI_ATOMIC_READ, 0, 0.0f, 1.1f, 10.2f, 10.2f },
	{ _AMO,  112, FI_ATOMIC_READ, 1 },
	{ _CAMO, 121, FI_CSWAP,     0, 12.0f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 122, FI_CSWAP,     0, 10.0f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 131, FI_CSWAP_NE,  0, 12.0f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 132, FI_CSWAP_NE,  0, 10.0f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 141, FI_CSWAP_LE,  0, 10.1f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 142, FI_CSWAP_LE,  0, 10.0f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 143, FI_CSWAP_LE,  0,  9.9f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 151, FI_CSWAP_LT,  0, 10.1f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 152, FI_CSWAP_LT,  0, 10.0f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 153, FI_CSWAP_LT,  0,  9.9f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 161, FI_CSWAP_GE,  0, 10.1f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 162, FI_CSWAP_GE,  0, 10.0f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 163, FI_CSWAP_GE,  0,  9.9f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 171, FI_CSWAP_GT,  0, 10.1f, 12.3f, 10.0f, 12.3f },
	{ _CAMO, 172, FI_CSWAP_GT,  0, 10.0f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 173, FI_CSWAP_GT,  0,  9.9f, 12.3f, 10.0f, 10.0f },
	{ _CAMO, 181, FI_MSWAP,     1 },
};

ParameterizedTestParameters(atomic, test_flt)
{
	struct test_flt_parms *params;
	int tests = ARRAY_SIZE(flt_parms);
	int i;

	params = malloc(sizeof(flt_parms) * 2);

	memcpy(params, flt_parms, sizeof(flt_parms));
	memcpy((uint8_t *)params + sizeof(flt_parms), flt_parms,
	       sizeof(flt_parms));

	/* Make duplicate tests that use a standard MR key */
	for (i = 0; i < tests; i++) {
		params[tests + i].key = MR_KEY_STD;
		params[tests + i].index += 1000;
	}

	return cr_make_param_array(struct test_flt_parms, params,
				   tests * 2);
}

ParameterizedTest(struct test_flt_parms *p, atomic, test_flt)
{
	struct mem_region mr;
	enum fi_datatype dt = FI_FLOAT;
	uint64_t *rma;
	uint64_t *loc;
	uint64_t lini = -1;

	rma = _cxit_create_mr(&mr, &p->key);

	loc = calloc(1, RMA_WIN_LEN);
	cr_assert_not_null(loc);

	if (p->opmask & _AMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, 0, 0,
			  rma, &p->rini, &p->rexp,
			  p->key);
	}

	if (p->opmask & _FAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, loc, &lini, rma, &p->rini, &p->rexp,
			  p->key);
	}

	if (p->opmask & _CAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  &p->comp, loc, &lini, rma, &p->rini,
			  &p->rexp,
			  p->key);
	}

	free(loc);
	_cxit_destroy_mr(&mr);
}

/* The DBL tests only test the double type.
 */
struct test_dbl_parms {
	int opmask;
	int index;
	enum fi_op op;
	int err;
	double comp;
	double o1;
	double rini;
	double rexp;
	uint64_t key;
};

static struct test_dbl_parms dbl_parms[] = {
	{ _AMO|_FAMO, 11, FI_MIN,  0, 0.0, 12.3, 12.0, 12.0 },
	{ _AMO|_FAMO, 12, FI_MIN,  0, 0.0, 12.0, 12.3, 12.0 },
	{ _AMO|_FAMO, 21, FI_MAX,  0, 0.0, 12.3, 12.0, 12.3 },
	{ _AMO|_FAMO, 22, FI_MAX,  0, 0.0, 12.0, 12.3, 12.3 },
	{ _AMO|_FAMO, 31, FI_SUM,  0, 0.0,  1.1,  1.2, (1.1 + 1.2) },
	{ _AMO|_FAMO, 32, FI_SUM,  0, 0.0,  0.4,  1.7, (0.4 + 1.7) },
	{ _AMO|_FAMO, 41, FI_LOR,  1 },
	{ _AMO|_FAMO, 51, FI_LAND, 1 },
	{ _AMO|_FAMO, 61, FI_LXOR, 1 },
	{ _AMO|_FAMO, 71, FI_BOR,  1 },
	{ _AMO|_FAMO, 81, FI_BAND, 1 },
	{ _AMO|_FAMO, 91, FI_BXOR, 1 },
	{ _AMO|_FAMO, 101, FI_ATOMIC_WRITE, 0, 0.0, 10.2, 123.4, 10.2 },
	{ _FAMO, 111, FI_ATOMIC_READ, 0, 0.0, 1.1, 10.2, 10.2 },
	{ _AMO,  112, FI_ATOMIC_READ, 1 },
	{ _CAMO, 121, FI_CSWAP,     0, 12.0, 12.3, 10.0, 10.0 },
	{ _CAMO, 122, FI_CSWAP,     0, 10.0, 12.3, 10.0, 12.3 },
	{ _CAMO, 131, FI_CSWAP_NE,  0, 12.0, 12.3, 10.0, 12.3 },
	{ _CAMO, 132, FI_CSWAP_NE,  0, 10.0, 12.3, 10.0, 10.0 },
	{ _CAMO, 141, FI_CSWAP_LE,  0, 10.1, 12.3, 10.0, 10.0 },
	{ _CAMO, 142, FI_CSWAP_LE,  0, 10.0, 12.3, 10.0, 12.3 },
	{ _CAMO, 143, FI_CSWAP_LE,  0,  9.9, 12.3, 10.0, 12.3 },
	{ _CAMO, 151, FI_CSWAP_LT,  0, 10.1, 12.3, 10.0, 10.0 },
	{ _CAMO, 152, FI_CSWAP_LT,  0, 10.0, 12.3, 10.0, 10.0 },
	{ _CAMO, 153, FI_CSWAP_LT,  0,  9.9, 12.3, 10.0, 12.3 },
	{ _CAMO, 161, FI_CSWAP_GE,  0, 10.1, 12.3, 10.0, 12.3 },
	{ _CAMO, 162, FI_CSWAP_GE,  0, 10.0, 12.3, 10.0, 12.3 },
	{ _CAMO, 163, FI_CSWAP_GE,  0,  9.9, 12.3, 10.0, 10.0 },
	{ _CAMO, 171, FI_CSWAP_GT,  0, 10.1, 12.3, 10.0, 12.3 },
	{ _CAMO, 172, FI_CSWAP_GT,  0, 10.0, 12.3, 10.0, 10.0 },
	{ _CAMO, 173, FI_CSWAP_GT,  0,  9.9, 12.3, 10.0, 10.0 },
	{ _CAMO, 181, FI_MSWAP,     1 },
};

ParameterizedTestParameters(atomic, test_dbl)
{
	struct test_dbl_parms *params;
	int tests = ARRAY_SIZE(dbl_parms);
	int i;

	params = malloc(sizeof(dbl_parms) * 2);

	memcpy(params, dbl_parms, sizeof(dbl_parms));
	memcpy((uint8_t *)params + sizeof(dbl_parms), dbl_parms,
	       sizeof(dbl_parms));

	/* Make duplicate tests that use a standard MR key */
	for (i = 0; i < tests; i++) {
		params[tests + i].key = MR_KEY_STD;
		params[tests + i].index += 1000;
	}

	return cr_make_param_array(struct test_dbl_parms, params,
				   tests * 2);
}

ParameterizedTest(struct test_dbl_parms *p, atomic, test_dbl)
{
	struct mem_region mr;
	enum fi_datatype dt = FI_DOUBLE;
	uint64_t *rma;
	uint64_t *loc;
	uint64_t lini = -1;

	rma = _cxit_create_mr(&mr, &p->key);

	loc = calloc(1, RMA_WIN_LEN);
	cr_assert_not_null(loc);

	if (p->opmask & _AMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, 0, 0,
			  rma, &p->rini, &p->rexp,
			  p->key);
	}

	if (p->opmask & _FAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, loc, &lini, rma, &p->rini, &p->rexp,
			  p->key);
	}

	if (p->opmask & _CAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  &p->comp, loc, &lini, rma, &p->rini,
			  &p->rexp,
			  p->key);
	}

	free(loc);
	_cxit_destroy_mr(&mr);
}

/* The CMPLX tests only test the float complex type.
 */
struct test_cplx_parms {
	int opmask;
	int index;
	enum fi_op op;
	int err;

	float complex comp;
	float complex o1;
	float complex rini;
	float complex rexp;
	uint64_t key;
};

static struct test_cplx_parms cplx_parms[] = {
	{ _AMO|_FAMO, 11, FI_MIN,  1 },
	{ _AMO|_FAMO, 21, FI_MAX,  1 },
	{ _AMO|_FAMO, 31, FI_SUM,  0, 0.0,  1.1,  1.2, (1.1 + 1.2) },
	{ _AMO|_FAMO, 32, FI_SUM,  0, 0.0,  0.4,  1.7, (0.4 + 1.7) },
	{ _AMO|_FAMO, 31, FI_SUM,  0,
		0.0f, 1.1f+I*0.4f, 1.2f+I*1.7f, (1.1f+I*0.4f + 1.2f+I*1.7f) },
	{ _AMO|_FAMO, 32, FI_SUM,  0,
		0.0f, 1.1f+I*1.7f, 1.2f+I*0.4f, (1.1f+I*1.7f + 1.2f+I*0.4f) },
	{ _AMO|_FAMO, 41, FI_LOR,  1 },
	{ _AMO|_FAMO, 51, FI_LAND, 1 },
	{ _AMO|_FAMO, 61, FI_LXOR, 1 },
	{ _AMO|_FAMO, 71, FI_BOR,  1 },
	{ _AMO|_FAMO, 81, FI_BAND, 1 },
	{ _AMO|_FAMO, 91, FI_BXOR, 1 },
	{ _AMO|_FAMO, 101, FI_ATOMIC_WRITE, 0,
		0.0f, 10.2f+I*1.1f, 0.3f+I*2.2f, 10.2f+I*1.1f },
	{ _FAMO, 111, FI_ATOMIC_READ, 0,
		0.0f, 1.1f+I*1.1f, 10.2f+I*1.1f, 10.2f+I*1.1f },
	{ _AMO,  112, FI_ATOMIC_READ, 1 },
	{ _CAMO, 121, FI_CSWAP,     0,
		12.0f+I*1.1f, 12.3f+I*1.1f, 10.0f+I*1.1f, 10.0f+I*1.1f },
	{ _CAMO, 122, FI_CSWAP,     0,
		10.0f+I*1.1f, 12.3f+I*1.1f, 10.0f+I*1.1f, 12.3f+I*1.1f },
	{ _CAMO, 131, FI_CSWAP_NE,  0,
		12.0f+I*1.1f, 12.3f+I*1.1f, 10.0f+I*1.1f, 12.3f+I*1.1f },
	{ _CAMO, 132, FI_CSWAP_NE,  0,
		10.0f+I*1.1f, 12.3f+I*1.1f, 10.0f+I*1.1f, 10.0f+I*1.1f },
	{ _CAMO, 141, FI_CSWAP_LE,  1 },
	{ _CAMO, 151, FI_CSWAP_LT,  1 },
	{ _CAMO, 161, FI_CSWAP_GE,  1 },
	{ _CAMO, 171, FI_CSWAP_GT,  1 },
	{ _CAMO, 181, FI_MSWAP,     1 },
};

ParameterizedTestParameters(atomic, test_cplx)
{
	struct test_cplx_parms *params;
	int tests = ARRAY_SIZE(cplx_parms);
	int i;

	params = malloc(sizeof(cplx_parms) * 2);

	memcpy(params, cplx_parms, sizeof(cplx_parms));
	memcpy((uint8_t *)params + sizeof(cplx_parms), cplx_parms,
	       sizeof(cplx_parms));

	/* Make duplicate tests that use a standard MR key */
	for (i = 0; i < tests; i++) {
		params[tests + i].key = MR_KEY_STD;
		params[tests + i].index += 1000;
	}

	return cr_make_param_array(struct test_cplx_parms, params,
				   tests * 2);
}

ParameterizedTest(struct test_cplx_parms *p, atomic, test_cplx)
{
	struct mem_region mr;
	enum fi_datatype dt = FI_FLOAT_COMPLEX;
	uint64_t *rma;
	uint64_t *loc;
	uint64_t lini = -1;
	uint64_t key = 0;

	rma = _cxit_create_mr(&mr, &key);

	loc = calloc(1, RMA_WIN_LEN);
	cr_assert_not_null(loc);

	if (p->opmask & _AMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, 0, 0,
			  rma, &p->rini, &p->rexp,
			  key);
	}

	if (p->opmask & _FAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, loc, &lini, rma, &p->rini, &p->rexp,
			  key);
	}

	if (p->opmask & _CAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  &p->comp, loc, &lini, rma, &p->rini,
			  &p->rexp,
			  key);
	}

	free(loc);
	_cxit_destroy_mr(&mr);
}

/* The DCMPLX tests only test the double complex type.
 */

struct test_dcplx_parms {
	int opmask;
	int index;
	enum fi_op op;
	int err;

	double complex comp;
	double complex o1;
	double complex rini;
	double complex rexp;
	uint64_t key;
};

static struct test_dcplx_parms dcplx_parms[] = {
	{ _AMO|_FAMO, 11, FI_MIN,  1 },
	{ _AMO|_FAMO, 21, FI_MAX,  1 },
	{ _AMO|_FAMO, 31, FI_SUM,  0,
		0.0, 1.1+I*0.4, 1.2+I*1.7, (1.1+I*0.4 + 1.2+I*1.7) },
	{ _AMO|_FAMO, 32, FI_SUM,  0,
		0.0, 1.1+I*1.7, 1.2+I*0.4, (1.1+I*1.7 + 1.2+I*0.4) },
	{ _AMO|_FAMO, 41, FI_LOR,  1 },
	{ _AMO|_FAMO, 51, FI_LAND, 1 },
	{ _AMO|_FAMO, 61, FI_LXOR, 1 },
	{ _AMO|_FAMO, 71, FI_BOR,  1 },
	{ _AMO|_FAMO, 81, FI_BAND, 1 },
	{ _AMO|_FAMO, 91, FI_BXOR, 1 },
	{ _AMO|_FAMO, 101, FI_ATOMIC_WRITE, 0,
		0.0, 10.2+I*1.1, 0.3+I*2.2, 10.2+I*1.1 },
	{ _FAMO, 111, FI_ATOMIC_READ, 0,
		0.0, 1.1+I*1.1, 10.2+I*1.1, 10.2+I*1.1 },
	{ _AMO,  112, FI_ATOMIC_READ, 1 },
	{ _CAMO, 121, FI_CSWAP,     0,
		12.0+I*1.1, 12.3+I*1.1, 10.0+I*1.1, 10.0+I*1.1 },
	{ _CAMO, 122, FI_CSWAP,     0,
		10.0+I*1.1, 12.3+I*1.1, 10.0+I*1.1, 12.3+I*1.1 },
	{ _CAMO, 131, FI_CSWAP_NE,  0,
		12.0+I*1.1, 12.3+I*1.1, 10.0+I*1.1, 12.3+I*1.1 },
	{ _CAMO, 132, FI_CSWAP_NE,  0,
		10.0+I*1.1, 12.3+I*1.1, 10.0+I*1.1, 10.0+I*1.1 },
	{ _CAMO, 141, FI_CSWAP_LE,  1 },
	{ _CAMO, 151, FI_CSWAP_LT,  1 },
	{ _CAMO, 161, FI_CSWAP_GE,  1 },
	{ _CAMO, 171, FI_CSWAP_GT,  1 },
	{ _CAMO, 181, FI_MSWAP,     1 },
};

ParameterizedTestParameters(atomic, test_dcplx)
{
	struct test_dcplx_parms *params;
	int tests = ARRAY_SIZE(dcplx_parms);
	int i;

	params = malloc(sizeof(dcplx_parms) * 2);

	memcpy(params, dcplx_parms, sizeof(dcplx_parms));
	memcpy((uint8_t *)params + sizeof(dcplx_parms), dcplx_parms,
	       sizeof(dcplx_parms));

	/* Make duplicate tests that use a standard MR key */
	for (i = 0; i < tests; i++) {
		params[tests + i].key = MR_KEY_STD;
		params[tests + i].index += 1000;
	}

	return cr_make_param_array(struct test_dcplx_parms, params,
				   tests * 2);
}

ParameterizedTest(struct test_dcplx_parms *p, atomic, test_dcplx)
{
	struct mem_region mr;
	enum fi_datatype dt = FI_DOUBLE_COMPLEX;
	uint64_t *rma;
	uint64_t *loc;
	uint64_t lini = -1;

	rma = _cxit_create_mr(&mr, &p->key);

	loc = calloc(1, RMA_WIN_LEN);
	cr_assert_not_null(loc);

	if (p->opmask & _AMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, 0, 0,
			  rma, &p->rini, &p->rexp,
			  p->key);
	}

	if (p->opmask & _FAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  0, loc, &lini, rma, &p->rini, &p->rexp,
			  p->key);
	}

	if (p->opmask & _CAMO) {
		_test_amo(p->index, dt, p->op, p->err, &p->o1,
			  &p->comp, loc, &lini, rma, &p->rini,
			  &p->rexp,
			  p->key);
	}

	free(loc);
	_cxit_destroy_mr(&mr);
}

Test(atomic, amo_cleanup)
{
	int ret;
	long i;
	uint8_t *send_buf;
	int win_len = 0x1000;
	int writes = 50;
	struct mem_region mr;
	uint64_t operand1 = 0;
	uint64_t key = RMA_WIN_KEY;

	send_buf = calloc(1, win_len);
	cr_assert_not_null(send_buf, "send_buf alloc failed");

	for (i = 0; i < win_len; i++)
		send_buf[i] = 0xb1 * i;

	_cxit_create_mr(&mr, &key);

	/* Send 8 bytes from send buffer data to RMA window 0 */
	for (i = 0; i < writes; i++) {
		do {
			ret = fi_atomic(cxit_ep, &operand1, 1, 0,
					cxit_ep_fi_addr, 0, key,
					FI_UINT64, FI_SUM, NULL);
			if (ret == -FI_EAGAIN)
				fi_cq_read(cxit_tx_cq, NULL, 0);
		} while (ret == -FI_EAGAIN);
		cr_assert(ret == FI_SUCCESS);
	}

	_cxit_destroy_mr(&mr);

	/* Exit without gathering events. */
}

/* Perform a batch of AMOs. A C_STATE update is required for each transaction
 * since each transaction in the batch uses a unique internal request.
 */
Test(atomic, amo_batch)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	int ret;
	int i;
	uint64_t key = RMA_WIN_KEY;

	_cxit_create_mr(&mr, &key);

	cr_assert(!fi_cntr_read(cxit_write_cntr));

	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

	while (fi_cntr_read(cxit_write_cntr) != 4)
		;

	for (i = 0; i < 4; i++) {
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
		validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
	}

	_cxit_destroy_mr(&mr);
}

void cxit_setup_amo_selective_completion(void)
{
	cxit_tx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;

	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->op_flags = FI_COMPLETION;
	cxit_setup_rma();
}

/* Test selective completion behavior with AMOs. */
Test(atomic_sel, selective_completion,
     .init = cxit_setup_amo_selective_completion,
     .fini = cxit_teardown_rma)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t compare;
	uint64_t result;
	uint64_t exp_remote = 0;
	uint64_t *rma;
	int ret;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_ioc compare_ioc;
	struct fi_ioc result_ioc;
	struct fi_rma_ioc rma_ioc;
	int count = 0;
	uint64_t key = RMA_WIN_KEY;

	rma = _cxit_create_mr(&mr, &key);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	result_ioc.addr = &result;
	result_ioc.count = 1;

	compare_ioc.addr = &compare;
	compare_ioc.count = 1;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	/* Non-fetching AMOs */

	/* Completion requested by default. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Completion explicitly requested. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, FI_COMPLETION);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Suppress completion. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Inject never generates an event */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_inject_atomic(cxit_ep, &operand1, 1,
			       cxit_ep_fi_addr, 0, key,
			       FI_UINT64, FI_SUM);
	cr_assert(ret == FI_SUCCESS);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Fetching AMOs */
	count = 0;

	/* Completion requested by default. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0,
			      &result, NULL,
			      cxit_ep_fi_addr, 0, key,
			      FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Completion explicitly requested. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_COMPLETION);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Suppress completion. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	/* Completion explicitly requested with inject. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_COMPLETION | FI_INJECT);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Suppress completion with inject. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_INJECT);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_read_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Comp AMOs */

	/* Completion requested by default. */
	ret = fi_compare_atomic(cxit_ep, &operand1, 1, 0,
				&compare, NULL,
				&result, NULL,
				cxit_ep_fi_addr, 0, key,
				FI_UINT64, FI_CSWAP, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Completion explicitly requested. */
	msg.op = FI_CSWAP;
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1, FI_COMPLETION);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Suppress completion. */
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_read_cntr) != count)
		;

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	_cxit_destroy_mr(&mr);
}

void cxit_setup_amo_selective_completion_suppress(void)
{
	cxit_tx_cq_bind_flags |= FI_SELECTIVE_COMPLETION;

	cxit_setup_getinfo();
	cxit_fi_hints->tx_attr->op_flags = 0;
	cxit_setup_rma();
}

/* Test selective completion behavior with RMA. */
Test(atomic_sel, selective_completion_suppress,
     .init = cxit_setup_amo_selective_completion_suppress,
     .fini = cxit_teardown_rma)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t compare;
	uint64_t result;
	uint64_t exp_remote = 0;
	uint64_t *rma;
	int ret;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_ioc compare_ioc;
	struct fi_ioc result_ioc;
	struct fi_rma_ioc rma_ioc;
	int count = 0;
	uint64_t key = RMA_WIN_KEY;

	rma = _cxit_create_mr(&mr, &key);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	result_ioc.addr = &result;
	result_ioc.count = 1;

	compare_ioc.addr = &compare;
	compare_ioc.count = 1;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	/* Non-fetching AMOs */

	/* Completion suppressed by default. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Completion explicitly requested. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, FI_COMPLETION);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	count++;

	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Suppress completion. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Inject never generates an event */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_inject_atomic(cxit_ep, &operand1, 1,
			       cxit_ep_fi_addr, 0, key,
			       FI_UINT64, FI_SUM);
	cr_assert(ret == FI_SUCCESS);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Fetching AMOs */
	count = 0;

	/* Completion suppressed by default. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomic(cxit_ep, &operand1, 1, 0,
			      &result, NULL,
			      cxit_ep_fi_addr, 0, key,
			      FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_read_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Completion explicitly requested. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_COMPLETION);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Suppress completion. */
	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_read_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Comp AMOs */

	/* Completion suppressed by default. */
	ret = fi_compare_atomic(cxit_ep, &operand1, 1, 0,
				&compare, NULL,
				&result, NULL,
				cxit_ep_fi_addr, 0, key,
				FI_UINT64, FI_CSWAP, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	/* Completion explicitly requested. */
	msg.op = FI_CSWAP;
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1, FI_COMPLETION);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Suppress completion. */
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	/* Completion explicitly requested with inject. */
	msg.op = FI_CSWAP;
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1,
				   FI_COMPLETION | FI_INJECT);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Suppress completion with inject. */
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1, FI_INJECT);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	while (fi_cntr_read(cxit_read_cntr) != count)
		;

	/* Make sure an event wasn't delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	_cxit_destroy_mr(&mr);
}

/* Test remote counter events with AMOs */
Test(atomic, rem_cntr)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote = 0;
	uint64_t *rma;
	int ret;
	int count = 0;
	uint64_t key = RMA_WIN_KEY;

	rma = _cxit_create_mr(&mr, &key);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	/* Wait for remote counter event, then check data */
	count++;

	while (fi_cntr_read(cxit_rem_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	operand1 = 3;
	exp_remote += operand1;
	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

	/* Wait for remote counter event, then check data */
	count++;

	while (fi_cntr_read(cxit_rem_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	operand1 = 9;
	exp_remote += operand1;
	ret = fi_atomic(cxit_ep, &operand1, 1, 0,
			cxit_ep_fi_addr, 0, key,
			FI_UINT64, FI_SUM, NULL);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

	/* Wait for remote counter event, then check data */
	count++;

	while (fi_cntr_read(cxit_rem_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	_cxit_destroy_mr(&mr);
}

/* Test simple operations: AMO SUM UINT64_T, FAMO SUM UINT64_T, and CAMO SWAP_NE
 * UINT64_T. If this doesn't work, nothing else will.
 */
TestSuite(atomic_flush, .init = cxit_setup_rma_disable_fi_rma_event,
	  .fini = cxit_teardown_rma, .disabled = AMO_DISABLED,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Perform a fetching AMO with flush at target. */
Test(atomic_flush, fetch_flush)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t fetch_remote = 4;
	uint64_t exp_remote = fetch_remote;
	uint64_t *rma;
	int ret;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;
	uint64_t result = 0;
	struct fi_ioc result_ioc = { .count = 1, .addr = &result };
	int count = 0;
	uint64_t flushes_start;
	uint64_t flushes_end;
	uint64_t key = RMA_WIN_KEY;
	bool enable = false;

	/* If FI_MR_PROV_KEY disable the remote provider key cache */
	fi_control(&cxit_domain->fid, FI_OPT_CXI_SET_PROV_KEY_CACHE,
		   &enable);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	rma = _cxit_create_mr(&mr, &key);
	*rma = fetch_remote;
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	operand1 = 1;
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_DELIVERY_COMPLETE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);
	cr_assert_eq(result, fetch_remote,
		     "Result = %ld, expected = %ld",
		     result, fetch_remote);

	_cxit_destroy_mr(&mr);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);
	cr_assert(flushes_end > flushes_start);
}

/* Perform a fetching AMO with flush at target, but use an illegal
 * RMA offset. Verify that an error is returned in the CQE even though
 * the subsequent flush succeeds.
 */
Test(atomic_flush, fetch_flush_bounds_err)
{
	struct mem_region mr;
	struct fi_cq_err_entry err;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1 = 1;
	uint64_t result = 0;
	uint64_t *rma;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;
	struct fi_ioc result_ioc = { .count = 1, .addr = &result };
	uint64_t key = RMA_WIN_KEY;
	int ret;
	bool enable = false;

	/* If FI_MR_PROV_KEY disable the remote provider key cache */
	fi_control(&cxit_domain->fid, FI_OPT_CXI_SET_PROV_KEY_CACHE,
		   &enable);

	rma = _cxit_create_mr(&mr, &key);
	cr_assert_not_null(rma);

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = RMA_WIN_LEN + 1;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_DELIVERY_COMPLETE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, -FI_EAVAIL, "Unexpected atomic flush success");

	ret = fi_cq_readerr(cxit_tx_cq, &err, 1);
	cr_assert_eq(ret, 1, "fi_cq_readerr error %d", ret);
	cr_assert_eq(err.err, FI_EIO, "Unexpected error value: %d", err.err);

	_cxit_destroy_mr(&mr);
}

/* Perform an AMO that uses a flushing ZBR at the target. */
Test(atomic, flush)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote = 0;
	uint64_t *rma;
	int ret;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;
	int count = 0;
	uint64_t flushes_start;
	uint64_t flushes_end;
	uint64_t key = RMA_WIN_KEY;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);


	rma = _cxit_create_mr(&mr, &key);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	operand1 = 1;
	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, FI_DELIVERY_COMPLETE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
	count++;

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	_cxit_destroy_mr(&mr);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_FLUSH_REQS,
				 &flushes_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);
	cr_assert(flushes_end > flushes_start);
}

/* Test AMO FI_MORE */
Test(atomic, more)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote;
	uint64_t *rma;
	int ret;
	int i = 0;
	uint64_t key = 0xa;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;


	rma = _cxit_create_mr(&mr, &key);
	exp_remote = 0;
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;
	exp_remote += operand1;

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	ret = fi_atomicmsg(cxit_ep, &msg, FI_MORE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	/* Ensure no completion before the doorbell ring */
	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
		cr_assert_eq(ret, -FI_EAGAIN,
			     "write failed %d", ret);
	} while (i++ < 100000);

	operand1 = 3;
	exp_remote += operand1;

	ret = fi_atomicmsg(cxit_ep, &msg, 0);
	cr_assert(ret == FI_SUCCESS, "Return code = %d", ret);

	/* Wait for two events. */
	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* Validate sent data */
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	_cxit_destroy_mr(&mr);
}

/* Test AMO FI_FENCE */
Test(atomic, fence)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote;
	uint64_t *rma;
	int ret;
	uint64_t key = 0xa;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;

	rma = _cxit_create_mr(&mr, &key);
	exp_remote = 0;
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;
	exp_remote += operand1;

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	ret = fi_atomicmsg(cxit_ep, &msg, FI_FENCE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* Validate sent data */
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	_cxit_destroy_mr(&mr);
}

void cxit_amo_setup_nofence(void)
{
	cxit_setup_getinfo();
	cxit_fi_hints->caps = CXIP_EP_PRI_CAPS;
	cxit_setup_rma();
}

/* Test AMO without FI_FENCE */
Test(atomic_nofence, nofence,
     .init = cxit_amo_setup_nofence,
     .fini = cxit_teardown_rma)
{
	struct mem_region mr;
	uint64_t operand1;
	uint64_t exp_remote;
	uint64_t *rma;
	int ret;
	uint64_t key = 0xa;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;

	rma = _cxit_create_mr(&mr, &key);
	exp_remote = 0;
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;
	exp_remote += operand1;

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	ret = fi_atomicmsg(cxit_ep, &msg, FI_FENCE);
	cr_assert(ret == -FI_EINVAL);

	_cxit_destroy_mr(&mr);
}

void cxit_setup_amo_opt(void)
{
	cxit_setup_getinfo();

	/* Explicitly request unordered RMA */
	cxit_fi_hints->caps = FI_ATOMIC;
	cxit_fi_hints->tx_attr->msg_order = 0;

	cxit_setup_rma();
}

TestSuite(amo_opt, .init = cxit_setup_amo_opt, .fini = cxit_teardown_rma,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test Unreliable/HRP AMOs */
Test(amo_opt, hrp)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote;
	uint64_t *rma;
	int ret;
	uint64_t key = 0xa;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;
	uint64_t res_start;
	uint64_t res_end;
	uint64_t hrp_acks_start;
	uint64_t hrp_acks_end;
	struct cxip_ep *cxi_ep;
	uint64_t compare;
	uint64_t result;
	struct fi_ioc compare_ioc = { .count = 1, .addr = &compare };
	struct fi_ioc result_ioc = { .count = 1, .addr = &result };

	/* HRP not supported in netsim */
	cxi_ep = container_of(cxit_ep, struct cxip_ep, ep);
	if (is_netsim(cxi_ep->ep_obj))
		return;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &res_start, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	ret = cxit_dom_read_cntr(C_CNTR_HNI_HRP_ACK,
				 &hrp_acks_start, NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	rma = _cxit_create_mr(&mr, &key);
	exp_remote = 0;
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	operand1 = 1;

	exp_remote += operand1;

	ioc.addr = &operand1;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	ret = fi_atomicmsg(cxit_ep, &msg, FI_CXI_UNRELIABLE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* Validate sent data */
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* HRP requires UNRELIABLE */
	ret = fi_atomicmsg(cxit_ep, &msg, FI_CXI_HRP);
	cr_assert(ret == -FI_EINVAL, "Return code  = %d", ret);

	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	exp_remote += operand1;
	ret = fi_atomicmsg(cxit_ep, &msg, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* HRP FAMO is invalid */
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == -FI_EBADFLAGS, "Return code  = %d", ret);

	/* Try unreliable FAMO */
	exp_remote += operand1;
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_CXI_UNRELIABLE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	/* wait a second to check the operation was performed. The HRP response
	 * returns before the request hits the NIC.
	 */
	usleep(1000);

	/* Validate sent data */
	cr_assert_eq(*rma, exp_remote,
		     "Result = %ld, expected = %ld",
		     *rma, exp_remote);

	/* HRP compare AMO is invalid */
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1,
				   FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == -FI_EBADFLAGS, "Return code  = %d", ret);

	/* Try unreliable compare AMO. */
	msg.op = FI_CSWAP;
	compare = exp_remote;
	operand1 = exp_remote + 1;
	ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc, NULL, 1,
				   &result_ioc, NULL, 1, FI_CXI_UNRELIABLE);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	sleep(1);

	/* Validate data */
	cr_assert_eq(*rma, operand1,
		     "Result = %ld, expected = %ld",
		     *rma, operand1);
	cr_assert_eq(result, exp_remote,
		     "Result = %ld, expected = %ld",
		     result, exp_remote);

	ret = cxit_dom_read_cntr(C_CNTR_IXE_RX_PTL_RESTRICTED_PKT,
				 &res_end, NULL, true);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	ret = cxit_dom_read_cntr(C_CNTR_HNI_HRP_ACK,
				 &hrp_acks_end, NULL, false);
	cr_assert_eq(ret, FI_SUCCESS, "cntr_read failed: %d\n", ret);

	cr_assert_eq(hrp_acks_end - hrp_acks_start, 1,
		     "unexpected hrp_acks count: %lu\n",
		     hrp_acks_end - hrp_acks_start);
	cr_assert_eq(res_end - res_start, 4,
		     "unexpected restricted packets count: %lu\n",
		     res_end - res_start);

	/* HRP does not support Fetching AMOS. */
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				 FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == -FI_EBADFLAGS, "Return code  = %d", ret);

	ret = fi_compare_atomicmsg(cxit_ep, &msg, &result_ioc, NULL, 1,
				   &result_ioc, NULL, 1,
				   FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == -FI_EBADFLAGS, "Return code  = %d", ret);

	_cxit_destroy_mr(&mr);
}

Test(atomic, std_mr_inject)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	uint64_t operand1;
	uint64_t exp_remote = 0;
	uint64_t *rma;
	int ret;
	int count = 0;
	int i;
	uint64_t win_key = CXIP_PTL_IDX_MR_OPT_CNT;

	rma = _cxit_create_mr(&mr, &win_key);
	cr_assert_eq(*rma, exp_remote,
			"Result = %ld, expected = %ld",
			*rma, exp_remote);

	operand1 = 1;

	for (i = 0; i < 10; i++) {
		exp_remote += operand1;
		ret = fi_inject_atomic(cxit_ep, &operand1, 1,
				cxit_ep_fi_addr, 0, win_key,
				FI_UINT64, FI_SUM);
		cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);
		count++;
	}

	/* Corrupt the user operand buffer to make sure the NIC is not using it
	 * for an inject.
	 */
	operand1 = 0;

	while (fi_cntr_read(cxit_write_cntr) != count)
		;

	cr_assert_eq(*rma, exp_remote,
			"Result = %ld, expected = %ld",
			*rma, exp_remote);

	/* Make sure no events were delivered */
	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	_cxit_destroy_mr(&mr);
}

/* Test ERRATA-2794 32bit non-fetch AMO with HRP work-around */
Test(amo_opt, errata_2794)
{
	struct mem_region mr;
	struct fi_cq_tagged_entry cqe;
	union {
		uint32_t	_32bit;
		uint64_t	_64bit;
	} operand, exp_remote, *rma;
	int ret;
	uint64_t key = 0xa;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc;
	struct fi_rma_ioc rma_ioc;
	struct cxip_ep *cxi_ep;

	/* HRP not supported in netsim */
	cxi_ep = container_of(cxit_ep, struct cxip_ep, ep);
	if (is_netsim(cxi_ep->ep_obj))
		return;

	rma = _cxit_create_mr(&mr, &key);

	ioc.addr = &operand;
	ioc.count = 1;

	rma_ioc.addr = 0;
	rma_ioc.count = 1;
	rma_ioc.key = key;

	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.datatype = FI_UINT64;
	msg.op = FI_SUM;

	/* Use 64-bit to make sure we are using a HRP communication profile */
	exp_remote._64bit = 0;
	cr_assert_eq(rma->_64bit, exp_remote._64bit,
		     "Result = %" PRId64 ", expected = %" PRId64,
		     rma->_64bit, exp_remote._64bit);

	operand._64bit = 1UL;
	exp_remote._64bit += operand._64bit;

	ret = fi_atomicmsg(cxit_ep, &msg, FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* wait a second to check the operation was performed. The HRP response
	 * returns before the request hits the NIC. Validate data and that
	 * CQ configured for HRP.
	 */
	usleep(1000);
	cr_assert_eq(rma->_64bit, exp_remote._64bit,
		     "Result = %" PRId64 ", expected = %" PRId64,
		     rma->_64bit, exp_remote._64bit);

	/* ERRATA-2794 */
	rma->_32bit = 0;
	exp_remote._32bit = 0;
	msg.datatype = FI_UINT32;

	operand._32bit = 1;
	exp_remote._32bit += operand._32bit;

	ret = fi_atomicmsg(cxit_ep, &msg, FI_CXI_UNRELIABLE | FI_CXI_HRP);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* wait a second to check the operation was performed, and validate
	 * data.
	 */
	usleep(1000);
	cr_assert_eq(rma->_32bit, exp_remote._32bit,
		     "Result = %d, expected = %d",
		     rma->_32bit, exp_remote._32bit);

	/* Perform successive 32-bit unsigned non-fetching atomic, no
	 * communication profile change would be required.
	 */
	exp_remote._32bit += operand._32bit;
	ret = fi_atomicmsg(cxit_ep, &msg, 0);
	cr_assert(ret == FI_SUCCESS, "Return code  = %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);
	validate_tx_event(&cqe, FI_ATOMIC | FI_WRITE, NULL);

	/* wait a second to check the operation was performed, and
	 * validate data.
	 */
	usleep(1000);
	cr_assert_eq(rma->_32bit, exp_remote._32bit,
		     "Result = %d, expected = %d",
		     rma->_32bit, exp_remote._32bit);

	_cxit_destroy_mr(&mr);
}

static void amo_hybrid_mr_desc_test_runner(bool fetching, bool compare,
					   bool cq_events, bool buf_mr,
					   bool compare_mr, bool result_mr,
					   bool mswap, bool read, bool flush)
{
	struct mem_region buf_window;
	struct mem_region compare_window;
	struct mem_region result_window;
	struct mem_region remote_window;
	uint64_t remote_key = 0x1;
	uint64_t buf_key = 0x2;
	uint64_t compare_key = 0x3;
	uint64_t result_key = 0x4;
	int win_len = 1;
	void *buf_desc[1] = {};
	void *compare_desc[1] = {};
	void *result_desc[1] = {};
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc = {};
	struct fi_rma_ioc rma_ioc = {};
	struct fi_ioc fetch_ioc = {};
	struct fi_ioc compare_ioc = {};
	int ret;
	uint64_t cqe_flags = fetching ? FI_ATOMIC | FI_READ :
		FI_ATOMIC | FI_WRITE;
	struct fid_cntr *cntr = fetching ? cxit_read_cntr : cxit_write_cntr;
	struct fi_cq_tagged_entry cqe;
	uint64_t amo_flags = cq_events ? FI_COMPLETION : 0;

	if (flush)
		amo_flags |= FI_DELIVERY_COMPLETE;
	else
		amo_flags |= FI_TRANSMIT_COMPLETE;

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &buf_key,
			&buf_window);
	cr_assert(ret == FI_SUCCESS);

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &compare_key,
			&compare_window);
	cr_assert(ret == FI_SUCCESS);

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &result_key,
			&result_window);
	cr_assert(ret == FI_SUCCESS);

	ret = mr_create(win_len, FI_REMOTE_READ | FI_REMOTE_WRITE, 0x3,
			&remote_key, &remote_window);
	cr_assert(ret == FI_SUCCESS);

	if (buf_mr)
		buf_desc[0] = fi_mr_desc(buf_window.mr);

	if (compare_mr)
		compare_desc[0] = fi_mr_desc(compare_window.mr);

	if (result_mr)
		result_desc[0] = fi_mr_desc(result_window.mr);

	ioc.addr = buf_window.mem;
	ioc.count = 1;

	rma_ioc.count = 1;
	rma_ioc.key = remote_key;

	msg.msg_iov = &ioc;
	msg.desc = buf_desc;
	msg.iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;

	if (!compare) {
		msg.datatype = FI_UINT8;

		if (fetching && read)
			msg.op = FI_ATOMIC_READ;
		else
			msg.op = FI_SUM;

		*buf_window.mem = 1;
		*result_window.mem = 0;
		*remote_window.mem = 1;

		if (fetching) {
			fetch_ioc.addr = result_window.mem;
			fetch_ioc.count = 1;

			ret = fi_fetch_atomicmsg(cxit_ep, &msg, &fetch_ioc,
						 result_desc, 1, amo_flags);
			cr_assert(ret == FI_SUCCESS);
		} else {
			ret = fi_atomicmsg(cxit_ep, &msg, amo_flags);
			cr_assert(ret == FI_SUCCESS);
		}

		while (1) {
			ret = fi_cntr_wait(cntr, 1, 1000);
			if (ret == FI_SUCCESS)
				break;
		}

		if (!read)
			cr_assert_eq(*remote_window.mem, 2,
				     "Data mismatch: expected=2 got=%d\n",
				     *remote_window.mem);

		if (fetching)
			cr_assert_eq(*result_window.mem, 1,
				     "Data mismatch: expected=1 got=%d\n",
				     *result_window.mem);
	} else if (mswap) {
		msg.datatype = FI_UINT8;
		msg.op = FI_MSWAP;

		compare_ioc.addr = compare_window.mem;
		compare_ioc.count = 1;

		fetch_ioc.addr = result_window.mem;
		fetch_ioc.count = 1;

		*buf_window.mem = 0xA0;
		*compare_window.mem = 0xB;
		*result_window.mem = 1;
		*remote_window.mem = 0xF;

		ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc,
					   compare_desc, 1, &fetch_ioc,
					   result_desc, 1, amo_flags);
		cr_assert_eq(ret, FI_SUCCESS, "Bad rc=%d\n", ret);

		while (1) {
			ret = fi_cntr_wait(cntr, 1, 1000);
			if (ret == FI_SUCCESS)
				break;
		}

		cr_assert_eq(*remote_window.mem, 4,
			     "Data mismatch: expected=4 got=%d\n",
			     *remote_window.mem);

		cr_assert_eq(*result_window.mem, 0xF,
			     "Data mismatch: expected=0xF got=%d\n",
			     *result_window.mem);
	} else {
		msg.datatype = FI_UINT8;
		msg.op = FI_CSWAP;

		compare_ioc.addr = compare_window.mem;
		compare_ioc.count = 1;

		fetch_ioc.addr = result_window.mem;
		fetch_ioc.count = 1;

		*buf_window.mem = 3;
		*compare_window.mem = 1;
		*result_window.mem = 0;
		*remote_window.mem = 1;

		ret = fi_compare_atomicmsg(cxit_ep, &msg, &compare_ioc,
					   compare_desc, 1, &fetch_ioc,
					   result_desc, 1, amo_flags);
		cr_assert_eq(ret, FI_SUCCESS, "Bad rc=%d\n", ret);

		while (1) {
			ret = fi_cntr_wait(cntr, 1, 1000);
			if (ret == FI_SUCCESS)
				break;
		}

		cr_assert_eq(*remote_window.mem, 3,
			     "Data mismatch: expected=3 got=%d\n",
			     *remote_window.mem);

		cr_assert_eq(*result_window.mem, 1,
			     "Data mismatch: expected=1 got=%d\n",
			     *result_window.mem);
	}

	if (cq_events) {
		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, cqe_flags, NULL);
	}

	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&remote_window);
	mr_destroy(&result_window);
	mr_destroy(&compare_window);
	mr_destroy(&buf_window);
}

TestSuite(amo_hybrid_mr_desc, .init = cxit_setup_rma_hybrid_mr_desc,
	  .fini = cxit_teardown_rma, .timeout = CXIT_DEFAULT_TIMEOUT);

Test(amo_hybrid_mr_desc, non_fetching_no_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(false, false, false, false, false,
				       false, false, false, false);
}

Test(amo_hybrid_mr_desc, non_fetching_buf_result_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(false, false, false, true, false, true,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, fetching_no_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, false, false, false, false, false,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, fetching_buf_result_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, false, false, true, false, true,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, non_fetching_no_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(false, false, true, false, false, false,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, non_fetching_buf_result_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(false, false, true, true, false, true,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, fetching_no_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, false, true, false, false, false,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, fetching_buf_result_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, false, true, true, false, true,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, compare_no_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, true, false, false, false, false,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, compare_buf_compare_result_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, true, false, true, true, true,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, compare_no_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, true, true, false, false, false,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, compare_buf_compare_result_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, true, true, true, true, true,
				       false, false, false);
}

Test(amo_hybrid_mr_desc, compare_mswap_buf_compare_result_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, true, false, true, true, true,
				       true, false, false);
}

Test(amo_hybrid_mr_desc, compare_mswap_buf_compare_result_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, true, true, true, true, true,
				       true, false, false);
}

Test(amo_hybrid_mr_desc, read_buf_result_mr_desc_no_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, false, false, true, false, true,
				       false, true, false);
}

Test(amo_hybrid_mr_desc, read_buf_result_mr_desc_cqe)
{
	amo_hybrid_mr_desc_test_runner(true, false, true, true, false, true,
				       false, true, false);
}

Test(amo_hybrid_mr_desc, non_fetching_no_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(false, false, false, false, false,
				       false, false, false, true);
}

Test(amo_hybrid_mr_desc, non_fetching_buf_result_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(false, false, false, true, false, true,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, fetching_no_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, false, false, false, false, false,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, fetching_buf_result_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, false, false, true, false, true,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, non_fetching_no_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(false, false, true, false, false, false,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, non_fetching_buf_result_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(false, false, true, true, false, true,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, fetching_no_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, false, true, false, false, false,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, fetching_buf_result_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, false, true, true, false, true,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, compare_no_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, true, false, false, false, false,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, compare_buf_compare_result_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, true, false, true, true, true,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, compare_no_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, true, true, false, false, false,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, compare_buf_compare_result_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, true, true, true, true, true,
				       false, false, true);
}

Test(amo_hybrid_mr_desc, compare_mswap_buf_compare_result_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, true, false, true, true, true,
				       true, false, true);
}

Test(amo_hybrid_mr_desc, compare_mswap_buf_compare_result_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, true, true, true, true, true,
				       true, false, true);
}

Test(amo_hybrid_mr_desc, read_buf_result_mr_desc_no_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, false, false, true, false, true,
				       false, true, true);
}

Test(amo_hybrid_mr_desc, read_buf_result_mr_desc_cqe_flush)
{
	amo_hybrid_mr_desc_test_runner(true, false, true, true, false, true,
				       false, true, true);
}

Test(amo_hybrid_mr_desc, fetching_amo_failure)
{
	struct mem_region buf_window;
	struct mem_region result_window;
	uint64_t remote_key = 0x1;
	uint64_t buf_key = 0x2;
	uint64_t result_key = 0x4;
	int win_len = 1;
	void *buf_desc[1] = {};
	void *result_desc[1] = {};
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc = {};
	struct fi_rma_ioc rma_ioc = {};
	struct fi_ioc fetch_ioc = {};
	int ret;
	struct fid_cntr *cntr = cxit_read_cntr;
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry cq_err;
	uint64_t amo_flags = FI_TRANSMIT_COMPLETE;

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &buf_key,
			&buf_window);
	cr_assert(ret == FI_SUCCESS);

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &result_key,
			&result_window);
	cr_assert(ret == FI_SUCCESS);

	buf_desc[0] = fi_mr_desc(buf_window.mr);
	result_desc[0] = fi_mr_desc(result_window.mr);

	ioc.addr = buf_window.mem;
	ioc.count = 1;

	rma_ioc.count = 1;
	rma_ioc.key = remote_key;

	msg.msg_iov = &ioc;
	msg.desc = buf_desc;
	msg.iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.datatype = FI_UINT8;
	msg.op = FI_SUM;

	fetch_ioc.addr = result_window.mem;
	fetch_ioc.count = 1;

	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &fetch_ioc, result_desc, 1,
				 amo_flags);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_readerr(cntr) != 1)
		;

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == -FI_EAVAIL);

	ret = fi_cq_readerr(cxit_tx_cq, &cq_err, 0);
	cr_assert(ret == 1);

	cr_assert(cq_err.flags == (FI_ATOMIC | FI_READ));
	cr_assert(cq_err.op_context == NULL);

	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&result_window);
	mr_destroy(&buf_window);
}

Test(amo_hybrid_mr_desc, amo_failure)
{
	struct mem_region buf_window;
	uint64_t remote_key = 0x1;
	uint64_t buf_key = 0x2;
	int win_len = 1;
	void *buf_desc[1] = {};
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc = {};
	struct fi_rma_ioc rma_ioc = {};
	int ret;
	struct fid_cntr *cntr = cxit_write_cntr;
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry cq_err;
	uint64_t amo_flags = FI_TRANSMIT_COMPLETE;

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &buf_key,
			&buf_window);
	cr_assert(ret == FI_SUCCESS);

	buf_desc[0] = fi_mr_desc(buf_window.mr);

	ioc.addr = buf_window.mem;
	ioc.count = 1;

	rma_ioc.count = 1;
	rma_ioc.key = remote_key;

	msg.msg_iov = &ioc;
	msg.desc = buf_desc;
	msg.iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.datatype = FI_UINT8;
	msg.op = FI_SUM;

	ret = fi_atomicmsg(cxit_ep, &msg, amo_flags);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_readerr(cntr) != 1)
		;

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == -FI_EAVAIL);

	ret = fi_cq_readerr(cxit_tx_cq, &cq_err, 0);
	cr_assert(ret == 1);

	cr_assert(cq_err.flags == (FI_ATOMIC | FI_WRITE));
	cr_assert(cq_err.op_context == NULL);

	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&buf_window);
}

Test(amo_hybrid_mr_desc, invalid_addr_fetching_amo_failure)
{
	struct mem_region buf_window;
	struct mem_region result_window;
	uint64_t remote_key = 0x1;
	uint64_t result_key = 0x4;
	int win_len = 1;
	void *buf_desc[1] = {};
	void *result_desc[1] = {};
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc = {};
	struct fi_rma_ioc rma_ioc = {};
	struct fi_ioc fetch_ioc = {};
	int ret;
	struct fid_cntr *cntr = cxit_read_cntr;
	struct fi_cq_tagged_entry cqe;
	struct fi_cq_err_entry cq_err;
	uint64_t amo_flags = FI_TRANSMIT_COMPLETE;

	ret = mr_create(win_len, FI_REMOTE_READ | FI_REMOTE_WRITE,
			0xa, &remote_key, &buf_window);
	cr_assert(ret == FI_SUCCESS);

	ret = mr_create(win_len, FI_READ | FI_WRITE, 0xa, &result_key,
			&result_window);
	cr_assert(ret == FI_SUCCESS);

	buf_desc[0] = fi_mr_desc(buf_window.mr);
	result_desc[0] = fi_mr_desc(result_window.mr);

	ioc.addr = buf_window.mem;
	ioc.count = 1;

	rma_ioc.count = 1;
	rma_ioc.key = remote_key;

	msg.msg_iov = &ioc;
	msg.desc = buf_desc;
	msg.iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;
	msg.datatype = FI_UINT8;
	msg.op = FI_SUM;

	fetch_ioc.addr = result_window.mem + 0xffffffffff;
	fetch_ioc.count = 1;

	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &fetch_ioc, result_desc, 1,
				 amo_flags);
	cr_assert(ret == FI_SUCCESS);

	while (fi_cntr_readerr(cntr) != 1)
		;

	do {
		ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	} while (ret == -FI_EAGAIN);
	cr_assert(ret == -FI_EAVAIL);

	ret = fi_cq_readerr(cxit_tx_cq, &cq_err, 0);
	cr_assert(ret == 1);

	cr_assert(cq_err.flags == (FI_ATOMIC | FI_READ));
	cr_assert(cq_err.op_context == NULL);

	ret = fi_cq_read(cxit_tx_cq, &cqe, 1);
	cr_assert(ret == -FI_EAGAIN);

	mr_destroy(&result_window);
	mr_destroy(&buf_window);
}

struct fi_query_atomic_test {
	enum fi_datatype datatype;
	enum fi_op op;
	bool valid_atomic_attr;
	uint64_t flags;
	int expected_rc;
	int amo_remap_to_pcie_fadd;
};

ParameterizedTestParameters(atomic, query_atomic)
{
	static struct fi_query_atomic_test params[] = {
		/* NULL atomic attributes. */
		{
			.datatype = FI_INT8,
			.op = FI_MIN,
			.valid_atomic_attr = false,
			.flags = 0,
			.expected_rc = -FI_EINVAL,
		},
		/* Bad dataype. */
		{
			.datatype = 0xffff,
			.op = FI_MIN,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EINVAL,
		},
		/* Bad op. */
		{
			.datatype = FI_INT8,
			.op = 0xffff,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EINVAL,
		},
		/* Bad flags. */
		{
			.datatype = FI_INT8,
			.op = FI_MIN,
			.valid_atomic_attr = true,
			.flags = FI_COMPARE_ATOMIC | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EINVAL,
		},
		/* Valid SUM FI_INT8. */
		{
			.datatype = FI_INT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = FI_SUCCESS,
		},
		/* Valid SUM FI_INT8 fetching. */
		{
			.datatype = FI_INT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_FETCH_ATOMIC,
			.expected_rc = FI_SUCCESS,
		}
	};
	size_t param_sz = ARRAY_SIZE(params);

	return cr_make_param_array(struct fi_query_atomic_test, params,
				   param_sz);
}

ParameterizedTest(struct fi_query_atomic_test *params, atomic, query_atomic)
{
	int ret;
	struct fi_atomic_attr atomic_attr;
	struct fi_atomic_attr *attr =
		params->valid_atomic_attr ? &atomic_attr : NULL;

	ret = fi_query_atomic(cxit_domain, params->datatype, params->op, attr,
			      params->flags);

	cr_assert_eq(ret, params->expected_rc,
		     "Unexpected fi_query_atomic() rc: expected=%d got=%d\n",
		     params->expected_rc, ret);
}

TestSuite(pcie_atomic, .init = reset_amo_remap_to_pcie_fadd,
	  .fini = reset_amo_remap_to_pcie_fadd,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

ParameterizedTestParameters(pcie_atomic, query_atomic)
{
	static struct fi_query_atomic_test params[] = {
		/* Valid SUM FI_INT8. */
		{
			.datatype = FI_INT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = FI_SUCCESS,
			.amo_remap_to_pcie_fadd = -1,
		},

		/* Invalid PCIe SUM FI_INT8. Only 32 and 64 bit operations are
		 * supported.
		 */
		{
			.datatype = FI_INT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = -1,
		},

		/* Valid SUM FI_INT32. */
		{
			.datatype = FI_INT32,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = FI_SUCCESS,
			.amo_remap_to_pcie_fadd = -1,
		},

		/* Invalid PCIe SUM FI_INT32 due to amo_remap_to_pcie_fadd being
		 * -1.
		 */
		{
			.datatype = FI_INT32,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = -1,
		},

		/* Invalid PCIe SUM FI_INT32 due to missing FI_FETCH_ATOMIC.
		 */
		{
			.datatype = FI_INT32,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_INT32 since FI_COMPARE_ATOMIC is invalid.
		 */
		{
			.datatype = FI_INT32,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_COMPARE_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Valid PCIe SUM FI_INT32 remapping C_AMO_OP_MIN. */
		{
			.datatype = FI_INT32,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = FI_SUCCESS,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Valid PCIe SUM FI_UINT32 remapping C_AMO_OP_MIN. */
		{
			.datatype = FI_UINT32,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = FI_SUCCESS,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Valid PCIe SUM FI_INT64 remapping C_AMO_OP_MIN. */
		{
			.datatype = FI_INT64,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = FI_SUCCESS,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Valid PCIe SUM FI_UINT64 remapping C_AMO_OP_MIN. */
		{
			.datatype = FI_UINT64,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = FI_SUCCESS,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_INT8. */
		{
			.datatype = FI_INT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_UINT8. */
		{
			.datatype = FI_UINT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_INT16. */
		{
			.datatype = FI_INT16,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_UINT16. */
		{
			.datatype = FI_UINT16,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_FLOAT. */
		{
			.datatype = FI_FLOAT,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_DOUBLE. */
		{
			.datatype = FI_DOUBLE,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_FLOAT_COMPLEX. */
		{
			.datatype = FI_FLOAT_COMPLEX,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_DOUBLE_COMPLEX. */
		{
			.datatype = FI_DOUBLE_COMPLEX,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_LONG_DOUBLE. */
		{
			.datatype = FI_LONG_DOUBLE,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid PCIe SUM FI_LONG_DOUBLE_COMPLEX. */
		{
			.datatype = FI_LONG_DOUBLE_COMPLEX,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = FI_CXI_PCIE_AMO | FI_FETCH_ATOMIC,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid FI_MIN operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_MIN,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Invalid FI_MAX operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_MAX,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MAX,
		},

		/* Invalid FI_SUM operation without PCIe AMO since it is
		 * remapped.
		 */
		{
			.datatype = FI_INT8,
			.op = FI_SUM,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SUM,
		},

		/* Invalid FI_LOR operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_LOR,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LOR,
		},

		/* Invalid FI_LAND operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_LAND,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LAND,
		},

		/* Invalid FI_BOR operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_BOR,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BOR,
		},

		/* Invalid FI_BAND operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_BAND,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BAND,
		},

		/* Invalid FI_LXOR operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_LXOR,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LXOR,
		},

		/* Invalid FI_BXOR operation since it is remapped. */
		{
			.datatype = FI_INT8,
			.op = FI_BXOR,
			.valid_atomic_attr = true,
			.flags = 0,
			.expected_rc = -FI_EOPNOTSUPP,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BXOR,
		},
	};
	size_t param_sz = ARRAY_SIZE(params);

	return cr_make_param_array(struct fi_query_atomic_test, params,
				   param_sz);
}

ParameterizedTest(struct fi_query_atomic_test *params, pcie_atomic,
		  query_atomic)
{
	int ret;
	struct fi_atomic_attr atomic_attr;
	struct fi_atomic_attr *attr =
		params->valid_atomic_attr ? &atomic_attr : NULL;

	/* The AMO remap value must be set before libfabric domain is allocated.
	 * Else, and inconsistent view of the AMO remap value will be read.
	 */
	set_amo_remap_to_pcie_fadd(params->amo_remap_to_pcie_fadd);
	cxit_setup_rma();

	ret = fi_query_atomic(cxit_domain, params->datatype, params->op, attr,
			      params->flags);

	cr_assert_eq(ret, params->expected_rc,
		     "Unexpected fi_query_atomic() rc: expected=%d got=%d\n",
		     params->expected_rc, ret);

	cxit_teardown_rma();
}

struct fi_pcie_fadd_test {
	enum fi_datatype dt;
	union {
		uint64_t u64_src;
		int64_t s64_src;
		uint32_t u32_src;
		int32_t s32_src;
	} src;
	union {
		uint64_t u64_dst;
		int64_t s64_dst;
		uint32_t u32_dst;
		int32_t s32_dst;
	} dst;
	union {
		uint64_t u64_result;
		int64_t s64_result;
		uint32_t u32_result;
		int32_t s32_result;
	} result;
	int amo_remap_to_pcie_fadd;
};

ParameterizedTestParameters(pcie_atomic, fadd)
{
	static struct fi_pcie_fadd_test params[] = {
		/* Interger overflow. */
		{
			.dt = FI_INT32,
			.src.s32_src = 2147483647,
			.dst.s32_dst = 1,
			.result.s32_result = -2147483648,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SWAP,
		},

		/* Unsigned interger overflow. */
		{
			.dt = FI_UINT32,
			.src.u32_src = 0xFFFFFFFF,
			.dst.u32_dst = 1,
			.result.u32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SWAP,
		},

		/* Long overflow. */
		{
			.dt = FI_INT64,
			.src.s64_src = 9223372036854775807,
			.dst.s64_dst = 1,
			.result.u64_result = 0x8000000000000000,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SWAP,
		},

		/* Unsigned long overflow. */
		{
			.dt = FI_UINT64,
			.src.u64_src = 0xFFFFFFFFFFFFFFFF,
			.dst.u64_dst = 1,
			.result.u64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SWAP,
		},

		/* Valid 32-bit AMO with C_AMO_OP_MIN remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Valid 64-bit AMO with C_AMO_OP_MIN remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MIN,
		},

		/* Valid 32-bit AMO with C_AMO_OP_MAX remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MAX,
		},

		/* Valid 64-bit AMO with C_AMO_OP_MAX remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_MAX,
		},

		/* Valid 32-bit AMO with C_AMO_OP_SUM remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SUM,
		},

		/* Valid 64-bit AMO with C_AMO_OP_SUM remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_SUM,
		},

		/* Valid 32-bit AMO with C_AMO_OP_LOR remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LOR,
		},

		/* Valid 64-bit AMO with C_AMO_OP_LOR remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LOR,
		},

		/* Valid 32-bit AMO with C_AMO_OP_LAND remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LAND,
		},

		/* Valid 64-bit AMO with C_AMO_OP_LAND remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LAND,
		},

		/* Valid 32-bit AMO with C_AMO_OP_BOR remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BOR,
		},

		/* Valid 64-bit AMO with C_AMO_OP_BOR remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BOR,
		},

		/* Valid 32-bit AMO with C_AMO_OP_BAND remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BAND,
		},

		/* Valid 64-bit AMO with C_AMO_OP_BAND remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BAND,
		},

		/* Valid 32-bit AMO with C_AMO_OP_LXOR remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LXOR,
		},

		/* Valid 64-bit AMO with C_AMO_OP_LXOR remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_LXOR,
		},

		/* Valid 32-bit AMO with C_AMO_OP_BXOR remapped. */
		{
			.dt = FI_INT32,
			.src.s32_src = -1,
			.dst.s32_dst = 1,
			.result.s32_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BXOR,
		},

		/* Valid 64-bit AMO with C_AMO_OP_BXOR remapped. */
		{
			.dt = FI_INT64,
			.src.s64_src = -4294967296,
			.dst.s64_dst = 4294967296,
			.result.s64_result = 0,
			.amo_remap_to_pcie_fadd = C_AMO_OP_BXOR,
		},
	};
	size_t param_sz = ARRAY_SIZE(params);

	return cr_make_param_array(struct fi_pcie_fadd_test, params,
				   param_sz);
}

ParameterizedTest(struct fi_pcie_fadd_test *params, pcie_atomic, fadd)
{
	int ret;
	size_t amo_size;
	uint64_t rkey = 0x1;
	uint64_t nic_rkey = 0x2;
	struct mem_region remote_window;
	struct mem_region nic_remote_window;
	union {
		uint64_t u64_fetch;
		int64_t s64_fetch;
		uint32_t u32_fetch;
		int32_t s32_fetch;
	} fetch;
	union {
		uint64_t u64_fetch;
		int64_t s64_fetch;
		uint32_t u32_fetch;
		int32_t s32_fetch;
	} nic_fetch;
	struct fi_msg_atomic msg = {};
	struct fi_ioc ioc = {};
	struct fi_rma_ioc rma_ioc = {};
	struct fi_ioc fetch_ioc = {};
	struct fi_cq_tagged_entry cqe;
	uint64_t cur_cpu_fetch_cntr;
	uint64_t new_cpu_fetch_cntr;
	struct cxip_ep *cxi_ep;

	if (params->dt == FI_INT32 || params->dt == FI_UINT32)
		amo_size = 4;
	else
		amo_size = 8;

	/* The AMO remap value must be set before libfabric domain is allocated.
	 * Else, and inconsistent view of the AMO remap value will be read.
	 */
	set_amo_remap_to_pcie_fadd(params->amo_remap_to_pcie_fadd);
	cxit_setup_rma();

	/* PCIe AMOs not supported on netsim. */
	cxi_ep = container_of(cxit_ep, struct cxip_ep, ep);
	if (is_netsim(cxi_ep->ep_obj))
		goto teardown;

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_CPU_FTCH_AMO_REQS,
				 &cur_cpu_fetch_cntr, NULL, true);
	cr_assert(ret == 0);

	/* Create target MR and copy destantion contents into it. */
	ret = mr_create(amo_size, FI_REMOTE_READ | FI_REMOTE_WRITE, 0, &rkey,
			&remote_window);
	cr_assert(ret == FI_SUCCESS);
	memcpy(remote_window.mem, &params->dst, amo_size);

	/* Create another target MR to be used for NIC AMO SUM comparison to the
	 * PCIe AMO.
	 */
	ret = mr_create(amo_size, FI_REMOTE_READ | FI_REMOTE_WRITE, 0,
			&nic_rkey, &nic_remote_window);
	cr_assert(ret == FI_SUCCESS);
	memcpy(nic_remote_window.mem, &params->dst, amo_size);

	/* Fill in fetching AMO desciptors. */
	ioc.addr = &params->src;
	ioc.count = 1;

	rma_ioc.key = rkey;
	rma_ioc.count = 1;

	fetch_ioc.addr = &fetch;
	fetch_ioc.count = 1;

	msg.datatype = params->dt;
	msg.op = FI_SUM;
	msg.msg_iov = &ioc;
	msg.iov_count = 1;
	msg.addr = cxit_ep_fi_addr;
	msg.rma_iov = &rma_ioc;
	msg.rma_iov_count = 1;

	/* Issue PCIe fetch add. */
	ret = fi_fetch_atomicmsg(cxit_ep, &msg, &fetch_ioc, NULL, 1,
				 FI_TRANSMIT_COMPLETE | FI_COMPLETION |
				 FI_CXI_PCIE_AMO);
	cr_assert(ret == FI_SUCCESS);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);

	/* Issue NIC fetching SUM AMO. */
	if (params->amo_remap_to_pcie_fadd != C_AMO_OP_SUM) {
		rma_ioc.key = nic_rkey;
		fetch_ioc.addr = &nic_fetch;

		ret = fi_fetch_atomicmsg(cxit_ep, &msg, &fetch_ioc, NULL, 1,
					FI_TRANSMIT_COMPLETE | FI_COMPLETION);
		cr_assert(ret == FI_SUCCESS);

		ret = cxit_await_completion(cxit_tx_cq, &cqe);
		cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

		validate_tx_event(&cqe, FI_ATOMIC | FI_READ, NULL);
	}

	if (params->dt == FI_INT32) {
		/* Compare PCIe FADD to the expected values. */
		cr_assert_eq(*((int32_t *)remote_window.mem),
			     params->result.s32_result,
			     "Unexpected remote AMO result: got=%d expected=%d\n",
			     *((int32_t *)remote_window.mem),
			     params->result.s32_result);
		cr_assert_eq(fetch.s32_fetch, params->dst.s32_dst,
			     "Unexpected fetch AMO result: got=%d expected=%d\n",
			     fetch.s32_fetch, params->dst.s32_dst);

		/* Compare PCIe FADD to the NIC fetch add/sum values. */
		if (params->amo_remap_to_pcie_fadd != C_AMO_OP_SUM) {
			cr_assert_eq(*((int32_t *)remote_window.mem),
				     *((int32_t *)nic_remote_window.mem),
				     "Unexpected remote AMO result: got=%d expected=%d\n",
				     *((int32_t *)remote_window.mem),
				     *((int32_t *)nic_remote_window.mem));
			cr_assert_eq(fetch.s32_fetch, nic_fetch.s32_fetch,
				     "Unexpected fetch AMO result: got=%d expected=%d\n",
				     fetch.s32_fetch, nic_fetch.s32_fetch);
		}
	} else if (params->dt == FI_UINT32) {
		/* Compare PCIe FADD to the expected values. */
		cr_assert_eq(*((uint32_t *)remote_window.mem),
			     params->result.u32_result,
			     "Unexpected remote AMO result: got=%u expected=%u\n",
			     *((uint32_t *)remote_window.mem),
			     params->result.s32_result);
		cr_assert_eq(fetch.u32_fetch, params->dst.u32_dst,
			     "Unexpected fetch AMO result: got=%u expected=%u\n",
			     fetch.u32_fetch, params->dst.u32_dst);

		/* Compare PCIe FADD to the NIC fetch add/sum values. */
		if (params->amo_remap_to_pcie_fadd != C_AMO_OP_SUM) {
			cr_assert_eq(*((uint32_t *)remote_window.mem),
				     *((uint32_t *)nic_remote_window.mem),
				     "Unexpected remote AMO result: got=%u expected=%u\n",
				     *((uint32_t *)remote_window.mem),
				     *((uint32_t *)nic_remote_window.mem));
			cr_assert_eq(fetch.u32_fetch, nic_fetch.u32_fetch,
				     "Unexpected fetch AMO result: got=%u expected=%u\n",
				      fetch.u32_fetch, nic_fetch.u32_fetch);
		}
	} else if (params->dt == FI_INT64) {
		/* Compare PCIe FADD to the expected values. */
		cr_assert_eq(*((int64_t *)remote_window.mem),
			     params->result.s64_result,
			     "Unexpected remote AMO result: got=%ld expected=%ld\n",
			     *((int64_t *)remote_window.mem),
			     params->result.s64_result);
		cr_assert_eq(fetch.s64_fetch, params->dst.s64_dst,
			     "Unexpected fetch AMO result: got=%ld expected=%ld\n",
			     fetch.s64_fetch, params->dst.s64_dst);

		/* Compare PCIe FADD to the NIC fetch add/sum values. */
		if (params->amo_remap_to_pcie_fadd != C_AMO_OP_SUM) {
			cr_assert_eq(*((int64_t *)remote_window.mem),
				     *((int64_t *)nic_remote_window.mem),
				     "Unexpected remote AMO result: got=%ld expected=%ld\n",
				     *((int64_t *)remote_window.mem),
				     *((int64_t *)nic_remote_window.mem));
			cr_assert_eq(fetch.s64_fetch, nic_fetch.s64_fetch,
				     "Unexpected fetch AMO result: got=%ld expected=%ld\n",
				     fetch.s64_fetch, nic_fetch.s64_fetch);
		}
	} else {
		/* Compare PCIe FADD to the expected values. */
		cr_assert_eq(*((uint64_t *)remote_window.mem),
			     params->result.u64_result,
			     "Unexpected remote AMO result: got=%lu expected=%lu\n",
			     *((uint64_t *)remote_window.mem),
			     params->result.u64_result);
		cr_assert_eq(fetch.u64_fetch, params->dst.u64_dst,
			     "Unexpected fetch AMO result: got=%lu expected=%lu\n",
			     fetch.u64_fetch, params->dst.u64_dst);

		/* Compare PCIe FADD to the NIC fetch add/sum values. */
		if (params->amo_remap_to_pcie_fadd != C_AMO_OP_SUM) {
			cr_assert_eq(*((uint64_t *)remote_window.mem),
				     *((uint64_t *)nic_remote_window.mem),
				     "Unexpected remote AMO result: got=%lu expected=%lu\n",
				     *((uint64_t *)remote_window.mem),
				     *((uint64_t *)nic_remote_window.mem));
			cr_assert_eq(fetch.u64_fetch, nic_fetch.u64_fetch,
				     "Unexpected fetch AMO result: got=%lu expected=%lu\n",
				     fetch.u64_fetch, nic_fetch.u64_fetch);
		}
	}

	ret = cxit_dom_read_cntr(C_CNTR_IXE_DMAWR_CPU_FTCH_AMO_REQS,
				 &new_cpu_fetch_cntr, NULL, true);
	cr_assert(ret == 0);

	cr_assert(cur_cpu_fetch_cntr + 1 == new_cpu_fetch_cntr);

	mr_destroy(&nic_remote_window);
	mr_destroy(&remote_window);

teardown:
	cxit_teardown_rma();
}
