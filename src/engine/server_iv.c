/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS server. It implements the server .
 */
#define D_LOGFAC	DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <gurt/list.h>
#include <cart/iv.h>
#include <daos_srv/iv.h>
#include <daos_prop.h>
#include "srv_internal.h"

static d_list_t			 ds_iv_ns_list;
static int			 ds_iv_ns_id = 1;
static int			 ds_iv_ns_tree_topo;
static d_list_t			 ds_iv_class_list;
static int			 ds_iv_class_nr;
static int			 crt_iv_class_nr;
static struct crt_iv_class	*crt_iv_class;

struct ds_iv_class *
iv_class_lookup(unsigned int class_id)
{
	struct ds_iv_class *found = NULL;
	struct ds_iv_class *class;

	d_list_for_each_entry(class, &ds_iv_class_list, iv_class_list) {
		if (class->iv_class_id == class_id) {
			found = class;
			break;
		}
	}
	return found;
}

int
ds_iv_class_register(unsigned int class_id, struct crt_iv_ops *crt_ops,
		     struct ds_iv_class_ops *class_ops)
{
	struct ds_iv_class	*class;
	bool			found = false;
	int			crt_iv_class_id = -1;
	int			i;

	class = iv_class_lookup(class_id);
	if (class)
		return -DER_EXIST;

	for (i = 0; i < crt_iv_class_nr; i++) {
		if (crt_iv_class[i].ivc_ops == crt_ops) {
			found = true;
			crt_iv_class_id = i;
			break;
		}
	}

	/* Update crt_iv_class */
	if (!found) {
		struct crt_iv_class *new_iv_class;

		D_ALLOC(new_iv_class, (crt_iv_class_nr + 1) *
				       sizeof(*new_iv_class));
		if (new_iv_class == NULL)
			return -DER_NOMEM;

		if (crt_iv_class_nr > 0)
			memcpy(new_iv_class, crt_iv_class,
			       crt_iv_class_nr * sizeof(*new_iv_class));

		new_iv_class[crt_iv_class_nr].ivc_id = 0;
		new_iv_class[crt_iv_class_nr].ivc_feats = 0;
		new_iv_class[crt_iv_class_nr].ivc_ops = crt_ops;
		if (crt_iv_class_nr > 0)
			D_FREE(crt_iv_class);
		crt_iv_class_nr++;
		crt_iv_class = new_iv_class;
		crt_iv_class_id = crt_iv_class_nr - 1;
	}

	D_ALLOC_PTR(class);
	if (class == NULL)
		return -DER_NOMEM;

	class->iv_class_crt_cbs = crt_ops;
	class->iv_class_id = class_id;
	class->iv_cart_class_id = crt_iv_class_id;
	class->iv_class_ops = class_ops;
	d_list_add(&class->iv_class_list, &ds_iv_class_list);
	ds_iv_class_nr++;
	D_DEBUG(DB_TRACE, "register %d/%d,", class->iv_class_id,
		class->iv_cart_class_id);
	return 0;
}

int
ds_iv_class_unregister(unsigned int class_id)
{
	struct ds_iv_class *class;

	d_list_for_each_entry(class, &ds_iv_class_list, iv_class_list) {
		if (class->iv_class_id == class_id) {
			d_list_del(&class->iv_class_list);
			D_FREE(class);
			return 0;
		}
	}

	D_DEBUG(DB_TRACE, "can not find the key %d\n", class_id);
	return 0;
}

/* Serialize iv_key so it can be put into RPC by IV */
int
iv_key_pack(crt_iv_key_t *key_iov, struct ds_iv_key *key_iv)
{
	struct ds_iv_class	*class;
	int			rc = 0;

	/* packing the key */
	class = iv_class_lookup(key_iv->class_id);
	D_ASSERT(class != NULL);

	if (class->iv_class_ops->ivc_key_pack) {
		rc = class->iv_class_ops->ivc_key_pack(class, key_iv, key_iov);
	} else {
		key_iov->iov_buf = key_iv;
		key_iov->iov_len = sizeof(*key_iv);
		key_iov->iov_buf_len = sizeof(*key_iv);
	}

	return rc;
}

/* Unserialize iv_key so it can be used in callback */
int
iv_key_unpack(struct ds_iv_key *key_iv, crt_iv_key_t *key_iov)
{
	struct ds_iv_class	*class;
	struct ds_iv_key	*tmp_key = key_iov->iov_buf;
	int			rc = 0;

	/* Note: key_id is integer, and always in the 1st place of
	 * ds_iv_key, so it is safe to use before unpack
	 */
	class = iv_class_lookup(tmp_key->class_id);
	D_ASSERTF(class != NULL, "class_id/rank %d/%u\n", tmp_key->class_id,
		  tmp_key->rank);

	if (class->iv_class_ops->ivc_key_unpack)
		rc = class->iv_class_ops->ivc_key_unpack(class, key_iov,
							 key_iv);
	else
		memcpy(key_iv, key_iov->iov_buf, sizeof(*key_iv));

	D_DEBUG(DB_TRACE, "unpack %d\n", key_iv->class_id);
	return rc;
}

