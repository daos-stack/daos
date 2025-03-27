/*
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
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
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <rdma/fi_tagged.h>
#include "shared.h"

int use_sendmsg;
uint64_t flag = 0;

static int send_msg(int sendmsg, size_t size)
{
	int ret;
	ft_tag = 0xabcd;

	if (ft_check_opts(FT_OPT_VERIFY_DATA)) {
		ret = ft_fill_buf(tx_buf, size);
		if (ret)
			return ret;
	}

	if (sendmsg) {
		ret = ft_sendmsg(ep, remote_fi_addr, size, &tx_ctx, flag);
		if (ret)
			return ret;
	} else {
		ret = ft_post_tx(ep, remote_fi_addr, size, NO_CQ_DATA, &tx_ctx);
		if (ret) {
			FT_PRINTERR("ft_post_rx", ret);
			return ret;
		}
	}
	if (flag & FI_INJECT) {
		memset(tx_buf, 0xb, size);
		ret = ft_cq_read_verify(txcq, &tx_ctx);
		if (ret) {
			FT_PRINTERR("ft_cq_read_verify", ret);
			return ret;
		}
		tx_cq_cntr++;
	} else if (flag & FI_INJECT_COMPLETE) {
		ret = ft_cq_read_verify(txcq, &tx_ctx);
		if (ret) {
			FT_PRINTERR("ft_cq_read_verify", ret);
			return ret;
		}
		tx_cq_cntr++;
		memset(tx_buf, 0xb, size);
	}

	return 0;
}

static int receive_msg(size_t size)
{
	int ret;
	struct fi_context inj_ctx;
	ft_tag = 0xabcd;

	ret = ft_post_rx(ep, size, &inj_ctx);
	if (ret) {
		FT_PRINTERR("ft_post_rx", ret);
		return ret;
	}

	ret = ft_cq_read_verify(rxcq, &inj_ctx);
	if (ret) {
		FT_PRINTERR("ft_cq_read_verify", ret);
		return ret;
	}
	rx_cq_cntr++;

	if (ft_check_opts(FT_OPT_VERIFY_DATA)) {
		ret = ft_check_buf(rx_buf, size);
		if (ret)
			return ret;
	}

	return 0;
}

static int run_test(void)
{
	int ret = 0, i;

	if (!use_sendmsg)
		hints->tx_attr->op_flags |= flag;

	ret = ft_init_fabric();
	if (ret)
		return ret;

	if (flag & FI_INJECT && opts.transfer_size > hints->tx_attr->inject_size)
		opts.transfer_size = hints->tx_attr->inject_size;

	fprintf(stdout, "Start testing %s\n", fi_tostr(&flag,
		FI_TYPE_OP_FLAGS));
	for (i = 0; i < opts.iterations; i++) {
		if (opts.dst_addr) {
			ret = send_msg(use_sendmsg, opts.transfer_size);
			if (ret)
				return ret;
		} else {
			ret = receive_msg(opts.transfer_size);
			if (ret)
				return ret;
		}
	}
	fprintf(stdout, "GOOD: Completed %s Testing\n", fi_tostr(&flag,
		FI_TYPE_OP_FLAGS));

	return 0;
}

int main(int argc, char **argv)
{
	int op;
	int ret = 0;

	opts = INIT_OPTS;
	use_sendmsg = 0;
	opts.iterations = 1;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "NvhA:" CS_OPTS ADDR_OPTS INFO_OPTS)) != -1) {
		switch (op) {
		default:
			ft_parse_addr_opts(op, optarg, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			ft_parsecsopts(op, optarg, &opts);
			break;
		case 'N':
			use_sendmsg = 1;
			break;
		case 'v':
			opts.options |= FT_OPT_VERIFY_DATA;
			break;
		case 'A':
			if (!strncasecmp("inj_complete", optarg, 12)) {
				flag = FI_INJECT_COMPLETE;
			} else if (!strncasecmp("inject", optarg, 6)) {
				flag = FI_INJECT;
			} else {
				printf ("Unsupported flag\n");
				return EXIT_FAILURE;
			}
			break;
		case '?':
		case 'h':
			ft_csusage(argv[0], "inject completion functional test");
			FT_PRINT_OPTS_USAGE("-N", "enable testing with fi_sendmsg");
			FT_PRINT_OPTS_USAGE("-v", "Enable DataCheck testing");
			FT_PRINT_OPTS_USAGE("-A", "Enable flag testing. Options: inject, inj_complete");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->mode = FI_CONTEXT;
	hints->caps = FI_TAGGED;
	hints->domain_attr->resource_mgmt = FI_RM_ENABLED;
	hints->domain_attr->mr_mode = opts.mr_mode;
	hints->addr_format = opts.address_format;

	ret = run_test();

	ft_free_res();
	return ft_exit_code(ret);
}
