/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * dc_mgmt: Management Client API
 */

#ifndef __DC_MGMT_H__
#define __DC_MGMT_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>

int dc_mgmt_init(void);

void dc_mgmt_fini(void);

struct cp_arg {
	struct dc_mgmt_sys	*sys;
	crt_rpc_t		*rpc;
};

int dc_cp(tse_task_t *task, void *data);
int dc_deprecated(tse_task_t *task);
int dc_mgmt_profile(char *path, int avg, bool start);
int dc_mgmt_get_bs_state(tse_task_t *task);

#define SYS_INFO_BUF_SIZE 16

/** GetAttachInfo system info */
struct sys_info {
	char		provider[DAOS_SYS_INFO_STRING_MAX + 1];
	char		interface[DAOS_SYS_INFO_STRING_MAX + 1];
	char		domain[DAOS_SYS_INFO_STRING_MAX + 1];
	uint32_t	crt_ctx_share_addr;
	uint32_t	crt_timeout;
};

/** Client system handle */
struct dc_mgmt_sys {
	d_list_t		sy_link;
	char			sy_name[DAOS_SYS_NAME_MAX + 1];
	int			sy_ref;
	bool			sy_server;
	int			sy_npsrs;
	struct dc_mgmt_psr     *sy_psrs;
	crt_group_t	       *sy_group;
	struct sys_info		sy_info;
};

int dc_mgmt_net_cfg(const char *name);
int dc_mgmt_sys_attach(const char *name, struct dc_mgmt_sys **sysp);
void dc_mgmt_sys_detach(struct dc_mgmt_sys *sys);
ssize_t dc_mgmt_sys_encode(struct dc_mgmt_sys *sys, void *buf, size_t cap);
ssize_t dc_mgmt_sys_decode(void *buf, size_t len, struct dc_mgmt_sys **sysp);
int dc_mgmt_get_pool_svc_ranks(struct dc_mgmt_sys *sys, const uuid_t puuid,
			       d_rank_list_t **svcranksp);
int dc_mgmt_disconnect(void);
#endif
