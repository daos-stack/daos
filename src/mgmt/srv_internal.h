/*
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
#include <daos_srv/smd.h>
#include <daos_security.h>
#include <daos_prop.h>

#include "svc.pb-c.h"
#include "storage_query.pb-c.h"
#include "rpc.h"
#include "srv_layout.h"

/** srv.c */
void ds_mgmt_hdlr_svc_rip(crt_rpc_t *rpc);
void ds_mgmt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_profile_hdlr(crt_rpc_t *rpc);
void ds_mgmt_pool_get_svcranks_hdlr(crt_rpc_t *rpc);
void ds_mgmt_mark_hdlr(crt_rpc_t *rpc);

/** srv_system.c */
/* Management service (used only for map broadcast) */
struct mgmt_svc {
	struct ds_rsvc		ms_rsvc;
	ABT_rwlock		ms_lock;
	uint32_t		map_version;
	struct server_entry	*map_servers;
	int			n_map_servers;
};

struct mgmt_grp_up_in {
	uint32_t		gui_map_version;
	struct server_entry	*gui_servers;
	int			gui_n_servers;
};

int ds_mgmt_svc_start(void);
int ds_mgmt_svc_stop(void);
int ds_mgmt_system_module_init(void);
void ds_mgmt_system_module_fini(void);
int ds_mgmt_svc_get(struct mgmt_svc **svc);
void ds_mgmt_svc_put(struct mgmt_svc *svc);
int ds_mgmt_group_update_handler(struct mgmt_grp_up_in *in);

/** srv_pool.c */
int ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev,
			d_rank_list_t *targets, size_t scm_size,
			size_t nvme_size, daos_prop_t *prop, uint32_t svc_nr,
			d_rank_list_t **svcp);
int ds_mgmt_destroy_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			 const char *group, uint32_t force);
int ds_mgmt_evict_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		       const char *group);
int ds_mgmt_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
				     uint32_t rank,
				     struct pool_target_id_list *tgt_list,
				     pool_comp_state_t new_state);
int ds_mgmt_pool_reintegrate(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			     uint32_t reint_rank,
			     struct pool_target_id_list *reint_list);
int ds_mgmt_pool_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			d_rank_list_t *rank_list, char *tgt_dev,
			size_t scm_size, size_t nvme_size);
int ds_mgmt_pool_set_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			  daos_prop_t *prop, daos_prop_t **result);
int ds_mgmt_pool_get_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			 daos_prop_t **access_prop);
int ds_mgmt_pool_overwrite_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			       struct daos_acl *acl, daos_prop_t **result);
int ds_mgmt_pool_update_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			    struct daos_acl *acl, daos_prop_t **result);
int ds_mgmt_pool_delete_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			    const char *principal, daos_prop_t **result);
int ds_mgmt_pool_list_cont(uuid_t uuid, d_rank_list_t *svc_ranks,
			   struct daos_pool_cont_info **containers,
			   uint64_t *ncontainers);
int ds_mgmt_pool_query(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		       daos_pool_info_t *pool_info);
int ds_mgmt_cont_set_owner(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			   uuid_t cont_uuid, const char *user,
			   const char *group);

/** srv_query.c */

/* Device health stats from nvme_stats */
struct mgmt_bio_health {
	struct nvme_stats		mb_dev_state;
	uuid_t				mb_devid;
};

int ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t uuid,
			     char *tgt_id);
int ds_mgmt_smd_list_devs(Mgmt__SmdDevResp *resp);
int ds_mgmt_smd_list_pools(Mgmt__SmdPoolResp *resp);
int ds_mgmt_dev_state_query(uuid_t uuid, Mgmt__DevStateResp *resp);
int ds_mgmt_dev_set_faulty(uuid_t uuid, Mgmt__DevStateResp *resp);
int ds_mgmt_get_bs_state(uuid_t bs_uuid, int *bs_state);
void ds_mgmt_hdlr_get_bs_state(crt_rpc_t *rpc_req);

/** srv_target.c */
int ds_mgmt_tgt_setup(void);
void ds_mgmt_tgt_cleanup(void);
void ds_mgmt_hdlr_tgt_create(crt_rpc_t *rpc_req);
void ds_mgmt_hdlr_tgt_destroy(crt_rpc_t *rpc_req);
int ds_mgmt_tgt_create_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv);
void ds_mgmt_tgt_profile_hdlr(crt_rpc_t *rpc);
int ds_mgmt_tgt_map_update_pre_forward(crt_rpc_t *rpc, void *arg);
void ds_mgmt_hdlr_tgt_map_update(crt_rpc_t *rpc);
int ds_mgmt_tgt_map_update_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				      void *priv);
void ds_mgmt_tgt_mark_hdlr(crt_rpc_t *rpc);

/** srv_util.c */
int ds_mgmt_group_update(crt_group_mod_op_t op, struct server_entry *servers,
			 int nservers, uint32_t version);
void ds_mgmt_kill_rank(bool force);

#endif /* __SRV_MGMT_INTERNAL_H__ */
