/**
 * (C) Copyright 2016 Intel Corporation.
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
 * dsms: Storage Implementation
 *
 * This file implements the dbtree classes used by dsms and other
 * storage-related stuff.
 */

#include <string.h>
#include <daos/btree.h>
#include <daos/mem.h>
#include <daos_errno.h>
#include "dsms_internal.h"
#include "dsms_layout.h"

#define HAVE_DBTREE_DELETE 0	/* dbtree_delete() not yet implemented */

#if !HAVE_DBTREE_DELETE

static int
lookup(daos_handle_t kvsh, daos_iov_t *key, daos_iov_t *val)
{
	int rc;

	rc = dbtree_lookup(kvsh, key, val);
	if (rc != 0)
		return rc;

	if (val->iov_len == 0)
		return -DER_NONEXIST;

	return 0;
}

static int
delete(daos_handle_t kvsh, daos_iov_t *key)
{
	daos_iov_t val;

	val.iov_buf = NULL;
	val.iov_buf_len = 0;
	val.iov_len = val.iov_buf_len;

	return dbtree_update(kvsh, key, &val);
}

#define dbtree_lookup	lookup
#define dbtree_delete	delete

#endif

static int
lookup_ptr(daos_handle_t kvsh, daos_iov_t *key, daos_iov_t *val)
{
	int rc;

	val->iov_buf = NULL;
	val->iov_buf_len = 0;
	val->iov_len = val->iov_buf_len;

	rc = dbtree_lookup(kvsh, key, val);
	if (rc != 0)
		return rc;

	return 0;
}

static int
create_kvs(daos_handle_t kvsh, daos_iov_t *key, unsigned int class,
	   uint64_t feats, unsigned int order, PMEMobjpool *mp,
	   daos_handle_t *kvsh_new)
{
	struct btr_root		buf;
	daos_iov_t		val;
	struct umem_attr	uma;
	daos_handle_t		h;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK);

	memset(&buf, 0, sizeof(buf));
	val.iov_buf = (void *)&buf;
	val.iov_buf_len = sizeof(buf);
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, key, &val);
	if (rc != 0)
		return rc;

	rc = lookup_ptr(kvsh, key, &val);
	if (rc != 0)
		return rc;

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;

	rc = dbtree_create_inplace(class, feats, order, &uma, val.iov_buf, &h);
	if (rc != 0)
		return rc;

	if (kvsh_new == NULL)
		dbtree_close(h);
	else
		*kvsh_new = h;

	return 0;
}

static int
open_kvs(daos_handle_t kvsh, daos_iov_t *key, PMEMobjpool *mp,
	 daos_handle_t *kvsh_child)
{
	daos_iov_t		val;
	struct umem_attr	uma;
	int			rc;

	rc = lookup_ptr(kvsh, key, &val);
	if (rc != 0)
		return rc;

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp;

	rc = dbtree_open_inplace(val.iov_buf, &uma, kvsh_child);
	if (rc != 0)
		return rc;

	return 0;
}

static int
destroy_kvs(daos_handle_t kvsh, daos_iov_t *key, PMEMobjpool *mp)
{
	volatile daos_handle_t	h;
	daos_handle_t		tmp;
	volatile int		rc;

	rc = open_kvs(kvsh, key, mp, &tmp);
	if (rc != 0)
		return rc;

	h = tmp;

	TX_BEGIN(mp) {
		rc = dbtree_destroy(h);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		h = DAOS_HDL_INVAL;

		rc = dbtree_delete(kvsh, key);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		if (!daos_handle_is_inval(h))
			dbtree_close(h);
	} TX_END

	return rc;
}

/*
 * KVS_NV: name-value pairs
 *
 * A name is a variable-length, '\0'-terminated string. A value is a
 * variable-size blob. Names are unordered.
 */

struct nv_rec {
	umem_id_t	nr_value;
	uint64_t	nr_value_size;
	uint64_t	nr_value_buf_size;
	uint64_t	nr_name_size;	/* strlen(name) + 1 */
	char		nr_name[];
};

static void
nv_hkey_gen(struct btr_instance *tins, daos_iov_t *key, void *hkey)
{
	const char     *name = key->iov_buf;
	uint32_t       *hash = hkey;

	/*
	 * TODO: This function should be allowed to return an error
	 * code.
	 */
	D_ASSERT(key->iov_len <= key->iov_buf_len);
	D_ASSERT(memchr(key->iov_buf, '\0', key->iov_len) != NULL);

	*hash = daos_hash_string_u32(name, strlen(name));
}

static int
nv_hkey_size(struct btr_instance *tins)
{
	return sizeof(uint32_t);
}

