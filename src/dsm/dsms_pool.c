/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsms: Pool Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related pool metadata.
 */

#include <daos_srv/daos_m_srv.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <libpmemobj.h>
#include <uuid/uuid.h>

#include <daos/btree.h>
#include <daos/mem.h>
#include <daos/transport.h>
#include <daos_srv/vos.h>

#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

/*
 * Create the root KVS in "*kvs" and add the pool and target UUIDs.
 */
static int
root_create(PMEMobjpool *mp, const uuid_t pool_uuid,
	    const uuid_t target_uuid, struct btr_root *kvs)
{
	struct umem_attr	uma;
	daos_handle_t		kvsh;
	int			rc;

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;
	rc = dbtree_create_inplace(KVS_NV, 0 /* feats */, 4 /* order */, &uma,
				   kvs, &kvsh);
	if (rc != 0) {
		D_ERROR("failed to create root kvs: %d\n", rc);
		D_GOTO(err, rc);
	}

	rc = dsms_kvs_nv_update(kvsh, POOL_UUID, pool_uuid, sizeof(uuid_t));
	if (rc != 0)
		D_GOTO(err_kvs, rc);

	rc = dsms_kvs_nv_update(kvsh, TARGET_UUID, target_uuid, sizeof(uuid_t));
	if (rc != 0)
		D_GOTO(err_kvs, rc);

	rc = dbtree_close(kvsh);
	D_ASSERTF(rc == 0, "%d\n", rc);

	return 0;

err_kvs:
	dbtree_destroy(kvsh);
err:
	return rc;
}

/*
 * Create the mpool, create the root kvs, create the superblock, and return the
 * target UUID.
 */
static int
mpool_create(const char *path, const uuid_t pool_uuid, uuid_t target_uuid_p)
{
	PMEMobjpool	       *mp;
	PMEMoid			sb_oid;
	struct superblock      *sb;
	uuid_t			target_uuid;
	int			rc;

	D_DEBUG(DF_DSMS, "creating mpool %s\n", path);

	uuid_generate(target_uuid);

	mp = pmemobj_create(path, MPOOL_LAYOUT, MPOOL_SIZE, 0666);
	if (mp == NULL) {
		D_ERROR("failed to create meta pool in %s: %d\n", path, errno);
		D_GOTO(err, rc = -DER_NOSPACE);
	}

	sb_oid = pmemobj_root(mp, sizeof(*sb));
	if (OID_IS_NULL(sb_oid)) {
		D_ERROR("failed to allocate root object in %s\n", path);
		D_GOTO(err_mp, rc = -DER_NOSPACE);
	}

	sb = pmemobj_direct(sb_oid);
	sb->s_magic = SUPERBLOCK_MAGIC;
	uuid_copy(sb->s_pool_uuid, pool_uuid);
	uuid_copy(sb->s_target_uuid, target_uuid);

	rc = root_create(mp, pool_uuid, target_uuid, &sb->s_root);
	if (rc != 0)
		D_GOTO(err_mp, rc);

	uuid_copy(target_uuid_p, target_uuid);
	pmemobj_close(mp);
	return 0;

err_mp:
	/* dmg will remove the mpool file anyway. */
	pmemobj_close(mp);
err:
	return rc;
}

/*
 * Create the vos pool.
 */
static int
vpool_create(const char *path, const uuid_t pool_uuid)
{
	daos_handle_t	vph;
	int		rc;

	D_DEBUG(DF_DSMS, "creating vos pool %s\n", path);

	/* A zero size accommodates the existing file created by dmg. */
	rc = vos_pool_create(path, (unsigned char *)pool_uuid, 0 /* size */,
			     &vph, NULL /* event */);
	if (rc != 0) {
		D_ERROR("failed to create vos pool in %s: %d\n", path, rc);
		return rc;
	}

	rc = vos_pool_close(vph, NULL /* event */);
	D_ASSERTF(rc == 0, "%d\n", rc);

	return 0;
}

/*
 * This code path does not need libpmemobj transactions or persistent resource
 * cleanups.
 */
int
dsms_pool_create(const uuid_t pool_uuid, const char *path, uuid_t target_uuid)
{
	char	filename[4096];
	int	rc;

	print_vos_path(path, pool_uuid, filename, sizeof(filename));
	rc = vpool_create(filename, pool_uuid);
	if (rc != 0)
		return rc;

	print_meta_path(path, pool_uuid, filename, sizeof(filename));
	return mpool_create(filename, pool_uuid, target_uuid);
}

/*
 * Create the pool handle KVS.
 */
