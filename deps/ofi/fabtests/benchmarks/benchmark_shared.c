/*
 * Copyright (c) 2015-2017 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013-2015 Intel Corporation.  All rights reserved.
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

#include <rdma/fi_errno.h>

#include "shared.h"
#include "benchmark_shared.h"

/* when the -j option is set, user supplied inject_size must be honored,
 * even if the provider may return a larger value. This flag is used to
 * distinguish between the '-j 0' option and no '-j' option at all. For
 * both cases hints->tx_attr->inject_size is 0.
 */
static int inject_size_set;

/* When performing RMA with validation, READ needs to ensure it deconflicts
 * it's memory access with the messages sent by ft_sync().  Do this by
 * offsetting all RMA operations away from the beginning of the buffer and
 * leave ft_sync to operate in that region.
 */
static int offset_rma_start = 0;

void ft_parse_benchmark_opts(int op, char *optarg)
{
	switch (op) {
	case 'v':
		opts.options |= FT_OPT_VERIFY_DATA;
		break;
	case 'k':
		ft_force_prefix(hints, &opts);
		break;
	case 'j':
		hints->tx_attr->inject_size = atoi(optarg);
		inject_size_set = 1;
		break;
	case 'W':
		opts.window_size = atoi(optarg);
		break;
	default:
		break;
	}
}

void ft_benchmark_usage(void)
{
	FT_PRINT_OPTS_USAGE("-v", "enables data_integrity checks");
	FT_PRINT_OPTS_USAGE("-k", "force prefix mode");
	FT_PRINT_OPTS_USAGE("-j", "maximum inject message size");
	FT_PRINT_OPTS_USAGE("-W", "window size* (for bandwidth tests)\n\n"
			"* The following condition is required to have at least "
			"one window\nsize # of messsages to be sent: "
			"# of iterations > window size");
}

int pingpong(void)
{
	int ret, i, inject_size;

	inject_size = inject_size_set ?
			hints->tx_attr->inject_size : fi->tx_attr->inject_size;

	if (opts.options & FT_OPT_ENABLE_HMEM)
		inject_size = 0;

	ret = ft_sync();
	if (ret)
		return ret;

	if (opts.dst_addr) {
		for (i = 0; i < opts.iterations + opts.warmup_iterations; i++) {
			if (i == opts.warmup_iterations)
				ft_start();

			if (opts.transfer_size <= inject_size)
				ret = ft_inject(ep, remote_fi_addr, opts.transfer_size);
			else
				ret = ft_tx(ep, remote_fi_addr, opts.transfer_size, &tx_ctx);
			if (ret)
				return ret;

			ret = ft_rx(ep, opts.transfer_size);
			if (ret)
				return ret;
		}
	} else {
		for (i = 0; i < opts.iterations + opts.warmup_iterations; i++) {
			if (i == opts.warmup_iterations)
				ft_start();

			ret = ft_rx(ep, opts.transfer_size);
			if (ret)
				return ret;

			if (opts.transfer_size <= inject_size)
				ret = ft_inject(ep, remote_fi_addr, opts.transfer_size);
			else
				ret = ft_tx(ep, remote_fi_addr, opts.transfer_size, &tx_ctx);
			if (ret)
				return ret;
		}
	}
	ft_stop();

	if (opts.machr)
		show_perf_mr(opts.transfer_size, opts.iterations, &start, &end, 2,
				opts.argc, opts.argv);
	else
		show_perf(NULL, opts.transfer_size, opts.iterations, &start, &end, 2);

	return 0;
}

