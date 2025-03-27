/*
 * Copyright (c) Amazon Inc.  All rights reserved.
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
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>

#include "unit_common.h"
#include "hmem.h"
#include "shared.h"

char err_buf[512];

int test_setopt_cuda_api_permmitted()
{
	bool optval = true;
	int err, ret;

	hints->caps |= FI_HMEM;
	hints->domain_attr->mr_mode |= FI_MR_HMEM;

	err = fi_getinfo(FT_FIVERSION, NULL,
			 0, 0, hints, &fi);
	if (err) {
		if (err == -FI_ENODATA) {
			ret = SKIPPED;
			FT_UNIT_STRERR(err_buf,
				       "no HMEM support",
				       err);
		} else {
			ret = FAIL;
			FT_UNIT_STRERR(err_buf,
				       "fi_getinfo failed!",
				       err);
		}

		goto out;
	}

	err = ft_open_fabric_res();
	if (err) {
		ret = FAIL;
		FT_UNIT_STRERR(err_buf,
			       "open fabric resource failed!",
			       err);
		goto out;
	}

	err = fi_endpoint(domain, fi, &ep, NULL);
	if (err) {
		ret = FAIL;
		FT_UNIT_STRERR(err_buf,
			       "open endpoint failed!",
			       err);
		goto out;
	}

	err = fi_setopt(&ep->fid, FI_OPT_ENDPOINT,
			FI_OPT_CUDA_API_PERMITTED,
			&optval, sizeof(optval) );
	if (err == -FI_ENOPROTOOPT) {
		/* Per document, any provider that claim
		 * support FI_HMEM capability is required
		 * to implement this option
		 */
		ret = FAIL;
		FT_UNIT_STRERR(err_buf,
			       "FI_OPT_CUDA_API_PERMITTED was not implemented!",
			       err);
	} else if (err == -FI_EOPNOTSUPP || err == -FI_EINVAL || err == 0) {
		/* per document, both -FI_EOPNOTSUPP and
		 * -FI_EINVAL are valid return for setting
		 * this option:
		 *	-FI_EOPNOTSUPP means the provider's HMEM CUDA
		 *	support rely on calling CUDA API.
		 *	-FI_EINVAL means there is no CUDA device
		 *	 or CUDA library available
		 */
		ret = PASS;
	} else {
		FT_UNIT_STRERR(err_buf, "fi_setopt failed!", err);
		ret = FAIL;
	}

out:
	ft_close_fids(); /* close ep, eq, domain, fabric */
	return ret;
}

static void usage(char *name)
{
        ft_unit_usage(name, "Unit test for fi_setopt");
}

int main(int argc, char **argv)
{
	int op;
	struct test_entry test_array[] = {
		TEST_ENTRY(test_setopt_cuda_api_permmitted, "Test FI_OPT_CUDA_API_PERMITTED"),
		TEST_ENTRY(NULL, ""),
	};

	int failed;

	hints = fi_allocinfo();
	if (!hints) {
		FT_UNIT_STRERR(err_buf,
			       "hints allocationed failed!",
			       -FI_ENOMEM);
		return FAIL;
	}

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

	hints->mode = ~0;
	hints->domain_attr->mode = ~0;
	hints->domain_attr->mr_mode = ~(FI_MR_BASIC | FI_MR_SCALABLE);
	hints->caps |= FI_MSG;

	failed = run_tests(test_array, err_buf);
	if (failed > 0) {
		printf("Summary: %d tests failed\n", failed);
	} else {
		printf("Summary: all tests passed\n");
	}

	ft_free_res();
	return (failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