static int
nv_key_cmp(struct btr_instance *tins, struct btr_record *rec, daos_iov_t *key)
{
	struct nv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

	return strcmp(r->nr_name, (const char *)key->iov_buf);
}

static int
nv_rec_alloc(struct btr_instance *tins, daos_iov_t *key, daos_iov_t *val,
	       struct btr_record *rec)
{
	struct nv_rec  *r;
	umem_id_t	rid;
	void	       *value;
	size_t		name_len;
	int		rc = -DER_INVAL;

	if (key->iov_len == 0 || key->iov_buf_len < key->iov_len ||
	    val->iov_len == 0 || val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc);

	name_len = strnlen((char *)key->iov_buf, key->iov_len);
	/* key->iov_buf may not be '\0'-terminated. */
	if (name_len == key->iov_len)
		D_GOTO(err, rc);

	rc = -DER_NOMEM;

	rid = umem_zalloc(&tins->ti_umm, sizeof(*r) + name_len + 1);
	if (UMMID_IS_NULL(rid))
		D_GOTO(err, rc);

	r = umem_id2ptr(&tins->ti_umm, rid);
	r->nr_value_size = val->iov_len;
	r->nr_value_buf_size = r->nr_value_size;

	r->nr_value = umem_alloc(&tins->ti_umm, r->nr_value_buf_size);
	if (UMMID_IS_NULL(r->nr_value))
		D_GOTO(err_r, rc);

	value = umem_id2ptr(&tins->ti_umm, r->nr_value);
	memcpy(value, val->iov_buf, r->nr_value_size);

	r->nr_name_size = name_len + 1;
	memcpy(r->nr_name, key->iov_buf, r->nr_name_size);

	rec->rec_mmid = rid;
	return 0;

err_r:
	umem_free(&tins->ti_umm, rid);
err:
	return rc;
}

static int
nv_rec_free(struct btr_instance *tins, struct btr_record *rec)
{
	struct nv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

	umem_free(&tins->ti_umm, r->nr_value);
	umem_free(&tins->ti_umm, rec->rec_mmid);
	return 0;
}

static int
nv_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key, daos_iov_t *val)
{
	struct nv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

	/* TODO: What sanity checks are required for key and val? */

	if (key != NULL) {
		if (key->iov_buf == NULL)
			key->iov_buf = r->nr_name;
		else if (r->nr_name_size <= key->iov_buf_len)
			memcpy(key->iov_buf, r->nr_name, r->nr_name_size);

		key->iov_len = r->nr_name_size;
	}

	if (val != NULL) {
		void   *value = umem_id2ptr(&tins->ti_umm, r->nr_value);

		if (val->iov_buf == NULL)
			val->iov_buf = value;
		else if (r->nr_value_size <= val->iov_buf_len)
			memcpy(val->iov_buf, value, r->nr_value_size);

		val->iov_len = r->nr_value_size;
	}

	return 0;
}

static int
nv_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	struct nv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	void	       *v;

	umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));

	if (r->nr_value_buf_size < val->iov_len) {
		umem_id_t vid;

		vid = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMMID_IS_NULL(vid))
			return -DER_NOMEM;

		umem_free(&tins->ti_umm, r->nr_value);

		r->nr_value = vid;
		r->nr_value_buf_size = val->iov_len;
	} else {
		umem_tx_add(&tins->ti_umm, r->nr_value, val->iov_len);
	}

	v = umem_id2ptr(&tins->ti_umm, r->nr_value);
	memcpy(v, val->iov_buf, val->iov_len);
	r->nr_value_size = val->iov_len;
	return 0;
}

static char *
nv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct nv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	void	       *value = umem_id2ptr(&tins->ti_umm, r->nr_value);
	uint32_t       *hkey = (uint32_t *)rec->rec_hkey;

	if (leaf)
		snprintf(buf, buf_len, "\"%s\":%p+"DF_U64"("DF_U64")",
			 r->nr_name, value, r->nr_value_size,
			 r->nr_value_buf_size);
	else
		snprintf(buf, buf_len, "%u", *hkey);

	return buf;
}

static btr_ops_t nv_ops = {
	.to_hkey_gen	= nv_hkey_gen,
	.to_hkey_size	= nv_hkey_size,
	.to_key_cmp	= nv_key_cmp,
	.to_rec_alloc	= nv_rec_alloc,
	.to_rec_free	= nv_rec_free,
	.to_rec_fetch	= nv_rec_fetch,
	.to_rec_update	= nv_rec_update,
	.to_rec_string	= nv_rec_string
};

