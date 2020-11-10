/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

/* This needs to be here to avoid pulling in all of srv_internal.h */
int ds_cont_tgt_destroy(uuid_t pool_uuid, uuid_t cont_uuid);
int ds_cont_tgt_force_close(uuid_t cont_uuid);

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

struct migrate_one {
	daos_key_t		 mo_dkey;
	uuid_t			 mo_pool_uuid;
	uuid_t			 mo_cont_uuid;
	daos_unit_oid_t		 mo_oid;
	daos_epoch_t		 mo_dkey_punch_eph;
	daos_epoch_t		 mo_epoch;
	daos_epoch_t		 mo_update_epoch;
	daos_iod_t		*mo_iods;
	struct dcs_iod_csums	*mo_iods_csums;
	daos_iod_t		*mo_punch_iods;
	daos_epoch_t		*mo_akey_punch_ephs;
	daos_epoch_t		 mo_rec_punch_eph;
	d_sg_list_t		*mo_sgls;
	unsigned int		 mo_iod_num;
	unsigned int		 mo_punch_iod_num;
	unsigned int		 mo_iod_alloc_num;
	unsigned int		 mo_rec_num;
	uint64_t		 mo_size;
	uint64_t		 mo_version;
	uint32_t		 mo_pool_tls_version;
	d_list_t		 mo_list;
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
	D_DEBUG(DB_REBUILD, "TLS destroy for "DF_UUID" ver %d\n",
		DP_UUID(tls->mpt_pool_uuid), tls->mpt_version);
	if (tls->mpt_pool)
		ds_pool_child_put(tls->mpt_pool);

	if (tls->mpt_svc_list.rl_ranks)
		D_FREE(tls->mpt_svc_list.rl_ranks);

	if (tls->mpt_clear_conts)
		d_hash_table_destroy_inplace(&tls->mpt_cont_dest_tab,
					     true /* force */);
	if (tls->mpt_done_eventual)
		ABT_eventual_free(&tls->mpt_done_eventual);
	if (!daos_handle_is_inval(tls->mpt_root_hdl))
		obj_tree_destroy(tls->mpt_root_hdl);
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

/** Hash table entry containing a container uuid that has been initialized */
struct migrate_init_cont_key {
	/** Container uuid that has already been initialized */
	uuid_t			cont_uuid;
	/** link chain on hash */
	d_list_t		cont_link;
};

static bool
migrate_init_cont_key_cmp(struct d_hash_table *htab, d_list_t *link,
			  const void *key, unsigned int ksize)
{
	struct migrate_init_cont_key *rec =
		container_of(link, struct migrate_init_cont_key, cont_link);

	D_ASSERT(ksize == sizeof(uuid_t));
	return !uuid_compare(rec->cont_uuid, key);
}

static void
migrate_init_cont_key_free(struct d_hash_table *htab, d_list_t *link)
{
	struct migrate_init_cont_key *rec =
		container_of(link, struct migrate_init_cont_key, cont_link);
	D_FREE(rec);
}

static d_hash_table_ops_t migrate_init_cont_tab_ops = {
	.hop_key_cmp	= migrate_init_cont_key_cmp,
	.hop_rec_free	= migrate_init_cont_key_free
};

struct migrate_pool_tls_create_arg {
	uuid_t	pool_uuid;
	uuid_t	pool_hdl_uuid;
	uuid_t  co_hdl_uuid;
	d_rank_list_t *svc_list;
	uint64_t max_eph;
	int	version;
	int	clear_conts;
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
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ABT_eventual_create(0, &pool_tls->mpt_done_eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

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
	pool_tls->mpt_clear_conts = arg->clear_conts;

	if (pool_tls->mpt_clear_conts) {
		rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 8, NULL,
					    &migrate_init_cont_tab_ops,
					    &pool_tls->mpt_cont_dest_tab);
		if (rc)
			D_GOTO(out, rc);
	}

	pool_tls->mpt_refcount = 1;
	rc = daos_rank_list_copy(&pool_tls->mpt_svc_list, arg->svc_list);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_REBUILD, "TLS %p create for "DF_UUID" ver %d rc %d\n",
		pool_tls, DP_UUID(pool_tls->mpt_pool_uuid), arg->version, rc);
	d_list_add(&pool_tls->mpt_list, &tls->ot_pool_list);
out:
	if (rc && pool_tls)
		migrate_pool_tls_destroy(pool_tls);

