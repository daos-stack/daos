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
 * This file is part of daos
 *
 * vos/vos_obj.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>
#include "vos_internal.h"

/** iterator for dkey/akey/recx */
struct vos_obj_iter {
	/* public part of the iterator */
	struct vos_iterator	 it_iter;
	/** handle of iterator */
	daos_handle_t		 it_hdl;
	/** condition of the iterator: epoch logic expression */
	vos_it_epc_expr_t	 it_epc_expr;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	 it_epr;
	/** condition of the iterator: attribute key */
	daos_key_t		 it_akey;
	/* reference on the object */
	struct vos_object	*it_obj;
};

struct iod_buf;

/** zero-copy I/O context */
struct vos_zc_context {
	bool			 zc_is_update;
	daos_epoch_t		 zc_epoch;
	/** number of descriptors of the I/O */
	unsigned int		 zc_iod_nr;
	daos_iod_t		*zc_iods;
	/** I/O buffers for all descriptors */
	struct iod_buf		*zc_iobufs;
	/** reference on the object */
	struct vos_object	*zc_obj;
	/** actv fields used for zc buffer reservation */
	unsigned int		 zc_actv_cnt;
	unsigned int		 zc_actv_at;
	struct pobj_action	*zc_actv;
};

static void vos_zcc_destroy(struct vos_zc_context *zcc, int err);

/** I/O buffer for a I/O descriptor */
struct iod_buf {
	/** scatter/gather list for the ZC IO on this descriptor */
	daos_sg_list_t		 db_sgl;
	/** data offset of the in-use iov of db_sgl (for non-zc) */
	daos_off_t		 db_iov_off;
	/** is it for zero-copy or not */
	bool			 db_zc;
	/** in-use iov index of db_sgl for non-zc, or mmid index for zc */
	unsigned int		 db_at;
	/** number of pre-allocated pmem buffers (for zc update only) */
	unsigned int		 db_mmid_nr;
	/** pre-allocated pmem buffers (for zc update only) */
	umem_id_t		*db_mmids;
};

static bool
iobuf_sgl_empty(struct iod_buf *iobuf)
{
	return !iobuf->db_sgl.sg_iovs;
}

static bool
iobuf_sgl_exhausted(struct iod_buf *iobuf)
{
	D__ASSERT(iobuf->db_at <= iobuf->db_sgl.sg_nr);
	return iobuf->db_at == iobuf->db_sgl.sg_nr;
}

/**
 * This function copies @size bytes from @addr to iovs of @iobuf::db_sgl.
 * If @addr is NULL but @len is non-zero, which represents a hole, this
 * function will skip @len bytes in @iobuf::db_sgl (iovs are untouched).
 * NB: this function is for non-zc only.
 */
static int
iobuf_cp_fetch(struct iod_buf *iobuf, daos_iov_t *iov)
{
	void	    *addr = iov->iov_buf;
	daos_size_t  size = iov->iov_len;

	while (!iobuf_sgl_exhausted(iobuf)) {
		daos_iov_t	*iov;
		daos_size_t	 nob;

		/** current iov */
		iov = &iobuf->db_sgl.sg_iovs[iobuf->db_at];
		if (iov->iov_buf_len <= iobuf->db_iov_off) {
			D_ERROR("Invalid iov[%d] "DF_U64"/"DF_U64"\n",
				iobuf->db_at, iobuf->db_iov_off,
				iov->iov_buf_len);
			return -1;
		}

		nob = min(size, iov->iov_buf_len - iobuf->db_iov_off);
		if (addr != NULL) {
			memcpy(iov->iov_buf + iobuf->db_iov_off, addr, nob);
			addr += nob;
		} /* otherwise it's a hole */

		iobuf->db_iov_off += nob;
		if (iobuf->db_iov_off == nob) /* the first population */
			iobuf->db_sgl.sg_nr_out++;

		iov->iov_len = iobuf->db_iov_off;
		if (iov->iov_len == iov->iov_buf_len) {
			/* consumed an iov, move to the next */
			iobuf->db_iov_off = 0;
			iobuf->db_at++;
		}

		size -= nob;
		if (size == 0)
			return 0;
	}
	D_DEBUG(DB_TRACE, "Consumed all iovs, "DF_U64" bytes left\n", size);
	return -1;
}

/** Fill iobuf for zero-copy fetch */
static int
iobuf_zc_fetch(struct iod_buf *iobuf, daos_iov_t *iov)
{
	daos_sg_list_t	*sgl;
	daos_iov_t	*iovs;
	int		 nr;
	int		 at;

	D__ASSERT(iobuf->db_iov_off == 0);

	sgl = &iobuf->db_sgl;
	at  = iobuf->db_at;
	nr  = sgl->sg_nr;

	if (at == nr - 1) {
		D__ALLOC(iovs, nr * 2 * sizeof(*iovs));
		if (iovs == NULL)
			return -DER_NOMEM;

		memcpy(iovs, &sgl->sg_iovs[0], nr * 2 * sizeof(*iovs));
		D__FREE(sgl->sg_iovs, nr * sizeof(*iovs));

		sgl->sg_iovs	= iovs;
		sgl->sg_nr	= nr * 2;
	}

	/* return the data address for rdma in upper level stack */
	sgl->sg_iovs[at] = *iov;
	sgl->sg_nr_out++;
	iobuf->db_at++;
	return 0;
}

static int
iobuf_fetch(struct iod_buf *iobuf, daos_iov_t *iov)
{
	int	rc;

	if (iobuf_sgl_empty(iobuf))
		return 0; /* size fetch */

	if (iobuf->db_zc)
		rc = iobuf_zc_fetch(iobuf, iov);
	else
		rc = iobuf_cp_fetch(iobuf, iov);

	if (rc)
		D__GOTO(out, rc);

	if (vos_csum_enabled()) {
		daos_csum_buf_t	cbuf;
		uint64_t	csum;

		/* XXX This is just for performance evaluation, checksum is
		 * not verified becausre the original checksum is not stored.
		 */
		daos_csum_set(&cbuf, &csum, sizeof(csum));
		rc = vos_csum_compute(&iobuf->db_sgl, &cbuf);
		if (rc != 0)
			D_ERROR("Checksum compute error: %d\n", rc);
	}
out:
	return rc;
}

/**
 * This function copies @size bytes from @iobuf::db_sgl to destination @addr.
 * NB: this function is for non-zc only.
 */
static int
iobuf_cp_update(struct iod_buf *iobuf, daos_iov_t *iov)
{
	void	    *addr = iov->iov_buf;
	daos_size_t  size = iov->iov_len;

	if (!iov->iov_buf)
		return 0; /* punch */

	D__ASSERT(!iobuf_sgl_empty(iobuf));
	while (!iobuf_sgl_exhausted(iobuf)) {
		daos_iov_t	*iov;
		daos_size_t	 nob;

		/** current iov */
		iov = &iobuf->db_sgl.sg_iovs[iobuf->db_at];
		if (iov->iov_len <= iobuf->db_iov_off) {
			D_ERROR("Invalid iov[%d] "DF_U64"/"DF_U64"\n",
				iobuf->db_at, iobuf->db_iov_off,
				iov->iov_len);
			return -1;
		}

		nob = min(size, iov->iov_len - iobuf->db_iov_off);
		memcpy(addr, iov->iov_buf + iobuf->db_iov_off, nob);

		iobuf->db_iov_off += nob;
		if (iobuf->db_iov_off == iov->iov_len) {
			/* consumed an iov, move to the next */
			iobuf->db_iov_off = 0;
			iobuf->db_at++;
		}

		addr += nob;
		size -= nob;
		if (size == 0)
			return 0;
	}
	D_DEBUG(DB_TRACE, "Consumed all iovs, "DF_U64" bytes left\n", size);
	return -1;
}

/** consume iobuf for zero-copy and do nothing else*/
static int
iobuf_zc_update(struct iod_buf *iobuf)
{
	D__ASSERT(iobuf->db_iov_off == 0);

	iobuf->db_at++;
	return 0;
}

static int
iobuf_update(struct iod_buf *iobuf, daos_iov_t *iov)
{
	int	rc;

	if (vos_csum_enabled()) {
		daos_csum_buf_t	 cbuf;
		uint64_t	 csum;

		/* XXX: checksum is not stored for now */
		daos_csum_set(&cbuf, &csum, sizeof(csum));
		rc = vos_csum_compute(&iobuf->db_sgl, &cbuf);
		if (rc != 0)
			D_ERROR("Checksum compute error: %d\n", rc);
	}

	if (iobuf->db_zc)
		return iobuf_zc_update(iobuf); /* iov is ignored */
	else
		return iobuf_cp_update(iobuf, iov);
}

static void
vos_empty_sgl(daos_sg_list_t *sgl)
{
	int	i;

	for (i = 0; i < sgl->sg_nr; i++)
		sgl->sg_iovs[i].iov_len = 0;
}

static struct vos_obj_iter *
vos_iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_obj_iter, it_iter);
}

struct vos_obj_iter*
vos_hdl2oiter(daos_handle_t hdl)
{
	return vos_iter2oiter(vos_hdl2iter(hdl));
}

/**
 * @defgroup vos_tree_helper Helper functions for tree operations
 * @{
 */

/**
 * store a bundle of parameters into a iovec, which is going to be passed
 * into dbtree operations as a compound key.
 */
