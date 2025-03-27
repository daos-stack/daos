/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2018 Hewlett Packard Enterprise Development LP
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include <ofi.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(cq, .init = cxit_setup_cq, .fini = cxit_teardown_cq,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic CQ creation */
Test(cq, simple)
{
	cxit_create_cqs();
	cr_assert(cxit_tx_cq != NULL);
	cr_assert(cxit_rx_cq != NULL);

	cxit_destroy_cqs();
}

static void req_populate(struct cxip_req *req, fi_addr_t *addr)
{
	*addr = 0xabcd0;
	req->flags = FI_SEND;
	req->context = 0xabcd2;
	req->data = 0xabcd4;
	req->tag = 0xabcd5;
	req->buf = 0xabcd6;
	req->data_len = 0xabcd7;
	req->discard = false;
}

Test(cq, read_fmt_context)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_entry entry;
	fi_addr_t req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete(&req);
	ret = fi_cq_read(cxit_tx_cq, &entry, 1);
	cr_assert(ret == 1);
	cr_assert((uint64_t)entry.op_context == req.context);

	cxit_destroy_cqs();
}

Test(cq, read_fmt_msg)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_msg_entry entry;
	fi_addr_t req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_MSG;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete(&req);
	ret = fi_cq_read(cxit_tx_cq, &entry, 1);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(entry.flags == req.flags);
	cr_assert(entry.len == req.data_len);

	cxit_destroy_cqs();
}

Test(cq, read_fmt_data)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_data_entry entry;
	fi_addr_t req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_DATA;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete(&req);
	ret = fi_cq_read(cxit_tx_cq, &entry, 1);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(entry.flags == req.flags);
	cr_assert(entry.len == req.data_len);
	cr_assert((uint64_t)entry.buf == req.buf);
	cr_assert(entry.data == req.data);

	cxit_destroy_cqs();
}

Test(cq, read_fmt_tagged)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_tagged_entry entry;
	fi_addr_t req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete(&req);
	ret = fi_cq_read(cxit_tx_cq, &entry, 1);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(entry.flags == req.flags);
	cr_assert(entry.len == req.data_len);
	cr_assert((uint64_t)entry.buf == req.buf);
	cr_assert(entry.data == req.data);
	cr_assert(entry.tag == req.tag);

	cxit_destroy_cqs();
}

Test(cq, readfrom_fmt_context)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_entry entry;
	fi_addr_t addr = 0, req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_CONTEXT;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete_addr(&req, req_addr);
	ret = fi_cq_readfrom(cxit_tx_cq, &entry, 1, &addr);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(addr == req_addr);

	cxit_destroy_cqs();
}

Test(cq, readfrom_fmt_msg)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_msg_entry entry;
	fi_addr_t addr = 0, req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_MSG;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete_addr(&req, req_addr);
	ret = fi_cq_readfrom(cxit_tx_cq, &entry, 1, &addr);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(entry.flags == req.flags);
	cr_assert(entry.len == req.data_len);
	cr_assert(addr == req_addr);

	cxit_destroy_cqs();
}

Test(cq, readfrom_fmt_data)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_data_entry entry;
	fi_addr_t addr = 0, req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_DATA;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete_addr(&req, req_addr);
	ret = fi_cq_readfrom(cxit_tx_cq, &entry, 1, &addr);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(entry.flags == req.flags);
	cr_assert(entry.len == req.data_len);
	cr_assert((uint64_t)entry.buf == req.buf);
	cr_assert(entry.data == req.data);
	cr_assert(addr == req_addr);

	cxit_destroy_cqs();
}

Test(cq, readfrom_fmt_tagged)
{
	int ret;
	struct cxip_req req;
	struct fi_cq_tagged_entry entry;
	fi_addr_t addr = 0, req_addr;
	struct cxip_cq *cxi_cq;

	cxit_tx_cq_attr.format = FI_CQ_FORMAT_TAGGED;
	cxit_create_cqs();

	req_populate(&req, &req_addr);
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	cxip_cq_req_complete_addr(&req, req_addr);
	ret = fi_cq_readfrom(cxit_tx_cq, &entry, 1, &addr);
	cr_assert(ret == 1);

	cr_assert((uint64_t)entry.op_context == req.context);
	cr_assert(entry.flags == req.flags);
	cr_assert(entry.len == req.data_len);
	cr_assert((uint64_t)entry.buf == req.buf);
	cr_assert(entry.data == req.data);
	cr_assert(entry.tag == req.tag);
	cr_assert(addr == req_addr);

	cxit_destroy_cqs();
}

