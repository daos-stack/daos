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

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>
#include <vos_hhash.h>
#include "vos_internal.h"

/** iterator for dkey/akey/recx */
struct vos_obj_iter {
	/* public part of the iterator */
	struct vos_iterator	 it_iter;
	/** handle of iterator */
	daos_handle_t		 it_hdl;
	/** epoch range (condition of the iterator) */
	daos_epoch_range_t	 it_epr;
	/* reference on the object */
	struct vos_obj_ref	*it_oref;
};

/** zero-copy I/O buffer for a vector */
struct vos_vec_zbuf {
	/** scatter/gather list for the ZC IO on this vector */
	daos_sg_list_t		 zb_sgl;
	/** number of pre-allocated pmem buffers for the ZC updates */
	unsigned int		 zb_mmid_nr;
	/** pre-allocated pmem buffers for the ZC updates of this vecotr */
	umem_id_t		*zb_mmids;
};

/** zero-copy I/O context */
struct vos_zc_context {
	bool			 zc_is_update;
	daos_epoch_t		 zc_epoch;
	/** number of vectors of the I/O */
	unsigned int		 zc_vec_nr;
	/** zero-copy buffers for all vectors */
	struct vos_vec_zbuf	*zc_vec_zbufs;
	/** reference on the object */
	struct vos_obj_ref	*zc_oref;
};

static void vos_zcc_destroy(struct vos_zc_context *zcc, int err);

static void
vos_empty_sgl(daos_sg_list_t *sgl)
{
	int	i;

	for (i = 0; i < sgl->sg_nr.num; i++)
		sgl->sg_iovs[i].iov_len = 0;
}

static void
vos_empty_viod(daos_vec_iod_t *viod)
{
	int	i;

	for (i = 0; i < viod->vd_nr; i++)
		viod->vd_recxs[i].rx_rsize = 0;
}

/**
 */
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
	if (rbund->rb_csum != NULL)
		/* XXX remove this crap to support checksum */
		memset(rbund->rb_csum, 0, sizeof(*rbund->rb_csum));

	daos_iov_set(iov, rbund, sizeof(*rbund));
}

/**
 * Prepare the record/recx tree, both of them are btree for now, although recx
 * tree could be rtree in the future.
 *
 * vector tree	: all akeys under the same dkey
 * recx tree	: all record extents under the same akey
 */
static int
tree_prepare(struct vos_obj_ref *oref, daos_handle_t parent_toh,
	     daos_key_t *key, bool read_only, daos_handle_t *toh)
{
	daos_csum_buf_t		 csum;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= key;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= key;
	rbund.rb_csum	= &csum;
	rbund.rb_mmid	= UMMID_NULL;
	memset(&csum, 0, sizeof(csum));

	/* NB: In order to avoid complexities of passing parameters to the
	 * multi-nested tree, tree operations are not nested, instead:
	 *
	 * - In the case of fetch, we load the subtree root stored in the
	 *   parent tree leaf.
	 * - In the case of update/insert, we call dbtree_update() which can
	 *   create and return the root for the subtree.
	 */
	if (read_only) {
		rc = dbtree_lookup(parent_toh, &kiov, &riov);
		if (rc != 0) {
			D_DEBUG(DF_VOS1, "Cannot find key: %d\n", rc);
			return rc;
		}
	} else {
		rc = dbtree_update(parent_toh, &kiov, &riov);
		if (rc != 0) {
			D_DEBUG(DF_VOS1, "Cannot add key: %d\n", rc);
			return rc;
		}
	}

	D_ASSERT(rbund.rb_btr != NULL);
	D_DEBUG(DF_VOS2, "Open subtree\n");

	rc = dbtree_open_inplace(rbund.rb_btr, vos_oref2uma(oref), toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Failed to open subtree %d: %d\n",
			rbund.rb_btr->tr_class, rc);
	}
	return rc;
}

/** close the record extent tree */
static void
tree_release(daos_handle_t toh)
{
	int	rc;

	rc = dbtree_close(toh);
	D_ASSERT(rc == 0 || rc == -DER_NO_HDL);
}

/** fetch data or data address of a recx from the recx tree */
static int
tree_recx_fetch(daos_handle_t toh, daos_epoch_range_t *epr, daos_recx_t *recx,
		daos_iov_t *iov, daos_csum_buf_t *csum)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_idx	= recx->rx_idx;
	kbund.kb_epr	= epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= iov;
	rbund.rb_csum	= csum;
	rbund.rb_recx	= recx;

	return dbtree_fetch(toh, BTR_PROBE_LE, &kiov, &kiov, &riov);
}