int
dsms_kvs_nv_update(daos_handle_t kvsh, const char *name, const void *value,
		   size_t size)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	D_DEBUG(DF_DSMS, "updating \"%s\":%p+%zu\n", name, value, size);

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	val.iov_buf = (void *)value;
	val.iov_buf_len = size;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update \"%s\": %d\n", name, rc);

	return rc;
}

int
dsms_kvs_nv_lookup(daos_handle_t kvsh, const char *name, void *value,
		   size_t size)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	D_DEBUG(DF_DSMS, "looking up \"%s\"\n", name);

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	val.iov_buf = value;
	val.iov_buf_len = size;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_lookup(kvsh, &key, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find \"%s\"\n", name);
		else
			D_ERROR("failed to look up \"%s\": %d\n", name, rc);
		return rc;
	}

	return 0;
}

/*
 * Output the address and the size of the value, instead of copying to volatile
 * memory.
 */
int
dsms_kvs_nv_lookup_ptr(daos_handle_t kvsh, const char *name, void **value,
		       size_t *size)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	D_DEBUG(DF_DSMS, "looking up \"%s\" ptr\n", name);

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	rc = lookup_ptr(kvsh, &key, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find \"%s\"\n", name);
		else
			D_ERROR("failed to look up \"%s\": %d\n", name, rc);
		return rc;
	}

	*value = val.iov_buf;
	*size = val.iov_len;
	return 0;
}

int
dsms_kvs_nv_delete(daos_handle_t kvsh, const char *name)
{
	daos_iov_t	key;
	int		rc;

	D_DEBUG(DF_DSMS, "deleting \"%s\"\n", name);

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	rc = dbtree_delete(kvsh, &key);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find \"%s\"\n", name);
		else
			D_ERROR("failed to delete \"%s\": %d\n", name, rc);
	}

	return rc;
}

/*
 * Create a KVS in place as the value for "name". If "kvsh_new" is not NULL,
 * then leave the new KVS open and return the handle in "*kvsh_new"; otherwise,
 * close the new KVS. "class", "feats", and "order" are passed to
 * dbtree_create_inplace() unchanged.
 */
int
dsms_kvs_nv_create_kvs(daos_handle_t kvsh, const char *name, unsigned int class,
		       uint64_t feats, unsigned int order, PMEMobjpool *mp,
		       daos_handle_t *kvsh_new)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	rc = create_kvs(kvsh, &key, class, feats, order, mp, kvsh_new);
	if (rc != 0)
		D_ERROR("failed to create \"%s\": %d\n", name, rc);

	return rc;
}

int
dsms_kvs_nv_open_kvs(daos_handle_t kvsh, const char *name, PMEMobjpool *mp,
		     daos_handle_t *kvsh_child)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	rc = open_kvs(kvsh, &key, mp, kvsh_child);
	if (rc != 0)
		D_ERROR("failed to open \"%s\": %d\n", name, rc);

	return rc;
}

/* Destroy a KVS in place as the value for "name". */
int
dsms_kvs_nv_destroy_kvs(daos_handle_t kvsh, const char *name, PMEMobjpool *mp)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	rc = destroy_kvs(kvsh, &key, mp);
	if (rc != 0)
		D_ERROR("failed to destroy \"%s\": %d\n", name, rc);

	return rc;
}

/*
 * KVS_UV: UUID-value pairs
 *
 * A UUID is of the uuid_t type. A value is a variable-size blob. UUIDs are
 * unordered.
 */

struct uv_rec {
	umem_id_t	ur_value;
	uint64_t	ur_value_size;
	uint64_t	ur_value_buf_size;
};

static void
uv_hkey_gen(struct btr_instance *tins, daos_iov_t *key, void *hkey)
{
	uuid_copy(*(uuid_t *)hkey, *(uuid_t *)key->iov_buf);
}

static int
uv_hkey_size(struct btr_instance *tins)
{
	return sizeof(uuid_t);
}

static int
uv_rec_alloc(struct btr_instance *tins, daos_iov_t *key, daos_iov_t *val,
	       struct btr_record *rec)
{
	struct uv_rec  *r;
	umem_id_t	rid;
	void	       *value;
	int		rc = -DER_INVAL;

	if (key->iov_len != sizeof(uuid_t) || key->iov_buf_len < key->iov_len ||
	    val->iov_len == 0 || val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc);

