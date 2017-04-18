/**
 * (C) Copyright 2016 Intel Corporation.
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
 * ds_pool: Pool Server API
 */

#ifndef __DAOS_SRV_POOL_H__
#define __DAOS_SRV_POOL_H__

#include <daos/btree.h>
#include <daos/lru.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>

#include <abt.h>

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
};

struct ds_pool *ds_pool_lookup(const uuid_t uuid);
void ds_pool_put(struct ds_pool *pool);

/*
 * Pool handle object
 *
 * Stores per-handle information, such as the capabilities. References the pool
 * object.
 */
struct ds_pool_hdl {
	daos_list_t		sph_entry;
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
	daos_list_t	spc_list;
	daos_handle_t	spc_hdl;
	uuid_t		spc_uuid;
	uint32_t	spc_map_version;
	int		spc_ref;
	struct pool_map *spc_map;
};

struct ds_pool_child *ds_pool_child_lookup(const uuid_t uuid);
void ds_pool_child_put(struct ds_pool_child *child);

int
ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
		     enum daos_module_id module, crt_opcode_t opcode,
		     crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
		     daos_rank_list_t *excluded_list);
int
ds_pool_pmap_broadcast(const uuid_t uuid, daos_rank_list_t *tgts_exclude);

struct pool_map *ds_pool_get_pool_map(const uuid_t uuid);
/*
 * Metadata pmem pool descriptor
 *
 * Referenced by pool and container service descriptors. In the future, we may
 * use separate files for ds_pool and ds_cont, after which the mpool code can
 * be retired.
 */
struct ds_pool_mpool {
	daos_list_t			mp_entry;
	uuid_t				mp_uuid;	/* of the DAOS pool */
	pthread_mutex_t			mp_lock;
	int				mp_ref;
	PMEMobjpool		       *mp_pmem;
	struct ds_pool_mpool_sb	       *mp_sb;
};

int ds_pool_mpool_lookup(const uuid_t pool_uuid, struct ds_pool_mpool **mpool);
void ds_pool_mpool_get(struct ds_pool_mpool *mpool);
void ds_pool_mpool_put(struct ds_pool_mpool *mpool);

/*
 * mpool storage layout
 *
 * On one storage node, all metadata belonging to the same DAOS pool are stored
 * in one libpmemobj pool, called an "mpool" in the code. In an mpool, the
 * metadata are stored in a number of dbtree trees that form one larger tree
 * structure. The root object of the mpool acts as the superblock, from which
 * one can find the ds_pool and ds_cont root trees as well as the compatibility
 * stuff:
 *
 *   Superblock (pmemobj_root):
 *
 *     ds_pool root tree (NV):
 *       ... (src/pool/srv_layout.h)
 *
 *     ds_cont root tree (NV):
 *       ... (src/container/srv_layout.h>
 */

/* These are for pmemobj_create() and/or pmemobj_open(). */
#define DS_POOL_MPOOL_LAYOUT	"dsms_metadata"
#define DS_POOL_MPOOL_SIZE	(1 << 26)	/* 64 MB */

int
ds_pool_local_open(uuid_t uuid, unsigned int version,
		   struct ds_pool_child **childp);
int
ds_pool_local_close(uuid_t uuid);

/*
 * mpool superblock (pmemobj_root)
 *
 * Because pool UUIDs are important and constant, they are stored redundantly
 * in path names and superblocks.
 *
 * TODO: Add compatibility and checksum stuff.
 */
struct ds_pool_mpool_sb {
	uint64_t	s_magic;
	uuid_t		s_pool_uuid;
	uuid_t		s_target_uuid;
	struct btr_root	s_pool_root;	/* ds_pool root tree */
	struct btr_root	s_cont_root;	/* ds_cont root tree */
};

/* ds_pool_mpool_sb::s_magic */
#define DS_POOL_MPOOL_SB_MAGIC 0x8120da0367913ef9

/*
 * TODO: Make the following internal functions of ds_pool after merging in
 * mgmt.
 */

int ds_pool_create(const uuid_t pool_uuid, const char *path,
		   uuid_t target_uuid);

int ds_pool_svc_create(const uuid_t pool_uuid, unsigned int uid,
		       unsigned int gid, unsigned int mode, int ntargets,
		       uuid_t target_uuids[], const char *group,
		       const daos_rank_list_t *target_addrs, int ndomains,
		       const int *domains, daos_rank_list_t *svc_addrs);
int ds_pool_svc_destroy(const uuid_t pool_uuid);

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

typedef int (*pool_iter_cb_t)(daos_handle_t ph, uuid_t co_uuid, void *arg);
int ds_pool_cont_iter(daos_handle_t ph, pool_iter_cb_t callback, void *arg);

typedef int (*obj_iter_cb_t)(uuid_t cont_uuid, daos_unit_oid_t oid, void *arg);
int ds_pool_obj_iter(uuid_t pool_uuid, obj_iter_cb_t callback, void *arg);

char *ds_pool_rdb_path(const uuid_t uuid, const uuid_t pool_uuid);
int ds_pool_svc_start(const uuid_t uuid);
void ds_pool_svc_stop(const uuid_t uuid);

#endif /* __DAOS_SRV_POOL_H__ */