static void
ds_iv_ns_get(struct ds_iv_ns *ns)
{
	ns->iv_refcount++;
	D_DEBUG(DB_TRACE, DF_UUID" ns ref %u\n",
		DP_UUID(ns->iv_pool_uuid), ns->iv_refcount);
}

static void
ds_iv_ns_destroy(void *ns);

void
ds_iv_ns_put(struct ds_iv_ns *ns)
{
	D_ASSERT(ns->iv_refcount > 0);
	ns->iv_refcount--;
	D_DEBUG(DB_TRACE, DF_UUID" ns ref %u\n",
		DP_UUID(ns->iv_pool_uuid), ns->iv_refcount);
	if (ns->iv_refcount == 1)
		ABT_eventual_set(ns->iv_done_eventual, NULL, 0);
	else if (ns->iv_refcount == 0)
		ds_iv_ns_destroy(ns);
}

static int
iv_ns_lookup_by_ivns(crt_iv_namespace_t ivns, struct ds_iv_ns **p_ns)
{
	struct ds_iv_ns *ns;

	*p_ns = NULL;
	d_list_for_each_entry(ns, &ds_iv_ns_list, iv_ns_link) {
		if (ns->iv_ns != ivns)
			continue;

		if (ns->iv_stop) {
			D_DEBUG(DB_MD, DF_UUID" stopping\n",
				DP_UUID(ns->iv_pool_uuid));
			*p_ns = ns;
			return -DER_SHUTDOWN;
		}
		ds_iv_ns_get(ns);
		*p_ns = ns;
		return 0;
	}
	return -DER_NONEXIST;
}

static bool
key_equal(struct ds_iv_entry *entry, struct ds_iv_key *key1,
	  struct ds_iv_key *key2)
{
	struct ds_iv_class *class = entry->iv_class;

	if (key1->class_id != key2->class_id)
		return false;

	if (class->iv_class_ops == NULL ||
	    class->iv_class_ops->ivc_key_cmp == NULL)
		return true;

	return class->iv_class_ops->ivc_key_cmp(&key1->key_buf,
						&key2->key_buf) == true;
}

static struct ds_iv_entry *
iv_class_entry_lookup(struct ds_iv_ns *ns, struct ds_iv_key *key)
{
	struct dss_module_info *dmi = dss_get_module_info();
	struct ds_iv_entry *found = NULL;
	struct ds_iv_entry *entry;

	D_ASSERT(dmi->dmi_xs_id == 0);
	d_list_for_each_entry(entry, &ns->iv_entry_list, iv_link) {
		if (key_equal(entry, key, &entry->iv_key)) {
			/* resolve the permission issue later and also
			 * hold the value XXX
			 */
			found = entry;
			break;
		}
	}

	return found;
}

static void
iv_entry_free(struct ds_iv_entry *entry)
{
	if (entry == NULL)
		return;

	if (entry->iv_value.sg_iovs != NULL) {
		struct ds_iv_class *class = entry->iv_class;

		if (class && class->iv_class_ops &&
		    class->iv_class_ops->ivc_ent_destroy)
			class->iv_class_ops->ivc_ent_destroy(&entry->iv_value);
		else
			d_sgl_fini(&entry->iv_value, true);
	}

	D_FREE(entry);
}

static int
update_iv_value(struct ds_iv_entry *entry, struct ds_iv_key *key,
		d_sg_list_t *src, void **priv)
{
	struct ds_iv_class	*class = entry->iv_class;
	int			rc;

	if (class->iv_class_ops && class->iv_class_ops->ivc_ent_update)
		rc = class->iv_class_ops->ivc_ent_update(entry, key, src, priv);
	else
		rc = daos_sgl_copy_data(&entry->iv_value, src);
	return rc;
}

static int
refresh_iv_value(struct ds_iv_entry *entry, struct ds_iv_key *key,
		 d_sg_list_t *src, int ref_rc, void *priv)
{
	struct ds_iv_class	*class = entry->iv_class;
	int			rc = 0;

	if (class->iv_class_ops && class->iv_class_ops->ivc_ent_refresh)
		rc = class->iv_class_ops->ivc_ent_refresh(entry, key, src,
							  ref_rc, priv);
	else if (src != NULL)
		rc = daos_sgl_copy_data(&entry->iv_value, src);
	return rc;
}

static int
iv_entry_alloc(struct ds_iv_ns *ns, struct ds_iv_class *class,
	       struct ds_iv_key *key, void *data, struct ds_iv_entry **entryp)
{
	struct ds_iv_entry *entry;
	int			 rc;

	D_ALLOC_PTR(entry);
	if (entry == NULL)
		return -DER_NOMEM;

	rc = class->iv_class_ops->ivc_ent_init(key, data, entry);
	if (rc)
		D_GOTO(free, rc);

	entry->ns = ns;
	entry->iv_valid = false;
	entry->iv_class = class;
	entry->iv_ref = 1;
	*entryp = entry;
free:
	if (rc)
		iv_entry_free(entry);

