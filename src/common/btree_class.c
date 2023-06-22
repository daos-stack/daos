/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dbtree Classes
 *
 * This file implements dbtree classes for different key and value types.
 */
#define D_LOGFAC	DD_FAC(tree)

#include <string.h>
#include <daos/btree_class.h>
#include <daos/dtx.h>

enum {
	BTR_NO_TX,		/**< no transaction support */
	BTR_IN_TX,		/**< already in a transaction */
	BTR_SUPPORT_TX,		/**< can support transaction */
};

static int
btr_check_tx(struct btr_attr *attr, struct umem_instance *umm)
{
	if (attr->ba_uma.uma_id != UMEM_CLASS_PMEM &&
	    attr->ba_uma.uma_id != UMEM_CLASS_ADMEM)
		return BTR_NO_TX;

#ifdef DAOS_PMEM_BUILD
	if (umm && umem_tx_inprogress(umm))
		return BTR_IN_TX;
#endif
	return BTR_SUPPORT_TX;
}

static int
lookup_ptr(daos_handle_t tree, d_iov_t *key, d_iov_t *val)
{
	int rc;

	d_iov_set(val, NULL /* buf */, 0 /* size */);

	rc = dbtree_lookup(tree, key, val);
	if (rc != 0)
		return rc;

	return 0;
}

static int
create_tree(daos_handle_t tree, d_iov_t *key, unsigned int class,
	    uint64_t feats, unsigned int order, daos_handle_t *tree_new)
{
	struct btr_root		buf;
	struct btr_attr		attr;
	d_iov_t			val;
	daos_handle_t		h;
	struct umem_instance	*umm;
	int			rc;

	rc = dbtree_query(tree, &attr, NULL);
	if (rc != 0)
		return rc;

	umm = btr_hdl2umm(tree);
	D_ASSERT(btr_check_tx(&attr, umm) == BTR_NO_TX ||
		 btr_check_tx(&attr, umm) == BTR_IN_TX);

	memset(&buf, 0, sizeof(buf));
	d_iov_set(&val, &buf, sizeof(buf));

	rc = dbtree_update(tree, key, &val);
	if (rc != 0)
		return rc;

	rc = lookup_ptr(tree, key, &val);
	if (rc != 0)
		return rc;

	rc = dbtree_create_inplace(class, feats, order, &attr.ba_uma,
				   val.iov_buf, &h);
	if (rc != 0)
		return rc;

	if (tree_new == NULL)
		dbtree_close(h);
	else
		*tree_new = h;

	return 0;
}

static int
open_tree(daos_handle_t tree, d_iov_t *key, struct btr_attr *attr,
	  daos_handle_t *tree_child)
{
	struct btr_attr		bta;
	d_iov_t		val;
	int			rc;

	rc = dbtree_query(tree, &bta, NULL);
	if (rc != 0)
		return rc;

	rc = lookup_ptr(tree, key, &val);
	if (rc != 0)
		return rc;

	rc = dbtree_open_inplace(val.iov_buf, &bta.ba_uma, tree_child);
	if (rc != 0)
		return rc;

	if (attr != NULL)
		*attr = bta;

	return 0;
}

static int
destroy_tree(daos_handle_t tree, d_iov_t *key)
{
	daos_handle_t		hdl;
	struct btr_attr		attr;
	struct umem_instance	umm;
	int			rc;

	rc = open_tree(tree, key, &attr, &hdl);
	if (rc != 0)
		return rc;

	umem_class_init(&attr.ba_uma, &umm);

	if (btr_check_tx(&attr, &umm) == BTR_NO_TX) {
		rc = dbtree_destroy(hdl, NULL);
		if (rc != 0) {
			dbtree_close(hdl);
			D_GOTO(out, rc);
		}
		rc = dbtree_delete(tree, BTR_PROBE_EQ, key, NULL);
		if (rc != 0)
			D_GOTO(out, rc);
	} else {
		volatile daos_handle_t	hdl_tmp = hdl;
		volatile int		rc_tmp = 0;

		rc_tmp = umem_tx_begin(&umm, NULL);
		if (rc_tmp != 0 && daos_handle_is_valid(hdl_tmp)) {
			dbtree_close(hdl_tmp);
			return rc_tmp;
		}

		rc_tmp = dbtree_destroy(hdl_tmp, NULL);
		if (rc_tmp == 0) {
			hdl_tmp = DAOS_HDL_INVAL;
			rc_tmp = dbtree_delete(tree, BTR_PROBE_EQ, key, NULL);
		}

		if (daos_handle_is_valid(hdl_tmp))
			dbtree_close(hdl_tmp);

		if (rc_tmp != 0)
			return umem_tx_abort(&umm, rc_tmp);

		return umem_tx_commit(&umm);
	}
out:
	return rc;
}

/*
 * KVS_NV: name-value pairs
 *
 * A name is a variable-length, '\0'-terminated string. A value is a
 * variable-size blob. Names are unordered.
 */

struct nv_rec {
	umem_off_t	nr_value;
	uint64_t	nr_value_size;
	uint64_t	nr_value_buf_size;
	uint64_t	nr_name_size;	/* strlen(name) + 1 */
	char		nr_name[];
};

static void
nv_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	const char	*key = key_iov->iov_buf;
	size_t		key_size = key_iov->iov_len;
	uint32_t	*hash = hkey;

	/*
	 * TODO: This function should be allowed to return an error
	 * code.
	 */
	D_ASSERT(key_iov->iov_len <= key_iov->iov_buf_len);

	*hash = d_hash_string_u32(key, key_size);
}

static int
nv_hkey_size(void)
{
	return sizeof(uint32_t);
}