Test(cq, cq_open_null_attr)
{
	int ret;
	struct fid_cq *cxi_open_cq = NULL;
	struct cxip_cq *cxi_cq = NULL;

	/* Open a CQ with a NULL attribute object pointer */
	ret = fi_cq_open(cxit_domain, NULL, &cxi_open_cq, NULL);
	cr_assert(ret == FI_SUCCESS, "fi_cq_open with NULL attr");
	cr_assert_not_null(cxi_open_cq);

	/* Validate that the default attributes were set */
	cxi_cq = container_of(cxi_open_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cxi_cq->attr.size, CXIP_CQ_DEF_SZ);
	cr_assert_eq(cxi_cq->attr.flags, 0);
	cr_assert_eq(cxi_cq->attr.format, FI_CQ_FORMAT_CONTEXT);
	cr_assert_eq(cxi_cq->attr.wait_obj, FI_WAIT_NONE);
	cr_assert_eq(cxi_cq->attr.signaling_vector, 0);
	cr_assert_eq(cxi_cq->attr.wait_cond, FI_CQ_COND_NONE);
	cr_assert_null((void *)cxi_cq->attr.wait_set);

	ret = fi_close(&cxi_open_cq->fid);
	cr_assert(ret == FI_SUCCESS);
	cxi_open_cq = NULL;
}

struct cq_format_attr_params {
	enum fi_cq_format in_format;
	enum fi_cq_format out_format;
	int status;
};

ParameterizedTestParameters(cq, cq_attr_format)
{
	size_t param_sz;

	static struct cq_format_attr_params params[] = {
		{.in_format = FI_CQ_FORMAT_CONTEXT,
		 .out_format = FI_CQ_FORMAT_CONTEXT,
		 .status = FI_SUCCESS},
		{.in_format = FI_CQ_FORMAT_MSG,
		 .out_format = FI_CQ_FORMAT_MSG,
		 .status = FI_SUCCESS},
		{.in_format = FI_CQ_FORMAT_DATA,
		 .out_format = FI_CQ_FORMAT_DATA,
		 .status = FI_SUCCESS},
		{.in_format = FI_CQ_FORMAT_TAGGED,
		 .out_format = FI_CQ_FORMAT_TAGGED,
		 .status = FI_SUCCESS},
		{.in_format = FI_CQ_FORMAT_UNSPEC,
		 .out_format = FI_CQ_FORMAT_CONTEXT,
		 .status = FI_SUCCESS},
		{.in_format = FI_CQ_FORMAT_UNSPEC - 1,
		 .out_format = -1, /* Unchecked in failure case */
		 .status = -FI_ENOSYS}
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct cq_format_attr_params, params,
				   param_sz);
}

ParameterizedTest(struct cq_format_attr_params *param, cq, cq_attr_format)
{
	int ret;
	struct fid_cq *cxi_open_cq = NULL;
	struct fi_cq_attr cxit_cq_attr = {0};
	struct cxip_cq *cxi_cq = NULL;

	cxit_cq_attr.format = param->in_format;
	cxit_cq_attr.wait_obj = FI_WAIT_NONE; /* default */
	cxit_cq_attr.size = 0; /* default */

	/* Open a CQ with a NULL attribute object pointer */
	ret = fi_cq_open(cxit_domain, &cxit_cq_attr, &cxi_open_cq, NULL);
	cr_assert_eq(ret, param->status,
		     "fi_cq_open() status mismatch %d != %d with format %d. %s",
		     ret, param->status, cxit_cq_attr.format,
		     fi_strerror(-ret));

	if (ret != FI_SUCCESS) {
		/* Test Complete */
		return;
	}

	/* Validate that the format attribute */
	cr_assert_not_null(cxi_open_cq,
			   "fi_cq_open() cxi_open_cq is NULL with format %d",
			   cxit_cq_attr.format);
	cxi_cq = container_of(cxi_open_cq, struct cxip_cq, util_cq.cq_fid);
	cr_assert_eq(cxi_cq->attr.format, param->out_format);

	ret = fi_close(&cxi_open_cq->fid);
	cr_assert(ret == FI_SUCCESS);
}

struct cq_wait_attr_params {
	enum fi_wait_obj in_wo;
	enum fi_wait_obj out_wo;
	int status;
};

ParameterizedTestParameters(cq, cq_attr_wait)
{
	size_t param_sz;

	static struct cq_wait_attr_params params[] = {
		{.in_wo = FI_WAIT_NONE,
		 .status = FI_SUCCESS},
		{.in_wo = FI_WAIT_FD,
		 .status = FI_SUCCESS},
		{.in_wo = FI_WAIT_SET,
		 .status = -FI_ENOSYS},
		{.in_wo = FI_WAIT_MUTEX_COND,
		 .status = -FI_ENOSYS},
		{.in_wo = FI_WAIT_UNSPEC,
		 .status = FI_SUCCESS},
		{.in_wo = FI_WAIT_NONE - 1,
		 .status = -FI_ENOSYS}
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct cq_wait_attr_params, params,
				   param_sz);
}