	return rc;
}

static int
iv_entry_lookup_or_create(struct ds_iv_ns *ns, struct ds_iv_key *key,
			  struct ds_iv_entry **got)
{
	struct ds_iv_entry	*entry;
	struct ds_iv_class	*class;
	int			rc;

	entry = iv_class_entry_lookup(ns, key);
	if (entry != NULL) {
		entry->iv_ref++;
		if (got != NULL)
			*got = entry;
		D_DEBUG(DB_TRACE, "Get entry %p/%d key %d\n",
			entry, entry->iv_ref, key->class_id);
		return 0;
	}

	class = iv_class_lookup(key->class_id);
	if (class == NULL) {
		D_ERROR("Can not find class %d\n", key->class_id);
		return -DER_NONEXIST;
	}

	/* Allocate the entry */
	rc = iv_entry_alloc(ns, class, key, NULL, &entry);
	if (rc)
		return rc;

	entry->iv_ref++;
	d_list_add(&entry->iv_link, &ns->iv_entry_list);
	*got = entry;

	return 1;
}

struct iv_priv_entry {
	struct ds_iv_entry	*entry;
	void			*priv;
};

static bool
iv_entry_valid(struct ds_iv_entry *entry, struct ds_iv_key *key)
{
	if (!entry->iv_valid)
		return false;

	if (entry->iv_class->iv_class_ops->ivc_ent_valid)
		return entry->iv_class->iv_class_ops->ivc_ent_valid(entry, key);

	return true;
}

static int
ivc_on_fetch(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	     crt_iv_ver_t *iv_ver, uint32_t flags,
	     d_sg_list_t *iv_value, void *priv)
{
	struct iv_priv_entry	*priv_entry = priv;
	struct ds_iv_ns		*ns = NULL;
	struct ds_iv_entry	*entry;
	struct ds_iv_key	key;
	bool			valid;
	int			 rc;

	D_ASSERT(iv_value != NULL);
	rc = iv_ns_lookup_by_ivns(ivns, &ns);
	if (rc != 0)
		return rc;
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	if (priv_entry == NULL) {
		/* find and prepare entry */
		rc = iv_entry_lookup_or_create(ns, &key, &entry);
		if (rc < 0)
			D_GOTO(output, rc);
	} else {
		D_ASSERT(priv_entry->entry != NULL);
		entry = priv_entry->entry;
	}

	valid = iv_entry_valid(entry, &key);
	D_DEBUG(DB_MD, "FETCH: Key [%d:%d] entry %p valid %s\n", key.rank,
		key.class_id, entry, valid ? "yes" : "no");

	/* Forward the request to its parent if it is not root, and
	 * let's caller decide how to deal with leader.
	 */
	if (!valid) {
		/* If the rank inside the iv_fetch request(key) does not
		 * match the current ns information, then it means the new
		 * leader just steps up.  Let's return -DER_NOTLEADER in this
		 * case, so IV fetch can keep retry, until the IV information
		 * is updated on all nodes.
		 */
		if ((key.rank == dss_self_rank() &&
		     key.rank != ns->iv_master_rank))
			D_GOTO(output, rc = -DER_NOTLEADER);
		else if (ns->iv_master_rank != dss_self_rank())
			D_GOTO(output, rc = -DER_IVCB_FORWARD);
	}

	if (entry->iv_class->iv_class_ops &&
	    entry->iv_class->iv_class_ops->ivc_ent_fetch)
		rc = entry->iv_class->iv_class_ops->ivc_ent_fetch(entry, &key, iv_value, priv);
	else
		rc = daos_sgl_copy_data(iv_value, &entry->iv_value);

output:
	if (flags & CRT_IV_FLAG_PENDING_FETCH && rc == -DER_IVCB_FORWARD) {
		/* For pending fetch request, let's reset to DER_NOTLEADER for retry */
		D_DEBUG(DB_MD, "[%d:%d] reset NOTLEADER to retry.\n", key.rank, key.class_id);
		rc = -DER_NOTLEADER;
	}

	ds_iv_ns_put(ns);
	return rc;
}