static void
tree_key_bundle2iov(struct vos_key_bundle *kbund, daos_iov_t *iov)
{
	memset(kbund, 0, sizeof(*kbund));
	daos_iov_set(iov, kbund, sizeof(*kbund));
}

/**
 * store a bundle of parameters into a iovec, which is going to be passed
 * into dbtree operations as a compound value (data buffer address, or ZC
 * buffer mmid, checksum etc).
 */
static void
tree_rec_bundle2iov(struct vos_rec_bundle *rbund, daos_iov_t *iov)
{
	memset(rbund, 0, sizeof(*rbund));
	daos_iov_set(iov, rbund, sizeof(*rbund));
}

enum {
	SUBTR_CREATE	= (1 << 0),	/**< may create the subtree */
	SUBTR_EVT	= (1 << 1),	/**< subtree is evtree */
};

/**
 * Load the subtree roots embedded in the parent tree record.
 *
 * akey tree	: all akeys under the same dkey
 * recx tree	: all record extents under the same akey, this function will
 *		  load both btree and evtree root.
 */
static int
tree_prepare(struct vos_object *obj, daos_epoch_range_t *epr,
	     daos_handle_t toh, enum vos_tree_class tclass,
	     daos_key_t *key, int flags, daos_handle_t *sub_toh)
{
	struct umem_attr	*uma = vos_obj2uma(obj);
	daos_csum_buf_t		 csum;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	if (tclass != VOS_BTR_AKEY && (flags & SUBTR_EVT))
		D__GOTO(failed, rc = -DER_INVAL);

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= key;
	kbund.kb_epr	= epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_mmid	= UMMID_NULL;
	rbund.rb_csum	= &csum;
	memset(&csum, 0, sizeof(csum));

	/* NB: In order to avoid complexities of passing parameters to the
	 * multi-nested tree, tree operations are not nested, instead:
	 *
	 * - In the case of fetch, we load the subtree root stored in the
	 *   parent tree leaf.
	 * - In the case of update/insert, we call dbtree_update() which may
	 *   create the root for the subtree, or just return it if it's already
	 *   there.
	 */
	if (flags & SUBTR_CREATE) {
		rbund.rb_iov = key;
		rbund.rb_tclass = tclass;
		rc = dbtree_update(toh, &kiov, &riov);
		if (rc != 0)
			D__GOTO(failed, rc);
	} else {
		daos_key_t tmp;

		daos_iov_set(&tmp, NULL, 0);
		rbund.rb_iov = &tmp;
		rc = dbtree_lookup(toh, &kiov, &riov);
		if (rc != 0)
			D__GOTO(failed, rc);
	}

	if (flags & SUBTR_EVT) {
		rc = evt_open_inplace(rbund.rb_evt, uma, sub_toh);
		if (rc != 0)
			D__GOTO(failed, rc);
	} else {
		rc = dbtree_open_inplace(rbund.rb_btr, uma, sub_toh);
		if (rc != 0)
			D__GOTO(failed, rc);
	}
	D_EXIT;
 failed:
	return rc;
}

/** Close the opened trees */
static void
tree_release(daos_handle_t toh, bool is_array)
{
	int	rc;

	if (is_array)
		rc = evt_close(toh);
	else
		rc = dbtree_close(toh);

	D__ASSERT(rc == 0 || rc == -DER_NO_HDL);
}

/**
 * @} vos_tree_helper
 */

/**
 * @defgroup vos_obj_io_func functions for object regular I/O
 * @{
 */
/** Fetch the single value within the specified epoch range of an key */
static int
akey_fetch_single(daos_handle_t toh, daos_epoch_range_t *epr,
		  daos_size_t *rsize, struct iod_buf *iobuf)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_csum_buf_t		 csum;
	daos_iov_t		 kiov; /* iov to carry key bundle */
	daos_iov_t		 riov; /* iov to carray record bundle */
	daos_iov_t		 diov; /* iov to return data buffer */
	int			 rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= &diov;
	rbund.rb_csum	= &csum;
	daos_iov_set(&diov, NULL, 0);
	daos_csum_set(&csum, NULL, 0);

	rc = dbtree_fetch(toh, BTR_PROBE_LE, &kiov, &kiov, &riov);
	if (rc == -DER_NONEXIST) {
		rbund.rb_rsize = 0;
		rc = 0;
	} else if (rc != 0) {
		D__GOTO(out, rc);
	}

	rc = iobuf_fetch(iobuf, &diov);
	if (rc != 0)
		D__GOTO(out, rc);

	*rsize = rbund.rb_rsize;
	D_EXIT;
 out:
	return rc;
}

/** Fetch a extent from an akey */
static int
akey_fetch_recx(daos_handle_t toh, daos_epoch_range_t *epr, daos_recx_t *recx,
		daos_size_t *rsize_p, struct iod_buf *iobuf)
{
	struct evt_entry	*ent;
	struct evt_entry_list	 ent_list;
	struct evt_rect		 rect;
	daos_iov_t		 iov;
	daos_size_t		 holes; /* hole width */
	daos_off_t		 index;
	daos_off_t		 end;
	unsigned int		 rsize;
	int			 rc;

	index = recx->rx_idx;
	end   = recx->rx_idx + recx->rx_nr;

	rect.rc_off_lo = index;
	rect.rc_off_hi = end - 1;
	rect.rc_epc_lo = epr->epr_lo;
	rect.rc_epc_hi = epr->epr_hi;

	evt_ent_list_init(&ent_list);
	rc = evt_find(toh, &rect, &ent_list);
	if (rc != 0)
		D__GOTO(failed, rc);

	rsize = 0;
	holes = 0;
	evt_ent_list_for_each(ent, &ent_list) {
		daos_off_t	lo = ent->en_rect.rc_off_lo;
		daos_off_t	hi = ent->en_rect.rc_off_hi;
		daos_size_t	nr;

		D__ASSERT(hi >= lo);
		nr = hi - lo + 1;

		if (lo != index) {
			D__ASSERTF(lo > index,
				  DF_U64"/"DF_U64", "DF_RECT", "DF_RECT"\n",
				  lo, index, DP_RECT(&rect),
				  DP_RECT(&ent->en_rect));
			holes += lo - index;
		}

		if (ent->en_inob == 0) { /* hole extent */
			index = lo + nr;
			holes += nr;
			continue;
		}

		if (rsize == 0)
			rsize = ent->en_inob;

		if (rsize != ent->en_inob) {
			D_ERROR("Record sizes of all indices must be "
				"the same: %u/%u\n", rsize, ent->en_inob);
			D__GOTO(failed, rc = -DER_IO_INVAL);
		}

		if (holes != 0) {
			daos_iov_set(&iov, NULL, holes * rsize);
			/* skip the hole in iobuf */
			rc = iobuf_fetch(iobuf, &iov);
			if (rc != 0)
				D__GOTO(failed, rc);
			holes = 0;
		}

		daos_iov_set(&iov, ent->en_addr, nr * rsize);
		rc = iobuf_fetch(iobuf, &iov);
		if (rc != 0)
			D__GOTO(failed, rc);

		index = lo + nr;
	}

	D__ASSERT(index <= end);
	if (index < end)
		holes += end - index;

	if (holes != 0) { /* trailing holes */
		if (rsize == 0) { /* nothing but holes */
			vos_empty_sgl(&iobuf->db_sgl);
		} else {
			daos_iov_set(&iov, NULL, holes * rsize);
			rc = iobuf_fetch(iobuf, &iov);
			if (rc != 0)
				D__GOTO(failed, rc);
		}
	}
	*rsize_p = rsize;
	D_EXIT;
 failed:
	evt_ent_list_fini(&ent_list);
	return rc;
}


/** fetch a set of record extents from the specified akey. */
static int
akey_fetch(struct vos_object *obj, daos_epoch_t epoch, daos_handle_t ak_toh,
	   daos_iod_t *iod, struct iod_buf *iobuf)
{
	daos_epoch_range_t	epr;
	daos_handle_t		toh;
	int			i;
	int			rc;
	int			flags = 0;
	bool			is_array = (iod->iod_type == DAOS_IOD_ARRAY);

	D_DEBUG(DB_TRACE, "Fetch %s value\n", is_array ? "array" : "single");

	if (is_array)
		flags |= SUBTR_EVT;

	epr.epr_lo = epr.epr_hi = epoch;
	rc = tree_prepare(obj, &epr, ak_toh, VOS_BTR_AKEY, &iod->iod_name,
			  flags, &toh);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_IO, "nonexistent akey\n");
		vos_empty_sgl(&iobuf->db_sgl);
		iod->iod_size = 0;
		return 0;

	} else if (rc != 0) {
		D_DEBUG(DB_IO, "Failed to open tree root: %d\n", rc);
		return rc;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = akey_fetch_single(toh, &epr, &iod->iod_size, iobuf);
		D__GOTO(out, rc);
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		daos_epoch_range_t *etmp;
		daos_size_t	    rsize;

		etmp = iod->iod_eprs ? &iod->iod_eprs[i] : &epr;
		rc = akey_fetch_recx(toh, etmp, &iod->iod_recxs[i], &rsize,
				     iobuf);
		if (rc != 0) {
			D_DEBUG(DB_IO, "Failed to fetch index %d: %d\n", i, rc);
			D__GOTO(out, rc);
		}

		if (rsize == 0) /* nothing but hole */
			continue;

		if (iod->iod_size == 0)
			iod->iod_size = rsize;

		if (iod->iod_size != rsize) {
			D_ERROR("Cannot support mixed record size "
				DF_U64"/"DF_U64"\n", iod->iod_size, rsize);
			D__GOTO(out, rc);
		}
	}
	D_EXIT;
 out:
	tree_release(toh, is_array);
	return rc;
}

