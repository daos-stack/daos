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

/*
 * Pool object
 *
 * Caches per-pool information, such as the pool map.
 */
struct ds_pool {
	struct daos_llink	sp_entry;
	uuid_t			sp_uuid;
	ABT_rwlock		sp_lock;
	struct pool_map	       *sp_map;
	uint32_t		sp_map_version;	/* temporary */
	crt_group_t	       *sp_group;
	ABT_mutex		sp_iv_refresh_lock;
	struct ds_iv_ns		*sp_iv_ns;
};

struct ds_pool_create_arg {
	uint32_t		pca_map_version;
	bool			pca_need_group;
};

int ds_pool_lookup_create(const uuid_t uuid, struct ds_pool_create_arg *arg,
			  struct ds_pool **pool);
struct ds_pool *ds_pool_lookup(const uuid_t uuid);
void ds_pool_put(struct ds_pool *pool);

/*
 * Pool handle object
 *
 * Stores per-handle information, such as the capabilities. References the pool
 * object.
 */
struct ds_pool_hdl {
	d_list_t		sph_entry;
	uuid_t			sph_uuid;	/* of the pool handle */
	uint64_t		sph_capas;
	struct ds_pool	       *sph_pool;
	int			sph_ref;
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
	d_list_t	spc_list;
	daos_handle_t	spc_hdl;
	struct ds_pool	*spc_pool;
	uuid_t		spc_uuid;
	uint32_t	spc_map_version;
	int		spc_ref;
};

/*
 * Pool properties uid/gid/mode
 *
 * Stores per-pool access control information
 *
 * This is only being exposed until the access control attributes are
 * proper encapsulated in the security module.
 */
struct pool_prop_ugm {
	uint32_t	pp_uid;
	uint32_t	pp_gid;
	uint32_t	pp_mode;
};

struct ds_pool_child *ds_pool_child_lookup(const uuid_t uuid);
struct ds_pool_child *ds_pool_child_get(struct ds_pool_child *child);
void ds_pool_child_put(struct ds_pool_child *child);

int ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
			 enum daos_module_id module, crt_opcode_t opcode,
			 crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
			 d_rank_list_t *excluded_list);

int ds_pool_map_buf_get(uuid_t uuid, d_iov_t *iov, uint32_t *map_ver);

int ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_exclude(uuid_t pool_uuid, struct pool_target_id_list *list);

int ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
			   unsigned int map_version);

/*
 * TODO: Make the following internal functions of ds_pool after merging in
 * mgmt.
 */

int ds_pool_create(const uuid_t pool_uuid, const char *path,
		   uuid_t target_uuid);

int ds_pool_svc_create(const uuid_t pool_uuid, int ntargets,
		       uuid_t target_uuids[], const char *group,
		       const d_rank_list_t *target_addrs, int ndomains,
		       const int *domains, daos_prop_t *prop,
		       d_rank_list_t *svc_addrs);
int ds_pool_svc_destroy(const uuid_t pool_uuid);

int ds_pool_svc_get_acl_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
			     daos_prop_t **prop);
int ds_pool_svc_set_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
			 daos_prop_t *prop);

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

typedef int (*ds_iter_cb_t)(uuid_t cont_uuid, vos_iter_entry_t *ent,
			     void *arg);
int ds_pool_iter(uuid_t pool_uuid, ds_iter_cb_t callback, void *arg,
		 uint32_t version, uint32_t intent);

struct cont_svc;
struct rsvc_hint;
int ds_pool_cont_svc_lookup_leader(uuid_t pool_uuid, struct cont_svc **svc,
				   struct rsvc_hint *hint);

int ds_pool_iv_ns_update(struct ds_pool *pool, unsigned int master_rank,
			 d_iov_t *iv_iov, unsigned int iv_ns_id);

int ds_pool_svc_term_get(uuid_t uuid, uint64_t *term);

int ds_pool_check_leader(uuid_t pool_uuid, daos_unit_oid_t *oid,
			 uint32_t version, struct pl_obj_layout **plo);

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
#endif /* __DAOS_SRV_POOL_H__ */