	return rc;
}

static struct migrate_pool_tls*
migrate_pool_tls_lookup_create(struct ds_pool *pool, int version,
			       uuid_t pool_hdl_uuid, uuid_t co_hdl_uuid,
			       uint64_t max_eph, int clear_conts)
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
	arg.clear_conts = clear_conts;
	arg.max_eph = max_eph;
	arg.svc_list = (d_rank_list_t *)entry->dpe_val_ptr;
	rc = dss_task_collective(migrate_pool_tls_create_one, &arg, 0,
				 DSS_ULT_REBUILD);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create migrate tls: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		D_GOTO(out, rc);
	}

	/* dss_task_collective does not do collective on xstream 0 */
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

static void
mrone_recx_daos_vos_internal(struct migrate_one *mrone,
			     struct daos_oclass_attr *oca,
			     bool daos2vos, int shard)
{
	daos_iod_t *iod;
	int cell_nr;
	int stripe_nr;
	int j;
	int k;

	D_ASSERT(DAOS_OC_IS_EC(oca));

	cell_nr = obj_ec_cell_rec_nr(oca);
	stripe_nr = obj_ec_stripe_rec_nr(oca);
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
mrone_recx_daos2_vos(struct migrate_one *mrone, struct daos_oclass_attr *oca)
{
	mrone_recx_daos_vos_internal(mrone, oca, true, -1);
}

static void
mrone_recx_vos2_daos(struct migrate_one *mrone, struct daos_oclass_attr *oca,
		     int shard)
{
	shard = shard % obj_ec_tgt_nr(oca);
	D_ASSERT(shard < obj_ec_data_tgt_nr(oca));
	mrone_recx_daos_vos_internal(mrone, oca, false, shard);
}

#define MIGRATE_STACK_SIZE	131072
#define MAX_BUF_SIZE		2048

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
	struct daos_oclass_attr	*oca;

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

	if (fetch) {
		rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
				   mrone->mo_iod_num, mrone->mo_iods, sgls,
				   NULL, DIOF_TO_LEADER, NULL);
		if (rc) {
			D_ERROR("dsc_obj_fetch %d\n", rc);
			return rc;
		}
	}

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_UPDATE))
		return 0;

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_UPDATE_FAIL))
		return -DER_INVAL;

	if (daos_oclass_is_ec(mrone->mo_oid.id_pub, &oca) &&
	    !obj_shard_is_ec_parity(mrone->mo_oid, &oca))
		mrone_recx_daos2_vos(mrone, oca);

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

			iod_csums = mrone->mo_iods_csums == NULL ? NULL
					: &mrone->mo_iods_csums[start];
			D_DEBUG(DB_TRACE, "update start %d cnt %d\n",
				start, iod_cnt);
			rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
					    mrone->mo_update_epoch,
					    mrone->mo_version,
					    0, &mrone->mo_dkey, iod_cnt,
					    &mrone->mo_iods[start], iod_csums,
					    &sgls[start]);
			if (rc) {
				D_ERROR("migrate failed: rc %d\n", rc);
				break;
			}
			iod_cnt = 0;
			start = i + 1;
		}
	}

	if (iod_cnt > 0) {
		iod_csums = mrone->mo_iods_csums == NULL ? NULL
				: &mrone->mo_iods_csums[start];
		rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_update_epoch,
				    mrone->mo_version,
				    0, &mrone->mo_dkey, iod_cnt,
				    &mrone->mo_iods[start], iod_csums,
				    &sgls[start]);
	}

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
		      daos_size_t size, struct daos_oclass_attr *oca,
		      daos_iod_t *iod, unsigned char *p_bufs[])
{
	daos_size_t	stride_nr = obj_ec_stripe_rec_nr(oca);
	daos_size_t	cell_nr = obj_ec_cell_rec_nr(oca);
	daos_recx_t	tmp_recx;
	d_iov_t		tmp_iov;
	d_sg_list_t	tmp_sgl;
	daos_size_t	write_nr;
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
		rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
				    mrone->mo_epoch,
				    mrone->mo_version,
				    0, &mrone->mo_dkey, 1, iod, NULL,
				    &tmp_sgl);
		size -= write_nr;
		offset += write_nr;
		buffer += write_nr * iod->iod_size;
	}
out:
	return rc;
}