static int
iv_on_update_internal(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
		      crt_iv_ver_t iv_ver, d_sg_list_t *iv_value,
		      bool invalidate, bool refresh, int ref_rc, void *priv)
{
	struct ds_iv_ns		*ns = NULL;
	struct ds_iv_entry	*entry;
	struct ds_iv_key	key;
	struct iv_priv_entry	*priv_entry = priv;
	int			rc = 0;

	rc = iv_ns_lookup_by_ivns(ivns, &ns);
	if (rc != 0)
		return rc;
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	if (priv_entry == NULL || priv_entry->entry == NULL) {
		/* find and prepare entry */
		rc = iv_entry_lookup_or_create(ns, &key, &entry);
		if (rc < 0)
			D_GOTO(output, rc);
	} else {
		entry = priv_entry->entry;
	}

	if (refresh) {
		rc = refresh_iv_value(entry, &key, iv_value, ref_rc,
				      priv_entry ? priv_entry->priv : NULL);
	} else {
		D_ASSERT(iv_value != NULL);
		if (ns->iv_master_rank != key.rank) {
			D_DEBUG(DB_MD, "key id %d master rank %u != %u: rc = %d\n",
				key.class_id, ns->iv_master_rank, key.rank, -DER_GRPVER);
			D_GOTO(output, rc = -DER_GRPVER);
		}
		rc = update_iv_value(entry, &key, iv_value,
				     priv_entry ? priv_entry->priv : NULL);
	}
	if (rc != 0) {
		D_DEBUG(DB_MD, "key id %d update failed: rc = %d\n",
			key.class_id, rc);
		D_GOTO(output, rc);
	}

	/* If the entry is being invalidate, then iv_valid is set inside the callback */
	if (!invalidate)
		entry->iv_valid = true;

	D_DEBUG(DB_MD, "key id %d rank %d myrank %d valid %s\n",
		key.class_id, key.rank, dss_self_rank(),
		invalidate ? "no" : "yes");
output:
	/* invalidate entry might require to delete entry after refresh */
	if (entry && entry->iv_to_delete)
		entry->iv_ref--; /* destroy in ivc_on_put */

	ds_iv_ns_put(ns);
	return rc;
}

/*  update callback will be called when syncing root to leaf */
static int
ivc_on_refresh(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	       crt_iv_ver_t iv_ver, d_sg_list_t *iv_value,
	       bool invalidate, int refresh_rc, void *priv)
{
	return iv_on_update_internal(ivns, iv_key, iv_ver, iv_value, invalidate,
				     true, refresh_rc, priv);
}

/*  update callback will be called when updating leaf to root */
static int
ivc_on_update(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	      crt_iv_ver_t iv_ver, uint32_t flags, d_sg_list_t *iv_value,
	      void *priv)
{
	return iv_on_update_internal(ivns, iv_key, iv_ver, iv_value, false,
				     false, 0, priv);
}

static void
ivc_pre_cb(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	   crt_generic_cb_t cb_func, void *cb_arg)
{
	int rc;

	/*
	 * Current EC aggregation periodically update IV, call
	 * dss_ult_periodic() to avoid interfering CPU relaxing.
	 */
	rc = dss_ult_periodic(cb_func, cb_arg, DSS_XS_SELF, 0, 0, NULL);
	if (rc)
		D_ERROR("dss_ult_periodic failed, rc "DF_RC"\n", DP_RC(rc));
}

static int
ivc_on_hash(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key, d_rank_t *root)
{
	struct ds_iv_ns		*ns = NULL;
	struct ds_iv_key	key;
	int			rc;

	iv_key_unpack(&key, iv_key);
	if (key.rank == ((d_rank_t)-1)) {
		D_INFO("Uninitialize master rank\n");
		return -DER_NOTLEADER;
	}

	/* Check if it matches with current NS master */
	rc = iv_ns_lookup_by_ivns(ivns, &ns);
	if (rc != 0)
		return rc;

	if (key.rank != ns->iv_master_rank) {
		D_INFO("ns %u key rank %d ns iv master rank %d\n",
		       ns->iv_ns_id, key.rank, ns->iv_master_rank);
		D_GOTO(out_put, rc = -DER_NOTLEADER);
	}

	*root = key.rank;
out_put:
	ds_iv_ns_put(ns);
	return rc;
}