/** Fetch a set of records under the same dkey */
static int
dkey_fetch(struct vos_object *obj, daos_epoch_t epoch, daos_key_t *dkey,
	   unsigned int iod_nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	   struct vos_zc_context *zcc)
{
	daos_handle_t		toh;
	daos_epoch_range_t	epr;
	int			i;
	int			rc;

	rc = vos_obj_tree_init(obj);
	if (rc != 0)
		return rc;

	epr.epr_lo = epr.epr_hi = epoch;
	rc = tree_prepare(obj, &epr, obj->obj_toh, VOS_BTR_DKEY, dkey, 0, &toh);
	if (rc == -DER_NONEXIST) {
		for (i = 0; i < iod_nr; i++) {
			iods[i].iod_size = 0;
			if (sgls != NULL)
				vos_empty_sgl(&sgls[i]);
		}
		D_DEBUG(DB_IO, "nonexistent dkey\n");
		return 0;

	} else if (rc != 0) {
		D_DEBUG(DB_IO, "Failed to prepare subtree: %d\n", rc);
		return rc;
	}

	for (i = 0; i < iod_nr; i++) {
		struct iod_buf	*iobuf;
		struct iod_buf	 iobuf_tmp;

		if (zcc) {
			iobuf = &zcc->zc_iobufs[i];
			iobuf->db_zc = true;
		} else {
			iobuf = &iobuf_tmp;
			memset(iobuf, 0, sizeof(*iobuf));
			if (sgls) {
				iobuf->db_sgl = sgls[i];
				iobuf->db_sgl.sg_nr_out = 0;
			}
		}

		rc = akey_fetch(obj, epoch, toh, &iods[i], iobuf);
		if (rc != 0)
			D__GOTO(failed, rc);

		if (sgls) {
			sgls[i].sg_nr = iobuf->db_sgl.sg_nr;
			sgls[i].sg_nr_out = iobuf->db_sgl.sg_nr_out;
		}
	}
	D_EXIT;
 failed:
	tree_release(toh, false);
	return rc;
}

/**
 * Fetch an array of records from the specified object.
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
	      daos_sg_list_t *sgls)
{
	struct vos_object *obj;
	int		   rc;

	D_DEBUG(DB_TRACE, "Fetch "DF_UOID", desc_nr %d, epoch "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, true, &obj);
	if (rc != 0)
		return rc;

	if (vos_obj_is_empty(obj)) {
		int	i;

		D_DEBUG(DB_IO, "Empty object, nothing to fetch\n");
		for (i = 0; i < iod_nr; i++) {
			iods[i].iod_size = 0;
			if (sgls != NULL)
				vos_empty_sgl(&sgls[i]);
		}
		D__GOTO(out, rc = 0);
	}

	rc = dkey_fetch(obj, epoch, dkey, iod_nr, iods, sgls, NULL);
 out:
	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}

static int
akey_update_single(daos_handle_t toh, daos_epoch_range_t *epr, uuid_t cookie,
		   uint32_t pm_ver, daos_size_t rsize, struct iod_buf *iobuf)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_csum_buf_t		csum;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	daos_iov_t		iov; /* iov for the sink buffer */
	umem_id_t		mmid;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= epr;

	daos_csum_set(&csum, NULL, 0);
	daos_iov_set(&iov, NULL, rsize);

	D__ASSERT(iobuf->db_at == 0);
	if (iobuf->db_zc) {
		D__ASSERT(iobuf->db_mmid_nr == 1);
		mmid = iobuf->db_mmids[0];
	} else {
		mmid = UMMID_NULL;
	}

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_csum	= &csum;
	rbund.rb_iov	= &iov;
	rbund.rb_rsize	= rsize;
	rbund.rb_mmid	= mmid;
	uuid_copy(rbund.rb_cookie, cookie);
	rbund.rb_ver	= pm_ver;

	rc = dbtree_update(toh, &kiov, &riov);
	if (rc != 0) {
		D_ERROR("Failed to update subtree: %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = iobuf_update(iobuf, &iov);
	if (rc != 0)
		D__GOTO(out, rc = -DER_IO_INVAL);

	D_EXIT;
out:
	return rc;
}

/**
 * Update a record extent.
 * See comment of vos_recx_fetch for explanation of @off_p.
 */
static int
akey_update_recx(daos_handle_t toh, daos_epoch_range_t *epr, uuid_t cookie,
		 uint32_t pm_ver, daos_recx_t *recx, daos_size_t rsize,
		 struct iod_buf *iobuf)
{
	struct evt_rect	rect;
	daos_iov_t	iov;
	int		rc;

	rect.rc_epc_lo = epr->epr_lo;
	rect.rc_epc_hi = epr->epr_hi;
	rect.rc_off_lo = recx->rx_idx;
	rect.rc_off_hi = recx->rx_idx + recx->rx_nr - 1;

	daos_iov_set(&iov, NULL, rsize);
	if (iobuf->db_zc) {
		rc = evt_insert(toh, cookie, pm_ver, &rect, rsize,
				iobuf->db_mmids[iobuf->db_at]);
		if (rc != 0)
			D__GOTO(out, rc);
	} else {
		daos_sg_list_t	sgl;

		sgl.sg_iovs   = &iov;
		sgl.sg_nr = 1;

		/* NB: evtree will return the allocated buffer addresses
		 * if there is no input buffer in sgl, which means we can
		 * copy actual data into those buffers after evt_insert_sgl().
		 * See iobuf_update() for the details.
		 */
		rc = evt_insert_sgl(toh, cookie, pm_ver, &rect, rsize, &sgl);
		if (rc != 0)
			D__GOTO(out, rc);

		D__ASSERT(iov.iov_buf != NULL || rsize == 0);
	}

	rc = iobuf_update(iobuf, &iov);
	if (rc != 0)
		D__GOTO(out, rc);

	D_EXIT;
out:
	return rc;
}


/** update a set of record extents (recx) under the same akey */
static int
akey_update(struct vos_object *obj, daos_epoch_t epoch, uuid_t cookie,
	    uint32_t pm_ver, daos_handle_t ak_toh, daos_iod_t *iod,
	    struct iod_buf *iobuf)
{
	daos_epoch_range_t	epr;
	daos_handle_t		toh;
	int			i;
	int			rc;
	int			flags = SUBTR_CREATE;
	bool			is_array = (iod->iod_type == DAOS_IOD_ARRAY);

	if (is_array)
		flags |= SUBTR_EVT;

	D_DEBUG(DB_TRACE, "Update %s value\n", is_array ? "array" : "single");

	epr.epr_lo = epoch;
	epr.epr_hi = DAOS_EPOCH_MAX;
	rc = tree_prepare(obj, &epr, ak_toh, VOS_BTR_AKEY, &iod->iod_name,
			  flags, &toh);
	if (rc != 0)
		return rc;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = akey_update_single(toh, &epr, cookie, pm_ver,
					iod->iod_size, iobuf);
		D__GOTO(out, rc);
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		daos_epoch_range_t *etmp;

		etmp = iod->iod_eprs ? &iod->iod_eprs[i] : &epr;
		rc = akey_update_recx(toh, etmp, cookie, pm_ver,
				      &iod->iod_recxs[i], iod->iod_size, iobuf);
		if (rc != 0)
			D__GOTO(out, rc);
	}
	D_EXIT;
 out:
	tree_release(toh, is_array);
	return rc;
}

static int
dkey_update(struct vos_object *obj, daos_epoch_t epoch, uuid_t cookie,
	    uint32_t pm_ver, daos_key_t *dkey, unsigned int iod_nr,
	    daos_iod_t *iods, daos_sg_list_t *sgls, struct vos_zc_context *zcc)
{
	daos_epoch_range_t	epr;
	daos_handle_t		ak_toh;
	daos_handle_t		ck_toh;
	int			i;
	int			rc;

	rc = vos_obj_tree_init(obj);
	if (rc != 0)
		return rc;

	epr.epr_lo = epoch;
	epr.epr_hi = DAOS_EPOCH_MAX;
	rc = tree_prepare(obj, &epr, obj->obj_toh, VOS_BTR_DKEY, dkey,
			  SUBTR_CREATE, &ak_toh);
	if (rc != 0)
		return rc;

	for (i = 0; i < iod_nr; i++) {
		struct iod_buf *iobuf;
		struct iod_buf  iobuf_tmp;

		if (zcc) {
			iobuf = &zcc->zc_iobufs[i];
			iobuf->db_zc = true;
		} else {
			iobuf = &iobuf_tmp;
			memset(iobuf, 0, sizeof(*iobuf));
			if (sgls)
				iobuf->db_sgl = sgls[i];
		}

		rc = akey_update(obj, epoch, cookie, pm_ver, ak_toh, &iods[i],
				 iobuf);
		if (rc != 0)
			D__GOTO(out, rc);
	}

	/** If dkey update is successful update the cookie tree */
	ck_toh = vos_obj2cookie_hdl(obj);
	rc = vos_cookie_find_update(ck_toh, cookie, epoch, true, NULL);
	if (rc) {
		D_ERROR("Failed to record cookie: %d\n", rc);
		D__GOTO(out, rc);
	}
	D_EXIT;
 out:
	tree_release(ak_toh, false);
	return rc;
}