static int
nv_key_cmp(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key)
{
	struct nv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	return dbtree_key_cmp_rc(
		memcmp((const void *)r->nr_name, (const void *)key->iov_buf,
			key->iov_len));
}

static void
nv_key_encode(struct btr_instance *tins, d_iov_t *key,
	      daos_anchor_t *anchor)
{
	if (key)
		embedded_key_encode(key, anchor);
}

static void
nv_key_decode(struct btr_instance *tins, d_iov_t *key,
	      daos_anchor_t *anchor)
{
	embedded_key_decode(key, anchor);
}

static int
nv_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct nv_rec  *r;
	umem_off_t	roff;
	void	       *value;
	size_t		name_len;
	int		rc = -DER_INVAL;

	if (key->iov_len == 0 || key->iov_buf_len < key->iov_len ||
	    val->iov_len == 0 || val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc);

	name_len = key->iov_len;

	rc = tins->ti_umm.umm_nospc_rc;

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r) + name_len);
	if (UMOFF_IS_NULL(roff))
		D_GOTO(err, rc = tins->ti_umm.umm_nospc_rc);

	r = umem_off2ptr(&tins->ti_umm, roff);
	r->nr_value_size = val->iov_len;
	r->nr_value_buf_size = r->nr_value_size;

	r->nr_value = umem_alloc(&tins->ti_umm, r->nr_value_buf_size);
	if (UMOFF_IS_NULL(r->nr_value))
		D_GOTO(err_r, rc = tins->ti_umm.umm_nospc_rc);

	value = umem_off2ptr(&tins->ti_umm, r->nr_value);
	memcpy(value, val->iov_buf, r->nr_value_size);

	r->nr_name_size = name_len;
	memcpy(r->nr_name, key->iov_buf, r->nr_name_size);

	rec->rec_off = roff;
	return 0;

err_r:
	umem_free(&tins->ti_umm, roff);
err:
	return rc;
}

static int
nv_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct nv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	umem_free(&tins->ti_umm, r->nr_value);
	umem_free(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
nv_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     d_iov_t *key, d_iov_t *val)
{
	struct nv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	/* TODO: What sanity checks are required for key and val? */

	if (key != NULL) {
		if (key->iov_buf == NULL) {
			key->iov_buf = r->nr_name;
			key->iov_buf_len = r->nr_name_size;
		} else if (r->nr_name_size <= key->iov_buf_len)
			memcpy(key->iov_buf, r->nr_name, r->nr_name_size);

		key->iov_len = r->nr_name_size;
	}

	if (val != NULL) {
		void   *value = umem_off2ptr(&tins->ti_umm, r->nr_value);

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
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct nv_rec	*r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void		*v;
	int		rc;

	rc = umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));
	if (rc != 0)
		return rc;

	if (r->nr_value_buf_size < val->iov_len) {
		umem_off_t voff;

		voff = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMOFF_IS_NULL(voff))
			return tins->ti_umm.umm_nospc_rc;

		umem_free(&tins->ti_umm, r->nr_value);

		r->nr_value = voff;
		r->nr_value_buf_size = val->iov_len;
	} else {
		rc = umem_tx_add(&tins->ti_umm, r->nr_value, val->iov_len);
		if (rc != 0)
			return rc;
	}

	v = umem_off2ptr(&tins->ti_umm, r->nr_value);
	memcpy(v, val->iov_buf, val->iov_len);
	r->nr_value_size = val->iov_len;
	return 0;
}

static char *
nv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct nv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void	       *value = umem_off2ptr(&tins->ti_umm, r->nr_value);
	uint32_t       *hkey = (uint32_t *)rec->rec_hkey;

	if (leaf)
		snprintf(buf, buf_len, "\"%p\":%p+"DF_U64"("DF_U64")",
			 r->nr_name, value, r->nr_value_size,
			 r->nr_value_buf_size);
	else
		snprintf(buf, buf_len, "%u", *hkey);

	return buf;
}

btr_ops_t dbtree_nv_ops = {
	.to_hkey_gen	= nv_hkey_gen,
	.to_hkey_size	= nv_hkey_size,
	.to_key_cmp	= nv_key_cmp,
	.to_key_encode	= nv_key_encode,
	.to_key_decode	= nv_key_decode,
	.to_rec_alloc	= nv_rec_alloc,
	.to_rec_free	= nv_rec_free,
	.to_rec_fetch	= nv_rec_fetch,
	.to_rec_update	= nv_rec_update,
	.to_rec_string	= nv_rec_string
};

int
dbtree_nv_update(daos_handle_t tree, const void *key, size_t key_size,
		 const void *value, size_t size)
{
	d_iov_t	key_iov;
	d_iov_t	val;
	int		rc;

	D_DEBUG(DB_TRACE, "updating \"%s\":%p+%zu\n", (char *)key, key,
		key_size);

	d_iov_set(&key_iov, (void *)key, key_size);
	d_iov_set(&val, (void *)value, size);

	rc = dbtree_update(tree, &key_iov, &val);
	if (rc != 0)
		D_ERROR("failed to update \"%s\": "DF_RC"\n", (char *)key,
			DP_RC(rc));

	return rc;
}

int
dbtree_nv_lookup(daos_handle_t tree, const void *key, size_t key_size,
		 void *value, size_t size)
{
	d_iov_t	key_iov;
	d_iov_t	val;
	int		rc;

	D_DEBUG(DB_TRACE, "looking up \"%s\"\n", (char *)key);

	d_iov_set(&key_iov, (void *)key, key_size);
	d_iov_set(&val, value, size);

	rc = dbtree_lookup(tree, &key_iov, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find \"%s\"\n", (char *)key);
		else
			D_ERROR("failed to look up \"%s\": %d\n", (char *)key,
				rc);
		return rc;
	}

	return 0;
}