ParameterizedTest(struct cq_wait_attr_params *param, cq, cq_attr_wait)
{
	int ret;
	struct fid_cq *cxi_open_cq = NULL;
	struct fi_cq_attr cxit_cq_attr = {0};

	cxit_cq_attr.wait_obj = param->in_wo;
	cxit_cq_attr.format = FI_CQ_FORMAT_UNSPEC; /* default */
	cxit_cq_attr.size = 0; /* default */

	/* Open a CQ with a NULL attribute object pointer */
	ret = fi_cq_open(cxit_domain, &cxit_cq_attr, &cxi_open_cq, NULL);
	cr_assert_eq(ret, param->status,
		     "fi_cq_open() status mismatch %d != %d with wait obj %d. %s",
		     ret, param->status, cxit_cq_attr.wait_obj,
		     fi_strerror(-ret));

	if (ret != FI_SUCCESS) {
		/* Test Complete */
		return;
	}

	ret = fi_close(&cxi_open_cq->fid);
	cr_assert(ret == FI_SUCCESS);
}

struct cq_size_attr_params {
	size_t in_sz;
	size_t out_sz;
};

ParameterizedTestParameters(cq, cq_attr_size)
{
	size_t param_sz;

	static struct cq_size_attr_params params[] = {
		{.in_sz = 0,
		 .out_sz = CXIP_CQ_DEF_SZ},
		{.in_sz = 1 << 9,
		 .out_sz = 1 << 9},
		{.in_sz = 1 << 6,
		 .out_sz = 1 << 6}
	};

	param_sz = ARRAY_SIZE(params);
	return cr_make_param_array(struct cq_size_attr_params, params,
				   param_sz);
}

ParameterizedTest(struct cq_size_attr_params *param, cq, cq_attr_size)
{
	int ret;
	struct fid_cq *cxi_open_cq = NULL;
	struct fi_cq_attr cxit_cq_attr = {0};

	cxit_cq_attr.format = FI_CQ_FORMAT_UNSPEC; /* default */
	cxit_cq_attr.wait_obj = FI_WAIT_NONE; /* default */
	cxit_cq_attr.size = param->in_sz;

	/* Open a CQ with a NULL attribute object pointer */
	ret = fi_cq_open(cxit_domain, &cxit_cq_attr, &cxi_open_cq, NULL);
	cr_assert_eq(ret, FI_SUCCESS,
		     "fi_cq_open() status mismatch %d != %d with size %ld. %s",
		     ret, FI_SUCCESS, cxit_cq_attr.size,
		     fi_strerror(-ret));
	cr_assert_not_null(cxi_open_cq);

	ret = fi_close(&cxi_open_cq->fid);
	cr_assert(ret == FI_SUCCESS);
}

Test(cq, cq_open_null_domain, .signal = SIGSEGV)
{
	struct fid_cq *cxi_open_cq = NULL;

	/*
	 * Attempt to open a CQ with a NULL domain pointer
	 * Expect a SIGSEGV since the fi_cq_open implementation attempts to
	 * use the domain pointer before checking.
	 */
	fi_cq_open(NULL, NULL, &cxi_open_cq, NULL);
}

Test(cq, cq_open_null_cq)
{
	/* Attempt to open a CQ with a NULL cq pointer */
	int ret;

	ret = fi_cq_open(cxit_domain, NULL, NULL, NULL);
	cr_assert(ret == -FI_EINVAL, "fi_cq_open with NULL cq");
}

Test(cq, cq_readerr_null_cq, .signal = SIGSEGV)
{
	struct fi_cq_err_entry err_entry;

	/* Attempt to read an err with a CQ with a NULL cq pointer */
	fi_cq_readerr(NULL, &err_entry, (uint64_t)0);
}

