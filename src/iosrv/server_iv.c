/**
 * (C) Copyright 2017 Intel Corporation.
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
 * This file is part of the DAOS server. It implements the server .
 */
#define D_LOGFAC	DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <gurt/list.h>
#include <cart/iv.h>
#include <daos_srv/iv.h>
#include "srv_internal.h"

static d_list_t			 ds_iv_ns_list;
static int			 ds_iv_ns_id = 1;
static d_list_t			 ds_iv_class_list;
static int			 ds_iv_class_nr;
static int			 crt_iv_class_nr;
static struct crt_iv_class	*crt_iv_class = NULL;

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
	D_ASSERT(class != NULL);

	if (class->iv_class_ops->ivc_key_unpack)
		rc = class->iv_class_ops->ivc_key_unpack(class, key_iov,
							 key_iv);
	else
		memcpy(key_iv, key_iov->iov_buf, sizeof(*key_iv));

	D_DEBUG(DB_TRACE, "unpack %d\n", key_iv->class_id);
	return rc;
}

static struct ds_iv_ns *
iv_ns_lookup_by_ivns(crt_iv_namespace_t ivns)
{
	struct ds_iv_ns *ns;

	d_list_for_each_entry(ns, &ds_iv_ns_list, iv_ns_link) {
		if (ns->iv_ns == ivns)
			return ns;
	}
	return NULL;
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
	struct ds_iv_entry *found = NULL;
	struct ds_iv_entry *entry;

	ABT_mutex_lock(ns->iv_lock);
	d_list_for_each_entry(entry, &ns->iv_entry_list, iv_link) {
		if (key_equal(entry, key, &entry->iv_key)) {
			/* resolve the permission issue later and also
			 * hold the value XXX
			 */
			found = entry;
			break;
		}
	}
	ABT_mutex_unlock(ns->iv_lock);

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
			daos_sgl_fini(&entry->iv_value, true);
	}

	D_FREE(entry);
}

static int
fetch_iv_value(struct ds_iv_entry *entry, struct ds_iv_key *key,
	       d_sg_list_t *dst, d_sg_list_t *src, void *priv)
{
	struct ds_iv_class	*class = entry->iv_class;
	int			rc;

	if (class->iv_class_ops && class->iv_class_ops->ivc_ent_fetch)
		rc = class->iv_class_ops->ivc_ent_fetch(entry, key, dst, src,
							priv);
	else
		rc = daos_sgl_copy_data(dst, src);

	return rc;
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
	int			rc;

	if (class->iv_class_ops && class->iv_class_ops->ivc_ent_refresh)
		rc = class->iv_class_ops->ivc_ent_refresh(entry, key, src,
							  ref_rc, priv);
	else
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
	ABT_mutex_lock(ns->iv_lock);
	d_list_add(&entry->iv_link, &ns->iv_entry_list);
	ABT_mutex_unlock(ns->iv_lock);
	*got = entry;

	return 1;
}

struct iv_priv_entry {
	struct ds_iv_entry	 *entry;
	void			**priv;
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
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry;
	struct ds_iv_key	key;
	bool			valid;
	int			 rc;

	D_ASSERT(iv_value != NULL);
	ns = iv_ns_lookup_by_ivns(ivns);
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	if (priv_entry == NULL) {
		/* find and prepare entry */
		rc = iv_entry_lookup_or_create(ns, &key, &entry);
		if (rc < 0)
			return rc;
	} else {
		D_ASSERT(priv_entry->entry != NULL);
		entry = priv_entry->entry;
	}

	valid = iv_entry_valid(entry, &key);
	D_DEBUG(DB_TRACE, "FETCH: Key [%d:%d] entry %p valid %s\n", key.rank,
		key.class_id, entry, valid ? "yes" : "no");

	/* Forward the request to its parent if it is not root, and
	 * let's caller decide how to deal with leader.
	 */
	if (!valid && ns->iv_master_rank != dss_self_rank())
		return -DER_IVCB_FORWARD;

	rc = fetch_iv_value(entry, &key, iv_value, &entry->iv_value, priv);
	if (rc == 0)
		entry->iv_valid = true;

	return rc;
}