/**
 * update data for a record extent, or install zero-copied mmid into the
 * record extent tree (if @mmid is not UMMID_NULL).
 */
static int
tree_recx_update(daos_handle_t toh, daos_epoch_range_t *epr,
		 daos_recx_t *recx, daos_iov_t *iov,
		 daos_csum_buf_t *csum, umem_id_t mmid)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_idx	= recx->rx_idx;
	kbund.kb_epr	= epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_csum	= csum;
	rbund.rb_iov	= iov;
	rbund.rb_recx	= recx;
	rbund.rb_mmid	= mmid;

	return dbtree_update(toh, &kiov, &riov);
}

static int
tree_iter_probe(struct vos_obj_iter *oiter, daos_hash_out_t *anchor)
{
	dbtree_probe_opc_t	opc;

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	return dbtree_iter_probe(oiter->it_hdl, opc, NULL, anchor);
}

static int
tree_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		daos_hash_out_t *anchor)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	daos_csum_buf_t		csum;

	tree_key_bundle2iov(&kbund, &kiov);
	tree_rec_bundle2iov(&rbund, &riov);

	rbund.rb_iov	= &it_entry->ie_key;
	rbund.rb_csum	= &csum;
	daos_iov_set(rbund.rb_iov, NULL, 0); /* no copy */
	daos_csum_set(rbund.rb_csum, NULL, 0);

	return dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
}

static int
tree_iter_next(struct vos_obj_iter *oiter)
{
	return dbtree_iter_next(oiter->it_hdl);
}

/**
 * @} vos_tree_helper
 */

/**
 * @defgroup vos_obj_io_func functions for object regular I/O
 * @{
 */

/** fetch a record extent */
static int
vos_recx_fetch(daos_handle_t toh, daos_epoch_range_t *epr, daos_recx_t *recx,
	       daos_iov_t *iov, daos_csum_buf_t *csum)
{
	daos_recx_t	recx_bak;
	int		rc;

	/* XXX lazy assumption... */
	D_ASSERT(recx->rx_nr == 1);

	recx_bak = *recx;
	rc = tree_recx_fetch(toh, epr, recx, iov, csum);
	if (rc == -DER_NONEXIST) {
		recx_bak.rx_idx = recx->rx_idx + 1; /* fake a mismatch */
		rc = 0;
	}

	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Failed to fetch index "DF_U64": %d\n",
			recx->rx_idx, rc);
		return rc;
	}

	/* If we store index and epoch in the same btree, then BTR_PROBE_LE is
	 * not enough, we also need to check if it is the same index.
	 */
	if (recx_bak.rx_idx != recx->rx_idx) {
		D_DEBUG(DF_VOS2, "Mismatched idx "DF_U64"/"DF_U64", no data\n",
			recx_bak.rx_idx, recx->rx_idx);
		iov->iov_len = 0;
	}
	return 0;
}

/** fetch a set of record extents from the specified vector. */
static int
vos_vec_fetch(struct vos_obj_ref *oref, daos_epoch_t epoch,
	      daos_handle_t vec_toh, daos_vec_iod_t *viod, daos_sg_list_t *sgl)
{
	daos_epoch_range_t	 eprange;
	daos_csum_buf_t		 checksum;
	daos_handle_t		 toh;
	int			 i;
	int			 rc;

	rc = tree_prepare(oref, vec_toh, &viod->vd_name, true, &toh);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DF_VOS2, "nonexistent record\n");
		vos_empty_viod(viod);
		vos_empty_sgl(sgl);
		return 0;
	}

	if (rc != 0)
		return rc;

	eprange.epr_lo = epoch;
	eprange.epr_hi = DAOS_EPOCH_MAX;
	daos_csum_set(&checksum, NULL, 0);

	/* XXX lazy assumption:
	 * value of each recx is stored in an individual iov.
	 */
	D_ASSERT(viod->vd_nr == sgl->sg_nr.num);

	for (i = 0; i < viod->vd_nr; i++) {
		daos_epoch_range_t *epr	 = &eprange;
		daos_csum_buf_t	   *csum = &checksum;

		if (viod->vd_eprs != NULL)
			epr = &viod->vd_eprs[i];

		if (viod->vd_csums != NULL)
			csum = &viod->vd_csums[i];

		rc = vos_recx_fetch(toh, epr, &viod->vd_recxs[i],
				    &sgl->sg_iovs[i], csum);
		if (rc != 0) {
			D_DEBUG(DF_VOS1,
				"Failed to fetch index %d: %d\n", i, rc);
			goto failed;
		}
	}
 failed:
	tree_release(toh);
	return rc;
}