Test(cq, cq_readerr_no_errs)
{
	int ret;
	struct fid_cq *cxi_open_cq = NULL;
	struct fi_cq_err_entry err_entry;

	/* Open a CQ */
	ret = fi_cq_open(cxit_domain, NULL, &cxi_open_cq, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cq_open with NULL attr");
	cr_assert_not_null(cxi_open_cq);

	/* Attempt to read an err with a CQ with a NULL buff pointer */
	ret = fi_cq_readerr(cxi_open_cq, &err_entry, (uint64_t)0);
	/* Expect no completions to be available */
	cr_assert_eq(ret, -FI_EAGAIN, "fi_cq_readerr returned %d", ret);

	ret = fi_close(&cxi_open_cq->fid);
	cr_assert_eq(ret, FI_SUCCESS);
}

void err_entry_comp(struct fi_cq_err_entry *a,
		   struct fi_cq_err_entry *b,
		   size_t size)
{
	uint8_t *data_a, *data_b;

	data_a = (uint8_t *)a;
	data_b = (uint8_t *)b;

	for (int i = 0; i < size; i++)
		if (data_a[i] != data_b[i])
			cr_expect_fail("Mismatch at offset %d. %02X - %02X",
				       i, data_a[i], data_b[i]);
}

Test(cq, cq_readerr_err)
{
	int ret;
	struct fid_cq *cxi_open_cq = NULL;
	struct fi_cq_err_entry err_entry, fake_entry;
	struct cxip_cq *cxi_cq;
	uint8_t *data_fake, *data_err;

	/* initialize the entries with data */
	data_fake = (uint8_t *)&fake_entry;
	data_err = (uint8_t *)&err_entry;
	for (int i = 0; i < sizeof(fake_entry); i++) {
		data_fake[i] = (uint8_t)i;
		data_err[i] = (uint8_t)0xa5;
	}
	fake_entry.prov_errno = 18;
	fake_entry.err_data = err_entry.err_data = NULL;
	fake_entry.err_data_size = err_entry.err_data_size = 0;

	/* Open a CQ */
	ret = fi_cq_open(cxit_domain, NULL, &cxi_open_cq, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_cq_open with NULL attr");
	cr_assert_not_null(cxi_open_cq);

	/* Add a fake error to the CQ's error ringbuffer */
	cxi_cq = container_of(cxi_open_cq, struct cxip_cq, util_cq.cq_fid);
	ofi_cq_write_error(&cxi_cq->util_cq, &fake_entry);

	/* Attempt to read an err with a CQ with a NULL buff pointer */
	ret = fi_cq_readerr(cxi_open_cq, &err_entry, (uint64_t)0);
	/* Expect 1 completion to be available */
	cr_assert_eq(ret, 1, "fi_cq_readerr returned %d", ret);
	/* Expect the data to match the fake entry */
	err_entry_comp(&err_entry, &fake_entry, sizeof(fake_entry));
	printf("prov_errno: %s\n",
			fi_cq_strerror(cxi_open_cq, err_entry.prov_errno,
				       NULL, NULL, 0));

	ret = fi_close(&cxi_open_cq->fid);
	cr_assert_eq(ret, FI_SUCCESS);
}

Test(cq, cq_readerr_reperr)
{
	int ret;
	struct fi_cq_err_entry err_entry = {0};
	struct cxip_req req = {0};
	size_t olen, err_data_size;
	int err, prov_errno;
	void *err_data;
	struct cxip_cq *cxi_cq;
	uint8_t err_buff[32] = {0};

	/* initialize the input data */
	req.flags = 0x12340987abcd5676;
	req.context = 0xa5a5a5a5a5a5a5a5;
	req.data_len = 0xabcdef0123456789;
	req.data = 0xbadcfe1032547698;
	req.tag = 0xefcdab0192837465;
	olen = 0x4545121290907878;
	err = -3;
	prov_errno = -2;
	err_data = (void *)err_buff;
	err_data_size = ARRAY_SIZE(err_buff);

	/* Open a CQ */
	cxit_create_cqs();
	cxi_cq = container_of(cxit_tx_cq, struct cxip_cq, util_cq.cq_fid);
	req.cq = cxi_cq;

	/* Add an error to the CQ's error ringbuffer */
	ret = cxip_cq_req_error(&req, olen, err, prov_errno,
				err_data, err_data_size, FI_ADDR_UNSPEC);
	cr_assert_eq(ret, 0, "cxip_cq_report_error() error %d", ret);

	/* Attempt to read an err with a CQ with a NULL buff pointer */
	ret = fi_cq_readerr(cxit_tx_cq, &err_entry, (uint64_t)0);
	cr_assert_eq(ret, 1, "fi_cq_readerr returned %d", ret);

	/* Expect the data to match the fake entry */
	cr_assert_eq(err_entry.err, err);
	cr_assert_eq(err_entry.olen, olen);
	cr_assert_eq(err_entry.len, req.data_len);
	cr_assert_eq(err_entry.prov_errno, prov_errno);
	cr_assert_eq(err_entry.flags, req.flags);
	cr_assert_eq(err_entry.data, req.data);
	cr_assert_eq(err_entry.tag, req.tag);
	cr_assert_eq(err_entry.op_context, (void *)(uintptr_t)req.context);
	cr_assert_eq(memcmp(err_entry.err_data, err_data, err_data_size), 0);
	cr_assert_leq(err_entry.err_data_size, err_data_size,
		      "Size mismatch. %zd, %zd",
		      err_entry.err_data_size, err_data_size);

	cxit_destroy_cqs();
}