static int
ivc_on_get(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	   crt_iv_ver_t iv_ver, crt_iv_perm_t permission,
	   d_sg_list_t *iv_value, void **priv)
{
	struct ds_iv_ns		*ns = NULL;
	struct ds_iv_entry	*entry;
	struct ds_iv_class	*class;
	struct ds_iv_key	key;
	struct iv_priv_entry	*priv_entry;
	void			*entry_priv_val;
	bool			alloc_entry = false;
	int			rc;

	rc = iv_ns_lookup_by_ivns(ivns, &ns);
	if (rc != 0)
		return rc;
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	/* find and prepare entry */
	rc = iv_entry_lookup_or_create(ns, &key, &entry);
	if (rc < 0)
		D_GOTO(out, rc);

	if (rc > 0)
		alloc_entry = true;

	class = entry->iv_class;
	if (iv_value) {
		rc = class->iv_class_ops->ivc_value_alloc(entry, &key, iv_value);
		if (rc)
			D_GOTO(out, rc);
	}

	rc = class->iv_class_ops->ivc_ent_get(entry, &entry_priv_val);
	if (rc)
		D_GOTO(out, rc);

	/* A failure here appears to leak the memory from ivc_value_alloc() above for pools */
	D_ALLOC_PTR(priv_entry);
	if (priv_entry == NULL) {
		class->iv_class_ops->ivc_ent_put(entry, entry_priv_val);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	priv_entry->priv = entry_priv_val;
	priv_entry->entry = entry;
	*priv = priv_entry;

out:
	if (rc) {
		if (alloc_entry) {
			d_list_del(&entry->iv_link);
			iv_entry_free(entry);
		}
		ds_iv_ns_put(ns);
	}

	return rc;
}

static void
ivc_on_put(crt_iv_namespace_t ivns, d_sg_list_t *iv_value, void *priv)
{
	struct ds_iv_ns		*ns = NULL;
	struct iv_priv_entry	*priv_entry = priv;
	struct ds_iv_entry	*entry;
	int			 rc;

	rc = iv_ns_lookup_by_ivns(ivns, &ns);
	if (rc != 0) {
		if (ns != NULL)
			ds_iv_ns_put(ns); /* balance ivc_on_get */
		return;
	}
	D_ASSERT(ns != NULL);

	D_ASSERT(priv_entry != NULL);

	entry = priv_entry->entry;
	D_ASSERT(entry != NULL);

	D_DEBUG(DB_TRACE, "Put entry %p/%d priv %p/%p\n", entry, entry->iv_ref - 1,
		priv_entry, priv_entry->priv);
	/* Let's deal with iv_value first */
	d_sgl_fini(iv_value, true);

	entry->iv_class->iv_class_ops->ivc_ent_put(entry, priv_entry->priv);

	D_FREE(priv_entry);
	if (--entry->iv_ref > 0)
		D_GOTO(put, rc);

	d_list_del(&entry->iv_link);
	iv_entry_free(entry);

put:
	/* one for lookup, the other one for balanced the get */
	ds_iv_ns_put(ns);
	ds_iv_ns_put(ns);

	return;
}

static int
ivc_pre_sync(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key, crt_iv_ver_t iv_ver,
	     d_sg_list_t *iv_value, void *arg)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry;
	struct ds_iv_key	key;
	struct iv_priv_entry	*priv_entry = arg;
	struct ds_iv_class	*class;
	int			rc = 0;

	rc = iv_ns_lookup_by_ivns(ivns, &ns);
	if (rc != 0)
		return rc;
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	if (priv_entry == NULL || priv_entry->entry == NULL) {
		/* find and prepare entry */
		rc = iv_entry_lookup_or_create(ns, &key, &entry);
		if (rc < 0)
			D_GOTO(output, rc);
	} else {
		entry = priv_entry->entry;
	}

	class = entry->iv_class;
	if (class->iv_class_ops && class->iv_class_ops->ivc_pre_sync)
		rc = class->iv_class_ops->ivc_pre_sync(entry, &key, iv_value);
output:
	ds_iv_ns_put(ns);
	return rc;
}

struct crt_iv_ops iv_cache_ops = {
	.ivo_pre_fetch		= ivc_pre_cb,
	.ivo_on_fetch		= ivc_on_fetch,
	.ivo_pre_update		= ivc_pre_cb,
	.ivo_on_update		= ivc_on_update,
	.ivo_pre_refresh	= ivc_pre_cb,
	.ivo_on_refresh		= ivc_on_refresh,
	.ivo_on_hash		= ivc_on_hash,
	.ivo_on_get		= ivc_on_get,
	.ivo_on_put		= ivc_on_put,
	.ivo_pre_sync		= ivc_pre_sync
};

static void
iv_ns_destroy_cb(crt_iv_namespace_t iv_ns, void *arg)
{
	struct ds_iv_ns		*ns = arg;

	D_ASSERT(d_list_empty(&ns->iv_entry_list));
	d_list_del(&ns->iv_ns_link);
	ABT_eventual_free(&ns->iv_done_eventual);
	D_FREE(ns);
}

static void
iv_ns_destroy_internal(struct ds_iv_ns *ns)
{
	if (ns->iv_ns)
		crt_iv_namespace_destroy(ns->iv_ns, iv_ns_destroy_cb, ns);
}

static struct ds_iv_ns *
ds_iv_ns_lookup(unsigned int ns_id)
{
	struct ds_iv_ns *ns;

	d_list_for_each_entry(ns, &ds_iv_ns_list, iv_ns_link) {
		if (ns->iv_ns_id == ns_id)
			return ns;
	}

	return NULL;
}

static int
iv_ns_create_internal(unsigned int ns_id, uuid_t pool_uuid,
		      d_rank_t master_rank, struct ds_iv_ns **pns)
{
	struct ds_iv_ns	*ns;
	int rc;

	ns = ds_iv_ns_lookup(ns_id);
	if (ns)
		return -DER_EXIST;

	D_ALLOC_PTR(ns);
	if (ns == NULL)
		return -DER_NOMEM;

	uuid_copy(ns->iv_pool_uuid, pool_uuid);
	D_INIT_LIST_HEAD(&ns->iv_entry_list);
	ns->iv_ns_id = ns_id;
	ns->iv_master_rank = master_rank;
	rc = ABT_eventual_create(0, &ns->iv_done_eventual);
	if (rc != ABT_SUCCESS) {
		D_FREE(ns);
		return dss_abterr2der(rc);
	}

	d_list_add(&ns->iv_ns_link, &ds_iv_ns_list);
	ns->iv_refcount = 1;
	*pns = ns;

	return 0;
}

