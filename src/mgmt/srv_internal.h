/*
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * ds_mgmt: Internal Declarations
 *
 * This file contains all declarations that are only used by ds_mgmts.
 * All external variables and functions must have a "ds_mgmt_" prefix.
 */

#ifndef __SRV_MGMT_INTERNAL_H__
#define __SRV_MGMT_INTERNAL_H__

#include <gurt/list.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/rsvc.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>

#include "mgmt.pb-c.h"
#include "rpc.h"
#include "srv_layout.h"

/** srv.c */
void ds_mgmt_hdlr_svc_rip(crt_rpc_t *rpc);
void ds_mgmt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_profile_hdlr(crt_rpc_t *rpc);

/** srv_system.c */

/* Management service */
struct mgmt_svc {
	struct ds_rsvc		ms_rsvc;
	ABT_rwlock		ms_lock;
	rdb_path_t		ms_root;
	rdb_path_t		ms_servers;
	rdb_path_t		ms_uuids;
	rdb_path_t		ms_pools;
	ABT_mutex		ms_mutex;
	bool			ms_step_down;
	bool			ms_distribute;
	ABT_cond		ms_distribute_cv;
	ABT_thread		ms_distributord;
	uint32_t		ms_map_version;
	uint32_t		ms_rank_next;
};

int ds_mgmt_system_module_init(void);
void ds_mgmt_system_module_fini(void);
int ds_mgmt_svc_start(bool create, size_t size, bool bootstrap, uuid_t srv_uuid,
		      char *addr);
int ds_mgmt_svc_stop(void);
int ds_mgmt_svc_lookup_leader(struct mgmt_svc **svc, struct rsvc_hint *hint);
void ds_mgmt_svc_put_leader(struct mgmt_svc *svc);
struct mgmt_join_in {
	uint32_t		ji_rank;
	struct server_rec	ji_server;
};
struct mgmt_join_out {
	uint32_t		jo_rank;
	uint8_t			jo_flags;	/* server_rec.sr_flags */
	struct rsvc_hint	jo_hint;
};
int ds_mgmt_join_handler(struct mgmt_join_in *in, struct mgmt_join_out *out);
int ds_mgmt_get_attach_info_handler(Mgmt__GetAttachInfoResp *resp);

/** srv_pool.c */
int ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev,
			d_rank_list_t *targets, size_t scm_size,
			size_t nvme_size, daos_prop_t *prop, uint32_t svc_nr,
			d_rank_list_t **svcp);
int ds_mgmt_destroy_pool(uuid_t pool_uuid, const char *group, uint32_t force);
void ds_mgmt_hdlr_pool_create(crt_rpc_t *rpc_req);
void ds_mgmt_hdlr_pool_destroy(crt_rpc_t *rpc_req);
int ds_mgmt_bio_health_query(void);

/** srv_target.c */
int ds_mgmt_tgt_init(void);
void ds_mgmt_tgt_fini(void);
void ds_mgmt_hdlr_tgt_create(crt_rpc_t *rpc_req);
void ds_mgmt_hdlr_tgt_destroy(crt_rpc_t *rpc_req);
int ds_mgmt_tgt_create_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv);
void ds_mgmt_tgt_profile_hdlr(crt_rpc_t *rpc);
int ds_mgmt_tgt_map_update_pre_forward(crt_rpc_t *rpc, void *arg);
void ds_mgmt_hdlr_tgt_map_update(crt_rpc_t *rpc);
int ds_mgmt_tgt_map_update_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				      void *priv);

#endif /* __SRV_MGMT_INTERNAL_H__ */