	rid = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMMID_IS_NULL(rid))
		D_GOTO(err, rc);

	r = umem_id2ptr(&tins->ti_umm, rid);
	r->ur_value_size = val->iov_len;
	r->ur_value_buf_size = r->ur_value_size;

	r->ur_value = umem_alloc(&tins->ti_umm, r->ur_value_buf_size);
	if (UMMID_IS_NULL(r->ur_value))
		D_GOTO(err_r, rc);

	value = umem_id2ptr(&tins->ti_umm, r->ur_value);
	memcpy(value, val->iov_buf, r->ur_value_size);

	rec->rec_mmid = rid;
	return 0;

err_r:
	umem_free(&tins->ti_umm, rid);
err:
	return rc;
}

static int
uv_rec_free(struct btr_instance *tins, struct btr_record *rec)
{
	struct uv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

	umem_free(&tins->ti_umm, r->ur_value);
	umem_free(&tins->ti_umm, rec->rec_mmid);
	return 0;
}

static int
uv_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key, daos_iov_t *val)
{
	/* TODO: What sanity checks are required for key and val? */

	if (key != NULL) {
		if (key->iov_buf == NULL)
			key->iov_buf = rec->rec_hkey;
		else if (key->iov_buf_len >= sizeof(uuid_t))
			memcpy(key->iov_buf, rec->rec_hkey, sizeof(uuid_t));

		key->iov_len = sizeof(uuid_t);
	}

	if (val != NULL) {
		struct uv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
		void	       *value = umem_id2ptr(&tins->ti_umm, r->ur_value);

		if (val->iov_buf == NULL)
			val->iov_buf = value;
		else if (r->ur_value_size <= val->iov_buf_len)
			memcpy(val->iov_buf, value, r->ur_value_size);

		val->iov_len = r->ur_value_size;
	}

	return 0;
}

static int
uv_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	struct uv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	void	       *v;

	umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));

	if (r->ur_value_buf_size < val->iov_len) {
		umem_id_t vid;

		vid = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMMID_IS_NULL(vid))
			return -DER_NOMEM;

		umem_free(&tins->ti_umm, r->ur_value);

		r->ur_value = vid;
		r->ur_value_buf_size = val->iov_len;
	} else {
		umem_tx_add(&tins->ti_umm, r->ur_value, val->iov_len);
	}

	v = umem_id2ptr(&tins->ti_umm, r->ur_value);
	memcpy(v, val->iov_buf, val->iov_len);
	r->ur_value_size = val->iov_len;
	return 0;
}

static char *
uv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct uv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	void	       *value = umem_id2ptr(&tins->ti_umm, r->ur_value);

	if (leaf)
		snprintf(buf, buf_len, DF_UUID":%p+"DF_U64"("DF_U64")",
			 DP_UUID(rec->rec_hkey), value, r->ur_value_size,
			 r->ur_value_buf_size);
	else
		snprintf(buf, buf_len, DF_UUID, DP_UUID(rec->rec_hkey));

	return buf;
}

static btr_ops_t uv_ops = {
	.to_hkey_gen	= uv_hkey_gen,
	.to_hkey_size	= uv_hkey_size,
	.to_rec_alloc	= uv_rec_alloc,
	.to_rec_free	= uv_rec_free,
	.to_rec_fetch	= uv_rec_fetch,
	.to_rec_update	= uv_rec_update,
	.to_rec_string	= uv_rec_string
};

int
dsms_kvs_uv_update(daos_handle_t kvsh, const uuid_t uuid, const void *value,
		   size_t size)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

	val.iov_buf = (void *)value;
	val.iov_buf_len = size;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update "DF_UUID": %d\n", DP_UUID(uuid), rc);

	return rc;
}

int
dsms_kvs_uv_lookup(daos_handle_t kvsh, const uuid_t uuid, void *value,
		   size_t size)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

	val.iov_buf = value;
	val.iov_buf_len = size;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_lookup(kvsh, &key, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find "DF_UUID"\n",
				DP_UUID(uuid));
		else
			D_ERROR("failed to look up "DF_UUID": %d\n",
				DP_UUID(uuid), rc);
		return rc;
	}

	return 0;
}

int
dsms_kvs_uv_delete(daos_handle_t kvsh, const uuid_t uuid)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

	rc = dbtree_delete(kvsh, &key);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find "DF_UUID"\n",
				DP_UUID(uuid));
		else
			D_ERROR("failed to delete "DF_UUID": %d\n",
				DP_UUID(uuid), rc);
	}

	return rc;
}

/*
 * Create a KVS in place as the value for "uuid". If "kvsh_new" is not NULL,
 * then leave the new KVS open and return the handle in "*kvsh_new"; otherwise,
 * close the new KVS. "class", "feats", and "order" are passed to
 * dbtree_create_inplace() unchanged.
 */