/**
 * Update an array of records for the specified object.
 */
int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uuid_t cookie, uint32_t pm_ver, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods, daos_sg_list_t *sgls)
{
	struct vos_object	*obj;
	PMEMobjpool		*pop;
	int			rc;

	D_DEBUG(DB_IO, "Update "DF_UOID", desc_nr %d, cookie "DF_UUID" epoch "
		DF_U64"\n", DP_UOID(oid), iod_nr, DP_UUID(cookie), epoch);

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, false,
			  &obj);
	if (rc != 0)
		return rc;

	pop = vos_obj2pop(obj);
	TX_BEGIN(pop) {
		rc = dkey_update(obj, epoch, cookie, pm_ver, dkey, iod_nr, iods,
				 sgls, NULL);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DB_IO, "Failed to update object: %d\n", rc);
	} TX_END

	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}

static int
key_punch(struct vos_object *obj, daos_epoch_t epoch, uuid_t cookie,
	  uint32_t pm_ver, daos_key_t *dkey, unsigned int akey_nr,
	  daos_key_t *akeys)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	daos_epoch_range_t	epr;
	daos_handle_t		dth;
	daos_handle_t		ath;
	int			rc;

	rc = vos_obj_tree_init(obj);
	if (rc)
		D__GOTO(out, rc);

	epr.epr_lo = epr.epr_hi = epoch;
	rc = tree_prepare(obj, &epr, obj->obj_toh, VOS_BTR_DKEY, dkey, 0, &dth);
	if (rc == -DER_NONEXIST)
		D__GOTO(out, rc = 0); /* noop */
	else if (rc)
		D__GOTO(out, rc); /* real failure */

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= &epr;

	tree_rec_bundle2iov(&rbund, &riov);
	uuid_copy(rbund.rb_cookie, cookie);
	rbund.rb_ver	= pm_ver;
	rbund.rb_tclass	= 0; /* punch */
	if (!akeys) {
		kbund.kb_key = dkey;
		rc = dbtree_update(obj->obj_toh, &kiov, &riov);
		if (rc != 0)
			D__GOTO(out, rc);

	} else {
		int	i;

		for (i = 0; i < akey_nr; i++) {
			rc = tree_prepare(obj, &epr, dth, VOS_BTR_AKEY,
					  &akeys[i], 0, &ath);
			if (rc == -DER_NONEXIST)
				D__GOTO(out_dk, rc = 0); /* noop */
			else if (rc)
				D__GOTO(out_dk, rc); /* real failure */

			tree_release(ath, false);
			kbund.kb_key = &akeys[i];
			rc = dbtree_update(dth, &kiov, &riov);
			if (rc != 0)
				D__GOTO(out_dk, rc);
		}
	}
	D_EXIT;
 out_dk:
	tree_release(dth, false);
 out:
	return rc;
}

static int
obj_punch(daos_handle_t coh, struct vos_object *obj, daos_epoch_t epoch,
	  uuid_t cookie)
{
	struct vos_container	*cont;
	int			 rc;

	cont = vos_hdl2cont(coh);
	rc = vos_oi_punch(cont, obj->obj_id, epoch, obj->obj_df);
	if (rc)
		D__GOTO(failed, rc);

	/* evict it from catch, because future fetch should only see empty
	 * object (without obj_df)
	 */
	vos_obj_evict(obj);
	D_EXIT;
failed:
	return rc;
}

/**
 * Punch an object, or punch a dkey, or punch an array of akeys.
 */
int
vos_obj_punch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      uuid_t cookie, uint32_t pm_ver, daos_key_t *dkey,
	      unsigned int akey_nr, daos_key_t *akeys)
{
	PMEMobjpool	  *pop;
	struct vos_object *obj;
	int		   rc;

	D_DEBUG(DB_IO, "Punch "DF_UOID", cookie "DF_UUID" epoch "
		DF_U64"\n", DP_UOID(oid), DP_UUID(cookie), epoch);

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, true,
			  &obj);
	if (rc != 0)
		return rc;

	if (vos_obj_is_empty(obj)) /* nothing to do */
		D__GOTO(out, rc = 0);

	pop = vos_obj2pop(obj);
	TX_BEGIN(pop) {
		if (dkey) { /* key punch */
			rc = key_punch(obj, epoch, cookie, pm_ver, dkey,
				       akey_nr, akeys);
		} else { /* object punch */
			rc = obj_punch(coh, obj, epoch, cookie);
		}

	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DB_IO, "Failed to punch object: %d\n", rc);
	} TX_END
	D_EXIT;
 out:
	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}

/**
 * @} vos_obj_io_func
 */

/*
 * @defgroup vos_obj_zio_func Zero-copy I/O functions
 * @{
 */

/** convert I/O handle to ZC context */
static struct vos_zc_context *
vos_ioh2zcc(daos_handle_t ioh)
{
	return (struct vos_zc_context *)ioh.cookie;
}

/** convert ZC context to I/O handle */
static daos_handle_t
vos_zcc2ioh(struct vos_zc_context *zcc)
{
	daos_handle_t ioh;

	ioh.cookie = (uint64_t)zcc;
	return ioh;
}

static void
vos_zcc_reserve_init(struct vos_zc_context *zcc)
{
	int total_acts = 0;
	int i;

	zcc->zc_actv = NULL;
	zcc->zc_actv_cnt = zcc->zc_actv_at = 0;

	if (!zcc->zc_is_update || POBJ_MAX_ACTIONS == 0)
		return;

	if (vos_obj2umm(zcc->zc_obj)->umm_ops->mo_reserve == NULL)
		return;

	for (i = 0; i < zcc->zc_iod_nr; i++) {
		daos_iod_t *iod = &zcc->zc_iods[i];

		total_acts += iod->iod_nr;
	}

	if (total_acts > POBJ_MAX_ACTIONS)
		return;

	D__ALLOC(zcc->zc_actv, total_acts * sizeof(*zcc->zc_actv));
	if (zcc->zc_actv == NULL)
		return;

	zcc->zc_actv_cnt = total_acts;
}

static void
vos_zcc_reserve_fini(struct vos_zc_context *zcc)
{
	if (zcc->zc_actv_cnt == 0)
		return;
	D__ASSERT(zcc->zc_actv != NULL);
	D__FREE(zcc->zc_actv, zcc->zc_actv_cnt * sizeof(*zcc->zc_actv));
}

/**
 * Create a zero-copy I/O context. This context includes buffers pointers
 * to return to caller which can proceed the zero-copy I/O.
 */
static int
vos_zcc_create(daos_handle_t coh, daos_unit_oid_t oid, bool read_only,
	       daos_epoch_t epoch, unsigned int iod_nr, daos_iod_t *iods,
	       struct vos_zc_context **zcc_pp)
{
	struct vos_zc_context *zcc;
	int		       rc;

	D__ALLOC_PTR(zcc);
	if (zcc == NULL)
		return -DER_NOMEM;

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, read_only,
			  &zcc->zc_obj);
	if (rc != 0)
		D__GOTO(failed, rc);

	zcc->zc_iod_nr = iod_nr;
	zcc->zc_iods = iods;
	D__ALLOC(zcc->zc_iobufs, zcc->zc_iod_nr * sizeof(*zcc->zc_iobufs));
	if (zcc->zc_iobufs == NULL)
		D__GOTO(failed, rc = -DER_NOMEM);

	zcc->zc_epoch = epoch;
	zcc->zc_is_update = !read_only;
	vos_zcc_reserve_init(zcc);
	*zcc_pp = zcc;
	return 0;
 failed:
	vos_zcc_destroy(zcc, rc);
	return rc;
}

/**
 * Free zero-copy buffers for @zcc, it returns false if it is called without
 * transactoin, but @zcc has pmem buffers. Otherwise it returns true.
 */
static int
vos_zcc_free_iobuf(struct vos_zc_context *zcc, bool has_tx, int err)
{
	struct iod_buf	*iobuf;
	int		 i;

	for (iobuf = &zcc->zc_iobufs[0];
	     iobuf < &zcc->zc_iobufs[zcc->zc_iod_nr]; iobuf++) {

		daos_sgl_fini(&iobuf->db_sgl, false);
		if (iobuf->db_mmids == NULL)
			continue;

		for (i = 0; i < iobuf->db_mmid_nr; i++) {
			umem_id_t mmid = iobuf->db_mmids[i];

			/*
			 * Don't bother to free the zc buffers if everything
			 * was done successfully or the buffers were not
			 * allocated but reserved.
			 */
			if (err == 0 || zcc->zc_actv_at != 0)
				continue;

			if (UMMID_IS_NULL(mmid))
				continue;

			if (!has_tx)
				return false;

			umem_free(vos_obj2umm(zcc->zc_obj), mmid);
			iobuf->db_mmids[i] = UMMID_NULL;
		}

		D__FREE(iobuf->db_mmids,
		       iobuf->db_mmid_nr * sizeof(*iobuf->db_mmids));
	}

	D__FREE(zcc->zc_iobufs, zcc->zc_iod_nr * sizeof(*zcc->zc_iobufs));
	return true;
}