static int
pool_handles_create(PMEMobjpool *mp, daos_handle_t rooth)
{
	struct btr_root		dummy;
	struct btr_root	       *kvs;
	struct umem_attr	uma;
	daos_handle_t		kvsh;
	size_t			size;
	int			rc;

	memset(&dummy, 0, sizeof(dummy));

	rc = dsms_kvs_nv_update(rooth, POOL_HANDLES, &dummy, sizeof(dummy));
	if (rc != 0)
		D_GOTO(err, rc);

	rc = dsms_kvs_nv_lookup_ptr(rooth, POOL_HANDLES, (void **)&kvs, &size);
	if (rc != 0)
		D_GOTO(err_nv, rc);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;

	rc = dbtree_create_inplace(KVS_UV, 0 /* feats */, 16 /* order */, &uma,
				   kvs, &kvsh);
	if (rc != 0) {
		D_ERROR("failed to create pool_handles kvs: %d\n", rc);
		D_GOTO(err_nv, rc);
	}

	dbtree_close(kvsh);
	return 0;

err_nv:
	dsms_kvs_nv_delete(rooth, POOL_HANDLES);
err:
	return rc;
}

static int
pool_metadata_init(PMEMobjpool *mp, daos_handle_t kvsh, uint32_t uid,
		   uint32_t gid, uint32_t mode, uint32_t ntargets,
		   const uuid_t target_uuids, const char *group,
		   const daos_rank_list_t *target_addrs, uint32_t ndomains,
		   const int *domains)
{
	struct pool_map_target *targets_p;
	struct pool_map_domain *domains_p;
	uint64_t		version = 1;
	uuid_t		       *target_uuid = (uuid_t *)target_uuids;
	int			rc;
	int			i;

	rc = dsms_kvs_nv_update(kvsh, POOL_UID, &uid, sizeof(uid));
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dsms_kvs_nv_update(kvsh, POOL_GID, &gid, sizeof(gid));
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dsms_kvs_nv_update(kvsh, POOL_MODE, &mode, sizeof(mode));
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * TODO: Verify the number of leaves indicated by "domains" matches
	 * "ntargets".
	 */

	D_ALLOC(targets_p, sizeof(*targets_p) * ntargets);
	if (targets_p == NULL)
		D_GOTO(out, rc);
	for (i = 0; i < ntargets; i++) {
		uuid_copy(targets_p[i].mt_uuid, target_uuid[i]);
		targets_p[i].mt_version = 1;
		targets_p[i].mt_fseq = 1;
		targets_p[i].mt_status = 0;	/* TODO */
	}

	D_ALLOC(domains_p, sizeof(*domains_p) * ndomains);
	if (domains_p == NULL)
		D_GOTO(out_targets_p, rc);
	for (i = 0; i < ndomains; i++) {
		domains_p[i].md_version = 1;
		domains_p[i].md_nchildren = domains[i];
	}

	rc = dsms_kvs_nv_update(kvsh, POOL_MAP_VERSION, &version,
				sizeof(version));
	if (rc != 0)
		D_GOTO(out_domains_p, rc);
	rc = dsms_kvs_nv_update(kvsh, POOL_MAP_NTARGETS, &ntargets,
				sizeof(ntargets));
	if (rc != 0)
		D_GOTO(out_domains_p, rc);
	rc = dsms_kvs_nv_update(kvsh, POOL_MAP_NDOMAINS, &ndomains,
				sizeof(ndomains));
	if (rc != 0)
		D_GOTO(out_domains_p, rc);
	rc = dsms_kvs_nv_update(kvsh, POOL_MAP_TARGETS, targets_p,
				sizeof(*targets_p) * ntargets);
	if (rc != 0)
		D_GOTO(out_domains_p, rc);
	rc = dsms_kvs_nv_update(kvsh, POOL_MAP_DOMAINS, domains_p,
				sizeof(*domains_p) * ndomains);
	if (rc != 0)
		D_GOTO(out_domains_p, rc);

	rc = pool_handles_create(mp, kvsh);
	if (rc != 0)
		D_GOTO(out_domains_p, rc);

out_domains_p:
	D_FREE(domains_p, sizeof(*domains_p) * ndomains);
out_targets_p:
	D_FREE(targets_p, sizeof(*targets_p) * ntargets);
out:
	return rc;
}