/*
 * Output the address and the size of the value, instead of copying to volatile
 * memory.
 */
int
dbtree_nv_lookup_ptr(daos_handle_t tree, const void *key, size_t key_size,
		     void **value, size_t *size)
{
	d_iov_t	key_iov;
	d_iov_t	val;
	int		rc;

	D_DEBUG(DB_TRACE, "looking up \"%s\" ptr\n", (char *)key);

	d_iov_set(&key_iov, (void *)key, key_size);

	rc = lookup_ptr(tree, &key_iov, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find \"%s\"\n", (char *)key);
		else
			D_ERROR("failed to look up \"%s\": %d\n", (char *)key,
				rc);
		return rc;
	}

	*value = val.iov_buf;
	*size = val.iov_len;
	return 0;
}

int
dbtree_nv_delete(daos_handle_t tree, const void *key, size_t key_size)
{
	d_iov_t	key_iov;
	int		rc;

	D_DEBUG(DB_TRACE, "deleting \"%s\"\n", (char *)key);

	d_iov_set(&key_iov, (void *)key, key_size);

	rc = dbtree_delete(tree, BTR_PROBE_EQ, &key_iov, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find \"%s\"\n", (char *)key);
		else
			D_ERROR("failed to delete \"%s\": %d\n", (char *)key,
				rc);
	}

	return rc;
}

/*
 * Create a KVS in place as the value for "name". If "tree_new" is not NULL,
 * then leave the new KVS open and return the handle in "*tree_new"; otherwise,
 * close the new KVS. "class", "feats", and "order" are passed to
 * dbtree_create_inplace() unchanged.
 */
int
dbtree_nv_create_tree(daos_handle_t tree, const void *key, size_t key_size,
		      unsigned int class, uint64_t feats, unsigned int order,
		      daos_handle_t *tree_new)
{
	d_iov_t	key_iov;
	int		rc;

	d_iov_set(&key_iov, (void *)key, key_size);

	rc = create_tree(tree, &key_iov, class, feats, order, tree_new);
	if (rc != 0)
		D_ERROR("failed to create \"%s\": "DF_RC"\n", (char *)key,
			DP_RC(rc));

	return rc;
}

int
dbtree_nv_open_tree(daos_handle_t tree, const void *key, size_t key_size,
		    daos_handle_t *tree_child)
{
	d_iov_t	key_iov;
	int		rc;

	d_iov_set(&key_iov, (void *)key, key_size);

	rc = open_tree(tree, &key_iov, NULL, tree_child);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find \"%s\"\n", (char *)key);
		else
			D_ERROR("failed to open \"%s\": "DF_RC"\n", (char *)key,
				DP_RC(rc));
	}

	return rc;
}

/* Destroy a KVS in place as the value for "name". */
int
dbtree_nv_destroy_tree(daos_handle_t tree, const void *key, size_t key_size)
{
	d_iov_t	key_iov;
	int		rc;

	d_iov_set(&key_iov, (void *)key, key_size);

	rc = destroy_tree(tree, &key_iov);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find \"%s\"\n", (char *)key);
		else
			D_ERROR("failed to destroy \"%s\": %d\n", (char *)key,
				rc);
	}

	return rc;
}

/*
 * KVS_UV: UUID-value pairs
 *
 * A UUID is of the uuid_t type. A value is a variable-size blob. UUIDs are
 * unordered.
 */

struct uv_rec {
	umem_off_t	ur_value;
	uint64_t	ur_value_size;
	uint64_t	ur_value_buf_size;
};

static void
uv_hkey_gen(struct btr_instance *tins, d_iov_t *key, void *hkey)
{
	uuid_copy(*(uuid_t *)hkey, *(uuid_t *)key->iov_buf);
}

static int
uv_hkey_size(void)
{
	return sizeof(uuid_t);
}

static int
uv_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct uv_rec  *r;
	umem_off_t	roff;
	void	       *value;
	int		rc = -DER_INVAL;

	if (key->iov_len != sizeof(uuid_t) || key->iov_buf_len < key->iov_len ||
	    val->iov_len == 0 || val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc);

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMOFF_IS_NULL(roff))
		D_GOTO(err, rc = tins->ti_umm.umm_nospc_rc);

	r = umem_off2ptr(&tins->ti_umm, roff);
	r->ur_value_size = val->iov_len;
	r->ur_value_buf_size = r->ur_value_size;

	r->ur_value = umem_alloc(&tins->ti_umm, r->ur_value_buf_size);
	if (UMOFF_IS_NULL(r->ur_value))
		D_GOTO(err_r, rc = tins->ti_umm.umm_nospc_rc);

	value = umem_off2ptr(&tins->ti_umm, r->ur_value);
	memcpy(value, val->iov_buf, r->ur_value_size);

	rec->rec_off = roff;
	return 0;

err_r:
	umem_free(&tins->ti_umm, roff);
err:
	return rc;
}

