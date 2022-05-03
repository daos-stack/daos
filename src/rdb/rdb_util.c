/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rdb: Utilities
 */

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_types.h>
#include <daos_api.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include "rdb_internal.h"

/*
 * d_iov_t encoding/decoding utilities
 *
 * These functions convert between a d_iov_t object and a byte stream in a
 * buffer. The format of such a byte stream is:
 *
 *   size_head (rdb_iov_size_t)
 *   data
 *   size_tail (rdb_iov_size_t)
 *
 * size_head and size_tail are identical, both indicates the size of data,
 * which equals iov_len of the corresponding d_iov_t object. The two sizes
 * allows decoding from the tail as well as from the head.
 */

typedef uint32_t rdb_iov_size_t;

/* Maximal buf_len and len of an iov */
const daos_size_t rdb_iov_max = (rdb_iov_size_t)-1LL;

/* If buf is NULL, then just calculate and return the length required. */
size_t
rdb_encode_iov(const d_iov_t *iov, void *buf)
{
	size_t len = sizeof(rdb_iov_size_t) * 2 + iov->iov_len;

	D_ASSERTF(iov->iov_len <= rdb_iov_max, DF_U64"\n", iov->iov_len);
	D_ASSERTF(iov->iov_buf_len <= rdb_iov_max, DF_U64"\n",
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
		D_ASSERTF(p - buf == len, "%td == %zu\n", p - buf, len);
	}
	return len;
}

