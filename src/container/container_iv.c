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
 * container IV cache
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/container.h>
#include "srv_internal.h"
#include <daos_srv/iv.h>
#include <daos/btree_class.h>
#include <daos/btree.h>
#include <daos/dtx.h>

/* XXX Temporary limit for IV */
#define MAX_SNAP_CNT	20

static struct cont_iv_key *
key2priv(struct ds_iv_key *iv_key)
{
	return (struct cont_iv_key *)iv_key->key_buf;
}

static uint32_t
cont_iv_snap_ent_size(int nr)
{
	return sizeof(struct cont_iv_entry) + nr * sizeof(uint64_t);
}

static int
cont_iv_snap_alloc_internal(d_sg_list_t *sgl)
{
	struct cont_iv_entry	*entry;
	int			entry_size;
	int			rc;

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	/* FIXME: allocate entry by the real size */
	entry_size = cont_iv_snap_ent_size(MAX_SNAP_CNT);
	D_ALLOC(entry, entry_size);
	if (entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	d_iov_set(&sgl->sg_iovs[0], entry, entry_size);
out:
	if (rc)
		daos_sgl_fini(sgl, true);

	return rc;
}

static int
cont_iv_ent_init(struct ds_iv_key *iv_key, void *data,
		 struct ds_iv_entry *entry)
{
	struct umem_attr uma = { 0 };
	daos_handle_t	 root_hdl;
	int		 rc;

	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create(DBTREE_CLASS_NV, 0, 4, &uma, NULL, &root_hdl);
	if (rc != 0) {
		D_ERROR("failed to create tree: %d\n", rc);
		return rc;
	}

	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;

	rc = daos_sgl_init(&entry->iv_value, 1);
	if (rc)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));
	if (entry->iv_value.sg_iovs[0].iov_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memcpy(entry->iv_value.sg_iovs[0].iov_buf, &root_hdl, sizeof(root_hdl));
out:
	if (rc != 0) {
		dbtree_destroy(root_hdl);
		daos_sgl_fini(&entry->iv_value, true);
	}

	return rc;
}

static int
cont_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
cont_iv_ent_put(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
delete_iter_cb(daos_handle_t ih, d_iov_t *key,
	       d_iov_t *val, void *arg)
{
	int rc;

	/* Delete the current container tree */
	rc = dbtree_iter_delete(ih, NULL);
	if (rc != 0)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_PUNCH,
			       NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	return rc;
}

static int
cont_iv_ent_destroy(d_sg_list_t *sgl)
{
	if (!sgl)
		return 0;

	if (sgl->sg_iovs && sgl->sg_iovs[0].iov_buf) {
		daos_handle_t *root_hdl = sgl->sg_iovs[0].iov_buf;
		int rc;

		while (!dbtree_is_empty(*root_hdl)) {
			rc = dbtree_iterate(*root_hdl, DAOS_INTENT_PUNCH, false,
					    delete_iter_cb, NULL);
			if (rc < 0) {
				D_ERROR("dbtree iterate fails %d\n", rc);
				return rc;
			}
		}
		dbtree_destroy(*root_hdl);
	}

	daos_sgl_fini(sgl, true);

	return 0;
}

static int
cont_iv_ent_copy(struct ds_iv_entry *entry, d_sg_list_t *dst_sgl,
		 struct cont_iv_entry *src)
{
	struct cont_iv_entry *dst = dst_sgl->sg_iovs[0].iov_buf;

	uuid_copy(dst->cont_uuid, src->cont_uuid);
	if (entry->iv_class->iv_class_id == IV_CONT_SNAP) {
		D_ASSERT(src->iv_snap.snap_cnt <= MAX_SNAP_CNT);

		dst->iv_snap.snap_cnt = src->iv_snap.snap_cnt;
		memcpy(dst->iv_snap.snaps, src->iv_snap.snaps,
		       src->iv_snap.snap_cnt * sizeof(src->iv_snap.snaps[0]));
	} else {
		D_ASSERT(entry->iv_class->iv_class_id == IV_CONT_CAPA);
		dst->iv_capa.capas = src->iv_capa.capas;
	}

	return 0;
}

static bool
is_master(struct ds_iv_entry *entry)
{
	d_rank_t myrank;

	crt_group_rank(NULL, &myrank);

	return entry->ns->iv_master_rank == myrank;
}

static int
cont_iv_snap_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key)
{
	struct cont_iv_entry	*iv_entry = NULL;
	struct cont_iv_key	*civ_key = key2priv(key);
	daos_handle_t		root_hdl;
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	daos_epoch_t		*snaps = NULL;
	int			snap_cnt = 0;
	int			rc;

	rc = ds_cont_get_snapshots(entry->ns->iv_pool_uuid,
				   civ_key->cont_uuid, &snaps,
				   &snap_cnt);
	if (rc)
		D_GOTO(out, rc);

	D_ALLOC(iv_entry, cont_iv_snap_ent_size(snap_cnt));
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf,
		sizeof(root_hdl));
	uuid_copy(iv_entry->cont_uuid, civ_key->cont_uuid);
	iv_entry->iv_snap.snap_cnt = snap_cnt;
	memcpy(iv_entry->iv_snap.snaps, snaps, snap_cnt * sizeof(*snaps));
	d_iov_set(&val_iov, iv_entry,
		  cont_iv_snap_ent_size(iv_entry->iv_snap.snap_cnt));
	d_iov_set(&key_iov, civ_key->cont_uuid, sizeof(uuid_t));
	rc = dbtree_update(root_hdl, &key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);