static int
iv_on_update_internal(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
		      crt_iv_ver_t iv_ver, d_sg_list_t *iv_value,
		      bool invalidate, bool refresh, int ref_rc, void *priv)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry;
	struct ds_iv_key	key;
	struct iv_priv_entry	*priv_entry = priv;
	int			rc = 0;

	ns = iv_ns_lookup_by_ivns(ivns);
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	if (priv_entry == NULL || priv_entry->entry == NULL) {
		/* find and prepare entry */
		rc = iv_entry_lookup_or_create(ns, &key, &entry);
		if (rc < 0)
			return rc;
	} else {
		entry = priv_entry->entry;
	}

	if (iv_value && iv_value->sg_iovs != NULL) {
		if (refresh)
			rc = refresh_iv_value(entry, &key, iv_value, ref_rc,
				      priv_entry ? priv_entry->priv : NULL);
		else
			rc = update_iv_value(entry, &key, iv_value,
				      priv_entry ? priv_entry->priv : NULL);
		if (rc != -DER_IVCB_FORWARD && rc != 0) {
			D_ERROR("key id %d update failed: rc = %d\n",
				key.class_id, rc);
			return rc;
		}
	}

	if (invalidate)
		entry->iv_valid = false;
	else
		entry->iv_valid = true;

	D_DEBUG(DB_TRACE, "key id %d rank %d myrank %d valid %s\n",
		key.class_id, key.rank, dss_self_rank(),
		invalidate ? "no" : "yes");

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

	rc = dss_ult_create(cb_func, cb_arg, DSS_ULT_MISC, DSS_TGT_SELF,
			    0, NULL);
	if (rc)
		D_ERROR("dss_ult_create failed, rc %d.\n", rc);
}

static int
ivc_on_hash(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key, d_rank_t *root)
{
	struct ds_iv_key key;

	iv_key_unpack(&key, iv_key);
	*root = key.rank;
	return 0;
}

static int
ivc_on_get(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	   crt_iv_ver_t iv_ver, crt_iv_perm_t permission,
	   d_sg_list_t *iv_value, void **priv)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry;
	struct ds_iv_class	*class;
	struct ds_iv_key	key;
	struct iv_priv_entry	*priv_entry;
	bool			alloc_entry = false;
	int			rc;

	ns = iv_ns_lookup_by_ivns(ivns);
	D_ASSERT(ns != NULL);

	iv_key_unpack(&key, iv_key);
	/* find and prepare entry */
	rc = iv_entry_lookup_or_create(ns, &key, &entry);
	if (rc < 0)
		return rc;

	if (rc > 0)
		alloc_entry = true;

	class = entry->iv_class;
	if (iv_value) {
		rc = class->iv_class_ops->ivc_value_alloc(entry, iv_value);
		if (rc)
			D_GOTO(out, rc);
	}

	rc = class->iv_class_ops->ivc_ent_get(entry, priv);
	if (rc)
		D_GOTO(out, rc);

	D_ALLOC_PTR(priv_entry);
	if (priv_entry == NULL) {
		class->iv_class_ops->ivc_ent_put(entry, priv);
		D_GOTO(out, rc);
	}

	priv_entry->priv = *priv;
	priv_entry->entry = entry;
	*priv = priv_entry;

out:
	if (rc && alloc_entry) {
		d_list_del(&entry->iv_link);
		iv_entry_free(entry);
	}

	return 0;
}

static int
ivc_on_put(crt_iv_namespace_t ivns, d_sg_list_t *iv_value, void *priv)
{
	struct iv_priv_entry	*priv_entry = priv;
	struct ds_iv_entry	*entry;
	int			 rc;

	D_ASSERT(priv_entry != NULL);

	entry = priv_entry->entry;
	D_ASSERT(entry != NULL);

	/* Let's deal with iv_value first */
	if (iv_value != NULL)
		daos_sgl_fini((d_sg_list_t *)iv_value, false);

	rc = entry->iv_class->iv_class_ops->ivc_ent_put(entry,
							priv_entry->priv);
	if (rc)
		return rc;

	D_FREE(priv_entry);
	D_DEBUG(DB_TRACE, "Put entry %p/%d\n", entry, entry->iv_ref - 1);
	if (--entry->iv_ref > 0)
		return 0;

	d_list_del(&entry->iv_link);
	iv_entry_free(entry);

	return 0;
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
};

static void
iv_ns_destroy_cb(crt_iv_namespace_t iv_ns, void *arg)
{
	struct ds_iv_ns		*ns = arg;
	struct ds_iv_entry	*entry;
	struct ds_iv_entry	*tmp;

	d_list_del(&ns->iv_ns_link);
	d_list_for_each_entry_safe(entry, tmp, &ns->iv_entry_list, iv_link) {
		d_list_del(&entry->iv_link);
		iv_entry_free(entry);
	}

	ABT_mutex_free(&ns->iv_lock);
	D_FREE(ns);
}

static void
iv_ns_destroy_internal(struct ds_iv_ns *ns)
{
	if (ns->iv_ns)
		crt_iv_namespace_destroy(ns->iv_ns, iv_ns_destroy_cb, ns);
}

static struct ds_iv_ns *
ds_iv_ns_lookup(unsigned int ns_id, d_rank_t master_rank)
{
	struct ds_iv_ns *ns;

	d_list_for_each_entry(ns, &ds_iv_ns_list, iv_ns_link) {
		if (ns->iv_ns_id == ns_id &&
		    ns->iv_master_rank == master_rank)
			return ns;
	}

	return NULL;
}