/* Returns the number of bytes processed or -DER_IO if the content is bad. */
ssize_t
rdb_decode_iov(const void *buf, size_t len, d_iov_t *iov)
{
	d_iov_t	v = {};
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
rdb_decode_iov_backward(const void *buf_end, size_t len, d_iov_t *iov)
{
	d_iov_t	v = {};
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

/* VOS access utilities */

void
rdb_oid_to_uoid(rdb_oid_t oid, daos_unit_oid_t *uoid)
{
	enum daos_otype_t type = DAOS_OT_MULTI_HASHED;

	uoid->id_pub.lo = oid & ~RDB_OID_CLASS_MASK;
	uoid->id_pub.hi = 0;
	uoid->id_shard  = 0;
	uoid->id_pad_32 = 0;
	/* Since we don't really use d-keys, use HASHED for both classes. */
	if ((oid & RDB_OID_CLASS_MASK) != RDB_OID_CLASS_GENERIC)
		type = DAOS_OT_AKEY_UINT64;

	daos_obj_set_oid(&uoid->id_pub, type, OR_RP_1, 1, 0);
}

void
rdb_anchor_set_zero(struct rdb_anchor *anchor)
{
	memset(&anchor->da_object, 0, sizeof(anchor->da_object));
	memset(&anchor->da_akey, 0, sizeof(anchor->da_akey));
}

void
rdb_anchor_set_eof(struct rdb_anchor *anchor)
{
	daos_anchor_set_eof(&anchor->da_object);
	daos_anchor_set_eof(&anchor->da_akey);
}

bool
rdb_anchor_is_eof(const struct rdb_anchor *anchor)
{
	return (daos_anchor_is_eof((daos_anchor_t *)&anchor->da_object) &&
		daos_anchor_is_eof((daos_anchor_t *)&anchor->da_akey));
}

void
rdb_anchor_to_hashes(const struct rdb_anchor *anchor, daos_anchor_t *obj_anchor,
		     daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor,
		     daos_anchor_t *ev_anchor, daos_anchor_t *sv_anchor)
{
	*obj_anchor = anchor->da_object;
	memset(dkey_anchor, 0, sizeof(*dkey_anchor));
	*akey_anchor = anchor->da_akey;
	memset(ev_anchor, 0, sizeof(*ev_anchor));
	memset(sv_anchor, 0, sizeof(*sv_anchor));
}

void
rdb_anchor_from_hashes(struct rdb_anchor *anchor, daos_anchor_t *obj_anchor,
		       daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor,
		       daos_anchor_t *ev_anchor, daos_anchor_t *sv_anchor)
{
	anchor->da_object = *obj_anchor;
	anchor->da_akey = *akey_anchor;
}

enum rdb_vos_op {
	RDB_VOS_QUERY,
	RDB_VOS_UPDATE
};

static void
rdb_vos_set_iods(enum rdb_vos_op op, int n, d_iov_t akeys[],
		 d_iov_t values[], daos_iod_t iods[])
{
	int i;

	for (i = 0; i < n; i++) {
		iods[i].iod_name = akeys[i];
		iods[i].iod_type = DAOS_IOD_SINGLE;
		iods[i].iod_size = 0;
		iods[i].iod_flags = 0;
		iods[i].iod_recxs = NULL;
		if (op == RDB_VOS_UPDATE) {
			D_ASSERT(values[i].iov_len > 0);
			iods[i].iod_size = values[i].iov_len;
		}
		iods[i].iod_nr = 1;
	}
}

static void
rdb_vos_set_sgls(enum rdb_vos_op op, int n, d_iov_t values[],
		 d_sg_list_t sgls[])
{
	int i;

	for (i = 0; i < n; i++) {
		sgls[i].sg_nr = 1;
		sgls[i].sg_nr_out = 0;
		if (op == RDB_VOS_UPDATE)
			D_ASSERT(values[i].iov_len > 0);
		sgls[i].sg_iovs = &values[i];
	}
}

static inline int
rdb_vos_fetch_check(d_iov_t *value, d_iov_t *value_orig)
{
	/*
	 * An empty value represents nonexistence. Keep the caller value intact
	 * in this case.
	 */
	if (value->iov_len == 0) {
		*value = *value_orig;
		return -DER_NONEXIST;
	}
	/*
	 * If the caller has an expected value length, check whether the actual
	 * value length matches it. (The != could be loosened to <, if
	 * necessary for compatibility reasons.)
	 */
	if (value_orig->iov_len > 0 && value->iov_len != value_orig->iov_len)
		return -DER_MISMATCH;
	return 0;
}

int
rdb_vos_fetch(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
	      daos_key_t *akey, d_iov_t *value)
{
	daos_unit_oid_t	uoid;
	daos_iod_t	iod;
	d_sg_list_t	sgl;
	d_iov_t	value_orig = *value;
	int		rc;

	rdb_oid_to_uoid(oid, &uoid);
	rdb_vos_set_iods(RDB_VOS_QUERY, 1 /* n */, akey, value, &iod);
	rdb_vos_set_sgls(RDB_VOS_QUERY, 1 /* n */, value, &sgl);
	rc = vos_obj_fetch(cont, uoid, epoch, 0 /* flags */, &rdb_dkey,
			   1 /* n */, &iod, &sgl);
	if (rc != 0)
		return rc;

	return rdb_vos_fetch_check(value, &value_orig);
}

/*
 * Fetch the persistent address of a value. Such an address will remain valid
 * until the value is punched and then aggregated or discarded, as rdb employs
 * only DAOS_IOD_SINGLE values.
 *
 * We have to use the zero-copy methods, as vos_obj_fetch() doesn't work in
 * this mode.
 */
int
rdb_vos_fetch_addr(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		   daos_key_t *akey, d_iov_t *value)
{
	daos_unit_oid_t	uoid;
	daos_iod_t	iod;
	daos_handle_t	io;
	struct bio_sglist *bsgl;
	d_iov_t	value_orig = *value;
	int		rc;

	rdb_oid_to_uoid(oid, &uoid);
	rdb_vos_set_iods(RDB_VOS_QUERY, 1 /* n */, akey, value, &iod);
	rc = vos_fetch_begin(cont, uoid, epoch, &rdb_dkey,
			     1 /* n */, &iod, 0 /* vos_flags */, NULL, &io,
			     NULL /* dth */);
	if (rc != 0)
		return rc;

	rc = bio_iod_prep(vos_ioh2desc(io), BIO_CHK_TYPE_IO, NULL, 0);
	if (rc) {
		D_ERROR("prep io descriptor error:"DF_RC"\n", DP_RC(rc));
		goto out;
	}

	bsgl = vos_iod_sgl_at(io, 0 /* idx */);
	D_ASSERT(bsgl != NULL);

	if (bsgl->bs_nr_out == 0) {
		D_ASSERTF(iod.iod_size == 0, DF_U64"\n", iod.iod_size);
		value->iov_buf = NULL;
		value->iov_buf_len = 0;
		value->iov_len = 0;
	} else {
		struct bio_iov *biov = &bsgl->bs_iovs[0];

		D_ASSERTF(bsgl->bs_nr_out == 1, "%u\n", bsgl->bs_nr_out);
		D_ASSERTF(iod.iod_size == bio_iov2len(biov),
			  DF_U64" == "DF_U64"\n", iod.iod_size,
			  bio_iov2len(biov));
		D_ASSERT(biov->bi_addr.ba_type == DAOS_MEDIA_SCM);

		value->iov_buf = bio_iov2raw_buf(biov);
		value->iov_buf_len = bio_iov2len(biov);
		value->iov_len = bio_iov2len(biov);
	}

	rc = bio_iod_post(vos_ioh2desc(io), 0);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
out:
	rc = vos_fetch_end(io, NULL, 0 /* err */);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));

	return rdb_vos_fetch_check(value, &value_orig);
}