out:
	if (iv_entry != NULL)
		D_FREE(iv_entry);
	if (snaps != NULL)
		D_FREE(snaps);

	return rc;
}

static int
cont_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		  d_sg_list_t *dst, d_sg_list_t *src, void **priv)
{
	struct cont_iv_entry	*src_iv;
	daos_handle_t		root_hdl;
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	struct cont_iv_key	*civ_key = key2priv(key);
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));

	d_iov_set(&key_iov, civ_key->cont_uuid, sizeof(uuid_t));
	d_iov_set(&val_iov, NULL, 0);
again:
	rc = dbtree_lookup(root_hdl, &key_iov, &val_iov);
	if (rc < 0) {
		if (rc == -DER_NONEXIST && is_master(entry)) {
			if (entry->iv_class->iv_class_id == IV_CONT_SNAP) {
				rc = cont_iv_snap_ent_fetch(entry, key);
				goto again;
			} else if (entry->iv_class->iv_class_id ==
							IV_CONT_CAPA) {
				/* Can not find the handle on leader */
				rc = -DER_NONEXIST;
			}
		}
		D_DEBUG(DB_MGMT, "lookup cont: rc %d\n", rc);
		D_GOTO(out, rc);
	}

	src_iv = val_iov.iov_buf;
	rc = cont_iv_ent_copy(entry, dst, src_iv);
out:
	return rc;
}

static int
cont_iv_capa_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
			d_sg_list_t *src, void **priv)
{
	struct cont_iv_key	*civ_key = key2priv(key);
	struct cont_iv_entry	*civ_ent = src->sg_iovs[0].iov_buf;
	int rc;

	D_ASSERT(civ_ent != NULL);

	/* open the container locally */
	rc = ds_cont_tgt_open(entry->ns->iv_pool_uuid,
			      civ_key->cont_uuid, civ_ent->cont_uuid,
			      civ_ent->iv_capa.capas);
	return rc;
}

static int
cont_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		   d_sg_list_t *src, void **priv)
{
	daos_handle_t		root_hdl;
	struct cont_iv_key	*civ_key = key2priv(key);
	d_iov_t		key_iov;
	d_iov_t		val_iov;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	if (entry->iv_class->iv_class_id == IV_CONT_CAPA) {
		rc = cont_iv_capa_ent_update(entry, key, src, priv);
		if (rc)
			return rc;
	}

	/* Put it to IV tree */
	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));
	d_iov_set(&key_iov, civ_key->cont_uuid, sizeof(uuid_t));
	d_iov_set(&val_iov, src->sg_iovs[0].iov_buf,
		     src->sg_iovs[0].iov_len);
	rc = dbtree_update(root_hdl, &key_iov, &val_iov);
	if (rc < 0)
		D_ERROR("failed to insert: rc %d\n", rc);

	return rc;
}

static int
cont_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		    d_sg_list_t *src, int ref_rc, void **priv)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	return cont_iv_ent_update(entry, key, src, priv);
}

static int
cont_iv_capa_alloc_internal(d_sg_list_t *sgl)
{
	struct cont_iv_entry	*entry;
	int			rc;

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC_PTR(entry);
	if (!entry) {
		daos_sgl_fini(sgl, true);
		return -DER_NOMEM;
	}

	daos_iov_set(&sgl->sg_iovs[0], entry, sizeof(*entry));
	return 0;
}

