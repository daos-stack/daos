/**
 * (C) Copyright 2019-2021 Intel Corporation.
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
#include <daos_api.h>

/* XXX Temporary limit for IV */
#define MAX_SNAP_CNT	50

static struct cont_iv_key *
key2priv(struct ds_iv_key *iv_key)
{
	return (struct cont_iv_key *)iv_key->key_buf;
}

static uint32_t
cont_iv_snap_ent_size(int nr)
{
	return offsetof(struct cont_iv_entry, iv_snap.snaps[nr]);
}

static int
cont_iv_snap_alloc_internal(d_sg_list_t *sgl)
{
	struct cont_iv_entry	*entry;
	int			entry_size;
	int			rc;

	rc = d_sgl_init(sgl, 1);
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
		d_sgl_fini(sgl, true);

	return rc;
}

static uint32_t
cont_iv_prop_ent_size(int nr)
{
	return offsetof(struct cont_iv_entry, iv_prop.cip_acl.dal_ace[nr]);
}

static int
cont_iv_prop_alloc_internal(d_sg_list_t *sgl)
{
	struct cont_iv_entry	*entry;
	int			entry_size;
	int			rc;

	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	entry_size = cont_iv_prop_ent_size(DAOS_ACL_MAX_ACE_LEN);
	D_ALLOC(entry, entry_size);
	if (entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	d_iov_set(&sgl->sg_iovs[0], entry, entry_size);
out:
	if (rc)
		d_sgl_fini(sgl, true);

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
		D_ERROR("failed to create tree: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;

	rc = d_sgl_init(&entry->iv_value, 1);
	if (rc)
		D_GOTO(out, rc);

	D_ALLOC(entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));
	if (entry->iv_value.sg_iovs[0].iov_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memcpy(entry->iv_value.sg_iovs[0].iov_buf, &root_hdl, sizeof(root_hdl));
out:
	if (rc != 0) {
		dbtree_destroy(root_hdl, NULL);
		d_sgl_fini(&entry->iv_value, true);
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
dbtree_empty(daos_handle_t root_hdl)
{
	int rc = 0;

	while (!dbtree_is_empty(root_hdl)) {
		rc = dbtree_iterate(root_hdl, DAOS_INTENT_PUNCH, false,
				    delete_iter_cb, NULL);
		if (rc < 0) {
			D_ERROR("dbtree iterate fails %d\n", rc);
			return rc;
		}
	}

	return rc;
}

static int
cont_iv_ent_destroy(d_sg_list_t *sgl)
{
	if (!sgl)
		return 0;

	if (sgl->sg_iovs && sgl->sg_iovs[0].iov_buf) {
		daos_handle_t *root_hdl = sgl->sg_iovs[0].iov_buf;
		dbtree_destroy(*root_hdl, NULL);
	}

	d_sgl_fini(sgl, true);

	return 0;
}

static int
cont_iv_ent_copy(struct ds_iv_entry *entry, d_sg_list_t *dst_sgl,
		 struct cont_iv_entry *src)
{
	struct cont_iv_entry *dst = dst_sgl->sg_iovs[0].iov_buf;

	uuid_copy(dst->cont_uuid, src->cont_uuid);
	switch (entry->iv_class->iv_class_id) {
	case IV_CONT_SNAP:
		D_ASSERT(src->iv_snap.snap_cnt <= MAX_SNAP_CNT);

		dst->iv_snap.snap_cnt = src->iv_snap.snap_cnt;
		memcpy(dst->iv_snap.snaps, src->iv_snap.snaps,
		       src->iv_snap.snap_cnt * sizeof(src->iv_snap.snaps[0]));
		break;
	case IV_CONT_CAPA:
		dst->iv_capa.flags = src->iv_capa.flags;
		dst->iv_capa.sec_capas = src->iv_capa.sec_capas;
		break;
	case IV_CONT_PROP:
		memcpy(&dst->iv_prop, &src->iv_prop,
		       offsetof(struct cont_iv_prop,
				cip_acl.dal_ace[src->iv_prop.cip_acl.dal_len]));
		break;
	default:
		D_ERROR("bad iv_class_id %d.\n", entry->iv_class->iv_class_id);
		return -DER_INVAL;
	};

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
cont_iv_snap_ent_create(struct ds_iv_entry *entry, struct ds_iv_key *key)
{
	struct cont_iv_entry	*iv_entry = NULL;
	struct cont_iv_key	*civ_key = key2priv(key);
	daos_handle_t		root_hdl;
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	daos_epoch_t		*snaps = NULL;
	int			snap_cnt = MAX_SNAP_CNT;
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
	d_iov_set(&key_iov, civ_key, sizeof(*civ_key));
	rc = dbtree_update(root_hdl, &key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);
out:
	if (iv_entry != NULL)
		D_FREE(iv_entry);
	if (snaps)
		D_FREE(snaps);
	return rc;
}

static void
cont_iv_prop_l2g(daos_prop_t *prop, struct cont_iv_prop *iv_prop)
{
	struct daos_prop_entry	*prop_entry;
	struct daos_acl		*acl;
	int			 i;

	D_ASSERT(prop->dpp_nr == CONT_PROP_NUM);
	for (i = 0; i < CONT_PROP_NUM; i++) {
		prop_entry = &prop->dpp_entries[i];
		switch (prop_entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_PROP_LABEL_MAX_LEN);
			strcpy(iv_prop->cip_label, prop_entry->dpe_str);
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			iv_prop->cip_layout_type = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			iv_prop->cip_layout_ver = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_CSUM:
			iv_prop->cip_csum = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			iv_prop->cip_csum_chunk_size = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			iv_prop->cip_csum_server_verify = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_DEDUP:
			iv_prop->cip_dedup = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			iv_prop->cip_dedup_size = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			iv_prop->cip_redun_fac = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			iv_prop->cip_redun_lvl = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
			iv_prop->cip_snap_max = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_COMPRESS:
			iv_prop->cip_compress = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_ENCRYPT:
			iv_prop->cip_encrypt = prop_entry->dpe_val;
			break;
		case DAOS_PROP_CO_ACL:
			acl = prop_entry->dpe_val_ptr;
			if (acl != NULL)
				memcpy(&iv_prop->cip_acl, acl,
				       daos_acl_get_size(acl));
			break;
		case DAOS_PROP_CO_OWNER:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			strcpy(iv_prop->cip_owner, prop_entry->dpe_str);
			break;
		case DAOS_PROP_CO_OWNER_GROUP:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			strcpy(iv_prop->cip_owner_grp, prop_entry->dpe_str);
			break;
		default:
			D_ASSERTF(0, "bad dpe_type %d\n", prop_entry->dpe_type);
			break;
		}
	}
}

static int
cont_iv_prop_ent_create(struct ds_iv_entry *entry, struct ds_iv_key *key)
{
	struct cont_iv_entry	*iv_entry = NULL;
	struct cont_iv_key	*civ_key = key2priv(key);
	daos_handle_t		root_hdl;
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	uint32_t		entry_size;
	daos_prop_t		*prop = NULL;
	int			 rc;

	rc = ds_cont_get_prop(entry->ns->iv_pool_uuid, civ_key->cont_uuid,
			      &prop);
	if (rc)
		D_GOTO(out, rc);

	entry_size = cont_iv_prop_ent_size(DAOS_ACL_MAX_ACE_LEN);
	D_ALLOC(iv_entry, entry_size);
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf,
		sizeof(root_hdl));

	uuid_copy(iv_entry->cont_uuid, civ_key->cont_uuid);
	cont_iv_prop_l2g(prop, &iv_entry->iv_prop);
	d_iov_set(&val_iov, iv_entry, entry_size);
	d_iov_set(&key_iov, civ_key, sizeof(*civ_key));

	rc = dbtree_update(root_hdl, &key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);
out:
	if (prop != NULL)
		daos_prop_free(prop);
	if (iv_entry != NULL)
		D_FREE(iv_entry);
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

	d_iov_set(&key_iov, civ_key, sizeof(*civ_key));
	d_iov_set(&val_iov, NULL, 0);
again:
	rc = dbtree_lookup(root_hdl, &key_iov, &val_iov);
	if (rc < 0) {
		if (rc == -DER_NONEXIST && is_master(entry)) {
			int	class_id;

			class_id = entry->iv_class->iv_class_id;
			if (class_id == IV_CONT_SNAP) {
				rc = cont_iv_snap_ent_create(entry, key);
				if (rc == 0)
					goto again;
				D_ERROR("create cont snap iv entry failed "
					""DF_RC"\n", DP_RC(rc));
			} else if (class_id == IV_CONT_PROP) {
				rc = cont_iv_prop_ent_create(entry, key);
				if (rc == 0)
					goto again;
				D_ERROR("create cont prop iv entry failed "
					""DF_RC"\n", DP_RC(rc));
			} else if (class_id == IV_CONT_CAPA) {
				/* Can not find the handle on leader */
				rc = -DER_NONEXIST;
			}
		}
		D_DEBUG(DB_MGMT, "lookup cont: rc "DF_RC"\n", DP_RC(rc));
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
	struct cont_iv_entry	*civ_ent;
	int rc;

	civ_ent = src->sg_iovs[0].iov_buf;
	/* open the container locally */
	rc = ds_cont_tgt_open(entry->ns->iv_pool_uuid,
			      civ_key->cont_uuid, civ_ent->cont_uuid,
			      civ_ent->iv_capa.flags,
			      civ_ent->iv_capa.sec_capas);
	return rc;
}

/* Update the EC agg epoch all servers to the leader */
static int
cont_iv_ent_agg_eph_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
			   d_sg_list_t *src)
{
	struct cont_iv_key	*civ_key = key2priv(key);
	struct cont_iv_entry	*civ_ent = src->sg_iovs[0].iov_buf;
	d_rank_t		rank;
	int			rc;

	rc = crt_group_rank(NULL, &rank);
	if (rc)
		return rc;

	if (rank != entry->ns->iv_master_rank)
		return -DER_IVCB_FORWARD;

	rc = ds_cont_leader_update_agg_eph(entry->ns->iv_pool_uuid,
					   civ_key->cont_uuid,
					   civ_ent->iv_agg_eph.rank,
					   civ_ent->iv_agg_eph.eph);
	return rc;
}

/* Each server refresh the VOS aggregation epoch gotten from the leader */
static int
cont_iv_ent_agg_eph_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
			    d_sg_list_t *src)
{
	struct cont_iv_entry	*civ_ent = src->sg_iovs[0].iov_buf;
	struct cont_iv_key	*civ_key = key2priv(key);
	int			rc;

	rc = ds_cont_tgt_refresh_agg_eph(entry->ns->iv_pool_uuid,
					 civ_key->cont_uuid,
					 civ_ent->iv_agg_eph.eph);
	return rc;
}

static int
cont_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		   d_sg_list_t *src, void **priv)
{
	daos_handle_t		root_hdl;
	struct cont_iv_key	*civ_key = key2priv(key);
	d_iov_t			key_iov;
	d_iov_t			val_iov;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	if (src != NULL) {
		if (entry->iv_class->iv_class_id == IV_CONT_CAPA) {
			rc = cont_iv_capa_ent_update(entry, key, src, priv);
			if (rc)
				return rc;
		} else if (entry->iv_class->iv_class_id == IV_CONT_SNAP) {
			struct cont_iv_entry *civ_ent = src->sg_iovs[0].iov_buf;

			rc = ds_cont_tgt_snapshots_update(
						entry->ns->iv_pool_uuid,
						civ_key->cont_uuid,
						&civ_ent->iv_snap.snaps[0],
						civ_ent->iv_snap.snap_cnt);
			if (rc)
				return rc;
		} else if (entry->iv_class->iv_class_id ==
						IV_CONT_AGG_EPOCH_REPORT) {
			rc = cont_iv_ent_agg_eph_update(entry, key, src);
			if (rc)
				return rc;
		} else if (entry->iv_class->iv_class_id ==
						IV_CONT_AGG_EPOCH_BOUNDRY) {
			rc = cont_iv_ent_agg_eph_refresh(entry, key, src);
			if (rc)
				return rc;
		}
	}

	memcpy(&root_hdl, entry->iv_value.sg_iovs[0].iov_buf, sizeof(root_hdl));
	d_iov_set(&key_iov, civ_key, sizeof(*civ_key));
	if (src == NULL) {
		/* If src == NULL, it is invalidate */
		if (entry->iv_class->iv_class_id == IV_CONT_CAPA &&
		    !uuid_is_null(civ_key->cont_uuid)) {
			rc = ds_cont_tgt_close(civ_key->cont_uuid);
			if (rc)
				return rc;
		}

		if (uuid_is_null(civ_key->cont_uuid)) {
			rc = dbtree_empty(root_hdl);
			if (rc)
				return rc;
		} else {
			rc = dbtree_delete(root_hdl, BTR_PROBE_EQ, &key_iov,
					   NULL);
			if (rc == -DER_NONEXIST)
				rc = 0;
		}
	} else {
		/* Put it to IV tree */
		d_iov_set(&val_iov, src->sg_iovs[0].iov_buf,
			     src->sg_iovs[0].iov_len);
		rc = dbtree_update(root_hdl, &key_iov, &val_iov);
	}

	if (rc < 0)
		D_ERROR("failed to insert: rc "DF_RC"\n", DP_RC(rc));

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
cont_iv_ent_alloc_internal(d_sg_list_t *sgl)
{
	struct cont_iv_entry	*entry;
	int			rc;

	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC_PTR(entry);
	if (!entry) {
		d_sgl_fini(sgl, true);
		return -DER_NOMEM;
	}

	d_iov_set(&sgl->sg_iovs[0], entry, sizeof(*entry));
	return 0;
}

static int
cont_iv_value_alloc(struct ds_iv_entry *entry, d_sg_list_t *sgl)
{
	D_ASSERT(entry->iv_class != NULL);

	switch (entry->iv_class->iv_class_id) {
	case IV_CONT_SNAP:
		return cont_iv_snap_alloc_internal(sgl);
	case IV_CONT_CAPA:
	case IV_CONT_AGG_EPOCH_REPORT:
	case IV_CONT_AGG_EPOCH_BOUNDRY:
		return cont_iv_ent_alloc_internal(sgl);
	case IV_CONT_PROP:
		return cont_iv_prop_alloc_internal(sgl);
	default:
		D_ERROR("bad iv_class_id %d.\n", entry->iv_class->iv_class_id);
		return -DER_INVAL;
	};

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
	d_iov_set(&key_iov, civ_key, sizeof(*civ_key));
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

static int
cont_iv_fetch(void *ns, int class_id, uuid_t key_uuid,
	      struct cont_iv_entry *cont_iv, int cont_iv_len, bool retry)
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
	civ_key->class_id = class_id;
	rc = ds_iv_fetch(ns, &key, cont_iv ? &sgl : NULL, retry);
	if (rc)
		D_CDEBUG(rc != -DER_NOTLEADER, DLOG_ERR, DB_MGMT,
			 DF_UUID" iv fetch failed "DF_RC"\n",
			 DP_UUID(key_uuid), DP_RC(rc));

	return rc;
}

static int
cont_iv_update(void *ns, int class_id, uuid_t key_uuid,
	       struct cont_iv_entry *cont_iv, int cont_iv_len,
	       unsigned int shortcut, unsigned int sync_mode, bool retry)
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
	civ_key->class_id = class_id;
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0, retry);
	if (rc)
		D_CDEBUG(rc == -DER_NOTLEADER, DB_ANY, DLOG_ERR,
			 DF_UUID" iv update failed "DF_RC"\n",
			 DP_UUID(key_uuid), DP_RC(rc));
	return rc;
}

