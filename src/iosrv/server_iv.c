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
#define DDSUBSYS	DDFAC(rebuild)

#include <abt.h>
#include <cart/iv.h>
#include <daos/common.h>
#include <daos/list.h>
#include <daos_srv/iv.h>
#include "srv_internal.h"

static daos_list_t	ds_iv_ns_list;
static d_rank_t		myrank;
static int		ds_iv_ns_id = 1;
static daos_list_t	ds_iv_key_type_list;

static struct ds_iv_key_type *
iv_key_type_lookup(unsigned int key_id)
{
	struct ds_iv_key_type *type;

	daos_list_for_each_entry(type, &ds_iv_key_type_list, iv_key_list) {
		if (type->iv_key_id == key_id)
			return type;
	}

	return NULL;
}

/**
 * UnRegister the key type.
 */
int
ds_iv_key_type_unregister(unsigned int key_id)
{
	struct ds_iv_key_type *type;

	daos_list_for_each_entry(type, &ds_iv_key_type_list, iv_key_list) {
		if (type->iv_key_id == key_id) {
			daos_list_del(&type->iv_key_list);
			D_FREE_PTR(type);
			return 0;
		}
	}

	D__DEBUG(DB_TRACE, "can not find the key %d\n", key_id);
	return 0;
}

/**
 * Register the key type and its cache management ops to the key list, so
 * when later creating the cache entry, it can find the ops by key.
 */
int
ds_iv_key_type_register(unsigned int key_id, struct ds_iv_entry_ops *ops)
{
	struct ds_iv_key_type *type;

	D__DEBUG(DB_TRACE, "register key %d\n", key_id);
	type = iv_key_type_lookup(key_id);
	if (type != NULL)
		return 0;

	D_ALLOC_PTR(type);
	if (type == NULL)
		return -DER_NOMEM;

	type->iv_key_id = key_id;
	type->iv_key_ops = ops;
	DAOS_INIT_LIST_HEAD(&type->iv_key_list);
	daos_list_add(&type->iv_key_list, &ds_iv_key_type_list);
	return 0;
}

static struct ds_iv_ns *
iv_ns_lookup_by_ivns(crt_iv_namespace_t ivns)
{
	struct ds_iv_ns *ns;

	daos_list_for_each_entry(ns, &ds_iv_ns_list, iv_ns_link) {
		if (ns->iv_ns == ivns)
			return ns;
	}
	return NULL;
}

static int
iv_ns_create_internal(unsigned int ns_id, d_rank_t rank,
			 struct ds_iv_ns **pns)
{
	struct ds_iv_ns *ns;
	struct ds_iv_ns *tmp;

	/* Destroy the ns with the same id, probably because the new
	 * is elected.
	 */
	daos_list_for_each_entry_safe(ns, tmp, &ds_iv_ns_list, iv_ns_link) {
		if (ns->iv_ns_id == ns_id) {
			daos_list_del(&ns->iv_ns_link);
			D__DEBUG(DB_TRACE, "orig rank %d -> %d\n",
				ns->iv_master_rank, rank);
			D_ASSERT(ns->iv_master_rank != rank);
			/* XXX Move the key/value to new namespace? */
			crt_iv_namespace_destroy(&ns->iv_ns);
			D_FREE_PTR(ns);
		}
	}

	D_ALLOC_PTR(ns);
	if (ns == NULL)
		return -DER_NOMEM;

	DAOS_INIT_LIST_HEAD(&ns->iv_key_list);
	ns->iv_ns_id = ns_id;
	ns->iv_master_rank = rank;
	ABT_mutex_create(&ns->iv_lock);
	daos_list_add(&ns->iv_ns_link, &ds_iv_ns_list);
	*pns = ns;
	return 0;
}

static void
iv_entry_free(struct ds_iv_entry *entry)
{
	if (entry == NULL)
		return;

	if (entry->value.sg_iovs != NULL)
		entry->ent_ops->iv_ent_destroy(&entry->value);

	D_FREE_PTR(entry);
}

