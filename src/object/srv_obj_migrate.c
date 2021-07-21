/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * migrate: migrate objects between servers.
 *
 */
#define D_LOGFAC	DD_FAC(server)

#include <daos_srv/pool.h>
#include <daos/btree_class.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/srv_csum.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

/* Max inflight data size per xstream */
/* Set the total inflight size to be 25% of MAX DMA size for
 * the moment, will adjust it later if needed.
 */
#define MIGRATE_MAX_SIZE	(1 << 28)
/* Max migrate ULT number on the server */
#define MIGRATE_MAX_ULT		8192

struct migrate_one {
	daos_key_t		 mo_dkey;
	uuid_t			 mo_pool_uuid;
	uuid_t			 mo_cont_uuid;
	daos_unit_oid_t		 mo_oid;
	daos_epoch_t		 mo_obj_punch_eph;
	daos_epoch_t		 mo_dkey_punch_eph;
	daos_epoch_t		 mo_epoch;
	daos_epoch_t		 mo_update_epoch;
	daos_iod_t		*mo_iods;
	struct dcs_iod_csums	*mo_iods_csums;
	daos_iod_t		*mo_punch_iods;
	daos_epoch_t		*mo_akey_punch_ephs;
	daos_epoch_t		 mo_rec_punch_eph;
	d_sg_list_t		*mo_sgls;
	struct daos_oclass_attr	 mo_oca;
	unsigned int		 mo_iod_num;
	unsigned int		 mo_punch_iod_num;
	unsigned int		 mo_iod_alloc_num;
	unsigned int		 mo_rec_num;
	uint64_t		 mo_size;
	uint64_t		 mo_version;
	uint32_t		 mo_pool_tls_version;
	d_list_t		 mo_list;
	d_iov_t			 mo_csum_iov;
};

struct migrate_obj_key {
	daos_unit_oid_t oid;
	daos_epoch_t	eph;
	uint32_t	tgt_idx;
};

/* Argument for container iteration and migrate */
struct iter_cont_arg {
	struct migrate_pool_tls *pool_tls;
	uuid_t			pool_uuid;
	uuid_t			pool_hdl_uuid;
	uuid_t			cont_uuid;
	uuid_t			cont_hdl_uuid;
	struct tree_cache_root	*cont_root;
	unsigned int		yield_freq;
	unsigned int		obj_cnt;
	uint64_t		*snaps;
	uint32_t		snap_cnt;
	uint32_t		version;
	uint32_t		ref_cnt;
};

/* Argument for object iteration and migrate */
struct iter_obj_arg {
	uuid_t			pool_uuid;
	uuid_t			cont_uuid;
	daos_unit_oid_t		oid;
	daos_epoch_t		epoch;
	unsigned int		shard;
	unsigned int		tgt_idx;
	uint64_t		*snaps;
	uint32_t		snap_cnt;
	uint32_t		version;
};

static int
obj_tree_destory_cb(daos_handle_t ih, d_iov_t *key_iov,
		    d_iov_t *val_iov, void *data)
{
	struct tree_cache_root *root = val_iov->iov_buf;
	int			rc;

	rc = dbtree_destroy(root->root_hdl, NULL);
	if (rc)
		D_ERROR("dbtree_destroy, cont "DF_UUID" failed: "DF_RC"\n",
			DP_UUID(*(uuid_t *)key_iov->iov_buf), DP_RC(rc));

	return rc;
}

int
obj_tree_destroy(daos_handle_t btr_hdl)
{
	int	rc;

	rc = dbtree_iterate(btr_hdl, DAOS_INTENT_PUNCH, false,
			    obj_tree_destory_cb, NULL);
	if (rc) {
		D_ERROR("dbtree iterate failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = dbtree_destroy(btr_hdl, NULL);

out:
	return rc;
}

/* Create tree root by key_iov */
static int
tree_cache_create_internal(daos_handle_t toh, unsigned int tree_class,
			   d_iov_t *key_iov, struct tree_cache_root **rootp)
{
	d_iov_t			val_iov;
	struct umem_attr	uma;
	struct tree_cache_root	root;
	struct btr_root		*broot;
	int			rc;

	D_ALLOC_PTR(broot);
	if (broot == NULL)
		return -DER_NOMEM;

	memset(&root, 0, sizeof(root));
	root.root_hdl = DAOS_HDL_INVAL;
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(tree_class, BTR_FEAT_DIRECT_KEY, 32,
				   &uma, broot, &root.root_hdl);
	if (rc) {
		D_ERROR("failed to create rebuild tree: "DF_RC"\n", DP_RC(rc));
		D_FREE(broot);
		D_GOTO(out, rc);
	}

	d_iov_set(&val_iov, &root, sizeof(root));
	rc = dbtree_update(toh, key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);

	d_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(toh, key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);

	*rootp = val_iov.iov_buf;
	D_ASSERT(*rootp != NULL);
out:
	if (rc < 0 && daos_handle_is_valid(root.root_hdl))
		dbtree_destroy(root.root_hdl, NULL);
	return rc;
}

static int
container_tree_create(daos_handle_t toh, uuid_t uuid,
		      struct tree_cache_root **rootp)
{
	d_iov_t	key_iov;

	d_iov_set(&key_iov, uuid, sizeof(uuid_t));

	return tree_cache_create_internal(toh, DBTREE_CLASS_NV, &key_iov,
					  rootp);
}

int
obj_tree_lookup(daos_handle_t toh, uuid_t co_uuid, daos_unit_oid_t oid,
		d_iov_t *val_iov)
{
	struct tree_cache_root	*cont_root = NULL;
	d_iov_t			key_iov;
	d_iov_t			tmp_iov;
	int			rc;

	/* locate the container first */
	d_iov_set(&key_iov, co_uuid, sizeof(uuid_t));
	d_iov_set(&tmp_iov, NULL, 0);
	rc = dbtree_lookup(toh, &key_iov, &tmp_iov);
	if (rc < 0) {
		if (rc != -DER_NONEXIST)
			D_ERROR("lookup cont "DF_UUID" failed, "DF_RC"\n",
				DP_UUID(co_uuid), DP_RC(rc));
		else
			D_DEBUG(DB_TRACE, "Container "DF_UUID" not exist\n",
				DP_UUID(co_uuid));
		return rc;
	}

	cont_root = tmp_iov.iov_buf;
	/* Then try to insert the object under the container */
	d_iov_set(&key_iov, &oid, sizeof(oid));
	rc = dbtree_lookup(cont_root->root_hdl, &key_iov, val_iov);
	if (rc < 0) {
		if (rc != -DER_NONEXIST)
			D_ERROR(DF_UUID"/"DF_UOID" "DF_RC"\n",
				DP_UUID(co_uuid), DP_UOID(oid), DP_RC(rc));
		else
			D_DEBUG(DB_TRACE, DF_UUID"/"DF_UOID " not exist\n",
				DP_UUID(co_uuid), DP_UOID(oid));
	}

	return rc;
}

int
obj_tree_insert(daos_handle_t toh, uuid_t co_uuid, daos_unit_oid_t oid,
		d_iov_t *val_iov)
{
	struct tree_cache_root	*cont_root = NULL;
	d_iov_t			key_iov;
	d_iov_t			tmp_iov;
	int			rc;

	/* locate the container first */
	d_iov_set(&key_iov, co_uuid, sizeof(uuid_t));
	d_iov_set(&tmp_iov, NULL, 0);
	rc = dbtree_lookup(toh, &key_iov, &tmp_iov);
	if (rc < 0) {
		if (rc != -DER_NONEXIST) {
			D_ERROR("lookup cont "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(co_uuid), DP_RC(rc));
			return rc;
		}

		D_DEBUG(DB_TRACE, "Create cont "DF_UUID" tree\n",
			DP_UUID(co_uuid));
		rc = container_tree_create(toh, co_uuid, &cont_root);
		if (rc) {
			D_ERROR("tree_create cont "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(co_uuid), DP_RC(rc));
			return rc;
		}
	} else {
		cont_root = tmp_iov.iov_buf;
	}

	/* Then try to insert the object under the container */
	d_iov_set(&key_iov, &oid, sizeof(oid));
	rc = dbtree_lookup(cont_root->root_hdl, &key_iov, val_iov);
	if (rc == 0) {
		D_DEBUG(DB_TRACE, DF_UOID"/"DF_UUID" already exits\n",
			DP_UOID(oid), DP_UUID(co_uuid));
		return -DER_EXIST;
	}

	rc = dbtree_update(cont_root->root_hdl, &key_iov, val_iov);
	if (rc < 0) {
		D_ERROR("failed to insert "DF_UOID": "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
		return rc;
	}
	cont_root->count++;
	D_DEBUG(DB_TRACE, "insert "DF_UOID"/"DF_UUID" in"
		" cont_root %p count %d\n", DP_UOID(oid),
		DP_UUID(co_uuid), cont_root, cont_root->count);

	return rc;
}

void
migrate_pool_tls_destroy(struct migrate_pool_tls *tls)
{
	D_DEBUG(DB_REBUILD, "TLS destroy for "DF_UUID" ver %d\n",
		DP_UUID(tls->mpt_pool_uuid), tls->mpt_version);
	if (tls->mpt_pool)
		ds_pool_child_put(tls->mpt_pool);
	if (tls->mpt_svc_list.rl_ranks)
		D_FREE(tls->mpt_svc_list.rl_ranks);
	if (tls->mpt_done_eventual)
		ABT_eventual_free(&tls->mpt_done_eventual);
	if (tls->mpt_inflight_cond)
		ABT_cond_free(&tls->mpt_inflight_cond);
	if (tls->mpt_inflight_mutex)
		ABT_mutex_free(&tls->mpt_inflight_mutex);
	if (daos_handle_is_valid(tls->mpt_root_hdl))
		obj_tree_destroy(tls->mpt_root_hdl);
	if (daos_handle_is_valid(tls->mpt_migrated_root_hdl))
		obj_tree_destroy(tls->mpt_migrated_root_hdl);
	d_list_del(&tls->mpt_list);
	D_FREE(tls);
}

void
migrate_pool_tls_get(struct migrate_pool_tls *tls)
{
	tls->mpt_refcount++;
}

void
migrate_pool_tls_put(struct migrate_pool_tls *tls)
{
	tls->mpt_refcount--;
	if (tls->mpt_fini && tls->mpt_refcount == 1)
		ABT_eventual_set(tls->mpt_done_eventual, NULL, 0);
	if (tls->mpt_refcount == 0)
		migrate_pool_tls_destroy(tls);
}

struct migrate_pool_tls *
migrate_pool_tls_lookup(uuid_t pool_uuid, unsigned int ver)
{
	struct obj_tls	*tls = obj_tls_get();
	struct migrate_pool_tls *pool_tls;
	struct migrate_pool_tls *found = NULL;

	D_ASSERT(tls != NULL);
	/* Only 1 thread will access the list, no need lock */
	d_list_for_each_entry(pool_tls, &tls->ot_pool_list, mpt_list) {
		if (uuid_compare(pool_tls->mpt_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) ||
		     ver == pool_tls->mpt_version)) {
			migrate_pool_tls_get(pool_tls);
			found = pool_tls;
			break;
		}
	}

	return found;
}

struct migrate_pool_tls_create_arg {
	uuid_t	pool_uuid;
	uuid_t	pool_hdl_uuid;
	uuid_t  co_hdl_uuid;
	d_rank_list_t *svc_list;
	uint64_t max_eph;
	int	version;
	int	del_local_objs;
};

int
migrate_pool_tls_create_one(void *data)
{
	struct migrate_pool_tls_create_arg *arg = data;
	struct obj_tls			   *tls = obj_tls_get();
	struct migrate_pool_tls		   *pool_tls;
	int rc;

	pool_tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	if (pool_tls != NULL) {
		/* Some one else already created, because collective function
		 * might yield xstream.
		 */
		migrate_pool_tls_put(pool_tls);
		return 0;
	}

	D_ALLOC_PTR(pool_tls);
	if (pool_tls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ABT_eventual_create(0, &pool_tls->mpt_done_eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&pool_tls->mpt_inflight_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = ABT_mutex_create(&pool_tls->mpt_inflight_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	uuid_copy(pool_tls->mpt_pool_uuid, arg->pool_uuid);
	uuid_copy(pool_tls->mpt_poh_uuid, arg->pool_hdl_uuid);
	uuid_copy(pool_tls->mpt_coh_uuid, arg->co_hdl_uuid);
	pool_tls->mpt_version = arg->version;
	pool_tls->mpt_rec_count = 0;
	pool_tls->mpt_obj_count = 0;
	pool_tls->mpt_size = 0;
	pool_tls->mpt_generated_ult = 0;
	pool_tls->mpt_executed_ult = 0;
	pool_tls->mpt_root_hdl = DAOS_HDL_INVAL;
	pool_tls->mpt_max_eph = arg->max_eph;
	pool_tls->mpt_pool = ds_pool_child_lookup(arg->pool_uuid);
	pool_tls->mpt_del_local_objs = arg->del_local_objs;
	pool_tls->mpt_inflight_max_size = MIGRATE_MAX_SIZE;
	pool_tls->mpt_inflight_max_ult = MIGRATE_MAX_ULT;
	pool_tls->mpt_inflight_size = 0;
	pool_tls->mpt_refcount = 1;
	rc = daos_rank_list_copy(&pool_tls->mpt_svc_list, arg->svc_list);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_REBUILD, "TLS %p create for "DF_UUID" "DF_UUID"/"DF_UUID
		" ver %d "DF_RC"\n", pool_tls, DP_UUID(pool_tls->mpt_pool_uuid),
		DP_UUID(arg->pool_hdl_uuid), DP_UUID(arg->co_hdl_uuid),
		arg->version, DP_RC(rc));
	d_list_add(&pool_tls->mpt_list, &tls->ot_pool_list);
out:
	if (rc && pool_tls)
		migrate_pool_tls_destroy(pool_tls);

	return rc;
}

static struct migrate_pool_tls*
migrate_pool_tls_lookup_create(struct ds_pool *pool, int version,
			       uuid_t pool_hdl_uuid, uuid_t co_hdl_uuid,
			       uint64_t max_eph, int del_local_objs)
{
	struct migrate_pool_tls *tls = NULL;
	struct migrate_pool_tls_create_arg arg = { 0 };
	daos_prop_t		*prop;
	struct daos_prop_entry	*entry;
	int			rc = 0;

	tls = migrate_pool_tls_lookup(pool->sp_uuid, version);
	if (tls)
		return tls;

	D_ALLOC_PTR(prop);
	if (prop == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_pool_iv_prop_fetch(pool, prop);
	if (rc)
		D_GOTO(out, rc);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_LIST);
	D_ASSERT(entry != NULL);

	uuid_copy(arg.pool_uuid, pool->sp_uuid);
	uuid_copy(arg.pool_hdl_uuid, pool_hdl_uuid);
	uuid_copy(arg.co_hdl_uuid, co_hdl_uuid);
	arg.version = version;
	arg.del_local_objs = del_local_objs;
	arg.max_eph = max_eph;
	arg.svc_list = (d_rank_list_t *)entry->dpe_val_ptr;
	rc = dss_task_collective(migrate_pool_tls_create_one, &arg, 0);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create migrate tls: "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* dss_task_collective does not do collective on xstream 0 */
	rc = migrate_pool_tls_create_one(&arg);
	if (rc)
		D_GOTO(out, rc);

	tls = migrate_pool_tls_lookup(pool->sp_uuid, version);
	D_ASSERT(tls != NULL);
out:
	D_DEBUG(DB_TRACE, "create tls "DF_UUID": "DF_RC"\n",
		DP_UUID(pool->sp_uuid), DP_RC(rc));
	if (prop != NULL)
		daos_prop_free(prop);

	return tls;
}

static void
mrone_recx_daos_vos_internal(struct migrate_one *mrone,
			     bool daos2vos, int shard)
{
	daos_iod_t *iod;
	int cell_nr;
	int stripe_nr;
	int j;
	int k;

	D_ASSERT(daos_oclass_is_ec(&mrone->mo_oca));

	cell_nr = obj_ec_cell_rec_nr(&mrone->mo_oca);
	stripe_nr = obj_ec_stripe_rec_nr(&mrone->mo_oca);
	/* Convert the DAOS to VOS EC offset */
	for (j = 0; j < mrone->mo_iod_num; j++) {
		iod = &mrone->mo_iods[j];
		if (iod->iod_type == DAOS_IOD_SINGLE)
			continue;
		for (k = 0; k < iod->iod_nr; k++) {
			daos_recx_t *recx;

			recx = &iod->iod_recxs[k];
			D_ASSERT(recx->rx_nr <= cell_nr);
			if (daos2vos)
				recx->rx_idx = obj_ec_idx_daos2vos(recx->rx_idx,
								   stripe_nr,
								   cell_nr);
			else
				recx->rx_idx = obj_ec_idx_vos2daos(recx->rx_idx,
								   stripe_nr,
								   cell_nr,
								   shard);
			D_DEBUG(DB_REBUILD, "j %d k %d "DF_U64"/"DF_U64"\n",
				j, k, recx->rx_idx, recx->rx_nr);
		}
	}
}

static void
mrone_recx_daos2_vos(struct migrate_one *mrone)
{
	mrone_recx_daos_vos_internal(mrone, true, -1);
}

static void
mrone_recx_vos2_daos(struct migrate_one *mrone, int shard)
{
	shard = shard % obj_ec_tgt_nr(&mrone->mo_oca);
	D_ASSERT(shard < obj_ec_data_tgt_nr(&mrone->mo_oca));
	mrone_recx_daos_vos_internal(mrone, false, shard);
}

static int
mrone_obj_fetch(struct migrate_one *mrone, daos_handle_t oh, d_sg_list_t *sgls,
		d_iov_t *csum_iov_fetch)
{
	uint32_t		 flags = DIOF_FOR_MIGRATION;
	int			 rc;

	if (daos_oclass_grp_size(&mrone->mo_oca) > 1)
		flags |= DIOF_TO_LEADER;

	rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
			   mrone->mo_iod_num, mrone->mo_iods, sgls, NULL,
			   flags, NULL, csum_iov_fetch);

	if (rc != 0)
		return rc;

	if (csum_iov_fetch != NULL &&
	    csum_iov_fetch->iov_len > csum_iov_fetch->iov_buf_len) {
		/** retry dsc_obj_fetch with appropriate csum_iov
		 * buf length
		 */
		void *p;

		D_REALLOC(p, csum_iov_fetch->iov_buf,
			  csum_iov_fetch->iov_buf_len, csum_iov_fetch->iov_len);
		if (p == NULL)
			return -DER_NOMEM;
		csum_iov_fetch->iov_buf_len = csum_iov_fetch->iov_len;
		csum_iov_fetch->iov_len = 0;
		csum_iov_fetch->iov_buf = p;

		rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
				   mrone->mo_iod_num, mrone->mo_iods, sgls,
				   NULL, flags, NULL, csum_iov_fetch);
	}

	return rc;
}

#define MIGRATE_STACK_SIZE	131072
#define MAX_BUF_SIZE		2048
#define CSUM_BUF_SIZE		256

/**
 * allocate the memory for the iods_csums and unpack the csum_iov into the
 * into the iods_csums.
 * Note: the csum_iov is modified so a shallow copy should be sent instead of
 * the original.
 */


static int
migrate_fetch_update_inline(struct migrate_one *mrone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont)
{
	d_sg_list_t		 sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t			 iov[DSS_ENUM_UNPACK_MAX_IODS];
	struct dcs_iod_csums	*iod_csums = NULL;
	int			 iod_cnt = 0;
	int			 start;
	char		 iov_buf[DSS_ENUM_UNPACK_MAX_IODS][MAX_BUF_SIZE];
	bool			 fetch = false;
	int			 i;
	int			 rc = 0;
	struct daos_csummer	*csummer = NULL;
	d_iov_t			*csums_iov = NULL;
	d_iov_t			 csum_iov_fetch = {0};
	d_iov_t			 tmp_csum_iov;

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	for (i = 0; i < mrone->mo_iod_num; i++) {
		if (mrone->mo_iods[i].iod_size == 0)
			continue;

		if (mrone->mo_sgls != NULL && mrone->mo_sgls[i].sg_nr > 0) {
			sgls[i] = mrone->mo_sgls[i];
		} else {
			sgls[i].sg_nr = 1;
			sgls[i].sg_nr_out = 1;
			d_iov_set(&iov[i], iov_buf[i], MAX_BUF_SIZE);
			sgls[i].sg_iovs = &iov[i];
			fetch = true;
		}
	}

	D_DEBUG(DB_REBUILD, DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64
		" fetch %s\n", DP_UOID(mrone->mo_oid), mrone,
		DP_KEY(&mrone->mo_dkey), mrone->mo_iod_num,
		mrone->mo_epoch, fetch ? "yes":"no");

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_UPDATE))
		return 0;

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_UPDATE_FAIL))
		return -DER_INVAL;


	if (fetch) {
		rc = daos_iov_alloc(&csum_iov_fetch, CSUM_BUF_SIZE, false);
		if (rc != 0)
			D_GOTO(out, rc);

		rc = mrone_obj_fetch(mrone, oh, sgls, &csum_iov_fetch);

		if (rc) {
			D_ERROR("mrone_obj_fetch "DF_RC"\n", DP_RC(rc));
			return rc;
		}

		/** Using all fetched data so use all fetched checksums */
		csums_iov = &csum_iov_fetch;
	} else {
		/** csums were packed from obj_enum because data is inlined */
		csums_iov = &mrone->mo_csum_iov;
	}
	D_ASSERT(csums_iov != NULL);
	/** make a copy of the iov because it will be modified while
	 * iterating over the csums
	 */
	tmp_csum_iov = *csums_iov;

	if (daos_oclass_is_ec(&mrone->mo_oca) &&
	    !obj_shard_is_ec_parity(mrone->mo_oid, &mrone->mo_oca))
		mrone_recx_daos2_vos(mrone);

	rc = daos_csummer_csum_init_with_packed(&csummer, csums_iov);
	if (rc != 0)
		D_GOTO(out, rc);

	for (i = 0, start = 0; i < mrone->mo_iod_num; i++) {
		if (mrone->mo_iods[i].iod_size > 0) {
			iod_cnt++;
			continue;
		} else {
			/* skip empty record */
			if (iod_cnt == 0) {
				D_DEBUG(DB_TRACE, "i %d iod_size = 0\n", i);
				continue;
			}

			D_DEBUG(DB_TRACE, "update start %d cnt %d\n",
				start, iod_cnt);

			if (daos_oclass_is_ec(&mrone->mo_oca)) {
				rc = daos_csummer_calc_iods(csummer,
					&sgls[start],  &mrone->mo_iods[start],
					NULL, iod_cnt, false, NULL, 0,
					&iod_csums);
				if (rc != 0) {
					D_ERROR("Error calculating checksums: "
						DF_RC"\n",
						DP_RC(rc));
					break;
				}
			} else {
				rc = daos_csummer_alloc_iods_csums_with_packed(
					csummer,
					&mrone->mo_iods[start],
					iod_cnt, &tmp_csum_iov,
					&iod_csums);
				if (rc != 0) {
					D_ERROR("setting up iods csums failed: "
							DF_RC"\n", DP_RC(rc));
					break;
				}
			}

			rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
					    mrone->mo_update_epoch,
					    mrone->mo_version,
					    0, &mrone->mo_dkey, iod_cnt,
					    &mrone->mo_iods[start], iod_csums,
					    &sgls[start]);
			daos_csummer_free_ic(csummer, &iod_csums);

			if (rc) {
				D_ERROR("migrate failed: "DF_RC"\n", DP_RC(rc));
				break;
			}
			iod_cnt = 0;
			start = i + 1;
		}
	}

	if (iod_cnt > 0) {
		rc = daos_csummer_alloc_iods_csums_with_packed(
			csummer,
			&mrone->mo_iods[start],
			iod_cnt,
			&tmp_csum_iov, &iod_csums);
		if (rc != 0) {
			D_ERROR("failed to alloc iod csums: "DF_RC". "
				"csum_iov was '%s'\n",
				DP_RC(rc), fetch ? "FETCHED" : "INLINE");
			D_GOTO(out, rc);
		}

		if (daos_oclass_is_ec(&mrone->mo_oca)) {
			rc = daos_csummer_calc_iods(csummer,
					&sgls[start],  &mrone->mo_iods[start],
					NULL, iod_cnt, false, NULL, 0,
					&iod_csums);
			if (rc != 0) {
				D_ERROR("Error calculating checksums: "
						DF_RC"\n",
					DP_RC(rc));
				D_GOTO(out, rc);
			}
		} else {
			rc = daos_csummer_alloc_iods_csums_with_packed(
				csummer, &mrone->mo_iods[start],
				iod_cnt, &tmp_csum_iov, &iod_csums);
			if (rc != 0) {
				D_ERROR("setting up iods csums failed: "
						DF_RC"\n", DP_RC(rc));
				D_GOTO(out, rc);
			}
		}

		rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_update_epoch,
				    mrone->mo_version,
				    0, &mrone->mo_dkey, iod_cnt,
				    &mrone->mo_iods[start], iod_csums,
				    &sgls[start]);

		if (rc) {
			D_ERROR("migrate failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
		daos_csummer_free_ic(csummer, &iod_csums);
	}

out:
	D_FREE(csum_iov_fetch.iov_buf);
	daos_csummer_destroy(&csummer);

	return rc;
}

