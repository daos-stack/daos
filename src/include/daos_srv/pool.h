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
 * \file
 *
 * ds_pool: Pool Server API
 */

#ifndef __DAOS_SRV_POOL_H__
#define __DAOS_SRV_POOL_H__

#include <abt.h>
#include <daos/common.h>
#include <daos/lru.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/placement.h>
#include <daos_srv/vos_types.h>
#include <daos_pool.h>
#include <daos_security.h>

/*
 * Pool object
 *
 * Caches per-pool information, such as the pool map.
 */
struct ds_pool {
	struct daos_llink	sp_entry;
	uuid_t			sp_uuid;	/* pool UUID */
	ABT_rwlock		sp_lock;
	struct pool_map	       *sp_map;
	uint32_t		sp_map_version;	/* temporary */
	uint64_t		sp_reclaim;
	crt_group_t	       *sp_group;
	ABT_mutex		sp_mutex;
	ABT_cond		sp_fetch_hdls_cond;
	ABT_cond		sp_fetch_hdls_done_cond;
	struct ds_iv_ns	       *sp_iv_ns;

	/* structure related to EC aggregate epoch query */
	d_list_t		sp_ec_ephs_list;
	struct sched_request	*sp_ec_ephs_req;

	uint32_t		sp_dtx_resync_version;
	/* Special pool/container handle uuid, which are
	 * created on the pool leader step up, and propagated
	 * to all servers by IV. Then they will be used by server
	 * to access the data on other servers.
	 */
	uuid_t			sp_srv_cont_hdl;
	uuid_t			sp_srv_pool_hdl;
	uint32_t		sp_stopping:1,
				sp_fetch_hdls:1;
};

struct ds_pool *ds_pool_lookup(const uuid_t uuid);
void ds_pool_put(struct ds_pool *pool);
void ds_pool_get(struct ds_pool *pool);

/*
 * Pool handle object
 *
 * Stores per-handle information, such as the capabilities. References the pool
 * object.
 */
struct ds_pool_hdl {
	d_list_t		sph_entry;
	uuid_t			sph_uuid;	/* of the pool handle */
	uint64_t		sph_flags;	/* user-provided flags */
	uint64_t		sph_sec_capas;	/* access capabilities */
	struct ds_pool	       *sph_pool;
	int			sph_ref;
	d_iov_t			sph_cred;
};

struct ds_pool_hdl *ds_pool_hdl_lookup(const uuid_t uuid);
void ds_pool_hdl_put(struct ds_pool_hdl *hdl);

/*
 * Per-thread pool object
 *
 * Stores per-thread, per-pool information, such as the vos pool handle. And,
 * caches per-pool information, such as the pool map version, so that DAOS
 * object I/Os do not need to access global, parent ds_pool objects.
 */
struct ds_pool_child {
	d_list_t		spc_list;
	daos_handle_t		spc_hdl;	/* vos_pool handle */
	struct ds_pool		*spc_pool;
	uuid_t			spc_uuid;	/* pool UUID */
	struct sched_request	*spc_gc_req;	/* Track GC ULT */
	struct sched_request	*spc_scrubbing_req; /* Track scrubbing ULT*/
	d_list_t		spc_cont_list;

	/* The current maxim rebuild epoch, (0 if there is no rebuild), so
	 * vos aggregation can not cross this epoch during rebuild to avoid
	 * interfering rebuild process.
	 */
	uint64_t	spc_rebuild_fence;

	/* The HLC when current rebuild ends, which will be used to compare
	 * with the aggregation full scan start HLC to know whether the
	 * aggregation needs to be restarted from 0. */
	uint64_t	spc_rebuild_end_hlc;
	uint32_t	spc_map_version;
	int		spc_ref;
};

struct ds_pool_child *ds_pool_child_lookup(const uuid_t uuid);
struct ds_pool_child *ds_pool_child_get(struct ds_pool_child *child);
void ds_pool_child_put(struct ds_pool_child *child);

int ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
			 enum daos_module_id module, crt_opcode_t opcode,
			 crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
			 d_rank_list_t *excluded_list);

int ds_pool_map_buf_get(uuid_t uuid, d_iov_t *iov, uint32_t *map_ver);
int ds_pool_get_open_handles(uuid_t pool_uuid, d_iov_t *hdls);

int ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_exclude(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_add_in(uuid_t pool_uuid, struct pool_target_id_list *list);

int ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
			   unsigned int map_version);

/*
 * TODO: Make the following internal functions of ds_pool after merging in
 * mgmt.
 */

int ds_pool_create(const uuid_t pool_uuid, const char *path,
		   uuid_t target_uuid);