/** free zero-copy I/O context */
static void
vos_zcc_destroy(struct vos_zc_context *zcc, int err)
{
	if (zcc->zc_iobufs != NULL) {
		PMEMobjpool	*pop;
		bool		 done;

		D__ASSERT(zcc->zc_obj != NULL);

		done = vos_zcc_free_iobuf(zcc, false, err);
		if (!done) {
			pop = vos_obj2pop(zcc->zc_obj);

			TX_BEGIN(pop) {
				done = vos_zcc_free_iobuf(zcc, true, err);
				D__ASSERT(done);

			} TX_ONABORT {
				err = umem_tx_errno(err);
				D_DEBUG(DB_IO,
					"Failed to free zcbuf: %d\n", err);
			} TX_END
		}

		if (zcc->zc_actv_at != 0 && err != 0) {
			D__ASSERT(zcc->zc_actv != NULL);
			umem_cancel(vos_obj2umm(zcc->zc_obj), zcc->zc_actv,
						zcc->zc_actv_at);
			zcc->zc_actv_at = 0;
		}
	}

	if (zcc->zc_obj)
		vos_obj_release(vos_obj_cache_current(), zcc->zc_obj);
	vos_zcc_reserve_fini(zcc);

	D__FREE_PTR(zcc);
}

static int
dkey_zc_fetch_begin(struct vos_zc_context *zcc, daos_epoch_t epoch,
		    daos_key_t *dkey)
{
	daos_iod_t	*iods = zcc->zc_iods;
	unsigned int	 iod_nr = zcc->zc_iod_nr;
	int	i;
	int	rc;

	/* NB: no cleanup in this function, vos_obj_zc_fetch_end will release
	 * all the resources.
	 */
	rc = vos_obj_tree_init(zcc->zc_obj);
	if (rc != 0)
		return rc;

	for (i = 0; i < iod_nr; i++) {
		struct iod_buf	*iobuf = &zcc->zc_iobufs[i];
		int		 nr    = iods[i].iod_nr;

		if (iods[i].iod_type == DAOS_IOD_SINGLE && nr != 1) {
			D_DEBUG(DB_IO, "Invalid nr=%d for single value\n", nr);
			return -DER_IO_INVAL;
		}

		rc = daos_sgl_init(&iobuf->db_sgl, nr);
		if (rc != 0) {
			D_DEBUG(DB_IO, "Failed to create sgl %d: %d\n", i, rc);
			return rc;
		}
	}

	rc = dkey_fetch(zcc->zc_obj, epoch, dkey, iod_nr, iods, NULL, zcc);
	if (rc != 0) {
		D_DEBUG(DB_IO, "Failed to get ZC buffer: %d\n", rc);
		return rc;
	}
	return 0;
}

/**
 * Fetch an array of records from the specified object in zero-copy mode,
 * this function will create and return scatter/gather list which can address
 * array data stored in pmem.
 */
int
vos_obj_zc_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid,
		       daos_epoch_t epoch, daos_key_t *dkey,
		       unsigned int iod_nr, daos_iod_t *iods,
		       daos_handle_t *ioh)
{
	struct vos_zc_context *zcc;
	int		       i;
	int		       rc;

	rc = vos_zcc_create(coh, oid, true, epoch, iod_nr, iods, &zcc);
	if (rc != 0)
		return rc;

	if (vos_obj_is_empty(zcc->zc_obj)) { /* nothing to fetch */
		for (i = 0; i < iod_nr; i++)
			iods[i].iod_size = 0;
	} else {
		rc = dkey_zc_fetch_begin(zcc, epoch, dkey);
		if (rc != 0)
			D__GOTO(failed, rc);
	}

	D_DEBUG(DB_IO, "Prepared zcbufs for fetching %d iods\n", iod_nr);
	*ioh = vos_zcc2ioh(zcc);
	return 0;
 failed:
	vos_obj_zc_fetch_end(vos_zcc2ioh(zcc), dkey, iod_nr, iods, rc);
	return rc;
}

/**
 * Finish the current zero-copy fetch operation and release responding
 * resources.
 */
int
vos_obj_zc_fetch_end(daos_handle_t ioh, daos_key_t *dkey, unsigned int iod_nr,
		     daos_iod_t *iods, int err)
{
	struct vos_zc_context	*zcc = vos_ioh2zcc(ioh);

	/* NB: it's OK to use the stale zcc->zc_obj for fetch_end */
	D__ASSERT(!zcc->zc_is_update);
	vos_zcc_destroy(zcc, err);
	return err;
}

static daos_size_t
vos_recx2irec_size(daos_size_t rsize, daos_csum_buf_t *csum)
{
	struct vos_rec_bundle	rbund;

	rbund.rb_csum	= csum;
	rbund.rb_rsize	= rsize;

	return vos_irec_size(&rbund);
}

static umem_id_t
vos_zc_reserve(struct vos_zc_context *zcc, daos_size_t size)
{
	struct vos_object	*obj = zcc->zc_obj;
	umem_id_t		 mmid;
	struct pobj_action	*act;

	if (zcc->zc_actv_cnt) {
		D__ASSERT(zcc->zc_actv_cnt > zcc->zc_actv_at);
		D__ASSERT(zcc->zc_actv != NULL);
		act = &zcc->zc_actv[zcc->zc_actv_at];
		mmid = umem_reserve(vos_obj2umm(obj), act, size);
		if (!UMMID_IS_NULL(mmid))
			zcc->zc_actv_at++;
	} else {
		mmid = umem_alloc(vos_obj2umm(obj), size);
	}

	return mmid;
}

/**
 * Prepare pmem buffers for the zero-copy update.
 *
 * NB: no cleanup in this function, vos_obj_zc_update_end will release all the
 * resources.
 */
static int
akey_zc_update_begin(struct vos_zc_context *zcc, int iod_idx)
{
	struct vos_object	*obj = zcc->zc_obj;
	daos_iod_t		*iod = &zcc->zc_iods[iod_idx];
	struct iod_buf		*iobuf = &zcc->zc_iobufs[iod_idx];
	int	i;
	int	rc;

	if (iod->iod_type == DAOS_IOD_SINGLE && iod->iod_nr != 1) {
		D_DEBUG(DB_IO, "Invalid nr=%d\n", iod->iod_nr);
		return -DER_IO_INVAL;
	}

	iobuf->db_mmid_nr = iod->iod_nr;
	D__ALLOC(iobuf->db_mmids, iod->iod_nr * sizeof(*iobuf->db_mmids));
	if (iobuf->db_mmids == NULL)
		return -DER_NOMEM;

	rc = daos_sgl_init(&iobuf->db_sgl, iod->iod_nr);
	if (rc != 0)
		return -DER_NOMEM;

	for (i = 0; i < iod->iod_nr; i++) {
		void		*addr;
		umem_id_t	 mmid;
		daos_size_t	 size;

		if (iod->iod_type == DAOS_IOD_SINGLE) {
			struct vos_irec_df *irec;

			size = vos_recx2irec_size(iod->iod_size, NULL);

			mmid = vos_zc_reserve(zcc, size);
			if (UMMID_IS_NULL(mmid))
				return -DER_NOMEM;

			/* return the pmem address, so upper layer stack can do
			 * RMA update for the record.
			 */
			irec = (struct vos_irec_df *)
				umem_id2ptr(vos_obj2umm(obj), mmid);
			irec->ir_cs_size = 0;
			irec->ir_cs_type = 0;

			addr = vos_irec2data(irec);
			size = iod->iod_size;

		} else { /* DAOS_IOD_ARRAY */
			size = iod->iod_recxs[i].rx_nr;
			if (iod->iod_size == 0) {
				mmid = UMMID_NULL;
				addr = NULL;
			} else {
				size *= iod->iod_size;

				mmid = vos_zc_reserve(zcc, size);
				if (UMMID_IS_NULL(mmid))
					return -DER_NOMEM;
				addr = umem_id2ptr(vos_obj2umm(obj), mmid);
			}
		}
		iobuf->db_mmids[i] = mmid;

		/* return the pmem address, so upper layer stack can do RMA
		 * update for the record.
		 */
		daos_iov_set(&iobuf->db_sgl.sg_iovs[i], addr, size);
		iobuf->db_sgl.sg_nr_out++;
	}
	return 0;
}

