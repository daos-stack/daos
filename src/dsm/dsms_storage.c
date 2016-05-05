/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015, 2016 Intel Corporation.
 */
/*
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

#define HAS_DBTREE_DELETE 0

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
	uint64_t       *hash = hkey;
	char	       *name = key->iov_buf;
	int		i;

	/*
	 * TODO: This function should be allowed to return an error
	 * code.
	 */
	assert(key->iov_len <= key->iov_buf_len);

	/* djb2 */
	*hash = 5381;
	for (i = 0; i < key->iov_len; i++) {
		if (name[i] == '\0')
			break;

		*hash = ((*hash << 5) + *hash) + name[i];
	}

	/* The key may not be terminated by '\0'. See the TODO above. */
	assert(i < key->iov_len);
}

static int
nv_hkey_size(struct btr_instance *tins)
{
	return sizeof(uint64_t);
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

	/* TODO: Add transactional considerations. */

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

	/* TODO: Add transactional considerations. */

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
	void	       *value = umem_id2ptr(&tins->ti_umm, r->nr_value);

	/* TODO: Add transactional considerations. */

	if (r->nr_value_buf_size < val->iov_len) {
		umem_free(&tins->ti_umm, r->nr_value);

		r->nr_value_size = 0;
		r->nr_value_buf_size = 0;

		r->nr_value = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMMID_IS_NULL(r->nr_value))
			return -DER_NOMEM;

		r->nr_value_buf_size = val->iov_len;
	}

	memcpy(value, val->iov_buf, val->iov_len);
	r->nr_value_size = val->iov_len;
	return 0;
}

static char *
nv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct nv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	void	       *value = umem_id2ptr(&tins->ti_umm, r->nr_value);

	if (leaf)
		snprintf(buf, buf_len, "\"%s\":%p+"DF_U64"("DF_U64")",
			 r->nr_name, value, r->nr_value_size,
			 r->nr_value_buf_size);
	else
		snprintf(buf, buf_len, DF_U64, (uint64_t)rec->rec_hkey);

	return buf;
}

static btr_ops_t nv_ops = {
	.to_hkey_gen	= nv_hkey_gen,
	.to_hkey_size	= nv_hkey_size,
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
	if (rc != 0)
		D_ERROR("failed to look up \"%s\": %d\n", name, rc);

#if !HAS_DBTREE_DELETE
	if (val.iov_len == 0)
		return -DER_NONEXIST;
#endif

	return rc;
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

	val.iov_buf = NULL;
	val.iov_buf_len = 0;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_lookup(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to look up \"%s\": %d\n", name, rc);

#if !HAS_DBTREE_DELETE
	if (val.iov_len == 0) {
		D_DEBUG(DF_DSMS, "\"%s\" treated as nonexistent\n", name);
		return -DER_NONEXIST;
	}
#endif

	*value = val.iov_buf;
	*size = val.iov_len;
	return rc;
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

#if HAS_DBTREE_DELETE
	rc = dbtree_delete(kvsh, &key);
	if (rc != 0)
		D_ERROR("failed to delete \"%s\": %d\n", name, rc);
#else
	daos_iov_t	val;

	val.iov_buf = NULL;
	val.iov_buf_len = 0;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update \"%s\": %d\n", name, rc);
#endif

	return rc;
}

/*
 * KVS_UV: UUID-value pairs
 *
 * A UUID is of the uuid_t type. A value is a variable-size blob. UUIDs are
 * unordered.
 */

struct uv_rec {
	umem_id_t	nr_value;
	uint64_t	nr_value_size;
	uint64_t	nr_value_buf_size;
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

	/* TODO: Add transactional considerations. */

	if (key->iov_len != sizeof(uuid_t) || key->iov_buf_len < key->iov_len ||
	    val->iov_len == 0 || val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc);

	rid = umem_zalloc(&tins->ti_umm, sizeof(*r));
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

	/* TODO: Add transactional considerations. */

	umem_free(&tins->ti_umm, r->nr_value);
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
		void	       *value = umem_id2ptr(&tins->ti_umm, r->nr_value);

		if (val->iov_buf == NULL)
			val->iov_buf = value;
		else if (r->nr_value_size <= val->iov_buf_len)
			memcpy(val->iov_buf, value, r->nr_value_size);

		val->iov_len = r->nr_value_size;
	}

	return 0;
}

static int
uv_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	struct uv_rec  *r = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	void	       *value;

	/* TODO: Add transactional considerations. */

	if (r->nr_value_buf_size < val->iov_len) {
		umem_free(&tins->ti_umm, r->nr_value);

		r->nr_value_size = 0;
		r->nr_value_buf_size = 0;

		r->nr_value = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMMID_IS_NULL(r->nr_value))
			return -DER_NOMEM;

		r->nr_value_buf_size = val->iov_len;
	}

	value = umem_id2ptr(&tins->ti_umm, r->nr_value);
	memcpy(value, val->iov_buf, val->iov_len);
	r->nr_value_size = val->iov_len;
	return 0;
}

static btr_ops_t uv_ops = {
	.to_hkey_gen	= uv_hkey_gen,
	.to_hkey_size	= uv_hkey_size,
	.to_rec_alloc	= uv_rec_alloc,
	.to_rec_free	= uv_rec_free,
	.to_rec_fetch	= uv_rec_fetch,
	.to_rec_update	= uv_rec_update
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
		D_ERROR("failed to update: %d\n", rc);

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
	if (rc != 0)
		D_ERROR("failed to look up: %d\n", rc);

#if !HAS_DBTREE_DELETE
	if (val.iov_len == 0) {
		D_DEBUG(DF_DSMS, DF_UUID" treated as nonexistent\n",
			DP_UUID(uuid));
		return -DER_NONEXIST;
	}
#endif

	return rc;
}

int
dsms_kvs_uv_delete(daos_handle_t kvsh, const uuid_t uuid)
{
	daos_iov_t	key;
	int		rc;

	key.iov_buf = (void *)uuid;
	key.iov_buf_len = sizeof(uuid_t);
	key.iov_len = key.iov_buf_len;

#if HAS_DBTREE_DELETE
	rc = dbtree_delete(kvsh, &key);
	if (rc != 0)
		D_ERROR("failed to delete: %d\n", rc);
#else
	daos_iov_t	val;

	val.iov_buf = NULL;
	val.iov_buf_len = 0;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update: %d\n", rc);
#endif

	return rc;
}

/* TODO: Implement KVS_EC. */

static DAOS_LIST_HEAD(mpool_cache);
static pthread_mutex_t mpool_cache_lock;

static int
mpool_init(const uuid_t pool_uuid, struct mpool *mp)
{
	char			path[4096];
	PMEMoid			sb_oid;
	struct superblock      *sb;
	struct umem_attr	uma;
	int			rc;

	DAOS_INIT_LIST_HEAD(&mp->mp_entry);
	uuid_copy(mp->mp_uuid, pool_uuid);
	mp->mp_ref = 1;

	rc = pthread_mutex_init(&mp->mp_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize mp_lock: %d\n", rc);
		D_GOTO(err, rc = -DER_NOMEM);
	}

	print_meta_path("." /* TODO: dmg */, pool_uuid, path, sizeof(path));

	mp->mp_pmem = pmemobj_open(path, MPOOL_LAYOUT);
	if (mp->mp_pmem == NULL) {
		D_ERROR("failed to open %s: %d\n", path, errno);
		D_GOTO(err_lock, rc = -DER_NONEXIST);
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
