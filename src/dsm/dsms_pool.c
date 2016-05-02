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
#include "dsms_storage.h"

/* TODO: Move these two path generators to daos_mgmt_srv.h. */

static void
print_vos_path(const char *path, const uuid_t pool_uuid, char *buf, size_t size)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(pool_uuid, uuid_str);
	rc = snprintf(buf, size, "%s/%s-vos", path, uuid_str);
	D_ASSERT(rc < size);
}

static void
print_meta_path(const char *path, const uuid_t pool_uuid, char *buf,
		size_t size)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(pool_uuid, uuid_str);
	rc = snprintf(buf, size, "%s/%s-meta", path, uuid_str);
	D_ASSERT(rc < size);
}

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
pool_handles_create(PMEMobjpool *mp, struct btr_root *kvs)
{
	struct umem_attr	uma;
	daos_handle_t		kvsh;
	int			rc;

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;
	rc = dbtree_create_inplace(KVS_UV, 0 /* feats */, 16 /* order */, &uma,
				   kvs, &kvsh);
	if (rc != 0) {
		D_ERROR("failed to create pool_handles kvs: %d\n", rc);
		return rc;
	}

	dbtree_close(kvsh);
	return 0;
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
	struct btr_root		pool_handles;
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

	rc = pool_handles_create(mp, &pool_handles);
	if (rc != 0)
		D_GOTO(out_domains_p, rc);

	rc = dsms_kvs_nv_update(kvsh, POOL_HANDLES, &pool_handles,
				sizeof(pool_handles));
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

int
dsms_hdlr_pool_connect(dtp_rpc_t *rpc)
{
	return 0;
}

int
dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc)
{
	return 0;
}
