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

/*
 * Target service pool object
 *
 * Caches per-pool information, such as the pool map. Used by pool, container,
 * and target services. Referenced by pool_svc, cont_svc, and tgt_pool_hdl
 * objects.
 */
struct tgt_pool {
	struct daos_llink	tp_entry;
	uuid_t			tp_uuid;
	struct pool_map	       *tp_map;
	uint32_t		tp_map_version;	/* until map is everywhere */
	dtp_group_t	       *tp_group;
};

struct tgt_pool_create_arg {
	struct pool_buf	       *pca_map_buf;
	uint32_t		pca_map_version;
	int			pca_create_group;
};
int dsms_tgt_pool_lookup(const uuid_t uuid, struct tgt_pool_create_arg *arg,
			 struct tgt_pool **pool);
void dsms_tgt_pool_put(struct tgt_pool *pool);

/*
 * Target service pool handle object
 *
 * Stores per-handle information, such as the capabilities. Used by container
 * and target services. References the pool object.
 */
struct tgt_pool_hdl {
	daos_list_t		tph_entry;
	uuid_t			tph_uuid;	/* of the pool handle */
	uint64_t		tph_capas;
	struct tgt_pool	       *tph_pool;
	int			tph_ref;
};

struct tgt_pool_hdl *dsms_tgt_pool_hdl_lookup(const uuid_t uuid);
void dsms_tgt_pool_hdl_put(struct tgt_pool_hdl *hdl);

/*
 * Target service per-thread pool object
 *
 * Stores per-thread, per-pool information, such as the vos pool handle. And,
 * caches per-pool information, such as the pool map version, so that DAOS
 * object I/Os do not need to access global tgt_pool objects.
 */
struct dsms_vpool {
	daos_list_t	dvp_list;
	daos_handle_t	dvp_hdl;
	uuid_t		dvp_uuid;
	uint32_t	dvp_map_version;
	int		dvp_ref;
};

struct dsms_vpool *vpool_lookup(const uuid_t vp_uuid);
void vpool_put(struct dsms_vpool *vpool);

/*
 * Metadata pmem pool descriptor
 *
 * Referenced by pool and container service descriptors. In the future, we may
 * use separate files for ds_pool and ds_cont, after which the mpool code can
 * be retired.
 */
struct mpool {
	daos_list_t		mp_entry;
	uuid_t			mp_uuid;	/* of the DAOS pool */
	pthread_mutex_t		mp_lock;
	int			mp_ref;
	PMEMobjpool	       *mp_pmem;
	struct superblock      *mp_sb;
};

int dsms_mpool_lookup(const uuid_t pool_uuid, struct mpool **mpool);
void dsms_mpool_get(struct mpool *mpool);
void dsms_mpool_put(struct mpool *mpool);

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
#define MPOOL_LAYOUT	"dsms_metadata"
#define MPOOL_SIZE	(1 << 26)	/* 64 MB */

/*
 * Superblock (pmemobj_root)
 *
 * Because pool UUIDs are important and constant, they are stored redundantly
 * in path names and superblocks.
 *
 * TODO: Add compatibility and checksum stuff.
 */
struct superblock {
	uint64_t	s_magic;
	uuid_t		s_pool_uuid;
	uuid_t		s_target_uuid;
	struct btr_root	s_pool_root;	/* ds_pool root tree */
	struct btr_root	s_cont_root;	/* ds_cont root tree */
};

/* superblock::s_magic */
#define SUPERBLOCK_MAGIC	0x8120da0367913ef9

/*
 * TODO: Make the following internal functions of ds_pool after merging in
 * mgmt.
 */

/*
 * Called by dmg on every storage node belonging to this pool. "path" is the
 * directory under which the VOS and metadata files shall be. "target_uuid"
 * returns the UUID generated for the target on this storage node.
 */
int
dsms_pool_create(const uuid_t pool_uuid, const char *path, uuid_t target_uuid);

/*
 * Called by dmg on a single storage node belonging to this pool after the
 * dsms_pool_create() phase completes. "target_uuids" shall be an array of the
 * target UUIDs returned by the dsms_pool_create() calls. "svc_addrs" returns
 * the ranks of the pool services replicas within "group".
 */
int
dsms_pool_svc_create(const uuid_t pool_uuid, unsigned int uid, unsigned int gid,
		     unsigned int mode, int ntargets, uuid_t target_uuids[],
		     const char *group, const daos_rank_list_t *target_addrs,
		     int ndomains, const int *domains,
		     daos_rank_list_t *svc_addrs);

/*
 * Called by dmg on the pool service leader to list all pool handles of a pool.
 * Upon successful completion, "buf" returns an array of handle UUIDs if its
 * large enough, while "size" returns the size of all the handle UUIDs assuming
 * "buf" is large enough.
 */
int
dsms_pool_hdl_list(const uuid_t pool_uuid, uuid_t buf, size_t *size);

/*
 * Called by dmg on the pool service leader to evict one or all pool handles of
 * a pool. If "handle_uuid" is NULL, all pool handles of the pool are evicted.
 */
int
dsms_pool_hdl_evict(const uuid_t pool_uuid, const uuid_t handle_uuid);

#endif /* __DAOS_SRV_POOL_H__ */