static int
uv_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct uv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	umem_free(&tins->ti_umm, r->ur_value);
	umem_free(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
uv_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     d_iov_t *key, d_iov_t *val)
{
	/* TODO: What sanity checks are required for key and val? */

	if (key != NULL) {
		if (key->iov_buf == NULL) {
			key->iov_buf = rec->rec_hkey;
			key->iov_buf_len = sizeof(uuid_t);
		} else if (key->iov_buf_len >= sizeof(uuid_t)) {
			uuid_copy(*(uuid_t *)key->iov_buf,
				  *(uuid_t *)rec->rec_hkey);
		}
		key->iov_len = sizeof(uuid_t);
	}

	if (val != NULL) {
		struct uv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
		void	       *value = umem_off2ptr(&tins->ti_umm,
						     r->ur_value);

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
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct uv_rec	*r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void		*v;
	int		rc;

	rc = umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));
	if (rc != 0)
		return rc;

	if (r->ur_value_buf_size < val->iov_len) {
		umem_off_t voff;

		voff = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMOFF_IS_NULL(voff))
			return tins->ti_umm.umm_nospc_rc;

		umem_free(&tins->ti_umm, r->ur_value);

		r->ur_value = voff;
		r->ur_value_buf_size = val->iov_len;
	} else {
		rc = umem_tx_add(&tins->ti_umm, r->ur_value, val->iov_len);
		if (rc != 0)
			return rc;
	}

	v = umem_off2ptr(&tins->ti_umm, r->ur_value);
	memcpy(v, val->iov_buf, val->iov_len);
	r->ur_value_size = val->iov_len;
	return 0;
}

static char *
uv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct uv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void	       *value = umem_off2ptr(&tins->ti_umm, r->ur_value);


	if (leaf)
		snprintf(buf, buf_len, DF_UUID":%p+"DF_U64"("DF_U64")",
			 DP_UUID(rec->rec_hkey), value, r->ur_value_size,
			 r->ur_value_buf_size);
	else
		snprintf(buf, buf_len, DF_UUID, DP_UUID(rec->rec_hkey));

	return buf;
}

static int
uv_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	return dbtree_key_cmp_rc(
		uuid_compare(*(uuid_t *)rec->rec_hkey, *(uuid_t *)hkey));
}

btr_ops_t dbtree_uv_ops = {
	.to_hkey_gen	= uv_hkey_gen,
	.to_hkey_size	= uv_hkey_size,
	.to_hkey_cmp	= uv_hkey_cmp,
	.to_rec_alloc	= uv_rec_alloc,
	.to_rec_free	= uv_rec_free,
	.to_rec_fetch	= uv_rec_fetch,
	.to_rec_update	= uv_rec_update,
	.to_rec_string	= uv_rec_string
};

int
dbtree_uv_update(daos_handle_t tree, const uuid_t uuid, const void *value,
		 size_t size)
{
	d_iov_t	key;
	d_iov_t	val;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));
	d_iov_set(&val, (void *)value, size);

	rc = dbtree_update(tree, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update "DF_UUID": "DF_RC"\n", DP_UUID(uuid),
			DP_RC(rc));

	return rc;
}

int
dbtree_uv_lookup(daos_handle_t tree, const uuid_t uuid, void *value,
		 size_t size)
{
	d_iov_t	key;
	d_iov_t	val;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));
	d_iov_set(&val, value, size);

	rc = dbtree_lookup(tree, &key, &val);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find "DF_UUID"\n",
				DP_UUID(uuid));
		else
			D_ERROR("failed to look up "DF_UUID": %d\n",
				DP_UUID(uuid), rc);
		return rc;
	}

	return 0;
}

int
dbtree_uv_fetch(daos_handle_t tree, dbtree_probe_opc_t opc,
		const uuid_t uuid_in, uuid_t uuid_out, void *value, size_t size)
{
	d_iov_t	key_in;
	d_iov_t	key_out;
	d_iov_t	val;
	int		rc;

	d_iov_set(&key_in, (void *)uuid_in, sizeof(uuid_t));
	d_iov_set(&key_out, uuid_out, sizeof(uuid_t));
	d_iov_set(&val, value, size);

	rc = dbtree_fetch(tree, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out,
			  &val);
	if (rc == -DER_NONEXIST)
		D_DEBUG(DB_TRACE, "cannot find opc=%d in="DF_UUID"\n", opc,
			DP_UUID(uuid_in));
	else if (rc != 0)
		D_ERROR("failed to fetch opc=%d in="DF_UUID": %d\n", opc,
			DP_UUID(uuid_in), rc);
	return rc;
}

int
dbtree_uv_delete(daos_handle_t tree, const uuid_t uuid)
{
	d_iov_t	key;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));

	rc = dbtree_delete(tree, BTR_PROBE_EQ, &key, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find "DF_UUID"\n",
				DP_UUID(uuid));
		else
			D_ERROR("failed to delete "DF_UUID": %d\n",
				DP_UUID(uuid), rc);
	}

	return rc;
}

/*
 * Create a KVS in place as the value for "uuid". If "tree_new" is not NULL,
 * then leave the new KVS open and return the handle in "*tree_new"; otherwise,
 * close the new KVS. "class", "feats", and "order" are passed to
 * dbtree_create_inplace() unchanged.
 */
int
dbtree_uv_create_tree(daos_handle_t tree, const uuid_t uuid, unsigned int class,
		      uint64_t feats, unsigned int order,
		      daos_handle_t *tree_new)
{
	d_iov_t	key;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));

	rc = create_tree(tree, &key, class, feats, order, tree_new);
	if (rc != 0)
		D_ERROR("failed to create "DF_UUID": "DF_RC"\n", DP_UUID(uuid),
			DP_RC(rc));

	return rc;
}

int
dbtree_uv_open_tree(daos_handle_t tree, const uuid_t uuid,
		    daos_handle_t *tree_child)
{
	d_iov_t	key;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));

	rc = open_tree(tree, &key, NULL, tree_child);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find "DF_UUID"\n",
				DP_UUID(uuid));
		else
			D_ERROR("failed to open "DF_UUID": %d\n", DP_UUID(uuid),
				rc);
	}

	return rc;
}