int
rdb_vos_query_key_max(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid, daos_key_t *akey)
{
	daos_unit_oid_t	uoid;
	int		rc;

	rdb_oid_to_uoid(oid, &uoid);
	rc = vos_obj_query_key(cont, uoid, DAOS_GET_AKEY|DAOS_GET_MAX, epoch, &rdb_dkey, akey,
			       NULL /* recx */, NULL /* max_write */, 0 /* cell sz */,
			       0 /* stripe sz */, NULL /* dth */);
	if (rc != 0) {
		D_ERROR("vos_obj_query_key((rdb,vos) oids=("DF_U64",lo="DF_U64", hi="DF_U64"), "
			"epoch="DF_U64" ...) failed, "DF_RC"\n", oid, uoid.id_pub.lo,
			uoid.id_pub.hi, epoch, DP_RC(rc));
	}

	return rc;
}

int
rdb_vos_iter_fetch(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		   enum rdb_probe_opc opc, daos_key_t *akey_in,
		   daos_key_t *akey_out, d_iov_t *value)
{
	vos_iter_param_t	param = {};
	daos_handle_t		iter;
	vos_iter_entry_t	entry;
	int			rc;

	D_ASSERTF(opc == RDB_PROBE_FIRST, "unsupported opc: %d\n", opc);
	D_ASSERT(akey_in == NULL);

	/* Find out the a-key. */
	param.ip_hdl = cont;
	rdb_oid_to_uoid(oid, &param.ip_oid);
	param.ip_dkey = rdb_dkey;
	param.ip_epr.epr_lo = epoch;
	param.ip_epr.epr_hi = epoch;
	rc = vos_iter_prepare(VOS_ITER_AKEY, &param, &iter, NULL);
	if (rc != 0)
		goto out;
	rc = vos_iter_probe(iter, NULL /* anchor */);
	if (rc != 0)
		goto out_iter;
	rc = vos_iter_fetch(iter, &entry, NULL /* anchor */);
	if (rc != 0)
		goto out_iter;

	/* Return the a-key. */
	if (akey_out != NULL) {
		if (akey_out->iov_buf == NULL) {
			*akey_out = entry.ie_key;
		} else if (akey_out->iov_buf_len >= entry.ie_key.iov_len) {
			memcpy(akey_out->iov_buf, entry.ie_key.iov_buf,
			       entry.ie_key.iov_len);
			akey_out->iov_len = entry.ie_key.iov_len;
		} else {
			akey_out->iov_len = entry.ie_key.iov_len;
		}
	}

	/* Fetch the value of the a-key. */
	if (value != NULL) {
		if (value->iov_buf == NULL)
			rc = rdb_vos_fetch_addr(cont, epoch, oid, &entry.ie_key,
						value);
		else
			rc = rdb_vos_fetch(cont, epoch, oid, &entry.ie_key,
					   value);
	}

out_iter:
	vos_iter_finish(iter);
out:
	return rc;
}

