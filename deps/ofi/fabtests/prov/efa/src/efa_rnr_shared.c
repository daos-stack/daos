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
 */

#include <stdio.h>

#include <rdma/fi_ext.h>

#include <shared.h>
#include "efa_rnr_shared.h"


void ft_efa_rnr_disable_hints_shm()
{
	hints->caps |= FI_REMOTE_COMM;
	hints->caps &= ~FI_LOCAL_COMM;
}

int ft_efa_rnr_init_fabric()
{
	int ret;
	size_t rnr_retry = 0;

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

	ret = ft_alloc_active_res(fi);
	if (ret) {
		FT_PRINTERR("ft_alloc_active_res", -ret);
		return ret;
	}

	fprintf(stdout, "Setting RNR retry count to %zu ...\n", rnr_retry);
	ret = fi_setopt(&ep->fid, FI_OPT_ENDPOINT, FI_OPT_EFA_RNR_RETRY, &rnr_retry, sizeof(rnr_retry));
	if (ret) {
		FT_PRINTERR("fi_setopt", -ret);
		return ret;
	}
	fprintf(stdout, "RNR retry count has been set to %zu.\n", rnr_retry);

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
	return 0;
}