/* Destroy a KVS in place as the value for "uuid". */
int
dbtree_uv_destroy_tree(daos_handle_t tree, const uuid_t uuid)
{
	d_iov_t	key;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));

	rc = destroy_tree(tree, &key);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "cannot find "DF_UUID"\n",
				DP_UUID(uuid));
		else
			D_ERROR("failed to destroy "DF_UUID": %d\n",
				DP_UUID(uuid), rc);
	}

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
};

static int
ec_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct ec_rec  *r;
	umem_off_t	roff;
	int		rc = -DER_INVAL;

	if (key->iov_len != sizeof(uint64_t) ||
	    key->iov_buf_len < key->iov_len || val->iov_len == 0 ||
	    val->iov_buf_len < val->iov_len)
		return rc;

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMOFF_IS_NULL(roff))
		return tins->ti_umm.umm_nospc_rc;

	r = umem_off2ptr(&tins->ti_umm, roff);
	r->er_counter = *(uint64_t *)val->iov_buf;

	rec->rec_off = roff;
	return 0;
}

static int
ec_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	umem_free(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
ec_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	     d_iov_t *key, d_iov_t *val)
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
		struct ec_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

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
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct ec_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	int rc;

	if (val->iov_len != sizeof(r->er_counter))
		return -DER_INVAL;

	rc = umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));
	if (rc)
		return rc;

	r->er_counter = *(uint64_t *)val->iov_buf;
	return 0;
}

static char *
ec_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct ec_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	uint64_t	e;

	memcpy(&e, rec->rec_hkey, sizeof(e));

	if (leaf)
		snprintf(buf, buf_len, DF_U64":"DF_U64, e, r->er_counter);
	else
		snprintf(buf, buf_len, DF_U64, e);

	return buf;
}

btr_ops_t dbtree_ec_ops = {
	.to_rec_alloc	= ec_rec_alloc,
	.to_rec_free	= ec_rec_free,
	.to_rec_fetch	= ec_rec_fetch,
	.to_rec_update	= ec_rec_update,
	.to_rec_string	= ec_rec_string
};

int
dbtree_ec_update(daos_handle_t tree, uint64_t epoch, const uint64_t *count)
{
	d_iov_t	key;
	d_iov_t	val;
	int		rc;

	D_DEBUG(DB_TRACE, "updating "DF_U64":"DF_U64"\n", epoch, *count);

	d_iov_set(&key, &epoch, sizeof(epoch));
	d_iov_set(&val, (void *)count, sizeof(*count));

	rc = dbtree_update(tree, &key, &val);
	if (rc != 0)
		D_ERROR("failed to update "DF_U64": "DF_RC"\n", epoch,
			DP_RC(rc));

	return rc;
}

int
dbtree_ec_lookup(daos_handle_t tree, uint64_t epoch, uint64_t *count)
{
	d_iov_t	key;
	d_iov_t	val;
	int		rc;

	d_iov_set(&key, &epoch, sizeof(epoch));
	d_iov_set(&val, (void *)count, sizeof(*count));

	rc = dbtree_lookup(tree, &key, &val);
	if (rc == -DER_NONEXIST)
		D_DEBUG(DB_TRACE, "cannot find "DF_U64"\n", epoch);
	else if (rc != 0)
		D_ERROR("failed to look up "DF_U64": "DF_RC"\n", epoch,
			DP_RC(rc));

	return rc;
}

int
dbtree_ec_fetch(daos_handle_t tree, dbtree_probe_opc_t opc,
		const uint64_t *epoch_in, uint64_t *epoch_out, uint64_t *count)
{
	d_iov_t	key_in;
	d_iov_t	key_out;
	d_iov_t	val;
	int		rc;

	d_iov_set(&key_in, (void *)epoch_in, sizeof(*epoch_in));
	d_iov_set(&key_out, epoch_out, sizeof(*epoch_out));
	d_iov_set(&val, (void *)count, sizeof(*count));

	rc = dbtree_fetch(tree, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out,
			  &val);
	if (rc == -DER_NONEXIST)
		D_DEBUG(DB_TRACE, "cannot find opc=%d in="DF_U64"\n",
			opc, epoch_in == NULL ? -1 : *epoch_in);
	else if (rc != 0)
		D_ERROR("failed to fetch opc=%d in="DF_U64": %d\n", opc,
			epoch_in == NULL ? -1 : *epoch_in, rc);
	return rc;
}

int
dbtree_ec_delete(daos_handle_t tree, uint64_t epoch)
{
	d_iov_t	key;
	int		rc;

	D_DEBUG(DB_TRACE, "deleting "DF_U64"\n", epoch);

	d_iov_set(&key, &epoch, sizeof(epoch));

	rc = dbtree_delete(tree, BTR_PROBE_EQ, &key, NULL);
	if (rc == -DER_NONEXIST)
		D_DEBUG(DB_TRACE, "cannot find "DF_U64"\n", epoch);
	else if (rc != 0)
		D_ERROR("failed to delete "DF_U64": "DF_RC"\n", epoch,
			DP_RC(rc));

	return rc;
}

/*
 * DBTREE_CLASS_KV
 */

struct kv_rec {
	umem_off_t	kr_value;
	uint64_t	kr_value_len;	/* length of value */
	uint64_t	kr_value_cap;	/* capacity of value buffer */
	uint64_t	kr_key_len;	/* length of key */
	uint8_t		kr_key[];
};

static void
kv_hkey_gen(struct btr_instance *tins, d_iov_t *key, void *hkey)
{
	uint64_t *hash = hkey;

	D_ASSERTF(key->iov_len > 0, DF_U64" > 0\n", key->iov_len);
	D_ASSERTF(key->iov_len <= key->iov_buf_len, DF_U64" <= "DF_U64"\n",
		  key->iov_len, key->iov_buf_len);
	*hash = d_hash_murmur64(key->iov_buf, key->iov_len, 609815U);
}