int
dsms_kvs_uv_create_kvs(daos_handle_t kvsh, const uuid_t uuid,
		       unsigned int class, uint64_t feats, unsigned int order,
		       PMEMobjpool *mp, daos_handle_t *kvsh_new)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

	rc = create_kvs(kvsh, &key, class, feats, order, mp, kvsh_new);
	if (rc != 0)
		D_ERROR("failed to create "DF_UUID": %d\n", DP_UUID(uuid), rc);

	return rc;
}

int
dsms_kvs_uv_open_kvs(daos_handle_t kvsh, const uuid_t uuid, PMEMobjpool *mp,
		     daos_handle_t *kvsh_child)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

	rc = open_kvs(kvsh, &key, mp, kvsh_child);
	if (rc != 0)
		D_ERROR("failed to open "DF_UUID": %d\n", DP_UUID(uuid), rc);

	return rc;
}

/* Destroy a KVS in place as the value for "uuid". */
int
dsms_kvs_uv_destroy_kvs(daos_handle_t kvsh, const uuid_t uuid, PMEMobjpool *mp)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

	rc = destroy_kvs(kvsh, &key, mp);
	if (rc != 0)
		D_ERROR("failed to destroy "DF_UUID": %d\n", DP_UUID(uuid), rc);

	return rc;
}

/*
 * KVS_EC: epoch-counter pairs
 *
 * An epoch is a uint64_t integer. A counter is a uint64_t integer too. Epochs
 * are numerically ordered.
 */

struct ec_rec {
	uint64_t	er_counter;
#if !HAVE_DBTREE_DELETE
	uint64_t	er_deleted;
#endif
};

static void
ec_hkey_gen(struct btr_instance *tins, daos_iov_t *key, void *hkey)
{
	*(uint64_t *)hkey = *(uint64_t *)key->iov_buf;
}

static int
ec_hkey_size(struct btr_instance *tins)
{
	return sizeof(uint64_t);
}

static int
ec_rec_alloc(struct btr_instance *tins, daos_iov_t *key, daos_iov_t *val,
	       struct btr_record *rec)
{
	struct ec_rec  *r;
	umem_id_t	rid;
	int		rc = -DER_INVAL;

	if (key->iov_len != sizeof(uint64_t) ||
	    key->iov_buf_len < key->iov_len || val->iov_len == 0 ||
	    val->iov_buf_len < val->iov_len)
		return rc;

	rid = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMMID_IS_NULL(rid))
		return rc;

	r = umem_id2ptr(&tins->ti_umm, rid);
	r->er_counter = *(uint64_t *)val->iov_buf;

	rec->rec_mmid = rid;
	return 0;
}

static int
ec_rec_free(struct btr_instance *tins, struct btr_record *rec)
{
	umem_free(&tins->ti_umm, rec->rec_mmid);
	return 0;
}

static int
ec_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     daos_iov_t *key, daos_iov_t *val)
{
	/* TODO: What sanity checks are required for key and val? */

	if (key != NULL) {
		if (key->iov_buf == NULL)
			key->iov_buf = rec->rec_hkey;
		else if (key->iov_buf_len >= sizeof(uint64_t))
			memcpy(key->iov_buf, rec->rec_hkey, sizeof(uint64_t));

		key->iov_len = sizeof(uint64_t);
	}

	if (val != NULL) {
		struct ec_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

#if !HAVE_DBTREE_DELETE
		if (r->er_deleted) {
			val->iov_len = 0;
			return 0;
		}
#endif

		if (val->iov_buf == NULL)
			val->iov_buf = &r->er_counter;
		else if (val->iov_buf_len >= sizeof(r->er_counter))
			*(uint64_t *)val->iov_buf = r->er_counter;

		val->iov_len = sizeof(r->er_counter);
	}

	return 0;
}

static int
ec_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	struct ec_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);

#if HAVE_DBTREE_DELETE
	if (val->iov_len != sizeof(r->er_counter))
#else
	if (val->iov_len != sizeof(r->er_counter) && val->iov_len != 0)
#endif
		return -DER_INVAL;

	umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));
#if HAVE_DBTREE_DELETE
	r->er_counter = *(uint64_t *)val->iov_buf;
#else
	if (val->iov_len == 0) {
		r->er_counter = 0;
		r->er_deleted = 1;
	} else {
		r->er_counter = *(uint64_t *)val->iov_buf;
		r->er_deleted = 0;
	}
#endif
	return 0;
}

static char *
ec_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct ec_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	uint64_t	e;

	memcpy(&e, rec->rec_hkey, sizeof(e));

	if (leaf)
		snprintf(buf, buf_len, DF_U64":"DF_U64, e, r->er_counter);
	else
		snprintf(buf, buf_len, DF_U64, e);

	return buf;
}

