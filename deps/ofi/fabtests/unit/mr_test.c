/*
 * Copyright (c) 2013-2017 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014-2016 Cisco Systems, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>

#include "unit_common.h"
#include "hmem.h"
#include "shared.h"

static char err_buf[512];

/*
 * Tests:
 */
static int mr_reg()
{
	int i, j;
	int ret = 0;
	int testret = FAIL;
	struct fid_mr *mr;
	uint64_t access;
	uint64_t *access_combinations;
	int cnt;

	if (opts.iface != FI_HMEM_SYSTEM) {
		ret = 0;
		testret = SKIPPED;
		sprintf(err_buf, "fi_mr_reg cannot be used to register hmem.");
		goto out;
	}

	access = ft_info_to_mr_access(fi);
	ret = ft_alloc_bit_combo(0, access, &access_combinations, &cnt);
	if (ret) {
		FT_UNIT_STRERR(err_buf, "ft_alloc_bit_combo failed", ret);
		goto out;
	}

	for (i = 0; i < test_cnt; i++) {
		buf_size = test_size[i].size;
		for (j = 0; j < cnt; j++) {
			ret = fi_mr_reg(domain, buf, buf_size,
					access_combinations[j], 0,
					FT_MR_KEY, 0, &mr, NULL);
			if (ret) {
				FT_UNIT_STRERR(err_buf, "fi_mr_reg failed", ret);
				goto free;
			}

			ret = fi_close(&mr->fid);
			if (ret) {
				FT_UNIT_STRERR(err_buf, "fi_close failed", ret);
				goto free;
			}
		}
	}
	testret = PASS;
free:
	ft_free_bit_combo(access_combinations);
out:
	return TEST_RET_VAL(ret, testret);
}

static int mr_regv()
{
	int i, j, n;
	int ret = 0;
	int testret = FAIL;
	struct fid_mr *mr;
	struct iovec *iov;
	char *base;

	if (opts.iface != FI_HMEM_SYSTEM) {
		ret = 0;
		testret = SKIPPED;
		sprintf(err_buf, "fi_mr_regv cannot be used to register hmem.");
		goto out;
	}

	iov = calloc(fi->domain_attr->mr_iov_limit, sizeof(*iov));
	if (!iov) {
		perror("calloc");
		ret = -FI_ENOMEM;
		goto out;
	}

	for (i = 0; i < test_cnt &&
	     test_size[i].size <= fi->domain_attr->mr_iov_limit; i++) {
		n = test_size[i].size;
		base = buf;

		for (j = 0; j < n; j++) {
			iov[j].iov_base = base;
			iov[j].iov_len = test_size[test_cnt - 1].size / n;
			base += iov[j].iov_len;
		}

		ret = fi_mr_regv(domain, &iov[0], n, ft_info_to_mr_access(fi), 0,
				 FT_MR_KEY, 0, &mr, NULL);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "fi_mr_regv failed", ret);
			goto free;
		}

		ret = fi_close(&mr->fid);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "fi_close failed", ret);
			goto free;
		}
	}
	testret = PASS;
free:
	free(iov);
out:
	return TEST_RET_VAL(ret, testret);
}

static int mr_regattr()
{
	int i, j, n;
	int ret = 0;
	int testret = FAIL;
	struct fid_mr *mr;
	struct iovec *iov;
	struct fi_mr_dmabuf *dmabuf = NULL;
	uint64_t flags = 0;
	struct fi_mr_attr attr = {0};
	char *base;

	iov = calloc(fi->domain_attr->mr_iov_limit, sizeof(*iov));
	if (!iov) {
		perror("calloc");
		ret = -FI_ENOMEM;
		goto out;
	}

	dmabuf = calloc(fi->domain_attr->mr_iov_limit, sizeof(*dmabuf));
	if (!dmabuf) {
		perror("calloc for dmabuf");
		ret = -FI_ENOMEM;
		goto free_iov;
	}

	for (i = 0; i < test_cnt &&
	     test_size[i].size <= fi->domain_attr->mr_iov_limit; i++) {

		n = test_size[i].size;
		base = buf;
		for (j = 0; j < n; j++) {
			iov[j].iov_base = base;
			iov[j].iov_len = test_size[test_cnt - 1].size / n;
			base += iov[j].iov_len;
		}

		if (opts.options & FT_OPT_REG_DMABUF_MR) {
			ret = ft_get_dmabuf_from_iov(dmabuf, iov, n, opts.iface);
			if (ret) {
				FT_UNIT_STRERR(err_buf, "ft_get_dmabuf_from_iov failed", ret);
				goto free_dmabuf;
			}
			flags |= FI_MR_DMABUF;
		}

		ft_fill_mr_attr(&iov[0], dmabuf, n, ft_info_to_mr_access(fi),
				FT_MR_KEY, opts.iface, opts.device, &attr, flags);
		ret = fi_mr_regattr(domain, &attr, flags, &mr);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "fi_mr_regattr failed", ret);
			goto free_dmabuf;
		}

		ret = fi_close(&mr->fid);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "fi_close failed", ret);
			goto free_dmabuf;
		}
	}
	testret = PASS;
