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
 * This program tests the functionality of RDM endpoint in the case
 * that a persistent server does ping-pong with multiple clients that
 * come and leave in sequence. The client connects to a server, sends
 * ping-pong, disconnects with the server by cleaning all fabric
 * resources, and repeats.
 * If the `-R` option is specified, it will re-use the first client's 
 * address for the subsequent clients by setting the src_addr for
 * endpoints 2..n to the output of fi_getname() of the first client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <shared.h>
#include <rdma/fi_cm.h>

static int run_pingpong(void)
{
	int ret, i;

	fprintf(stdout, "Start ping-pong.\n");
	for (i = 0; i < opts.iterations; i++) {
		if (opts.dst_addr) {
			ret = ft_tx(ep, remote_fi_addr, opts.transfer_size, &tx_ctx);
			if (ret) {
				FT_PRINTERR("ft_tx", -ret);
				return ret;
			}
			ret = ft_rx(ep, opts.transfer_size);
			if (ret) {
				FT_PRINTERR("ft_rx", -ret);
				return ret;
			}
		} else {
			ret = ft_rx(ep, opts.transfer_size);
			if (ret) {
				FT_PRINTERR("ft_rx", -ret);
				return ret;
			}
			ret = ft_tx(ep, remote_fi_addr, opts.transfer_size, &tx_ctx);
			if (ret) {
				FT_PRINTERR("ft_tx", -ret);
				return ret;
			}
		}
	}

	fprintf(stdout, "Ping-pong succeeds.\n");
	return 0;
}

static int run_server(void)
{
	int nconn, ret;

	ret = ft_init_fabric();
	if (ret) {
		FT_PRINTERR("ft_init_fabric", -ret);
		return ret;
	}

	nconn = opts.num_connections;

	while (nconn) {
		ret = run_pingpong();
		if (ret) {
			FT_PRINTERR("run_pingpong", -ret);
			return ret;
		}
		if (--nconn) {
			ret = ft_init_av();
			if (ret) {
				FT_PRINTERR("ft_init_av", -ret);
				return ret;
			}
		}
	}
	return 0;
}

static int run_client(int client_id, bool address_reuse)
{
	static char name[1024];
	static size_t size = sizeof(name);
	int ret;

	ret = ft_init();
	if (ret) {
		FT_PRINTERR("ft_init", -ret);
		return ret;
	}

	ret = ft_init_oob();
	if (ret) {
		FT_PRINTERR("ft_init_oob", -ret);
		return ret;
	}

	if (!client_id && oob_sock >= 0) {
		ret = ft_sock_sync(oob_sock, 0);
		if (ret)
			return ret;
	}

	ret = ft_getinfo(hints, &fi);
	if (ret) {
		FT_PRINTERR("ft_getinfo", -ret);
		return ret;
	}

	ret = ft_open_fabric_res();
	if (ret) {
		FT_PRINTERR("ft_open_fabric_res", -ret);
		return ret;
	}

	if (client_id > 0 && address_reuse) {
		memcpy(fi->src_addr, name, size);
		fi->src_addrlen = size;
	}

	ret = ft_alloc_active_res(fi);
	if (ret) {
		FT_PRINTERR("ft_alloc_active_res", -ret);
		return ret;
	}

	ret = ft_enable_ep_recv();
	if (ret) {
		FT_PRINTERR("ft_enable_ep_recv", -ret);
		return ret;
	}

	ret = ft_init_av();
	if (ret) {
		FT_PRINTERR("ft_init_av", -ret);
		return ret;
	}

	if (client_id == 0) {
		ret = fi_getname(&ep->fid, name, &size);
		if (ret) {
			FT_PRINTERR("fi_getname", -ret);
			return ret;
		}
	}

	return run_pingpong();
}

static void print_opts_usage(char *name, char *desc)
{
	ft_usage(name, desc);
	/* rdm_multi_client test op type */
	FT_PRINT_OPTS_USAGE("-R", "Reuse the address of the first client for subsequent clients");
}

int main(int argc, char **argv)
{
	int op, ret, i;
	struct fi_info *save;
	bool address_reuse = false;

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SIZE;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, "URh" ADDR_OPTS INFO_OPTS CS_OPTS)) != -1) {
		switch (op) {
		default:
			ft_parse_addr_opts(op, optarg, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			ft_parsecsopts(op, optarg, &opts);
			break;
		case 'U':
			hints->tx_attr->op_flags |= FI_DELIVERY_COMPLETE;
			break;
		case 'R':
			address_reuse = true;
			break;
		case '?':
		case 'h':
			print_opts_usage(argv[0], "RDM multi-client test");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG;
	hints->mode = FI_CONTEXT;
	hints->domain_attr->mr_mode = opts.mr_mode;
	hints->addr_format = opts.address_format;

	if (opts.dst_addr) {
		for (i = 0; i < opts.num_connections; i++) {
			printf("Starting client: %d\n", i);
			ret = run_client(i, address_reuse);
			if (ret) {
				FT_PRINTERR("run_client", -ret);
				goto out;
			}
			// Reuse hints for each iteration without using fi_dupinfo
			// because that would complicate memory ownership between the
			// application and the library, which Windows doesn't like.
			save = hints;
			hints = NULL;
			ft_free_res();
			hints = save;
		}
	} else {
		ret = run_server();
		if (ret)
			FT_PRINTERR("run_server", -ret);
	}
out:
	ft_free_res();
	return ft_exit_code(ret);
}
