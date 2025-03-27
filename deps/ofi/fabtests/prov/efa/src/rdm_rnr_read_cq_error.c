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
 * This test resets RNR retry counter to 0 via fi_setopt, and test if
 * an RNR error CQ entry can be read.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <shared.h>
#include "efa_rnr_shared.h"


static int rnr_read_cq_error(void)
{
	int total_send, expected_rnr_error;
	int ret, i, cnt, rnr_flag;
	const char *prov_errmsg;

	expected_rnr_error = fi->rx_attr->size;
	rnr_flag = 0;
	/*
	 * In order for the sender to get RNR error, we need to first consume
	 * all pre-posted receive buffer (in efa provider, fi->rx_attr->size
	 * receiving buffer are pre-posted) on the receiver side, the subsequent
	 * sends (expected_rnr_error) will then get RNR errors.
	 */
	total_send = fi->rx_attr->size + expected_rnr_error;

	for (i = 0; i < total_send; i++) {
		do {
			ret = fi_send(ep, tx_buf, 32, mr_desc, remote_fi_addr, &tx_ctx);
			if (ret == -FI_EAGAIN) {
				(void) fi_cq_read(txcq, NULL, 0);
				continue;
			}

			if (ret < 0) {
				FT_PRINTERR("fi_send", -ret);
				return ret;
			}
		} while (ret == -FI_EAGAIN);
	}

	cnt = total_send;
	do {
		struct fi_cq_data_entry comp = {0};
		struct fi_cq_err_entry comp_err = {0};

		ret = fi_cq_read(txcq, &comp, 1);
		if (ret == 1) {
			cnt--;
		} else if (ret == -FI_EAVAIL) {
			ret = fi_cq_readerr(txcq, &comp_err, FI_SEND);
			if (ret < 0 && ret != -FI_EAGAIN) {
				FT_PRINTERR("fi_cq_readerr", -ret);
				return ret;
			} else if (ret == 1) {
				cnt--;
				if (comp_err.err == FI_ENORX) {
					rnr_flag = 1;
					printf("Got RNR error CQ entry as expected: %d, %s\n",
						comp_err.err, fi_strerror(comp_err.err));
					prov_errmsg = fi_cq_strerror(txcq, comp_err.prov_errno,
								     comp_err.err_data,
								     comp_err.buf,
								     comp_err.len);
					if (strstr(prov_errmsg, "Destination resource not ready") == NULL) {
						printf("Got unexpected provider error message.\n");
						printf("    Expected error message to have \"Destination resource not ready\" in it\n");
						printf("    Got: %s\n", prov_errmsg);
						return -FI_EINVAL;
					}
				} else {
					printf("Got non-RNR error CQ entry: %d, %s\n",
						comp_err.err, fi_strerror(comp_err.err));
					return comp_err.err;
				}
			}
		} else if (ret < 0 && ret != -FI_EAGAIN) {
			FT_PRINTERR("fi_cq_read", -ret);
			return ret;
		}
	} while (cnt);

	return (rnr_flag) ? 0 : -FI_EINVAL;
}


static int run()
{
	int ret;

	ret = ft_efa_rnr_init_fabric();
	if (ret) {
		FT_PRINTERR("ft_efa_rnr_init_fabric", -ret);
		return ret;
	}

	/* client does fi_send and then poll CQ to get error (FI_ENORX) CQ entry */
	if (opts.dst_addr) {
		ret = rnr_read_cq_error();
		if (ret) {
			FT_PRINTERR("rnr_poll_cq_error", -ret);
			return ret;
		}
	}
	/*
	 * To get RNR error on the client side, the server should not close its
	 * endpoint while the client is still sending.
	 * ft_reset_oob() will re-initialize OOB sync between server and client.
	 * Calling it here to ensure the client has finished the sending.
	 * And both server and client are ready to close endpoint and free resources.
	 */
	ret = ft_reset_oob();
	if (ret) {
		FT_PRINTERR("ft_reset_oob", -ret);
		return ret;
	}

	ret = ft_close_oob();
	if (ret) {
		FT_PRINTERR("ft_close_oob", -ret);
		return ret;
	}
	ft_free_res();

	return 0;
}


int main(int argc, char **argv)
{
	int op, ret;

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SIZE;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt(argc, argv, ADDR_OPTS INFO_OPTS CS_OPTS)) != -1) {
		switch (op) {
		default:
			ft_parse_addr_opts(op, optarg, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			ft_parsecsopts(op, optarg, &opts);
			break;
		case '?':
		case 'h':
			ft_usage(argv[0], "RDM RNR poll error CQ entry test");
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG;
	hints->mode |= FI_CONTEXT;
	hints->domain_attr->mr_mode = opts.mr_mode;

	/* FI_RM_DISABLED is required to get RNR error CQ entry */
	hints->domain_attr->resource_mgmt = FI_RM_DISABLED;
	/*
	 * RNR error is generated from EFA device, so disable shm transfer by
	 * setting FI_REMOTE_COMM and unsetting FI_LOCAL_COMM in order to ensure
	 * EFA device is being used when running this test on a single node.
	 */
	ft_efa_rnr_disable_hints_shm();

	ret = run();
	if (ret)
		FT_PRINTERR("run", -ret);

	return ft_exit_code(ret);
}
