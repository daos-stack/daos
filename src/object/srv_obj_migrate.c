/**
 * (C) Copyright 2019 Intel Corporation.
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
#include <daos_srv/daos_server.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif
struct migrate_one {
	daos_key_t	mo_dkey;
	uuid_t		mo_pool_uuid;
	uuid_t		mo_cont_uuid;
	daos_unit_oid_t	mo_oid;
	daos_epoch_t	mo_dkey_punch_eph;
	daos_epoch_t	mo_epoch;
	daos_iod_t	*mo_iods;
	daos_iod_t	*mo_punch_iods;
	daos_epoch_t	*mo_akey_punch_ephs;
	daos_epoch_t	mo_rec_punch_eph;
	d_sg_list_t	*mo_sgls;
	unsigned int	mo_iod_num;
	unsigned int	mo_punch_iod_num;
	unsigned int	mo_iod_alloc_num;
	unsigned int	mo_rec_num;
	uint64_t	mo_size;
	uint64_t	mo_version;
	uint32_t	mo_pool_tls_version;
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
	daos_handle_t		cont_hdl;
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
	daos_handle_t		cont_hdl;
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
		D_ERROR("dbtree_destroy, cont "DF_UUID" failed, rc %d.\n",
			DP_UUID(*(uuid_t *)key_iov->iov_buf), rc);

	return rc;
}

int
obj_tree_destroy(daos_handle_t btr_hdl)
{
	int	rc;

	rc = dbtree_iterate(btr_hdl, DAOS_INTENT_PUNCH, false,
			    obj_tree_destory_cb, NULL);
	if (rc) {
		D_ERROR("dbtree iterate fails %d\n", rc);
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

	rc = dbtree_create_inplace(tree_class, 0, 32, &uma, broot,
				   &root.root_hdl);
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
	if (rc < 0 && !daos_handle_is_inval(root.root_hdl))
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
obj_tree_insert(daos_handle_t toh, uuid_t co_uuid, daos_unit_oid_t oid,
		d_iov_t *val_iov)
{
	struct tree_cache_root	*cont_root;
	d_iov_t			key_iov;
	d_iov_t			tmp_iov;
	int			rc;

	/* locate the container first */
	d_iov_set(&key_iov, co_uuid, sizeof(uuid_t));
	d_iov_set(&tmp_iov, NULL, 0);
	rc = dbtree_lookup(toh, &key_iov, &tmp_iov);
	if (rc < 0) {
		if (rc != -DER_NONEXIST) {
			D_ERROR("lookup cont "DF_UUID" failed, rc %d\n",
				DP_UUID(co_uuid), rc);
			return rc;
		}

		D_DEBUG(DB_TRACE, "Create cont "DF_UUID" tree\n",
			DP_UUID(co_uuid));
		rc = container_tree_create(toh, co_uuid, &cont_root);
		if (rc) {
			D_ERROR("tree_create cont "DF_UUID" failed, rc %d\n",
				DP_UUID(co_uuid), rc);
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
		D_ERROR("failed to insert "DF_UOID": rc %d\n",
			DP_UOID(oid), rc);
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
	D_DEBUG(DB_TRACE, "TLS destroy for "DF_UUID" ver %d\n",
		DP_UUID(tls->mpt_pool_uuid), tls->mpt_version);
	if (tls->mpt_pool)
		ds_pool_child_put(tls->mpt_pool);

	d_rank_list_free(&tls->mpt_svc_list);

	obj_tree_destroy(tls->mpt_root_hdl);
	d_list_del(&tls->mpt_list);
	D_FREE(tls);
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
			found = pool_tls;
			pool_tls->mpt_refcount++;
			break;
		}
	}

	return found;
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
	if (tls->mpt_refcount == 0)
		migrate_pool_tls_destroy(tls);
}

struct migrate_pool_tls_create_arg {
	uuid_t	pool_uuid;
	uuid_t	pool_hdl_uuid;
	uuid_t  co_hdl_uuid;
	d_rank_list_t *svc_list;
	uint64_t max_eph;
	int	version;
};

int migrate_pool_tls_create_one(void *data)
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
		return -DER_NOMEM;

	uuid_copy(pool_tls->mpt_pool_uuid, arg->pool_uuid);
	uuid_copy(pool_tls->mpt_poh_uuid, arg->pool_hdl_uuid);
	uuid_copy(pool_tls->mpt_coh_uuid, arg->co_hdl_uuid);
	pool_tls->mpt_version = arg->version;
	pool_tls->mpt_pool_hdl = DAOS_HDL_INVAL;
	pool_tls->mpt_rec_count = 0;
	pool_tls->mpt_obj_count = 0;
	pool_tls->mpt_size = 0;
	pool_tls->mpt_generated_ult = 0;
	pool_tls->mpt_executed_ult = 0;
	pool_tls->mpt_root_hdl = DAOS_HDL_INVAL;
	pool_tls->mpt_max_eph = arg->max_eph;
	pool_tls->mpt_pool = ds_pool_child_lookup(arg->pool_uuid);
	d_list_add(&pool_tls->mpt_list, &tls->ot_pool_list);

	pool_tls->mpt_refcount = 1;
	rc = daos_rank_list_copy(&pool_tls->mpt_svc_list, arg->svc_list);
	D_DEBUG(DB_TRACE, "TLS %p create for "DF_UUID" ver %d rc %d\n",
		pool_tls, DP_UUID(pool_tls->mpt_pool_uuid), arg->version, rc);
	return rc;
}