static int
cont_iv_snapshot_invalidate(void *ns, uuid_t cont_uuid, unsigned int shortcut,
			    unsigned int sync_mode)
{
	struct ds_iv_key	key = { 0 };
	struct cont_iv_key	*civ_key;
	int			rc;

	civ_key = key2priv(&key);
	uuid_copy(civ_key->cont_uuid, cont_uuid);
	key.class_id = IV_CONT_SNAP;
	rc = ds_iv_invalidate(ns, &key, shortcut, sync_mode, 0, false);
	if (rc)
		D_ERROR("iv invalidate failed %d\n", rc);

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
			   iv_entry_size, true);
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

int
cont_iv_snapshots_update(void *ns, uuid_t cont_uuid, uint64_t *snapshots,
			 int snap_count)
{
	struct cont_iv_entry	*iv_entry;
	uint32_t		 entry_size;
	int			 rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry_size = cont_iv_snap_ent_size(snap_count);
	D_ALLOC(iv_entry, entry_size);
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(iv_entry->cont_uuid, cont_uuid);
	iv_entry->iv_snap.snap_cnt = snap_count;
	memcpy(iv_entry->iv_snap.snaps, snapshots,
	       sizeof(*snapshots) * snap_count);

	rc = cont_iv_update(ns, IV_CONT_SNAP, cont_uuid, iv_entry, entry_size,
			    CRT_IV_SHORTCUT_TO_ROOT, CRT_IV_SYNC_EAGER,
			    false /* retry */);
	D_FREE(iv_entry);
out:
	return rc;
}