/* Destroy iv ns. */
static void
ds_iv_ns_destroy(void *ns)
{
	struct ds_iv_ns *iv_ns = ns;

	if (iv_ns == NULL)
		return;

	D_DEBUG(DB_MGMT, "destroy ivns %d\n", iv_ns->iv_ns_id);
	iv_ns_destroy_internal(iv_ns);
}

/** Create namespace for server IV. */
int
ds_iv_ns_create(crt_context_t ctx, uuid_t pool_uuid,
		crt_group_t *grp, unsigned int *ns_id,
		struct ds_iv_ns **p_iv_ns)
{
	struct ds_iv_ns		*ns = NULL;
	int			rc;

	rc = iv_ns_create_internal(ds_iv_ns_id++, pool_uuid,
				   -1 /* master_rank */, &ns);
	if (rc)
		return rc;

	rc = crt_iv_namespace_create(ctx, grp, ds_iv_ns_tree_topo, crt_iv_class,
				     crt_iv_class_nr, 0, &ns->iv_ns);
	if (rc)
		D_GOTO(free, rc);

	*p_iv_ns = ns;
	*ns_id = ns->iv_ns_id;
free:
	if (rc)
		ds_iv_ns_destroy(ns);

	return rc;
}

/* Update iv namespace */
void
ds_iv_ns_update(struct ds_iv_ns *ns, unsigned int master_rank, uint64_t term)
{
	if (term <= ns->iv_master_term)
		return;

	D_INFO("update iv_ns %u master rank %u->%u term "DF_U64"->"DF_U64
	       " myrank %u ns %p\n", ns->iv_ns_id, ns->iv_master_rank,
	       master_rank,  ns->iv_master_term, term, dss_self_rank(), ns);
	ns->iv_master_rank = master_rank;
	ns->iv_master_term = term;
}

void
ds_iv_ns_start(struct ds_iv_ns *ns)
{
	ds_iv_ns_get(ns);
}

void
ds_iv_ns_leader_stop(struct ds_iv_ns *ns)
{
	/* Set iv_stop on the leader, so all arriving IV requests will return
	 * failure after this.
	 */
	ns->iv_stop = 1;
}

void
ds_iv_ns_stop(struct ds_iv_ns *ns)
{
	struct ds_iv_entry *entry;
	struct ds_iv_entry *tmp;

	ns->iv_stop = 1;
	ds_iv_ns_put(ns);
	if (ns->iv_refcount > 1) {
		int rc;

		D_DEBUG(DB_MGMT, DF_UUID" ns stop wait ref %u\n",
			DP_UUID(ns->iv_pool_uuid), ns->iv_refcount);
		rc = ABT_eventual_wait(ns->iv_done_eventual, NULL);
		D_ASSERT(rc == ABT_SUCCESS);
		D_DEBUG(DB_MGMT, DF_UUID" ns stopped\n",
			DP_UUID(ns->iv_pool_uuid));
	}

	d_list_for_each_entry_safe(entry, tmp, &ns->iv_entry_list, iv_link) {
		d_list_del(&entry->iv_link);
		iv_entry_free(entry);
	}

	D_INFO(DF_UUID" ns stopped\n", DP_UUID(ns->iv_pool_uuid));
}

unsigned int
ds_iv_ns_id_get(void *ns)
{
	return ((struct ds_iv_ns *)ns)->iv_ns_id;
}

void
ds_iv_init()
{
	D_INIT_LIST_HEAD(&ds_iv_ns_list);
	D_INIT_LIST_HEAD(&ds_iv_class_list);
	ds_iv_ns_tree_topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
}

void
ds_iv_fini(void)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_ns		*tmp;
	struct ds_iv_class	*class;
	struct ds_iv_class	*class_tmp;

	d_list_for_each_entry_safe(ns, tmp, &ds_iv_ns_list, iv_ns_link) {
		d_list_del_init(&ns->iv_ns_link);
		iv_ns_destroy_internal(ns);
	}

	d_list_for_each_entry_safe(class, class_tmp, &ds_iv_class_list,
				   iv_class_list) {
		d_list_del(&class->iv_class_list);
		D_FREE(class);
	}

	if (crt_iv_class_nr > 0)
		D_FREE(crt_iv_class);
}

enum opc {
	IV_FETCH	= 1,
	IV_UPDATE,
	IV_INVALIDATE,
};

struct iv_cb_info {
	ABT_future	future;
	struct ds_iv_ns  *ns;
	struct ds_iv_key *key;
	d_sg_list_t	*value;
	unsigned int	opc;
	int		result;
	crt_iv_sync_t	sync;
};

static int
ds_iv_done(crt_iv_namespace_t ivns, uint32_t class_id,
	   crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	   d_sg_list_t *iv_value, int rc, void *cb_arg)
{
	struct iv_cb_info	*cb_info = cb_arg;
	int			ret = 0;

	cb_info->result = rc;

	if (cb_info->opc == IV_FETCH && cb_info->value && rc == 0) {
		struct ds_iv_key key;

		D_ASSERT(cb_info->ns != NULL);
		iv_key_unpack(&key, iv_key);
		if (iv_value->sg_iovs[0].iov_len > 0 &&
		    cb_info->value->sg_iovs[0].iov_buf_len >=
		    iv_value->sg_iovs[0].iov_len)
			rc = daos_sgl_copy_data(cb_info->value, iv_value);
		else
			D_DEBUG(DB_MD, "key %d/%d does not"
				" provide enough buf "DF_U64" < "
				DF_U64"\n", key.class_id, key.rank,
				cb_info->value->sg_iovs[0].iov_buf_len,
				iv_value->sg_iovs[0].iov_len);
	}

	ABT_future_set(cb_info->future, &rc);
	return ret;
}