int
dsms_pool_svc_create(const uuid_t pool_uuid, unsigned int uid, unsigned int gid,
		     unsigned int mode, int ntargets, const uuid_t target_uuids,
		     const char *group, const daos_rank_list_t *target_addrs,
		     int ndomains, const int *domains, const char *path,
		     daos_rank_list_t *svc_addrs)
{
	PMEMobjpool	       *mp;
	PMEMoid			sb_oid;
	struct superblock      *sb;
	struct umem_attr	uma;
	daos_handle_t		kvsh;
	char			filename[4096];
	int			rc;

	print_meta_path(path, pool_uuid, filename, sizeof(filename));

	mp = pmemobj_open(filename, MPOOL_LAYOUT);
	if (mp == NULL) {
		D_ERROR("failed to open meta pool %s: %d\n", filename, errno);
		D_GOTO(out, rc = -DER_INVAL);
	}

	sb_oid = pmemobj_root(mp, sizeof(*sb));
	if (OID_IS_NULL(sb_oid)) {
		D_ERROR("failed to retrieve root object in %s\n", filename);
		D_GOTO(out_mp, rc = -DER_INVAL);
	}
	sb = pmemobj_direct(sb_oid);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;
	rc = dbtree_open_inplace(&sb->s_root, &uma, &kvsh);
	if (rc != 0) {
		D_ERROR("failed to open root kvs in %s: %d\n", filename, rc);
		D_GOTO(out_mp, rc);
	}

	rc = pool_metadata_init(mp, kvsh, uid, gid, mode, ntargets,
				target_uuids, group, target_addrs, ndomains,
				domains);

	dbtree_close(kvsh);
out_mp:
	pmemobj_close(mp);
out:
	return rc;
}

/*
 * Pool metadata descriptor
 *
 * References the mpool descriptor.
 *
 * TODO: p_rwlock currently protects all pool metadata, both volatile and
 * persistent.  When moving to the event-driven model, we shall replace it with
 * a non-blocking implementation.
 */
struct pool {
	daos_list_t		p_entry;
	uuid_t			p_uuid;
	struct mpool	       *p_mpool;
	pthread_rwlock_t	p_rwlock;	/* see TODO in struct comment */
	pthread_mutex_t		p_lock;
	int			p_ref;
	daos_handle_t		p_handles;	/* pool handle KVS */
};

/*
 * TODO: pool_cache is very similar to mpool_cache. Around the end of 2016,
 * consider if the two could share the same template.
 */
static DAOS_LIST_HEAD(pool_cache);
static pthread_mutex_t pool_cache_lock;

static int
pool_init(const uuid_t uuid, struct pool *pool)
{
	struct mpool	       *mpool;
	struct btr_root	       *kvs;
	size_t			size;
	struct umem_attr	uma;
	int			rc;

	DAOS_INIT_LIST_HEAD(&pool->p_entry);
	uuid_copy(pool->p_uuid, uuid);
	pool->p_ref = 1;

	rc = dsms_mpool_lookup(uuid, &mpool);
	if (rc != 0)
		D_GOTO(err, rc);

	pool->p_mpool = mpool;

	rc = pthread_rwlock_init(&pool->p_rwlock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize p_rwlock: %d\n", rc);
		D_GOTO(err, rc = -DER_NOMEM);
	}

	rc = pthread_mutex_init(&pool->p_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize p_lock: %d\n", rc);
		D_GOTO(err_rwlock, rc = -DER_NOMEM);
	}

	rc = dsms_kvs_nv_lookup_ptr(mpool->mp_root, POOL_HANDLES, (void **)&kvs,
				    &size);
	if (rc != 0)
		D_GOTO(err_rwlock, rc);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mpool->mp_pmem;
	rc = dbtree_open_inplace(kvs, &uma, &pool->p_handles);
	if (rc != 0) {
		D_ERROR("failed to open pool handle kvs: %d\n", rc);
		D_GOTO(err_rwlock, rc);
	}

	return 0;

err_rwlock:
	pthread_rwlock_destroy(&pool->p_rwlock);
err:
	return rc;
}

static void
pool_get(struct pool *pool)
{
	pthread_mutex_lock(&pool->p_lock);
	pool->p_ref++;
	pthread_mutex_unlock(&pool->p_lock);
}