free_dmabuf:
	free(dmabuf);
free_iov:
	free(iov);
out:
	return TEST_RET_VAL(ret, testret);
}

static int mr_reg_free_then_alloc()
{
	int i, ret, testret;
	size_t buf_size = test_size[test_cnt-1].size;
	struct fid_mr *mr;
	char *buf2;
	struct iovec iov;
	struct fi_mr_attr attr = {0};
	int numtry = 5;
	uint64_t flags = 0;
	struct fi_mr_dmabuf dmabuf = {0};

	testret = FAIL;
	for (i = 0; i < numtry; ++i) {
		iov.iov_base = buf;
		iov.iov_len = buf_size;
		if (opts.options & FT_OPT_REG_DMABUF_MR) {
			ret = ft_get_dmabuf_from_iov(&dmabuf, &iov, 1, opts.iface);
			if (ret) {
				FT_UNIT_STRERR(err_buf, "ft_get_dmabuf_from_iov failed", ret);
				goto out;
			}
			flags |= FI_MR_DMABUF;
		}
		ft_fill_mr_attr(&iov, &dmabuf, 1, ft_info_to_mr_access(fi),
				FT_MR_KEY, opts.iface, opts.device, &attr, flags);

		ret = fi_mr_regattr(domain, &attr, flags, &mr);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "fi_mr_reg failed", ret);
			goto out;
		}

		ret = fi_close(&mr->fid);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "fi_close failed", ret);
			goto out;
		}

		ret = ft_hmem_alloc(opts.iface, opts.device,
				    (void **)&buf2, buf_size);
		if (ret)
			goto out;

		ret = ft_hmem_free(opts.iface, buf);
		if (ret) {
			FT_UNIT_STRERR(err_buf, "ft_hmem_free failed",
				       ret);
			goto out;
		}

		buf = buf2;
		iov.iov_base = buf;
	}

	testret = PASS;
out:
	return TEST_RET_VAL(ret, testret);
}

struct test_entry test_array[] = {
	TEST_ENTRY(mr_reg, "Test fi_mr_reg across different access combinations"),
	TEST_ENTRY(mr_regv, "Test fi_mr_regv across various buffer sizes"),
	TEST_ENTRY(mr_regattr, "Test fi_mr_regattr across various buffer sizes"),
	TEST_ENTRY(mr_reg_free_then_alloc, "Test fi_mr_reg on buff that was freed and allocated"),
	{ NULL, "" }
};

static void usage(char *name)
{
	ft_unit_usage(name, "Unit test for Memory Region (MR)");
	ft_hmem_usage();
}

int main(int argc, char **argv)
{
	int op, ret;
	int failed = 0;

	buf = NULL;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, FAB_OPTS HMEM_OPTS "h")) != -1) {
		switch (op) {
		default:
			ft_parseinfo(op, optarg, hints, &opts);
			break;
		case '?':
		case 'h':
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	ret = ft_init();
	if (ret) {
		FT_PRINTERR("ft_init", ret);
		goto out;
	}

	hints->mode = ~0;
	hints->domain_attr->mode = ~0;
	hints->domain_attr->mr_mode = ~(FI_MR_BASIC | FI_MR_SCALABLE | FI_MR_LOCAL);

	hints->caps |= FI_MSG | FI_RMA;
	if (opts.options & FT_OPT_ENABLE_HMEM)
		hints->caps |= FI_HMEM;

	ret = fi_getinfo(FT_FIVERSION, NULL, 0, 0, hints, &fi);
	if (ret) {
		hints->caps &= ~FI_RMA;
		ret = fi_getinfo(FT_FIVERSION, NULL, 0, 0, hints, &fi);
		if (ret) {
			FT_PRINTERR("fi_getinfo", ret);
			goto out;
		}
	}

	if (!ft_info_to_mr_access(fi))
		goto out;

	if (!fi->domain_attr->mr_iov_limit) {
		ret = -FI_EINVAL;
		FT_PRINTERR("mr_iov_limit not set", ret);
		goto out;
	}

	ret = ft_open_fabric_res();
	if (ret)
		goto out;

	ret = ft_hmem_alloc(opts.iface, opts.device, (void **)&buf,
			    test_size[test_cnt - 1].size);
	if (ret)
		goto out;

	printf("Testing MR on fabric %s\n", fi->fabric_attr->name);

	failed = run_tests(test_array, err_buf);
	if (failed > 0) {
		printf("Summary: %d tests failed\n", failed);
	} else {
		printf("Summary: all tests passed\n");
	}

out:
	ft_free_res();
	return ret ? ft_exit_code(ret) : (failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
