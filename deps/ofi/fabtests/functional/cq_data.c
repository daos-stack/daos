/*
 * Copyright (c) 2013-2015 Intel Corporation.  All rights reserved.
 * Copyright (c) 2016, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>

#include <shared.h>

static int run_test()
{
	int ret;
	size_t size = 1000;
	struct fi_cq_data_entry comp = {0};
	struct fi_rma_iov remote;
	uint64_t mask = UINT64_MAX;

	if (fi->domain_attr->cq_data_size < sizeof(uint64_t))
		mask ^= mask << (fi->domain_attr->cq_data_size * 8);

	if (opts.cqdata_op == FT_CQDATA_WRITEDATA) {
		ret = ft_exchange_keys(&remote);
		if (ret)
			return ret;
	}

	if (opts.dst_addr) {
		if (opts.cqdata_op == FT_CQDATA_SENDDATA) {
			fprintf(stdout,
				"Posting send with CQ data: 0x%" PRIx64 "\n",
				remote_cq_data);
			ret = ft_post_tx(ep, remote_fi_addr, size, remote_cq_data, &tx_ctx);
		} else  if (opts.cqdata_op == FT_CQDATA_WRITEDATA) {
			fprintf(stdout,
				"Posting write with CQ data: 0x%" PRIx64 "\n",
				ft_init_cq_data(fi));

			ret = ft_post_rma(FT_RMA_WRITEDATA, tx_buf, size, &remote, &tx_ctx);
		} else {
			fprintf(stdout, "invalid cqdata_op: %d\n", opts.cqdata_op);
			ret = -FI_EINVAL;
		}
		if (ret)
			return ret;

		ret = ft_get_tx_comp(tx_seq);
		fprintf(stdout, "Done\n");
	} else {
		fprintf(stdout, "Waiting for CQ data from client\n");
		ret = fi_cq_read(rxcq, &comp, 1);
		while (ret == 0 || ret == -FI_EAGAIN)
			ret = fi_cq_read(rxcq, &comp, 1);

		if (ret < 0) {
			if (ret == -FI_EAVAIL) {
				ret = ft_cq_readerr(rxcq);
			} else {
				FT_PRINTERR("fi_cq_sread", ret);
			}
			return ret;
		}

		if (comp.flags & FI_REMOTE_CQ_DATA) {
			if ((comp.data & mask) == (remote_cq_data & mask)) {
				fprintf(stdout, "remote_cq_data: success\n");
				ret = 0;
			} else {
				fprintf(stdout, "error, Expected data:0x%" PRIx64
					", Received data:0x%" PRIx64 "\n",
					remote_cq_data, comp.data);
				ret = -FI_EIO;
			}

			if (comp.len == size) {
				fprintf(stdout, "fi_cq_data_entry.len verify: success\n");
				ret = 0;
			} else {
				fprintf(stdout, "error, Expected len:%zu, Received len:%zu\n",
					size, comp.len);
				ret = -FI_EIO;
			}
		} else {
			fprintf(stdout, "error, CQ data flag not set\n");
			ret = -FI_EBADFLAGS;
		}
	}

	return ret;
}

static int run(void)
{
	int ret;

	if (hints->ep_attr->type == FI_EP_MSG)
		ret = ft_init_fabric_cm();
	else
		ret = ft_init_fabric();
	if (ret)
		return ret;

	ret = run_test();

	fi_shutdown(ep, 0);
	return ret;
}

int main(int argc, char **argv)
{
	int op, ret;

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SIZE;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "h" ADDR_OPTS API_OPTS INFO_OPTS)) != -1) {
		switch (op) {
		default:
			ft_parse_addr_opts(op, optarg, &opts);
			ft_parse_api_opts(op, optarg, hints, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			break;
		case '?':
		case 'h':
			ft_usage(argv[0], "A client-server example that transfers CQ data.\n");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->domain_attr->cq_data_size = 4;  /* required minimum */
	hints->mode |= FI_CONTEXT | FI_RX_CQ_DATA;

	hints->caps = FI_MSG;
	if (opts.cqdata_op == FT_CQDATA_WRITEDATA)
		hints->caps |= FI_RMA;

	hints->domain_attr->mr_mode = opts.mr_mode;
	hints->addr_format = opts.address_format;

	cq_attr.format = FI_CQ_FORMAT_DATA;

	ret = run();

	ft_free_res();
	return ft_exit_code(ret);
}
