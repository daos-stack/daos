/*
 * Copyright (c) 2022, Amazon.com, Inc.  All rights reserved.
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
 * This test returns whether or not the (optionally provided) provider
 * supports FI_HMEM
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <shared.h>

int main(int argc, char** argv)
{
	int ret;
	int op;
	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;
	hints->mode = ~0;
	hints->domain_attr->mode = ~0;
	hints->domain_attr->mr_mode = ~(FI_MR_BASIC | FI_MR_SCALABLE);
	while ((op = getopt(argc, argv, "p:h")) != -1) {
		switch (op) {
		case 'p':
			hints->fabric_attr->prov_name = strdup(optarg);
			break;
		case '?':
		case 'h':
			FT_PRINT_OPTS_USAGE("-p <provider>", "specific provider name eg shm, efa");
			return EXIT_FAILURE;
		}
	}

	ret = ft_init();
	if (ret) {
		FT_PRINTERR("ft_init", -ret);
		goto out;
	}
	hints->caps |= FI_HMEM;
	ret = ft_getinfo(hints, &fi);
	if (ret) {
		goto out;
	}
out:
	fi_freeinfo(hints);
	fi_freeinfo(fi);
	return ft_exit_code(ret);
}