/** fetch a set of records under the same dkey */
static int
vos_dkey_fetch(struct vos_obj_ref *oref, daos_epoch_t epoch, daos_key_t *dkey,
	       unsigned int viod_nr, daos_vec_iod_t *viods,
	       daos_sg_list_t *sgls, struct vos_zc_context *zcc)
{
	daos_handle_t	toh;
	int		i;
	int		rc;
	bool		empty = false;

	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	rc = tree_prepare(oref, oref->or_toh, dkey, true, &toh);
	if (rc == -DER_NONEXIST)
		empty = true;
	else if (rc != 0)
		return rc;

	for (i = 0; i < viod_nr; i++) {
		daos_sg_list_t *sgl;

		sgl = zcc != NULL ? &zcc->zc_vec_zbufs[i].zb_sgl : &sgls[i];
		if (empty) {
			vos_empty_viod(&viods[i]);
			vos_empty_sgl(sgl);
			continue;
		}

		rc = vos_vec_fetch(oref, epoch, toh, &viods[i], sgl);
		if (rc != 0)
			goto out;
	}
 out:
	if (!empty)
		tree_release(toh);
	return rc;
}

/**
 * Fetch an array of vectors from the specified object.
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_key_t *dkey, unsigned int viod_nr, daos_vec_iod_t *viods,
	      daos_sg_list_t *sgls, daos_event_t *ev)
{
	struct vos_obj_ref *oref;
	int		    rc;

	D_DEBUG(DF_VOS2, "Fetch "DF_UOID", desc_nr %d\n",
		DP_UOID(oid), viod_nr);

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &oref);
	if (rc != 0)
		return rc;

	if (vos_obj_is_new(oref->or_obj)) {
		D_DEBUG(DF_VOS2, "New object, nothing to fetch\n");
		goto out;
	}

	rc = vos_dkey_fetch(oref, epoch, dkey, viod_nr, viods, sgls, NULL);
 out:
	vos_obj_ref_release(vos_obj_cache_current(), oref);
	return rc;
}

/** update a record extent (recx) */
static int
vos_recx_update(daos_handle_t toh, daos_epoch_range_t *epr, daos_recx_t *recx,
		daos_iov_t *iov, daos_csum_buf_t *csum, umem_id_t recx_mmid)
{
	daos_epoch_range_t epr_tmp;
	int		   rc;

	/* XXX lazy assumption... */
	D_ASSERT(recx->rx_nr == 1);
	recx->rx_rsize = iov->iov_len;

	epr_tmp = *epr; /* save it */
	epr->epr_hi = DAOS_EPOCH_MAX;

	rc = tree_recx_update(toh, epr, recx, iov, csum, recx_mmid);
	if (rc != 0)
		goto out;

	if (epr_tmp.epr_hi == DAOS_EPOCH_MAX)
		goto out; /* done */

	/* XXX reserved for cache missing, for the time being, the upper level
	 * stack should prevent this from happening.
	 */
	D_ASSERTF(0, "Not ready for cache tiering...\n");
	recx->rx_rsize = DAOS_REC_MISSING;

	epr->epr_lo = epr_tmp.epr_hi + 1;
	epr->epr_hi = DAOS_EPOCH_MAX;

	rc = tree_recx_update(toh, epr, recx, iov, csum, UMMID_NULL);
 out:
	*epr = epr_tmp; /* restore */
	if (rc != 0)
		D_DEBUG(DF_VOS1, "Failed to update subtree: %d\n", rc);

	return rc;
}

/** update a set of record extents (recx) under the same akey */
static int
vos_vec_update(struct vos_obj_ref *oref, daos_epoch_t epoch,
	       daos_handle_t vec_toh, daos_vec_iod_t *viod,
	       daos_sg_list_t *sgl, struct vos_vec_zbuf *zbuf)
{
	daos_epoch_range_t	 eprange;
	daos_csum_buf_t		 checksum;
	daos_handle_t		 toh;
	int			 i;
	int			 rc;

	rc = tree_prepare(oref, vec_toh, &viod->vd_name, false, &toh);
	if (rc != 0)
		return rc;

	eprange.epr_lo = epoch;
	eprange.epr_hi = DAOS_EPOCH_MAX;
	daos_csum_set(&checksum, NULL, 0);

	/* XXX lazy assumption: value of each recx is stored in one iov */
	D_ASSERT(sgl->sg_nr.num == viod->vd_nr);

	for (i = 0; i < viod->vd_nr; i++) {
		daos_epoch_range_t *epr	 = &eprange;
		daos_csum_buf_t	   *csum = &checksum;
		umem_id_t	    mmid = UMMID_NULL;

		if (viod->vd_eprs != NULL)
			epr = &viod->vd_eprs[i];

		if (viod->vd_csums != NULL)
			csum = &viod->vd_csums[i];

		if (zbuf != NULL)
			mmid = zbuf->zb_mmids[i];

		rc = vos_recx_update(toh, epr, &viod->vd_recxs[i],
				     &sgl->sg_iovs[i], csum, mmid);
		if (rc != 0)
			goto failed;
	}
 failed:
	tree_release(toh);
	return rc;
}