static struct migrate_pool_tls*
migrate_pool_tls_lookup_create(struct ds_pool *pool, int version,
			       uuid_t pool_hdl_uuid, uuid_t co_hdl_uuid,
			       uint64_t max_eph)
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
	arg.max_eph = max_eph;
	arg.svc_list = (d_rank_list_t *)entry->dpe_val_ptr;
	rc = dss_task_collective(migrate_pool_tls_create_one, &arg, 0,
				 DSS_ULT_REBUILD);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create migrate tls: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		D_GOTO(out, rc);
	}

	/* dss_task_collecitve does not do collective on xstream 0 */
	rc = migrate_pool_tls_create_one(&arg);
	if (rc)
		D_GOTO(out, rc);

	tls = migrate_pool_tls_lookup(pool->sp_uuid, version);
	D_ASSERT(tls != NULL);
out:
	D_DEBUG(DB_TRACE, "create tls "DF_UUID" rc %d\n",
		DP_UUID(pool->sp_uuid), rc);
	if (prop != NULL)
		daos_prop_free(prop);

	return tls;
}

#define MIGRATE_STACK_SIZE	131072
#define MAX_BUF_SIZE		2048

static int
migrate_fetch_update_inline(struct migrate_one *mrone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont)
{
	d_sg_list_t	sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t	iov[DSS_ENUM_UNPACK_MAX_IODS];
	int		iod_cnt = 0;
	int		start;
	char		iov_buf[DSS_ENUM_UNPACK_MAX_IODS][MAX_BUF_SIZE];
	bool		fetch = false;
	int		i;
	int		rc;

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

	D_DEBUG(DB_TRACE, DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64
		" fetch %s\n", DP_UOID(mrone->mo_oid), mrone,
		DP_KEY(&mrone->mo_dkey), mrone->mo_iod_num,
		mrone->mo_epoch, fetch ? "yes":"no");

	if (fetch) {
		rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
				   mrone->mo_iod_num, mrone->mo_iods, sgls,
				   NULL);
		if (rc) {
			D_ERROR("dsc_obj_fetch %d\n", rc);
			return rc;
		}
	}

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_UPDATE))
		return 0;

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_UPDATE_FAIL))
		return -DER_INVAL;

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
			rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
					    mrone->mo_epoch, mrone->mo_version,
					    &mrone->mo_dkey, iod_cnt,
					    &mrone->mo_iods[start], NULL,
					    &sgls[start]);
			if (rc) {
				D_ERROR("migrate failed: rc %d\n", rc);
				break;
			}
			iod_cnt = 0;
			start = i + 1;
		}
	}

	if (iod_cnt > 0)
		rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_epoch, mrone->mo_version,
				    &mrone->mo_dkey, iod_cnt,
				    &mrone->mo_iods[start], NULL,
				    &sgls[start]);

	return rc;
}

static int
migrate_fetch_update_bulk(struct migrate_one *mrone, daos_handle_t oh,
			  struct ds_cont_child *ds_cont)
{
	d_sg_list_t	 sgls[DSS_ENUM_UNPACK_MAX_IODS], *sgl;
	daos_handle_t	 ioh;
	int		 rc, i, ret, sgl_cnt = 0;

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	rc = vos_update_begin(ds_cont->sc_hdl, mrone->mo_oid, mrone->mo_epoch,
			      &mrone->mo_dkey, mrone->mo_iod_num,
			      mrone->mo_iods, NULL, &ioh, NULL);
	if (rc != 0) {
		D_ERROR(DF_UOID"preparing update fails: %d\n",
			DP_UOID(mrone->mo_oid), rc);
		return rc;
	}

	rc = bio_iod_prep(vos_ioh2desc(ioh));
	if (rc) {
		D_ERROR("Prepare EIOD for "DF_UOID" error: %d\n",
			DP_UOID(mrone->mo_oid), rc);
		goto end;
	}

	for (i = 0; i < mrone->mo_iod_num; i++) {
		struct bio_sglist	*bsgl;

		bsgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(bsgl != NULL);
		sgl = &sgls[i];

		rc = bio_sgl_convert(bsgl, sgl);
		if (rc)
			goto post;
		sgl_cnt++;
	}

	D_DEBUG(DB_TRACE,
		DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64"\n",
		DP_UOID(mrone->mo_oid), mrone, DP_KEY(&mrone->mo_dkey),
		mrone->mo_iod_num, mrone->mo_epoch);

	rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
			   mrone->mo_iod_num, mrone->mo_iods, sgls, NULL);
	if (rc)
		D_ERROR("migrate dkey "DF_KEY" failed rc %d\n",
			DP_KEY(&mrone->mo_dkey), rc);
