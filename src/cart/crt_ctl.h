/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It's the header for crt_ctl.c.
 */

#ifndef __CRT_CTL_H__
#define __CRT_CTL_H__

/* crt uri lookup cache info */
struct crt_uri_cache {
	struct crt_grp_cache	*grp_cache;
	uint32_t		 max_count;
	uint32_t		 idx;
};

void crt_hdlr_ctl_get_uri_cache(crt_rpc_t *rpc_req);
void crt_hdlr_ctl_ls(crt_rpc_t *rpc_req);
void crt_hdlr_ctl_get_hostname(crt_rpc_t *rpc_req);
void crt_hdlr_ctl_get_pid(crt_rpc_t *rpc_req);

#endif /* __CRT_CTL_H__ */