static int
obj_ec_encode_buf(daos_obj_id_t oid, struct daos_oclass_attr *oca,
		  daos_size_t iod_size, unsigned char *buffer,
		  unsigned char *p_bufs[])
{
	struct obj_ec_codec	*codec;
	daos_size_t	cell_bytes = obj_ec_cell_rec_nr(oca) * iod_size;
	unsigned int	k = obj_ec_data_tgt_nr(oca);
	unsigned int	p = obj_ec_parity_tgt_nr(oca);
	unsigned char	*data[k];
	int		i;

	codec = obj_ec_codec_get(daos_obj_id2class(oid));
	D_ASSERT(codec != NULL);

	for (i = 0; i < p && p_bufs[i] == NULL; i++) {
		D_ALLOC(p_bufs[i], cell_bytes);
		if (p_bufs[i] == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < k; i++)
		data[i] = buffer + i * cell_bytes;

	ec_encode_data((int)cell_bytes, k, p, codec->ec_gftbls, data, p_bufs);
	return 0;
}

static int
migrate_update_parity(struct migrate_one *mrone, struct ds_cont_child *ds_cont,
		      unsigned char *buffer, daos_off_t offset,
		      daos_size_t size, daos_iod_t *iod,
		      unsigned char *p_bufs[], d_iov_t *csum_iov)
{
	struct daos_oclass_attr	*oca = &mrone->mo_oca;
	daos_size_t		 stride_nr = obj_ec_stripe_rec_nr(oca);
	daos_size_t		 cell_nr = obj_ec_cell_rec_nr(oca);
	daos_recx_t		 tmp_recx;
	d_iov_t			 tmp_iov;
	d_sg_list_t		 tmp_sgl;
	daos_size_t		 write_nr;
	struct daos_csummer	*csummer = NULL;
	struct dcs_iod_csums	*iod_csums = NULL;
	int		rc = 0;

	tmp_sgl.sg_nr = tmp_sgl.sg_nr_out = 1;
	while (size > 0) {
		if (offset % stride_nr != 0)
			write_nr =
			  min(roundup(offset, stride_nr) - offset, size);
		else
			write_nr = min(stride_nr, size);

		if (write_nr == stride_nr) {
			unsigned int shard;

			shard = mrone->mo_oid.id_shard % obj_ec_tgt_nr(oca);

			D_ASSERT(shard >= obj_ec_data_tgt_nr(oca));
			shard -= obj_ec_data_tgt_nr(oca);
			D_ASSERT(shard < obj_ec_parity_tgt_nr(oca));
			rc = obj_ec_encode_buf(mrone->mo_oid.id_pub,
					       oca, iod->iod_size, buffer,
					       p_bufs);
			if (rc)
				D_GOTO(out, rc);
			tmp_recx.rx_idx = obj_ec_idx_daos2vos(offset, stride_nr,
							      cell_nr);
			tmp_recx.rx_idx |= PARITY_INDICATOR;
			tmp_recx.rx_nr = cell_nr;
			d_iov_set(&tmp_iov, p_bufs[shard],
				  cell_nr * iod->iod_size);
			D_DEBUG(DB_IO, "parity "DF_U64"/"DF_U64" "DF_U64"\n",
				tmp_recx.rx_idx, tmp_recx.rx_nr, iod->iod_size);
		} else {
			tmp_recx.rx_idx = offset;
			tmp_recx.rx_nr = write_nr;
			d_iov_set(&tmp_iov, buffer, write_nr * iod->iod_size);
			D_DEBUG(DB_IO, "replicate "DF_U64"/"DF_U64" "
				DF_U64"\n", tmp_recx.rx_idx,
				tmp_recx.rx_nr, iod->iod_size);
		}

		tmp_sgl.sg_iovs = &tmp_iov;
		iod->iod_recxs = &tmp_recx;

		rc = daos_csummer_csum_init_with_packed(&csummer, csum_iov);
		if (rc != 0) {
			D_ERROR("Error initializing csummer");
			D_GOTO(out, rc);
		}

		rc = daos_csummer_calc_iods(csummer, &tmp_sgl, iod, NULL, 1,
					    false, NULL, 0, &iod_csums);
		if (rc != 0) {
			D_ERROR("Error calculating checksums: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_epoch,
				    mrone->mo_version,
				    0, &mrone->mo_dkey, 1, iod, iod_csums,
				    &tmp_sgl);
		size -= write_nr;
		offset += write_nr;
		buffer += write_nr * iod->iod_size;
	}
out:
	daos_csummer_free_ic(csummer, &iod_csums);
	daos_csummer_destroy(&csummer);
	return rc;
}

static int
migrate_fetch_update_parity(struct migrate_one *mrone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont)
{
	d_sg_list_t	 sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t		 iov[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	d_iov_t		 csum_iov_fetch = { 0 };
	d_iov_t		 tmp_csum_iov_fetch;
	char		*data;
	daos_size_t	 size;
	unsigned int	 p = obj_ec_parity_tgt_nr(&mrone->mo_oca);
	unsigned char	*p_bufs[OBJ_EC_MAX_P] = { 0 };
	unsigned char	*ptr;
	int		 i;
	int		 rc;

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	for (i = 0; i < mrone->mo_iod_num; i++) {
		size = daos_iods_len(&mrone->mo_iods[i], 1);
		D_ALLOC(data, size);
		if (data == NULL)
			D_GOTO(out, rc =-DER_NOMEM);

		d_iov_set(&iov[i], data, size);
		sgls[i].sg_nr = 1;
		sgls[i].sg_nr_out = 1;
		sgls[i].sg_iovs = &iov[i];
	}

	D_DEBUG(DB_REBUILD,
		DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64"\n",
		DP_UOID(mrone->mo_oid), mrone, DP_KEY(&mrone->mo_dkey),
		mrone->mo_iod_num, mrone->mo_epoch);

	rc = daos_iov_alloc(&csum_iov_fetch, CSUM_BUF_SIZE, false);
	if (rc)
		D_GOTO(out, rc);
	rc = mrone_obj_fetch(mrone, oh, sgls, &csum_iov_fetch);
	tmp_csum_iov_fetch = csum_iov_fetch;
	if (rc) {
		D_ERROR("migrate dkey "DF_KEY" failed: "DF_RC"\n",
			DP_KEY(&mrone->mo_dkey), DP_RC(rc));
		D_GOTO(out, rc);
	}

	for (i = 0; i < mrone->mo_iod_num; i++) {
		daos_iod_t	*iod;
		int		j;
		daos_off_t	offset;
		daos_iod_t	tmp_iod;

		iod = &mrone->mo_iods[i];
		offset = iod->iod_recxs[0].rx_idx;
		size = iod->iod_recxs[0].rx_nr;
		tmp_iod = *iod;
		ptr = iov[i].iov_buf;
		for (j = 1; j < iod->iod_nr; j++) {
			daos_recx_t	*recx = &iod->iod_recxs[j];

			if (offset + size == recx->rx_idx) {
				size += recx->rx_nr;
				continue;
			}

			tmp_iod.iod_nr = 1;
			rc = migrate_update_parity(mrone, ds_cont, ptr, offset,
						   size, &tmp_iod, p_bufs,
						   &tmp_csum_iov_fetch);
			if (rc)
				D_GOTO(out, rc);
			ptr += size * iod->iod_size;
			offset = recx->rx_idx;
			size = recx->rx_nr;
		}

		if (size > 0)
			rc = migrate_update_parity(mrone, ds_cont, ptr, offset,
						   size, &tmp_iod, p_bufs,
						   &tmp_csum_iov_fetch);
	}
out:
	for (i = 0; i < mrone->mo_iod_num; i++) {
		if (iov[i].iov_buf)
			D_FREE(iov[i].iov_buf);
	}

	for (i = 0; i < p; i++) {
		if (p_bufs[i] != NULL)
			D_FREE(p_bufs[i]);
	}

	daos_iov_free(&csum_iov_fetch);

	return rc;
}

static int
migrate_fetch_update_single(struct migrate_one *mrone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont)
{
	d_sg_list_t		 sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t			 iov[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	struct dcs_layout	 los[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	char			*data;
	daos_size_t		 size;
	int			 i;
	int			 rc;
	d_iov_t			 csum_iov_fetch = {0};
	struct daos_csummer	*csummer = NULL;
	d_iov_t			 tmp_csum_iov;
	struct dcs_iod_csums	*iod_csums = NULL;

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	for (i = 0; i < mrone->mo_iod_num; i++) {
		D_ASSERT(mrone->mo_iods[i].iod_type == DAOS_IOD_SINGLE);

		size = daos_iods_len(&mrone->mo_iods[i], 1);
		D_ASSERT(size != -1);
		D_ALLOC(data, size);
		if (data == NULL)
			D_GOTO(out, rc =-DER_NOMEM);

		d_iov_set(&iov[i], data, size);
		sgls[i].sg_nr = 1;
		sgls[i].sg_nr_out = 1;
		sgls[i].sg_iovs = &iov[i];
	}

	D_DEBUG(DB_REBUILD,
		DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64"\n",
		DP_UOID(mrone->mo_oid), mrone, DP_KEY(&mrone->mo_dkey),
		mrone->mo_iod_num, mrone->mo_epoch);

	rc = daos_iov_alloc(&csum_iov_fetch, CSUM_BUF_SIZE, false);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = mrone_obj_fetch(mrone, oh, sgls, &csum_iov_fetch);
	if (rc) {
		D_ERROR("migrate dkey "DF_KEY" failed: "DF_RC"\n",
			DP_KEY(&mrone->mo_dkey), DP_RC(rc));
		D_GOTO(out, rc);
	}

	for (i = 0; i < mrone->mo_iod_num; i++) {
		daos_iod_t	*iod = &mrone->mo_iods[i];
		uint32_t	start_shard;

		if (mrone->mo_iods[i].iod_size == 0) {
			/* zero size iod will cause assertion failure
			 * in VOS, so let's check here.
			 * So the object is being destroyed between
			 * object enumeration and object fetch on
			 * the remote target, which is usually caused
			 * by container destroy or snapshot deletion.
			 * Since this is rare, let's simply return
			 * failure for this rebuild, then reschedule
			 * the rebuild and retry.
			 */
			rc = -DER_DATA_LOSS;
			D_DEBUG(DB_REBUILD,
				DF_UOID" %p dkey "DF_KEY" "DF_KEY" nr %d/%d"
				" eph "DF_U64" "DF_RC"\n",
				DP_UOID(mrone->mo_oid),
				mrone, DP_KEY(&mrone->mo_dkey),
				DP_KEY(&mrone->mo_iods[i].iod_name),
				mrone->mo_iod_num, i, mrone->mo_epoch,
				DP_RC(rc));
			D_GOTO(out, rc);
		}

		if (!daos_oclass_is_ec(&mrone->mo_oca))
			continue;

		start_shard = rounddown(mrone->mo_oid.id_shard,
					obj_ec_tgt_nr(&mrone->mo_oca));
		if (obj_ec_singv_one_tgt(iod->iod_size, &sgls[i],
					 &mrone->mo_oca)) {
			D_DEBUG(DB_REBUILD, DF_UOID" one tgt.\n",
				DP_UOID(mrone->mo_oid));
			los[i].cs_even_dist = 0;
			continue;
		}

		if (obj_shard_is_ec_parity(mrone->mo_oid, &mrone->mo_oca)) {
			rc = obj_ec_singv_encode_buf(mrone->mo_oid,
						     &mrone->mo_oca,
						     iod, &sgls[i],
						     &sgls[i].sg_iovs[0]);
			if (rc)
				D_GOTO(out, rc);
		} else {
			rc = obj_ec_singv_split(mrone->mo_oid, &mrone->mo_oca,
						iod->iod_size, &sgls[i]);
			if (rc)
				D_GOTO(out, rc);
		}

		obj_singv_ec_rw_filter(mrone->mo_oid, &mrone->mo_oca, iod,
				       NULL, mrone->mo_epoch, ORF_EC,
				       start_shard, 1,
				       true, false, NULL);
		los[i].cs_even_dist = 1;
		los[i].cs_bytes = obj_ec_singv_cell_bytes(
					mrone->mo_iods[i].iod_size,
					&mrone->mo_oca);
		los[i].cs_nr = obj_ec_tgt_nr(&mrone->mo_oca);
		D_DEBUG(DB_CSUM, "los[%d]: "DF_LAYOUT"\n", i,
			DP_LAYOUT(los[i]));
	}

	rc = daos_csummer_csum_init_with_packed(&csummer, &csum_iov_fetch);
	if (rc != 0)
		D_GOTO(out, rc);

	if (daos_oclass_is_ec(&mrone->mo_oca)) {
		/** Calc checksum for EC single value, since it may be striped,
		 * and we need re-calculate the single stripe checksum.
		 */
		D_DEBUG(DB_CSUM,
			DF_C_UOID_DKEY" REBUILD: Calculating csums\n",
			DP_C_UOID_DKEY(mrone->mo_oid, &mrone->mo_dkey));
		rc = daos_csummer_calc_iods(csummer, sgls, mrone->mo_iods, NULL,
					    mrone->mo_iod_num, false, NULL,
					    -1, &iod_csums);
	} else {
		D_DEBUG(DB_CSUM,
			DF_C_UOID_DKEY" REBUILD: Using packed csums\n",
			DP_C_UOID_DKEY(mrone->mo_oid, &mrone->mo_dkey));
		tmp_csum_iov = csum_iov_fetch;
		rc = daos_csummer_alloc_iods_csums_with_packed(csummer,
							       mrone->mo_iods,
							      mrone->mo_iod_num,
							       &tmp_csum_iov,
							       &iod_csums);
	}
	if (rc != 0) {
		D_ERROR("unable to allocate iod csums: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
			    mrone->mo_update_epoch, mrone->mo_version,
			    0, &mrone->mo_dkey, mrone->mo_iod_num,
			    mrone->mo_iods, iod_csums, sgls);
out:
	for (i = 0; i < mrone->mo_iod_num; i++) {
		if (iov[i].iov_buf)
			D_FREE(iov[i].iov_buf);

		/* since iod_recxs is being used by single value update somehow,
		 * let's reset it after update.
		 */
		if (mrone->mo_iods[i].iod_type == DAOS_IOD_SINGLE)
			mrone->mo_iods[i].iod_recxs = NULL;
	}

	daos_csummer_free_ic(csummer, &iod_csums);
	daos_iov_free(&csum_iov_fetch);
	daos_csummer_destroy(&csummer);

	return rc;
}

static int
migrate_fetch_update_bulk(struct migrate_one *mrone, daos_handle_t oh,
			  struct ds_cont_child *ds_cont)
{
	d_sg_list_t		 sgls[DSS_ENUM_UNPACK_MAX_IODS];
	daos_handle_t		 ioh;
	int			 rc, rc1, i, ret, sgl_cnt = 0;
	struct daos_csummer	*csummer = NULL;
	d_iov_t			 csum_iov_fetch = {0};
	struct dcs_iod_csums	*iod_csums = NULL;
	d_iov_t			 tmp_csum_iov;


	if (obj_shard_is_ec_parity(mrone->mo_oid, &mrone->mo_oca))
		return migrate_fetch_update_parity(mrone, oh, ds_cont);

	if (daos_oclass_is_ec(&mrone->mo_oca))
		mrone_recx_daos2_vos(mrone);

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	rc = vos_update_begin(ds_cont->sc_hdl, mrone->mo_oid,
			      mrone->mo_update_epoch, 0, &mrone->mo_dkey,
			      mrone->mo_iod_num, mrone->mo_iods,
			      mrone->mo_iods_csums, 0, &ioh, NULL);
	if (rc != 0) {
		D_ERROR(DF_UOID"preparing update fails: "DF_RC"\n",
			DP_UOID(mrone->mo_oid), DP_RC(rc));
		return rc;
	}

	rc = bio_iod_prep(vos_ioh2desc(ioh), BIO_CHK_TYPE_REBUILD, NULL,
			  CRT_BULK_RW);
	if (rc) {
		D_ERROR("Prepare EIOD for "DF_UOID" error: "DF_RC"\n",
			DP_UOID(mrone->mo_oid), DP_RC(rc));
		goto end;
	}

	for (i = 0; i < mrone->mo_iod_num; i++) {
		struct bio_sglist	*bsgl;

		bsgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(bsgl != NULL);

		rc = bio_sgl_convert(bsgl, &sgls[i]);
		if (rc)
			goto post;
		sgl_cnt++;
	}

	D_DEBUG(DB_REBUILD,
		DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64"\n",
		DP_UOID(mrone->mo_oid), mrone, DP_KEY(&mrone->mo_dkey),
		mrone->mo_iod_num, mrone->mo_epoch);

	if (daos_oclass_is_ec(&mrone->mo_oca))
		mrone_recx_vos2_daos(mrone, mrone->mo_oid.id_shard);

	rc = daos_iov_alloc(&csum_iov_fetch, CSUM_BUF_SIZE, false);
	if (rc != 0)
		D_GOTO(post, rc);

	rc = mrone_obj_fetch(mrone, oh, sgls, &csum_iov_fetch);
	if (rc) {
		D_ERROR("migrate dkey "DF_KEY" failed: "DF_RC"\n",
			DP_KEY(&mrone->mo_dkey), DP_RC(rc));
		D_GOTO(post, rc);
	}

	rc = daos_csummer_csum_init_with_packed(&csummer, &csum_iov_fetch);
	if (rc != 0)
		D_GOTO(post, rc);

	if (daos_oclass_is_ec(&mrone->mo_oca)) {
		D_DEBUG(DB_CSUM,
			DF_C_UOID_DKEY" REBUILD: Calculating csums. "
			"IOD count: %d\n",
			DP_C_UOID_DKEY(mrone->mo_oid, &mrone->mo_dkey),
			mrone->mo_iod_num);
		rc = daos_csummer_calc_iods(csummer, sgls, mrone->mo_iods, NULL,
					    mrone->mo_iod_num, false, NULL, -1,
					    &iod_csums);
	} else {
		D_DEBUG(DB_CSUM,
			DF_C_UOID_DKEY" REBUILD: Using packed csums\n",
			DP_C_UOID_DKEY(mrone->mo_oid, &mrone->mo_dkey));
		tmp_csum_iov = csum_iov_fetch;
		rc = daos_csummer_alloc_iods_csums_with_packed(csummer,
							mrone->mo_iods,
							mrone->mo_iod_num,
							&tmp_csum_iov,
							&iod_csums);
		if (rc != 0) {
			D_ERROR("Failed to alloc iod csums: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(post, rc);
		}
	}

	vos_set_io_csum(ioh, iod_csums);
post:
	for (i = 0; i < sgl_cnt; i++)
		d_sgl_fini(&sgls[i], false);

	if (daos_oclass_is_ec(&mrone->mo_oca))
		mrone_recx_daos2_vos(mrone);

	ret = bio_iod_post(vos_ioh2desc(ioh));
	if (ret) {
		D_ERROR("Post EIOD for "DF_UOID" error: "DF_RC"\n",
			DP_UOID(mrone->mo_oid), DP_RC(ret));
		rc = rc ? : ret;
	}

	for (i = 0; rc == 0 && i < mrone->mo_iod_num; i++) {
		if (mrone->mo_iods[i].iod_size == 0) {
			/* zero size iod will cause assertion failure
			 * in VOS, so let's check here.
			 * So the object is being destroyed between
			 * object enumeration and object fetch on
			 * the remote target, which is usually caused
			 * by container destroy or snapshot deletion.
			 * Since this is rare, let's simply return
			 * failure for this rebuild, then reschedule
			 * the rebuild and retry.
			 */
			rc = -DER_DATA_LOSS;
			D_DEBUG(DB_REBUILD,
				DF_UOID" %p dkey "DF_KEY" "DF_KEY" nr %d/%d"
				" eph "DF_U64" "DF_RC"\n",
				DP_UOID(mrone->mo_oid),
				mrone, DP_KEY(&mrone->mo_dkey),
				DP_KEY(&mrone->mo_iods[i].iod_name),
				mrone->mo_iod_num, i, mrone->mo_epoch,
				DP_RC(rc));
			D_GOTO(end, rc);
		}
	}
end:
	rc1 = vos_update_end(ioh, mrone->mo_version, &mrone->mo_dkey, rc, NULL,
			     NULL);
	daos_csummer_free_ic(csummer, &iod_csums);
	daos_csummer_destroy(&csummer);
	daos_iov_free(&csum_iov_fetch);
	if (rc == 0)
		rc = rc1;
	return rc;
}

/**
 * Punch dkeys/akeys before migrate.
 */
static int
migrate_punch(struct migrate_pool_tls *tls, struct migrate_one *mrone,
	      struct ds_cont_child *cont)
{
	int	rc = 0;
	int	i;

	/* Punch dkey */
	if (mrone->mo_dkey_punch_eph != 0) {
		D_DEBUG(DB_REBUILD, DF_UOID" punch dkey "DF_KEY"/"DF_U64"\n",
			DP_UOID(mrone->mo_oid), DP_KEY(&mrone->mo_dkey),
			mrone->mo_dkey_punch_eph);
		rc = vos_obj_punch(cont->sc_hdl, mrone->mo_oid,
				   mrone->mo_dkey_punch_eph,
				   tls->mpt_version, VOS_OF_REPLAY_PC,
				   &mrone->mo_dkey, 0, NULL, NULL);
		if (rc) {
			D_ERROR(DF_UOID" punch dkey failed: "DF_RC"\n",
				DP_UOID(mrone->mo_oid), DP_RC(rc));
			return rc;
		}
	}

	for (i = 0; i < mrone->mo_iod_num; i++) {
		daos_epoch_t eph;

		eph = mrone->mo_akey_punch_ephs[i];
		D_ASSERT(eph != DAOS_EPOCH_MAX);
		if (eph == 0)
			continue;

		D_DEBUG(DB_REBUILD, DF_UOID" mrone %p punch dkey "
			DF_KEY" akey "DF_KEY" eph "DF_U64"\n",
			DP_UOID(mrone->mo_oid), mrone,
			DP_KEY(&mrone->mo_dkey),
			DP_KEY(&mrone->mo_iods[i].iod_name), eph);

		rc = vos_obj_punch(cont->sc_hdl, mrone->mo_oid,
				   eph, tls->mpt_version,
				   VOS_OF_REPLAY_PC, &mrone->mo_dkey,
				   1, &mrone->mo_iods[i].iod_name,
				   NULL);
		if (rc) {
			D_ERROR(DF_UOID" punch akey failed: "DF_RC"\n",
				DP_UOID(mrone->mo_oid), DP_RC(rc));
			return rc;
		}
	}

	/* punch records */
	if (mrone->mo_punch_iod_num > 0) {
		rc = vos_obj_update(cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_rec_punch_eph,
				    mrone->mo_version, 0, &mrone->mo_dkey,
				    mrone->mo_punch_iod_num,
				    mrone->mo_punch_iods, NULL, NULL);
		D_DEBUG(DB_REBUILD, DF_UOID" mrone %p punch %d eph "DF_U64
			" records: "DF_RC"\n", DP_UOID(mrone->mo_oid), mrone,
			mrone->mo_punch_iod_num, mrone->mo_rec_punch_eph,
			DP_RC(rc));
	}

	return rc;
}

static int
migrate_dkey(struct migrate_pool_tls *tls, struct migrate_one *mrone,
	     daos_size_t data_size)
{
	struct ds_cont_child	*cont;
	struct cont_props	 props;
	daos_handle_t		 poh = DAOS_HDL_INVAL;
	daos_handle_t		 coh = DAOS_HDL_INVAL;
	daos_handle_t		 oh  = DAOS_HDL_INVAL;
	int			 rc;

	rc = ds_cont_child_open_create(tls->mpt_pool_uuid, mrone->mo_cont_uuid,
				       &cont);
	if (rc) {
		if (rc == -DER_SHUTDOWN) {
			D_DEBUG(DB_REBUILD, DF_UUID "container is being"
				" destroyed\n", DP_UUID(mrone->mo_cont_uuid));
			rc = 0;
		}
		D_GOTO(out, rc);
	}

	rc = dsc_pool_open(tls->mpt_pool_uuid, tls->mpt_poh_uuid, 0,
			   NULL, tls->mpt_pool->spc_pool->sp_map,
			   &tls->mpt_svc_list, &poh);
	if (rc)
		D_GOTO(cont_put, rc);

	/* Open client dc handle used to read the remote object data */
	rc = dsc_cont_open(poh, mrone->mo_cont_uuid, tls->mpt_coh_uuid, 0,
			   &coh);
	if (rc)
		D_GOTO(pool_close, rc);

	/* Open the remote object */
	rc = dsc_obj_open(coh, mrone->mo_oid.id_pub, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(cont_close, rc);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_TGT_NOSPACE))
		D_GOTO(obj_close, rc = -DER_NOSPACE);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_REBUILD)) {
		D_DEBUG(DB_REBUILD, DF_UUID" disable rebuild\n",
			DP_UUID(tls->mpt_pool_uuid));
		D_GOTO(obj_close, rc);
	}

	dsc_cont_get_props(coh, &props);
	rc = dsc_obj_id2oc_attr(mrone->mo_oid.id_pub, &props, &mrone->mo_oca);
	if (rc) {
		D_ERROR("Unknown object class: %d\n",
			daos_obj_id2class(mrone->mo_oid.id_pub));
		D_GOTO(obj_close, rc);
	}

	/* punch the object */
	if (mrone->mo_obj_punch_eph) {
		rc = vos_obj_punch(cont->sc_hdl, mrone->mo_oid,
				   mrone->mo_obj_punch_eph,
				   tls->mpt_version, VOS_OF_REPLAY_PC,
				   NULL, 0, NULL, NULL);
		if (rc) {
			D_ERROR(DF_UOID" punch obj failed: "DF_RC"\n",
				DP_UOID(mrone->mo_oid), DP_RC(rc));
			D_GOTO(obj_close, rc);
		}
	}

	rc = migrate_punch(tls, mrone, cont);
	if (rc)
		D_GOTO(obj_close, rc);

	if (data_size == 0) {
		D_DEBUG(DB_REBUILD, "empty mrone %p\n", mrone);
		D_GOTO(obj_close, rc);
	}

	if (mrone->mo_iods[0].iod_type == DAOS_IOD_SINGLE)
		rc = migrate_fetch_update_single(mrone, oh, cont);
	else if (data_size < MAX_BUF_SIZE || data_size == (daos_size_t)(-1))
		rc = migrate_fetch_update_inline(mrone, oh, cont);
	else
		rc = migrate_fetch_update_bulk(mrone, oh, cont);

	tls->mpt_rec_count += mrone->mo_rec_num;
	tls->mpt_size += mrone->mo_size;
obj_close:
	dsc_obj_close(oh);
cont_close:
	dsc_cont_close(poh, coh);
pool_close:
	dsc_pool_close(poh);
cont_put:
	ds_cont_child_put(cont);
out:
	return rc;
}

static void
migrate_one_destroy(struct migrate_one *mrone)
{
	int i;

	D_ASSERT(d_list_empty(&mrone->mo_list));
	daos_iov_free(&mrone->mo_dkey);

	if (mrone->mo_iods)
		daos_iods_free(mrone->mo_iods, mrone->mo_iod_alloc_num, true);

	if (mrone->mo_punch_iods)
		daos_iods_free(mrone->mo_punch_iods, mrone->mo_iod_alloc_num,
			       true);

	if (mrone->mo_akey_punch_ephs)
		D_FREE(mrone->mo_akey_punch_ephs);

	if (mrone->mo_sgls) {
		for (i = 0; i < mrone->mo_iod_alloc_num; i++)
			d_sgl_fini(&mrone->mo_sgls[i], true);
		D_FREE(mrone->mo_sgls);
	}

	if (mrone->mo_iods_csums)
		D_FREE(mrone->mo_iods_csums);

	D_FREE(mrone);
}

static void
migrate_one_ult(void *arg)
{
	struct migrate_one	*mrone = arg;
	struct migrate_pool_tls	*tls;
	daos_size_t		data_size;
	int			rc = 0;

	if (daos_fail_check(DAOS_REBUILD_TGT_REBUILD_HANG))
		dss_sleep(daos_fail_value_get() * 1000000);

	tls = migrate_pool_tls_lookup(mrone->mo_pool_uuid,
				      mrone->mo_pool_tls_version);
	if (tls == NULL || tls->mpt_fini) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
		       DP_UUID(mrone->mo_pool_uuid));
		goto out;
	}

	data_size = daos_iods_len(mrone->mo_iods, mrone->mo_iod_num);
	D_DEBUG(DB_TRACE, "mrone %p data size is "DF_U64"\n",
		mrone, data_size);
	D_ASSERT(data_size != (daos_size_t)-1);
	D_DEBUG(DB_REBUILD, "mrone %p inflight size "DF_U64" max "DF_U64"\n",
		mrone, tls->mpt_inflight_size, tls->mpt_inflight_max_size);

	while (tls->mpt_inflight_size + data_size >=
	       tls->mpt_inflight_max_size && tls->mpt_inflight_max_size != 0
	       && !tls->mpt_fini) {
		D_DEBUG(DB_REBUILD, "mrone %p wait "DF_U64"/"DF_U64"\n",
			mrone, tls->mpt_inflight_size,
			tls->mpt_inflight_max_size);
		ABT_mutex_lock(tls->mpt_inflight_mutex);
		ABT_cond_wait(tls->mpt_inflight_cond, tls->mpt_inflight_mutex);
		ABT_mutex_unlock(tls->mpt_inflight_mutex);
	}

	if (tls->mpt_fini)
		D_GOTO(out, rc);

	tls->mpt_inflight_size += data_size;
	rc = migrate_dkey(tls, mrone, data_size);
	tls->mpt_inflight_size -= data_size;

	ABT_mutex_lock(tls->mpt_inflight_mutex);
	ABT_cond_broadcast(tls->mpt_inflight_cond);
	ABT_mutex_unlock(tls->mpt_inflight_mutex);

	D_DEBUG(DB_REBUILD, DF_UOID" migrate dkey "DF_KEY" inflight "DF_U64": "
		DF_RC"\n", DP_UOID(mrone->mo_oid), DP_KEY(&mrone->mo_dkey),
		tls->mpt_inflight_size, DP_RC(rc));

	/* Ignore nonexistent error because puller could race
	 * with user's container destroy:
	 * - puller got the container+oid from a remote scanner
	 * - user destroyed the container
	 * - puller try to open container or pulling data
	 *   (nonexistent)
	 * This is just a workaround...
	 */
	if (rc != -DER_NONEXIST && tls->mpt_status == 0)
		tls->mpt_status = rc;
out:
	migrate_one_destroy(mrone);
	if (tls != NULL) {
		tls->mpt_executed_ult++;
		migrate_pool_tls_put(tls);
	}
}

/* If src_iod is NULL, it will try to merge the recxs inside dst_iod */
static int
migrate_merge_iod_recx(daos_iod_t *dst_iod, daos_iod_t *src_iod)
{
	struct obj_auxi_list_recx	*recx;
	struct obj_auxi_list_recx	*tmp;
	daos_recx_t	*recxs;
	d_list_t	merge_list;
	int		nr_recxs = 0;
	int		i;
	int		rc = 0;

	D_INIT_LIST_HEAD(&merge_list);
	if (src_iod != NULL) {
		recxs = src_iod->iod_recxs;
		for (i = 0; i < src_iod->iod_nr; i++) {
			D_DEBUG(DB_REBUILD, "src merge "DF_U64"/"DF_U64"\n",
				recxs[i].rx_idx, recxs[i].rx_nr);
			rc = merge_recx(&merge_list, recxs[i].rx_idx,
					recxs[i].rx_nr);
			if (rc)
				D_GOTO(out, rc);
		}
	}

	D_ASSERT(dst_iod != NULL);
	recxs = dst_iod->iod_recxs;
	for (i = 0; i < dst_iod->iod_nr; i++) {
		D_DEBUG(DB_REBUILD, "dst merge "DF_U64"/"DF_U64"\n",
			recxs[i].rx_idx, recxs[i].rx_nr);
		rc = merge_recx(&merge_list, recxs[i].rx_idx, recxs[i].rx_nr);
		if (rc)
			D_GOTO(out, rc);
	}

	d_list_for_each_entry(recx, &merge_list, recx_list)
		nr_recxs++;

	if (nr_recxs > dst_iod->iod_nr) {
		D_ALLOC_ARRAY(recxs, nr_recxs);
		if (recxs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		recxs = dst_iod->iod_recxs;
	}

	i = 0;
	d_list_for_each_entry_safe(recx, tmp, &merge_list, recx_list) {
		recxs[i++] = recx->recx;

		D_DEBUG(DB_REBUILD, "merge recx "DF_U64"/"DF_U64"\n",
			recx->recx.rx_idx, recx->recx.rx_nr);
		d_list_del(&recx->recx_list);
		D_FREE(recx);
	}

	if (dst_iod->iod_recxs != recxs)
		D_FREE(dst_iod->iod_recxs);

	dst_iod->iod_recxs = recxs;
	dst_iod->iod_nr = i;
out:
	d_list_for_each_entry_safe(recx, tmp, &merge_list, recx_list) {
		d_list_del(&recx->recx_list);
		D_FREE(recx);
	}
	return rc;
}

static int
migrate_iod_sgl_add(daos_iod_t *iods, uint32_t *iods_num, daos_iod_t *new_iod,
		    d_sg_list_t *sgls, d_sg_list_t *new_sgl)
{
	daos_iod_t *dst_iod;
	int	   rc = 0;
	int	   i;

	for (i = 0; i < *iods_num; i++) {
		if (daos_iov_cmp(&iods[i].iod_name, &new_iod->iod_name))
			break;
	}

	/* None duplicate iods, let's create new one */
	if (i == *iods_num) {
		rc = daos_iod_copy(&iods[i], new_iod);
		if (rc)
			return rc;

		if (new_sgl) {
			rc = daos_sgl_alloc_copy_data(&sgls[i], new_sgl);
			if (rc) {
				daos_iov_free(&iods[i].iod_name);
				return rc;
			}
		}

		if (new_iod->iod_type == DAOS_IOD_SINGLE) {
			iods[i].iod_recxs = NULL;
		} else {
			/**
			 * recx in iod has been reused, i.e. it will be
			 * freed with mrone, so let's set iod_recxs in
			 * iod to be NULL to avoid it is being freed
			 * with iod afterwards.
			 */
			new_iod->iod_recxs = NULL;
		}
		D_DEBUG(DB_REBUILD, "add new akey "DF_KEY" at %d\n",
			DP_KEY(&new_iod->iod_name), i);
		(*iods_num)++;
		return rc;
	}

	/* Try to merge the iods to the existent IOD */
	dst_iod = &iods[i];
	if (dst_iod->iod_size != new_iod->iod_size ||
	    dst_iod->iod_type != new_iod->iod_type) {
		D_ERROR(DF_KEY" dst_iod size "DF_U64" != "DF_U64
			" dst_iod type %d != %d\n",
			DP_KEY(&new_iod->iod_name), dst_iod->iod_size,
			new_iod->iod_size, dst_iod->iod_type,
			new_iod->iod_type);
		D_GOTO(out, rc);
	}

	rc = migrate_merge_iod_recx(dst_iod, new_iod);
	if (rc)
		D_GOTO(out, rc);

	if (new_sgl) {
		rc = daos_sgl_merge(&sgls[i], new_sgl);
		if (rc)
			D_GOTO(out, rc);
	}

	D_DEBUG(DB_REBUILD, "Merge akey "DF_KEY" to %d\n",
		DP_KEY(&new_iod->iod_name), i);
out:
	return rc;
}

static int
rw_iod_pack(struct migrate_one *mrone, daos_iod_t *iod, d_sg_list_t *sgl)
{
	uint64_t total_size = 0;
	int	 rec_cnt = 0;
	int	 i;
	int	 rc;

	D_ASSERT(iod->iod_size > 0);

	if (sgl && mrone->mo_sgls == NULL) {
		D_ASSERT(mrone->mo_iod_alloc_num > 0);
		D_ALLOC_ARRAY(mrone->mo_sgls, mrone->mo_iod_alloc_num);
		if (mrone->mo_sgls == NULL)
			return -DER_NOMEM;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rec_cnt = 1;
		total_size = iod->iod_size;
		D_DEBUG(DB_REBUILD, "single recx "DF_U64"\n", total_size);
	} else {
		for (i = 0; i < iod->iod_nr; i++) {
			D_DEBUG(DB_REBUILD, "new recx "DF_U64"/"DF_U64"\n",
				iod->iod_recxs[i].rx_idx,
				iod->iod_recxs[i].rx_nr);
			rec_cnt += iod->iod_recxs[i].rx_nr;
			total_size += iod->iod_recxs[i].rx_nr * iod->iod_size;
		}
	}

	rc = migrate_iod_sgl_add(mrone->mo_iods, &mrone->mo_iod_num, iod,
				 mrone->mo_sgls, sgl);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_REBUILD,
		"idx %d akey "DF_KEY" nr %d size "DF_U64" type %d rec %d total "
		DF_U64"\n", mrone->mo_iod_num - 1, DP_KEY(&iod->iod_name),
		iod->iod_nr, iod->iod_size, iod->iod_type, rec_cnt, total_size);

	mrone->mo_rec_num += rec_cnt;
	mrone->mo_size += total_size;

out:
	return rc;
}

static int
punch_iod_pack(struct migrate_one *mrone, daos_iod_t *iod, daos_epoch_t eph)
{
	int idx = mrone->mo_punch_iod_num;
	int rc;

	D_ASSERT(iod->iod_size == 0);

	if (mrone->mo_punch_iods == NULL) {
		D_ALLOC_ARRAY(mrone->mo_punch_iods, mrone->mo_iod_alloc_num);
		if (mrone->mo_punch_iods == NULL)
			return -DER_NOMEM;
	}

	rc = migrate_iod_sgl_add(mrone->mo_punch_iods, &mrone->mo_punch_iod_num,
				 iod, NULL, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE,
		"idx %d akey "DF_KEY" nr %d size "DF_U64" type %d\n",
		idx, DP_KEY(&iod->iod_name), iod->iod_nr, iod->iod_size,
		iod->iod_type);

	if (mrone->mo_rec_punch_eph < eph)
		mrone->mo_rec_punch_eph = eph;
out:
	return rc;
}

/*
 * Try merge IOD into other IODs.
 *
 * return 0 means all recxs of the IOD are merged.
 * return 1 means not all recxs of the IOD are merged, i.e. it still
 * needs to insert IOD.
 */
static int
migrate_one_merge(struct migrate_one *mo, struct dss_enum_unpack_io *io)
{
	bool	need_insert = false;
	int	i;
	int	rc = 0;

	for (i = 0; i <= io->ui_iods_top; i++) {
		int j;

		if (io->ui_iods[i].iod_nr == 0)
			continue;

		for (j = 0; j < mo->mo_iod_num; j++) {
			if (!daos_iov_cmp(&mo->mo_iods[j].iod_name,
					  &io->ui_iods[i].iod_name))
				continue;
			if (mo->mo_iods[j].iod_type == DAOS_IOD_ARRAY) {
				rc = migrate_merge_iod_recx(&mo->mo_iods[j],
							    &io->ui_iods[i]);
				if (rc)
					D_GOTO(out, rc);

				/* If recxs can be merged to other iods, then
				 * it do not need to be processed anymore
				 */
				io->ui_iods[i].iod_nr = 0;
			}
			break;
		}
		if (j == mo->mo_iod_num)
			need_insert = true;
	}

	if (need_insert)
		rc = 1;
out:
	return rc;
}

struct enum_unpack_arg {
	struct iter_obj_arg	*arg;
	struct daos_oclass_attr	oc_attr;
	daos_epoch_range_t	epr;
	d_list_t		merge_list;
	uint32_t		iterate_parity:1,
				invalid_inline_sgl:1;
};

static int
migrate_one_insert(struct enum_unpack_arg *arg,
		   struct dss_enum_unpack_io *io, daos_epoch_t epoch)
{
	struct iter_obj_arg	*iter_arg = arg->arg;
	daos_unit_oid_t		oid = io->ui_oid;
	daos_key_t		*dkey = &io->ui_dkey;
	daos_epoch_t		dkey_punch_eph = io->ui_dkey_punch_eph;
	daos_epoch_t		obj_punch_eph = io->ui_obj_punch_eph;
	daos_iod_t		*iods = io->ui_iods;
	daos_epoch_t		*akey_ephs = io->ui_akey_punch_ephs;
	daos_epoch_t		*rec_ephs = io->ui_rec_punch_ephs;
	int			iod_eph_total = io->ui_iods_top + 1;
	d_sg_list_t		*sgls = io->ui_sgls;
	uint32_t		version = io->ui_version;
	struct migrate_pool_tls *tls;
	struct migrate_one	*mrone = NULL;
	bool			inline_copy = true;
	int			i;
	int			rc = 0;

	D_DEBUG(DB_REBUILD, "migrate dkey "DF_KEY" iod nr %d\n", DP_KEY(dkey),
		iod_eph_total);

	tls = migrate_pool_tls_lookup(iter_arg->pool_uuid, iter_arg->version);
	if (tls == NULL || tls->mpt_fini) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
		       DP_UUID(iter_arg->pool_uuid));
		D_GOTO(put, rc = 0);
	}
	if (iod_eph_total == 0 || tls->mpt_version <= version ||
	    tls->mpt_fini) {
		D_DEBUG(DB_REBUILD, "No need eph_total %d version %u"
			" migrate ver %u fini %d\n", iod_eph_total, version,
			tls->mpt_version, tls->mpt_fini);
		D_GOTO(put, rc = 0);
	}

	D_ALLOC_PTR(mrone);
	if (mrone == NULL)
		D_GOTO(put, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&mrone->mo_list);
	D_ALLOC_ARRAY(mrone->mo_iods, iod_eph_total);
	if (mrone->mo_iods == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	mrone->mo_epoch = arg->epr.epr_hi;
	mrone->mo_update_epoch = epoch;
	mrone->mo_obj_punch_eph = obj_punch_eph;
	mrone->mo_dkey_punch_eph = dkey_punch_eph;
	D_ALLOC_ARRAY(mrone->mo_akey_punch_ephs, iod_eph_total);
	if (mrone->mo_akey_punch_ephs == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	mrone->mo_oid = oid;
	mrone->mo_oid.id_shard = iter_arg->shard;
	uuid_copy(mrone->mo_cont_uuid, iter_arg->cont_uuid);
	uuid_copy(mrone->mo_pool_uuid, tls->mpt_pool_uuid);
	mrone->mo_pool_tls_version = tls->mpt_version;
	mrone->mo_iod_alloc_num = iod_eph_total;

	/* only do the copy below when each with inline recx data */
	for (i = 0; i < iod_eph_total; i++) {
		int j;

		if (sgls[i].sg_nr == 0 || sgls[i].sg_iovs == NULL ||
		    arg->invalid_inline_sgl) {
			inline_copy = false;
			break;
		}

		for (j = 0; j < sgls[i].sg_nr; j++) {
			if (sgls[i].sg_iovs[j].iov_len == 0 ||
			    sgls[i].sg_iovs[j].iov_buf == NULL) {
				inline_copy = false;
				break;
			}
		}

		if (!inline_copy)
			break;
	}
	for (i = 0; i < iod_eph_total; i++) {
		/* Pack punched epoch here */
		mrone->mo_akey_punch_ephs[i] = akey_ephs[i];
		if (akey_ephs[i] != 0)
			D_DEBUG(DB_TRACE, "punched %d akey "DF_KEY" "
				DF_U64"\n", i, DP_KEY(&iods[i].iod_name),
				akey_ephs[i]);

		if (iods[i].iod_nr == 0)
			continue;

		if (iods[i].iod_size == 0) {
			rc = punch_iod_pack(mrone, &iods[i], rec_ephs[i]);
			if (rc)
				D_GOTO(free, rc);
		} else {
			rc = rw_iod_pack(mrone, &iods[i],
					 inline_copy ? &sgls[i] : NULL);
			if (rc)
				D_GOTO(free, rc);
		}
	}

	rc = daos_iov_copy(&mrone->mo_csum_iov, &io->ui_csum_iov);
	if (rc != 0)
		D_GOTO(free, rc);

	mrone->mo_version = version;
	D_DEBUG(DB_TRACE, "create migrate dkey ult %d\n", iter_arg->tgt_idx);

	rc = daos_iov_copy(&mrone->mo_dkey, dkey);
	if (rc != 0)
		D_GOTO(free, rc);

	D_DEBUG(DB_REBUILD, DF_UOID" %p dkey "DF_KEY" migrate on idx %d"
		" iod_num %d\n", DP_UOID(mrone->mo_oid), mrone,
		DP_KEY(dkey), iter_arg->tgt_idx,
		mrone->mo_iod_num);

	d_list_add(&mrone->mo_list, &arg->merge_list);

free:
	if (rc != 0 && mrone != NULL) {
		d_list_del_init(&mrone->mo_list);
		migrate_one_destroy(mrone);
	}
put:
	if (tls)
		migrate_pool_tls_put(tls);
	return rc;
}

static int
migrate_enum_unpack_cb(struct dss_enum_unpack_io *io, void *data)
{
	struct enum_unpack_arg	*arg = data;
	struct migrate_one	*mo;
	bool			merged = false;
	daos_epoch_t		epoch = arg->epr.epr_hi;
	int			rc = 0;
	int			i;

	if (daos_oclass_is_ec(&arg->oc_attr)) {
		/* Convert EC object offset to DAOS offset. */
		for (i = 0; i <= io->ui_iods_top; i++) {
			daos_iod_t	*iod = &io->ui_iods[i];
			uint32_t	shard;

			if (iod->iod_type == DAOS_IOD_SINGLE)
				continue;

			rc = obj_recx_ec2_daos(&arg->oc_attr,
					       io->ui_oid.id_shard,
					       &iod->iod_recxs, &iod->iod_nr);
			if (rc != 0)
				return rc;

			/* After convert to DAOS offset, there might be
			 * some duplicate recxs due to replication/parity
			 * space. let's remove them.
			 */
			rc = migrate_merge_iod_recx(iod, NULL);
			if (rc)
				return rc;

			shard = arg->arg->shard % obj_ec_tgt_nr(&arg->oc_attr);
			/* For data shard, convert to single shard offset */
			if (shard < obj_ec_data_tgt_nr(&arg->oc_attr)) {
				D_DEBUG(DB_REBUILD, "convert shard %u tgt %d\n",
					shard,
					obj_ec_data_tgt_nr(&arg->oc_attr));
				/* data shard */
				rc = obj_recx_ec_daos2shard(&arg->oc_attr,
							    shard,
							    &iod->iod_recxs,
							    &iod->iod_nr);
				if (rc)
					return rc;

				/* To avoid aligning  inline sgl, so let's set
				 * invalid_inline_sgl and force re-fetching
				 * the online data.
				 */
				arg->invalid_inline_sgl = 1;
				/* No data needs to be migrate. */
				if (iod->iod_nr == 0)
					continue;
				/* NB: data epoch can not be larger than
				 * parity epoch, otherwise it will cause
				 * issue in degraded fetch, since it will
				 * use the parity epoch to fetch data
				 */
				if (io->ui_rec_min_ephs[i] < epoch)
					epoch = io->ui_rec_min_ephs[i];
			}
		}

		d_list_for_each_entry(mo, &arg->merge_list, mo_list) {
			if (daos_oid_cmp(mo->mo_oid.id_pub,
					 io->ui_oid.id_pub) == 0 &&
			    daos_key_match(&mo->mo_dkey, &io->ui_dkey)) {
				rc = migrate_one_merge(mo, io);
				if (rc != 1) {
					if (rc == 0)
						merged = true;
					break;
				}
			}
		}
	}

	if (!merged)
		rc = migrate_one_insert(arg, io, epoch);

	return rc;
}

static int
migrate_obj_punch_one(void *data)
{
	struct migrate_pool_tls *tls;
	struct iter_obj_arg	*arg = data;
	struct ds_cont_child	*cont;
	int			rc;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	if (tls == NULL || tls->mpt_fini) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
		       DP_UUID(arg->pool_uuid));
		D_GOTO(put, rc = 0);
	}
	D_DEBUG(DB_REBUILD, "tls %p "DF_UUID" version %d punch "DF_UOID"\n",
		tls, DP_UUID(tls->mpt_pool_uuid), arg->version,
		DP_UOID(arg->oid));
	rc = ds_cont_child_lookup(tls->mpt_pool_uuid, arg->cont_uuid, &cont);
	D_ASSERT(rc == 0);

	rc = vos_obj_punch(cont->sc_hdl, arg->oid, arg->epoch,
			   tls->mpt_version, VOS_OF_REPLAY_PC,
			   NULL, 0, NULL, NULL);
	ds_cont_child_put(cont);
	if (rc)
		D_ERROR(DF_UOID" migrate punch failed: "DF_RC"\n",
			DP_UOID(arg->oid), DP_RC(rc));
put:
	if (tls)
		migrate_pool_tls_put(tls);
	return rc;
}