post:
	for (i = 0; i < sgl_cnt; i++) {
		sgl = &sgls[i];
		daos_sgl_fini(sgl, false);
	}

	ret = bio_iod_post(vos_ioh2desc(ioh));
	if (ret) {
		D_ERROR("Post EIOD for "DF_UOID" error: %d\n",
			DP_UOID(mrone->mo_oid), ret);
		rc = rc ? : ret;
	}

end:
	vos_update_end(ioh, mrone->mo_version, &mrone->mo_dkey, rc, NULL);
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
		D_DEBUG(DB_TRACE, DF_UOID" punch dkey "DF_KEY"/"DF_U64"\n",
			DP_UOID(mrone->mo_oid), DP_KEY(&mrone->mo_dkey),
			mrone->mo_dkey_punch_eph);
		rc = vos_obj_punch(cont->sc_hdl, mrone->mo_oid,
				   mrone->mo_dkey_punch_eph,
				   tls->mpt_version, VOS_OF_REPLAY_PC,
				   &mrone->mo_dkey, 0, NULL, NULL);
		if (rc) {
			D_ERROR(DF_UOID" punch dkey failed: rc %d\n",
				DP_UOID(mrone->mo_oid), rc);
			return rc;
		}
	}

	for (i = 0; i < mrone->mo_iod_num; i++) {
		daos_epoch_t eph;

		eph = mrone->mo_akey_punch_ephs[i];
		D_ASSERT(eph != DAOS_EPOCH_MAX);
		if (eph == 0)
			continue;

		D_DEBUG(DB_TRACE, DF_UOID" mrone %p punch dkey "
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
			D_ERROR(DF_UOID" punch akey failed: rc %d\n",
				DP_UOID(mrone->mo_oid), rc);
			return rc;
		}
	}

	/* punch records */
	if (mrone->mo_punch_iod_num > 0) {
		rc = vos_obj_update(cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_rec_punch_eph,
				    mrone->mo_version, &mrone->mo_dkey,
				    mrone->mo_punch_iod_num,
				    mrone->mo_punch_iods, NULL, NULL);
		D_DEBUG(DB_TRACE, DF_UOID" mrone %p punch %d eph "DF_U64
			" records: %d\n", DP_UOID(mrone->mo_oid), mrone,
			mrone->mo_punch_iod_num, mrone->mo_rec_punch_eph, rc);
	}

	return rc;
}

static int
migrate_dkey(struct migrate_pool_tls *tls, struct migrate_one *mrone)
{
	struct ds_cont_child	*cont;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	daos_handle_t		oh;
	daos_size_t		data_size;
	int			rc;

	if (daos_handle_is_inval(tls->mpt_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;

		rc = dc_pool_local_open(tls->mpt_pool_uuid,
					tls->mpt_poh_uuid, 0, NULL,
					tls->mpt_pool->spc_pool->sp_map,
					&tls->mpt_svc_list, &ph);
		if (rc)
			D_GOTO(free, rc);

		tls->mpt_pool_hdl = ph;
	}

	/* Open client dc handle */
	rc = dc_cont_local_open(mrone->mo_cont_uuid, tls->mpt_coh_uuid,
				0, tls->mpt_pool_hdl, &coh);
	if (rc)
		D_GOTO(free, rc);

	rc = dsc_obj_open(coh, mrone->mo_oid.id_pub, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(cont_close, rc);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_TGT_NOSPACE))
		D_GOTO(obj_close, rc = -DER_NOSPACE);

	rc = ds_cont_child_lookup(tls->mpt_pool_uuid, mrone->mo_cont_uuid,
				  &cont);
	if (rc)
		D_GOTO(obj_close, rc);

	rc = migrate_punch(tls, mrone, cont);
	if (rc)
		D_GOTO(cont_put, rc);

	data_size = daos_iods_len(mrone->mo_iods, mrone->mo_iod_num);

	D_DEBUG(DB_TRACE, "data size is "DF_U64"\n", data_size);
	/* DAOS_REBUILD_TGT_NO_REBUILD are for testing purpose */
	if ((data_size > 0 || data_size == (daos_size_t)(-1)) &&
	    !DAOS_FAIL_CHECK(DAOS_REBUILD_NO_REBUILD)) {
		if (data_size < MAX_BUF_SIZE || data_size == (daos_size_t)(-1))
			rc = migrate_fetch_update_inline(mrone, oh, cont);
		else
			rc = migrate_fetch_update_bulk(mrone, oh, cont);
	}

	tls->mpt_rec_count += mrone->mo_rec_num;
	tls->mpt_size += mrone->mo_size;
cont_put:
	ds_cont_child_put(cont);
obj_close:
	dsc_obj_close(oh);
cont_close:
	dc_cont_local_close(tls->mpt_pool_hdl, coh);
free:
	return rc;
}

void
migrate_one_destroy(struct migrate_one *mrone)
{
	int i;

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
			daos_sgl_fini(&mrone->mo_sgls[i], true);
		D_FREE(mrone->mo_sgls);
	}

	D_FREE(mrone);
}