static int
migrate_fetch_update_parity(struct migrate_one *mrone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont,
			    struct daos_oclass_attr *oca)
{
	d_sg_list_t	 sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t		 iov[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	char		 *data;
	daos_size_t	 size;
	unsigned int	 p = obj_ec_parity_tgt_nr(oca);
	unsigned char	 *p_bufs[p];
	unsigned char	 *ptr;
	int		 i;
	int		 rc;

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	for (i = 0; i < mrone->mo_iod_num; i++) {
		size = daos_iods_len(&mrone->mo_iods[i], 1);
		D_ALLOC(data, size);
		if (data == NULL)
			D_GOTO(out, rc =-DER_NOMEM);

		memset(p_bufs, 0, p * sizeof(p_bufs));
		d_iov_set(&iov[i], data, size);
		sgls[i].sg_nr = 1;
		sgls[i].sg_nr_out = 1;
		sgls[i].sg_iovs = &iov[i];
	}

	D_DEBUG(DB_REBUILD,
		DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64"\n",
		DP_UOID(mrone->mo_oid), mrone, DP_KEY(&mrone->mo_dkey),
		mrone->mo_iod_num, mrone->mo_epoch);

	rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
			   mrone->mo_iod_num, mrone->mo_iods, sgls, NULL,
			   DIOF_TO_LEADER, NULL);
	if (rc) {
		D_ERROR("migrate dkey "DF_KEY" failed rc %d\n",
			DP_KEY(&mrone->mo_dkey), rc);
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
						   size, oca, &tmp_iod, p_bufs);
			if (rc)
				D_GOTO(out, rc);
			ptr += size * iod->iod_size;
			offset = recx->rx_idx;
			size = recx->rx_nr;
		}

		if (size > 0)
			rc = migrate_update_parity(mrone, ds_cont, ptr, offset,
						   size, oca, &tmp_iod, p_bufs);
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

	return rc;
}

static int
migrate_fetch_update_single(struct migrate_one *mrone, daos_handle_t oh,
			    struct ds_cont_child *ds_cont)
{
	struct daos_oclass_attr	*oca;
	d_sg_list_t	 	sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t		 	iov[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	char		 	*data;
	daos_size_t	 	size;
	int		 	i;
	int		 	rc;

	oca = daos_oclass_attr_find(mrone->mo_oid.id_pub);
	D_ASSERT(oca != NULL);
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

	rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
			   mrone->mo_iod_num, mrone->mo_iods, sgls, NULL,
			   DIOF_TO_LEADER, NULL);
	if (rc) {
		D_ERROR("migrate dkey "DF_KEY" failed rc %d\n",
			DP_KEY(&mrone->mo_dkey), rc);
		D_GOTO(out, rc);
	}

	for (i = 0; i < mrone->mo_iod_num && DAOS_OC_IS_EC(oca); i++) {
		daos_iod_t	*iod = &mrone->mo_iods[i];
		uint32_t	start_shard;

		start_shard = rounddown(mrone->mo_oid.id_shard,
					obj_ec_tgt_nr(oca));
		if (obj_ec_singv_one_tgt(iod, &sgls[i], oca)) {
			D_DEBUG(DB_REBUILD, DF_UOID" one tgt.\n",
				DP_UOID(mrone->mo_oid));
			continue;
		}

		if (obj_shard_is_ec_parity(mrone->mo_oid, NULL)) {
			rc = obj_ec_singv_encode_buf(mrone->mo_oid.id_pub,
						     mrone->mo_oid.id_shard,
						     iod, oca, &sgls[i],
						     &sgls[i].sg_iovs[0]);
			if (rc)
				D_GOTO(out, rc);
		} else {
			rc = obj_ec_singv_split(mrone->mo_oid.id_pub,
						mrone->mo_oid.id_shard,
						iod->iod_size, oca, &sgls[i]);
			if (rc)
				D_GOTO(out, rc);
		}

		obj_singv_ec_rw_filter(&mrone->mo_oid, iod, NULL,
				       mrone->mo_epoch, ORF_EC,
				       start_shard, 1,
				       true, false, NULL);
	}

	rc = vos_obj_update(ds_cont->sc_hdl, mrone->mo_oid,
			    mrone->mo_update_epoch, mrone->mo_version,
			    0, &mrone->mo_dkey, mrone->mo_iod_num,
			    mrone->mo_iods, mrone->mo_iods_csums, sgls);
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

	return rc;
}

