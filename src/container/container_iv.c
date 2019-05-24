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
/* Container IV structure */
struct cont_iv_entry {
	uuid_t	cont_uuid;
	int	snap_size;
	uint64_t snaps[0];
};

struct cont_iv_key {
	uuid_t	cont_uuid;
};

static struct cont_iv_key *
key2priv(struct ds_iv_key *iv_key)
{
	return (struct cont_iv_key *)iv_key->key_buf;
}

static uint32_t
cont_iv_ent_size(int nr)
{
	return sizeof(struct cont_iv_entry) + nr * sizeof(uint64_t);
}

static int
cont_iv_value_alloc_internal(d_sg_list_t *sgl)
{
	struct cont_iv_entry	*entry;
	int			entry_size;
	int			rc;

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	/* FIXME: allocate entry by the real size */
	entry_size = cont_iv_ent_size(MAX_SNAP_CNT);
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
cont_iv_ent_copy(struct cont_iv_entry *dst, struct cont_iv_entry *src)
{
	D_ASSERT(src->snap_size < MAX_SNAP_CNT);

	uuid_copy(dst->cont_uuid, src->cont_uuid);
	dst->snap_size = src->snap_size;

	memcpy(dst->snaps, src->snaps, src->snap_size * sizeof(src->snaps[0]));
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
cont_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		  d_sg_list_t *dst, d_sg_list_t *src, void **priv)
{
	struct cont_iv_entry	*src_iv;
	daos_handle_t		root_hdl;
	struct cont_iv_key	*civ_key = key2priv(key);
	d_iov_t		key_iov;
	d_iov_t		val_iov;
	struct cont_iv_entry	*iv_entry = NULL;
	daos_epoch_t		*snaps = NULL;
	int			snap_cnt = MAX_SNAP_CNT;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));

	d_iov_set(&key_iov, civ_key->cont_uuid, sizeof(uuid_t));
	d_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(root_hdl, &key_iov, &val_iov);
	if (rc < 0) {
		if (rc == -DER_NONEXIST && is_master(entry)) {
			rc = ds_cont_get_snapshots(entry->ns->iv_pool_uuid,
						  civ_key->cont_uuid, &snaps,
						  &snap_cnt);
			if (rc)
				D_GOTO(out, rc);

			D_ALLOC(iv_entry, cont_iv_ent_size(snap_cnt));
			if (iv_entry == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			uuid_copy(iv_entry->cont_uuid, civ_key->cont_uuid);
			iv_entry->snap_size = snap_cnt;
			memcpy(iv_entry->snaps, snaps,
			       snap_cnt * sizeof(daos_epoch_t));
			d_iov_set(&val_iov, iv_entry,
				     cont_iv_ent_size(iv_entry->snap_size));
			rc = dbtree_update(root_hdl, &key_iov, &val_iov);
			if (rc)
				D_GOTO(out, rc);
		} else {
			D_DEBUG(DB_MGMT, "lookup cont: rc %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	src_iv = val_iov.iov_buf;

	rc = cont_iv_ent_copy((struct cont_iv_entry *)dst->sg_iovs[0].iov_buf,
				src_iv);

out:
	if (iv_entry != NULL)
		D_FREE(iv_entry);
	if (snaps != NULL)
		D_FREE(snaps);

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
cont_iv_value_alloc(struct ds_iv_entry *entry, d_sg_list_t *sgl)
{
	return cont_iv_value_alloc_internal(sgl);
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
};

int
cont_iv_fetch(void *ns, struct cont_iv_entry *cont_iv)
{
	d_sg_list_t		sgl;
	d_iov_t		iov;
	uint32_t		cont_iv_len;
	struct ds_iv_key	key = { 0 };
	struct cont_iv_key	*civ_key;
	int			rc;

	cont_iv_len = cont_iv_ent_size(cont_iv->snap_size);
	iov.iov_buf = cont_iv;
	iov.iov_len = cont_iv_len;
	iov.iov_buf_len = cont_iv_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_CONT_SNAP;
	civ_key = key2priv(&key);
	uuid_copy(civ_key->cont_uuid, cont_iv->cont_uuid);
	rc = ds_iv_fetch(ns, &key, &sgl);
	if (rc)
		D_ERROR(DF_UUID" iv fetch failed %d\n",
			DP_UUID(cont_iv->cont_uuid), rc);

	D_DEBUG(DB_TRACE, "snap_size is %d\n", cont_iv->snap_size);
	return rc;
}

int
cont_iv_update(void *ns, struct cont_iv_entry *cont_iv,
	       unsigned int shortcut, unsigned int sync_mode)
{
	d_sg_list_t		sgl;
	d_iov_t		iov;
	uint32_t		cont_iv_len;
	struct ds_iv_key	key;
	struct cont_iv_key	*civ_key;
	int			rc;

	cont_iv_len = cont_iv_ent_size(cont_iv->snap_size);
	iov.iov_buf = cont_iv;
	iov.iov_len = cont_iv_len;
	iov.iov_buf_len = cont_iv_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_CONT_SNAP;
	civ_key = key2priv(&key);
	uuid_copy(civ_key->cont_uuid, cont_iv->cont_uuid);
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0);
	if (rc)
		D_ERROR("iv update failed %d\n", rc);

	return rc;
}

int
cont_iv_snapshots_fetch(void *ns, uuid_t cont_uuid, uint64_t **snapshots,
			int *snap_count)
{
	struct cont_iv_entry	*iv_entry;
	int			rc;

	D_ALLOC(iv_entry, cont_iv_ent_size(MAX_SNAP_CNT));
	if (iv_entry == NULL)
		return -DER_NOMEM;

	uuid_copy(iv_entry->cont_uuid, cont_uuid);
	iv_entry->snap_size = MAX_SNAP_CNT;
	rc = cont_iv_fetch(ns, iv_entry);
	if (rc)
		D_GOTO(free, rc);

	D_ASSERT(iv_entry->snap_size <= MAX_SNAP_CNT);
	if (iv_entry->snap_size == 0) {
		*snap_count = 0;
		D_GOTO(free, rc = 0);
	}

	D_ALLOC(*snapshots, sizeof(iv_entry->snaps[0]) * iv_entry->snap_size);
	if (*snapshots == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	memcpy(*snapshots, iv_entry->snaps,
	       sizeof(iv_entry->snaps[0]) * iv_entry->snap_size);
	*snap_count = iv_entry->snap_size;
free:
	D_FREE(iv_entry);
	return rc;
}


int
ds_cont_iv_init(void)
{
	return ds_iv_class_register(IV_CONT_SNAP, &iv_cache_ops,
				    &cont_iv_ops);
}

int
ds_cont_iv_fini(void)
{
	return ds_iv_class_unregister(IV_CONT_SNAP);
}