static int
kv_hkey_size(void)
{
	return sizeof(uint64_t);
}

static int
kv_key_cmp(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key)
{
	struct kv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	int		rc;

	rc = memcmp(r->kr_key, key->iov_buf, min(r->kr_key_len, key->iov_len));
	if (rc == 0)
		rc = r->kr_key_len - key->iov_len;
	return dbtree_key_cmp_rc(rc);
}

static int
kv_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	     struct btr_record *rec, d_iov_t *val_out)
{
	struct kv_rec  *r;
	umem_off_t	roff;
	void	       *v;
	int		rc;

	if (key->iov_len == 0 || key->iov_buf_len < key->iov_len ||
	    val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc = -DER_INVAL);

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r) + key->iov_len);
	if (UMOFF_IS_NULL(roff))
		D_GOTO(err, rc = tins->ti_umm.umm_nospc_rc);
	r = umem_off2ptr(&tins->ti_umm, roff);

	r->kr_value_len = val->iov_len;
	r->kr_value_cap = r->kr_value_len;
	r->kr_value = umem_alloc(&tins->ti_umm, r->kr_value_cap);
	if (UMOFF_IS_NULL(r->kr_value))
		D_GOTO(err_r, rc = tins->ti_umm.umm_nospc_rc);
	v = umem_off2ptr(&tins->ti_umm, r->kr_value);
	memcpy(v, val->iov_buf, r->kr_value_len);

	r->kr_key_len = key->iov_len;
	memcpy(r->kr_key, key->iov_buf, r->kr_key_len);

	rec->rec_off = roff;
	return 0;

err_r:
	umem_free(&tins->ti_umm, roff);
err:
	return rc;
}

static int
kv_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct kv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	umem_free(&tins->ti_umm, r->kr_value);
	umem_free(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
kv_rec_fetch(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key,
	     d_iov_t *val)
{
	struct kv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	if (key != NULL) {
		if (key->iov_buf == NULL)
			key->iov_buf = r->kr_key;
		else if (r->kr_key_len <= key->iov_buf_len)
			memcpy(key->iov_buf, r->kr_key, r->kr_key_len);
		key->iov_len = r->kr_key_len;
	}
	if (val != NULL) {
		void *v = umem_off2ptr(&tins->ti_umm, r->kr_value);

		if (val->iov_buf == NULL)
			val->iov_buf = v;
		else if (r->kr_value_len <= val->iov_buf_len)
			memcpy(val->iov_buf, v, r->kr_value_len);
		val->iov_len = r->kr_value_len;
	}
	return 0;
}

static int
kv_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct kv_rec	*r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void		*v;
	int		rc;

	rc = umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));
	if (rc != 0)
		return rc;

	if (r->kr_value_cap < val->iov_len) {
		umem_off_t voff;

		voff = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMOFF_IS_NULL(voff))
			return tins->ti_umm.umm_nospc_rc;
		umem_free(&tins->ti_umm, r->kr_value);
		r->kr_value = voff;
		r->kr_value_cap = val->iov_len;
	} else {
		rc = umem_tx_add(&tins->ti_umm, r->kr_value, val->iov_len);
		if (rc != 0)
			return rc;
	}
	v = umem_off2ptr(&tins->ti_umm, r->kr_value);

	memcpy(v, val->iov_buf, val->iov_len);
	r->kr_value_len = val->iov_len;
	return 0;
}

static char *
kv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct kv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void	       *v = umem_off2ptr(&tins->ti_umm, r->kr_value);
	uint64_t       *hkey = (uint64_t *)rec->rec_hkey;

	if (leaf)
		snprintf(buf, buf_len, "%p+"DF_U64":%p+"DF_U64"("DF_U64")",
			 r->kr_key, r->kr_key_len, v, r->kr_value_len,
			 r->kr_value_cap);
	else
		snprintf(buf, buf_len, DF_U64, *hkey);
	return buf;
}

btr_ops_t dbtree_kv_ops = {
	.to_hkey_gen	= kv_hkey_gen,
	.to_hkey_size	= kv_hkey_size,
	.to_key_cmp	= kv_key_cmp,
	.to_rec_alloc	= kv_rec_alloc,
	.to_rec_free	= kv_rec_free,
	.to_rec_fetch	= kv_rec_fetch,
	.to_rec_update	= kv_rec_update,
	.to_rec_string	= kv_rec_string
};

/*
 * DBTREE_CLASS_IV
 */

struct iv_rec {
	umem_off_t	ir_value;
	uint64_t	ir_value_len;	/* length of value */
	uint64_t	ir_value_cap;	/* capacity of value buffer */
};

static void *
iv_key_get(struct btr_instance *tins, struct btr_record *rec, void *value)
{
	/** For direct key, the first field of the value must be the key */
	if (tins->ti_root->tr_feats & BTR_FEAT_DIRECT_KEY)
		return value;
	else
		return rec->rec_hkey;
}

static int
iv_key_cmp_common(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key, void *value,
		  uint64_t len)
{
	void            *v = iv_key_get(tins, rec, value);
	uint64_t	 a, b;

	D_ASSERT(key->iov_buf != NULL);
	b = *(uint64_t *)key->iov_buf;

	D_ASSERT(len >= sizeof(uint64_t));
	D_ASSERT(v != NULL);
	a = *(uint64_t *)v;

	return (a < b) ? BTR_CMP_LT : ((a > b) ? BTR_CMP_GT : BTR_CMP_EQ);
}