static int
migrate_fetch_update_bulk(struct migrate_one *mrone, daos_handle_t oh,
			  struct ds_cont_child *ds_cont)
{
	d_sg_list_t	 sgls[DSS_ENUM_UNPACK_MAX_IODS], *sgl;
	daos_handle_t	 ioh;
	struct daos_oclass_attr *oca;
	int		 rc, i, ret, sgl_cnt = 0;

	if (obj_shard_is_ec_parity(mrone->mo_oid, &oca))
		return migrate_fetch_update_parity(mrone, oh, ds_cont, oca);

	if (DAOS_OC_IS_EC(oca))
		mrone_recx_daos2_vos(mrone, oca);

	D_ASSERT(mrone->mo_iod_num <= DSS_ENUM_UNPACK_MAX_IODS);
	rc = vos_update_begin(ds_cont->sc_hdl, mrone->mo_oid,
			      mrone->mo_update_epoch, 0, &mrone->mo_dkey,
			      mrone->mo_iod_num, mrone->mo_iods,
			      mrone->mo_iods_csums, false, 0, &ioh, NULL);
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

		rc = bio_sgl_convert(bsgl, sgl, false);
		if (rc)
			goto post;
		sgl_cnt++;
	}

	D_DEBUG(DB_REBUILD,
		DF_UOID" mrone %p dkey "DF_KEY" nr %d eph "DF_U64"\n",
		DP_UOID(mrone->mo_oid), mrone, DP_KEY(&mrone->mo_dkey),
		mrone->mo_iod_num, mrone->mo_epoch);

	if (DAOS_OC_IS_EC(oca))
		mrone_recx_vos2_daos(mrone, oca, mrone->mo_oid.id_shard);

	rc = dsc_obj_fetch(oh, mrone->mo_epoch, &mrone->mo_dkey,
			   mrone->mo_iod_num, mrone->mo_iods, sgls, NULL,
			   DIOF_TO_LEADER, NULL);
	if (rc)
		D_ERROR("migrate dkey "DF_KEY" failed rc %d\n",
			DP_KEY(&mrone->mo_dkey), rc);
post:
	for (i = 0; i < sgl_cnt; i++) {
		sgl = &sgls[i];
		daos_sgl_fini(sgl, false);
	}

	if (DAOS_OC_IS_EC(oca))
		mrone_recx_daos2_vos(mrone, oca);

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
		D_DEBUG(DB_REBUILD, DF_UOID" punch dkey "DF_KEY"/"DF_U64"\n",
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
			D_ERROR(DF_UOID" punch akey failed: rc %d\n",
				DP_UOID(mrone->mo_oid), rc);
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

		rc = dsc_pool_open(tls->mpt_pool_uuid, tls->mpt_poh_uuid, 0,
				   NULL, tls->mpt_pool->spc_pool->sp_map,
				   &tls->mpt_svc_list, &ph);
		if (rc)
			D_GOTO(free, rc);

		tls->mpt_pool_hdl = ph;
	}

	rc = ds_cont_child_open_create(tls->mpt_pool_uuid, mrone->mo_cont_uuid,
				       &cont);
	if (rc)
		D_GOTO(free, rc);

	/* Open client dc handle used to read the remote object data */
	rc = dsc_cont_open(tls->mpt_pool_hdl, mrone->mo_cont_uuid,
			   tls->mpt_coh_uuid, 0, &coh);
	if (rc)
		D_GOTO(cont_put, rc);

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

	rc = migrate_punch(tls, mrone, cont);
	if (rc)
		D_GOTO(obj_close, rc);

	data_size = daos_iods_len(mrone->mo_iods, mrone->mo_iod_num);
	D_DEBUG(DB_TRACE, "data size is "DF_U64"\n", data_size);
	if (data_size == 0) {
		D_DEBUG(DB_REBUILD, "skipe empty iod\n");
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
	dsc_cont_close(tls->mpt_pool_hdl, coh);
cont_put:
	ds_cont_child_put(cont);
free:
	return rc;
}

void
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
			daos_sgl_fini(&mrone->mo_sgls[i], true);
		D_FREE(mrone->mo_sgls);
	}

	if (mrone->mo_iods_csums) {
		struct dcs_iod_csums	*iod_csum;
		int			 j;

		for (i = 0; i < mrone->mo_iod_alloc_num; i++) {
			iod_csum = &mrone->mo_iods_csums[i];
			for (j = 0; j < iod_csum->ic_nr; j++)
				D_FREE(iod_csum->ic_data[j].cs_csum);
			D_FREE(iod_csum->ic_data);
		}
		D_FREE(mrone->mo_iods_csums);
	}

	D_FREE(mrone);
}