int pingpong_rma(enum ft_rma_opcodes rma_op, struct fi_rma_iov *remote)
{
	int ret, i, inject_size;

	inject_size = inject_size_set ?
			hints->tx_attr->inject_size : fi->tx_attr->inject_size;

	if (ft_check_opts(FT_OPT_ENABLE_HMEM))
		inject_size = 0;

	/* for FT_OPT_VERIFY_DATA, we cannot use inject, as we require
	 * completions to indicate delivery has completed. */
	if (ft_check_opts(FT_OPT_VERIFY_DATA))
		inject_size = 0;

	if (opts.transfer_size == 0) {
		FT_ERR("Zero-sized transfers not supported");
		return EXIT_FAILURE;
	}

	/* Init rx_buf with invalid iteration number.
	 * This must be done before the sender sends any data.
	 */
	if (rma_op == FT_RMA_WRITE)
		*(rx_buf + opts.transfer_size - 1) = (char)-1;

	ret = ft_sync();
	if (ret)
		return ret;

	if (opts.dst_addr) {
		for (i = 0; i < opts.iterations + opts.warmup_iterations; i++) {

			if (i == opts.warmup_iterations)
				ft_start();

			if (rma_op == FT_RMA_WRITE)
				*(tx_buf + opts.transfer_size - 1) = (char)i;

			if (opts.transfer_size <= inject_size)
				ret = ft_inject_rma(rma_op, remote, ep,
						    remote_fi_addr,
						    opts.transfer_size);
			else
				ret = ft_tx_rma(rma_op, remote, ep, remote_fi_addr,
						opts.transfer_size, &tx_ctx);
			if (ret)
				return ret;

			ret = ft_rx_rma(i, rma_op, ep, opts.transfer_size);
			if (ret)
				return ret;
		}
	} else {
		for (i = 0; i < opts.iterations + opts.warmup_iterations; i++) {
			if (i == opts.warmup_iterations)
				ft_start();

			ret = ft_rx_rma(i, rma_op, ep, opts.transfer_size);
			if (ret)
				return ret;

			if (rma_op == FT_RMA_WRITE)
				*(tx_buf + opts.transfer_size - 1) = (char)i;

			if (opts.transfer_size <= inject_size)
				ret = ft_inject_rma(rma_op, remote, ep,
						remote_fi_addr,
						opts.transfer_size);
			else
				ret = ft_tx_rma(rma_op, remote, ep, remote_fi_addr,
						opts.transfer_size, &tx_ctx);
			if (ret)
				return ret;
		}
	}
	ft_stop();

	if (opts.machr)
		show_perf_mr(opts.transfer_size, opts.iterations, &start, &end, 2,
				opts.argc, opts.argv);
	else
		show_perf(NULL, opts.transfer_size, opts.iterations, &start, &end, 2);

	return 0;
}

static int bw_tx_comp()
{
	int ret;

	ret = ft_get_tx_comp(tx_seq);
	if (ret)
		return ret;
	return ft_rx(ep, FT_RMA_SYNC_MSG_BYTES);
}

static int bw_rx_comp(int window)
{
	int ret, i;

	/* rx_seq is always one ahead */
	ret = ft_get_rx_comp(rx_seq - 1);
	if (ret)
		return ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA)) {
		for (i = 0; i < window; i++) {
			ret = ft_check_buf((char *) rx_ctx_arr[i].buf +
					ft_rx_prefix_size(),
					opts.transfer_size);
			if (ret)
				return ret;
		}
	}

	return ft_tx(ep, remote_fi_addr, FT_RMA_SYNC_MSG_BYTES, &tx_ctx);
}

static int rma_bw_rx_comp()
{
	int ret;

	/* rx_seq is always one ahead */
	ret = ft_get_rx_comp(rx_seq - 1);
	if (ret)
		return ret;

	return ft_tx(ep, remote_fi_addr, FT_RMA_SYNC_MSG_BYTES, &tx_ctx);
}

int bandwidth(void)
{
	int ret, i, j, inject_size;

	inject_size = inject_size_set ?
			hints->tx_attr->inject_size : fi->tx_attr->inject_size;

	if (opts.options & FT_OPT_ENABLE_HMEM)
		inject_size = 0;

	ret = ft_sync();
	if (ret)
		return ret;

	/* The loop structured allows for the possibility that the sender
	 * immediately overruns the receiving side on the first transfer (or
	 * the entire window). This could result in exercising parts of the
	 * provider's implementation of FI_RM_ENABLED. For better or worse,
	 * some MPI-level benchmarks tend to use this type of loop for measuring
	 * bandwidth.  */

	if (opts.dst_addr) {
		for (i = j = 0; i < opts.iterations + opts.warmup_iterations; i++) {
			if (i == opts.warmup_iterations)
				ft_start();

			if (ft_check_opts(FT_OPT_VERIFY_DATA)) {
				ret = ft_fill_buf(tx_ctx_arr[j].buf,
						  opts.transfer_size);
				if (ret)
					return ret;
			}
			if (opts.transfer_size <= inject_size) {
				ret = ft_post_inject_buf(ep, remote_fi_addr,
						opts.transfer_size, NO_CQ_DATA,
						tx_ctx_arr[j].buf, tx_seq);
			} else {
				ret = ft_post_tx_buf(ep, remote_fi_addr,
						opts.transfer_size, NO_CQ_DATA,
						&tx_ctx_arr[j].context,
						tx_ctx_arr[j].buf,
						mr_desc, tx_seq);
			}
			if (ret)
				return ret;

			if (++j == opts.window_size) {
				ret = bw_tx_comp();
				if (ret)
					return ret;
				j = 0;
			}
		}
		ret = bw_tx_comp();
		if (ret)
			return ret;
	} else {
		for (i = j = 0; i < opts.iterations + opts.warmup_iterations; i++) {
			if (i == opts.warmup_iterations)
				ft_start();

			ret = ft_post_rx_buf(ep, opts.transfer_size,
					     &rx_ctx_arr[j].context,
					     rx_ctx_arr[j].buf, mr_desc,
					     ft_tag);
			if (ret)
				return ret;

			if (++j == opts.window_size) {
				ret = bw_rx_comp(j);
				if (ret)
					return ret;
				j = 0;
			}
		}
		ret = bw_rx_comp(j);
		if (ret)
			return ret;
	}
	ft_stop();

	if (opts.machr)
		show_perf_mr(opts.transfer_size, opts.iterations, &start, &end, 1,
				opts.argc, opts.argv);
	else
		show_perf(NULL, opts.transfer_size, opts.iterations, &start, &end, 1);

	return 0;
}