int
rdb_vos_iterate(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		bool backward, rdb_iterate_cb_t cb, void *arg)
{
	vos_iter_param_t	param = {};
	daos_handle_t		iter;
	int			rc;

	D_ASSERTF(!backward, "unsupported direction: %d\n", backward);

	/* Prepare an iteration from the first a-key. */
	param.ip_hdl = cont;
	rdb_oid_to_uoid(oid, &param.ip_oid);
	param.ip_dkey = rdb_dkey;
	param.ip_epr.epr_lo = epoch;
	param.ip_epr.epr_hi = epoch;
	rc = vos_iter_prepare(VOS_ITER_AKEY, &param, &iter, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			/* No a-keys. */
			rc = 0;
		goto out;
	}
	rc = vos_iter_probe(iter, NULL /* anchor */);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			/* No a-keys. */
			rc = 0;
		goto out_iter;
	}

	for (;;) {
		d_iov_t		value;
		vos_iter_entry_t	entry;

		/* Fetch the a-key and the address of its value. */
		rc = vos_iter_fetch(iter, &entry, NULL /* anchor */);
		if (rc != 0)
			break;
		d_iov_set(&value, NULL /* buf */, 0 /* size */);
		rc = rdb_vos_fetch_addr(cont, epoch, oid, &entry.ie_key,
					&value);
		if (rc != 0)
			break;

		rc = cb(iter, &entry.ie_key, &value, arg);
		if (rc != 0) {
			if (rc == 1)
				/* Stop without errors. */
				rc = 0;
			break;
		}

		/* Move to next a-key. */
		rc = vos_iter_next(iter, NULL /* anchor */);
		if (rc != 0) {
			if (rc == -DER_NONEXIST)
				/* No more a-keys. */
				rc = 0;
			break;
		}
	}

out_iter:
	vos_iter_finish(iter);
out:
	return rc;
}

int
rdb_vos_update(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid, bool crit,
	       int n, d_iov_t akeys[], d_iov_t values[])
{
	daos_unit_oid_t	uoid;
	daos_iod_t	iods[n];
	d_sg_list_t	sgls[n];
	uint64_t	vos_flags = crit ? VOS_OF_CRIT : 0;

	D_ASSERTF(n <= RDB_VOS_BATCH_MAX, "%d <= %d\n", n, RDB_VOS_BATCH_MAX);
	rdb_oid_to_uoid(oid, &uoid);
	rdb_vos_set_iods(RDB_VOS_UPDATE, n, akeys, values, iods);
	rdb_vos_set_sgls(RDB_VOS_UPDATE, n, values, sgls);
	return vos_obj_update(cont, uoid, epoch, RDB_PM_VER, vos_flags,
			      &rdb_dkey, n, iods, NULL, sgls);
}

int
rdb_vos_punch(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid, int n,
	      d_iov_t akeys[])
{
	daos_unit_oid_t	uoid;

	rdb_oid_to_uoid(oid, &uoid);
	return vos_obj_punch(cont, uoid, epoch, RDB_PM_VER, 0,
			     n == 0 ? NULL : &rdb_dkey, n,
			     n == 0 ? NULL : akeys, NULL);
}

int
rdb_vos_discard(daos_handle_t cont, daos_epoch_t low, daos_epoch_t high)
{
	daos_epoch_range_t range;

	D_ASSERTF(low <= high && high <= DAOS_EPOCH_MAX, DF_U64" "DF_U64"\n",
		  low, high);
	range.epr_lo = low;
	range.epr_hi = high;

	return vos_discard(cont, NULL /* objp */, &range, NULL, NULL);
}

int
rdb_vos_aggregate(daos_handle_t cont, daos_epoch_t high)
{
	daos_epoch_range_t	epr;

	D_ASSERTF(high < DAOS_EPOCH_MAX, DF_U64"\n", high);
	epr.epr_lo = 0;
	epr.epr_hi = high;

	return vos_aggregate(cont, &epr, NULL, NULL,
			     VOS_AGG_FL_FORCE_SCAN | VOS_AGG_FL_FORCE_MERGE);
}

/* Return amount of vos pool SCM memory available accounting for
 * VOS PMDK allocation state and VOS "system reserved" memory.
 * TODO: decide if we should also account for VOS in-flight "held" memory.
 */
int
rdb_scm_left(struct rdb *db, daos_size_t *scm_left_outp)
{
	struct vos_pool_space	vps;
	int rc;

	rc = vos_pool_query_space(db->d_uuid, &vps);
	if (rc) {
		D_ERROR(DF_UUID": failed to query vos pool space: "DF_RC"\n",
			DP_UUID(db->d_uuid), DP_RC(rc));
		return rc;
	}

	if (SCM_FREE(&vps) > SCM_SYS(&vps))
		*scm_left_outp = SCM_FREE(&vps) - SCM_SYS(&vps);
	else
		*scm_left_outp = 0;

	return 0;
}
