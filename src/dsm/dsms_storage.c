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
#include <daos_errno.h>
#include <daos/mem.h>
#include "dsms_internal.h"
#include "dsms_storage.h"

/*
 * KVS_NV: Name-value pairs
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

static btr_ops_t nv_ops = {
	.to_hkey_gen	= nv_hkey_gen,
	.to_hkey_size	= nv_hkey_size,
	.to_rec_alloc	= nv_rec_alloc,
	.to_rec_free	= nv_rec_free,
	.to_rec_fetch	= nv_rec_fetch,
	.to_rec_update	= nv_rec_update
};

int
dsms_kvs_nv_update(daos_handle_t kvsh, const char *name, const void *value,
		   size_t size)
{
	daos_iov_t	key;
	daos_iov_t	val;
	int		rc;

	key.iov_buf = (void *)name;
	key.iov_buf_len = strlen(name) + 1;
	key.iov_len = key.iov_buf_len;

	val.iov_buf = (void *)value;
	val.iov_buf_len = size;
	val.iov_len = val.iov_buf_len;

	rc = dbtree_update(kvsh, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update %s: %d\n", name, rc);

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

/* TODO: Implement KVS_EC. */

int
dsms_storage_init(void)
{
	int	rc;

	rc = dbtree_class_register(KVS_NV, 0 /* feats */, &nv_ops);
	if (rc != 0) {
		D_ERROR("failed to register KVS_NV: %d\n", rc);
		return rc;
	}

	rc = dbtree_class_register(KVS_UV, 0 /* feats */, &uv_ops);
	if (rc != 0)
		D_ERROR("failed to register KVS_UV: %d\n", rc);

	return rc;
}

void
dsms_storage_fini(void)
{
	/*
	 * Because there isn't a dbtree_class_unregister() at the moment, we
	 * cannot safely unload this module in theory.
	 */
}