static int
iv_ns_create_internal(unsigned int ns_id, uuid_t pool_uuid,
		      d_rank_t master_rank, struct ds_iv_ns **pns)
{
	struct ds_iv_ns	*ns;

	ns = ds_iv_ns_lookup(ns_id, master_rank);
	if (ns)
		return -DER_EXIST;

	D_ALLOC_PTR(ns);
	if (ns == NULL)
		return -DER_NOMEM;

	uuid_copy(ns->iv_pool_uuid, pool_uuid);
	D_INIT_LIST_HEAD(&ns->iv_entry_list);
	ns->iv_ns_id = ns_id;
	ns->iv_master_rank = master_rank;
	ABT_mutex_create(&ns->iv_lock);
	d_list_add(&ns->iv_ns_link, &ds_iv_ns_list);
	*pns = ns;

	return 0;
}

/* Destroy iv ns. */
void
ds_iv_ns_destroy(void *ns)
{
	struct ds_iv_ns *iv_ns = ns;

	if (iv_ns == NULL)
		return;

	D_DEBUG(DB_TRACE, "destroy ivns %d\n", iv_ns->iv_ns_id);
	iv_ns_destroy_internal(iv_ns);
}

/**
 * Create namespace for server IV, which will only
 * be called on master node
 */
int
ds_iv_ns_create(crt_context_t ctx, uuid_t pool_uuid,
		crt_group_t *grp, unsigned int *ns_id, d_iov_t *ivns,
		struct ds_iv_ns **p_iv_ns)
{
	d_iov_t			*g_ivns;
	d_iov_t			tmp = { 0 };
	struct ds_iv_ns		*ns = NULL;
	int			tree_topo;
	int			rc;

	/* Create namespace on master */
	rc = iv_ns_create_internal(ds_iv_ns_id++, pool_uuid, dss_self_rank(),
				   &ns);
	if (rc)
		return rc;

	if (ivns == NULL)
		g_ivns = &tmp;
	else
		g_ivns = ivns;

	/* Let's set the topo to 32 to avoid cart IV failover,
	 * which is not supported yet. XXX
	 */
	tree_topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	rc = crt_iv_namespace_create(ctx, grp, tree_topo, crt_iv_class,
				     crt_iv_class_nr, &ns->iv_ns, g_ivns);
	if (rc)
		D_GOTO(free, rc);

	*p_iv_ns = ns;
	*ns_id = ns->iv_ns_id;
free:
	if (rc)
		ds_iv_ns_destroy(ns);

	return rc;
}

int
ds_iv_ns_attach(crt_context_t ctx, uuid_t pool_uuid, unsigned int ns_id,
		unsigned int master_rank, d_iov_t *iv_ctxt,
		struct ds_iv_ns **p_iv_ns)
{
	struct ds_iv_ns	*ns = NULL;
	d_rank_t	myrank = dss_self_rank();
	int		rc;

	/* the ns for master will be created in ds_iv_ns_create() */
	if (master_rank == myrank)
		return 0;

	ns = ds_iv_ns_lookup(ns_id, master_rank);
	if (ns) {
		D_DEBUG(DB_TRACE, "lookup iv_ns %d master rank %d"
			" myrank %d ns %p\n", ns_id, master_rank,
			myrank, ns);
		*p_iv_ns = ns;
		return 0;
	}

	rc = iv_ns_create_internal(ns_id, pool_uuid, master_rank, &ns);
	if (rc)
		return rc;

	rc = crt_iv_namespace_attach(ctx, (d_iov_t *)iv_ctxt, crt_iv_class,
				     crt_iv_class_nr, &ns->iv_ns);
	if (rc)
		D_GOTO(free, rc);

	D_DEBUG(DB_TRACE, "create iv_ns %d master rank %d myrank %d ns %p\n",
		ns_id, master_rank, myrank, ns);
	*p_iv_ns = ns;

free:
	if (rc)
		ds_iv_ns_destroy(ns);

	return rc;
}

/* Update iv namespace */
int
ds_iv_ns_update(uuid_t pool_uuid, unsigned int master_rank,
		crt_group_t *grp, d_iov_t *iv_iov,
		unsigned int iv_ns_id, struct ds_iv_ns **iv_ns)
{
	struct ds_iv_ns	*ns;
	int		rc;

	D_ASSERT(iv_ns != NULL);
	if (*iv_ns != NULL &&
	    (*iv_ns)->iv_master_rank != master_rank) {
		/* If root has been changed, let's destroy the
		 * previous IV ns
		 */
		ds_iv_ns_destroy(*iv_ns);
		*iv_ns = NULL;
	}

	if (*iv_ns != NULL)
		return 0;

	/* Create new iv_ns */
	if (iv_iov == NULL) {
		/* master node */
		rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx,
				     pool_uuid, grp, &iv_ns_id, NULL, &ns);
	} else {
		/* other node */
		rc = ds_iv_ns_attach(dss_get_module_info()->dmi_ctx,
				     pool_uuid, iv_ns_id, master_rank, iv_iov,
				     &ns);
	}

	if (rc) {
		D_ERROR("pool iv ns create failed %d\n", rc);
		return rc;
	}

	*iv_ns = ns;
	return rc;
}

