/*
 * Copyright (c) 2013-2016 Intel Corporation.  All rights reserved.
 * Copyright (c) 2023 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * This software is available to you under the BSD license
 * below:
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

#include <rdma/fi_errno.h>

#include <shared.h>
#include "benchmark_shared.h"

static int run(void)
{
	int i, ret;

	if (hints->ep_attr->type == FI_EP_MSG) {
		if (!opts.dst_addr) {
			ret = ft_start_server();
			if (ret)
				return ret;
		}

		ret = opts.dst_addr ? ft_client_connect() : ft_server_connect();
	} else {
		ret = ft_init_fabric();
	}
	if (ret)
		return ret;

	ret = ft_exchange_keys(&remote);
	if (ret)
		return ret;

	if (!(opts.options & FT_OPT_SIZE)) {
		for (i = 0; i < TEST_CNT; i++) {
			if (!ft_use_size(i, opts.sizes_enabled))
				continue;
			opts.transfer_size = test_size[i].size;
			init_test(&opts, test_name, sizeof(test_name));
			ret = pingpong_rma(opts.rma_op, &remote);
			if (ret)
				goto out;
		}
	} else {
		init_test(&opts, test_name, sizeof(test_name));
		ret = pingpong_rma(opts.rma_op, &remote);
		if (ret)
			goto out;
	}

	ft_finalize();
out:
	return ret;
}

int main(int argc, char **argv)
{
	int op, ret;

	opts = INIT_OPTS;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	hints->caps = FI_MSG | FI_RMA | FI_WRITE | FI_REMOTE_WRITE;
	hints->domain_attr->resource_mgmt = FI_RM_ENABLED;
	hints->mode = FI_CONTEXT;
	hints->domain_attr->threading = FI_THREAD_DOMAIN;
	hints->addr_format = opts.address_format;

	while ((op = getopt_long(argc, argv, "Uh" CS_OPTS INFO_OPTS API_OPTS
			    BENCHMARK_OPTS, long_opts, &lopt_idx)) != -1) {
		switch (op) {
		default:
			if (!ft_parse_long_opts(op, optarg))
				continue;
			ft_parse_benchmark_opts(op, optarg);
			ft_parseinfo(op, optarg, hints, &opts);
			ft_parsecsopts(op, optarg, &opts);
			ret = ft_parse_api_opts(op, optarg, hints, &opts);
			if (ret)
				return ret;
			break;
		case 'U':
			hints->tx_attr->op_flags |= FI_DELIVERY_COMPLETE;
			break;
		case '?':
		case 'h':
			ft_csusage(argv[0], "Pingpong test using RMA operations.");
			ft_benchmark_usage();
			FT_PRINT_OPTS_USAGE("-o <op>", "rma op type: write|writedata (default: write)\n");
			ft_longopts_usage();
			return EXIT_FAILURE;
		}
	}

	if (opts.rma_op != FT_RMA_WRITE && opts.rma_op != FT_RMA_WRITEDATA) {
		FT_ERR("Only write and writedata operations are supported by rma_pingpong");
		return EXIT_FAILURE;
	}

	if (opts.iface != FI_HMEM_SYSTEM && opts.rma_op == FT_RMA_WRITE) {
		FT_ERR("rma_pingpong write test does not support HMEM");
		return EXIT_FAILURE;
	}

	/* data validation on read and write ops requires delivery_complete semantics. */
	if (opts.rma_op != FT_RMA_WRITEDATA && ft_check_opts(FT_OPT_VERIFY_DATA))
		hints->tx_attr->op_flags |= FI_DELIVERY_COMPLETE;

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->domain_attr->mr_mode = opts.mr_mode;

	ret = run();

	ft_free_res();
	return -ret;
}
