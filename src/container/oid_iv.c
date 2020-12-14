/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * Container Object ID IV.
 */
#define D_LOGFAC	DD_FAC(container)

#include <abt.h>
#include <daos_errno.h>
#include <cart/iv.h>
#include <daos/common.h>
#include <gurt/list.h>
#include <daos_srv/iv.h>
#include "srv_internal.h"

#define OID_BLOCK 32

struct oid_iv_key {
	/** The Key ID, being the container uuid */
	uuid_t		key_id;
	/** Pool uuid, needed at the root to lookup pool hdl */
	uuid_t		poh_uuid;
	/** Container handle uuid needed at the root to access metadata */
	uuid_t		coh_uuid;
};

/** IV cache entry will be represented by this structure on each node. */
struct oid_iv_entry {
	/** value of the IV entry */
	struct oid_iv_range	rg;
	/** protect the entry */
	ABT_mutex		lock;
};

/** Priv data in the iv layer */
struct oid_iv_priv {
	/** num of oids requested before forwarding the request */
	daos_size_t	num_oids;
};

static struct oid_iv_key *
key2priv(struct ds_iv_key *iv_key)
{
	return (struct oid_iv_key *)iv_key->key_buf;
}

static bool
oid_iv_key_cmp(void *key1, void *key2)
{
	struct oid_iv_key *oid_key1 = key1;
	struct oid_iv_key *oid_key2 = key2;

	if (uuid_compare(oid_key1->key_id, oid_key2->key_id) == 0 &&
	    uuid_compare(oid_key1->poh_uuid, oid_key2->poh_uuid) == 0 &&
	    uuid_compare(oid_key1->coh_uuid, oid_key2->coh_uuid) == 0)
		return true;

	return false;
}

static int
oid_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		 d_sg_list_t *src, d_sg_list_t *dst, void **priv)
{
	D_ASSERT(0);
	return 0;
}

static int
oid_iv_ent_refresh(struct ds_iv_entry *iv_entry, struct ds_iv_key *key,
		   d_sg_list_t *src, int ref_rc, void **_priv)
{
	struct oid_iv_priv	*priv = (struct oid_iv_priv *)_priv;
	daos_size_t		num_oids;
	struct oid_iv_entry	*entry;
	struct oid_iv_range	*oids;
	struct oid_iv_range	*avail;

	D_ASSERT(priv);
	num_oids = priv->num_oids;
	D_DEBUG(DB_TRACE, "%u: ON REFRESH %zu\n", dss_self_rank(), num_oids);
	D_ASSERT(num_oids != 0);

	entry = iv_entry->iv_value.sg_iovs[0].iov_buf;
	D_ASSERT(entry != NULL);

	/** if iv op failed, just release the entry lock acquired in update */
	if (ref_rc != 0)
		goto out;

	avail = &entry->rg;
	oids = src->sg_iovs[0].iov_buf;

	avail->num_oids = oids->num_oids;
	avail->oid = oids->oid;

	/** Update the entry by reserving what was asked for */
	D_ASSERT(avail->num_oids >= num_oids);
	avail->num_oids -= num_oids;
	avail->oid += num_oids;

	/** Set the number of oids to what was asked for. */
	oids->num_oids = num_oids;

out:
	ABT_mutex_unlock(entry->lock);
	return ref_rc;
}