static int
vos_dkey_update(struct vos_obj_ref *oref, daos_epoch_t epoch, daos_key_t *dkey,
		unsigned int viod_nr, daos_vec_iod_t *viods,
		daos_sg_list_t *sgls, struct vos_zc_context *zcc)
{
	daos_handle_t	toh;
	int		i;
	int		rc;

	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	rc = tree_prepare(oref, oref->or_toh, dkey, false, &toh);
	if (rc != 0)
		return rc;

	for (i = 0; i < viod_nr; i++) {
		struct vos_vec_zbuf *zbuf;
		daos_sg_list_t	    *sgl;

		if (zcc != NULL) {
			zbuf = &zcc->zc_vec_zbufs[i];
			sgl  = &zbuf->zb_sgl;
		} else {
			D_ASSERT(sgls != NULL);
			sgl = &sgls[i];
			zbuf = NULL;
		}

		rc = vos_vec_update(oref, epoch, toh, &viods[i], sgl, zbuf);
		if (rc != 0)
			goto out;
	}
 out:
	tree_release(toh);
	return rc;
}

/**
 * Update an array of vectors for the specified object.
 */
int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       daos_key_t *dkey, unsigned int viod_nr, daos_vec_iod_t *viods,
	       daos_sg_list_t *sgls, daos_event_t *ev)
{
	struct vos_obj_ref	*oref;
	PMEMobjpool		*pop;
	int			 rc;

	D_DEBUG(DF_VOS2, "Update "DF_UOID", desc_nr %d\n",
		DP_UOID(oid), viod_nr);

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &oref);
	if (rc != 0)
		return rc;

	pop = vos_oref2pop(oref);
	TX_BEGIN(pop) {
		rc = vos_dkey_update(oref, epoch, dkey, viod_nr, viods, sgls,
				     NULL);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DF_VOS1, "Failed to update object: %d\n", rc);
	} TX_END

	vos_obj_ref_release(vos_obj_cache_current(), oref);
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

/**
 * Create a zero-copy I/O context. This context includes buffers pointers
 * to return to caller which can proceed the zero-copy I/O.
 */
static int
vos_zcc_create(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       unsigned int viod_nr, daos_vec_iod_t *viods,
	       struct vos_zc_context **zcc_pp)
{
	struct vos_zc_context *zcc;
	int		       rc;

	D_ALLOC_PTR(zcc);
	if (zcc == NULL)
		return -DER_NOMEM;

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &zcc->zc_oref);
	if (rc != 0)
		D_GOTO(failed, rc);

	zcc->zc_vec_nr = viod_nr;
	D_ALLOC(zcc->zc_vec_zbufs, zcc->zc_vec_nr * sizeof(*zcc->zc_vec_zbufs));
	if (zcc->zc_vec_zbufs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);

	zcc->zc_epoch = epoch;
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
vos_zcc_free_zbuf(struct vos_zc_context *zcc, bool has_tx, bool failed)
{
	struct vos_vec_zbuf *zbuf;
	int		     i;

	for (zbuf = &zcc->zc_vec_zbufs[0];
	     zbuf < &zcc->zc_vec_zbufs[zcc->zc_vec_nr]; zbuf++) {

		daos_sgl_fini(&zbuf->zb_sgl, false);
		if (zbuf->zb_mmids == NULL)
			continue;

		for (i = 0; i < zbuf->zb_mmid_nr; i++) {
			umem_id_t mmid = zbuf->zb_mmids[i];

			if (UMMID_IS_NULL(mmid) || !failed)
				continue;

			if (!has_tx)
				return false;

			umem_free(vos_oref2umm(zcc->zc_oref), mmid);
			zbuf->zb_mmids[i] = UMMID_NULL;
		}

		D_FREE(zbuf->zb_mmids,
		       zbuf->zb_mmid_nr * sizeof(*zbuf->zb_mmids));
	}

	D_FREE(zcc->zc_vec_zbufs, zcc->zc_vec_nr * sizeof(*zcc->zc_vec_zbufs));
	return true;
}