int ds_pool_start(uuid_t uuid);
void ds_pool_stop(uuid_t uuid);
int ds_pool_extend(uuid_t pool_uuid, int ntargets, uuid_t target_uuids[],
		   const d_rank_list_t *rank_list, int ndomains,
		   const int *domains, d_rank_list_t *svc_ranks);
int ds_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *ranks,
				uint32_t rank,
				struct pool_target_id_list *target_list,
				pool_comp_state_t state);

int ds_pool_svc_create(const uuid_t pool_uuid, int ntargets,
		       uuid_t target_uuids[], const char *group,
		       const d_rank_list_t *target_addrs, int ndomains,
		       const int *domains, daos_prop_t *prop,
		       d_rank_list_t *svc_addrs);
int ds_pool_svc_destroy(const uuid_t pool_uuid);

int ds_pool_svc_get_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
			 daos_prop_t *prop);
int ds_pool_svc_set_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
			 daos_prop_t *prop);
int ds_pool_svc_update_acl(uuid_t pool_uuid, d_rank_list_t *ranks,
			   struct daos_acl *acl);
int ds_pool_svc_delete_acl(uuid_t pool_uuid, d_rank_list_t *ranks,
			   enum daos_acl_principal_type principal_type,
			   const char *principal_name);

int ds_pool_svc_query(uuid_t pool_uuid, d_rank_list_t *ranks,
		      daos_pool_info_t *pool_info);

int ds_pool_prop_fetch(struct ds_pool *pool, unsigned int bit,
		       daos_prop_t **prop_out);
/*
 * Called by dmg on the pool service leader to list all pool handles of a pool.
 * Upon successful completion, "buf" returns an array of handle UUIDs if its
 * large enough, while "size" returns the size of all the handle UUIDs assuming
 * "buf" is large enough.
 */
int ds_pool_hdl_list(const uuid_t pool_uuid, uuid_t buf, size_t *size);

/*
 * Called by dmg on the pool service leader to evict one or all pool handles of
 * a pool. If "handle_uuid" is NULL, all pool handles of the pool are evicted.
 */
int ds_pool_hdl_evict(const uuid_t pool_uuid, const uuid_t handle_uuid);

struct cont_svc;
struct rsvc_hint;
int ds_pool_cont_svc_lookup_leader(uuid_t pool_uuid, struct cont_svc **svc,
				   struct rsvc_hint *hint);

void ds_pool_iv_ns_update(struct ds_pool *pool, unsigned int master_rank);

int ds_pool_iv_map_update(struct ds_pool *pool, struct pool_buf *buf,
		       uint32_t map_ver);
int ds_pool_iv_prop_update(struct ds_pool *pool, daos_prop_t *prop);
int ds_pool_iv_prop_fetch(struct ds_pool *pool, daos_prop_t *prop);
int ds_pool_iv_svc_fetch(struct ds_pool *pool, d_rank_list_t **svc_p);

int ds_pool_iv_srv_hdl_fetch(struct ds_pool *pool, uuid_t *pool_hdl_uuid,
			     uuid_t *cont_hdl_uuid);

int ds_pool_svc_term_get(uuid_t uuid, uint64_t *term);

int ds_pool_elect_dtx_leader(struct ds_pool *pool, daos_unit_oid_t *oid,
			     uint32_t version);
int ds_pool_check_dtx_leader(struct ds_pool *pool, daos_unit_oid_t *oid,
			     uint32_t version);

int
ds_pool_child_map_refresh_sync(struct ds_pool_child *dpc);
int
ds_pool_child_map_refresh_async(struct ds_pool_child *dpc);

enum map_ranks_class {
	MAP_RANKS_UP,
	MAP_RANKS_DOWN
};

int
map_ranks_init(const struct pool_map *map, enum map_ranks_class class,
	       d_rank_list_t *ranks);

void
map_ranks_fini(d_rank_list_t *ranks);

int ds_pool_get_ranks(const uuid_t pool_uuid, int status,
		      d_rank_list_t *ranks);

int ds_pool_get_failed_tgt_idx(const uuid_t pool_uuid, int **failed_tgts,
			       unsigned int *failed_tgts_cnt);
int ds_pool_svc_list_cont(uuid_t uuid, d_rank_list_t *ranks,
			  struct daos_pool_cont_info **containers,
			  uint64_t *ncontainers);

int ds_pool_svc_check_evict(uuid_t pool_uuid, d_rank_list_t *ranks,
			    uint32_t force);
void
ds_pool_disable_evict(void);
void
ds_pool_enable_evict(void);

int dsc_pool_open(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		       unsigned int flags, const char *grp,
		       struct pool_map *map, d_rank_list_t *svc_list,
		       daos_handle_t *ph);
int dsc_pool_close(daos_handle_t ph);

#endif /* __DAOS_SRV_POOL_H__ */