static int bw_rma_comp(enum ft_rma_opcodes rma_op, int num_completions)
{
	int ret;

	if (rma_op == FT_RMA_WRITEDATA) {
		/* for writedata, only the client sends,
		 * and only the server verifies. */
		if (opts.dst_addr)
			return bw_tx_comp();
		ret = rma_bw_rx_comp();
	} else {
		ret = ft_get_tx_comp(tx_seq);
	}

	if (ret)
		return ret;

	if (ft_check_opts(FT_OPT_VERIFY_DATA)) {
		if (rma_op == FT_RMA_WRITE)
			ft_sync();
		ret = ft_check_buf(rx_buf + offset_rma_start,
			           opts.transfer_size * num_completions);
	}

	return ret;
}

int bandwidth_rma(enum ft_rma_opcodes rma_op, struct fi_rma_iov *remote)
{
	int ret, i, j, inject_size;
	size_t offset;

	inject_size = inject_size_set ?
			hints->tx_attr->inject_size: fi->tx_attr->inject_size;

	if (ft_check_opts(FT_OPT_ENABLE_HMEM))
		inject_size = 0;

	/* for FT_OPT_VERIFY_DATA, we cannot use inject, as we require
	 * completions to indicate delivery has completed. */
	if (ft_check_opts(FT_OPT_VERIFY_DATA))
		inject_size = 0;

	ret = ft_sync();
	if (ret)
		return ret;

	offset_rma_start = FT_RMA_SYNC_MSG_BYTES +
			   MAX(ft_tx_prefix_size(), ft_rx_prefix_size());
	for (i = j = 0; i < opts.iterations + opts.warmup_iterations; i++) {
		if (i == opts.warmup_iterations)
			ft_start();
		if (j == 0) {
			offset = offset_rma_start;
			if (ft_check_opts(FT_OPT_VERIFY_DATA) && opts.transfer_size > 0) {
				ret = ft_fill_buf(tx_buf + offset_rma_start,
					opts.transfer_size * opts.window_size);
				if (ret)
					return ret;

				ret = ft_fill_buf(rx_buf + offset_rma_start + 1,
					opts.transfer_size * opts.window_size - 1);
				if (ret)
					return ret;

				ft_sync();
			}
		}
		switch (rma_op) {
		case FT_RMA_WRITE:
			if (opts.transfer_size <= inject_size) {
				ret = ft_post_rma_inject(FT_RMA_WRITE, tx_buf + offset,
						opts.transfer_size, remote);
			} else {
				ret = ft_post_rma(FT_RMA_WRITE, tx_buf + offset,
						opts.transfer_size, remote,
						&tx_ctx_arr[j].context);
			}
			break;
		case FT_RMA_WRITEDATA:
			if (!opts.dst_addr) {
				if (fi->rx_attr->mode & FI_RX_CQ_DATA)
					ret = ft_post_rx(ep, 0, &rx_ctx_arr[j].context);
				else
					/* Just increment the seq # instead of
					 * posting recv so that we wait for
					 * remote write completion on the next
					 * iteration */
					rx_seq++;

			} else {
				if (opts.transfer_size <= inject_size) {
					ret = ft_post_rma_inject(FT_RMA_WRITEDATA,
							tx_buf + offset,
							opts.transfer_size,
							remote);
				} else {
					ret = ft_post_rma(FT_RMA_WRITEDATA,
							tx_buf + offset,
							opts.transfer_size,
							remote,	&tx_ctx_arr[j].context);
				}
			}
			break;
		case FT_RMA_READ:
			ret = ft_post_rma(FT_RMA_READ, rx_buf + offset, opts.transfer_size,
					remote,	&tx_ctx_arr[j].context);
			break;
		default:
			FT_ERR("Unknown RMA op type\n");
			return EXIT_FAILURE;
		}
		if (ret)
			return ret;

		if (++j == opts.window_size) {
			ret = bw_rma_comp(rma_op, j);
			if (ret)
				return ret;
			j = 0;
		}
		offset += opts.transfer_size;
	}
	ret = bw_rma_comp(rma_op, j);
	if (ret)
		return ret;
	ft_stop();

	if (opts.machr)
		show_perf_mr(opts.transfer_size, opts.iterations, &start, &end,	1,
				opts.argc, opts.argv);
	else
		show_perf(NULL, opts.transfer_size, opts.iterations, &start, &end, 1);
	return 0;
}