int
cont_iv_snapshots_refresh(void *ns, uuid_t cont_uuid)
{
	struct cont_iv_entry	*iv_entry;
	uint32_t		 entry_size;
	int			 rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry_size = cont_iv_snap_ent_size(MAX_SNAP_CNT);
	D_ALLOC(iv_entry, entry_size);
	if (iv_entry == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	rc = cont_iv_fetch(ns, IV_CONT_SNAP, cont_uuid, iv_entry, entry_size,
			   false /* retry */);
	if (rc == 0)
		D_ASSERT(iv_entry->iv_snap.snap_cnt <= MAX_SNAP_CNT);

	D_FREE(iv_entry);
out:
	return rc;
}

struct iv_capa_ult_arg {
	uuid_t		pool_uuid;
	uuid_t		cont_uuid;
	uuid_t		cont_hdl_uuid;
	/* This is only for testing purpose to
	 * invalidate the current hdl inside ULT.
	 */
	bool		invalidate_current;
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

	if (arg->invalidate_current) {
		rc = cont_iv_capability_invalidate(pool->sp_iv_ns,
						   arg->cont_hdl_uuid,
						   CRT_IV_SYNC_NONE);
		if (rc)
			D_GOTO(out, rc);
	}

	rc = cont_iv_fetch(pool->sp_iv_ns, IV_CONT_CAPA,
			   arg->cont_hdl_uuid, &entry, sizeof(entry),
			   false /* retry */);
	if (rc)
		D_GOTO(out, rc);

	uuid_copy(arg->cont_uuid, entry.cont_uuid);
out:
	if (pool != NULL)
		ds_pool_put(pool);

	ABT_eventual_set(arg->eventual, (void *)&rc, sizeof(rc));
}

static int
cont_iv_hdl_fetch(uuid_t cont_hdl_uuid, uuid_t pool_uuid,
		  struct ds_cont_hdl **cont_hdl)
{
	struct iv_capa_ult_arg	arg;
	ABT_eventual		eventual;
	bool			invalidate_current = false;
	int			*status;
	int			rc;

	if (DAOS_FAIL_CHECK(DAOS_FORCE_CAPA_FETCH)) {
		invalidate_current = true;
	} else {
		*cont_hdl = ds_cont_hdl_lookup(cont_hdl_uuid);
		if (*cont_hdl != NULL) {
			D_DEBUG(DB_TRACE, "get hdl %p\n", cont_hdl);
			return 0;
		}
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
	arg.invalidate_current = invalidate_current;
	rc = dss_ult_create(cont_iv_capa_refresh_ult, &arg, DSS_XS_SYS,
			    0, 0, NULL);
	if (rc)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));
	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

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
cont_iv_ec_agg_eph_update_internal(void *ns, uuid_t cont_uuid,
				   daos_epoch_t eph, unsigned int shortcut,
				   unsigned int sync_mode,
				   uint32_t op)
{
	struct cont_iv_entry	iv_entry = { 0 };
	int			rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	iv_entry.iv_agg_eph.eph = eph;
	uuid_copy(iv_entry.cont_uuid, cont_uuid);
	rc = crt_group_rank(NULL, &iv_entry.iv_agg_eph.rank);
	if (rc)
		return rc;

	rc = cont_iv_update(ns, op, cont_uuid, &iv_entry,
			    sizeof(struct cont_iv_entry), shortcut,
			    sync_mode, false /* retry */);
	return rc;
}

int
cont_iv_ec_agg_eph_update(void *ns, uuid_t cont_uuid, daos_epoch_t eph)
{
	return cont_iv_ec_agg_eph_update_internal(ns, cont_uuid, eph,
						  CRT_IV_SHORTCUT_TO_ROOT,
						  CRT_IV_SYNC_NONE,
						  IV_CONT_AGG_EPOCH_REPORT);
}

int
cont_iv_ec_agg_eph_refresh(void *ns, uuid_t cont_uuid, daos_epoch_t eph)
{
	return cont_iv_ec_agg_eph_update_internal(ns, cont_uuid, eph,
						  0, CRT_IV_SYNC_LAZY,
						  IV_CONT_AGG_EPOCH_BOUNDRY);
}

int
cont_iv_capability_update(void *ns, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
			  uint64_t flags, uint64_t sec_capas)
{
	struct cont_iv_entry	iv_entry = { 0 };
	int			rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	iv_entry.iv_capa.flags = flags;
	iv_entry.iv_capa.sec_capas = sec_capas;
	uuid_copy(iv_entry.cont_uuid, cont_uuid);

	rc = cont_iv_update(ns, IV_CONT_CAPA, cont_hdl_uuid, &iv_entry,
			    sizeof(struct cont_iv_entry),
			    CRT_IV_SHORTCUT_TO_ROOT, CRT_IV_SYNC_EAGER,
			    false /* retry */);
	return rc;
}

int
cont_iv_capability_invalidate(void *ns, uuid_t cont_hdl_uuid, int mode)
{
	struct ds_iv_key	key = { 0 };
	struct cont_iv_key	*civ_key;
	int			rc;

	civ_key = key2priv(&key);
	uuid_copy(civ_key->cont_uuid, cont_hdl_uuid);
	civ_key->class_id = IV_CONT_CAPA;

	key.class_id = IV_CONT_CAPA;
	rc = ds_iv_invalidate(ns, &key, 0, mode, 0, false /* retry */);
	if (rc)
		D_ERROR("iv invalidate failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int
cont_iv_prop_g2l(struct cont_iv_prop *iv_prop, daos_prop_t *prop)
{
	struct daos_prop_entry	*prop_entry;
	struct daos_acl		*acl;
	void			*label_alloc = NULL;
	void			*acl_alloc = NULL;
	void			*owner_alloc = NULL;
	void			*owner_grp_alloc = NULL;
	int			 i;
	int			 rc = 0;

	D_ASSERT(prop->dpp_nr == CONT_PROP_NUM);
	for (i = 0; i < CONT_PROP_NUM; i++) {
		prop_entry = &prop->dpp_entries[i];
		prop_entry->dpe_type = DAOS_PROP_CO_MIN + i + 1;
		switch (prop_entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			D_ASSERT(strlen(iv_prop->cip_label) <=
				 DAOS_PROP_LABEL_MAX_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->cip_label,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (prop_entry->dpe_str)
				label_alloc = prop_entry->dpe_str;
			else
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			prop_entry->dpe_val = iv_prop->cip_layout_type;
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			prop_entry->dpe_val = iv_prop->cip_layout_ver;
			break;
		case DAOS_PROP_CO_CSUM:
			prop_entry->dpe_val = iv_prop->cip_csum;
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			prop_entry->dpe_val = iv_prop->cip_csum_chunk_size;
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			prop_entry->dpe_val = iv_prop->cip_csum_server_verify;
			break;
		case DAOS_PROP_CO_DEDUP:
			prop_entry->dpe_val = iv_prop->cip_dedup;
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			prop_entry->dpe_val = iv_prop->cip_dedup_size;
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			prop_entry->dpe_val = iv_prop->cip_redun_fac;
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			prop_entry->dpe_val = iv_prop->cip_redun_lvl;
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
			prop_entry->dpe_val = iv_prop->cip_snap_max;
			break;
		case DAOS_PROP_CO_COMPRESS:
			prop_entry->dpe_val = iv_prop->cip_compress;
			break;
		case DAOS_PROP_CO_ENCRYPT:
			prop_entry->dpe_val = iv_prop->cip_encrypt;
			break;
		case DAOS_PROP_CO_ACL:
			acl = &iv_prop->cip_acl;
			if (acl->dal_ver != 0) {
				D_ASSERT(daos_acl_validate(acl) == 0);
				acl_alloc = daos_acl_dup(acl);
				if (acl_alloc != NULL)
					prop_entry->dpe_val_ptr = acl_alloc;
				else
					D_GOTO(out, rc = -DER_NOMEM);
			} else {
				prop_entry->dpe_val_ptr = NULL;
			}
			break;
		case DAOS_PROP_CO_OWNER:
			D_ASSERT(strlen(iv_prop->cip_owner) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->cip_owner,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (prop_entry->dpe_str)
				owner_alloc = prop_entry->dpe_str;
			else
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case DAOS_PROP_CO_OWNER_GROUP:
			D_ASSERT(strlen(iv_prop->cip_owner_grp) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->cip_owner_grp,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (prop_entry->dpe_str)
				owner_grp_alloc = prop_entry->dpe_str;
			else
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		default:
			D_ASSERTF(0, "bad dpe_type %d\n", prop_entry->dpe_type);
			break;
		}
	}

out:
	if (rc) {
		if (acl_alloc)
			daos_acl_free(acl_alloc);
		if (label_alloc)
			D_FREE(label_alloc);
		if (owner_alloc)
			D_FREE(owner_alloc);
		if (owner_grp_alloc)
			D_FREE(owner_grp_alloc);
	}
	return rc;
}

int
cont_iv_prop_update(void *ns, uuid_t cont_uuid, daos_prop_t *prop)
{
	struct cont_iv_entry	*iv_entry;
	uint32_t		 entry_size;
	int			 rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry_size = cont_iv_prop_ent_size(DAOS_ACL_MAX_ACE_LEN);
	D_ALLOC(iv_entry, entry_size);
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(iv_entry->cont_uuid, cont_uuid);
	cont_iv_prop_l2g(prop, &iv_entry->iv_prop);

	rc = cont_iv_update(ns, IV_CONT_PROP, cont_uuid, iv_entry,
			    entry_size, CRT_IV_SHORTCUT_TO_ROOT,
			    CRT_IV_SYNC_EAGER, false /* retry */);
	D_FREE(iv_entry);

out:
	return rc;
}

struct iv_prop_ult_arg {
	struct ds_iv_ns		*iv_ns;
	daos_prop_t		*prop;
	uuid_t			 cont_uuid;
	ABT_eventual		 eventual;
};

static void
cont_iv_prop_fetch_ult(void *data)
{
	struct iv_prop_ult_arg	*arg = data;
	struct cont_iv_entry	*iv_entry;
	int			iv_entry_size;
	daos_prop_t		*prop = arg->prop;
	daos_prop_t		*prop_fetch = NULL;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	iv_entry_size = cont_iv_prop_ent_size(DAOS_ACL_MAX_ACE_LEN);
	D_ALLOC(iv_entry, iv_entry_size);
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	prop_fetch = daos_prop_alloc(CONT_PROP_NUM);
	if (prop_fetch == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = cont_iv_fetch(arg->iv_ns, IV_CONT_PROP, arg->cont_uuid,
			   iv_entry, iv_entry_size, false /* retry */);
	if (rc) {
		D_ERROR("cont_iv_fetch failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_ASSERT(prop != NULL);
	rc = cont_iv_prop_g2l(&iv_entry->iv_prop, prop_fetch);
	if (rc) {
		D_ERROR("cont_iv_prop_g2l failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = daos_prop_copy(prop, prop_fetch);
	if (rc) {
		D_ERROR("daos_prop_copy failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	D_FREE(iv_entry);
	daos_prop_free(prop_fetch);
	ABT_eventual_set(arg->eventual, (void *)&rc, sizeof(rc));
}

int
cont_iv_prop_fetch(struct ds_iv_ns *ns, uuid_t cont_uuid,
		   daos_prop_t *cont_prop)
{
	struct iv_prop_ult_arg	arg;
	ABT_eventual		eventual;
	int			*status;
	int			rc;

	if (ns == NULL || cont_prop == NULL || uuid_is_null(cont_uuid))
		return -DER_INVAL;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	arg.iv_ns = ns;
	uuid_copy(arg.cont_uuid, cont_uuid);
	arg.prop = cont_prop;
	arg.eventual = eventual;
	rc = dss_ult_create(cont_iv_prop_fetch_ult, &arg, DSS_XS_SYS,
			    0, DSS_DEEP_STACK_SZ, NULL);
	if (rc)
		D_GOTO(out, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));
	if (*status != 0)
		D_GOTO(out, rc = *status);

out:
	ABT_eventual_free(&eventual);
	return rc;
}

/*
 * exported APIs
 */
int
ds_cont_fetch_snaps(struct ds_iv_ns *ns, uuid_t cont_uuid,
		    uint64_t **snapshots, int *snap_count)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	return cont_iv_snapshots_fetch(ns, cont_uuid, snapshots, snap_count);
}

int
ds_cont_revoke_snaps(struct ds_iv_ns *ns, uuid_t cont_uuid,
		     unsigned int shortcut, unsigned int sync_mode)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	return cont_iv_snapshot_invalidate(ns, cont_uuid, shortcut, sync_mode);
}

int
ds_cont_fetch_prop(struct ds_iv_ns *ns, uuid_t co_uuid,
		   daos_prop_t *cont_prop)
{
	/* NB: it can be called from any xstream */
	return cont_iv_prop_fetch(ns, co_uuid, cont_prop);
}

int
ds_cont_find_hdl(uuid_t po_uuid, uuid_t coh_uuid, struct ds_cont_hdl **coh_p)
{
	/* NB: it can be called from any xstream */
	return cont_iv_hdl_fetch(coh_uuid, po_uuid, coh_p);
}

int
ds_cont_iv_fini(void)
{
	ds_iv_class_unregister(IV_CONT_SNAP);
	ds_iv_class_unregister(IV_CONT_CAPA);
	ds_iv_class_unregister(IV_CONT_PROP);
	ds_iv_class_unregister(IV_CONT_AGG_EPOCH_REPORT);
	ds_iv_class_unregister(IV_CONT_AGG_EPOCH_BOUNDRY);
	return 0;
}

int
ds_cont_iv_init(void)
{
	int rc;

	rc = ds_iv_class_register(IV_CONT_SNAP, &iv_cache_ops, &cont_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_CONT_CAPA, &iv_cache_ops, &cont_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_CONT_PROP, &iv_cache_ops, &cont_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_CONT_AGG_EPOCH_REPORT, &iv_cache_ops,
				  &cont_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_CONT_AGG_EPOCH_BOUNDRY, &iv_cache_ops,
				  &cont_iv_ops);
	if (rc)
		D_GOTO(out, rc);
out:
	if (rc)
		ds_cont_iv_fini();

	return rc;
}

