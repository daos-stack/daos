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
 * rdb: Utilities
 */

#define DDSUBSYS DDFAC(rdb)

#include <daos_srv/rdb.h>

#include "rdb_internal.h"

/*
 * daos_iov_t encoding/decoding utilities
 *
 * These functions convert between a daos_iov_t object and a byte stream in a
 * buffer. The format of such a byte stream is:
 *
 *   size_head (rdb_iov_size_t)
 *   data
 *   size_tail (rdb_iov_size_t)
 *
 * size_head and size_tail are identical, both indicates the size of data,
 * which equals iov_len of the corresponding daos_iov_t object. The two sizes
 * allows decoding from the tail as well as from the head.
 */

typedef uint32_t rdb_iov_size_t;

/* Maximal buf_len and len of an iov */
const daos_size_t rdb_iov_max = (rdb_iov_size_t)-1LL;

/* If buf is NULL, then just calculate and return the length required. */
size_t
rdb_encode_iov(const daos_iov_t *iov, void *buf)
{
	size_t len = sizeof(rdb_iov_size_t) * 2 + iov->iov_len;

	D__ASSERTF(iov->iov_len <= rdb_iov_max, DF_U64"\n", iov->iov_len);
	D__ASSERTF(iov->iov_buf_len <= rdb_iov_max, DF_U64"\n",
		  iov->iov_buf_len);
	if (buf != NULL) {
		void *p = buf;

		/* iov_len (head) */
		*(rdb_iov_size_t *)p = iov->iov_len;
		p += sizeof(rdb_iov_size_t);
		/* iov_buf */
		memcpy(p, iov->iov_buf, iov->iov_len);
		p += iov->iov_len;
		/* iov_len (tail) */
		*(rdb_iov_size_t *)p = iov->iov_len;
		p += sizeof(rdb_iov_size_t);
		D__ASSERTF(p - buf == len, "%td == %zu\n", p - buf, len);
	}
	return len;
}

/* Returns the number of bytes processed or -DER_IO if the content is bad. */
ssize_t
rdb_decode_iov(const void *buf, size_t len, daos_iov_t *iov)
{
	daos_iov_t	v = {};
	const void     *p = buf;

	/* iov_len (head) */
	if (p + sizeof(rdb_iov_size_t) > buf + len) {
		D_ERROR("truncated iov_len (head): %zu < %zu\n", len,
			sizeof(rdb_iov_size_t));
		return -DER_IO;
	}
	v.iov_len = *(const rdb_iov_size_t *)p;
	if (v.iov_len > rdb_iov_max) {
		D_ERROR("invalid iov_len (head): "DF_U64" > "DF_U64"\n",
			v.iov_len, rdb_iov_max);
		return -DER_IO;
	}
	v.iov_buf_len = v.iov_len;
	p += sizeof(rdb_iov_size_t);
	/* iov_buf */
	if (v.iov_len != 0) {
		if (p + v.iov_len > buf + len) {
			D_ERROR("truncated iov_buf: %zu < %zu\n", buf + len - p,
				v.iov_len);
			return -DER_IO;
		}
		v.iov_buf = (void *)p;
		p += v.iov_len;
	}
	/* iov_len (tail) */
	if (p + sizeof(rdb_iov_size_t) > buf + len) {
		D_ERROR("truncated iov_len (tail): %zu < %zu\n", buf + len - p,
			sizeof(rdb_iov_size_t));
		return -DER_IO;
	}
	if (*(const rdb_iov_size_t *)p != v.iov_len) {
		D_ERROR("inconsistent iov_lens: "DF_U64" != %u\n",
			v.iov_len, *(const rdb_iov_size_t *)p);
		return -DER_IO;
	}
	p += sizeof(rdb_iov_size_t);
	*iov = v;
	return p - buf;
}