static void
migrate_one_ult(void *arg)
{
	struct migrate_one	*mrone = arg;
	struct migrate_pool_tls	*tls;
	int			rc;

	while (daos_fail_check(DAOS_REBUILD_TGT_REBUILD_HANG))
		ABT_thread_yield();

	tls = migrate_pool_tls_lookup(mrone->mo_pool_uuid,
				      mrone->mo_pool_tls_version);
	if (tls == NULL) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
			DP_UUID(mrone->mo_pool_uuid));
		return;
	}

	if (tls->mpt_fini)
		goto out;

	rc = migrate_dkey(tls, mrone);
	D_DEBUG(DB_TRACE, DF_UOID" migrate dkey "DF_KEY" rc %d\n",
		DP_UOID(mrone->mo_oid), DP_KEY(&mrone->mo_dkey), rc);

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
	tls->mpt_executed_ult++;
	migrate_pool_tls_put(tls);
}

static int
rw_iod_pack(struct migrate_one *mrone, daos_iod_t *iod, d_sg_list_t *sgls)
{
	int idx = mrone->mo_iod_num;
	int rec_cnt = 0;
	uint64_t total_size = 0;
	int i;
	int rc;

	D_ASSERT(iod->iod_size > 0);

	rc = daos_iod_copy(&mrone->mo_iods[idx], iod);
	if (rc)
		return rc;

	for (i = 0; i < iod->iod_nr; i++) {
		rec_cnt += iod->iod_recxs[i].rx_nr;
		total_size += iod->iod_recxs[i].rx_nr * iod->iod_size;
	}

	D_DEBUG(DB_TRACE,
		"idx %d akey "DF_KEY" nr %d size "DF_U64" type %d\n",
		idx, DP_KEY(&iod->iod_name), iod->iod_nr, iod->iod_size,
		iod->iod_type);

	/* Check if data has been retrieved by iteration */
	if (sgls) {
		if (mrone->mo_sgls == NULL) {
			D_ASSERT(mrone->mo_iod_alloc_num > 0);
			D_ALLOC_ARRAY(mrone->mo_sgls, mrone->mo_iod_alloc_num);
			if (mrone->mo_sgls == NULL)
				return -DER_NOMEM;
		}

		rc = daos_sgl_alloc_copy_data(&mrone->mo_sgls[idx], sgls);
		if (rc)
			D_GOTO(out, rc);
	}

	mrone->mo_iod_num++;
	mrone->mo_rec_num += rec_cnt;
	mrone->mo_size += total_size;
	iod->iod_recxs = NULL;

out:
	return 0;
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

	rc = daos_iod_copy(&mrone->mo_punch_iods[idx], iod);
	if (rc)
		return rc;

	D_DEBUG(DB_TRACE,
		"idx %d akey "DF_KEY" nr %d size "DF_U64" type %d\n",
		idx, DP_KEY(&iod->iod_name), iod->iod_nr, iod->iod_size,
		iod->iod_type);

	if (mrone->mo_rec_punch_eph < eph)
		mrone->mo_rec_punch_eph = eph;

	mrone->mo_punch_iod_num++;
	iod->iod_recxs = NULL;
	return 0;
}

/*
 * Queue dkey to the migrate dkey list on each xstream. Note that this function
 * steals the memory of the recx, csum, and epr arrays from iods.
 */