static int
cont_iv_value_alloc(struct ds_iv_entry *entry, d_sg_list_t *sgl)
{
	D_ASSERT(entry->iv_class != NULL);

	if (entry->iv_class->iv_class_id == IV_CONT_SNAP)
		return cont_iv_snap_alloc_internal(sgl);
	else if (entry->iv_class->iv_class_id == IV_CONT_CAPA)
		return cont_iv_capa_alloc_internal(sgl);

	return 0;
}

static bool
cont_iv_ent_valid(struct ds_iv_entry *entry, struct ds_iv_key *key)
{
	daos_handle_t		root_hdl;
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	struct cont_iv_key	*civ_key = key2priv(key);
	int			rc;

	if (!entry->iv_valid)
		return false;

	/* Let's check whether the container really exist */
	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));
	d_iov_set(&key_iov, civ_key->cont_uuid, sizeof(uuid_t));
	d_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(root_hdl, &key_iov, &val_iov);
	if (rc != 0)
		return false;

	return true;
}

struct ds_iv_class_ops cont_iv_ops = {
	.ivc_ent_init	= cont_iv_ent_init,
	.ivc_ent_get	= cont_iv_ent_get,
	.ivc_ent_put	= cont_iv_ent_put,
	.ivc_ent_destroy = cont_iv_ent_destroy,
	.ivc_ent_fetch	= cont_iv_ent_fetch,
	.ivc_ent_update	= cont_iv_ent_update,
	.ivc_ent_refresh = cont_iv_ent_refresh,
	.ivc_value_alloc = cont_iv_value_alloc,
	.ivc_ent_valid	= cont_iv_ent_valid,
};

int
cont_iv_fetch(void *ns, int class_id, uuid_t key_uuid,
	      struct cont_iv_entry *cont_iv, int cont_iv_len)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key = { 0 };
	struct cont_iv_key	*civ_key;
	int			rc;

	iov.iov_buf = cont_iv;
	iov.iov_len = cont_iv_len;
	iov.iov_buf_len = cont_iv_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	key.class_id = class_id;
	civ_key = key2priv(&key);
	uuid_copy(civ_key->cont_uuid, key_uuid);
	rc = ds_iv_fetch(ns, &key, cont_iv ? &sgl : NULL);
	if (rc)
		D_ERROR(DF_UUID" iv fetch failed %d\n",
			DP_UUID(key_uuid), rc);

	return rc;
}

int
cont_iv_update(void *ns, int class_id, uuid_t key_uuid,
	       struct cont_iv_entry *cont_iv, int cont_iv_len,
	       unsigned int shortcut, unsigned int sync_mode)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	struct cont_iv_key	*civ_key;
	int			rc;

	iov.iov_buf = cont_iv;
	iov.iov_len = cont_iv_len;
	iov.iov_buf_len = cont_iv_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = class_id;
	civ_key = key2priv(&key);
	uuid_copy(civ_key->cont_uuid, key_uuid);
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0);
	if (rc)
		D_ERROR(DF_UUID" iv update failed %d\n", DP_UUID(key_uuid), rc);

	return rc;
}