/** free zero-copy I/O context */
static void
vos_zcc_destroy(struct vos_zc_context *zcc, int err)
{
	if (zcc->zc_vec_zbufs != NULL) {
		PMEMobjpool	*pop;
		bool		 done;

		done = vos_zcc_free_zbuf(zcc, false, err != 0);
		if (!done) {
			D_ASSERT(zcc->zc_oref != NULL);
			pop = vos_oref2pop(zcc->zc_oref);

			TX_BEGIN(pop) {
				done = vos_zcc_free_zbuf(zcc, true, err != 0);
				D_ASSERT(done);

			} TX_ONABORT {
				err = umem_tx_errno(err);
				D_DEBUG(DF_VOS1,
					"Failed to free zcbuf: %d\n", err);
			} TX_END
		}
	}

	if (zcc->zc_oref)
		vos_obj_ref_release(vos_obj_cache_current(), zcc->zc_oref);

	D_FREE_PTR(zcc);
}

static int
vos_vec_zc_fetch_begin(struct vos_obj_ref *oref, daos_epoch_t epoch,
		       daos_key_t *dkey, unsigned int viod_nr,
		       daos_vec_iod_t *viods, struct vos_zc_context *zcc)
{
	int	i;
	int	rc;

	/* NB: no cleanup in this function, vos_obj_zc_fetch_end will release
	 * all the resources.
	 */
	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	for (i = 0; i < viod_nr; i++) {
		struct vos_vec_zbuf *zbuf = &zcc->zc_vec_zbufs[i];

		rc = daos_sgl_init(&zbuf->zb_sgl, viods[i].vd_nr);
		if (rc != 0) {
			D_DEBUG(DF_VOS1,
				"Failed to create sgl for vector %d\n", i);
			return rc;
		}
	}

	rc = vos_dkey_fetch(oref, epoch, dkey, viod_nr, viods, NULL, zcc);
	if (rc != 0) {
		D_DEBUG(DF_VOS1,
			"Failed to get ZC buffer for vector %d\n", i);
		return rc;
	}

	return 0;
}

/**
 * Fetch an array of vectors from the specified object in zero-copy mode,
 * this function will create and return scatter/gather list which can address
 * vector data stored in pmem.
 */
int
vos_obj_zc_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid,
		      daos_epoch_t epoch, daos_key_t *dkey,
		      unsigned int viod_nr, daos_vec_iod_t *viods,
		      daos_handle_t *ioh, daos_event_t *ev)
{
	struct vos_zc_context *zcc;
	int		       rc;

	rc = vos_zcc_create(coh, oid, epoch, viod_nr, viods, &zcc);
	if (rc != 0)
		return rc;

	rc = vos_vec_zc_fetch_begin(zcc->zc_oref, epoch, dkey, viod_nr,
				    viods, zcc);
	if (rc != 0)
		goto failed;

	D_DEBUG(DF_VOS2, "Prepared zcbufs for fetching %d vectors\n", viod_nr);
	*ioh = vos_zcc2ioh(zcc);
	return 0;
 failed:
	vos_obj_zc_fetch_end(vos_zcc2ioh(zcc), dkey, viod_nr, viods, rc, NULL);
	return rc;
}

/**
 * Finish the current zero-copy fetch operation and release responding
 * resources.
 */
int
vos_obj_zc_fetch_end(daos_handle_t ioh, daos_key_t *dkey, unsigned int viod_nr,
		     daos_vec_iod_t *viods, int err, daos_event_t *ev)
{
	struct vos_zc_context	*zcc = vos_ioh2zcc(ioh);

	D_ASSERT(!zcc->zc_is_update);
	vos_zcc_destroy(zcc, err);
	return err;
}

static daos_size_t
vos_recx2irec_size(daos_recx_t *recx, daos_csum_buf_t *csum)
{
	struct vos_rec_bundle	rbund;

	rbund.rb_csum	= csum;
	rbund.rb_recx	= recx;
	return vos_irec_size(&rbund);
}

/**
 * Prepare pmem buffers for the zero-copy update.
 *
 * NB: no cleanup in this function, vos_obj_zc_update_end will release all the
 * resources.
 */
static int
vos_rec_zc_update_begin(struct vos_obj_ref *oref, daos_vec_iod_t *viod,
			struct vos_vec_zbuf *zbuf)
{
	int	i;
	int	rc;

	zbuf->zb_mmid_nr = viod->vd_nr;
	D_ALLOC(zbuf->zb_mmids, zbuf->zb_mmid_nr * sizeof(*zbuf->zb_mmids));
	if (zbuf->zb_mmids == NULL)
		return -DER_NOMEM;

