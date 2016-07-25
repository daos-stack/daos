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
 * dsms: Pool Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related pool metadata.
 */

#include <daos_srv/daos_m_srv.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libpmemobj.h>
#include <uuid/uuid.h>

#include <daos/btree.h>
#include <daos/mem.h>
#include <daos/pool_map.h>
#include <daos/transport.h>
#include <daos_srv/daos_server.h>

#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

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
	volatile daos_handle_t	kvsh = DAOS_HDL_INVAL;
	uuid_t			target_uuid;
	int			rc = 0;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

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

	TX_BEGIN(mp) {
		struct umem_attr	uma;
		daos_handle_t		h;

		pmemobj_tx_add_range_direct(sb, sizeof(*sb));

		sb->s_magic = SUPERBLOCK_MAGIC;
		uuid_copy(sb->s_pool_uuid, pool_uuid);
		uuid_copy(sb->s_target_uuid, target_uuid);

		/* sb->s_root */
		uma.uma_id = UMEM_CLASS_PMEM;
		uma.uma_u.pmem_pool = mp;
		rc = dbtree_create_inplace(KVS_NV, 0 /* feats */, 4 /* order */,
					   &uma, &sb->s_root, &h);
		if (rc != 0) {
			D_ERROR("failed to create root kvs: %d\n", rc);
			pmemobj_tx_abort(rc);
		}
		kvsh = h;

		rc = dsms_kvs_nv_update(kvsh, POOL_UUID, pool_uuid,
					sizeof(uuid_t));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_update(kvsh, TARGET_UUID, target_uuid,
					sizeof(uuid_t));
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		rc = pmemobj_tx_errno();
		if (rc > 0)
			rc = -DER_NOSPACE;
	} TX_FINALLY {
		if (!daos_handle_is_inval(kvsh))
			dbtree_close(kvsh);
	} TX_END

	if (rc != 0)
		D_GOTO(err_mp, rc);

	uuid_copy(target_uuid_p, target_uuid);
	pmemobj_close(mp);
	return 0;

err_mp:
	pmemobj_close(mp);
	if (remove(path) != 0)
		D_ERROR("failed to remove %s: %d\n", path, errno);
err:
	return rc;
}

int
dsms_pool_create(const uuid_t pool_uuid, const char *path, uuid_t target_uuid)
{
	char	*fpath;
	int	 rc;

	rc = asprintf(&fpath, "%s%s", path, DSM_META_FILE);
	if (rc < 0)
		return -DER_NOMEM;

	rc = mpool_create(fpath, pool_uuid, target_uuid);
	free(fpath);
	return rc;
}

static int
uuid_compare_cb(const void *a, const void *b)
{
	uuid_t *ua = (uuid_t *)a;
	uuid_t *ub = (uuid_t *)b;

	return uuid_compare(*ua, *ub);
}

