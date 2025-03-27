/*
 * Copyright (c) 2021, Amazon.com, Inc.  All rights reserved.
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
 *
 * This test resets RNR retry counter to 0 via fi_setopt, and check if
 * queue/re-send logic works correctly for different packet types. To run
 * the test, one needs to use `-c` option to specify the category of packet types
 *
 *    The tests against all different packet types can be running with runfabtests.sh.
 *    The relationship between each test and the corresponding packet type it checks against is:
 *    "fi_efa_rnr_queue_resend -c 0 -S 1048576" - CTS, DATA / EOR(RDMA Read)
 *    "fi_efa_rnr_queue_resend -c 0 -o read -S 4" - READRSP
 *    "fi_efa_rnr_queue_resend -c 0 -A read -S 4" - ATOMRSP
 *    "fi_efa_rnr_queue_resend -c 0 -U -S 4" - RECEIPT
 *    "fi_efa_rnr_queue_resend -c 1 -S 4" - EAGER_MSGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -T -S 4" - EAGER_TAGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -S 16384" - MEDIUM_MSGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -T -S 16384" - MEDIUM_TAGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -S 1048576" - LONGCTS_MSGRTM / LONGREAD_MSGRTM (RDMA Read)
 *    "fi_efa_rnr_queue_resend -c 1 -T -S 1048576" - LONGCTS_TAGRTM / LONGREAD_TAGRTM (RDMA Read)
 *    "fi_efa_rnr_queue_resend -c 1 -o write -S 4" - EAGER_RTW
 *    "fi_efa_rnr_queue_resend -c 1 -o write -S 1048576" - LONGCTS_RTW / LONGREAD_RTW (RDMA Read)
 *    "fi_efa_rnr_queue_resend -c 1 -o read -S 4" - SHORT_RTR
 *    "fi_efa_rnr_queue_resend -c 1 -o read -S 1048576" - LONGCTS_RTR
 *    "fi_efa_rnr_queue_resend -c 1 -A write -S 4" - WRITE_RTA
 *    "fi_efa_rnr_queue_resend -c 1 -A read -S 4" - FETCH_RTA
 *    "fi_efa_rnr_queue_resend -c 1 -A cswap -S 4" - COMPARE_RTA
 *    "fi_efa_rnr_queue_resend -c 1 -U -S 4" - DC_EAGER_MSGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -T -U -S 4" - DC_EAGER_TAGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -U -S 16384" - DC_MEDIUM_MSGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -T -U -S 16384" - DC_MEDIUM_TAGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -U -S 1048576" - DC_LONGCTS_MSGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -T -U -S 1048576" - DC_LONGCTS_TAGRTM
 *    "fi_efa_rnr_queue_resend -c 1 -o write -U -S 4" - DC_EAGER_RTW
 *    "fi_efa_rnr_queue_resend -c 1 -o write -U -S 1048576" - DC_LONGCTS_RTW
 *    "fi_efa_rnr_queue_resend -c 1 -A write -U -S 4" - DC_WRITE_RTA
 *    "fi_efa_rnr_queue_resend -c 1 -o writedata -S 4" - WRITEDATA
 *
 * In addition, HANDSHAKE packet's queue/re-send can be easily triggered during
 * initial ft_sync's ft_rx() on the server side, as the client does not
 * pre-post internal rx buffers until it polls completion in ft_sync's
 * ft_tx(). All of the above tests have the sync procedure, so we do not
 * add a dedicated test to check HANDSHAKE packet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <rdma/fi_atomic.h>
#include <shared.h>
#include "efa_rnr_shared.h"

int global_expected_rnr_error = 1;

/* global atomic resources */
void *result = NULL;
void *compare = NULL;
struct fid_mr *mr_result = NULL;
struct fid_mr *mr_compare = NULL;