	rc = daos_sgl_init(&zbuf->zb_sgl, viod->vd_nr);
	if (rc != 0)
		return -DER_NOMEM;

	for (i = 0; i < viod->vd_nr; i++) {
		daos_recx_t	*recx = &viod->vd_recxs[i];
		daos_csum_buf_t *csum;
		struct vos_irec	*irec;
		umem_id_t	 mmid;

		csum = viod->vd_csums == NULL ? NULL : &viod->vd_csums[i];
		mmid = umem_alloc(vos_oref2umm(oref),
				  vos_recx2irec_size(recx, csum));
		if (UMMID_IS_NULL(mmid))
			return -DER_NOMEM;

		zbuf->zb_mmids[i] = mmid;

		/* return the pmem address, so upper layer stack can do RMA
		 * update for the record.
		 */
		irec = (struct vos_irec *)umem_id2ptr(vos_oref2umm(oref), mmid);
		irec->ir_cs_size = csum == NULL ? 0 : csum->cs_len;
		irec->ir_cs_type = csum == NULL ? 0 : csum->cs_type;
		daos_iov_set(&zbuf->zb_sgl.sg_iovs[i],
			     vos_irec2data(irec), recx->rx_rsize * recx->rx_nr);
	}
	return 0;
}

static int
vos_vec_zc_update_begin(struct vos_obj_ref *oref, unsigned int viod_nr,
			daos_vec_iod_t *viods, struct vos_zc_context *zcc)
{
	int	i;
	int	rc;

	D_ASSERT(oref == zcc->zc_oref);
	for (i = 0; i < viod_nr; i++) {
		rc = vos_rec_zc_update_begin(oref, &viods[i],
					     &zcc->zc_vec_zbufs[i]);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * Create zero-copy buffers for the vectors to be updated. After storing data
 * in the returned ZC buffer, user should call vos_obj_zc_update_end() to
 * create indices for these data buffers.
 */
int
vos_obj_zc_update_begin(daos_handle_t coh, daos_unit_oid_t oid,
			daos_epoch_t epoch, daos_key_t *dkey,
			unsigned int viod_nr, daos_vec_iod_t *viods,
			daos_handle_t *ioh, daos_event_t *ev)
{
	struct vos_zc_context	*zcc;
	PMEMobjpool		*pop;
	int			 rc;

	rc = vos_zcc_create(coh, oid, epoch, viod_nr, viods, &zcc);
	if (rc != 0)
		return rc;

	zcc->zc_is_update = true;
	pop = vos_oref2pop(zcc->zc_oref);

	TX_BEGIN(pop) {
		rc = vos_vec_zc_update_begin(zcc->zc_oref, viod_nr, viods, zcc);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DF_VOS1, "Failed to update object: %d\n", rc);
	} TX_END

	if (rc != 0)
		goto failed;

	D_DEBUG(DF_VOS2, "Prepared zcbufs for updating %d vectors\n", viod_nr);
	*ioh = vos_zcc2ioh(zcc);
	return 0;
 failed:
	vos_obj_zc_update_end(vos_zcc2ioh(zcc), dkey, viod_nr, viods, rc, NULL);
	return rc;
}

/**
 * Submit the current zero-copy I/O operation to VOS and release responding
 * resources.
 */
int
vos_obj_zc_update_end(daos_handle_t ioh, daos_key_t *dkey, unsigned int viod_nr,
		      daos_vec_iod_t *viods, int err, daos_event_t *ev)
{
	struct vos_zc_context	*zcc = vos_ioh2zcc(ioh);
	PMEMobjpool		*pop;

	D_ASSERT(zcc->zc_is_update);
	if (err != 0)
		goto out;

	D_ASSERT(zcc->zc_oref != NULL);
	pop = vos_oref2pop(zcc->zc_oref);

	TX_BEGIN(pop) {
		D_DEBUG(DF_VOS1, "Submit ZC update\n");
		err = vos_dkey_update(zcc->zc_oref, zcc->zc_epoch, dkey,
				      viod_nr, viods, NULL, zcc);

	} TX_ONABORT {
		err = umem_tx_errno(err);
		D_DEBUG(DF_VOS1, "Failed to submit ZC update: %d\n", err);
	} TX_END
 out:
	vos_zcc_destroy(zcc, err);
	return err;
}

int
vos_obj_zc_vec2sgl(daos_handle_t ioh, unsigned int vec_at,
		   daos_sg_list_t **sgl_pp)
{
	struct vos_zc_context *zcc = vos_ioh2zcc(ioh);

	D_ASSERT(zcc->zc_vec_zbufs != NULL);
	D_ASSERT(vec_at < zcc->zc_vec_nr);

	*sgl_pp = &zcc->zc_vec_zbufs[vec_at].zb_sgl;
	return 0;
}

/**
 * @} vos_obj_zio_func
 */

/**
 * VOS object iterators:
 * - iterate d-key
 * - iterate a-key (unsupported)
 * - iterate recx
 */

/** prepare a vector iterator */
static int
vec_iter_prepare(struct vos_obj_iter *oiter)
{
	return dbtree_iter_prepare(oiter->it_oref->or_toh, 0, &oiter->it_hdl);
}

/**
 * Prepare the iterator for the record tree.
 */
static int
rec_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey)
{
	struct vos_obj_ref *oref = oiter->it_oref;
	daos_handle_t	    toh;
	int		    rc;

	rc = tree_prepare(oref, oref->or_toh, dkey, true, &toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Cannot load the recx tree: %d\n", rc);
		return rc;
	}

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	tree_release(toh);
	return rc;
}