static int
iv_key_cmp(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key)
{
	struct iv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	return iv_key_cmp_common(tins, rec, key, umem_off2ptr(&tins->ti_umm, r->ir_value),
				 r->ir_value_len);
}

static void
iv_key_encode(struct btr_instance *tins, d_iov_t *key, daos_anchor_t *anchor)
{
	D_ASSERT(key->iov_len >= sizeof(uint64_t));
	memcpy(&anchor->da_buf[0], key->iov_buf, sizeof(uint64_t));
}

static void
iv_key_decode(struct btr_instance *tins, d_iov_t *key, daos_anchor_t *anchor)
{
	key->iov_buf = &anchor->da_buf[0];
	key->iov_buf_len = sizeof(uint64_t);
	key->iov_len = key->iov_buf_len;
}

static int
iv_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	     struct btr_record *rec, d_iov_t *val_out)
{
	struct iv_rec  *r;
	umem_off_t	roff;
	void	       *v;
	int		rc;

	if (key->iov_len != sizeof(uint64_t) ||
	    key->iov_buf_len < key->iov_len || val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc = -DER_INVAL);

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMOFF_IS_NULL(roff))
		D_GOTO(err, rc = tins->ti_umm.umm_nospc_rc);
	r = umem_off2ptr(&tins->ti_umm, roff);

	r->ir_value_len = val->iov_len;
	r->ir_value_cap = r->ir_value_len;
	r->ir_value = umem_alloc(&tins->ti_umm, r->ir_value_cap);
	if (UMOFF_IS_NULL(r->ir_value))
		D_GOTO(err_r, rc = tins->ti_umm.umm_nospc_rc);
	v = umem_off2ptr(&tins->ti_umm, r->ir_value);
	memcpy(v, val->iov_buf, r->ir_value_len);

	rec->rec_off = roff;

	if (val_out) {
		D_ASSERT(val_out->iov_buf == NULL);
		val_out->iov_buf = v;
		val_out->iov_len = r->ir_value_len;
	}
	return 0;

err_r:
	umem_free(&tins->ti_umm, roff);
err:
	return rc;
}

static int
iv_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct iv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	int            rc;

	rc = umem_free(&tins->ti_umm, r->ir_value);
	if (rc != 0)
		return rc;
	return umem_free(&tins->ti_umm, rec->rec_off);
}

static int
iv_rec_fetch(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key,
	     d_iov_t *val)
{
	struct iv_rec  *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void	       *v = umem_off2ptr(&tins->ti_umm, r->ir_value);

	if (key != NULL) {
		void *k = iv_key_get(tins, rec, v);

		if (key->iov_buf == NULL)
			key->iov_buf = k;
		else if (key->iov_buf_len >= sizeof(uint64_t))
			memcpy(key->iov_buf, k, sizeof(uint64_t));
		key->iov_len = sizeof(uint64_t);
	}

	if (val != NULL) {
		if (val->iov_buf == NULL)
			val->iov_buf = v;
		else if (r->ir_value_len <= val->iov_buf_len)
			memcpy(val->iov_buf, v, r->ir_value_len);
		val->iov_len = r->ir_value_len;
	}
	return 0;
}

static int
iv_rec_update(struct btr_instance *tins, struct btr_record *rec,
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct iv_rec	*r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void		*v;
	int		rc;

	D_ASSERTF(key->iov_len == sizeof(uint64_t), DF_U64"\n", key->iov_len);
	rc = umem_tx_add_ptr(&tins->ti_umm, r, sizeof(*r));
	if (rc != 0)
		return rc;

	if (r->ir_value_cap < val->iov_len) {
		umem_off_t voff;

		voff = umem_alloc(&tins->ti_umm, val->iov_len);
		if (UMOFF_IS_NULL(voff))
			return tins->ti_umm.umm_nospc_rc;
		umem_free(&tins->ti_umm, r->ir_value);
		r->ir_value = voff;
		r->ir_value_cap = val->iov_len;
	} else {
		rc = umem_tx_add(&tins->ti_umm, r->ir_value, val->iov_len);
		if (rc != 0)
			return rc;
	}
	v = umem_off2ptr(&tins->ti_umm, r->ir_value);
	memcpy(v, val->iov_buf, val->iov_len);
	r->ir_value_len = val->iov_len;

	if (val_out) {
		D_ASSERT(val_out->iov_buf == NULL);
		val_out->iov_buf = v;
		val_out->iov_len = r->ir_value_len;
	}
	return 0;
}

static char *
iv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
	      char *buf, int buf_len)
{
	struct btr_record *key_rec = rec;
	struct iv_rec     *r       = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	struct iv_rec     *key_ir  = r;
	void	       *v = umem_off2ptr(&tins->ti_umm, r->ir_value);
	void              *k;
	uint64_t	key;

	if (!leaf && (tins->ti_root->tr_feats & BTR_FEAT_DIRECT_KEY)) {
		struct btr_node	*nd = umem_off2ptr(&tins->ti_umm, rec->rec_node[0]);
		char		*addr = (char *)&nd[1];

		key_rec = (struct btr_record *)&addr[0];
		key_ir  = umem_off2ptr(&tins->ti_umm, key_rec->rec_off);
	}

	k = iv_key_get(tins, key_rec, umem_off2ptr(&tins->ti_umm, key_ir->ir_value));

	memcpy(&key, k, sizeof(key));
	if (leaf)
		snprintf(buf, buf_len, DF_U64":%p+"DF_U64"("DF_U64")", key, v,
			 r->ir_value_len, r->ir_value_cap);
	else
		snprintf(buf, buf_len, DF_U64, key);
	return buf;
}