static int
migrate_start_ult(struct enum_unpack_arg *unpack_arg)
{
	struct migrate_pool_tls *tls;
	struct iter_obj_arg	*arg = unpack_arg->arg;
	struct migrate_one	*mrone;
	struct migrate_one	*tmp;
	int			rc = 0;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	if (tls == NULL || tls->mpt_fini) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
		       DP_UUID(arg->pool_uuid));
		D_GOTO(put, rc = 0);
	}
	d_list_for_each_entry_safe(mrone, tmp, &unpack_arg->merge_list,
				   mo_list) {
		/* Recover the OID (with correct shard) after merging IOD
		 * from all shards.
		 */
		mrone->mo_oid = arg->oid;
		D_DEBUG(DB_REBUILD, DF_UOID" %p dkey "DF_KEY" migrate on idx %d"
			" iod_num %d\n", DP_UOID(mrone->mo_oid), mrone,
			DP_KEY(&mrone->mo_dkey), arg->tgt_idx,
			mrone->mo_iod_num);

		d_list_del_init(&mrone->mo_list);
		rc = dss_ult_create(migrate_one_ult, mrone, DSS_XS_VOS,
				    arg->tgt_idx, MIGRATE_STACK_SIZE, NULL);
		if (rc) {
			migrate_one_destroy(mrone);
			break;
		}
		tls->mpt_generated_ult++;
	}