static int
dkey_zc_update_begin(struct vos_zc_context *zcc, daos_key_t *dkey)
{
	int	i;
	int	rc;

	for (i = 0; i < zcc->zc_iod_nr; i++) {
		rc = akey_zc_update_begin(zcc, i);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * Create zero-copy buffers for the records to be updated. After storing data
 * in the returned ZC buffer, user should call vos_obj_zc_update_end() to
 * create indices for these data buffers.
 */
int
vos_obj_zc_update_begin(daos_handle_t coh, daos_unit_oid_t oid,
			daos_epoch_t epoch, daos_key_t *dkey,
			unsigned int iod_nr, daos_iod_t *iods,
			daos_handle_t *ioh)
{
	struct vos_zc_context	*zcc;
	PMEMobjpool		*pop;
	int			 rc;

	rc = vos_zcc_create(coh, oid, false, epoch, iod_nr, iods, &zcc);
	if (rc != 0)
		return rc;

	if (zcc->zc_actv_cnt != 0) {
		rc = dkey_zc_update_begin(zcc, dkey);
	} else {
		pop = vos_obj2pop(zcc->zc_obj);
		TX_BEGIN(pop) {
			rc = dkey_zc_update_begin(zcc, dkey);
		} TX_ONABORT {
			rc = umem_tx_errno(rc);
			D_DEBUG(DB_IO, "Failed to update object: %d\n", rc);
		} TX_END
	}

	if (rc != 0)
		goto failed;

	D_DEBUG(DB_IO, "Prepared zcbufs for updating %d arrays\n", iod_nr);
	*ioh = vos_zcc2ioh(zcc);
	return 0;
 failed:
	vos_obj_zc_update_end(vos_zcc2ioh(zcc), 0, 0, dkey, iod_nr, iods, rc);
	return rc;
}

/**
 * Submit the current zero-copy I/O operation to VOS and release responding
 * resources.
 */
int
vos_obj_zc_update_end(daos_handle_t ioh, uuid_t cookie, uint32_t pm_ver,
		      daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		      int err)
{
	struct vos_zc_context	*zcc = vos_ioh2zcc(ioh);
	PMEMobjpool		*pop;

	D__ASSERT(zcc->zc_is_update);
	if (err != 0)
		D__GOTO(out, err);

	D__ASSERT(zcc->zc_obj != NULL);
	err = vos_obj_revalidate(vos_obj_cache_current(), zcc->zc_epoch,
				 &zcc->zc_obj);
	if (err != 0)
		D__GOTO(out, err);

	pop = vos_obj2pop(zcc->zc_obj);

	TX_BEGIN(pop) {
		if (zcc->zc_actv_at != 0) {
			D_DEBUG(DB_IO, "Publish ZC reservation\n");
			err = umem_tx_publish(vos_obj2umm(zcc->zc_obj),
					      zcc->zc_actv, zcc->zc_actv_at);
		}
		D_DEBUG(DB_IO, "Submit ZC update\n");
		err = dkey_update(zcc->zc_obj, zcc->zc_epoch, cookie,
				  pm_ver, dkey, iod_nr, iods, NULL, zcc);
	} TX_ONABORT {
		err = umem_tx_errno(err);
		D_DEBUG(DB_IO, "Failed to submit ZC update: %d\n", err);
	} TX_END

	D_EXIT;
 out:
	vos_zcc_destroy(zcc, err);
	return err;
}

int
vos_obj_zc_sgl_at(daos_handle_t ioh, unsigned int idx, daos_sg_list_t **sgl_pp)
{
	struct vos_zc_context *zcc = vos_ioh2zcc(ioh);

	D__ASSERT(zcc->zc_iobufs != NULL);
	if (idx >= zcc->zc_iod_nr) {
		*sgl_pp = NULL;
		D_DEBUG(DB_IO, "Invalid iod index %d/%d.\n",
			idx, zcc->zc_iod_nr);
		return -DER_NONEXIST;
	}

	*sgl_pp = &zcc->zc_iobufs[idx].db_sgl;
	return 0;
}

/**
 * @} vos_obj_zio_func
 */

/**
 * @defgroup vos_obj_iters VOS object iterators
 * @{
 *
 * - iterate d-key
 * - iterate a-key (array)
 * - iterate recx
 */
static int
key_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *ent,
		daos_hash_out_t *anchor)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	daos_csum_buf_t		csum;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= &ent->ie_epr;

	tree_rec_bundle2iov(&rbund, &riov);

	rbund.rb_iov	= &ent->ie_key;
	rbund.rb_csum	= &csum;

	daos_iov_set(rbund.rb_iov, NULL, 0); /* no copy */
	daos_csum_set(rbund.rb_csum, NULL, 0);

	return dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
}

/**
 * Check if the current entry can match the iterator condition, this function
 * retuns IT_OPC_NOOP for true, returns IT_OPC_NEXT or IT_OPC_PROBE if further
 * operation is required. If IT_OPC_PROBE is returned, then the key to be
 * probed and its epoch range are also returned to @ent.
 */
static int
key_iter_match(struct vos_obj_iter *oiter, vos_iter_entry_t *ent)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_epoch_range_t	*epr = &oiter->it_epr;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_handle_t		 toh;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 iop;
	int			 rc;

	rc = key_iter_fetch(oiter, ent, NULL);
	if (rc)
		D__GOTO(out, iop = rc);

	/* check epoch condition */
	iop = IT_OPC_NOOP;
	if (ent->ie_epr.epr_hi < epr->epr_lo) {
		iop = IT_OPC_PROBE;
		ent->ie_epr = *epr;

	} else if (ent->ie_epr.epr_lo > epr->epr_hi) {
		if (ent->ie_epr.epr_hi < DAOS_EPOCH_MAX) {
			iop = IT_OPC_PROBE;
			ent->ie_epr.epr_lo =
			ent->ie_epr.epr_hi = DAOS_EPOCH_MAX;
		} else {
			iop = IT_OPC_NEXT;
		}
	}

	if (iop != IT_OPC_NOOP)
		D__GOTO(out, iop); /* not in the range, need further operation */

	if ((oiter->it_iter.it_type == VOS_ITER_AKEY) ||
	    (oiter->it_akey.iov_buf == NULL)) /* dkey w/o akey as condition */
		D__GOTO(out, iop = IT_OPC_NOOP);

	/* else: has akey as condition */
	rc = tree_prepare(obj, &ent->ie_epr, obj->obj_toh, VOS_BTR_DKEY,
			  &ent->ie_key, 0, &toh);
	if (rc != 0) {
		D_DEBUG(DB_IO, "can't load the akey tree: %d\n", rc);
		D__GOTO(out, iop = rc);
	}

	/* check if the akey exists */
	tree_rec_bundle2iov(&rbund, &riov);
	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key = &oiter->it_akey;
	kbund.kb_epr = &oiter->it_epr;

	rc = dbtree_lookup(toh, &kiov, &riov);
	tree_release(toh, false);
	if (rc == 0) /* match the condition (akey), done */
		D__GOTO(out, iop = IT_OPC_NOOP);

	if (rc == -DER_NONEXIST) /* can't find the akey */
		D__GOTO(out, iop = IT_OPC_NEXT);

	D_EXIT;
out:
	return iop; /* a real failure */
}

/**
 * Check if the current item can match the provided condition (with the
 * giving a-key). If the item can't match the condition, this function
 * traverses the tree until a matched item is found.
 */
static int
key_iter_find_match(struct vos_obj_iter *oiter)
{
	int	rc;

	while (1) {
		vos_iter_entry_t	entry;
		struct vos_key_bundle	kbund;
		daos_iov_t		kiov;

		rc = key_iter_match(oiter, &entry);
		switch (rc) {
		default:
			D_ERROR("match failed, rc=%d\n", rc);
			D_ASSERT(rc < 0);
			D__GOTO(out, rc);

		case IT_OPC_NOOP:
			/* already match the condition, no further operation */
			D__GOTO(out, rc = 0);

		case IT_OPC_PROBE:
			/* probe the returned key and epoch range */
			tree_key_bundle2iov(&kbund, &kiov);
			kbund.kb_key	= &entry.ie_key;
			kbund.kb_epr	= &entry.ie_epr;
			rc = dbtree_iter_probe(oiter->it_hdl, BTR_PROBE_GE,
					       &kiov, NULL);
			if (rc)
				D__GOTO(out, rc);
			break;

		case IT_OPC_NEXT:
			/* move to the next tree record */
			rc = dbtree_iter_next(oiter->it_hdl);
			if (rc)
				D__GOTO(out, rc);
			break;
		}
	}
	D_EXIT;
 out:
	return rc;
}

static int
key_iter_probe(struct vos_obj_iter *oiter, daos_hash_out_t *anchor)
{
	int	rc;

	rc = dbtree_iter_probe(oiter->it_hdl,
			       anchor ? BTR_PROBE_GE : BTR_PROBE_FIRST,
			       NULL, anchor);
	if (rc)
		D__GOTO(out, rc);

	rc = key_iter_find_match(oiter);
	if (rc)
		D__GOTO(out, rc);
	D_EXIT;
 out:
	return rc;
}

static int
key_iter_next(struct vos_obj_iter *oiter)
{
	int	rc;

	rc = dbtree_iter_next(oiter->it_hdl);
	if (rc)
		D__GOTO(out, rc);

	rc = key_iter_find_match(oiter);
	if (rc)
		D__GOTO(out, rc);
	D_EXIT;
out:
	return rc;
}

/**
 * Iterator for the d-key tree.
 */
static int
dkey_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *akey)
{
	/* optional condition, d-keys with the provided attribute (a-key) */
	oiter->it_akey = *akey;

	return dbtree_iter_prepare(oiter->it_obj->obj_toh, 0, &oiter->it_hdl);
}

/**
 * Iterator for the akey tree.
 */
static int
akey_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 toh;
	int			 rc;

	rc = tree_prepare(obj, &oiter->it_epr, obj->obj_toh, VOS_BTR_DKEY,
			  dkey, 0, &toh);
	if (rc != 0) {
		D_ERROR("Cannot load the akey tree: %d\n", rc);
		return rc;
	}

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc)
		D__GOTO(out, rc);

	tree_release(toh, false);
	D_EXIT;
out:
	return rc;
}