static btr_ops_t ec_ops = {
	.to_hkey_gen	= ec_hkey_gen,
	.to_hkey_size	= ec_hkey_size,
	.to_rec_alloc	= ec_rec_alloc,
	.to_rec_free	= ec_rec_free,
	.to_rec_fetch	= ec_rec_fetch,
	.to_rec_update	= ec_rec_update,
	.to_rec_string	= ec_rec_string
};

int
dsms_kvs_ec_update(daos_handle_t kvsh, uint64_t epoch, const uint64_t *count)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	D_DEBUG(DF_DSMS, "updating "DF_U64":"DF_U64"\n", epoch, *count);

	key.iov_buf = &epoch;
	key.iov_buf_len = sizeof(epoch);
	key.iov_len = key.iov_buf_len;

	val.iov_buf = (void *)count;
	val.iov_buf_len = sizeof(*count);
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update "DF_U64": %d\n", epoch, rc);

	return rc;
}

int
dsms_kvs_ec_lookup(daos_handle_t kvsh, uint64_t epoch, uint64_t *count)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	key.iov_buf = &epoch;
	key.iov_buf_len = sizeof(epoch);
	key.iov_len = key.iov_buf_len;

	val.iov_buf = count;
	val.iov_buf_len = sizeof(*count);
	val.iov_len = val.iov_buf_len;

	rc = dbtree_lookup(kvsh, &key, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find "DF_U64"\n", epoch);
		else
			D_ERROR("failed to look up "DF_U64": %d\n", epoch, rc);
	}

	return rc;
}

#if HAVE_DBTREE_DELETE
int
dsms_kvs_ec_fetch(daos_handle_t kvsh, dbtree_probe_opc_t opc,
		  const uint64_t *epoch_in, uint64_t *epoch_out,
		  uint64_t *count)
{
	daos_iov_t	key_in;
	daos_iov_t	key_out;
	daos_iov_t	val;
	int		rc;

	key_in.iov_buf = (void *)epoch_in;
	key_in.iov_buf_len = sizeof(*epoch_in);
	key_in.iov_len = key_in.iov_buf_len;

	key_out.iov_buf = epoch_out;
	key_out.iov_buf_len = sizeof(*epoch_out);
	key_out.iov_len = key_out.iov_buf_len;

	val.iov_buf = count;
	val.iov_buf_len = sizeof(*count);
	val.iov_len = val.iov_buf_len;

	rc = dbtree_fetch(kvsh, opc, &key_in, &key_out, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find opc=%d in="DF_U64"\n",
				opc, epoch_in == NULL ? -1 : *epoch_in);
		else
			D_ERROR("failed to fetch opc=%d in="DF_U64": %d\n", opc,
				epoch_in == NULL ? -1 : *epoch_in, rc);
	}

	return rc;
}
#else
int
dsms_kvs_ec_fetch(daos_handle_t kvsh, dbtree_probe_opc_t opc,
		  const uint64_t *epoch_in, uint64_t *epoch_out,
		  uint64_t *count)
{
	daos_iov_t	key_in;
	daos_iov_t	key_out;
	daos_iov_t	val;
	uint64_t	e;
	uint64_t	c;
	daos_handle_t	iter;
	int		rc;

	D_ASSERT(opc == BTR_PROBE_FIRST || opc == BTR_PROBE_LAST ||
		 opc == BTR_PROBE_EQ || opc == BTR_PROBE_GE ||
		 opc == BTR_PROBE_LE);
	D_ASSERT(opc == BTR_PROBE_FIRST || opc == BTR_PROBE_LAST ||
		 epoch_in != NULL);

	rc = dbtree_iter_prepare(kvsh, 0 /* options */, &iter);
	if (rc != 0) {
		D_ERROR("failed to prepare iterator for opc=%d in="DF_U64
			": %d\n", opc, epoch_in == NULL ? -1 : *epoch_in, rc);
		D_GOTO(out, rc);
	}

	key_in.iov_buf = (void *)epoch_in;
	key_in.iov_buf_len = sizeof(*epoch_in);
	key_in.iov_len = key_in.iov_buf_len;

	rc = dbtree_iter_probe(iter, opc, &key_in, NULL /* anchor */);
	if (rc != 0) {
		if (rc != -DER_NONEXIST)
			D_ERROR("failed to probe opc=%d in="DF_U64": %d\n", opc,
				epoch_in == NULL ? -1 : *epoch_in, rc);
		D_GOTO(out_iter, rc);
	}

	key_out.iov_buf = &e;
	key_out.iov_buf_len = sizeof(e);
	key_out.iov_len = key_out.iov_buf_len;

	val.iov_buf = &c;
	val.iov_buf_len = sizeof(c);
	val.iov_len = val.iov_buf_len;

	rc = dbtree_iter_fetch(iter, &key_out, &val, NULL /* anchor */);
	if (rc != 0) {
		D_ERROR("failed to fetch opc=%d in="DF_U64": %d\n", opc,
			epoch_in == NULL ? -1 : *epoch_in, rc);
		D_GOTO(out_iter, rc);
	}

	/* Done if the record is not deleted. */
	if (val.iov_len != 0)
		D_GOTO(out_fill, rc = 0);

	D_DEBUG(DF_DSMS, "found deleted opc=%d in="DF_U64"\n", opc,
		epoch_in == NULL ? -1 : *epoch_in);

	if (opc == BTR_PROBE_EQ)
		D_GOTO(out_iter, rc = -DER_NONEXIST);

	for (;;) {
		D_DEBUG(DF_DSMS, "moving to next/prev\n");

		if (opc == BTR_PROBE_FIRST || opc == BTR_PROBE_GE)
			rc = dbtree_iter_next(iter);
		else
			rc = dbtree_iter_prev(iter);
		if (rc != 0) {
			if (rc != -DER_NONEXIST)
				D_ERROR("failed to move iterator for opc=%d in="
					DF_U64": %d\n", opc,
					epoch_in == NULL ?
					-1 : *epoch_in, rc);
			D_GOTO(out_iter, rc);
		}

		rc = dbtree_iter_fetch(iter, &key_out, &val,
				       NULL /* anchor */);
		if (rc != 0) {
			D_ERROR("failed to fetch opc=%d in="DF_U64
				": %d\n", opc,
				epoch_in == NULL ? -1 : *epoch_in, rc);
			D_GOTO(out_iter, rc);
		}

		/* Done if the record is not deleted. */
		if (val.iov_len != 0)
			break;
	}

out_fill:
	if (epoch_out != NULL)
		*epoch_out = e;
	if (count != NULL)
		*count = c;
out_iter:
	dbtree_iter_finish(iter);
out:
	if (rc == -DER_NONEXIST)
		D_DEBUG(DF_DSMS, "cannot find opc=%d in="DF_U64"\n", opc,
			epoch_in == NULL ? -1 : *epoch_in);
	else if (rc == 0)
		D_DEBUG(DF_DSMS, "found opc=%d in="DF_U64": "DF_U64":"DF_U64
			"\n", opc, epoch_in == NULL ? -1 : *epoch_in, e, c);
	return rc;
}
#endif