put:
	if (tls)
		migrate_pool_tls_put(tls);
	return rc;
}

#define KDS_NUM		16
#define ITER_BUF_SIZE	2048

/**
 * Iterate akeys/dkeys of the object
 */
static int
migrate_one_epoch_object(daos_epoch_range_t *epr, struct migrate_pool_tls *tls,
			 struct iter_obj_arg *arg)
{
	daos_anchor_t		 anchor;
	daos_anchor_t		 dkey_anchor;
	daos_anchor_t		 akey_anchor;
	char			 stack_buf[ITER_BUF_SIZE] = {0};
	char			*buf = NULL;
	daos_size_t		 buf_len;
	daos_key_desc_t		 kds[KDS_NUM] = {0};
	d_iov_t			 csum = {0};
	uint8_t			 stack_csum_buf[CSUM_BUF_SIZE] = {0};
	struct cont_props	 props;
	struct enum_unpack_arg	 unpack_arg = { 0 };
	d_iov_t			 iov = { 0 };
	d_sg_list_t		 sgl = { 0 };
	daos_handle_t		 poh = DAOS_HDL_INVAL;
	daos_handle_t		 coh = DAOS_HDL_INVAL;
	daos_handle_t		 oh  = DAOS_HDL_INVAL;
	uint32_t		 num;
	int			 rc = 0;