static int alloc_atomic_res()
{
	int ret;
	int mr_local = !!(fi->domain_attr->mr_mode & FI_MR_LOCAL);

	result = malloc(buf_size);
	if (!result) {
		perror("malloc");
		return -1;
	}

	compare = malloc(buf_size);
	if (!compare) {
		perror("malloc");
		return -1;
	}

	// registers local data buffer that stores results
	ret = fi_mr_reg(domain, result, buf_size,
			(mr_local ? FI_READ : 0) | FI_REMOTE_WRITE, 0,
			0, 0, &mr_result, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", -ret);
		return ret;
	}

	// registers local data buffer that contains comparison data
	ret = fi_mr_reg(domain, compare, buf_size,
			(mr_local ? FI_WRITE : 0)  | FI_REMOTE_READ, 0,
			0, 0, &mr_compare, NULL);
	if (ret) {
		FT_PRINTERR("fi_mr_reg", ret);
		return ret;
	}

	return 0;
}

static void free_atomic_res()
{
	if (mr_result) {
		FT_CLOSE_FID(mr_result);
		mr_result = NULL;
	}
	if (mr_compare) {
		FT_CLOSE_FID(mr_compare);
		mr_compare = NULL;
	}

	if (result) {
		free(result);
		result = NULL;
	}
	if (compare) {
		free(compare);
		compare = NULL;
	}
}

static int trigger_rnr_queue_resend(enum fi_op atomic_op, void *result, void *compare,
				    struct fid_mr *mr_result, struct fid_mr *mr_compare)
{
	int i, ret;
	struct fi_context fi_ctx_atomic;