/**
 * Record extent (recx) iterator
 */

/**
 * Record extent (recx) iterator
 */
static int singv_iter_fetch(struct vos_obj_iter *oiter,
			   vos_iter_entry_t *it_entry,
			   daos_hash_out_t *anchor);
/**
 * Prepare the iterator for the recx tree.
 */
static int
singv_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		  daos_key_t *akey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 dk_toh;
	daos_handle_t		 ak_toh;
	int			 rc;

	rc = tree_prepare(obj, &oiter->it_epr, obj->obj_toh, VOS_BTR_DKEY,
			  dkey, 0, &dk_toh);
	if (rc != 0)
		D__GOTO(failed_0, rc);

	rc = tree_prepare(obj, &oiter->it_epr, dk_toh, VOS_BTR_AKEY,
			  akey, 0, &ak_toh);
	if (rc != 0)
		D__GOTO(failed_1, rc);

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(ak_toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc != 0) {
		D_DEBUG(DB_IO, "Cannot prepare singv iterator: %d\n", rc);
		D__GOTO(failed_2, rc);
	}
	D_EXIT;
 failed_2:
	tree_release(ak_toh, false);
 failed_1:
	tree_release(dk_toh, false);
 failed_0:
	return rc;
}

/**
 * Probe the recx based on @opc and conditions in @entry (index and epoch),
 * return the matched one to @entry.
 */
static int
singv_iter_probe_fetch(struct vos_obj_iter *oiter, dbtree_probe_opc_t opc,
		       vos_iter_entry_t *entry)
{
	struct vos_key_bundle	kbund;
	daos_iov_t		kiov;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr = &entry->ie_epr;

	rc = dbtree_iter_probe(oiter->it_hdl, opc, &kiov, NULL);
	if (rc != 0)
		return rc;

	memset(entry, 0, sizeof(*entry));
	rc = singv_iter_fetch(oiter, entry, NULL);
	return rc;
}

/**
 * Find the data that was written before/in the specified epoch of @oiter
 * for the recx in @entry. If this recx has no data for this epoch, then
 * this function will move on to the next recx and repeat this process.
 */
static int
singv_iter_probe_epr(struct vos_obj_iter *oiter, vos_iter_entry_t *entry)
{
	daos_epoch_range_t *epr_cond = &oiter->it_epr;

	while (1) {
		daos_epoch_range_t *epr = &entry->ie_epr;
		int		    rc;

		if (epr->epr_lo == epr_cond->epr_lo)
			return 0; /* matched */

		switch (oiter->it_epc_expr) {
		default:
			return -DER_INVAL;

		case VOS_IT_EPC_RE:
			if (epr->epr_lo >= epr_cond->epr_lo &&
			    epr->epr_lo <= epr_cond->epr_hi)
				return 0; /** Falls in the range */
			/**
			 * This recx may have data for epoch >
			 * entry->ie_epr.epr_lo
			 */
			if (epr->epr_lo < epr_cond->epr_lo)
				epr->epr_lo = epr_cond->epr_lo;
			else /** epoch not in this index search next epoch */
				epr->epr_lo = DAOS_EPOCH_MAX;

			rc = singv_iter_probe_fetch(oiter, BTR_PROBE_GE, entry);
			break;

		case VOS_IT_EPC_RR:
			if (epr->epr_lo <= epr_cond->epr_hi) {
				if (epr->epr_lo >= epr_cond->epr_lo)
					return 0; /** Falls in the range */

				return -DER_NONEXIST; /* end of story */
			}

			epr->epr_lo = epr_cond->epr_hi;
			rc = singv_iter_probe_fetch(oiter, BTR_PROBE_LE, entry);
			break;

		case VOS_IT_EPC_GE:
			if (epr->epr_lo > epr_cond->epr_lo)
				return 0; /* matched */

			/* this recx may have data for the specified epoch, we
			 * can use BTR_PROBE_GE to find out.
			 */
			epr->epr_lo = epr_cond->epr_lo;
			rc = singv_iter_probe_fetch(oiter, BTR_PROBE_GE, entry);
			break;

		case VOS_IT_EPC_LE:
			if (epr->epr_lo < epr_cond->epr_lo) {
				/* this recx has data for the specified epoch,
				 * we can use BTR_PROBE_LE to find the closest
				 * epoch of this recx.
				 */
				epr->epr_lo = epr_cond->epr_lo;
				rc = singv_iter_probe_fetch(oiter, BTR_PROBE_LE,
							   entry);
				return rc;
			}
			/* No matched epoch from in index, try the next index.
			 * NB: Nobody can use DAOS_EPOCH_MAX as an epoch of
			 * update, so using BTR_PROBE_GE & DAOS_EPOCH_MAX can
			 * effectively find the index of the next recx.
			 */
			epr->epr_lo = DAOS_EPOCH_MAX;
			rc = singv_iter_probe_fetch(oiter, BTR_PROBE_GE, entry);
			break;

		case VOS_IT_EPC_EQ:
			if (epr->epr_lo < epr_cond->epr_lo) {
				/* this recx may have data for the specified
				 * epoch, we try to find it by BTR_PROBE_EQ.
				 */
				epr->epr_lo = epr_cond->epr_lo;
				rc = singv_iter_probe_fetch(oiter, BTR_PROBE_EQ,
							   entry);
				if (rc == 0) /* found */
					return 0;

				if (rc != -DER_NONEXIST) /* real failure */
					return rc;
				/* not found, fall through for the next one */
			}
			/* No matched epoch in this index, try the next index.
			 * See the comment for VOS_IT_EPC_LE.
			 */
			epr->epr_lo = DAOS_EPOCH_MAX;
			rc = singv_iter_probe_fetch(oiter, BTR_PROBE_GE, entry);
			break;
		}
		if (rc != 0)
			return rc;
	}
}

static int
singv_iter_probe(struct vos_obj_iter *oiter, daos_hash_out_t *anchor)
{
	vos_iter_entry_t	entry;
	struct vos_key_bundle	kbund;
	daos_iov_t		kiov;
	daos_hash_out_t		tmp;
	int			opc;
	int			rc;

	if (oiter->it_epc_expr == VOS_IT_EPC_RR)
		opc = anchor == NULL ? BTR_PROBE_LAST : BTR_PROBE_LE;
	else
		opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;

	rc = dbtree_iter_probe(oiter->it_hdl, opc, NULL, anchor);
	if (rc != 0)
		return rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= &entry.ie_epr;

	memset(&entry, 0, sizeof(entry));
	rc = singv_iter_fetch(oiter, &entry, &tmp);
	if (rc != 0)
		return rc;

	if (anchor != NULL) {
		if (memcmp(anchor, &tmp, sizeof(tmp)) == 0)
			return 0;

		D_DEBUG(DB_IO, "Can't find the provided anchor\n");
		/**
		 * the original recx has been merged/discarded, so we need to
		 * call singv_iter_probe_epr() and check if the current record
		 * can match the condition.
		 */
	}

	rc = singv_iter_probe_epr(oiter, &entry);
	return rc;
}

static int
singv_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		daos_hash_out_t *anchor)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= &it_entry->ie_epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= &it_entry->ie_iov;
	rbund.rb_csum	= &it_entry->ie_csum;

	daos_iov_set(rbund.rb_iov, NULL, 0); /* no data copy */
	daos_csum_set(rbund.rb_csum, NULL, 0);

	rc = dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
	if (rc)
		D__GOTO(out, rc);

	uuid_copy(it_entry->ie_cookie, rbund.rb_cookie);
	it_entry->ie_rsize	 = rbund.rb_rsize;
	it_entry->ie_ver	 = rbund.rb_ver;
	it_entry->ie_recx.rx_idx = 0;
	it_entry->ie_recx.rx_nr  = 1;
 out:
	return rc;
}

static int
singv_iter_next(struct vos_obj_iter *oiter)
{
	vos_iter_entry_t entry;
	int		 rc;
	int		 opc;

	memset(&entry, 0, sizeof(entry));
	rc = singv_iter_fetch(oiter, &entry, NULL);
	if (rc != 0)
		return rc;

	if (oiter->it_epc_expr == VOS_IT_EPC_RE)
		entry.ie_epr.epr_lo +=  1;
	else if (oiter->it_epc_expr == VOS_IT_EPC_RR)
		entry.ie_epr.epr_lo -=  1;
	else
		entry.ie_epr.epr_lo = DAOS_EPOCH_MAX;

	opc = (oiter->it_epc_expr == VOS_IT_EPC_RR) ?
		BTR_PROBE_LE : BTR_PROBE_GE;

	rc = singv_iter_probe_fetch(oiter, opc, &entry);
	if (rc != 0)
		return rc;

	rc = singv_iter_probe_epr(oiter, &entry);
	return rc;
}

/**
 * Prepare the iterator for the recx tree.
 */