static int
pool_lookup(const uuid_t uuid, struct pool **pool)
{
	struct pool    *p;
	int		rc;

	pthread_mutex_lock(&pool_cache_lock);

	daos_list_for_each_entry(p, &pool_cache, p_entry) {
		if (uuid_compare(p->p_uuid, uuid) == 0) {
			pool_get(p);
			*pool = p;
			D_GOTO(out, rc = 0);
		}
	}

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("failed to allocate pool descriptor\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = pool_init(uuid, p);
	if (rc != 0) {
		D_FREE_PTR(p);
		D_GOTO(out, rc);
	}

	daos_list_add(&p->p_entry, &pool_cache);
	D_DEBUG(DF_DSMS, "created new pool descriptor %p\n", pool);

	*pool = p;
out:
	pthread_mutex_unlock(&pool_cache_lock);
	return rc;
}

static void
pool_put(struct pool *pool)
{
	int is_last_ref = 0;

	pthread_mutex_lock(&pool->p_lock);
	if (pool->p_ref == 1)
		is_last_ref = 1;
	else
		pool->p_ref--;
	pthread_mutex_unlock(&pool->p_lock);

	if (is_last_ref) {
		pthread_mutex_lock(&pool_cache_lock);
		pthread_mutex_lock(&pool->p_lock);
		pool->p_ref--;
		if (pool->p_ref == 0) {
			D_DEBUG(DF_DSMS, "freeing pool descriptor %p\n", pool);
			dbtree_close(pool->p_handles);
			pthread_mutex_destroy(&pool->p_lock);
			pthread_rwlock_destroy(&pool->p_rwlock);
			dsms_mpool_put(pool->p_mpool);
			daos_list_del(&pool->p_entry);
			D_FREE_PTR(pool);
		} else {
			pthread_mutex_unlock(&pool->p_lock);
		}
		pthread_mutex_unlock(&pool_cache_lock);
	}
}

struct pool_attr {
	uint32_t	pa_uid;
	uint32_t	pa_gid;
	uint32_t	pa_mode;
};

static int
pool_attr_read(const struct pool *pool, struct pool_attr *attr)
{
	int rc;

	rc = dsms_kvs_nv_lookup(pool->p_mpool->mp_root, POOL_UID, &attr->pa_uid,
				sizeof(uint32_t));
	if (rc != 0)
		return rc;

	rc = dsms_kvs_nv_lookup(pool->p_mpool->mp_root, POOL_GID, &attr->pa_gid,
				sizeof(uint32_t));
	if (rc != 0)
		return rc;

	rc = dsms_kvs_nv_lookup(pool->p_mpool->mp_root, POOL_MODE,
				&attr->pa_mode, sizeof(uint32_t));
	if (rc != 0)
		return rc;

	return 0;
}

static int
permitted(const struct pool_attr *attr, uint32_t uid, uint32_t gid,
	  uint64_t capas)
{
	/* TODO */
	return 1;
}

int
dsms_hdlr_pool_connect(dtp_rpc_t *rpc)
{
	struct pool_connect_in	       *in = rpc->dr_input;
	struct pool_connect_out	       *out = rpc->dr_output;
	struct pool		       *pool;
	struct pool_attr		attr;
	struct pool_hdl			hdl;
	int				rc;

	D_DEBUG(DF_DSMS, "processing rpc %p\n", rpc);

	rc = pool_lookup(in->pci_pool, &pool);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&pool->p_rwlock);

	rc = pool_attr_read(pool, &attr);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	if (!permitted(&attr, in->pci_uid, in->pci_gid, in->pci_capas)) {
		D_ERROR("refusing connect attempt for uid %u gid %u "DF_X64"\n",
			in->pci_uid, in->pci_gid, in->pci_capas);
		D_GOTO(out_lock, rc = -DER_NO_PERM);
	}

	rc = dsms_kvs_uv_lookup(pool->p_handles, in->pci_pool_hdl, &hdl,
				sizeof(hdl));
	if (rc == 0) {
		if (hdl.ph_capas != in->pci_capas) {
			/* The existing one does not match the new one. */
			D_ERROR("found conflicting pool handle\n");
			rc = -DER_EXIST;
		}
		D_GOTO(out_lock, rc);
	}

	hdl.ph_capas = in->pci_capas;

	/* TX BEGIN */

	rc = dsms_kvs_uv_update(pool->p_handles, in->pci_pool_hdl, &hdl,
				sizeof(hdl));

	/* TX END */

out_lock:
	pthread_rwlock_unlock(&pool->p_rwlock);
	pool_put(pool);
out:
	D_DEBUG(DF_DSMS, "replying rpc %p with %d\n", rpc, rc);
	out->pco_rc = rc;
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc)
{
	struct pool_disconnect_in      *in = rpc->dr_input;
	struct pool_disconnect_out     *out = rpc->dr_output;
	struct pool		       *pool;
	int				rc;

	D_DEBUG(DF_DSMS, "processing rpc %p\n", rpc);

	rc = pool_lookup(in->pdi_pool, &pool);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&pool->p_rwlock);

	/* TX BEGIN */

	rc = dsms_kvs_uv_delete(pool->p_handles, in->pdi_pool_hdl);
	if (rc == -DER_NONEXIST)
		rc = 0;

	/* TX END */

	pthread_rwlock_unlock(&pool->p_rwlock);
	pool_put(pool);
out:
	D_DEBUG(DF_DSMS, "replying rpc %p with %d\n", rpc, rc);
	out->pdo_rc = rc;
	return dtp_reply_send(rpc);
}

int
dsms_pool_init(void)
{
	int rc;

	rc = pthread_mutex_init(&pool_cache_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize pool cache lock: %d\n", rc);
		rc = -DER_NOMEM;
	}

	return rc;
}

void
dsms_pool_fini(void)
{
	pthread_mutex_destroy(&pool_cache_lock);
}
