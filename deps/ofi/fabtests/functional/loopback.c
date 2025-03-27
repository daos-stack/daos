/*
 * Copyright (c) 2021 Intel Corporation.  All rights reserved.
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
#include <unistd.h>

#include <shared.h>


static int run(void)
{
	int ret;

	ret = ft_getinfo(hints, &fi);
	if (ret)
		return ret;

	ret = ft_open_fabric_res();
	if (ret)
		return ret;

	ret = ft_alloc_active_res(fi);
	if (ret)
		return ret;

	ret = ft_enable_ep_recv();
	if (ret)
		return ret;

	opts.dst_addr = fi->src_addr;
	fi->dest_addr = fi->src_addr;
	fi->dest_addrlen = fi->src_addrlen;

	ret = ft_init_av();
	if (ret)
		goto out;

	ret = ft_send_greeting(ep);
	if (ret)
		goto out;

	ret = ft_recv_greeting(ep);
	if (ret)
		goto out;

out:
	fi->dest_addr = NULL;
	fi->dest_addrlen = 0;
	return ret;
}

int main(int argc, char **argv)
{
	int op, ret;

	opts = INIT_OPTS;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	opts.src_addr = "127.0.0.1";
	hints->caps = FI_LOCAL_COMM | FI_MSG | FI_TAGGED;
	hints->ep_attr->type = FI_EP_RDM;
	hints->mode = FI_CONTEXT;

	while ((op = getopt(argc, argv, "h" INFO_OPTS)) != -1) {
		switch (op) {
		default:
			ft_parseinfo(op, optarg, hints, &opts);
			break;
		case '?':
		case 'h':
			ft_usage(argv[0], "A loopback communication test.");
			return EXIT_FAILURE;
		}
	}

	hints->domain_attr->mr_mode = opts.mr_mode;
	ret = run();

	ft_free_res();
	return ft_exit_code(ret);
}