static int
iv_key_msize(int alloc_overhead)
{
	return alloc_overhead + sizeof(struct iv_rec);
}

static int
iv_hkey_size(void)
{
	return sizeof(uint64_t);
}

btr_ops_t dbtree_iv_ops = {
	.to_rec_msize	= iv_key_msize,
	.to_hkey_size	= iv_hkey_size,
	.to_key_cmp	= iv_key_cmp,
	.to_key_encode	= iv_key_encode,
	.to_key_decode	= iv_key_decode,
	.to_rec_alloc	= iv_rec_alloc,
	.to_rec_free	= iv_rec_free,
	.to_rec_fetch	= iv_rec_fetch,
	.to_rec_update	= iv_rec_update,
	.to_rec_string	= iv_rec_string
};

/*
 * DBTREE_CLASS_IFV
 */

struct ifv_rec {
	uint64_t ir_value_len; /* length of value */
	char     ir_value[0];  /* Inline value */
};

static int
ifv_key_cmp(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key)
{
	struct ifv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	return iv_key_cmp_common(tins, rec, key, &r->ir_value[0], r->ir_value_len);
}

static int
ifv_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val, struct btr_record *rec,
	      d_iov_t *val_out)
{
	struct ifv_rec *r;
	umem_off_t      roff;
	int             rc;

	if (key->iov_len != sizeof(uint64_t) || key->iov_buf_len < key->iov_len ||
	    val->iov_buf_len < val->iov_len)
		D_GOTO(err, rc = -DER_INVAL);

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r) + val->iov_len);
	if (UMOFF_IS_NULL(roff))
		D_GOTO(err, rc = tins->ti_umm.umm_nospc_rc);
	r = umem_off2ptr(&tins->ti_umm, roff);

	r->ir_value_len = val->iov_len;
	memcpy(&r->ir_value[0], val->iov_buf, r->ir_value_len);

	rec->rec_off = roff;

	if (val_out) {
		D_ASSERT(val_out->iov_buf == NULL);
		val_out->iov_buf = &r->ir_value[0];
		val_out->iov_len = r->ir_value_len;
	}
	return 0;
err:
	return rc;
}

static int
ifv_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	return umem_free(&tins->ti_umm, rec->rec_off);
}

static int
ifv_rec_fetch(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key, d_iov_t *val)
{
	struct ifv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	void           *v = &r->ir_value[0];

	if (key != NULL) {
		void *k = iv_key_get(tins, rec, v);

		if (key->iov_buf == NULL)
			key->iov_buf = k;
		else if (key->iov_buf_len >= sizeof(uint64_t))
			memcpy(key->iov_buf, k, sizeof(uint64_t));
		key->iov_len = sizeof(uint64_t);
	}

	if (val != NULL) {
		if (val->iov_buf == NULL)
			val->iov_buf = v;
		else if (r->ir_value_len <= val->iov_buf_len)
			memcpy(val->iov_buf, v, r->ir_value_len);
		val->iov_len = r->ir_value_len;
	}
	return 0;
}

static int
ifv_rec_update(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key, d_iov_t *val,
	       d_iov_t *val_out)
{
	struct ifv_rec *r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	int             rc;

	D_ASSERTF(key->iov_len == sizeof(uint64_t), DF_U64 "\n", key->iov_len);
	D_ASSERTF(r->ir_value_len == val->iov_len,
		  "Misuse of fixed size value btree class " DF_U64 " != " DF_U64 "\n",
		  r->ir_value_len, val->iov_len);

	rc = umem_tx_add_ptr(&tins->ti_umm, &r->ir_value[0], val->iov_len);
	if (rc != 0)
		return rc;

	memcpy(&r->ir_value[0], val->iov_buf, val->iov_len);

	if (val_out) {
		D_ASSERT(val_out->iov_buf == NULL);
		val_out->iov_buf = &r->ir_value[0];
		val_out->iov_len = r->ir_value_len;
	}
	return 0;
}

static char *
ifv_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf, char *buf, int buf_len)
{
	struct btr_record *key_rec = rec;
	struct ifv_rec    *r       = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	struct ifv_rec    *key_ir  = r;
	void              *v       = &r->ir_value[0];
	void              *k;
	uint64_t           key;

	if (!leaf && (tins->ti_root->tr_feats & BTR_FEAT_DIRECT_KEY)) {
		struct btr_node	*nd = umem_off2ptr(&tins->ti_umm, rec->rec_node[0]);
		char		*addr = (char *)&nd[1];

		key_rec = (struct btr_record *)&addr[0];
		key_ir  = umem_off2ptr(&tins->ti_umm, key_rec->rec_off);
	}

	k = iv_key_get(tins, key_rec, &key_ir->ir_value[0]);

	memcpy(&key, k, sizeof(key));
	if (leaf)
		snprintf(buf, buf_len, DF_U64 ":%p+" DF_U64, key, v, r->ir_value_len);
	else
		snprintf(buf, buf_len, DF_U64, key);
	return buf;
}

static int
ifv_key_msize(int alloc_overhead)
{
	return alloc_overhead + sizeof(struct ifv_rec);
}

btr_ops_t dbtree_ifv_ops = {.to_rec_msize  = ifv_key_msize,
			    .to_hkey_size  = iv_hkey_size,
			    .to_key_cmp    = ifv_key_cmp,
			    .to_key_encode = iv_key_encode,
			    .to_key_decode = iv_key_decode,
			    .to_rec_alloc  = ifv_rec_alloc,
			    .to_rec_free   = ifv_rec_free,
			    .to_rec_fetch  = ifv_rec_fetch,
			    .to_rec_update = ifv_rec_update,
			    .to_rec_string = ifv_rec_string};