static int
iv_op_internal(struct ds_iv_ns *ns, struct ds_iv_key *key_iv,
	       d_sg_list_t *value, crt_iv_sync_t *sync,
	       unsigned int shortcut, int opc)
{
	struct iv_cb_info	cb_info;
	ABT_future		future;
	crt_iv_key_t		key_iov;
	struct ds_iv_class	*class;
	int			rc;

	rc = ABT_future_create(1, NULL, &future);
	if (rc)
		return rc;

	key_iv->rank = ns->iv_master_rank;
	class = iv_class_lookup(key_iv->class_id);
	D_ASSERT(class != NULL);
	D_DEBUG(DB_MD, "class_id %d master %d crt class id %d opc %d\n",
		key_iv->class_id, key_iv->rank, class->iv_cart_class_id, opc);

	iv_key_pack(&key_iov, key_iv);
	memset(&cb_info, 0, sizeof(cb_info));
	cb_info.future = future;
	cb_info.key = key_iv;
	cb_info.value = value;
	cb_info.opc = opc;
	cb_info.ns = ns;
	ds_iv_ns_get(ns);
	switch (opc) {
	case IV_FETCH:
		rc = crt_iv_fetch(ns->iv_ns, class->iv_cart_class_id,
				  (crt_iv_key_t *)&key_iov, 0,
				  0, ds_iv_done, &cb_info);
		break;
	case IV_UPDATE:
		D_ASSERT(sync != NULL);
		rc = crt_iv_update(ns->iv_ns, class->iv_cart_class_id,
				   (crt_iv_key_t *)&key_iov, 0,
				   (d_sg_list_t *)value, shortcut,
				   *sync, ds_iv_done, &cb_info);
		break;
	case IV_INVALIDATE:
		D_ASSERT(sync != NULL);
		rc = crt_iv_invalidate(ns->iv_ns, class->iv_cart_class_id,
				       (crt_iv_key_t *)&key_iov, 0, 0, *sync,
				       ds_iv_done, &cb_info);
		break;
	default:
		D_ASSERT(0);
	}

	if (rc)
		D_GOTO(out, rc);

	ABT_future_wait(future);
	rc = cb_info.result;
	D_DEBUG(DB_MD, "class_id %d opc %d rc %d\n", key_iv->class_id, opc, rc);
out:
	ds_iv_ns_put(ns);
	ABT_future_free(&future);
	return rc;
}

struct iv_op_ult_arg {
	d_sg_list_t	iv_value;
	struct ds_iv_key iv_key;
	struct ds_iv_ns	*ns;
	unsigned int	shortcut;
	crt_iv_sync_t	iv_sync;
	int		opc;
	bool		retry;
};

static int
_iv_op(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value,
       crt_iv_sync_t *sync, unsigned int shortcut, bool retry, int opc)
{
	int rc;

	if (ns->iv_stop)
		return -DER_SHUTDOWN;
retry:
	rc = iv_op_internal(ns, key, value, sync, shortcut, opc);
	if (retry && !ns->iv_stop &&
	    (daos_rpc_retryable_rc(rc) || rc == -DER_NOTLEADER)) {
		if (rc == -DER_NOTLEADER && key->rank != (d_rank_t)(-1) &&
		    sync && (sync->ivs_mode == CRT_IV_SYNC_LAZY ||
			     sync->ivs_mode == CRT_IV_SYNC_EAGER)) {
			/* If leader has been changed, it does not need to
			 * retry at all, because IV sync always start from
			 * the leader.
			 */
			D_WARN("sync (class %d) leader changed\n", key->class_id);
			return rc;
		}

		/* otherwise retry and wait for others to update the ns. */
		/* IV fetch might return IVCB_FORWARD if the IV fetch forward RPC is queued,
		 * but in-flight fetch request return IVCB_FORWARD, then queued RPC will
		 * reply IVCB_FORWARD.
		 */
		D_WARN("ns %u retry for class %d opc %d rank %u/%u: " DF_RC "\n", ns->iv_ns_id,
		       key->class_id, opc, key->rank, ns->iv_master_rank, DP_RC(rc));
		/* sleep 1sec and retry */
		dss_sleep(1000);
		goto retry;
	}

	return rc;
}