static bool
key_equal(daos_key_t *key1, daos_key_t *key2)
{
	struct ds_iv_key *ds_key1;
	struct ds_iv_key *ds_key2;

	ds_key1 = key1->iov_buf;
	ds_key2 = key2->iov_buf;

	if (ds_key1->key_id == ds_key2->key_id)
		return true;

	return false;
}

static int
copy_iv_value(d_sg_list_t *dst, d_sg_list_t *src)
{
	int i;

	D_ASSERT(dst != NULL);
	D_ASSERT(src != NULL);
	D_ASSERT(dst->sg_nr.num <= src->sg_nr.num);
	D_ASSERT(dst->sg_iovs != NULL);

	for (i = 0; i < dst->sg_nr.num; i++) {
		D_ASSERT(src->sg_iovs[i].iov_buf != NULL);
		if (dst->sg_iovs[i].iov_buf == src->sg_iovs[i].iov_buf)
			continue;
		/* Note: If dst iov_buf is NULL, it will only set buf ptr
		 * to avoid memory copy.
		 */
		if (dst->sg_iovs[i].iov_buf == NULL) {
			dst->sg_iovs[i].iov_buf = src->sg_iovs[i].iov_buf;
			dst->sg_iovs[i].iov_buf_len =
					src->sg_iovs[i].iov_buf_len;
			dst->sg_iovs[i].iov_len =
					src->sg_iovs[i].iov_len;
		} else {
			D_ASSERTF(dst->sg_iovs[i].iov_buf_len >=
				  src->sg_iovs[i].iov_len,
				  "dst buf len %d src len %d\n",
				  (int)dst->sg_iovs[i].iov_buf_len,
				  (int)src->sg_iovs[i].iov_len);
			memcpy(dst->sg_iovs[i].iov_buf, src->sg_iovs[i].iov_buf,
			       src->sg_iovs[i].iov_len);
			dst->sg_iovs[i].iov_len = src->sg_iovs[i].iov_len;
		}
	}

	return 0;
}

static int
fetch_iv_value(struct ds_iv_entry *entry, d_sg_list_t *dst,
	       d_sg_list_t *src)
{
	int rc;

	D_ASSERT(entry->ent_ops != NULL);
	if (entry->ent_ops->iv_ent_fetch != NULL)
		rc = entry->ent_ops->iv_ent_fetch(dst, src);
	else
		rc = copy_iv_value(dst, src);
	return rc;
}

static int
update_iv_value(struct ds_iv_entry *entry, d_sg_list_t *dst,
		d_sg_list_t *src)
{
	int rc;

	D_ASSERT(entry->ent_ops != NULL);
	if (entry->ent_ops->iv_ent_update != NULL)
		rc = entry->ent_ops->iv_ent_update(dst, src);
	else
		rc = copy_iv_value(dst, src);
	return rc;
}

static int
refresh_iv_value(struct ds_iv_entry *entry, d_sg_list_t *dst,
		 d_sg_list_t *src)
{
	int rc;

	D_ASSERT(entry->ent_ops != NULL);
	if (entry->ent_ops->iv_ent_refresh != NULL)
		rc = entry->ent_ops->iv_ent_refresh(dst, src);
	else
		rc = copy_iv_value(dst, src);
	return rc;
}

static int
iv_on_fetch(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	    crt_iv_ver_t *iv_ver, uint32_t flags,
	    d_sg_list_t *iv_value, void *priv)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry;
	struct ds_iv_key	*key;

	D_ASSERT(iv_value != NULL);
	ns = iv_ns_lookup_by_ivns(ivns);
	D_ASSERT(ns != NULL);
	key = iv_key->iov_buf;

	entry = priv;
	D_ASSERT(entry != NULL);

	D__DEBUG(DB_TRACE, "FETCH: Key [%d:%d] entry %p valid %s\n", key->rank,
		 key->key_id, entry, entry->valid ? "yes" : "no");

	if (!entry->valid)
		return -DER_IVCB_FORWARD;

	/* iv value already include the valid data */
	if (iv_value->sg_iovs != NULL &&
	    entry->value.sg_iovs[0].iov_buf == iv_value->sg_iovs[0].iov_buf)
		return 0;

	fetch_iv_value(entry, iv_value, &entry->value);
	return 0;
}