int
dsms_kvs_ec_delete(daos_handle_t kvsh, uint64_t epoch)
{
	daos_iov_t	key;
	int		rc;

	D_DEBUG(DF_DSMS, "deleting "DF_U64"\n", epoch);

	key.iov_buf = &epoch;
	key.iov_buf_len = sizeof(epoch);
	key.iov_len = key.iov_buf_len;

	rc = dbtree_delete(kvsh, &key);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, "cannot find "DF_U64"\n", epoch);
		else
			D_ERROR("failed to delete "DF_U64": %d\n", epoch, rc);
	}

	return rc;
}

static DAOS_LIST_HEAD(mpool_cache);
static pthread_mutex_t mpool_cache_lock;

static int
mpool_init(const uuid_t pool_uuid, struct mpool *mp)
{
	PMEMoid			sb_oid;
	struct superblock      *sb;
	struct umem_attr	uma;
	char		       *path;
	int			rc;

	DAOS_INIT_LIST_HEAD(&mp->mp_entry);
	uuid_copy(mp->mp_uuid, pool_uuid);
	mp->mp_ref = 1;

	rc = pthread_mutex_init(&mp->mp_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize mp_lock: %d\n", rc);
		D_GOTO(err, rc = -DER_NOMEM);
	}

	rc = dmgs_tgt_file(pool_uuid, DSM_META_FILE, NULL, &path);
	if (rc != 0) {
		D_ERROR("failed to lookup path: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	mp->mp_pmem = pmemobj_open(path, MPOOL_LAYOUT);
	if (mp->mp_pmem == NULL) {
		if (errno == ENOENT)
			D_DEBUG(DF_DSMS, "cannot find %s: %d\n", path, errno);
		else
			D_ERROR("failed to open %s: %d\n", path, errno);
		D_GOTO(err_path, rc = -DER_NONEXIST);
	}

	sb_oid = pmemobj_root(mp->mp_pmem, sizeof(*sb));
	sb = pmemobj_direct(sb_oid);

	if (sb->s_magic != SUPERBLOCK_MAGIC) {
		D_ERROR("found invalid superblock magic: "DF_X64"\n",
			sb->s_magic);
		D_GOTO(err_pmem, rc = -DER_NONEXIST);
	}

	D_DEBUG(DF_DSMS, DF_UUID": opening root kvs\n", DP_UUID(pool_uuid));

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = mp->mp_pmem;
	rc = dbtree_open_inplace(&sb->s_root, &uma, &mp->mp_root);
	if (rc != 0) {
		D_ERROR("failed to open root kvs: %d\n", rc);
		D_GOTO(err_pmem, rc);
	}

	return 0;

err_pmem:
	pmemobj_close(mp->mp_pmem);
err_path:
	free(path);
err_lock:
	pthread_mutex_destroy(&mp->mp_lock);
err:
	return rc;
}

void
dsms_mpool_get(struct mpool *mpool)
{
	pthread_mutex_lock(&mpool->mp_lock);
	mpool->mp_ref++;
	pthread_mutex_unlock(&mpool->mp_lock);
}

int
dsms_mpool_lookup(const uuid_t pool_uuid, struct mpool **mpool)
{
	struct mpool   *mp;
	int		rc = 0;

	D_DEBUG(DF_DSMS, DF_UUID": looking up\n", DP_UUID(pool_uuid));

	pthread_mutex_lock(&mpool_cache_lock);

	daos_list_for_each_entry(mp, &mpool_cache, mp_entry) {
		if (uuid_compare(mp->mp_uuid, pool_uuid) == 0) {
			D_DEBUG(DF_DSMS, DF_UUID": found %p\n",
				DP_UUID(pool_uuid), mp);
			dsms_mpool_get(mp);
			*mpool = mp;
			D_GOTO(out, rc = 0);
		}
	}

	D_ALLOC_PTR(mp);
	if (mp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = mpool_init(pool_uuid, mp);
	if (rc != 0) {
		D_FREE_PTR(mp);
		D_GOTO(out, rc);
	}

	daos_list_add(&mp->mp_entry, &mpool_cache);
	D_DEBUG(DF_DSMS, DF_UUID": allocated %p\n", DP_UUID(pool_uuid), mp);

	*mpool = mp;
out:
	pthread_mutex_unlock(&mpool_cache_lock);
	return rc;
}

void
dsms_mpool_put(struct mpool *mpool)
{
	int is_last_ref = 0;

	pthread_mutex_lock(&mpool->mp_lock);
	D_ASSERTF(mpool->mp_ref > 0, "%d\n", mpool->mp_ref);
	if (mpool->mp_ref == 1)
		is_last_ref = 1;
	else
		mpool->mp_ref--;
	pthread_mutex_unlock(&mpool->mp_lock);

	if (is_last_ref) {
		pthread_mutex_lock(&mpool_cache_lock);
		pthread_mutex_lock(&mpool->mp_lock);
		mpool->mp_ref--;
		if (mpool->mp_ref == 0) {
			D_DEBUG(DF_DSMS, "freeing mpool %p\n", mpool);
			dbtree_close(mpool->mp_root);
			pmemobj_close(mpool->mp_pmem);
			pthread_mutex_destroy(&mpool->mp_lock);
			daos_list_del(&mpool->mp_entry);
			D_FREE_PTR(mpool);
		} else {
			pthread_mutex_unlock(&mpool->mp_lock);
		}
		pthread_mutex_unlock(&mpool_cache_lock);
	}
}

int
dsms_storage_init(void)
{
	int rc;

	rc = dbtree_class_register(KVS_NV, 0 /* feats */, &nv_ops);
	if (rc != 0) {
		D_ERROR("failed to register KVS_NV: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(KVS_UV, 0 /* feats */, &uv_ops);
	if (rc != 0) {
		D_ERROR("failed to register KVS_UV: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(KVS_EC, 0 /* feats */, &ec_ops);
	if (rc != 0) {
		D_ERROR("failed to register KVS_EC: %d\n", rc);
		return rc;
	}

	rc = pthread_mutex_init(&mpool_cache_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize mpool cache lock: %d\n", rc);
		rc = -DER_NOMEM;
	}

	return rc;
}

void
dsms_storage_fini(void)
{
	/*
	 * Because there isn't a dbtree_class_unregister() at the moment, we
	 * cannot safely unload this module in theory.
	 */

	pthread_mutex_destroy(&mpool_cache_lock);
}
