/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_mgmt: Management Client API
 */

#ifndef __DC_MGMT_H__
#define __DC_MGMT_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>
#include <daos/pool.h>
#include "svc.pb-c.h"

int
dc_mgmt_init(void);

void
dc_mgmt_fini(void);

struct cp_arg {
	struct dc_mgmt_sys *sys;
	crt_rpc_t          *rpc;
};

int
dc_cp(tse_task_t *task, void *data);
int
dc_deprecated(tse_task_t *task);
int
dc_mgmt_profile(char *path, int avg, bool start);
int
dc_mgmt_get_bs_state(tse_task_t *task);

/** System info */
struct dc_mgmt_sys_info {
	char           provider[DAOS_SYS_INFO_STRING_MAX + 1];
	char           interface[DAOS_SYS_INFO_STRING_MAX + 1];
	char           domain[DAOS_SYS_INFO_STRING_MAX + 1];
	uint32_t       crt_ctx_share_addr;
	uint32_t       crt_timeout;
	int32_t        srv_srx_set;
	d_rank_list_t *ms_ranks;
};

/** Client system handle */
struct dc_mgmt_sys {
	d_list_t                sy_link;
	char                    sy_name[DAOS_SYS_NAME_MAX + 1];
	int                     sy_ref;
	bool                    sy_server;
	crt_group_t            *sy_group;
	struct dc_mgmt_sys_info sy_info;
};

int
dc_mgmt_sys_attach(const char *name, struct dc_mgmt_sys **sysp);
void
dc_mgmt_sys_detach(struct dc_mgmt_sys *sys);
ssize_t
dc_mgmt_sys_encode(struct dc_mgmt_sys *sys, void *buf, size_t cap);
ssize_t
dc_mgmt_sys_decode(void *buf, size_t len, struct dc_mgmt_sys **sysp);

int
dc_mgmt_net_cfg(const char *name);
int
dc_mgmt_get_pool_svc_ranks(struct dc_mgmt_sys *sys, const uuid_t puuid, d_rank_list_t **svcranksp);
int
dc_mgmt_pool_find(struct dc_mgmt_sys *sys, const char *label, uuid_t puuid,
		  d_rank_list_t **svcranksp);
int
dc_mgmt_notify_pool_connect(struct dc_pool *pool);
int
dc_mgmt_notify_pool_disconnect(struct dc_pool *pool);
int
dc_mgmt_notify_exit(void);
int
dc_mgmt_net_get_num_srv_ranks(void);

int
dc_get_attach_info(const char *name, bool all_ranks, struct dc_mgmt_sys_info *info,
		   Mgmt__GetAttachInfoResp **respp);

void
dc_put_attach_info(struct dc_mgmt_sys_info *info, Mgmt__GetAttachInfoResp *resp);

#endif