static void
iv_op_ult(void *arg)
{
	struct iv_op_ult_arg *ult_arg = arg;

	D_ASSERT(ult_arg->iv_sync.ivs_mode == CRT_IV_SYNC_LAZY);
	/* Since it will put LAZY sync in a separate and asynchronous ULT, so
	 * let's use EAGER mode in CRT to make it simipler.
	 */
	ult_arg->iv_sync.ivs_mode = CRT_IV_SYNC_EAGER;
	_iv_op(ult_arg->ns, &ult_arg->iv_key,
	       ult_arg->iv_value.sg_nr == 0 ? NULL : &ult_arg->iv_value,
	       &ult_arg->iv_sync, ult_arg->shortcut, ult_arg->retry, ult_arg->opc);
	ds_iv_ns_put(ult_arg->ns);
	d_sgl_fini(&ult_arg->iv_value, true);
	D_FREE(ult_arg);
}

static int
iv_op_async(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value,
	    crt_iv_sync_t *sync, unsigned int shortcut, bool retry, int opc)
{
	struct iv_op_ult_arg	*ult_arg;
	int			rc;

	D_ALLOC_PTR(ult_arg);
	if (ult_arg == NULL)
		return -DER_NOMEM;

	/* Asynchronous mode, let's realloc the value and key, since
	 * the input parameters will be invalid after the call.
	 */
	if (value) {
		rc = daos_sgl_alloc_copy_data(&ult_arg->iv_value, value);
		if (rc) {
			D_FREE(ult_arg);
			return rc;
		}
	}

	memcpy(&ult_arg->iv_key, key, sizeof(*key));
	ult_arg->shortcut = shortcut;
	ult_arg->iv_sync = *sync;
	ult_arg->retry = retry;
	ds_iv_ns_get(ns);
	ult_arg->ns = ns;
	ult_arg->opc = opc;
	rc = dss_ult_create(iv_op_ult, ult_arg, DSS_XS_SYS, 0, 0, NULL);
	if (rc != 0) {
		ds_iv_ns_put(ult_arg->ns);
		d_sgl_fini(&ult_arg->iv_value, true);
		D_FREE(ult_arg);
	}

	return rc;
}

static int
iv_op(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value,
      crt_iv_sync_t *sync, unsigned int shortcut, bool retry, int opc)
{
	if (ns->iv_stop)
		return -DER_SHUTDOWN;

	if (sync && sync->ivs_mode == CRT_IV_SYNC_LAZY)
		return iv_op_async(ns, key, value, sync, shortcut, retry, opc);

	return _iv_op(ns, key, value, sync, shortcut, retry, opc);
}

/**
 * Fetch the value from the iv_entry, if the entry does not exist, it
 * will create the iv entry locally.
 * param ns[in]		iv namespace.
 * param key[in]	iv key
 * param value[out]	value to hold the fetch value.
 *
 * return		0 if succeed, otherwise error code.
 */
int
ds_iv_fetch(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value,
	    bool retry)
{
	return iv_op(ns, key, value, NULL, 0, retry, IV_FETCH);
}

/**
 * Update the value to the iv_entry through Cart IV, and it will mark the
 * entry to be valid, so the following fetch will retrieve the value from
 * local cache entry.
 * NB: for lazy update, it will clone the key and buffer and free them in
 * the complete callback, in case the caller release them right away.
 *
 * param ns[in]		iv namespace.
 * param key[in]	iv key
 * param value[in]	value for update.
 * param shortcut[in]	shortcut hints (see crt_iv_shortcut_t)
 * param sync_mode[in]	syncmode for update (see crt_iv_sync_mode_t)
 * param sync_flags[in]	sync flags for update (see crt_iv_sync_flag_t)
 *
 * return		0 if succeed, otherwise error code.
 */
int
ds_iv_update(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value,
	     unsigned int shortcut, unsigned int sync_mode,
	     unsigned int sync_flags, bool retry)
{
	crt_iv_sync_t		iv_sync = { 0 };
	int			rc;

	iv_sync.ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	iv_sync.ivs_mode = sync_mode;
	iv_sync.ivs_flags = sync_flags;
	rc = iv_op(ns, key, value, &iv_sync, shortcut, retry, IV_UPDATE);
	return rc;
}

/**
 * invalidate the iv_entry through Cart IV, and it will mark the
 * entry to be invalid, so the following fetch will not be able to
 * retrieve the value from local cache entry.
 *
 * param ns[in]		iv namespace.
 * param key[in]	iv key
 * param shortcut[in]	shortcut hints (see crt_iv_shortcut_t)
 * param sync_mode[in]	syncmode for invalid (see crt_iv_sync_mode_t)
 * param sync_flags[in]	sync flags for invalid (see crt_iv_sync_flag_t)
 *
 * return		0 if succeed, otherwise error code.
 */
int
ds_iv_invalidate(struct ds_iv_ns *ns, struct ds_iv_key *key,
		 unsigned int shortcut, unsigned int sync_mode,
		 unsigned int sync_flags, bool retry)
{
	crt_iv_sync_t iv_sync = { 0 };

	iv_sync.ivs_event = CRT_IV_SYNC_EVENT_NOTIFY;
	iv_sync.ivs_mode = sync_mode;
	iv_sync.ivs_flags = sync_flags;

	return iv_op(ns, key, NULL, &iv_sync, shortcut, retry, IV_INVALIDATE);
}