/**
 * Get IV ns global identifer from cart.
 */
int
ds_iv_global_ns_get(struct ds_iv_ns *ns, d_iov_t *g_ivns)
{
	return crt_iv_global_namespace_get(ns->iv_ns, g_ivns);
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
}

void
ds_iv_fini(void)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_ns		*tmp;
	struct ds_iv_class	*class;
	struct ds_iv_class	*class_tmp;

	d_list_for_each_entry_safe(class, class_tmp, &ds_iv_class_list,
				   iv_class_list) {
		d_list_del(&class->iv_class_list);
		D_FREE(class);
	}

	d_list_for_each_entry_safe(ns, tmp, &ds_iv_ns_list, iv_ns_link) {
		iv_ns_destroy_internal(ns);
		D_FREE(ns);
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
};

static int
ds_iv_done(crt_iv_namespace_t ivns, uint32_t class_id,
	   crt_iv_key_t *iv_key, crt_iv_ver_t *iv_ver,
	   d_sg_list_t *iv_value, int rc, void *cb_arg)
{
	struct iv_cb_info	*cb_info = cb_arg;
	int			ret = 0;

	cb_info->result = rc;

	if (cb_info->opc == IV_FETCH && cb_info->value) {
		struct ds_iv_entry	*entry;
		struct ds_iv_key	key;

		D_ASSERT(cb_info->ns != NULL);
		entry = iv_class_entry_lookup(cb_info->ns, cb_info->key);
		D_ASSERT(entry != NULL);
		iv_key_unpack(&key, iv_key);
		ret = fetch_iv_value(entry, &key, cb_info->value, iv_value,
				     NULL);
	}

	ABT_future_set(cb_info->future, &rc);
	return ret;
}

static int
iv_internal(struct ds_iv_ns *ns, struct ds_iv_key *key_iv, d_sg_list_t *value,
	    crt_iv_sync_t *sync, unsigned int shortcut, int opc)
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
	D_DEBUG(DB_TRACE, "class_id %d crt class id %d opc %d\n",
		key_iv->class_id, class->iv_cart_class_id, opc);

	iv_key_pack(&key_iov, key_iv);
	memset(&cb_info, 0, sizeof(cb_info));
	cb_info.future = future;
	cb_info.key = key_iv;
	cb_info.value = value;
	cb_info.opc = opc;
	cb_info.ns = ns;
	switch (opc) {
	case IV_FETCH:
		rc = crt_iv_fetch(ns->iv_ns, class->iv_cart_class_id,
				  (crt_iv_key_t *)&key_iov, 0,
				  0, ds_iv_done, &cb_info);
		break;
	case IV_UPDATE:
		rc = crt_iv_update(ns->iv_ns, class->iv_cart_class_id,
				   (crt_iv_key_t *)&key_iov, 0,
				   (d_sg_list_t *)value, shortcut,
				   *sync, ds_iv_done, &cb_info);
		break;
	case IV_INVALIDATE:
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
	D_DEBUG(DB_TRACE, "class_id %d opc %d rc %d\n", key_iv->class_id, opc,
		rc);
out:
	ABT_future_free(&future);
	return rc;
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
ds_iv_fetch(struct ds_iv_ns *ns, struct ds_iv_key *key, d_sg_list_t *value)
{
	return iv_internal(ns, key, value, NULL, 0, IV_FETCH);
}

/**
 * Update the value to the iv_entry through Cart IV, and it will mark the
 * entry to be valid, so the following fetch will retrieve the value from
 * local cache entry.
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
	     unsigned int sync_flags)
{
	crt_iv_sync_t	iv_sync;

	iv_sync.ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	iv_sync.ivs_mode = sync_mode;
	iv_sync.ivs_flags = sync_flags;

	return iv_internal(ns, key, value, &iv_sync, shortcut, IV_UPDATE);
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
		 unsigned int sync_flags)
{
	crt_iv_sync_t iv_sync;

	iv_sync.ivs_event = CRT_IV_SYNC_EVENT_NOTIFY;
	iv_sync.ivs_mode = sync_mode;
	iv_sync.ivs_flags = sync_flags;

	return iv_internal(ns, key, NULL, &iv_sync, shortcut, IV_INVALIDATE);
}