	D_DEBUG(DB_REBUILD, "migrate obj "DF_UOID" for shard %u eph "
		DF_U64"-"DF_U64"\n", DP_UOID(arg->oid), arg->shard, epr->epr_lo,
		epr->epr_hi);

	rc = dsc_pool_open(tls->mpt_pool_uuid, tls->mpt_poh_uuid, 0,
			   NULL, tls->mpt_pool->spc_pool->sp_map,
			   &tls->mpt_svc_list, &poh);
	if (rc) {
		D_ERROR("dsc_pool_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dsc_cont_open(poh, arg->cont_uuid, tls->mpt_coh_uuid, 0, &coh);
	if (rc) {
		D_ERROR("dsc_cont_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_pool, rc);
	}

	rc = dsc_obj_open(coh, arg->oid.id_pub, DAOS_OO_RW, &oh);
	if (rc) {
		D_ERROR("dsc_obj_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_cont, rc);
	}

	memset(&anchor, 0, sizeof(anchor));
	memset(&dkey_anchor, 0, sizeof(dkey_anchor));
	dc_obj_shard2anchor(&dkey_anchor, arg->shard);
	memset(&akey_anchor, 0, sizeof(akey_anchor));
	unpack_arg.arg = arg;
	unpack_arg.epr = *epr;
	D_INIT_LIST_HEAD(&unpack_arg.merge_list);
	buf = stack_buf;
	buf_len = ITER_BUF_SIZE;

	dsc_cont_get_props(coh, &props);
	rc = dsc_obj_id2oc_attr(arg->oid.id_pub, &props, &unpack_arg.oc_attr);
	if (rc) {
		D_ERROR("Unknown object class: %d\n",
			daos_obj_id2class(arg->oid.id_pub));
		D_GOTO(out_cont, rc);
	}

	d_iov_set(&csum, stack_csum_buf, CSUM_BUF_SIZE);
	while (!tls->mpt_fini) {
		memset(buf, 0, buf_len);
		memset(kds, 0, KDS_NUM * sizeof(*kds));
		iov.iov_len = 0;
		iov.iov_buf = buf;
		iov.iov_buf_len = buf_len;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;

		csum.iov_len = 0;

		num = KDS_NUM;
		daos_anchor_set_flags(&dkey_anchor,
				      DIOF_TO_LEADER | DIOF_WITH_SPEC_EPOCH |
				      DIOF_TO_SPEC_GROUP | DIOF_FOR_MIGRATION);
retry:
		unpack_arg.invalid_inline_sgl = 0;
		rc = dsc_obj_list_obj(oh, epr, NULL, NULL, NULL,
				     &num, kds, &sgl, &anchor,
				     &dkey_anchor, &akey_anchor, &csum);

		if (rc == -DER_KEY2BIG) {
			D_DEBUG(DB_REBUILD, "migrate obj "DF_UOID" got "
				"-DER_KEY2BIG, key_len "DF_U64"\n",
				DP_UOID(arg->oid), kds[0].kd_key_len);
			buf_len = roundup(kds[0].kd_key_len * 2, 8);
			if (buf != stack_buf)
				D_FREE(buf);
			D_ALLOC(buf, buf_len);
			if (buf == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			continue;
		} else if (rc == -DER_TRUNC &&
			   csum.iov_len > csum.iov_buf_len) {
			D_DEBUG(DB_REBUILD, "migrate obj csum buf "
				"not large enough. Increase and try again");
			if (csum.iov_buf != stack_csum_buf)
				D_FREE(csum.iov_buf);

			csum.iov_buf_len = csum.iov_len;
			csum.iov_len = 0;
			D_ALLOC(csum.iov_buf, csum.iov_buf_len);
			if (csum.iov_buf == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			continue;
		} else if (rc && daos_anchor_get_flags(&dkey_anchor) &
			   DIOF_TO_LEADER) {
			if (rc != -DER_INPROGRESS) {
				daos_anchor_set_flags(&dkey_anchor,
						      DIOF_WITH_SPEC_EPOCH |
						      DIOF_TO_SPEC_GROUP);
				D_DEBUG(DB_REBUILD, "retry to non leader "
					DF_UOID": "DF_RC"\n",
					DP_UOID(arg->oid), DP_RC(rc));
			} else {
				/* Keep retry on leader if it is inprogress,
				 * since the new dtx leader might still
				 * resync the uncommitted records.
				 */
				D_DEBUG(DB_REBUILD, "retry leader "DF_UOID"\n",
					DP_UOID(arg->oid));
			}
			D_GOTO(retry, rc);
		} else if (rc) {
			/* container might have been destroyed. Or there is
			 * no spare target left for this object see
			 * obj_grp_valid_shard_get()
			 */
			/* DER_DATA_LOSS means it can not find any replicas
			 * to rebuild the data, see obj_list_common.
			 */
			/* If the container is being destroyed, it may return
			 * -DER_NONEXIST, see obj_ioc_init().
			 */
			if (rc == -DER_DATA_LOSS || rc == -DER_NONEXIST) {
				D_DEBUG(DB_REBUILD, "No replicas for "DF_UOID
					" %d\n", DP_UOID(arg->oid), rc);
				num = 0;
				rc = 0;
			}

			D_DEBUG(DB_REBUILD, "Can not rebuild "
				DF_UOID"\n", DP_UOID(arg->oid));
			break;
		}

		/* Each object enumeration RPC will at least one OID */
		if (num < 2) {
			D_DEBUG(DB_REBUILD, "enumeration buffer %u empty"
				DF_UOID"\n", num, DP_UOID(arg->oid));
			break;
		}

		rc = dss_enum_unpack(arg->oid, kds, num, &sgl, &csum,
				     migrate_enum_unpack_cb, &unpack_arg);
		if (rc) {
			D_ERROR("migrate "DF_UOID" failed: %d\n",
				DP_UOID(arg->oid), rc);
			break;
		}

		rc = migrate_start_ult(&unpack_arg);
		if (rc) {
			D_ERROR("start migrate "DF_UOID" failed: "DF_RC"\n",
				DP_UOID(arg->oid), DP_RC(rc));
			break;
		}

		if (daos_anchor_is_eof(&dkey_anchor))
			break;
	}

	if (buf != NULL && buf != stack_buf)
		D_FREE(buf);

	if (csum.iov_buf != NULL && csum.iov_buf != stack_csum_buf)
		D_FREE(csum.iov_buf);

	dsc_obj_close(oh);
out_cont:
	dsc_cont_close(poh, coh);
out_pool:
	dsc_pool_close(poh);
out:
	D_DEBUG(DB_REBUILD, "obj "DF_UOID" for shard %u eph "
		DF_U64"-"DF_U64": "DF_RC"\n", DP_UOID(arg->oid), arg->shard,
		epr->epr_lo, epr->epr_hi, DP_RC(rc));

	return rc;
}

void
ds_migrate_fini_one(uuid_t pool_uuid, uint32_t ver)
{
	struct migrate_pool_tls *tls;

	tls = migrate_pool_tls_lookup(pool_uuid, ver);
	if (tls == NULL)
		return;

	tls->mpt_fini = 1;

	ABT_mutex_lock(tls->mpt_inflight_mutex);
	ABT_cond_broadcast(tls->mpt_inflight_cond);
	ABT_mutex_unlock(tls->mpt_inflight_mutex);
	migrate_pool_tls_put(tls); /* lookup */
	migrate_pool_tls_put(tls); /* destroy */
}

struct migrate_abort_arg {
	uuid_t	pool_uuid;
	uint32_t version;
};

int
migrate_fini_one_ult(void *data)
{
	struct migrate_abort_arg *arg = data;
	struct migrate_pool_tls	*tls;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	if (tls == NULL)
		return 0;

	D_ASSERT(tls->mpt_refcount > 1);
	tls->mpt_fini = 1;

	ABT_mutex_lock(tls->mpt_inflight_mutex);
	ABT_cond_broadcast(tls->mpt_inflight_cond);
	ABT_mutex_unlock(tls->mpt_inflight_mutex);

	ABT_eventual_wait(tls->mpt_done_eventual, NULL);
	migrate_pool_tls_put(tls); /* destroy */

	D_DEBUG(DB_TRACE, "abort one ult "DF_UUID"\n", DP_UUID(arg->pool_uuid));
	return 0;
}

/* Abort the migration */
void
ds_migrate_abort(uuid_t pool_uuid, unsigned int version)
{
	struct migrate_pool_tls *tls;
	struct migrate_abort_arg arg;
	int			 rc;

	tls = migrate_pool_tls_lookup(pool_uuid, version);
	if (tls == NULL)
		return;

	uuid_copy(arg.pool_uuid, pool_uuid);
	arg.version = version;
	rc = dss_thread_collective(migrate_fini_one_ult, &arg, 0);
	if (rc)
		D_ERROR("migrate abort: %d\n", rc);

	migrate_pool_tls_put(tls);
}

static int
migrate_obj_punch(struct iter_obj_arg *arg)
{
	return dss_task_collective(migrate_obj_punch_one, arg, 0);
}

/* Destroys an object prior to migration. Called exactly once per object ID per
 * container on the appropriate VOS target xstream.
 */
static int
destroy_existing_obj(struct migrate_pool_tls *tls, unsigned int tgt_idx,
		     daos_unit_oid_t *oid, uuid_t cont_uuid)
{
	struct ds_cont_child *cont;
	int rc;

	rc = ds_cont_child_open_create(tls->mpt_pool_uuid, cont_uuid, &cont);
	if (rc == -DER_SHUTDOWN) {
		D_DEBUG(DB_REBUILD, DF_UUID "container is being destroyed\n",
			DP_UUID(cont_uuid));
		return 0;
	}

	if (rc != 0) {
		D_ERROR("Failed to open cont to clear obj before migrate; pool="
			DF_UUID" cont="DF_UUID"\n",
			DP_UUID(tls->mpt_pool_uuid), DP_UUID(cont_uuid));
		return rc;
	}

	rc = vos_obj_delete(cont->sc_hdl, *oid);
	if (rc != 0) {
		D_ERROR("Migrate failed to destroy object prior to "
			"reintegration: pool/object "DF_UUID"/"DF_UOID
			" rc: "DF_RC"\n", DP_UUID(tls->mpt_pool_uuid),
			DP_UOID(*oid), DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_REBUILD, "destroyed pool/object "DF_UUID"/"DF_UOID
		" before reintegration\n",
		DP_UUID(tls->mpt_pool_uuid), DP_UOID(*oid));

	ds_cont_child_put(cont);

	return DER_SUCCESS;
}

/**
 * This ULT manages migration one object ID for one container. It does not do
 * the data migration itself - instead it iterates akeys/dkeys as a client and
 * schedules the actual data migration on their own ULTs
 *
 * If this is reintegration (mpt_del_local_objs==true), this ULT will be
 * launched on the target where data is stored so that it can be safely deleted
 * prior to migration. If this not reintegration (mpt_del_local_objs==false),
 * this ULT will be launched on a pseudorandom ULT to increase parallelism by
 * spreading the work among many xstreams.
 *
 * Note that this ULT is guaranteed to only be spawned once per object per
 * container per migration session (using mpt_migrated_root)
 */
static void
migrate_obj_ult(void *data)
{
	struct iter_obj_arg	*arg = data;
	struct migrate_pool_tls	*tls = NULL;
	daos_epoch_range_t	 epr;
	int			 i;
	int			 rc;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	if (tls == NULL || tls->mpt_fini) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
		       DP_UUID(arg->pool_uuid));
		D_GOTO(free, rc = 0);
	}

	if (tls->mpt_del_local_objs) {
		/* Destroy this object ID locally prior to migration */
		rc = destroy_existing_obj(tls, arg->tgt_idx, &arg->oid,
					  arg->cont_uuid);
		if (rc) {
			/* Something went wrong trying to destroy this object.
			 * Since destroying objects prior to migration is
			 * currently only used for reintegration, it makes sense
			 * to fail the reintegration operation at this point
			 * rather than proceeding and potentially losing data
			 */
			D_ERROR("destroy_existing_obj failed: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(free, rc);
		}
	}

	if (arg->epoch != DAOS_EPOCH_MAX) {
		rc = migrate_obj_punch(arg);
		if (rc)
			D_GOTO(free, rc);
	}

	for (i = 0; i < arg->snap_cnt; i++) {
		epr.epr_lo = i > 0 ? arg->snaps[i-1] + 1 : 0;
		epr.epr_hi = arg->snaps[i];
		rc = migrate_one_epoch_object(&epr, tls, arg);
		if (rc)
			D_GOTO(free, rc);
	}

	epr.epr_lo = arg->snaps ? arg->snaps[arg->snap_cnt - 1] + 1 : 0;
	D_ASSERT(tls->mpt_max_eph != 0);
	epr.epr_hi = tls->mpt_max_eph;
	rc = migrate_one_epoch_object(&epr, tls, arg);

free:
	if (arg->epoch == DAOS_EPOCH_MAX)
		tls->mpt_obj_count++;

	tls->mpt_obj_executed_ult++;
	if (rc == -DER_NONEXIST) {
		struct ds_cont_child *cont_child = NULL;
		int ret;

		ret = ds_cont_child_lookup(tls->mpt_pool_uuid, arg->cont_uuid,
					   &cont_child);
		if (ret != 0 || cont_child->sc_stopping) {
			/**
			 * If the current container is being destroyed, let's
			 * ignore the -DER_NONEXIST failure.
			 */
			D_DEBUG(DB_REBUILD, DF_UUID" status %d:%d\n",
				DP_UUID(arg->cont_uuid), ret,
				cont_child ? cont_child->sc_stopping : 0);
			rc = 0;
		}

		if (cont_child)
			ds_cont_child_put(cont_child);
	}

	if (tls->mpt_status == 0 && rc < 0)
		tls->mpt_status = rc;

	D_DEBUG(DB_REBUILD, ""DF_UUID"/%u stop migrate obj "DF_UOID
		" for shard %u executed "DF_U64" : " DF_RC"\n",
		DP_UUID(tls->mpt_pool_uuid), tls->mpt_version,
		DP_UOID(arg->oid), arg->shard, tls->mpt_obj_executed_ult,
		DP_RC(rc));
	D_FREE(arg->snaps);
	D_FREE(arg);
	migrate_pool_tls_put(tls);
}

struct migrate_obj_val {
	daos_epoch_t	epoch;
	uint32_t	shard;
	uint32_t	tgt_idx;
};

/* This is still running on the main migration ULT */
static int
migrate_one_object(daos_unit_oid_t oid, daos_epoch_t eph, unsigned int shard,
		   unsigned int tgt_idx, void *data)
{
	struct iter_cont_arg	*cont_arg = data;
	struct iter_obj_arg	*obj_arg;
	struct migrate_pool_tls *tls = cont_arg->pool_tls;
	daos_handle_t		 toh = tls->mpt_migrated_root_hdl;
	struct migrate_obj_val	 val;
	d_iov_t			 val_iov;
	int			 ult_tgt_idx;
	int			 rc;

	D_ASSERT(daos_handle_is_valid(toh));

	D_ALLOC_PTR(obj_arg);
	if (obj_arg == NULL)
		return -DER_NOMEM;

	obj_arg->oid = oid;
	obj_arg->epoch = eph;
	obj_arg->shard = shard;
	obj_arg->tgt_idx = tgt_idx;
	uuid_copy(obj_arg->pool_uuid, cont_arg->pool_tls->mpt_pool_uuid);
	uuid_copy(obj_arg->cont_uuid, cont_arg->cont_uuid);
	obj_arg->version = cont_arg->pool_tls->mpt_version;
	if (cont_arg->snaps) {
		D_ALLOC(obj_arg->snaps,
			sizeof(*cont_arg->snaps) * cont_arg->snap_cnt);
		if (obj_arg->snaps == NULL)
			D_GOTO(free, rc = -DER_NOMEM);

		obj_arg->snap_cnt = cont_arg->snap_cnt;
		memcpy(obj_arg->snaps, cont_arg->snaps,
		       sizeof(*obj_arg->snaps) * cont_arg->snap_cnt);
	}

	if (cont_arg->pool_tls->mpt_del_local_objs) {
		/* This ULT will need to destroy objects prior to migration. To
		 * do this it must be scheduled on the xstream where that data
		 * is stored.
		 */
		ult_tgt_idx = tgt_idx;
	} else {
		/* This ULT will not need to destroy data, it will act as a
		 * client to enumerate the data to migrate, then migrate that
		 * data on a different ULT that is pinned to the appropriate
		 * target.
		 *
		 * Because no data migration happens here, schedule this
		 * pseudorandomly to get better performance by leveraging all
		 * the xstreams.
		 */
		ult_tgt_idx = oid.id_pub.lo % dss_tgt_nr;
	}

	/* Let's iterate the object on different xstream */
	rc = dss_ult_create(migrate_obj_ult, obj_arg, DSS_XS_VOS,
			    ult_tgt_idx, MIGRATE_STACK_SIZE,
			    NULL);
	if (rc)
		goto free;

	tls->mpt_obj_generated_ult++;

	val.epoch = eph;
	val.shard = shard;
	val.tgt_idx = tgt_idx;

	d_iov_set(&val_iov, &val, sizeof(struct migrate_obj_val));
	rc = obj_tree_insert(toh, cont_arg->cont_uuid, oid, &val_iov);
	D_DEBUG(DB_REBUILD, "Insert "DF_UUID"/"DF_UUID"/"DF_UOID": ver %u "
		"generated "DF_U64" "DF_RC"\n", DP_UUID(tls->mpt_pool_uuid),
		DP_UUID(cont_arg->cont_uuid), DP_UOID(oid), tls->mpt_version,
		tls->mpt_obj_generated_ult, DP_RC(rc));

	return 0;

free:
	D_FREE(obj_arg->snaps);
	D_FREE(obj_arg);
	return rc;
}

#define DEFAULT_YIELD_FREQ	128

static int
migrate_obj_iter_cb(daos_handle_t ih, d_iov_t *key_iov, d_iov_t *val_iov,
		    void *data)
{
	struct iter_cont_arg		*arg = data;
	daos_unit_oid_t			*oid = key_iov->iov_buf;
	struct migrate_obj_val		*obj_val = val_iov->iov_buf;
	daos_epoch_t			epoch = obj_val->epoch;
	unsigned int			tgt_idx = obj_val->tgt_idx;
	unsigned int			shard = obj_val->shard;
	int				rc;

	if (arg->pool_tls->mpt_fini)
		return 1;

	D_DEBUG(DB_REBUILD, "obj migrate "DF_UUID"/"DF_UOID" %"PRIx64
		" eph "DF_U64" start\n", DP_UUID(arg->cont_uuid), DP_UOID(*oid),
		ih.cookie, epoch);

	rc = migrate_one_object(*oid, epoch, shard, tgt_idx, arg);
	if (rc != 0) {
		D_ERROR("obj "DF_UOID" migration failed: "DF_RC"\n",
			DP_UOID(*oid), DP_RC(rc));
		return rc;
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc) {
		D_ERROR("dbtree_iter_delete failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (--arg->yield_freq == 0) {
		arg->yield_freq = DEFAULT_YIELD_FREQ;
		ABT_thread_yield();
	}

	/* re-probe the dbtree after deletion */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_MIGRATION,
			       NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;
	else if (rc != 0)
		D_ERROR("dbtree_iter_probe failed: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/* This iterates the migration database "container", which is different than the
 * similarly identified by container UUID as the actual container in VOS.
 * However, this container only contains object IDs that were specified to be
 * migrated
 */
static int
migrate_cont_iter_cb(daos_handle_t ih, d_iov_t *key_iov,
		     d_iov_t *val_iov, void *data)
{
	struct ds_pool		*dp;
	struct iter_cont_arg	 arg = { 0 };
	struct tree_cache_root	*root = val_iov->iov_buf;
	struct migrate_pool_tls	*tls = data;
	uint64_t		*snapshots = NULL;
	uuid_t			 cont_uuid;
	int			 snap_cnt;
	d_iov_t			 tmp_iov;
	int			 rc;

	uuid_copy(cont_uuid, *(uuid_t *)key_iov->iov_buf);
	D_DEBUG(DB_REBUILD, "iter cont "DF_UUID"/%"PRIx64" %"PRIx64" start\n",
		DP_UUID(cont_uuid), ih.cookie, root->root_hdl.cookie);

	dp = ds_pool_lookup(tls->mpt_pool_uuid);
	D_ASSERT(dp != NULL);
	rc = ds_cont_fetch_snaps(dp->sp_iv_ns, cont_uuid, &snapshots,
				 &snap_cnt);
	if (rc) {
		D_ERROR("ds_cont_fetch_snaps failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_put, rc);
	}

	arg.yield_freq	= DEFAULT_YIELD_FREQ;
	arg.obj_cnt	= root->count;
	arg.cont_root	= root;
	arg.snaps	= snapshots;
	arg.snap_cnt	= snap_cnt;
	arg.pool_tls	= tls;
	uuid_copy(arg.cont_uuid, cont_uuid);
	while (!dbtree_is_empty(root->root_hdl)) {
		uint64_t ult_cnt;

		D_ASSERT(tls->mpt_obj_generated_ult >=
			 tls->mpt_obj_executed_ult);
		D_ASSERT(tls->mpt_generated_ult >= tls->mpt_executed_ult);

		ult_cnt = max(tls->mpt_obj_generated_ult -
			      tls->mpt_obj_executed_ult,
			      tls->mpt_generated_ult -
			      tls->mpt_executed_ult);

		while (ult_cnt >= tls->mpt_inflight_max_ult && !tls->mpt_fini) {
			ABT_mutex_lock(tls->mpt_inflight_mutex);
			ABT_cond_wait(tls->mpt_inflight_cond,
				      tls->mpt_inflight_mutex);
			ABT_mutex_unlock(tls->mpt_inflight_mutex);
			ult_cnt = max(tls->mpt_obj_generated_ult -
				      tls->mpt_obj_executed_ult,
				      tls->mpt_generated_ult -
				      tls->mpt_executed_ult);
			D_DEBUG(DB_REBUILD, "obj "DF_U64"/"DF_U64", key"
				DF_U64"/"DF_U64" "DF_U64"\n",
				tls->mpt_obj_generated_ult,
				tls->mpt_obj_executed_ult,
				tls->mpt_generated_ult,
				tls->mpt_executed_ult, ult_cnt);
		}

		if (tls->mpt_fini)
			break;

		rc = dbtree_iterate(root->root_hdl, DAOS_INTENT_MIGRATION,
				    false, migrate_obj_iter_cb, &arg);
		if (rc || tls->mpt_fini)
			break;
	}

	D_DEBUG(DB_REBUILD, "iter cont "DF_UUID"/%"PRIx64" finish.\n",
		DP_UUID(cont_uuid), ih.cookie);

	/* Snapshot fetch will yield the ULT, let's reprobe before delete  */
	d_iov_set(&tmp_iov, cont_uuid, sizeof(uuid_t));
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, DAOS_INTENT_MIGRATION,
			       &tmp_iov, NULL);
	if (rc) {
		D_ASSERT(rc != -DER_NONEXIST);
		D_GOTO(free, rc);
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc) {
		D_ERROR("dbtree_iter_delete failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(free, rc);
	}

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_MIGRATION,
			       NULL, NULL);

	if (rc == -DER_NONEXIST) {
		rc = 1; /* empty after delete */
		D_GOTO(free, rc);
	}
free:
	if (snapshots)
		D_FREE(snapshots);

out_put:
	if (tls->mpt_status == 0 && rc < 0)
		tls->mpt_status = rc;
	ds_pool_put(dp);
	return rc;
}

struct migrate_ult_arg {
	uuid_t		pool_uuid;
	uint32_t	version;
};

static void
migrate_ult(void *arg)
{
	struct migrate_pool_tls	*pool_tls = arg;
	int			rc;

	D_ASSERT(pool_tls != NULL);
	while (!dbtree_is_empty(pool_tls->mpt_root_hdl)) {
		rc = dbtree_iterate(pool_tls->mpt_root_hdl,
				    DAOS_INTENT_PURGE, false,
				    migrate_cont_iter_cb, pool_tls);
		if (rc < 0) {
			D_ERROR("dbtree iterate failed: "DF_RC"\n", DP_RC(rc));
			if (pool_tls->mpt_status == 0)
				pool_tls->mpt_status = rc;
			break;
		}
	}

	pool_tls->mpt_ult_running = 0;
	migrate_pool_tls_put(pool_tls);
}

/**
 * Init migrate objects tree, so the migrating objects are added to
 * to-be-migrated tree(mpt_root/mpt_root_hdl) first, then moved to
 * migrated tree once it is being migrated. (mpt_migrated_root/
 * mpt_migrated_root_hdl). The incoming objects will check both
 * tree to see if the objects being migrated already.
 */
static int
migrate_del_object_tree(struct migrate_pool_tls *tls)
{
	struct umem_attr uma;
	int rc;

	if (daos_handle_is_inval(tls->mpt_root_hdl)) {
		/* migrate tree root init */
		memset(&uma, 0, sizeof(uma));
		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_UV, 0, 4, &uma,
					   &tls->mpt_root,
					   &tls->mpt_root_hdl);
		if (rc != 0) {
			D_ERROR("failed to create tree: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	if (daos_handle_is_inval(tls->mpt_migrated_root_hdl)) {
		/* migrate tree root init */
		memset(&uma, 0, sizeof(uma));
		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_UV, 0, 4, &uma,
					   &tls->mpt_migrated_root,
					   &tls->mpt_migrated_root_hdl);
		if (rc != 0) {
			D_ERROR("failed to create tree: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	return 0;
}

/**
 * Only insert objects if the objects does not exist in both
 * tobe-migrated tree and migrated tree.
 */
static int
migrate_try_obj_insert(struct migrate_pool_tls *tls, uuid_t co_uuid,
		       daos_unit_oid_t oid, daos_epoch_t epoch,
		       unsigned int shard, unsigned int tgt_idx)
{
	struct migrate_obj_val	val;
	daos_handle_t		toh = tls->mpt_root_hdl;
	daos_handle_t		migrated_toh = tls->mpt_migrated_root_hdl;
	d_iov_t			val_iov;
	int			rc;

	D_ASSERT(daos_handle_is_valid(toh));
	D_ASSERT(daos_handle_is_valid(migrated_toh));

	val.epoch = epoch;
	val.shard = shard;
	val.tgt_idx = tgt_idx;
	D_DEBUG(DB_REBUILD, "Insert migrate "DF_UUID"/"DF_UOID" "DF_U64
		"/%d/%d\n", DP_UUID(co_uuid), DP_UOID(oid), epoch, shard,
		tgt_idx);

	d_iov_set(&val_iov, &val, sizeof(struct migrate_obj_val));
	rc = obj_tree_lookup(toh, co_uuid, oid, &val_iov);
	if (rc != -DER_NONEXIST) {
		D_DEBUG(DB_REBUILD, DF_UUID"/"DF_UOID" not need insert: "
			DF_RC"\n", DP_UUID(co_uuid), DP_UOID(oid), DP_RC(rc));
		return rc;
	}

	rc = obj_tree_lookup(migrated_toh, co_uuid, oid, &val_iov);
	if (rc != -DER_NONEXIST) {
		D_DEBUG(DB_REBUILD, DF_UUID"/"DF_UOID" not need insert: "
			DF_RC"\n", DP_UUID(co_uuid), DP_UOID(oid), DP_RC(rc));
		return rc;
	}

	rc = obj_tree_insert(toh, co_uuid, oid, &val_iov);

	return rc;
}

/* Got the object list to migrate objects from remote target to
 * this target.
 */
void
ds_obj_migrate_handler(crt_rpc_t *rpc)
{
	struct obj_migrate_in	*migrate_in;
	struct obj_migrate_out	*migrate_out;
	struct migrate_pool_tls *pool_tls = NULL;
	daos_unit_oid_t		*oids;
	unsigned int		oids_count;
	daos_epoch_t		*ephs;
	unsigned int		ephs_count;
	uint32_t		*shards;
	unsigned int		shards_count;
	uuid_t			po_uuid;
	uuid_t			po_hdl_uuid;
	uuid_t			co_uuid;
	uuid_t			co_hdl_uuid;
	struct ds_pool		*pool = NULL;
	unsigned int		i;
	int			rc;

	migrate_in = crt_req_get(rpc);
	oids = migrate_in->om_oids.ca_arrays;
	oids_count = migrate_in->om_oids.ca_count;
	ephs = migrate_in->om_ephs.ca_arrays;
	ephs_count = migrate_in->om_ephs.ca_count;
	shards = migrate_in->om_shards.ca_arrays;
	shards_count = migrate_in->om_shards.ca_count;

	if (oids_count == 0 || shards_count == 0 || ephs_count == 0 ||
	    oids_count != shards_count || oids_count != ephs_count) {
		D_ERROR("oids %u shards %u ephs %d\n",
			oids_count, shards_count, ephs_count);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (migrate_in->om_tgt_idx >= dss_tgt_nr) {
		D_ERROR("Wrong tgt idx %d\n", migrate_in->om_tgt_idx);
		D_GOTO(out, rc = -DER_INVAL);
	}

	uuid_copy(co_uuid, migrate_in->om_cont_uuid);
	uuid_copy(co_hdl_uuid, migrate_in->om_coh_uuid);
	uuid_copy(po_uuid, migrate_in->om_pool_uuid);
	uuid_copy(po_hdl_uuid, migrate_in->om_poh_uuid);

	pool = ds_pool_lookup(po_uuid);
	if (pool == NULL) {
		D_DEBUG(DB_TRACE, DF_UUID" pool service is not started yet\n",
			DP_UUID(po_uuid));
		D_GOTO(out, rc = -DER_AGAIN);
	}

	if (pool->sp_stopping) {
		D_DEBUG(DB_TRACE, DF_UUID" pool service is stopping.\n",
			DP_UUID(po_uuid));
		D_GOTO(out, rc = 0);
	}

	/* Check if the pool tls exists */
	pool_tls = migrate_pool_tls_lookup_create(pool, migrate_in->om_version,
						  po_hdl_uuid, co_hdl_uuid,
						  migrate_in->om_max_eph,
						  migrate_in->om_del_local_obj);
	if (pool_tls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* NB: only create this tree on xstream 0 */
	rc = migrate_del_object_tree(pool_tls);
	if (rc)
		D_GOTO(out, rc);

	/* Insert these oids/conts into the local tree */
	for (i = 0; i < oids_count; i++) {
		/* firstly insert/check rebuilt tree */
		rc = migrate_try_obj_insert(pool_tls, co_uuid, oids[i], ephs[i],
					    shards[i], migrate_in->om_tgt_idx);
		if (rc == -DER_EXIST) {
			D_DEBUG(DB_TRACE, DF_UOID"/"DF_UUID"exists.\n",
				DP_UOID(oids[i]), DP_UUID(co_uuid));
			rc = 0;
			continue;
		} else if (rc < 0) {
			D_ERROR("insert "DF_UOID"/"DF_U64" "DF_UUID
				" shard %u to rebuilt tree failed, rc %d.\n",
				DP_UOID(oids[i]), ephs[i],
				DP_UUID(co_uuid), shards[i], rc);
			break;
		}
	}
	if (rc < 0)
		D_GOTO(out, rc);

	/* Check and create task to iterate the to-be-rebuilt tree */
	if (!pool_tls->mpt_ult_running) {
		pool_tls->mpt_ult_running = 1;
		migrate_pool_tls_get(pool_tls);
		rc = dss_ult_create(migrate_ult, pool_tls, DSS_XS_SELF,
				    0, 0, NULL);
		if (rc) {
			pool_tls->mpt_ult_running = 0;
			migrate_pool_tls_put(pool_tls);
			D_ERROR("Create migrate ULT failed: rc %d\n", rc);
		}
	}
out:
	if (pool)
		ds_pool_put(pool);

	if (pool_tls)
		migrate_pool_tls_put(pool_tls);
	migrate_out = crt_reply_get(rpc);
	migrate_out->om_status = rc;
	dss_rpc_reply(rpc, DAOS_REBUILD_DROP_OBJ);
}

struct migrate_query_arg {
	uuid_t	pool_uuid;
	ABT_mutex status_lock;
	struct ds_migrate_status dms;
	uint32_t obj_generated_ult;
	uint32_t obj_executed_ult;
	uint32_t generated_ult;
	uint32_t executed_ult;
	uint32_t version;
};

static int
migrate_check_one(void *data)
{
	struct migrate_query_arg	*arg = data;
	struct migrate_pool_tls		*tls;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	if (tls == NULL)
		return 0;

	ABT_mutex_lock(arg->status_lock);
	arg->dms.dm_rec_count += tls->mpt_rec_count;
	arg->dms.dm_obj_count += tls->mpt_obj_count;
	arg->dms.dm_total_size += tls->mpt_size;
	arg->obj_generated_ult += tls->mpt_obj_generated_ult;
	arg->obj_executed_ult += tls->mpt_obj_executed_ult;
	arg->generated_ult += tls->mpt_generated_ult;
	arg->executed_ult += tls->mpt_executed_ult;
	if (arg->dms.dm_status == 0)
		arg->dms.dm_status = tls->mpt_status;
	ABT_mutex_unlock(arg->status_lock);

	D_DEBUG(DB_REBUILD, "status %d/%d  rec/obj/size "
		DF_U64"/"DF_U64"/"DF_U64"\n", tls->mpt_status,
		arg->dms.dm_status, tls->mpt_rec_count,
		tls->mpt_obj_count, tls->mpt_size);

	migrate_pool_tls_put(tls);
	return 0;
}

int
ds_migrate_query_status(uuid_t pool_uuid, uint32_t ver,
			struct ds_migrate_status *dms)
{
	struct migrate_query_arg	arg = { 0 };
	struct migrate_pool_tls		*tls;
	int				rc;

	tls = migrate_pool_tls_lookup(pool_uuid, ver);
	if (tls == NULL)
		return 0;

	uuid_copy(arg.pool_uuid, pool_uuid);
	arg.version = ver;
	rc = ABT_mutex_create(&arg.status_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc);

	rc = dss_thread_collective(migrate_check_one, &arg, 0);
	if (rc)
		D_GOTO(out, rc);

	/**
	 * The object ULT is generated by 0 xstream, and dss_collective does not
	 * do collective on 0 xstream
	 **/
	arg.obj_generated_ult += tls->mpt_obj_generated_ult;
	tls->mpt_obj_executed_ult = arg.obj_executed_ult;
	tls->mpt_generated_ult = arg.generated_ult;
	tls->mpt_executed_ult = arg.executed_ult;
	*dms = arg.dms;
	if (arg.obj_generated_ult > arg.obj_executed_ult ||
	    arg.generated_ult > arg.executed_ult || tls->mpt_ult_running)
		dms->dm_migrating = 1;
	else
		dms->dm_migrating = 0;

	ABT_mutex_lock(tls->mpt_inflight_mutex);
	ABT_cond_broadcast(tls->mpt_inflight_cond);
	ABT_mutex_unlock(tls->mpt_inflight_mutex);

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" ver %u migrating=%s,"
		" obj_count="DF_U64", rec_count="DF_U64
		" size="DF_U64" obj %u/%u general %u/%u status %d\n",
		DP_UUID(pool_uuid), ver, dms->dm_migrating ? "yes" : "no",
		dms->dm_obj_count, dms->dm_rec_count, dms->dm_total_size,
		arg.obj_generated_ult, arg.obj_executed_ult,
		arg.generated_ult, arg.executed_ult, dms->dm_status);
out:
	ABT_mutex_free(&arg.status_lock);
	migrate_pool_tls_put(tls);
	return rc;
}

/**
 * Migrate object from its replicas to the target(@tgt_id).
 *
 * param pool [in]		ds_pool of the pool.
 * param pool_hdl_uuid [in]	pool_handle uuid.
 * param cont_uuid [in]		container uuid.
 * param cont_hdl_uuid [in]	container handle uuid.
 * param tgt_id [in]		target id where the data to be migrated.
 * param max_eph [in]		maxim epoch of the migration.
 * param oids [in]		array of the objects to be migrated.
 * param ephs [in]		epoch of the objects.
 * param shards [in]		it can be NULL, otherwise it indicates
 *				the source shard of the migration, so it
 *				is only used for replicate objects.
 * param cnt [in]		count of objects.
 * param del_local_objs [in]	remove container contents before migrating
 *
 * return			0 if it succeeds, otherwise errno.
 */
int
ds_object_migrate(struct ds_pool *pool, uuid_t pool_hdl_uuid,
		  uuid_t cont_hdl_uuid, uuid_t cont_uuid, int tgt_id,
		  uint32_t version, uint64_t max_eph, daos_unit_oid_t *oids,
		  daos_epoch_t *ephs, unsigned int *shards, int cnt,
		  int del_local_objs)
{
	struct obj_migrate_in	*migrate_in = NULL;
	struct obj_migrate_out	*migrate_out = NULL;
	struct pool_target	*target;
	crt_endpoint_t		tgt_ep = {0};
	crt_opcode_t		opcode;
	unsigned int		index;
	crt_rpc_t		*rpc;
	int			rc;

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = pool_map_find_target(pool->sp_map, tgt_id, &target);
	if (rc != 1 || (target->ta_comp.co_status != PO_COMP_ST_UPIN
			&& target->ta_comp.co_status != PO_COMP_ST_UP
			&& target->ta_comp.co_status != PO_COMP_ST_NEW)) {
		/* Remote target has failed, no need retry, but not
		 * report failure as well and next rebuild will handle
		 * it anyway.
		 */
		ABT_rwlock_unlock(pool->sp_lock);
		D_DEBUG(DB_TRACE, "Can not find tgt %d or target is down %d\n",
			tgt_id, target->ta_comp.co_status);
		return -DER_NONEXIST;
	}

	/* NB: let's send object list to 0 xstream to simplify the migrate
	 * object handling process for now, for example avoid lock to insert
	 * objects in the object tree.
	 */
	tgt_ep.ep_rank = target->ta_comp.co_rank;
	index = target->ta_comp.co_index;
	ABT_rwlock_unlock(pool->sp_lock);
	tgt_ep.ep_tag = 0;
	opcode = DAOS_RPC_OPCODE(DAOS_OBJ_RPC_MIGRATE, DAOS_OBJ_MODULE,
				 DAOS_OBJ_VERSION);
	rc = crt_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep, opcode,
			    &rpc);
	if (rc) {
		D_ERROR("crt_req_create failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	migrate_in = crt_req_get(rpc);
	uuid_copy(migrate_in->om_pool_uuid, pool->sp_uuid);
	uuid_copy(migrate_in->om_poh_uuid, pool_hdl_uuid);
	uuid_copy(migrate_in->om_cont_uuid, cont_uuid);
	uuid_copy(migrate_in->om_coh_uuid, cont_hdl_uuid);
	migrate_in->om_version = version;
	migrate_in->om_max_eph = max_eph,
	migrate_in->om_tgt_idx = index;
	migrate_in->om_del_local_obj = del_local_objs;

	migrate_in->om_oids.ca_arrays = oids;
	migrate_in->om_oids.ca_count = cnt;
	migrate_in->om_ephs.ca_arrays = ephs;
	migrate_in->om_ephs.ca_count = cnt;

	if (shards) {
		migrate_in->om_shards.ca_arrays = shards;
		migrate_in->om_shards.ca_count = cnt;
	}
	rc = dss_rpc_send(rpc);
	if (rc) {
		D_ERROR("dss_rpc_send failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	migrate_out = crt_reply_get(rpc);
	rc = migrate_out->om_status;
out:
	D_DEBUG(DB_REBUILD, DF_UUID" migrate object: %d\n",
		DP_UUID(pool->sp_uuid), rc);
	if (rpc)
		crt_req_decref(rpc);

	return rc;
}