	if (opts.rma_op) {
		for (i = 0; i < global_expected_rnr_error; i++) {
			switch (opts.rma_op) {
			case FT_RMA_WRITE:
			case FT_RMA_WRITEDATA:
				ret = ft_post_rma(opts.rma_op, tx_buf, opts.transfer_size,
						&remote, &tx_ctx_arr[fi->rx_attr->size].context);
				break;
			case FT_RMA_READ:
				ret = ft_post_rma(FT_RMA_READ, rx_buf, opts.transfer_size,
						&remote, &tx_ctx_arr[fi->rx_attr->size].context);
				break;
			default:
				FT_ERR("Unknown RMA op type\n");
				return EXIT_FAILURE;
			}
			if (ret)
				return ret;
		}
	} else if (atomic_op) {
		for (i = 0; i < global_expected_rnr_error; i++) {
			switch(atomic_op) {
			case FI_ATOMIC_WRITE:
				ret = ft_post_atomic(FT_ATOMIC_BASE, ep, NULL, NULL, NULL, NULL,
			                        &remote, FI_INT32, FI_ATOMIC_WRITE, &fi_ctx_atomic);
				break;
			case FI_ATOMIC_READ:
				ret = ft_post_atomic(FT_ATOMIC_FETCH, ep, NULL, NULL, result,
						fi_mr_desc(mr_result), &remote, FI_INT32,
						FI_ATOMIC_READ, &fi_ctx_atomic);
				break;
			case FI_CSWAP:
				ret = ft_post_atomic(FT_ATOMIC_COMPARE, ep,
						compare, fi_mr_desc(mr_compare),
						result, fi_mr_desc(mr_result), &remote, FI_INT32,
						FI_CSWAP, &fi_ctx_atomic);
				break;
			default:
				FT_ERR("Unknown atomic op type\n");
				return EXIT_FAILURE;
			}
			if (ret)
				return ret;
		}
	} else {
		for (i = 0; i < global_expected_rnr_error; i++) {
			ret = ft_post_tx(ep, remote_fi_addr, opts.transfer_size, NO_CQ_DATA, &tx_ctx);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int rnr_queue_resend_test(int req_pkt, enum fi_op atomic_op)
{
	int ret, i;

	/*
	 * The handshake procedure between server and client will happen in
	 * either ft_sync() or ft_exchange_key(), which is before the real
	 * RNR triggering procedure.
	 */
	if (opts.rma_op || atomic_op) {
		ret = ft_exchange_keys(&remote);
		if (ret) {
			FT_PRINTERR("ft_exchange_keys()", -ret);
			return ret;
		}
	} else {
		ret = ft_sync();
		if (ret) {
			FT_PRINTERR("ft_sync()", -ret);
			return ret;
		}
	}
	/*
	 * Wait 1s here to ensure the server receives EFA_RDM_RECEIPT_PKT if delivery_complete
	 * is requested, before the client starts sending (fi->rx_attr->size) packets.
	 * Without this, the server might already receive multiple packets, before
	 * receiving EFA_RDM_RECEIPT_PKT, which causes the server's internally
	 * pre-posted rx buffer not getting run out.
	 */
	sleep(1);

	/* Real RNR triggering procedure */
	if (opts.dst_addr) {
		/*
		 * Client first posts fi->rx_attr->size sends to server, in order to run out
		 * of server's pre-posted internal rx buffers. This is the common step, no
		 * matter we check REQ or non-REQ packet types.
		 */
		for (i = 0; i < fi->rx_attr->size; i++) {
			ret = ft_post_tx(ep, remote_fi_addr, 32, NO_CQ_DATA, &tx_ctx);
			if (ret)
				return ret;
		}
		/*
		 * To check REQ packet types, the client will post send/rma/atomic operation by
		 * calling trigger_rnr_queue_resend(), which will post a REQ packet to the server.
		 * Since server side has already run out of its pre-posted internal rx buffers,
		 * the post of this REQ packet on client side will get RNR, and be queued/re-sent.
		 *
		 * For example, the client can send a medium message to the server by posting
		 * a MEDIUM_MSGRTM packet. Because the server has run of its pre-posted rx
		 * buffers, and is in sleep stage, the post of MEDIUM_MSGRTM packet on client side
		 * will get RNR, and be queued/re-sent.
		 */
		if (req_pkt) {
			ret = trigger_rnr_queue_resend(atomic_op, result, compare, mr_result, mr_compare);
			if (ret)
				return ret;
		} else if (!opts.rma_op && !atomic_op) {
			for (i = 0; i < global_expected_rnr_error; i++) {
				ret = ft_rx(ep, opts.transfer_size);
				if (ret)
					return ret;
			}
		}
		ret = ft_get_tx_comp(tx_seq);
		if (ret)
			return ret;
	} else {
		/*
		 * To check non-REQ packet types, the server first posts send/rma/atomic operation,
		 * which triggers the client side posting a non-REQ packet types (e.g., CTS, READRSP, ATOMRSP)
		 * back. Since server side has already run out of its pre-posted internal rx buffers,
		 * the post of the non-REQ packet on client side will get RNR, and be queued/re-sent.
		 *
		 * For example, server can send a long message to the server by posting a LONGCTS_MSGRTM
		 * packet. When client receives this  LONGCTS_MSGRTM packet, it will post a CTS packet back.
		 * Since the server has already run out of its internal rx buffers, the post of CTS
		 * packet will trigger RNR, and be queued/re-sent on the client side.
		 */
		if (!req_pkt) {
			sleep(3);
			ret = trigger_rnr_queue_resend(atomic_op, result, compare, mr_result, mr_compare);
			if (ret)
				return ret;
		}
		printf("Sleeping 3 seconds to trigger RNR on the client side\n");
		sleep(3);
		for (i = 0; i < fi->rx_attr->size; i++) {
			ret = ft_rx(ep, 32);
			if (ret)
				return ret;
		}
		if (!req_pkt) {
			ret = ft_get_tx_comp(tx_seq);
			if (ret)
				return ret;
		} else if (!opts.rma_op && !atomic_op) {
			for (i = 0; i < global_expected_rnr_error; i++) {
				ret = ft_rx(ep, opts.transfer_size);
				if (ret)
					return ret;
			}
		}
	}

	ret = ft_sync();
	if (ret) {
		FT_PRINTERR("ft_sync()", -ret);
		return ret;
	}

	return ret;
}


static int run(int req_pkt, enum fi_op atomic_op)
{
	int ret;

	ret = ft_efa_rnr_init_fabric();
	if (ret) {
		FT_PRINTERR("ft_efa_rnr_init_fabric", -ret);
		goto out;
	}

	ret = alloc_atomic_res();
	if (ret) {
		FT_PRINTERR("alloc_atomic_res()", -ret);
		goto out;
	}

	ret = rnr_queue_resend_test(req_pkt, atomic_op);
	if (ret) {
		FT_PRINTERR("rnr_queue_resend_test", -ret);
		goto out;
	}

	ret = ft_close_oob();
	if (ret) {
		FT_PRINTERR("ft_close_oob", -ret);
		goto out;
	}

out:
	free_atomic_res();
	ft_free_res();
	return ret;
}

static void print_opts_usage(char *name, char *desc)
{
	ft_usage(name, desc);
	/* efa_rnr_queue_resend test usage */
	FT_PRINT_OPTS_USAGE("-c", "Category of Packet type: 1(Request)/0(non-Request), default: 1)");
	FT_PRINT_OPTS_USAGE("-A <op>", "atomic op type: write|read|cswap");
	FT_PRINT_OPTS_USAGE("-T", "Run test with tagged message");
}

int main(int argc, char **argv)
{
	int ret, op, req_pkt;
	enum fi_op atomic_op;
	size_t size_to_check_data_pkt;

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SIZE;
	opts.rma_op = 0;
	atomic_op = FI_MIN;
	req_pkt = 1;
	size_to_check_data_pkt = 131072;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "c:A:TUh" ADDR_OPTS INFO_OPTS CS_OPTS API_OPTS)) != -1) {
		switch (op) {
		default:
			ft_parse_addr_opts(op, optarg, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			ft_parsecsopts(op, optarg, &opts);
			if (!atomic_op) {
				ret = ft_parse_api_opts(op, optarg, hints, &opts);
				if (ret)
					return ret;
			}
			break;
		case 'c':
			req_pkt = atoi(optarg);
			if (req_pkt != 0 && req_pkt != 1) {
				fprintf(stdout, "Invalid value for category of packet type.\n");
				return EXIT_FAILURE;
			}
			break;
		case 'A':
			if (!opts.rma_op) {
				if (!strncasecmp("write", optarg, 5)) {
					atomic_op = FI_ATOMIC_WRITE;
				} else if (!strncasecmp("read", optarg, 4)) {
					atomic_op = FI_ATOMIC_READ;
				} else if (!strncasecmp("cswap", optarg, 5)) {
					atomic_op = FI_CSWAP;
				} else {
					fprintf(stdout, "Unsupported atomic op.\n");
					return EXIT_FAILURE;
				}
			}
			break;
		case 'T':
			hints->caps |= FI_TAGGED;
			break;
		case 'U':
			hints->tx_attr->op_flags |= FI_DELIVERY_COMPLETE;
			ret = setenv("FI_EFA_RX_SIZE", "32", 1);
			if (ret) {
				fprintf(stdout, "Failed to reset number of pre-posted rx buffer to 32\n");
				return EXIT_FAILURE;
			}
			break;
		case '?':
		case 'h':
			print_opts_usage(argv[0], "RDM RNR packet queue/re-send test");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps |= FI_MSG | FI_RMA | FI_ATOMICS;
	hints->mode |= FI_CONTEXT;
	hints->domain_attr->mr_mode = opts.mr_mode;

	/* FI_RM_ENABLED to is required for queue/resend logic to happen in RNR case */
	hints->domain_attr->resource_mgmt = FI_RM_ENABLED;
	/*
	 * RNR error is generated from EFA device, so disable shm transfer by
	 * setting FI_REMOTE_COMM and unsetting FI_LOCAL_COMM in order to ensure
	 * EFA device is being used when running this test on a single node.
	 */
	ft_efa_rnr_disable_hints_shm();

	/*
	 * When the server posts EFA_RDM_LONGCTS_MSGRTM_PKT in order to trigger EFA_RDM_CTS_PKT with
	 * RNR, also reset number of pre-posted rx buffer to 1, so we can easily check for
	 * EFA_RDM_CTSDATA_PKT in the same test.
	 */
	if (!req_pkt && !atomic_op && !opts.rma_op && opts.transfer_size >= size_to_check_data_pkt) {
		ret = setenv("FI_EFA_RX_SIZE", "1", 1);
		if (ret) {
			fprintf(stdout, "Failed to reset number of pre-posted rx buffer to 1\n");
			return EXIT_FAILURE;
		}
	}

	ret = run(req_pkt, atomic_op);
	if (ret)
		FT_PRINTERR("run", -ret);

	return ft_exit_code(ret);
}