/* Returns the number of bytes processed or -DER_IO if the content is bad. */
ssize_t
rdb_decode_iov_backward(const void *buf_end, size_t len, daos_iov_t *iov)
{
	daos_iov_t	v = {};
	const void     *p = buf_end;

	/* iov_len (tail) */
	if (p - sizeof(rdb_iov_size_t) < buf_end - len) {
		D_ERROR("truncated iov_len (tail): %zu < %zu\n", len,
			sizeof(rdb_iov_size_t));
		return -DER_IO;
	}
	p -= sizeof(rdb_iov_size_t);
	v.iov_len = *(const rdb_iov_size_t *)p;
	if (v.iov_len > rdb_iov_max) {
		D_ERROR("invalid iov_len (tail): "DF_U64" > "DF_U64"\n",
			v.iov_len, rdb_iov_max);
		return -DER_IO;
	}
	v.iov_buf_len = v.iov_len;
	/* iov_buf */
	if (v.iov_len != 0) {
		if (p - v.iov_len < buf_end - len) {
			D_ERROR("truncated iov_buf: %zu < %zu\n",
				p - (buf_end - len), v.iov_len);
			return -DER_IO;
		}
		p -= v.iov_len;
		v.iov_buf = (void *)p;
	}
	/* iov_len (head) */
	if (p - sizeof(rdb_iov_size_t) < buf_end - len) {
		D_ERROR("truncated iov_len (head): %zu < %zu\n",
			p - (buf_end - len), sizeof(rdb_iov_size_t));
		return -DER_IO;
	}
	p -= sizeof(rdb_iov_size_t);
	if (*(const rdb_iov_size_t *)p != v.iov_len) {
		D_ERROR("inconsistent iov_lens: "DF_U64" != %u\n",
			v.iov_len, *(const rdb_iov_size_t *)p);
		return -DER_IO;
	}
	*iov = v;
	return buf_end - p;
}

/*
 * Tree value utilities
 *
 * These functions handle tree values that represent other trees. Currently,
 * each such value is simply a btr_root object; we may want to include at least
 * a magic number later.
 */

static inline int
rdb_tree_class(enum rdb_kvs_class class)
{
	switch (class) {
	case RDB_KVS_GENERIC:
		return DBTREE_CLASS_KV;
	case RDB_KVS_INTEGER:
		return DBTREE_CLASS_IV;
	default:
		return -DER_IO;
	}
}

int
rdb_create_tree(daos_handle_t parent, daos_iov_t *key, enum rdb_kvs_class class,
		uint64_t feats, unsigned int order, daos_handle_t *child)
{
	daos_iov_t	value;
	struct btr_root	buf = {};
	struct btr_attr	attr;
	daos_handle_t	h;
	int		rc;

	/* Allocate the value and look up its address. */
	daos_iov_set(&value, &buf, sizeof(buf));
	rc = dbtree_update(parent, key, &value);
	if (rc != 0)
		return rc;
	daos_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = dbtree_lookup(parent, key, &value);
	if (rc != 0)
		return rc;

	/* Create the child tree in the value. */
	rc = dbtree_query(parent, &attr, NULL /* stat */);
	if (rc != 0)
		return rc;
	rc = rdb_tree_class(class);
	if (rc < 0)
		return rc;
	rc = dbtree_create_inplace(rc, feats, order, &attr.ba_uma,
				   value.iov_buf, &h);
	if (rc != 0)
		return rc;

	if (child == NULL)
		dbtree_close(h);
	else
		*child = h;
	return 0;
}

int
rdb_open_tree(daos_handle_t parent, daos_iov_t *key, daos_handle_t *child)
{
	daos_iov_t	value = {};
	struct btr_attr	attr;
	int		rc;

	/* Look up the address of the value. */
	rc = dbtree_lookup(parent, key, &value);
	if (rc != 0)
		return rc;

	/* Open the child tree in the value. */
	rc = dbtree_query(parent, &attr, NULL /* stat */);
	if (rc != 0)
		return rc;
	return dbtree_open_inplace(value.iov_buf, &attr.ba_uma, child);
}

int
rdb_destroy_tree(daos_handle_t parent, daos_iov_t *key)
{
	volatile daos_handle_t	hdl;
	daos_handle_t		hdl_tmp;
	struct btr_attr		attr;
	int			rc;

	/* Open the child tree. */
	rc = rdb_open_tree(parent, key, &hdl_tmp);
	if (rc != 0)
		return rc;
	hdl = hdl_tmp;

	rc = dbtree_query(parent, &attr, NULL /* stat */);
	if (rc != 0)
		return rc;
	TX_BEGIN(attr.ba_uma.uma_u.pmem_pool) {
		/* Destroy the child tree. */
		rc = dbtree_destroy(hdl);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		hdl = DAOS_HDL_INVAL;

		/* Delete the value. */
		rc = dbtree_delete(parent, key, NULL);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		if (!daos_handle_is_inval(hdl))
			dbtree_close(hdl);
		rc = umem_tx_errno(rc);
	} TX_END
	return rc;
}
