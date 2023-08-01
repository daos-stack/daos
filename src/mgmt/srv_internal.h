/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos_srv/daos_engine.h>
#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/smd.h>
#include <daos_security.h>
#include <daos_prop.h>

#include "svc.pb-c.h"
#include "smd.pb-c.h"
#include "rpc.h"
#include "srv_layout.h"

/** srv.c */
void ds_mgmt_hdlr_svc_rip(crt_rpc_t *rpc);
void ds_mgmt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_profile_hdlr(crt_rpc_t *rpc);
void ds_mgmt_pool_get_svcranks_hdlr(crt_rpc_t *rpc);
void ds_mgmt_pool_find_hdlr(crt_rpc_t *rpc);
void ds_mgmt_mark_hdlr(crt_rpc_t *rpc);
void dss_bind_to_xstream_cpuset(int tgt_id);

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
int ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev, d_rank_list_t *targets,
			size_t scm_size, size_t nvme_size, daos_prop_t *prop, d_rank_list_t **svcp,
			int domains_nr, uint32_t *domains);
int ds_mgmt_destroy_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks);
int ds_mgmt_evict_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks, uuid_t *handles,
		       size_t n_handles, uint32_t destroy, uint32_t force_destroy,
		       char *machine, uint32_t *count);
int ds_mgmt_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
				     struct pool_target_addr_list *target_addrs,
				     pool_comp_state_t state, size_t scm_size, size_t nvme_size);
int ds_mgmt_pool_reintegrate(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			     uint32_t reint_rank,
			     struct pool_target_id_list *reint_list);
int ds_mgmt_pool_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			d_rank_list_t *rank_list, char *tgt_dev,
			size_t scm_size, size_t nvme_size,
			size_t domains_nr, uint32_t *domains);
int ds_mgmt_pool_set_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			  daos_prop_t *prop);
int ds_mgmt_pool_get_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			  daos_prop_t *prop);
int ds_mgmt_pool_upgrade(uuid_t pool_uuid, d_rank_list_t *svc_ranks);
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
int ds_mgmt_pool_query(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_list_t **ranks,
		       daos_pool_info_t *pool_info, uint32_t *pool_layout_ver,
		       uint32_t *upgrade_layout_ver);
int ds_mgmt_pool_query_targets(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_t rank,
			       d_rank_list_t *tgts, daos_target_info_t **infos);

int ds_mgmt_cont_set_owner(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			   uuid_t cont_uuid, const char *user,
			   const char *group);

/** srv_query.c */

/* Device health stats from nvme_stats */
struct mgmt_bio_health {
	struct nvme_stats	mb_dev_state;
	uuid_t			mb_devid;
	uint64_t		mb_meta_size;
	uint64_t		mb_rdb_size;
};

int ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t uuid);
int ds_mgmt_smd_list_devs(Ctl__SmdDevResp *resp);
int ds_mgmt_smd_list_pools(Ctl__SmdPoolResp *resp);
int ds_mgmt_dev_set_faulty(uuid_t uuid, Ctl__DevManageResp *resp);
int ds_mgmt_dev_manage_led(Ctl__LedManageReq *req, Ctl__DevManageResp *resp);
int ds_mgmt_get_bs_state(uuid_t bs_uuid, int *bs_state);
void ds_mgmt_hdlr_get_bs_state(crt_rpc_t *rpc_req);
int ds_mgmt_dev_replace(uuid_t old_uuid, uuid_t new_uuid, Ctl__DevManageResp *resp);

/** srv_target.c */
int ds_mgmt_tgt_setup(void);
void ds_mgmt_tgt_cleanup(void);
void ds_mgmt_hdlr_tgt_create(crt_rpc_t *rpc_req);
void ds_mgmt_hdlr_tgt_destroy(crt_rpc_t *rpc_req);
int ds_mgmt_tgt_create_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv);
int ds_mgmt_tgt_create_post_reply(crt_rpc_t *rpc, void *priv);
void ds_mgmt_tgt_profile_hdlr(crt_rpc_t *rpc);
int ds_mgmt_tgt_map_update_pre_forward(crt_rpc_t *rpc, void *arg);
void ds_mgmt_hdlr_tgt_map_update(crt_rpc_t *rpc);
int ds_mgmt_tgt_map_update_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				      void *priv);
void ds_mgmt_tgt_mark_hdlr(crt_rpc_t *rpc);

/** srv_util.c */
int ds_mgmt_group_update(struct server_entry *servers, int nservers, uint32_t version);
void ds_mgmt_kill_rank(bool force);

#endif /* __SRV_MGMT_INTERNAL_H__ */