static int
recx_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		  daos_key_t *akey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 dk_toh;
	daos_handle_t		 ak_toh;
	int			 rc;

	rc = tree_prepare(obj, &oiter->it_epr, obj->obj_toh, VOS_BTR_DKEY,
			  dkey, 0, &dk_toh);
	if (rc != 0)
		D__GOTO(failed_0, rc);

	rc = tree_prepare(obj, &oiter->it_epr, dk_toh, VOS_BTR_AKEY,
			  akey, SUBTR_EVT, &ak_toh);
	if (rc != 0)
		D__GOTO(failed_1, rc);

	rc = evt_iter_prepare(ak_toh, EVT_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc != 0) {
		D_DEBUG(DB_IO, "Cannot prepare recx iterator : %d\n", rc);
		D__GOTO(failed_2, rc);
	}
	D_EXIT;
 failed_2:
	tree_release(ak_toh, true);
 failed_1:
	tree_release(dk_toh, false);
 failed_0:
	return rc;
}
static int
recx_iter_probe(struct vos_obj_iter *oiter, daos_hash_out_t *anchor)
{
	int	opc;
	int	rc;

	opc = anchor ? EVT_ITER_FIND : EVT_ITER_FIRST;
	rc = evt_iter_probe(oiter->it_hdl, opc, NULL, anchor);
	return rc;
}

static int
recx_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		daos_hash_out_t *anchor)
{
	struct evt_rect	 *rect;
	struct evt_entry  entry;
	int		  rc;

	rc = evt_iter_fetch(oiter->it_hdl, &entry, anchor);
	if (rc != 0)
		D__GOTO(out, rc);

	memset(it_entry, 0, sizeof(*it_entry));

	rect = &entry.en_rect;
	it_entry->ie_epr.epr_lo	 = rect->rc_epc_lo;
	it_entry->ie_epr.epr_hi	 = rect->rc_epc_hi;
	it_entry->ie_recx.rx_idx = rect->rc_off_lo;
	it_entry->ie_recx.rx_nr	 = rect->rc_off_hi - rect->rc_off_lo + 1;
	it_entry->ie_rsize	 = entry.en_inob;
	uuid_copy(it_entry->ie_cookie, entry.en_cookie);
	it_entry->ie_ver	= entry.en_ver;
 out:
	return rc;
}

static int
recx_iter_next(struct vos_obj_iter *oiter)
{
	return evt_iter_next(oiter->it_hdl);
}

static int
recx_iter_fini(struct vos_obj_iter *oiter)
{
	return evt_iter_finish(oiter->it_hdl);
}

/**
 * common functions for iterator.
 */
static int vos_obj_iter_fini(struct vos_iterator *vitr);

/** prepare an object content iterator */
int
vos_obj_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
		  struct vos_iterator **iter_pp)
{
	struct vos_obj_iter *oiter;
	int		     rc;

	D__ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->it_epr = param->ip_epr;
	/* XXX the condition epoch ranges could cover multiple versions of
	 * the object/key if it's punched more than once.
	 */
	rc = vos_obj_hold(vos_obj_cache_current(), param->ip_hdl,
			  param->ip_oid, param->ip_epr.epr_hi, true,
			  &oiter->it_obj);
	if (rc != 0)
		D__GOTO(failed, rc);

	if (vos_obj_is_empty(oiter->it_obj)) {
		D_DEBUG(DB_IO, "Empty object, nothing to iterate\n");
		D__GOTO(failed, rc = -DER_NONEXIST);
	}

	rc = vos_obj_tree_init(oiter->it_obj);
	if (rc != 0)
		goto failed;

	switch (type) {
	default:
		D_ERROR("unknown iterator type %d.\n", type);
		rc = -DER_INVAL;
		break;

	case VOS_ITER_DKEY:
		rc = dkey_iter_prepare(oiter, &param->ip_akey);
		break;

	case VOS_ITER_AKEY:
		rc = akey_iter_prepare(oiter, &param->ip_dkey);
		break;

	case VOS_ITER_SINGLE:
		oiter->it_epc_expr = param->ip_epc_expr;
		rc = singv_iter_prepare(oiter, &param->ip_dkey,
					&param->ip_akey);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_prepare(oiter, &param->ip_dkey, &param->ip_akey);
		break;
	}

	if (rc != 0)
		D__GOTO(failed, rc);

	*iter_pp = &oiter->it_iter;
	return 0;
 failed:
	vos_obj_iter_fini(&oiter->it_iter);
	return rc;
}

/** release the object iterator */
static int
vos_obj_iter_fini(struct vos_iterator *iter)
{
	struct vos_obj_iter	*oiter = vos_iter2oiter(iter);
	int			 rc;

	if (daos_handle_is_inval(oiter->it_hdl))
		D__GOTO(out, rc = -DER_NO_HDL);

	switch (iter->it_type) {
	default:
		D__ASSERT(0);
		break;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		rc = dbtree_iter_finish(oiter->it_hdl);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_fini(oiter);
		break;
	}
 out:
	if (oiter->it_obj != NULL)
		vos_obj_release(vos_obj_cache_current(), oiter->it_obj);

	D__FREE_PTR(oiter);
	return 0;
}

int
vos_obj_iter_probe(struct vos_iterator *iter, daos_hash_out_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D__ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_probe(oiter, anchor);

	case VOS_ITER_SINGLE:
		return singv_iter_probe(oiter, anchor);

	case VOS_ITER_RECX:
		return recx_iter_probe(oiter, anchor);
	}
}

static int
vos_obj_iter_next(struct vos_iterator *iter)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D__ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_next(oiter);

	case VOS_ITER_SINGLE:
		return singv_iter_next(oiter);

	case VOS_ITER_RECX:
		return recx_iter_next(oiter);
	}
}

static int
vos_obj_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		   daos_hash_out_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D__ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_fetch(oiter, it_entry, anchor);

	case VOS_ITER_SINGLE:
		return singv_iter_fetch(oiter, it_entry, anchor);

	case VOS_ITER_RECX:
		return recx_iter_fetch(oiter, it_entry, anchor);
	}
}

static int
obj_iter_delete(struct vos_obj_iter *oiter, void *args)
{
	int		rc = 0;
	PMEMobjpool	*pop;

	D_DEBUG(DB_TRACE, "BTR delete called of obj\n");
	pop = vos_obj2pop(oiter->it_obj);

	TX_BEGIN(pop) {
		rc = dbtree_iter_delete(oiter->it_hdl, args);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Failed to delete iter entry: %d\n", rc);
	} TX_END

	return rc;
}

static int
vos_obj_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D__ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		return obj_iter_delete(oiter, args);

	case VOS_ITER_RECX:
		return -DER_NOSYS;
	}
}

static int
vos_obj_iter_empty(struct vos_iterator *iter)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	if (daos_handle_is_inval(oiter->it_hdl))
		return -DER_NO_HDL;

	switch (iter->it_type) {
	default:
		D__ASSERT(0);
		return -DER_INVAL;
	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		return dbtree_iter_empty(oiter->it_hdl);
	case VOS_ITER_RECX:
		return -DER_NOSYS;
	}
}

struct vos_iter_ops	vos_obj_iter_ops = {
	.iop_prepare	= vos_obj_iter_prep,
	.iop_finish	= vos_obj_iter_fini,
	.iop_probe	= vos_obj_iter_probe,
	.iop_next	= vos_obj_iter_next,
	.iop_fetch	= vos_obj_iter_fetch,
	.iop_delete	= vos_obj_iter_delete,
	.iop_empty	= vos_obj_iter_empty,
};
/**
 * @} vos_obj_iters
 */

static int
vos_oi_set_attr_helper(daos_handle_t coh, daos_unit_oid_t oid,
		       daos_epoch_t epoch, uint64_t attr, bool set)
{
	PMEMobjpool	  *pop;
	struct vos_object *obj;
	int		   rc;

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, false,
			  &obj);
	if (rc != 0)
		return rc;

	pop = vos_obj2pop(obj);
	TX_BEGIN(pop) {
		rc = umem_tx_add_ptr(vos_obj2umm(obj), &obj->obj_df->vo_oi_attr,
				     sizeof(obj->obj_df->vo_oi_attr));
		if (set) {
			obj->obj_df->vo_oi_attr |= attr;
		} else {
			/* Only clear bits that are set */
			uint64_t to_clear = attr & obj->obj_df->vo_oi_attr;

			obj->obj_df->vo_oi_attr ^= to_clear;
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DB_IO, "Failed to set attributes on object: %d\n", rc);
	} TX_END
	D_EXIT;
	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}

int
vos_oi_set_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t attr)
{
	D_DEBUG(DB_IO, "Set attributes "DF_UOID", epoch "DF_U64", attributes "
		 DF_X64"\n", DP_UOID(oid), epoch, attr);

	return vos_oi_set_attr_helper(coh, oid, epoch, attr, true);
}

int
vos_oi_clear_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t attr)
{
	D_DEBUG(DB_IO, "Clear attributes "DF_UOID", epoch "DF_U64
		 ", attributes "DF_X64"\n", DP_UOID(oid), epoch, attr);

	return vos_oi_set_attr_helper(coh, oid, epoch, attr, false);
}

int
vos_oi_get_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t *attr)
{
	struct vos_object *obj;
	int		   rc = 0;

	D_DEBUG(DB_IO, "Get attributes "DF_UOID", epoch "DF_U64"\n",
		 DP_UOID(oid), epoch);

	if (attr == NULL) {
		D_ERROR("Invalid attribute argument\n");
		return -DER_INVAL;
	}

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, true,
			  &obj);
	if (rc != 0)
		return rc;

	*attr = 0;

	if (obj->obj_df == NULL) /* nothing to do */
		D__GOTO(out, rc = 0);

	*attr = obj->obj_df->vo_oi_attr;

	D_EXIT;

out:
	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}