/**
 * @addtogroup recx_tree
 */
/**
 * Record extent (recx) iterator
 */
static int recx_iter_fetch(struct vos_obj_iter *oiter,
			   vos_iter_entry_t *it_entry,
			   daos_hash_out_t *anchor);
/**
 * Prepare the iterator for the recx tree.
 */
static int
recx_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		  daos_key_t *akey)
{
	struct vos_obj_ref *oref = oiter->it_oref;
	daos_handle_t	    dk_toh;
	daos_handle_t	    ak_toh;
	int		    rc;

	rc = tree_prepare(oref, oref->or_toh, dkey, true, &dk_toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Cannot load the record tree: %d\n", rc);
		return rc;
	}

	rc = tree_prepare(oref, dk_toh, akey, true, &ak_toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Cannot load the recx tree: %d\n", rc);
		D_GOTO(failed_0, rc);
	}

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(ak_toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Cannot prepare recx iterator: %d\n", rc);
		D_GOTO(failed_1, rc);
	}

 failed_1:
	tree_release(ak_toh);
 failed_0:
	tree_release(dk_toh);
	return rc;
}

/**
 * Probe the recx based on @opc and conditions in @entry (index and epoch),
 * return the matched one to @entry.
 */
static int
recx_iter_probe_fetch(struct vos_obj_iter *oiter, dbtree_probe_opc_t opc,
		      vos_iter_entry_t *entry)
{
	struct vos_key_bundle	kbund;
	daos_iov_t		kiov;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_idx	= entry->ie_recx.rx_idx;
	kbund.kb_epr	= &entry->ie_epr;

	rc = dbtree_iter_probe(oiter->it_hdl, opc, &kiov, NULL);
	if (rc != 0)
		return rc;

	memset(entry, 0, sizeof(*entry));
	rc = recx_iter_fetch(oiter, entry, NULL);
	return rc;
}

/**
 * Find the data that was written before/in the specified epoch of @oiter
 * for the recx in @entry. If this recx has no data for this epoch, then
 * this function will move on to the next recx and repeat this process.
 */
static int
recx_iter_probe_epr(struct vos_obj_iter *oiter, vos_iter_entry_t *entry)
{
	while (1) {
		int	rc;

		if (entry->ie_epr.epr_lo == oiter->it_epr.epr_lo)
			return 0; /* exactly match */

		if (entry->ie_epr.epr_lo < oiter->it_epr.epr_lo) {
			/* this recx has data for the specified epoch, we can
			 * use BTR_PROBE_LE to find the closest epoch of this
			 * recx.
			 */
			entry->ie_epr.epr_lo = oiter->it_epr.epr_lo;
			rc = recx_iter_probe_fetch(oiter, BTR_PROBE_LE, entry);
			return rc;
		}

		/* NB: Nobody can use DAOS_EPOCH_MAX as an epoch of update,
		 * so using BTR_PROBE_GE & DAOS_EPOCH_MAX can effectively find
		 * the index of the next recx.
		 */
		entry->ie_epr.epr_lo = DAOS_EPOCH_MAX;
		rc = recx_iter_probe_fetch(oiter, BTR_PROBE_GE, entry);
		if (rc != 0)
			return rc;
	}
}

