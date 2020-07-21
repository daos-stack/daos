/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