static void
migrate_one_ult(void *arg)
{
	struct migrate_one	*mrone = arg;
	struct migrate_pool_tls	*tls;
	int			rc;

	if (daos_fail_check(DAOS_REBUILD_TGT_REBUILD_HANG))
		dss_sleep(daos_fail_value_get() * 1000000);

	tls = migrate_pool_tls_lookup(mrone->mo_pool_uuid,
				      mrone->mo_pool_tls_version);
	if (tls == NULL || tls->mpt_fini) {
		D_WARN("some one abort the rebuild "DF_UUID"\n",
			DP_UUID(mrone->mo_pool_uuid));
		goto out;
	}

	rc = migrate_dkey(tls, mrone);
	D_DEBUG(DB_REBUILD, DF_UOID" migrate dkey "DF_KEY" rc %d\n",
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
	if (tls != NULL) {
		tls->mpt_executed_ult++;
		migrate_pool_tls_put(tls);
	}
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
		D_DEBUG(DB_REBUILD, "recx "DF_U64"/"DF_U64"\n",
			iod->iod_recxs[i].rx_idx, iod->iod_recxs[i].rx_nr);
		rec_cnt += iod->iod_recxs[i].rx_nr;
		total_size += iod->iod_recxs[i].rx_nr * iod->iod_size;
	}

	D_DEBUG(DB_REBUILD,
		"idx %d akey "DF_KEY" nr %d size "DF_U64" type %d rec %d total "
		DF_U64"\n", idx, DP_KEY(&iod->iod_name), iod->iod_nr,
		iod->iod_size, iod->iod_type, rec_cnt, total_size);

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

	if (iod->iod_type == DAOS_IOD_SINGLE)
		mrone->mo_iods[idx].iod_recxs = NULL;
	else
		iod->iod_recxs = NULL;

	mrone->mo_iod_num++;
	mrone->mo_rec_num += rec_cnt;
	mrone->mo_size += total_size;

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

static int
migrate_one_iod_merge_recx(daos_unit_oid_t oid, daos_iod_t *dst_iod,
			   daos_iod_t *src_iod)
{
	struct obj_auxi_list_recx	*recx;
	struct obj_auxi_list_recx	*tmp;
	struct daos_oclass_attr		*oca;
	daos_recx_t	*recxs;
	d_list_t	merge_list;
	int		nr_recxs = 0;
	int		i;
	int		rc = 0;

	oca = daos_oclass_attr_find(oid.id_pub);
	if (oca == NULL)
		return -DER_NONEXIST;

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
				rc = migrate_one_iod_merge_recx(io->ui_oid,
							&mo->mo_iods[j],
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
	daos_epoch_range_t	epr;
	d_list_t		merge_list;
	uint32_t		iterate_parity:1;
};

static int
migrate_one_insert(struct enum_unpack_arg *arg,
		   struct dss_enum_unpack_io *io, daos_epoch_t epoch)
{
	struct iter_obj_arg	*iter_arg = arg->arg;
	daos_unit_oid_t		oid = io->ui_oid;
	daos_key_t		*dkey = &io->ui_dkey;
	daos_epoch_t		dkey_punch_eph = io->ui_dkey_punch_eph;
	daos_iod_t		*iods = io->ui_iods;
	struct dcs_iod_csums	*iods_csums = io->ui_iods_csums;
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
	D_ASSERT(tls != NULL);
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

	D_ALLOC_ARRAY(mrone->mo_iods_csums, iod_eph_total);
	if (mrone->mo_iods_csums == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	mrone->mo_epoch = arg->epr.epr_hi;
	mrone->mo_update_epoch = epoch;
	mrone->mo_dkey_punch_eph = dkey_punch_eph;
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

		if (iods[i].iod_size == 0) {
			rc = punch_iod_pack(mrone, &iods[i], rec_ephs[i]);
		} else {
			rc = rw_iod_pack(mrone, &iods[i],
					 inline_copy ? &sgls[i] : NULL);
			mrone->mo_iods_csums[mrone->mo_iod_num - 1] =
				iods_csums[i];
		}

		if (rc != 0)
			goto free;

		/**
		 * mrone owns the allocated memory now and will free it in
		 * migrate_one_destroy
		 */
		iods_csums[i].ic_data = NULL;
		iods_csums[i].ic_nr = 0;
	}

	mrone->mo_version = version;
	D_DEBUG(DB_TRACE, "create migrate dkey ult %d\n",
		iter_arg->tgt_idx);

	rc = daos_iov_copy(&mrone->mo_dkey, dkey);
	if (rc != 0)
		D_GOTO(free, rc);

	mrone->mo_oid = oid;
	mrone->mo_oid.id_shard = iter_arg->shard;
	uuid_copy(mrone->mo_cont_uuid, iter_arg->cont_uuid);
	uuid_copy(mrone->mo_pool_uuid, tls->mpt_pool_uuid);
	mrone->mo_pool_tls_version = tls->mpt_version;
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
	migrate_pool_tls_put(tls);
	return rc;
}

static int
migrate_enum_unpack_cb(struct dss_enum_unpack_io *io, void *data)
{
	struct enum_unpack_arg	*arg = data;
	struct migrate_one	*mo;
	struct daos_oclass_attr	*oca;
	bool			merged = false;
	daos_epoch_t		epoch = arg->epr.epr_hi;
	int			rc = 0;
	int			i;

	if (daos_oclass_is_ec(io->ui_oid.id_pub, &oca)) {
		/* Convert EC object offset to DAOS offset. */
		for (i = 0; i <= io->ui_iods_top; i++) {
			daos_iod_t *iod = &io->ui_iods[i];

			if (iod->iod_type == DAOS_IOD_SINGLE)
				continue;

			rc = obj_recx_ec2_daos(oca, io->ui_oid.id_shard,
					       &iod->iod_recxs, &iod->iod_nr);
			if (rc != 0)
				return rc;

			/* After convert to DAOS offset, there might be
			 * some duplicate recxs due to replication/parity
			 * space. let's remove them.
			 */
			rc = migrate_one_iod_merge_recx(io->ui_oid,
							iod, NULL);
			if (rc)
				return rc;

			/* For data shard, convert to single shard offset */
			if (arg->arg->shard < obj_ec_data_tgt_nr(oca)) {
				/* data shard */
				rc = obj_recx_ec_daos2shard(oca,
							    arg->arg->shard,
							    &iod->iod_recxs,
							    &iod->iod_nr);
				if (rc)
					return rc;
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
	D_ASSERT(tls != NULL);
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
		D_ERROR(DF_UOID" migrate punch failed rc %d\n",
			DP_UOID(arg->oid), rc);
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
	D_ASSERT(tls != NULL);
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
		rc = dss_ult_create(migrate_one_ult, mrone, DSS_ULT_REBUILD,
				    arg->tgt_idx, MIGRATE_STACK_SIZE, NULL);
		if (rc) {
			migrate_one_destroy(mrone);
			break;
		}
		tls->mpt_generated_ult++;
	}

	migrate_pool_tls_put(tls);
	return rc;
}

#define KDS_NUM		16
#define ITER_BUF_SIZE	2048
#define CSUM_BUF_SIZE	256

/**
 * Iterate akeys/dkeys of the object
 */
static int
migrate_one_epoch_object(daos_handle_t oh, daos_epoch_range_t *epr,
			 struct migrate_pool_tls *tls, struct iter_obj_arg *arg)
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
	struct enum_unpack_arg	 unpack_arg = { 0 };
	d_iov_t			 iov = { 0 };
	d_sg_list_t		 sgl = { 0 };
	uint32_t		 num;
	daos_size_t		 size;
	int			 rc = 0;

	D_DEBUG(DB_REBUILD, "migrate obj "DF_UOID" for shard %u eph "
		DF_U64"-"DF_U64"\n", DP_UOID(arg->oid), arg->shard, epr->epr_lo,
		epr->epr_hi);

	memset(&anchor, 0, sizeof(anchor));
	memset(&dkey_anchor, 0, sizeof(dkey_anchor));
	dc_obj_shard2anchor(&dkey_anchor, arg->shard);
	memset(&akey_anchor, 0, sizeof(akey_anchor));
	unpack_arg.arg = arg;
	unpack_arg.epr = *epr;
	D_INIT_LIST_HEAD(&unpack_arg.merge_list);
	buf = stack_buf;
	buf_len = ITER_BUF_SIZE;

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
				      DIOF_TO_SPEC_SHARD);
retry:
		rc = dsc_obj_list_obj(oh, epr, NULL, NULL, &size,
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
			daos_anchor_set_flags(&dkey_anchor,
					      DIOF_WITH_SPEC_EPOCH |
					      DIOF_TO_SPEC_SHARD);
			D_DEBUG(DB_REBUILD, "No leader available %d retry"
				DF_UOID"\n", rc, DP_UOID(arg->oid));
			D_GOTO(retry, rc);
		} else if (rc) {
			/* container might have been destroyed. Or there is
			 * no spare target left for this object see
			 * obj_grp_valid_shard_get()
			 */
			/* DER_DATA_LOSS means it can not find any replicas
			 * to rebuild the data, see obj_list_common.
			 */
			D_DEBUG(DB_REBUILD, "Can not rebuild "
				DF_UOID"\n", DP_UOID(arg->oid));
			break;
		}

		if (num == 0)
			break;

		sgl.sg_iovs[0].iov_len = size;
		rc = dss_enum_unpack(arg->oid, kds, num, &sgl, &csum,
				     migrate_enum_unpack_cb, &unpack_arg);
		if (rc) {
			D_ERROR("migrate "DF_UOID" failed: %d\n",
				DP_UOID(arg->oid), rc);
			break;
		}

		rc = migrate_start_ult(&unpack_arg);
		if (rc) {
			D_ERROR("start migrate "DF_UOID" failed: %d\n",
				DP_UOID(arg->oid), rc);
			break;
		}

		if (daos_anchor_is_eof(&dkey_anchor))
			break;
	}

	if (buf != NULL && buf != stack_buf)
		D_FREE(buf);

	if (csum.iov_buf != NULL && csum.iov_buf != stack_csum_buf)
		D_FREE(csum.iov_buf);

	D_DEBUG(DB_REBUILD, "obj "DF_UOID" for shard %u eph "
		DF_U64"-"DF_U64": rc %d\n", DP_UOID(arg->oid), arg->shard,
		epr->epr_lo, epr->epr_hi, rc);

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
	rc = dss_thread_collective(migrate_fini_one_ult, &arg, 0,
				   DSS_ULT_REBUILD);
	if (rc)
		D_ERROR("migrate abort: %d\n", rc);

	migrate_pool_tls_put(tls);
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
	D_DEBUG(DB_REBUILD, "stop migrate obj "DF_UOID" for shard %u rc %d\n",
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
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_REBUILD,
			       NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;
	else if (rc != 0)
		D_ERROR("dbtree_iter_probe failed: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/* Destroys a container exactly one time per migration session. Uses the
 * mpt_cont_dest_tab field of the tls to store which containers have already
 * been deleted this session.
 *
 * Only used for reintegration
 */
static int
destroy_existing_container(struct migrate_pool_tls *tls, uuid_t cont_uuid)
{
	d_list_t *link;
	int rc;

	link = d_hash_rec_find(&tls->mpt_cont_dest_tab, cont_uuid,
			       sizeof(uuid_t));
	if (!link) {
		/* Not actually storing anything in the table - just using it
		 * to test set membership. The link stored is just the simplest
		 * base list type
		 */
		struct migrate_init_cont_key *key;

		D_DEBUG(DB_REBUILD,
			"destroying pool/cont/hdl "DF_UUID"/"DF_UUID"/"DF_UUID
			" before reintegration\n", DP_UUID(tls->mpt_pool_uuid),
			DP_UUID(cont_uuid), DP_UUID(tls->mpt_coh_uuid));

		rc = ds_cont_tgt_force_close(cont_uuid);
		if (rc != 0) {
			D_ERROR("Migrate failed to close container "
				"prior to reintegration: pool: "DF_UUID
				", cont: "DF_UUID" rc: "DF_RC"\n",
				DP_UUID(tls->mpt_pool_uuid), DP_UUID(cont_uuid),
				DP_RC(rc));
		}

		rc = ds_cont_tgt_destroy(tls->mpt_pool_uuid, cont_uuid);
		if (rc != 0) {
			D_ERROR("Migrate failed to destroy container "
				"prior to reintegration: pool: "DF_UUID
				", cont: "DF_UUID" rc: "DF_RC"\n",
				DP_UUID(tls->mpt_pool_uuid), DP_UUID(cont_uuid),
				DP_RC(rc));
		}

		/* Insert a link into the hash table to mark this cont_uuid as
		 * having already been initialized
		 */
		D_ALLOC_PTR(key);
		if (key == NULL)
			return -DER_NOMEM;

		uuid_copy(key->cont_uuid, cont_uuid);
		D_INIT_LIST_HEAD(&key->cont_link);
		rc = d_hash_rec_insert(&tls->mpt_cont_dest_tab, cont_uuid,
				       sizeof(uuid_t), &key->cont_link, true);
		if (rc) {
			D_ERROR("Failed to insert uuid table entry "DF_RC"\n",
				DP_RC(rc));
			D_FREE(key);
			return rc;
		}
	}

	return 0;
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

	/* Create dc_pool locally */
	if (daos_handle_is_inval(tls->mpt_pool_hdl)) {
		daos_handle_t ph = DAOS_HDL_INVAL;

		rc = dsc_pool_open(tls->mpt_pool_uuid, tls->mpt_poh_uuid, 0,
				   NULL, dp->sp_map, &tls->mpt_svc_list, &ph);
		if (rc) {
			D_ERROR("dsc_pool_open failed: "DF_RC"\n", DP_RC(rc));
			D_GOTO(free, rc);
		}

		tls->mpt_pool_hdl = ph;
	}

	if (tls->mpt_clear_conts) {
		destroy_existing_container(tls, cont_uuid);
		if (rc) {
			D_ERROR("destroy_existing_container failed: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(free, rc);
		}
	}

	/*
	 * Open the remote container as a *client* to be used later to pull
	 * objects
	 */
	rc = dsc_cont_open(tls->mpt_pool_hdl, cont_uuid, tls->mpt_coh_uuid,
			   0, &coh);
	if (rc) {
		D_ERROR("dsc_cont_open failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(free, rc);
	}

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

	rc1 = dsc_cont_close(tls->mpt_pool_hdl, coh);
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
	if (rc) {
		D_ERROR("dbtree_iter_delete failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(free, rc);
	}

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_REBUILD,
			       NULL, NULL);

	if (rc == -DER_NONEXIST) {
		rc = 1; /* empty after delete */
		D_GOTO(free, rc);
	}
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
			D_ERROR("dbtree iterate failed: "DF_RC"\n", DP_RC(rc));
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
		D_ERROR("failed to create tree: "DF_RC"\n", DP_RC(rc));
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

	D_DEBUG(DB_REBUILD, "Insert migrate "DF_UOID" "DF_U64"/%d/%d\n",
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

	if (pool->sp_stopping) {
		D_DEBUG(DB_TRACE, DF_UUID" pool service is stopping.\n",
			DP_UUID(po_uuid));
		D_GOTO(out, rc = 0);
	}

	/* Check if the pool tls exists */
	pool_tls = migrate_pool_tls_lookup_create(pool, migrate_in->om_version,
						  po_hdl_uuid, co_hdl_uuid,
						  migrate_in->om_max_eph,
						  migrate_in->om_clear_conts);
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
	if (arg->dms.dm_status == 0)
		arg->dms.dm_status = tls->mpt_status;

	ABT_mutex_unlock(arg->status_lock);
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

	rc = dss_thread_collective(migrate_check_one, &arg, 0, DSS_ULT_REBUILD);
	if (rc)
		D_GOTO(out, rc);

	/**
	 * The object ULT is generated by 0 xstream, and dss_collective does not
	 * do collective on 0 xstream
	 **/
	arg.obj_generated_ult += tls->mpt_obj_generated_ult;
	*dms = arg.dms;
	if (arg.obj_generated_ult > arg.obj_executed_ult ||
	    arg.generated_ult > arg.executed_ult || tls->mpt_ult_running)
		dms->dm_migrating = 1;
	else
		dms->dm_migrating = 0;

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" migrating=%s,"
		" obj_count="DF_U64", rec_count="DF_U64
		" size="DF_U64" obj %u/%u general %u/%u status %d\n",
		DP_UUID(pool_uuid), dms->dm_migrating ? "yes" : "no",
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
 * param clear_conts [in]	remove container contents before migrating
 *
 * return			0 if it succeeds, otherwise errno.
 */
int
ds_object_migrate(struct ds_pool *pool, uuid_t pool_hdl_uuid,
		  uuid_t cont_hdl_uuid, uuid_t cont_uuid, int tgt_id,
		  uint32_t version, uint64_t max_eph, daos_unit_oid_t *oids,
		  daos_epoch_t *ephs, unsigned int *shards, int cnt,
		  int clear_conts)
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
	migrate_in->om_clear_conts = clear_conts;

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