static int
recx_iter_probe(struct vos_obj_iter *oiter, daos_hash_out_t *anchor)
{
	vos_iter_entry_t	entry;
	struct vos_key_bundle	kbund;
	daos_iov_t		kiov;
	daos_hash_out_t		tmp;
	int			opc;
	int			rc;

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	rc = dbtree_iter_probe(oiter->it_hdl, opc, NULL, anchor);
	if (rc != 0)
		return rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr = &entry.ie_epr;

	memset(&entry, 0, sizeof(entry));
	rc = recx_iter_fetch(oiter, &entry, &tmp);
	if (rc != 0)
		return rc;

	if (anchor != NULL) {
		if (memcmp(anchor, &tmp, sizeof(tmp)) == 0)
			return 0;

		/* XXX: the original recx has been merged/discarded?
		 * anyway, returns error for now.
		 */
		D_DEBUG(DF_VOS2, "Can't find the provided anchor\n");
		return -DER_AGAIN;
	}

	rc = recx_iter_probe_epr(oiter, &entry);
	return rc;
}

static int
recx_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		daos_hash_out_t *anchor)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	daos_csum_buf_t		csum;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= &it_entry->ie_epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_recx	= &it_entry->ie_recx;
	rbund.rb_iov	= &it_entry->ie_iov;
	rbund.rb_csum	= &csum;

	daos_iov_set(rbund.rb_iov, NULL, 0); /* no data copy */
	daos_csum_set(rbund.rb_csum, NULL, 0);

	rc = dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
	return rc;
}

static int
recx_iter_next(struct vos_obj_iter *oiter)
{
	vos_iter_entry_t entry;
	int		 rc;

	memset(&entry, 0, sizeof(entry));
	rc = recx_iter_fetch(oiter, &entry, NULL);
	if (rc != 0)
		return rc;

	/* NB: Nobody should use DAOS_EPOCH_MAX as an epoch of update,
	 * so using BTR_PROBE_GE & DAOS_EPOCH_MAX can effectively find
	 * the index of the next recx.
	 */
	entry.ie_epr.epr_lo = DAOS_EPOCH_MAX;
	rc = recx_iter_probe_fetch(oiter, BTR_PROBE_GE, &entry);
	if (rc != 0)
		return rc;

	rc = recx_iter_probe_epr(oiter, &entry);
	return rc;
}

/**
 * @} recx_tree
 */

/**
 * common functions for iterator.
 */
static int vos_obj_iter_fini(struct vos_iterator *vitr);

static struct vos_obj_iter *
vos_iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_obj_iter, it_iter);
}

/** prepare an object content iterator */
int
vos_obj_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
		  struct vos_iterator **iter_pp)
{
	struct vos_obj_iter *oiter;
	int		     rc;

	if (param->ip_epr.epr_lo == 0) /* the most recent one */
		param->ip_epr.epr_lo = DAOS_EPOCH_MAX;

	/* XXX can't support range iteration */
	param->ip_epr.epr_hi = DAOS_EPOCH_MAX;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->it_epr = param->ip_epr;
	rc = vos_obj_ref_hold(vos_obj_cache_current(), param->ip_hdl,
			      param->ip_oid, &oiter->it_oref);
	if (rc != 0)
		D_GOTO(failed, rc);

	if (vos_obj_is_new(oiter->it_oref->or_obj)) {
		D_DEBUG(DF_VOS2, "New object, nothing to iterate\n");
		return -DER_NONEXIST;
	}

	rc = vos_obj_tree_init(oiter->it_oref);
	if (rc != 0)
		goto failed;

	switch (type) {
	default:
		D_ERROR("unknown iterator type %d.\n", type);
		rc = -DER_INVAL;
		break;

	case VOS_ITER_DKEY:
		rc = vec_iter_prepare(oiter);
		break;

	case VOS_ITER_AKEY:
		rc = rec_iter_prepare(oiter, &param->ip_dkey);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_prepare(oiter, &param->ip_dkey, &param->ip_akey);
		break;
	}

	if (rc != 0)
		D_GOTO(failed, rc);

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
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	if (!daos_handle_is_inval(oiter->it_hdl))
		dbtree_iter_finish(oiter->it_hdl);

	if (oiter->it_oref != NULL)
		vos_obj_ref_release(vos_obj_cache_current(), oiter->it_oref);

	D_FREE_PTR(oiter);
	return 0;
}

int
vos_obj_iter_probe(struct vos_iterator *iter, daos_hash_out_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return tree_iter_probe(oiter, anchor);

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
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return tree_iter_next(oiter);

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
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return tree_iter_fetch(oiter, it_entry, anchor);

	case VOS_ITER_RECX:
		return recx_iter_fetch(oiter, it_entry, anchor);
	}
}

struct vos_iter_ops	vos_obj_iter_ops = {
	.iop_prepare	= vos_obj_iter_prep,
	.iop_finish	= vos_obj_iter_fini,
	.iop_probe	= vos_obj_iter_probe,
	.iop_next	= vos_obj_iter_next,
	.iop_fetch	= vos_obj_iter_fetch,
};