static struct ds_iv_entry *
iv_entry_lookup(struct ds_iv_ns *ns, crt_iv_key_t *key)
{
	struct ds_iv_entry *found = NULL;
	struct ds_iv_entry *entry;

	ABT_mutex_lock(ns->iv_lock);
	daos_list_for_each_entry(entry, &ns->iv_key_list, link) {
		if (key_equal((daos_key_t *)key, &entry->key)) {
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

static int
iv_on_update_internal(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
		      crt_iv_ver_t iv_ver, d_sg_list_t *iv_value,
		      bool invalidate, bool refresh, void *priv)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry = priv;
	struct ds_iv_key	*key;
	int			rc;

	ns = iv_ns_lookup_by_ivns(ivns);
	D_ASSERT(ns != NULL);

	key = (struct ds_iv_key *)iv_key->iov_buf;

	if (key->rank == myrank)
		rc = 0;
	else
		rc = -DER_IVCB_FORWARD;

	if (entry == NULL) {
		entry = iv_entry_lookup(ns, iv_key);
		if (entry == NULL)
			D__GOTO(out, rc = -DER_INVAL);
	}

	if (iv_value && iv_value->sg_iovs != NULL) {
		if (refresh)
			refresh_iv_value(entry, &entry->value, iv_value);
		else
			update_iv_value(entry, &entry->value, iv_value);
	}

	if (invalidate)
		entry->valid = false;
	else
		entry->valid = true;

out:
	D__DEBUG(DB_TRACE, "key id %d rank %d myrank %d rc %d\n",
		 key->key_id, key->rank, myrank, rc);
	return rc;
}

static int
iv_on_refresh(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	      crt_iv_ver_t iv_ver, d_sg_list_t *iv_value, bool invalidate,
	      int refresh_rc, void *priv)
{
	int rc;

	rc = iv_on_update_internal(ivns, iv_key, iv_ver, iv_value, invalidate,
				   true, priv);
	if (rc == -DER_IVCB_FORWARD)
		rc = 0;

	return rc;
}

static int
iv_on_update(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	     crt_iv_ver_t iv_ver, uint32_t flags, d_sg_list_t *iv_value,
	     void *priv)
{
	return iv_on_update_internal(ivns, iv_key, iv_ver, iv_value, false,
				     false, priv);
}

static int
iv_on_hash(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key, d_rank_t *root)
{
	struct ds_iv_key *key;

	key = iv_key->iov_buf;
	*root = key->rank;
	return 0;
}

static int
iv_entry_alloc(struct ds_iv_key *iv_key, struct ds_iv_key_type *type,
	       void *data, struct ds_iv_entry **entryp)
{
	struct ds_iv_entry *entry;
	daos_iov_t	   iov;
	struct ds_iv_key   *key;
	int		   rc;

	D_ALLOC_PTR(entry);
	if (entry == NULL)
		return -DER_NOMEM;

	entry->valid = false;
	iov.iov_buf = iv_key;
	iov.iov_len = sizeof(*iv_key);
	iov.iov_buf_len = sizeof(*iv_key);
	rc = daos_iov_copy(&entry->key, &iov);
	if (rc)
		D_GOTO(free, rc);

	rc = type->iv_key_ops->iv_ent_alloc(iv_key, data, &entry->value);
	if (rc)
		D_GOTO(free, rc);

	key = entry->key.iov_buf;
	key->key_id = iv_key->key_id;
	key->rank = iv_key->rank;

	entry->ent_ops = type->iv_key_ops;
	entry->ref = 1;
	*entryp = entry;
free:
	if (rc)
		iv_entry_free(entry);

	return rc;
}

static int
iv_on_get(crt_iv_namespace_t ivns, crt_iv_key_t *iv_key,
	  crt_iv_ver_t iv_ver, crt_iv_perm_t permission,
	  d_sg_list_t *iv_value, void **priv)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_entry	*entry;
	struct ds_iv_entry	*found = NULL;
	struct ds_iv_key	*key;
	struct ds_iv_key_type	*type;
	int			rc;

	ns = iv_ns_lookup_by_ivns(ivns);
	D_ASSERT(ns != NULL);

	/* Let's find it from cache first */
	found = iv_entry_lookup(ns, iv_key);
	if (found != NULL) {
		found->ref++;
		*priv = found;
		if (iv_value != NULL) {
			D__DEBUG(DB_TRACE, "get entry valid %s\n",
				 found->valid ? "yes" : "no");
			rc = found->ent_ops->iv_ent_get(iv_value, found);
			if (rc)
				return rc;
		}
		return 0;
	}

	key = iv_key->iov_buf;
	type = iv_key_type_lookup(key->key_id);
	if (type == NULL)
		return -DER_NONEXIST;

	/* Allocate the entry */
	rc = iv_entry_alloc(key, type, NULL, &entry);
	if (rc)
		return rc;

	entry->ref++;
	ABT_mutex_lock(ns->iv_lock);
	daos_list_add(&entry->link, &ns->iv_key_list);
	ABT_mutex_unlock(ns->iv_lock);

	if (iv_value != NULL) {
		rc = entry->ent_ops->iv_ent_get(iv_value, entry);
		if (rc)
			return rc;
	}

	*priv = entry;

	return 0;
}

static int
iv_on_put(crt_iv_namespace_t ivns, d_sg_list_t *iv_value, void *priv)
{
	struct ds_iv_entry	*entry = priv;
	int			rc;

	D_ASSERT(entry != NULL);

	/* Let's deal with iv_value first */
	if (iv_value != NULL && iv_value->sg_iovs != NULL &&
	    iv_value->sg_iovs[0].iov_buf !=
	    entry->value.sg_iovs[0].iov_buf)
		daos_sgl_fini((daos_sg_list_t *)iv_value, false);

	rc = entry->ent_ops->iv_ent_put(&entry->value, entry);
	if (rc)
		return rc;

	if (--entry->ref > 0)
		return 0;

	iv_entry_free(entry);

	return 0;
}

struct crt_iv_ops ivc_ops = {
	.ivo_on_fetch = iv_on_fetch,
	.ivo_on_update = iv_on_update,
	.ivo_on_refresh = iv_on_refresh,
	.ivo_on_hash = iv_on_hash,
	.ivo_on_get = iv_on_get,
	.ivo_on_put = iv_on_put,
};

/**
 * Create namespace for server IV, which will only
 * be called on master node
 */
int
ds_iv_ns_create(crt_context_t ctx, unsigned int *ns_id,
		daos_iov_t *g_ivns, struct ds_iv_ns **p_iv_ns)
{
	struct ds_iv_ns		*ns = NULL;
	struct crt_iv_class	iv_class;
	int			tree_topo;
	int			rc;

	/* Create namespace on master */
	rc = iv_ns_create_internal(ds_iv_ns_id++, myrank, &ns);
	if (rc)
		return rc;

	*ns_id = ns->iv_ns_id;
	iv_class.ivc_id = ns->iv_ns_id;
	iv_class.ivc_feats = 0;
	iv_class.ivc_ops = &ivc_ops;

	/* Let's set the topo to 32 to avoid cart IV failover,
	 * which is not supported yet. XXX
	 */
	tree_topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	rc = crt_iv_namespace_create(ctx, NULL, tree_topo,
				     &iv_class, 1, &ns->iv_ns,
				     (d_iov_t *)g_ivns);
	if (rc) {
		daos_list_del(&ns->iv_ns_link);
		D_FREE_PTR(ns);
		return rc;
	}

	*p_iv_ns = ns;
	return 0;
}

int
ds_iv_ns_attach(crt_context_t ctx, unsigned int ns_id,
		unsigned int master_rank, daos_iov_t *iv_ctxt,
		struct ds_iv_ns **p_iv_ns)
{
	struct crt_iv_class	iv_class;
	struct ds_iv_ns		*ns = NULL;
	int			rc;

	/* the ns for master will be created in ds_iv_ns_create() */
	if (master_rank == myrank)
		return 0;

	iv_class.ivc_id = ns_id;
	iv_class.ivc_feats = 0;
	iv_class.ivc_ops = &ivc_ops;
	rc = iv_ns_create_internal(ns_id, master_rank, &ns);
	if (rc)
		D_GOTO(out, rc);

	rc = crt_iv_namespace_attach(ctx, (d_iov_t *)iv_ctxt,
				     &iv_class, 1, &ns->iv_ns);

	if (rc) {
		daos_list_del(&ns->iv_ns_link);
		D_FREE_PTR(ns);
		D_GOTO(out, rc);
	}

	D__DEBUG(DB_TRACE, "create iv_ns %d master rank %d myrank %d ns %p\n",
		 ns_id, master_rank, myrank, ns);
	*p_iv_ns = ns;
out:
	return rc;
}

unsigned int
ds_iv_ns_id_get(void *ns)
{
	return ((struct ds_iv_ns *)ns)->iv_ns_id;
}

static void
ds_iv_ns_destroy_internal(struct ds_iv_ns *ns)
{
	struct ds_iv_entry *entry;
	struct ds_iv_entry *tmp;

	daos_list_for_each_entry_safe(entry, tmp, &ns->iv_key_list, link) {
		daos_list_del(&entry->link);
		iv_entry_free(entry);
	}

	crt_iv_namespace_destroy(ns->iv_ns);
	ABT_mutex_free(&ns->iv_lock);
}

/* Destroy iv ns. */
void
ds_iv_ns_destroy(void *ns)
{
	struct ds_iv_ns *iv_ns = ns;

	if (iv_ns == NULL)
		return;

	daos_list_del(&iv_ns->iv_ns_link);
	ds_iv_ns_destroy_internal(iv_ns);
}

int
ds_iv_init()
{
	int rc;

	DAOS_INIT_LIST_HEAD(&ds_iv_ns_list);
	DAOS_INIT_LIST_HEAD(&ds_iv_key_type_list);
	rc = crt_group_rank(NULL, &myrank);
	return rc;
}

int
ds_iv_fini(void)
{
	struct ds_iv_ns		*ns;
	struct ds_iv_ns		*tmp;
	struct ds_iv_key_type	*type;
	struct ds_iv_key_type	*type_tmp;

	daos_list_for_each_entry_safe(type, type_tmp, &ds_iv_key_type_list,
				      iv_key_list) {
		daos_list_del(&type->iv_key_list);
		D_FREE_PTR(type);
	}

	daos_list_for_each_entry_safe(ns, tmp, &ds_iv_ns_list, iv_ns_link) {
		daos_list_del(&ns->iv_ns_link);
		ds_iv_ns_destroy_internal(ns);
		D_FREE_PTR(ns);
	}

	return 0;
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
	struct iv_cb_info *cb_info = cb_arg;

	cb_info->result = rc;

	if (cb_info->opc == IV_FETCH) {
		struct ds_iv_entry	*entry;

		D_ASSERT(cb_info->ns != NULL);
		entry = iv_entry_lookup(cb_info->ns, iv_key);
		D_ASSERT(entry != NULL);
		fetch_iv_value(entry, cb_info->value, iv_value);
	}

	ABT_future_set(cb_info->future, &rc);
	return 0;
}

static int
iv_internal(struct ds_iv_ns *ns, unsigned int key_id,
	    d_sg_list_t *value, crt_iv_sync_t *sync,
	    unsigned int shortcut, int opc)
{
	struct iv_cb_info	cb_info;
	ABT_future		future;
	struct ds_iv_key	key;
	crt_iv_key_t		iv_key;
	int			rc;

	rc = ABT_future_create(1, NULL, &future);
	if (rc)
		return rc;

	memset(&key, 0, sizeof(key));
	key.key_id = key_id;
	key.rank = ns->iv_master_rank;
	iv_key.iov_len = sizeof(key);
	iv_key.iov_buf_len = sizeof(key);
	iv_key.iov_buf = &key;

	D__DEBUG(DB_TRACE, "key_id %d opc %d\n", key_id, opc);
	memset(&cb_info, 0, sizeof(cb_info));
	cb_info.future = future;
	cb_info.key = &key;
	cb_info.value = value;
	cb_info.opc = opc;
	cb_info.ns = ns;
	switch (opc) {
	case IV_FETCH:
		rc = crt_iv_fetch(ns->iv_ns, 0, (crt_iv_key_t *)&iv_key, 0,
				  0, ds_iv_done, &cb_info);
		break;
	case IV_UPDATE:
		rc = crt_iv_update(ns->iv_ns, 0, (crt_iv_key_t *)&iv_key, 0,
				   (d_sg_list_t *)value, shortcut,
				   *sync, ds_iv_done, &cb_info);
		break;
	case IV_INVALIDATE:
		rc = crt_iv_invalidate(ns->iv_ns, 0, (crt_iv_key_t *)&iv_key,
				       0, 0, *sync, ds_iv_done, &cb_info);
		break;
	default:
		D_ASSERT(0);
	}

	if (rc)
		D_GOTO(out, rc);

	ABT_future_wait(future);
	rc = cb_info.result;
	D__DEBUG(DB_TRACE, "key_id %d opc %d rc %d\n", key_id, opc, rc);
out:
	ABT_future_free(&future);
	return rc;
}

/**
 * Fetch the value from the iv_entry, if the entry does not exist, it
 * will create the iv entry locally.
 */
int
ds_iv_fetch(struct ds_iv_ns *ns, unsigned int key_id, d_sg_list_t *value)
{
	return iv_internal(ns, key_id, value, NULL, 0, IV_FETCH);
}

/**
 * Update the value from the iv_entry, then mark the value to be valid.
 * There are two sync modes:
 *	CRT_IV_SYNC_LAZY: Update the value asynchronously.
 *	CRT_IV_SYNC_EAGER: Update the value synchronously.
 */
int
ds_iv_update(struct ds_iv_ns *ns, unsigned int key_id, d_sg_list_t *value,
	     unsigned int shortcut, unsigned int sync_mode)
{
	crt_iv_sync_t	iv_sync;

	iv_sync.ivs_event = CRT_IV_SYNC_EVENT_UPDATE;
	iv_sync.ivs_mode = sync_mode;

	return iv_internal(ns, key_id, value, &iv_sync, shortcut, IV_UPDATE);
}

/**
 * Invalidate the value from the iv_entry.
 * There are two sync modes:
 *	CRT_IV_SYNC_LAZY: Invalidate the value asynchronously.
 *	CRT_IV_SYNC_EAGER: Invalidate the value synchronously.
 */
int
ds_iv_invalidate(struct ds_iv_ns *ns, unsigned int key_id,
		 unsigned int shortcut, unsigned int sync_mode)
{
	crt_iv_sync_t iv_sync;

	iv_sync.ivs_event = CRT_IV_SYNC_EVENT_NOTIFY;
	iv_sync.ivs_mode = sync_mode;

	return iv_internal(ns, key_id, NULL, &iv_sync, shortcut,
			   IV_INVALIDATE);
}