static int
oid_iv_ent_update(struct ds_iv_entry *ns_entry, struct ds_iv_key *iv_key,
		  d_sg_list_t *src, void **_priv)
{
	struct oid_iv_priv	*priv = (struct oid_iv_priv *)_priv;
	struct oid_iv_entry	*entry;
	struct oid_iv_range	*oids;
	struct oid_iv_range	*avail;
	daos_size_t		num_oids;
	d_rank_t		myrank = dss_self_rank();
	int			rc;

	D_ASSERT(priv != NULL);

	entry = ns_entry->iv_value.sg_iovs[0].iov_buf;
	ABT_mutex_lock(entry->lock);
	avail = &entry->rg;

	oids = src->sg_iovs[0].iov_buf;
	num_oids = oids->num_oids;

	D_DEBUG(DB_TRACE, "%u: ON UPDATE, num_oids = %zu\n", myrank, num_oids);
	D_DEBUG(DB_TRACE, "%u: ENTRY NUM OIDS = %zu, oid = %" PRIu64 "\n",
		myrank, avail->num_oids, avail->oid);

	if (ns_entry->ns->iv_master_rank == myrank) {
		struct oid_iv_key *key;

		key = key2priv(&ns_entry->iv_key);
		rc = ds_cont_oid_fetch_add(key->poh_uuid, key->key_id,
					   key->coh_uuid, num_oids,
					   &avail->oid);
		if (rc) {
			D_ERROR("failed to fetch and update max_oid "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(err_lock, rc);
		}
		oids->oid = avail->oid;
		oids->num_oids = num_oids;
		D_DEBUG(DB_TRACE, "%u: ROOT MAX_OID = %"PRIu64"\n", myrank,
			avail->oid);
		priv->num_oids = 0;
		ABT_mutex_unlock(entry->lock);
		return 0;
	}

	if (avail->num_oids >= num_oids) {
		D_DEBUG(DB_TRACE, "%u: IDs available\n", myrank);
		/** set the oid value in the iv value */
		oids->oid = avail->oid;
		oids->num_oids = num_oids;

		/** Update the current entry */
		avail->num_oids -= num_oids;
		avail->oid += num_oids;

		priv->num_oids = 0;
		/** release entry lock */
		ABT_mutex_unlock(entry->lock);

		return 0;
	}

	/** increase the number of oids requested before forwarding */
	if (num_oids < OID_BLOCK)
		oids->num_oids = OID_BLOCK;
	else
		oids->num_oids = (num_oids / OID_BLOCK) * OID_BLOCK * 2;

	/** Keep track of how much this node originally requested */
	priv->num_oids = num_oids;

	D_DEBUG(DB_TRACE, "%u: IDs not available, FORWARD %zu oids\n",
		myrank, oids->num_oids);

	/** entry->lock will be released in on_refresh() */
	return -DER_IVCB_FORWARD;

err_lock:
	ABT_mutex_unlock(entry->lock);
	return rc;
}

static int
oid_iv_ent_get(struct ds_iv_entry *entry, void **_priv)
{
	struct oid_iv_priv	*priv;

	D_DEBUG(DB_TRACE, "%u: OID GET\n", dss_self_rank());

	D_ALLOC_PTR(priv);
	if (priv == NULL)
		return -DER_NOMEM;

	*_priv = priv;
	return 0;
}

static int
oid_iv_ent_put(struct ds_iv_entry *entry, void **_priv)
{
	struct oid_iv_priv *priv = (struct oid_iv_priv *)_priv;

	D_DEBUG(DB_TRACE, "%u: ON PUT\n", dss_self_rank());

	D_FREE(priv);
	_priv = NULL;

	return 0;
}

static int
oid_iv_ent_init(struct ds_iv_key *iv_key, void *data, struct ds_iv_entry *entry)
{
	struct oid_iv_entry	*oid_entry;
	struct oid_iv_key	*key, *ent_key;
	int			rc;

	rc = d_sgl_init(&entry->iv_value, 1);
	if (rc)
		return rc;

	D_ALLOC_PTR(oid_entry);
	if (oid_entry == NULL)
		return -DER_NOMEM;

	/* create the entry mutex */
	rc = ABT_mutex_create(&oid_entry->lock);
	if (rc != ABT_SUCCESS) {
		D_FREE(oid_entry);
		return dss_abterr2der(rc);
	}

	/** init the entry key */
	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;
	key = key2priv(iv_key);
	ent_key = key2priv(&entry->iv_key);
	uuid_copy(ent_key->key_id, key->key_id);
	uuid_copy(ent_key->poh_uuid, key->poh_uuid);
	uuid_copy(ent_key->coh_uuid, key->coh_uuid);

	entry->iv_value.sg_iovs[0].iov_buf = oid_entry;
	entry->iv_value.sg_iovs[0].iov_buf_len = sizeof(struct oid_iv_entry);
	entry->iv_value.sg_iovs[0].iov_len = sizeof(struct oid_iv_entry);

	return rc;
}

static int
oid_iv_ent_destroy(d_sg_list_t *sgl)
{
	struct oid_iv_entry *entry;

	entry = sgl->sg_iovs[0].iov_buf;
	ABT_mutex_free(&entry->lock);
	d_sgl_fini(sgl, true);

	return 0;
}

static int
oid_iv_alloc(struct ds_iv_entry *entry, d_sg_list_t *sgl)
{
	int rc;

	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, sizeof(struct oid_iv_range));
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	sgl->sg_iovs[0].iov_buf_len = sizeof(struct oid_iv_range);
	sgl->sg_iovs[0].iov_len = sizeof(struct oid_iv_range);

free:
	if (rc)
		d_sgl_fini(sgl, true);
	return rc;
}

struct ds_iv_class_ops oid_iv_ops = {
	.ivc_key_cmp		= oid_iv_key_cmp,
	.ivc_ent_init		= oid_iv_ent_init,
	.ivc_ent_get		= oid_iv_ent_get,
	.ivc_ent_put		= oid_iv_ent_put,
	.ivc_ent_destroy	= oid_iv_ent_destroy,
	.ivc_ent_fetch		= oid_iv_ent_fetch,
	.ivc_ent_update		= oid_iv_ent_update,
	.ivc_ent_refresh	= oid_iv_ent_refresh,
	.ivc_value_alloc	= oid_iv_alloc,
};

int
oid_iv_reserve(void *ns, uuid_t poh_uuid, uuid_t co_uuid,
	       uuid_t coh_uuid, uint64_t num_oids, d_sg_list_t *value)
{
	struct oid_iv_key	*oid_key;
	struct ds_iv_key        key;
	struct oid_iv_range	*oids;
	int		rc;

	D_DEBUG(DB_TRACE, "%d: OID alloc CUUID "DF_UUIDF" num_oids %"PRIu64"\n",
		dss_self_rank(), DP_UUID(co_uuid), num_oids);

	memset(&key, 0, sizeof(key));
	key.class_id = IV_OID;

	oid_key = (struct oid_iv_key *)key.key_buf;
	uuid_copy(oid_key->key_id, co_uuid);
	uuid_copy(oid_key->poh_uuid, poh_uuid);
	uuid_copy(oid_key->coh_uuid, coh_uuid);

	oids = value->sg_iovs[0].iov_buf;
	oids->num_oids = num_oids;

	rc = ds_iv_update(ns, &key, value, 0, CRT_IV_SYNC_NONE,
			  CRT_IV_SYNC_BIDIRECTIONAL, false /* retry */);
	if (rc)
		D_ERROR("iv update failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
ds_oid_iv_init(void)
{
	return ds_iv_class_register(IV_OID, &iv_cache_ops, &oid_iv_ops);
}

int
ds_oid_iv_fini(void)
{
	return ds_iv_class_unregister(IV_OID);
}