static int
pool_metadata_init(PMEMobjpool *mp, daos_handle_t kvsh, uint32_t uid,
		   uint32_t gid, uint32_t mode, uint32_t ntargets,
		   uuid_t target_uuids[], const char *group,
		   const daos_rank_list_t *target_addrs, uint32_t ndomains,
		   const int *domains)
{
	struct pool_buf	       *map_buf;
	struct pool_component	map_comp;
	uint32_t		map_version = 1;
	uuid_t		       *uuids;
	int			rc;
	int			i;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK);
	D_ASSERTF(ntargets == target_addrs->rl_nr.num, "ntargets=%u num=%u\n",
		  ntargets, target_addrs->rl_nr.num);

	map_buf = pool_buf_alloc(ntargets + ndomains);
	if (map_buf == NULL)
		return -DER_NOMEM;

	/*
	 * Make a sorted target UUID array to determine target IDs. See the
	 * bsearch() call below.
	 */
	D_ALLOC(uuids, sizeof(uuid_t) * ntargets);
	if (uuids == NULL) {
		pool_buf_free(map_buf);
		return -DER_NOMEM;
	}
	memcpy(uuids, target_uuids, sizeof(uuid_t) * ntargets);
	qsort(uuids, ntargets, sizeof(uuid_t), uuid_compare_cb);

	/* Fill the pool_buf out. */
	for (i = 0; i < ndomains; i++) {
		map_comp.co_type = PO_COMP_TP_RACK;	/* TODO */
		map_comp.co_status = PO_COMP_ST_UP;
		map_comp.co_padding = 0;
		map_comp.co_id = i;
		map_comp.co_rank = 0;
		map_comp.co_ver = map_version;
		map_comp.co_fseq = 1;
		map_comp.co_nr = domains[i];

		rc = pool_buf_attach(map_buf, &map_comp, 1 /* comp_nr */);
		if (rc != 0) {
			D_FREE(uuids, sizeof(uuid_t) * ntargets);
			pool_buf_free(map_buf);
			return rc;
		}
	}
	for (i = 0; i < ntargets; i++) {
		uuid_t *p = bsearch(target_uuids[i], uuids, ntargets,
				    sizeof(uuid_t), uuid_compare_cb);

		map_comp.co_type = PO_COMP_TP_TARGET;
		map_comp.co_status = PO_COMP_ST_UP;
		map_comp.co_padding = 0;
		map_comp.co_id = p - uuids;
		map_comp.co_rank = target_addrs->rl_ranks[i];
		map_comp.co_ver = map_version;
		map_comp.co_fseq = 1;
		map_comp.co_nr = dss_nthreads;

		rc = pool_buf_attach(map_buf, &map_comp, 1 /* comp_nr */);
		if (rc != 0) {
			D_FREE(uuids, sizeof(uuid_t) * ntargets);
			pool_buf_free(map_buf);
			return rc;
		}
	}

	TX_BEGIN(mp) {
		rc = dsms_kvs_nv_update(kvsh, POOL_UID, &uid, sizeof(uid));
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = dsms_kvs_nv_update(kvsh, POOL_GID, &gid, sizeof(gid));
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = dsms_kvs_nv_update(kvsh, POOL_MODE, &mode, sizeof(mode));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_update(kvsh, POOL_MAP_VERSION, &map_version,
					sizeof(map_version));
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = dsms_kvs_nv_update(kvsh, POOL_MAP_BUFFER, map_buf,
					pool_buf_size(map_buf->pb_nr));
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = dsms_kvs_nv_update(kvsh, POOL_MAP_TARGET_UUIDS, uuids,
					sizeof(uuid_t) * ntargets);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(kvsh, POOL_HANDLES, KVS_UV,
					    0 /* feats */, 16 /* order */, mp,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_FINALLY {
		D_FREE(uuids, sizeof(uuid_t) * ntargets);
		pool_buf_free(map_buf);
	} TX_END

	return 0;
}

static int
cont_metadata_init(PMEMobjpool *mp, daos_handle_t rooth)
{
	return dsms_kvs_nv_create_kvs(rooth, CONTAINERS, KVS_UV, 0 /* feats */,
				      16 /* order */, mp, NULL /* kvsh_new */);
}

int
dsms_pool_svc_create(const uuid_t pool_uuid, unsigned int uid, unsigned int gid,
		     unsigned int mode, int ntargets,
		     uuid_t target_uuids[], const char *group,
		     const daos_rank_list_t *target_addrs, int ndomains,
		     const int *domains, daos_rank_list_t *svc_addrs)
{
	PMEMobjpool	       *mp;
	PMEMoid			sb_oid;
	struct superblock      *sb;
	struct umem_attr	uma;
	daos_handle_t		kvsh;
	char		       *path;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	rc = dmgs_tgt_file(pool_uuid, DSM_META_FILE, NULL, &path);
	if (rc)
		D_GOTO(out, rc);

	mp = pmemobj_open(path, MPOOL_LAYOUT);
	if (mp == NULL) {
		D_ERROR("failed to open meta pool %s: %d\n", path, errno);
		D_GOTO(out_path, rc = -DER_INVAL);
	}

	sb_oid = pmemobj_root(mp, sizeof(*sb));
	if (OID_IS_NULL(sb_oid)) {
		D_ERROR("failed to retrieve root object in %s\n", path);
		D_GOTO(out_mp, rc = -DER_INVAL);
	}

	sb = pmemobj_direct(sb_oid);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;
	rc = dbtree_open_inplace(&sb->s_root, &uma, &kvsh);
	if (rc != 0) {
		D_ERROR("failed to open root kvs in %s: %d\n", path, rc);
		D_GOTO(out_mp, rc);
	}

	TX_BEGIN(mp) {
		rc = pool_metadata_init(mp, kvsh, uid, gid, mode, ntargets,
					target_uuids, group, target_addrs,
					ndomains, domains);
		if (rc != 0) {
			D_ERROR("failed to init pool metadata: %d\n", rc);
			pmemobj_tx_abort(rc);
		}

		rc = cont_metadata_init(mp, kvsh);
		if (rc != 0) {
			D_ERROR("failed to init container metadata: %d\n", rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = pmemobj_tx_errno();
		if (rc > 0)
			rc = -DER_NOSPACE;
	} TX_END

	dbtree_close(kvsh);
out_mp:
	pmemobj_close(mp);
out_path:
	free(path);
out:
	return rc;
}

/*
 * Pool service
 *
 * References the mpool descriptor.
 *
 * TODO: p_rwlock currently protects all pool metadata, both volatile and
 * persistent.  When moving to the event-driven model, we shall replace it with
 * a non-blocking implementation.
 */
struct pool_svc {
	daos_list_t		ps_entry;
	uuid_t			ps_uuid;
	struct mpool	       *ps_mpool;
	pthread_rwlock_t	ps_rwlock;	/* see TODO in struct comment */
	pthread_mutex_t		ps_lock;
	int			ps_ref;
	daos_handle_t		ps_handles;	/* pool handle KVS */
	dtp_group_t	       *ps_group;
	struct pool_map	       *ps_map;
};

/*
 * TODO: pool_svc_cache is very similar to mpool_cache. Around the end of 2016,
 * consider if the two could share the same template.
 */
static DAOS_LIST_HEAD(pool_svc_cache);
static pthread_mutex_t pool_svc_cache_lock;

static int
pool_svc_init(const uuid_t uuid, struct pool_svc *svc)
{
	struct mpool	       *mpool;
	struct btr_root	       *kvs;
	size_t			size;
	struct umem_attr	uma;
	int			rc;

	DAOS_INIT_LIST_HEAD(&svc->ps_entry);
	uuid_copy(svc->ps_uuid, uuid);
	svc->ps_ref = 1;

	rc = dsms_mpool_lookup(uuid, &mpool);
	if (rc != 0)
		D_GOTO(err, rc);

	svc->ps_mpool = mpool;

	rc = pthread_rwlock_init(&svc->ps_rwlock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize ps_rwlock: %d\n", rc);
		D_GOTO(err_mp, rc = -DER_NOMEM);
	}

	rc = pthread_mutex_init(&svc->ps_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize ps_lock: %d\n", rc);
		D_GOTO(err_rwlock, rc = -DER_NOMEM);
	}

	rc = dsms_kvs_nv_lookup_ptr(mpool->mp_root, POOL_HANDLES, (void **)&kvs,
				    &size);
	if (rc != 0)
		D_GOTO(err_lock, rc);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mpool->mp_pmem;
	rc = dbtree_open_inplace(kvs, &uma, &svc->ps_handles);
	if (rc != 0) {
		D_ERROR("failed to open pool handle kvs: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	return 0;

err_lock:
	pthread_mutex_destroy(&svc->ps_lock);
err_rwlock:
	pthread_rwlock_destroy(&svc->ps_rwlock);
err_mp:
	dsms_mpool_put(mpool);
err:
	return rc;
}

static void
pool_svc_get(struct pool_svc *svc)
{
	pthread_mutex_lock(&svc->ps_lock);
	svc->ps_ref++;
	pthread_mutex_unlock(&svc->ps_lock);
}

static int
pool_svc_lookup(const uuid_t uuid, struct pool_svc **svc)
{
	struct pool_svc	       *p;
	int			rc;

	pthread_mutex_lock(&pool_svc_cache_lock);

	daos_list_for_each_entry(p, &pool_svc_cache, ps_entry) {
		if (uuid_compare(p->ps_uuid, uuid) == 0) {
			pool_svc_get(p);
			*svc = p;
			D_GOTO(out, rc = 0);
		}
	}

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("failed to allocate pool_svc descriptor\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = pool_svc_init(uuid, p);
	if (rc != 0) {
		D_FREE_PTR(p);
		D_GOTO(out, rc);
	}

	daos_list_add(&p->ps_entry, &pool_svc_cache);
	D_DEBUG(DF_DSMS, "created new pool_svc descriptor %p\n", p);

	*svc = p;
out:
	pthread_mutex_unlock(&pool_svc_cache_lock);
	return rc;
}

static void
pool_svc_put(struct pool_svc *svc)
{
	int is_last_ref = 0;

	pthread_mutex_lock(&svc->ps_lock);
	if (svc->ps_ref == 1)
		is_last_ref = 1;
	else
		svc->ps_ref--;
	pthread_mutex_unlock(&svc->ps_lock);

	if (is_last_ref) {
		pthread_mutex_lock(&pool_svc_cache_lock);
		pthread_mutex_lock(&svc->ps_lock);
		svc->ps_ref--;
		if (svc->ps_ref == 0) {
			D_DEBUG(DF_DSMS, "freeing pool_svc %p\n", svc);
			dbtree_close(svc->ps_handles);
			pthread_mutex_destroy(&svc->ps_lock);
			pthread_rwlock_destroy(&svc->ps_rwlock);
			dsms_mpool_put(svc->ps_mpool);
			daos_list_del(&svc->ps_entry);
			D_FREE_PTR(svc);
		} else {
			pthread_mutex_unlock(&svc->ps_lock);
		}
		pthread_mutex_unlock(&pool_svc_cache_lock);
	}
}

struct pool_attr {
	uint32_t	pa_uid;
	uint32_t	pa_gid;
	uint32_t	pa_mode;
};

static int
pool_attr_read(const struct pool_svc *svc, struct pool_attr *attr)
{
	int rc;

	rc = dsms_kvs_nv_lookup(svc->ps_mpool->mp_root, POOL_UID, &attr->pa_uid,
				sizeof(uint32_t));
	if (rc != 0)
		return rc;

	rc = dsms_kvs_nv_lookup(svc->ps_mpool->mp_root, POOL_GID, &attr->pa_gid,
				sizeof(uint32_t));
	if (rc != 0)
		return rc;

	rc = dsms_kvs_nv_lookup(svc->ps_mpool->mp_root, POOL_MODE,
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

/*
 * Retrieve the address of the persistent pool map buffer and the pool map
 * version into "map_buf" and "map_version", respectively.
 */
static int
map_retrieve(const struct pool_svc *svc, struct pool_buf **map_buf,
	     uint32_t *map_version)
{
	struct pool_buf	       *buf;
	size_t			buf_size;
	uint32_t		version;
	int			rc;

	rc = dsms_kvs_nv_lookup(svc->ps_mpool->mp_root, POOL_MAP_VERSION,
				&version, sizeof(version));
	if (rc != 0)
		return rc;

	/* Look up the address of the persistent pool map buffer. */
	rc = dsms_kvs_nv_lookup_ptr(svc->ps_mpool->mp_root, POOL_MAP_BUFFER,
				    (void **)&buf, &buf_size);
	if (rc != 0)
		return rc;

	D_DEBUG(DF_DSMS, "version=%u ntargets=%u ndomains=%u\n", version,
		buf->pb_target_nr, buf->pb_domain_nr);

	*map_buf = buf;
	*map_version = version;
	return 0;
}

struct pool_connect_cb_arg {
	struct pool_svc	       *pcc_svc;
	int			pcc_skip_update;
};

static int
pool_connect_cb(const struct dtp_bulk_cb_info *cb_info)
{
	struct dtp_bulk_desc	       *desc = cb_info->bci_bulk_desc;
	struct pool_connect_cb_arg     *arg = cb_info->bci_arg;
	struct pool_connect_in	       *in = dtp_req_get(desc->bd_rpc);
	struct pool_connect_out	       *out = dtp_reply_get(desc->bd_rpc);
	struct pool_hdl			hdl;
	struct pool_svc		       *svc = arg->pcc_svc;
	int				rc = cb_info->bci_rc;

	if (rc != 0) {
		D_ERROR("failed to transfer pool map to client: %d\n", rc);
		D_GOTO(out, rc);
	}

	if (arg->pcc_skip_update)
		D_GOTO(out, rc = 0);

	hdl.ph_capas = in->pci_capas;

	/* TX_BEGIN */
	rc = dsms_kvs_uv_update(svc->ps_handles, in->pci_pool_hdl, &hdl,
				sizeof(hdl));
	/* TX_END */

out:
	dtp_bulk_free(desc->bd_local_hdl);
	D_DEBUG(DF_DSMS, "replying rpc %p with %d\n", desc->bd_rpc, rc);
	out->pco_ret = rc;
	rc = dtp_reply_send(desc->bd_rpc);
	dtp_req_decref(desc->bd_rpc);
	D_FREE_PTR(arg);
	pthread_rwlock_unlock(&svc->ps_rwlock);
	pool_svc_put(svc);
	return rc;
}

int
dsms_hdlr_pool_connect(dtp_rpc_t *rpc)
{
	struct pool_connect_in	       *pci;
	struct pool_connect_out	       *pco;
	struct pool_svc		       *svc;
	struct pool_attr		attr;
	struct pool_hdl			hdl;
	struct pool_buf		       *map_buf;
	size_t				map_buf_size;
	daos_iov_t			map_iov;
	daos_sg_list_t			map_sgl;
	dtp_bulk_t			map_bulk;
	struct dtp_bulk_desc		map_desc;
	dtp_bulk_opid_t			map_opid;
	struct pool_connect_cb_arg     *arg;
	daos_size_t			client_bulk_size;
	int				rc;

	D_DEBUG(DF_DSMS, "processing rpc %p\n", rpc);

	pci = dtp_req_get(rpc);
	D_ASSERT(pci != NULL);
	pco = dtp_reply_get(rpc);

	rc = pool_svc_lookup(pci->pci_pool, &svc);
	if (rc != 0)
		D_GOTO(err, rc);

	pthread_rwlock_wrlock(&svc->ps_rwlock);

	rc = pool_attr_read(svc, &attr);
	if (rc != 0)
		D_GOTO(err_lock, rc);

	if (!permitted(&attr, pci->pci_uid, pci->pci_gid,
		       pci->pci_capas)) {
		D_ERROR("refusing connect attempt for uid %u gid %u "DF_X64"\n",
			pci->pci_uid, pci->pci_gid, pci->pci_capas);
		D_GOTO(err_lock, rc = -DER_NO_PERM);
	}

	/*
	 * Fill pco_mode here, since doing so in pool_connect_cb() would
	 * require either "attr" or another KVS lookup.
	 */
	pco->pco_mode = attr.pa_mode;

	/* Prepare "arg" for pool_connect_cb(). */
	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		D_ERROR("failed to allocate arg for bulk callback\n");
		D_GOTO(err_lock, rc = -DER_NOMEM);
	}

	arg->pcc_svc = svc;
	arg->pcc_skip_update = 0;

	/* Check existing pool handles. */
	rc = dsms_kvs_uv_lookup(svc->ps_handles, pci->pci_pool_hdl, &hdl,
				sizeof(hdl));
	if (rc == 0) {
		if (hdl.ph_capas == pci->pci_capas) {
			/*
			 * We may skip the update transaction and only do the
			 * pool map transfer.
			 */
			arg->pcc_skip_update = 1;
		} else {
			/* The existing one does not match the new one. */
			D_ERROR("found conflicting pool handle\n");
			D_GOTO(err_arg, rc = -DER_EXIST);
		}
	} else if (rc != -DER_NONEXIST) {
		D_GOTO(err_arg, rc);
	}

	/*
	 * If successful, this stores the address of the persistent pool map
	 * buffer into "map_buf".
	 */
	rc = map_retrieve(svc, &map_buf, &pco->pco_pool_map_version);
	if (rc != 0) {
		D_ERROR("failed to read pool map: %d\n", rc);
		D_GOTO(err_arg, rc);
	}

	map_buf_size = pool_buf_size(map_buf->pb_nr);

	/* Check if the client bulk buffer is large enough. */
	rc = dtp_bulk_get_len(pci->pci_pool_map_bulk, &client_bulk_size);
	if (rc != 0)
		D_GOTO(err_arg, rc);
	if (client_bulk_size < map_buf_size) {
		D_ERROR("client pool map buffer ("DF_U64") < required (%ld)\n",
			client_bulk_size, map_buf_size);
		pco->pco_pool_map_buf_size = map_buf_size;
		D_GOTO(err_arg, rc = -DER_TRUNC);
	}

	map_iov.iov_buf = map_buf;
	map_iov.iov_buf_len = map_buf_size;
	map_iov.iov_len = map_iov.iov_buf_len;
	map_sgl.sg_nr.num = 1;
	map_sgl.sg_nr.num_out = 0;
	map_sgl.sg_iovs = &map_iov;

	rc = dtp_bulk_create(rpc->dr_ctx, &map_sgl, DTP_BULK_RO, &map_bulk);
	if (rc != 0)
		D_GOTO(err_arg, rc);

	/* Prepare "map_desc" for dtp_bulk_transfer(). */
	dtp_req_addref(rpc);
	map_desc.bd_rpc = rpc;
	map_desc.bd_bulk_op = DTP_BULK_PUT;
	map_desc.bd_remote_hdl = pci->pci_pool_map_bulk;
	map_desc.bd_remote_off = 0;
	map_desc.bd_local_hdl = map_bulk;
	map_desc.bd_local_off = 0;
	map_desc.bd_len = map_iov.iov_len;

	/*
	 * Transfer the pool map to the client before adding the pool handle,
	 * so that we don't need to worry about rolling back the transaction
	 * when the tranfer fails. The client has already been authenticated
	 * and authorized at this point. If an error occurs after the transfer
	 * completes, then we simply return the error and the client will throw
	 * its pool_buf away.
	 */
	rc = dtp_bulk_transfer(&map_desc, pool_connect_cb, arg, &map_opid);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return 0;

err_rpc:
	dtp_req_decref(map_desc.bd_rpc);
	dtp_bulk_free(map_bulk);
err_arg:
	D_FREE_PTR(arg);
err_lock:
	pthread_rwlock_unlock(&svc->ps_rwlock);
	pool_svc_put(svc);
err:
	D_DEBUG(DF_DSMS, "replying rpc %p with %d\n", rpc, rc);
	pco->pco_ret = rc;
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc)
{
	struct pool_disconnect_in      *pdi;
	struct pool_disconnect_out     *pdo;
	struct pool_svc		       *svc;
	int				rc;

	D_DEBUG(DF_DSMS, "processing rpc %p\n", rpc);
	pdi = dtp_req_get(rpc);
	D_ASSERT(pdi != NULL);

	rc = pool_svc_lookup(pdi->pdi_pool, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->ps_rwlock);

	/* TX BEGIN */

	rc = dsms_kvs_uv_delete(svc->ps_handles, pdi->pdi_pool_hdl);
	if (rc == -DER_NONEXIST)
		rc = 0;

	/* TX END */

	pthread_rwlock_unlock(&svc->ps_rwlock);
	pool_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "replying rpc %p with %d\n", rpc, rc);
	pdo = dtp_reply_get(rpc);
	D_ASSERT(pdo != NULL);
	pdo->pdo_ret = rc;
	return dtp_reply_send(rpc);
}

int
dsms_module_pool_init(void)
{
	int rc;

	rc = pthread_mutex_init(&pool_svc_cache_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize pool_svc cache lock: %d\n", rc);
		rc = -DER_NOMEM;
	}

	return rc;
}

void
dsms_module_pool_fini(void)
{
	pthread_mutex_destroy(&pool_svc_cache_lock);
}