static int
migrate_one_queue(struct iter_obj_arg *iter_arg, daos_epoch_t epoch,
		  daos_unit_oid_t *oid, daos_key_t *dkey, daos_epoch_t dkey_eph,
		  daos_iod_t *iods, daos_epoch_t *akey_ephs,
		  daos_epoch_t *rec_ephs, int iod_eph_total, d_sg_list_t *sgls,
		  uint32_t version)
{
	struct migrate_pool_tls *tls;
	struct migrate_one	*mrone = NULL;
	bool			inline_copy = true;
	int			i;
	int			rc;

	D_DEBUG(DB_TRACE, "migrate dkey "DF_KEY" iod nr %d\n", DP_KEY(dkey),
		iod_eph_total);

	tls = migrate_pool_tls_lookup(iter_arg->pool_uuid, iter_arg->version);
	D_ASSERT(tls != NULL);
	if (iod_eph_total == 0 || tls->mpt_version <= version) {
		D_DEBUG(DB_TRACE, "No need eph_total %d version %u"
			" migrate ver %u\n", iod_eph_total, version,
			tls->mpt_version);
		return 0;
	}

	D_ALLOC_PTR(mrone);
	if (mrone == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(mrone->mo_iods, iod_eph_total);
	if (mrone->mo_iods == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	mrone->mo_epoch = epoch;
	mrone->mo_dkey_punch_eph = dkey_eph;
	D_ALLOC_ARRAY(mrone->mo_akey_punch_ephs, iod_eph_total);
	if (mrone->mo_akey_punch_ephs == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	mrone->mo_iod_alloc_num = iod_eph_total;
	/* only do the copy below when each with inline recx data */
	for (i = 0; i < iod_eph_total; i++) {
		int j;

		if (sgls[i].sg_nr == 0 || sgls[i].sg_iovs == NULL) {
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

		if (iods[i].iod_size == 0)
			rc = punch_iod_pack(mrone, &iods[i], rec_ephs[i]);
		else
			rc = rw_iod_pack(mrone, &iods[i],
					 inline_copy ? &sgls[i] : NULL);
	}

	mrone->mo_version = version;
	D_DEBUG(DB_TRACE, "create migrate dkey ult %d\n",
		iter_arg->tgt_idx);

	rc = daos_iov_copy(&mrone->mo_dkey, dkey);
	if (rc != 0)
		D_GOTO(free, rc);

	mrone->mo_oid = *oid;
	uuid_copy(mrone->mo_cont_uuid, iter_arg->cont_uuid);
	uuid_copy(mrone->mo_pool_uuid, tls->mpt_pool_uuid);
	mrone->mo_pool_tls_version = tls->mpt_version;
	D_DEBUG(DB_TRACE, DF_UOID" %p dkey "DF_KEY" migrate on idx %d"
		" iod_num %d\n", DP_UOID(mrone->mo_oid), mrone,
		DP_KEY(dkey), iter_arg->tgt_idx,
		mrone->mo_iod_num);

	rc = dss_ult_create(migrate_one_ult, mrone, DSS_ULT_REBUILD,
			    iter_arg->tgt_idx, MIGRATE_STACK_SIZE, NULL);
	if (rc)
		D_GOTO(free, rc);

	tls->mpt_generated_ult++;
free:
	if (rc != 0 && mrone != NULL)
		migrate_one_destroy(mrone);

	return rc;
}

struct enum_unpack_arg {
	struct iter_obj_arg	*arg;
	daos_epoch_range_t	epr;
};

static int
migrate_enum_unpack_cb(struct dss_enum_unpack_io *io, void *data)
{
	struct enum_unpack_arg *arg = data;

	return migrate_one_queue(arg->arg, arg->epr.epr_hi, &io->ui_oid,
				 &io->ui_dkey, io->ui_dkey_punch_eph,
				 io->ui_iods, io->ui_akey_punch_ephs,
				 io->ui_rec_punch_ephs,
				 io->ui_iods_top + 1, io->ui_sgls,
				 io->ui_version);
}

static int
migrate_obj_punch_one(void *data)
{
	struct migrate_pool_tls *tls;
	struct iter_obj_arg	*arg = data;
	struct ds_cont_child	*cont;
	int			rc;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	D_ASSERT(tls != NULL);
	D_DEBUG(DB_TRACE, "tls %p "DF_UUID" version %d punch "DF_UOID"\n", tls,
		DP_UUID(tls->mpt_pool_uuid), arg->version, DP_UOID(arg->oid));
	rc = ds_cont_child_lookup(tls->mpt_pool_uuid, arg->cont_uuid, &cont);
	D_ASSERT(rc == 0);

	rc = vos_obj_punch(cont->sc_hdl, arg->oid, arg->epoch,
			   tls->mpt_version, VOS_OF_REPLAY_PC,
			   NULL, 0, NULL, NULL);
	ds_cont_child_put(cont);
	if (rc)
		D_ERROR(DF_UOID" migrate punch failed rc %d\n",
			DP_UOID(arg->oid), rc);

	return rc;
}

#define KDS_NUM		16
#define ITER_BUF_SIZE   2048

/**
 * Iterate akeys/dkeys of the object
 */
static int
migrate_one_epoch_object(daos_handle_t oh, daos_epoch_range_t *epr,
			 struct migrate_pool_tls *tls, struct iter_obj_arg *arg)
{
	daos_anchor_t	anchor;
	daos_anchor_t	dkey_anchor;
	daos_anchor_t	akey_anchor;
	char		stack_buf[ITER_BUF_SIZE];
	char		*buf = NULL;
	daos_size_t	buf_len;
	daos_key_desc_t		kds[KDS_NUM] = { 0 };
	struct dss_enum_arg	enum_arg = { 0 };
	struct enum_unpack_arg unpack_arg = { 0 };
	d_iov_t		iov = { 0 };
	d_sg_list_t	sgl = { 0 };
	uint32_t	num;
	daos_size_t	size;
	int		rc = 0;

	D_DEBUG(DB_TRACE, "migrate obj "DF_UOID" for shard %u eph "
		DF_U64"-"DF_U64"\n", DP_UOID(arg->oid), arg->shard, epr->epr_lo,
		epr->epr_hi);

	memset(&anchor, 0, sizeof(anchor));
	memset(&dkey_anchor, 0, sizeof(dkey_anchor));
	memset(&akey_anchor, 0, sizeof(akey_anchor));
	dc_obj_shard2anchor(&dkey_anchor, arg->shard);
	daos_anchor_set_flags(&dkey_anchor,
			      DIOF_TO_LEADER | DIOF_WITH_SPEC_EPOCH);

	unpack_arg.arg = arg;
	unpack_arg.epr = *epr;
	buf = stack_buf;
	buf_len = ITER_BUF_SIZE;
	while (!tls->mpt_fini) {
		memset(buf, 0, buf_len);
		iov.iov_len = 0;
		iov.iov_buf = buf;
		iov.iov_buf_len = buf_len;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;

		num = KDS_NUM;
		rc = dsc_obj_list_obj(oh, epr, NULL, NULL, &size,
				     &num, kds, &sgl, &anchor,
				     &dkey_anchor, &akey_anchor);
		if (rc == -DER_KEY2BIG) {
			D_DEBUG(DB_TRACE, "migrate obj "DF_UOID" got "
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
		} else if (rc) {
			/* container might have been destroyed. Or there is
			 * no spare target left for this object see
			 * obj_grp_valid_shard_get()
			 */
			rc = (rc == -DER_NONEXIST) ? 0 : rc;
			break;
		}
		if (num == 0)
			break;

		iov.iov_len = size;

		enum_arg.oid = arg->oid;
		enum_arg.kds = kds;
		enum_arg.kds_cap = KDS_NUM;
		enum_arg.kds_len = num;
		enum_arg.sgl = &sgl;
		enum_arg.sgl_idx = 1;
		enum_arg.chk_key2big = true;
		rc = dss_enum_unpack(VOS_ITER_DKEY, &enum_arg,
				     migrate_enum_unpack_cb, &unpack_arg);
		if (rc) {
			D_ERROR("migrate "DF_UOID" failed: %d\n",
				DP_UOID(arg->oid), rc);
			break;
		}

		if (daos_anchor_is_eof(&dkey_anchor))
			break;
	}

	if (buf != NULL && buf != stack_buf)
		D_FREE(buf);

	D_DEBUG(DB_TRACE, "obj "DF_UOID" for shard %u eph "
		DF_U64"-"DF_U64": rc %d\n", DP_UOID(arg->oid), arg->shard,
		epr->epr_lo, epr->epr_hi, rc);

	return rc;
}

static int
migrate_obj_punch(struct iter_obj_arg *arg)
{
	return dss_task_collective(migrate_obj_punch_one, arg, 0,
				   DSS_ULT_REBUILD);
}

/**
 * Iterate akeys/dkeys of the object
 */
static void
migrate_obj_ult(void *data)
{
	struct iter_obj_arg	*arg = data;
	struct migrate_pool_tls	*tls = NULL;
	daos_handle_t		oh;
	daos_epoch_range_t	epr;
	int			i;
	int			rc;

	tls = migrate_pool_tls_lookup(arg->pool_uuid, arg->version);
	D_ASSERT(tls != NULL);
	if (arg->epoch != DAOS_EPOCH_MAX) {
		rc = migrate_obj_punch(arg);
		if (rc)
			D_GOTO(free, rc);
	}

	rc = dsc_obj_open(arg->cont_hdl, arg->oid.id_pub, DAOS_OO_RW, &oh);
	if (rc)
		D_GOTO(free, rc);

	for (i = 0; i < arg->snap_cnt; i++) {
		epr.epr_lo = i > 0 ? arg->snaps[i-1] + 1 : 0;
		epr.epr_hi = arg->snaps[i];
		rc = migrate_one_epoch_object(oh, &epr, tls, arg);
		if (rc)
			D_GOTO(close, rc);
	}

	epr.epr_lo = arg->snaps ? arg->snaps[arg->snap_cnt - 1] + 1 : 0;
	D_ASSERT(tls->mpt_max_eph != 0);
	epr.epr_hi = tls->mpt_max_eph;
	rc = migrate_one_epoch_object(oh, &epr, tls, arg);

close:
	dsc_obj_close(oh);
free:
	if (arg->epoch == DAOS_EPOCH_MAX)
		tls->mpt_obj_count++;

	tls->mpt_obj_executed_ult++;
	if (tls->mpt_status == 0 && rc < 0)
		tls->mpt_status = rc;
	D_DEBUG(DB_TRACE, "stop migrate obj "DF_UOID" for shard %u rc %d\n",
		DP_UOID(arg->oid), arg->shard, rc);
	if (arg->snaps)
		D_FREE(arg->snaps);
	D_FREE(arg);
	migrate_pool_tls_put(tls);
}

static int
migrate_one_object(daos_unit_oid_t oid, daos_epoch_t eph, unsigned int shard,
		   unsigned int tgt_idx, void *data)
{
	struct iter_cont_arg	*cont_arg = data;
	struct iter_obj_arg	*obj_arg;
	int			rc;

	D_ALLOC_PTR(obj_arg);
	if (obj_arg == NULL)
		return -DER_NOMEM;

	obj_arg->oid = oid;
	obj_arg->epoch = eph;
	obj_arg->shard = shard;
	obj_arg->tgt_idx = tgt_idx;
	obj_arg->cont_hdl = cont_arg->cont_hdl;
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

	/* Let's iterate the object on different xstream */
	rc = dss_ult_create(migrate_obj_ult, obj_arg, DSS_ULT_REBUILD,
			    oid.id_pub.lo % dss_tgt_nr, MIGRATE_STACK_SIZE,
			    NULL);
	if (rc == 0)
		cont_arg->pool_tls->mpt_obj_generated_ult++;
free:
	if (rc)
		D_FREE(obj_arg);

	return rc;
}

struct migrate_obj_val {
	daos_epoch_t	epoch;
	uint32_t	shard;
	uint32_t	tgt_idx;
};

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

	D_DEBUG(DB_TRACE, "obj migrate "DF_UUID"/"DF_UOID" %"PRIx64
		" eph "DF_U64" start\n", DP_UUID(arg->cont_uuid), DP_UOID(*oid),
		ih.cookie, epoch);

	rc = migrate_one_object(*oid, epoch, shard, tgt_idx, arg);
	if (rc != 0) {
		D_ERROR("obj "DF_UOID" migration rc %d\n", DP_UOID(*oid), rc);
		return rc;
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	if (--arg->yield_freq == 0) {
		arg->yield_freq = DEFAULT_YIELD_FREQ;
		ABT_thread_yield();
	}

	/* re-probe the dbtree after deletion */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_REBUILD,
			       NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;
	return rc;
}

static int
migrate_cont_iter_cb(daos_handle_t ih, d_iov_t *key_iov,
		     d_iov_t *val_iov, void *data)
{
	struct ds_pool		*dp;
	struct iter_cont_arg	arg = { 0 };
	struct tree_cache_root	*root = val_iov->iov_buf;
	struct migrate_pool_tls	*tls = data;
	daos_handle_t		coh = DAOS_HDL_INVAL;
	uuid_t			cont_uuid;
	uint64_t		*snapshots = NULL;
	int			snap_cnt;
	int			rc;
	int			rc1;

	uuid_copy(cont_uuid, *(uuid_t *)key_iov->iov_buf);
	D_DEBUG(DB_TRACE, "iter cont "DF_UUID"/%"PRIx64" %"PRIx64" start\n",
		DP_UUID(cont_uuid), ih.cookie, root->root_hdl.cookie);

	dp = ds_pool_lookup(tls->mpt_pool_uuid);
	D_ASSERT(dp != NULL);
	rc = cont_iv_snapshots_fetch(dp->sp_iv_ns, cont_uuid,
				     &snapshots, &snap_cnt);
	if (rc)
		D_GOTO(out_put, rc);

	/* Create dc_pool locally */
	if (daos_handle_is_inval(tls->mpt_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;

		rc = dc_pool_local_open(tls->mpt_pool_uuid,
					tls->mpt_poh_uuid, 0, NULL,
					dp->sp_map, &tls->mpt_svc_list, &ph);
		if (rc)
			D_GOTO(free, rc);

		tls->mpt_pool_hdl = ph;
	}


	rc = dc_cont_local_open(cont_uuid, tls->mpt_coh_uuid,
				0, tls->mpt_pool_hdl, &coh);
	if (rc)
		D_GOTO(free, rc);

	arg.cont_hdl	= coh;
	arg.yield_freq	= DEFAULT_YIELD_FREQ;
	arg.obj_cnt	= root->count;
	arg.cont_root	= root;
	arg.snaps	= snapshots;
	arg.snap_cnt	= snap_cnt;
	arg.pool_tls	= tls;
	uuid_copy(arg.cont_uuid, cont_uuid);
	while (!dbtree_is_empty(root->root_hdl)) {
		rc = dbtree_iterate(root->root_hdl, DAOS_INTENT_REBUILD, false,
				    migrate_obj_iter_cb, &arg);
		if (rc || tls->mpt_fini) {
			if (tls->mpt_status == 0)
				tls->mpt_status = rc;
			break;
		}
	}

	rc1 = dc_cont_local_close(tls->mpt_pool_hdl, coh);
	if (rc1 != 0 || rc != 0)
		D_GOTO(free, rc = rc ? rc : rc1);

	D_DEBUG(DB_TRACE, "iter cont "DF_UUID"/%"PRIx64" finish.\n",
		DP_UUID(cont_uuid), ih.cookie);

	/* Snapshot fetch will yield the ULT, let's reprobe before delete  */
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, DAOS_INTENT_REBUILD,
			       key_iov, NULL);
	if (rc) {
		D_ASSERT(rc != -DER_NONEXIST);
		D_GOTO(free, rc);
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		D_GOTO(free, rc);

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_REBUILD,
			       NULL, NULL);

	if (rc == -DER_NONEXIST)
		D_GOTO(free, rc);
free:
	if (snapshots)
		D_FREE(snapshots);

out_put:
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
			D_ERROR("dbtree iterate fails %d\n", rc);
			if (pool_tls->mpt_status == 0)
				pool_tls->mpt_status = rc;
			break;
		}
	}

	pool_tls->mpt_ult_running = 0;
	migrate_pool_tls_put(pool_tls);
}

static int
migrate_tree_get_hdl(struct migrate_pool_tls *tls, daos_handle_t *hdl)
{
	struct umem_attr uma = { 0 };
	int rc;

	if (!daos_handle_is_inval(tls->mpt_root_hdl)) {
		*hdl = tls->mpt_root_hdl;
		return 0;
	}

	/* migrate tree root init */
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_NV, 0, 4, &uma,
				   &tls->mpt_root,
				   &tls->mpt_root_hdl);
	if (rc != 0) {
		D_ERROR("failed to create tree: %d\n", rc);
		return rc;
	}

	*hdl = tls->mpt_root_hdl;
	return 0;
}

int
migrate_obj_insert(daos_handle_t toh, uuid_t co_uuid, daos_unit_oid_t oid,
		   daos_epoch_t epoch, unsigned int shard, unsigned int tgt_idx)
{
	struct migrate_obj_val	val;
	d_iov_t			val_iov;
	int			rc;

	val.epoch = epoch;
	val.shard = shard;
	val.tgt_idx = tgt_idx;

	D_DEBUG(DB_TRACE, "Insert migrate "DF_UOID" "DF_U64"/%d/%d\n",
		DP_UOID(oid), epoch, shard, tgt_idx);
	d_iov_set(&val_iov, &val, sizeof(struct migrate_obj_val));

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
	daos_handle_t		btr_hdl;
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

	/* Check if the pool tls exists */
	pool_tls = migrate_pool_tls_lookup_create(pool, migrate_in->om_version,
						  po_hdl_uuid, co_hdl_uuid,
						  migrate_in->om_max_eph);
	if (pool_tls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* NB: only create this tree on xstream 0 */
	rc = migrate_tree_get_hdl(pool_tls, &btr_hdl);
	if (rc)
		D_GOTO(out, rc);

	/* Insert these oids/conts into the local tree */
	for (i = 0; i < oids_count; i++) {
		/* firstly insert/check rebuilt tree */
		rc = migrate_obj_insert(btr_hdl, co_uuid, oids[i], ephs[i],
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
		rc = dss_ult_create(migrate_ult, pool_tls, DSS_ULT_REBUILD,
				    DSS_TGT_SELF, 0, NULL);
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
	if (arg->dms.dm_status)
		arg->dms.dm_status = tls->mpt_status;

	ABT_mutex_unlock(arg->status_lock);
	return 0;
}

int
ds_migrate_query_status(uuid_t pool_uuid, uint32_t ver,
			struct ds_migrate_status *dms)
{
	struct migrate_query_arg	arg = { 0 };
	int				rc;

	uuid_copy(arg.pool_uuid, pool_uuid);
	arg.version = ver;
	ABT_mutex_create(&arg.status_lock);

	rc = dss_thread_collective(migrate_check_one, &arg, 0, DSS_ULT_REBUILD);
	if (rc)
		D_GOTO(out, rc);

	*dms = arg.dms;
	if (arg.obj_generated_ult > arg.obj_executed_ult ||
	    arg.generated_ult > arg.executed_ult)
		dms->dm_migrating = 1;
	else
		dms->dm_migrating = 0;

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" migrating=%s,"
		" obj_count="DF_U64", rec_count="DF_U64
		"size = "DF_U64" obj %u/%u general %u/%u status %d\n",
		DP_UUID(pool_uuid), dms->dm_migrating ? "yes" : "no",
		dms->dm_obj_count, dms->dm_rec_count, dms->dm_total_size,
		arg.obj_generated_ult, arg.obj_executed_ult,
		arg.generated_ult, arg.executed_ult, dms->dm_status);
out:
	ABT_mutex_free(&arg.status_lock);
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
	migrate_pool_tls_put(tls); /* lookup */
	migrate_pool_tls_put(tls); /* destroy */
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
 *
 * return			0 if it succeeds, otherwise errno.
 */
int
ds_object_migrate(struct ds_pool *pool, uuid_t pool_hdl_uuid,
		  uuid_t cont_hdl_uuid, uuid_t cont_uuid, int tgt_id,
		  uint32_t version, uint64_t max_eph, daos_unit_oid_t *oids,
		  daos_epoch_t *ephs, unsigned int *shards, int cnt)
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
	if (rc != 1 || target->ta_comp.co_status != PO_COMP_ST_UPIN) {
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
	if (rc)
		D_GOTO(out, rc);

	migrate_in = crt_req_get(rpc);
	uuid_copy(migrate_in->om_pool_uuid, pool->sp_uuid);
	uuid_copy(migrate_in->om_poh_uuid, pool_hdl_uuid);
	uuid_copy(migrate_in->om_cont_uuid, cont_uuid);
	uuid_copy(migrate_in->om_coh_uuid, cont_hdl_uuid);
	migrate_in->om_version = version;
	migrate_in->om_max_eph = max_eph,
	migrate_in->om_tgt_idx = index;

	migrate_in->om_oids.ca_arrays = oids;
	migrate_in->om_oids.ca_count = cnt;
	migrate_in->om_ephs.ca_arrays = ephs;
	migrate_in->om_ephs.ca_count = cnt;
	if (shards) {
		migrate_in->om_shards.ca_arrays = shards;
		migrate_in->om_shards.ca_count = cnt;
	}
	rc = dss_rpc_send(rpc);

	migrate_out = crt_reply_get(rpc);
	if (rc == 0)
		rc = migrate_out->om_status;
out:
	D_DEBUG(DB_TRACE, DF_UUID" migrate object: %d\n",
		DP_UUID(pool->sp_uuid), rc);
	if (rpc)
		crt_req_decref(rpc);

	return rc;
}