int
cont_iv_snapshots_fetch(void *ns, uuid_t cont_uuid, uint64_t **snapshots,
			int *snap_count)
{
	struct cont_iv_entry	*iv_entry;
	int			iv_entry_size;
	int			rc;

	iv_entry_size = cont_iv_snap_ent_size(MAX_SNAP_CNT);
	D_ALLOC(iv_entry, iv_entry_size);
	if (iv_entry == NULL)
		return -DER_NOMEM;

	rc = cont_iv_fetch(ns, IV_CONT_SNAP, cont_uuid, iv_entry,
			   iv_entry_size);
	if (rc)
		D_GOTO(free, rc);

	D_ASSERT(iv_entry->iv_snap.snap_cnt <= MAX_SNAP_CNT);
	if (iv_entry->iv_snap.snap_cnt == 0) {
		*snap_count = 0;
		D_GOTO(free, rc = 0);
	}

	D_ALLOC(*snapshots,
	      sizeof(iv_entry->iv_snap.snaps[0]) * iv_entry->iv_snap.snap_cnt);
	if (*snapshots == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	memcpy(*snapshots, iv_entry->iv_snap.snaps,
	       sizeof(iv_entry->iv_snap.snaps[0]) * iv_entry->iv_snap.snap_cnt);
	*snap_count = iv_entry->iv_snap.snap_cnt;
free:
	D_FREE(iv_entry);
	return rc;
}

struct iv_capa_ult_arg {
	uuid_t		pool_uuid;
	uuid_t		cont_uuid;
	uuid_t		cont_hdl_uuid;
	ABT_eventual	eventual;
};

static void
cont_iv_capa_refresh_ult(void *data)
{
	struct iv_capa_ult_arg	*arg = data;
	struct cont_iv_entry	entry = { 0 };
	struct ds_pool		*pool;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	pool = ds_pool_lookup(arg->pool_uuid);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	rc = cont_iv_fetch(pool->sp_iv_ns, IV_CONT_CAPA,
			   arg->cont_hdl_uuid, &entry, sizeof(entry));
	if (rc)
		D_GOTO(out, rc);

	uuid_copy(arg->cont_uuid, entry.cont_uuid);
out:
	if (pool != NULL)
		ds_pool_put(pool);

	ABT_eventual_set(arg->eventual, (void *)&rc, sizeof(rc));
}

int
cont_iv_capa_fetch(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		   uuid_t cont_uuid, struct ds_cont_hdl **cont_hdl)
{
	struct iv_capa_ult_arg	arg;
	ABT_eventual		eventual;
	int			*status;
	int			rc;

	if (!DAOS_FAIL_CHECK(DAOS_FORCE_CAPA_FETCH)) {
		*cont_hdl = ds_cont_hdl_lookup(cont_hdl_uuid);
		if (*cont_hdl != NULL)
			return 0;
	}

	D_DEBUG(DB_TRACE, "Can not find "DF_UUID" hdl\n",
		DP_UUID(cont_hdl_uuid));

	/* Fetch the capability from the leader. To avoid extra locks,
	 * all metadatas are maintained by xstream 0, so let's create
	 * an ULT on xstream 0 to let xstream 0 to handle capa fetch
	 * and update.
	 */
	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	uuid_copy(arg.pool_uuid, pool_uuid);
	uuid_copy(arg.cont_hdl_uuid, cont_hdl_uuid);
	arg.eventual = eventual;
	rc = dss_ult_create(cont_iv_capa_refresh_ult, &arg,
			    DSS_ULT_POOL_SRV, 0, 0, NULL);
	if (rc)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));
	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

	if (uuid_compare(arg.cont_uuid, cont_uuid) != 0) {
		D_ERROR("different cont "DF_UUID" != "DF_UUID"\n",
			DP_UUID(arg.cont_uuid), DP_UUID(cont_uuid));
		D_GOTO(out_eventual, rc = -DER_NONEXIST);
	}

	*cont_hdl = ds_cont_hdl_lookup(cont_hdl_uuid);
	if (*cont_hdl == NULL) {
		D_DEBUG(DB_TRACE, "Can not find "DF_UUID" hdl\n",
			DP_UUID(cont_hdl_uuid));
		D_GOTO(out_eventual, rc = -DER_NONEXIST);
	}

out_eventual:
	ABT_eventual_free(&eventual);
	return rc;
}

int
cont_iv_capability_update(void *ns, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
			  uint64_t capas)
{
	struct cont_iv_entry	iv_entry = { 0 };
	struct cont_iv_key	key = { 0 };
	int			rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	iv_entry.iv_capa.capas = capas;
	uuid_copy(iv_entry.cont_uuid, cont_uuid);
	uuid_copy(key.cont_uuid, cont_hdl_uuid);

	rc = cont_iv_update(ns, IV_CONT_CAPA, cont_hdl_uuid, &iv_entry,
			    sizeof(struct cont_iv_entry),
			    CRT_IV_SHORTCUT_TO_ROOT, CRT_IV_SYNC_LAZY);
	return rc;
}

int
ds_cont_iv_init(void)
{
	int rc;

	rc = ds_iv_class_register(IV_CONT_CAPA, &iv_cache_ops, &cont_iv_ops);
	if (rc)
		return rc;

	rc = ds_iv_class_register(IV_CONT_SNAP, &iv_cache_ops, &cont_iv_ops);
	if (rc) {
		ds_iv_class_unregister(IV_CONT_SNAP);
		return rc;
	}

	return rc;
}

int
ds_cont_iv_fini(void)
{
	ds_iv_class_unregister(IV_CONT_SNAP);
	ds_iv_class_unregister(IV_CONT_CAPA);

	return 0;
}
