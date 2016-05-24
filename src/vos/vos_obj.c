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
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
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

static void vos_zcc_destroy(struct vos_zc_context *zcc);

static void
vos_key_bundle2iov(struct vos_key_bundle *kbund, daos_iov_t *iov)
{
	memset(kbund, 0, sizeof(*kbund));
	daos_iov_set(iov, kbund, sizeof(*kbund));
}

static void
vos_rec_bundle2iov(struct vos_rec_bundle *rbund, daos_iov_t *iov)
{
	memset(rbund, 0, sizeof(*rbund));
	if (rbund->rb_csum != NULL)
		/* XXX remove this crap to support checksum */
		memset(rbund->rb_csum, 0, sizeof(*rbund->rb_csum));

	daos_iov_set(iov, rbund, sizeof(*rbund));
}

/** fetch a record extent */
static int
vos_recx_fetch(daos_handle_t toh, daos_epoch_range_t *epr, daos_dkey_t *dkey,
	       daos_akey_t *rex_name, daos_recx_t *rex, daos_iov_t *iov,
	       daos_csum_buf_t *csum)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_recx_t		 rex_bak;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	/* XXX lazy assumption... */
	D_ASSERT(rex->rx_nr == 1);
	vos_key_bundle2iov(&kbund, &kiov);

	kbund.kb_rex	= rex;
	kbund.kb_epr	= epr;

	vos_rec_bundle2iov(&rbund, &riov);
	rbund.rb_csum	= csum;
	rbund.rb_iov	= iov;

	rex_bak = *rex;
	rc = dbtree_fetch(toh, BTR_PROBE_LE, &kiov, &kiov, &riov);
	if (rc != 0 && rc != -DER_NONEXIST) {
		D_DEBUG(DF_VOS1, "Failed to fetch index "DF_U64": %d\n",
			rex->rx_idx, rc);
		return rc;
	}

	if (rc == -DER_NONEXIST) {
		rex_bak.rx_idx = rex->rx_idx + 1; /* fake a mismatch */
		rc = 0;
	}

	/* If we store index and epoch in the same btree, then BTR_PROBE_LE is
	 * not enough, we also need to check if it is the same index.
	 */
	if (rex_bak.rx_idx != rex->rx_idx) {
		D_DEBUG(DF_VOS2, "Mismatched idx "DF_U64"/"DF_U64", no data\n",
			rex_bak.rx_idx, rex->rx_idx);
		iov->iov_len = DAOS_REC_NODATA;
	}
	return 0;
}

/** fetch a set of record extents from the specified vector. */
static int
vos_vec_fetch(struct vos_obj_ref *oref, daos_epoch_t epoch, daos_dkey_t *dkey,
	      daos_vec_iod_t *vio, daos_sg_list_t *sgl)
{
	daos_epoch_range_t	 eprange;
	daos_csum_buf_t		 checksum;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	daos_handle_t		 toh;
	int			 i;
	int			 rc;

	/* XXX lazy assumption:
	 * value of each recx is stored in an individual iov.
	 */
	D_ASSERT(vio->vd_nr == sgl->sg_nr.num);

	eprange.epr_lo = epoch;
	eprange.epr_hi = DAOS_EPOCH_MAX;

	vos_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= dkey;

	vos_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= dkey; /* XXX dkey only */
	rbund.rb_csum	= &checksum;

	rc = dbtree_lookup(oref->or_toh, &kiov, &riov);
	if (rc != 0) {
		if (rc != -DER_NONEXIST) {
			D_DEBUG(DF_VOS1, "Failed to find the key: %d\n", rc);
			return rc;
		}

		D_DEBUG(DF_VOS2,
			"Cannot find dkey, set nodata for all records\n");
		for (i = 0; i < vio->vd_nr; i++)
			sgl->sg_iovs[i].iov_len = DAOS_REC_NODATA;
		return 0;
	}

	/* NB: In order to avoid complexities of passing parameters to the
	 * multi-nested tree, tree operations are not nested:
	 * - fetch() of non-leaf tree only returns the key stored in itself,
	 *   it also returns the root of its subtree.
	 * - tree user has to open the sub-tree, and explicitly call fetch()
	 *   for the sub-tree.
	 */
	D_ASSERT(rbund.rb_btr != NULL);
	D_DEBUG(DF_VOS2, "Open subtree\n");

	rc = dbtree_open_inplace(rbund.rb_btr, vos_oref2uma(oref), &toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Failed to open subtree %d: %d\n",
			rbund.rb_btr->tr_class, rc);
		return rc;
	}

	for (i = 0; i < vio->vd_nr; i++) {
		daos_epoch_range_t *epr	 = &eprange;
		daos_csum_buf_t	   *csum = &checksum;

		if (vio->vd_eprs != NULL)
			epr = &vio->vd_eprs[i];

		if (vio->vd_csums != NULL)
			csum = &vio->vd_csums[i];

		rc = vos_recx_fetch(toh, epr, dkey, &vio->vd_name,
				    &vio->vd_recxs[i], &sgl->sg_iovs[i], csum);
		if (rc != 0) {
			D_DEBUG(DF_VOS1,
				"Failed to fetch index %d: %d\n", i, rc);
			goto failed;
		}
	}
 failed:
	rc = dbtree_close(toh);
	return rc;
}

static int
vos_oref_fetch(struct vos_obj_ref *oref, daos_epoch_t epoch,
	       daos_dkey_t *dkey, unsigned int nr, daos_vec_iod_t *vios,
	       daos_sg_list_t *sgls)
{
	int	i;
	int	rc;

	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	for (i = 0; i < nr; i++) {
		rc = vos_vec_fetch(oref, epoch, dkey, &vios[i], &sgls[i]);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * Fetch an array of vectors from the specified object.
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_dkey_t *dkey, unsigned int nr, daos_vec_iod_t *vios,
	      daos_sg_list_t *sgls, daos_event_t *ev)
{
	struct vos_obj_ref *oref;
	int		    rc;

	D_DEBUG(DF_VOS2, "Fetch "DF_UOID", desc_nr %d\n", DP_UOID(oid), nr);

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &oref);
	if (rc != 0)
		return rc;

	if (vos_obj_is_new(oref->or_obj)) {
		D_DEBUG(DF_VOS2, "New object, nothing to fetch\n");
		goto out;
	}

	rc = vos_oref_fetch(oref, epoch, dkey, nr, vios, sgls);
 out:
	vos_obj_ref_release(vos_obj_cache_current(), oref);
	return rc;
}

/** update a record extent */
static int
vos_recx_update(daos_handle_t toh, daos_epoch_range_t *epr, daos_dkey_t *dkey,
		daos_akey_t *rex_name, daos_recx_t *rex, daos_iov_t *iov,
		daos_csum_buf_t *csum, umem_id_t rex_mmid)
{
	daos_epoch_range_t	eprange;
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	int			rc;

	/* XXX lazy assumption... */
	D_ASSERT(rex->rx_nr == 1);
	rex->rx_rsize = iov->iov_len;

	eprange = *epr; /* save it */
	epr->epr_hi = DAOS_EPOCH_MAX;

	vos_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= epr;
	kbund.kb_rex	= rex;

	vos_rec_bundle2iov(&rbund, &riov);
	rbund.rb_csum	= csum;
	rbund.rb_iov	= iov;
	rbund.rb_mmid	= rex_mmid;

	rc = dbtree_update(toh, &kiov, &riov);
	if (rc != 0)
		goto out;

	if (eprange.epr_hi == DAOS_EPOCH_MAX)
		goto out; /* done */

	/* XXX reserved for cache missing, for the time being, the upper level
	 * stack should prevent this from happening.
	 */
	D_ERROR("Not ready for cache tiering...\n");

	rex->rx_rsize = DAOS_REC_MISSING;

	epr->epr_lo = eprange.epr_hi + 1;
	epr->epr_hi = DAOS_EPOCH_MAX;

	rc = dbtree_update(toh, &kiov, &riov);
 out:
	*epr = eprange; /* restore */
	if (rc != 0)
		D_DEBUG(DF_VOS1, "Failed to update subtree: %d\n", rc);

	return rc;
}

/** update a set of record extents of the specified vector. */
static int
vos_vec_update(struct vos_obj_ref *oref, daos_epoch_t epoch, daos_dkey_t *dkey,
	       daos_vec_iod_t *vio, daos_sg_list_t *sgl,
	       struct vos_vec_zbuf *zbuf)
{
	daos_epoch_range_t	 eprange;
	daos_csum_buf_t		 checksum;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	daos_handle_t		 toh;
	int			 i;
	int			 rc;
	int			 rc1;

	eprange.epr_lo = epoch;
	eprange.epr_hi = DAOS_EPOCH_MAX;

	vos_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= dkey;

	memset(&checksum, 0, sizeof(checksum));
	vos_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= dkey; /* XXX dkey only */
	rbund.rb_csum	= &checksum; /* and dkey has no checksum for now... */
	rbund.rb_mmid	= UMMID_NULL;

	rc = dbtree_update(oref->or_toh, &kiov, &riov);
	if (rc != 0)
		return rc;

	/* NB: In order to avoid complexities of passing parameters to the
	 * multi-nested tree, tree operations are not nested:
	 * - update() of non-leaf tree only returns the sub-tree root
	 * - tree user has to open the sub-tree, and explicitly call update()
	 *   for the sub-tree.
	 */
	D_ASSERT(rbund.rb_btr != NULL);
	D_DEBUG(DF_VOS2, "Open subtree\n");

	rc = dbtree_open_inplace(rbund.rb_btr, vos_oref2uma(oref), &toh);
	if (rc != 0)
		return rc;

	/* XXX lazy assumption: value of each rex is stored in one iov */
	D_ASSERT(sgl->sg_nr.num == vio->vd_nr);

	for (i = 0; i < vio->vd_nr; i++) {
		daos_epoch_range_t *epr	 = &eprange;
		daos_csum_buf_t	   *csum = &checksum;
		umem_id_t	    mmid = UMMID_NULL;

		if (vio->vd_eprs != NULL)
			epr = &vio->vd_eprs[i];

		if (vio->vd_csums != NULL)
			csum = &vio->vd_csums[i];

		if (zbuf != NULL)
			mmid = zbuf->zb_mmids[i];

		rc = vos_recx_update(toh, epr, dkey, &vio->vd_name,
				     &vio->vd_recxs[i], &sgl->sg_iovs[i],
				     csum, mmid);
		if (rc != 0)
			goto failed;
	}
 failed:
	rc1 = dbtree_close(toh);
	if (rc == 0)
		rc = rc1;

	return rc;
}

static int
vos_oref_update(struct vos_obj_ref *oref, daos_epoch_t epoch,
		daos_dkey_t *dkey, unsigned int nr, daos_vec_iod_t *vios,
		daos_sg_list_t *sgls, struct vos_zc_context *zcc)
{
	int	rc;
	int	i;

	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	for (i = 0; i < nr; i++) {
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

		rc = vos_vec_update(oref, epoch, dkey, &vios[i], sgl, zbuf);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * Update an array of vectors for the specified object.
 */
int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       daos_dkey_t *dkey, unsigned int nr, daos_vec_iod_t *vios,
	       daos_sg_list_t *sgls, daos_event_t *ev)
{
	struct vos_obj_ref	*oref;
	PMEMobjpool		*pop;
	int			 rc;

	D_DEBUG(DF_VOS2, "Update "DF_UOID", desc_nr %d\n", DP_UOID(oid), nr);

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &oref);
	if (rc != 0)
		return rc;

	pop = vos_oref2pop(oref);
	TX_BEGIN(pop) {
		rc = vos_oref_update(oref, epoch, dkey, nr, vios, sgls, NULL);
	} TX_ONABORT {
		D_DEBUG(DF_VOS1, "Failed to update object\n");
	} TX_END

	vos_obj_ref_release(vos_obj_cache_current(), oref);
	return rc;
}

/*
 * Zero-copy I/O functions
 */

/** help functions */

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
		  unsigned int vio_nr, daos_vec_iod_t *vios,
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

	zcc->zc_vec_nr = vio_nr;
	D_ALLOC(zcc->zc_vec_zbufs, zcc->zc_vec_nr * sizeof(*zcc->zc_vec_zbufs));
	if (zcc->zc_vec_zbufs == NULL)
		D_GOTO(failed, rc = -DER_NOMEM);

	zcc->zc_epoch = epoch;
	*zcc_pp = zcc;
	return 0;
 failed:
	vos_zcc_destroy(zcc);
	return rc;
}

/**
 * Free zero-copy buffers for @zcc, it returns false if it is called without
 * transactoin, but @zcc has pmem buffers. Otherwise it returns true.
 */
static int
vos_zcc_free_zbuf(struct vos_zc_context *zcc, bool has_tx)
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

			if (UMMID_IS_NULL(mmid))
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
vos_zcc_destroy(struct vos_zc_context *zcc)
{
	if (zcc->zc_vec_zbufs != NULL) {
		PMEMobjpool	*pop;
		bool		 done;

		done = vos_zcc_free_zbuf(zcc, false);
		if (!done) {
			D_ASSERT(zcc->zc_oref != NULL);
			pop = vos_oref2pop(zcc->zc_oref);

			TX_BEGIN(pop) {
				done = vos_zcc_free_zbuf(zcc, true);
				D_ASSERT(done);

			} TX_ONABORT {
				D_DEBUG(DF_VOS1, "Failed to free zcbuf\n");
			} TX_END
		}
	}

	if (zcc->zc_oref)
		vos_obj_ref_release(vos_obj_cache_current(), zcc->zc_oref);

	D_FREE_PTR(zcc);
}

static int
vos_oref_zc_fetch_prep(struct vos_obj_ref *oref, daos_epoch_t epoch,
		       daos_dkey_t *dkey, unsigned int vio_nr,
		       daos_vec_iod_t *vios, struct vos_zc_context *zcc)
{
	int	i;
	int	rc;

	/* NB: no cleanup in this function, vos_obj_zc_submit will release
	 * all the resources.
	 */
	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	for (i = 0; i < vio_nr; i++) {
		struct vos_vec_zbuf *zbuf = &zcc->zc_vec_zbufs[i];

		rc = daos_sgl_init(&zbuf->zb_sgl, vios[i].vd_nr);
		if (rc != 0) {
			D_DEBUG(DF_VOS1,
				"Failed to create sgl for vector %d\n", i);
			return rc;
		}

		rc = vos_vec_fetch(oref, epoch, dkey, &vios[i], &zbuf->zb_sgl);
		if (rc != 0) {
			D_DEBUG(DF_VOS1,
				"Failed to get ZC buffer for vector %d\n", i);
			return rc;
		}
	}
	return 0;
}

/**
 * Fetch an array of vectors from the specified object in zero-copy mode,
 * this function will create and return scatter/gather list which can address
 * vector data stored in pmem.
 */
int
vos_obj_zc_fetch_prep(daos_handle_t coh, daos_unit_oid_t oid,
		      daos_epoch_t epoch, daos_dkey_t *dkey,
		      unsigned int vio_nr, daos_vec_iod_t *vios,
		      daos_handle_t *ioh, daos_event_t *ev)
{
	struct vos_zc_context *zcc;
	int		       rc;

	rc = vos_zcc_create(coh, oid, epoch, vio_nr, vios, &zcc);
	if (rc != 0)
		return rc;

	rc = vos_oref_zc_fetch_prep(zcc->zc_oref, epoch, dkey, vio_nr,
				    vios, zcc);
	if (rc != 0)
		goto failed;

	D_DEBUG(DF_VOS2, "Prepared zcbufs for fetching %d vectors\n", vio_nr);
	*ioh = vos_zcc2ioh(zcc);
	return 0;
 failed:
	vos_obj_zc_submit(vos_zcc2ioh(zcc), dkey, vio_nr, vios, rc, NULL);
	return rc;
}

static daos_size_t
vos_recx2irec_size(daos_recx_t *recx)
{
	struct vos_rec_bundle	rbund;
	daos_csum_buf_t		csum;
	daos_iov_t		iov;

	daos_iov_set(&iov, NULL, recx->rx_rsize * recx->rx_nr);
	memset(&csum, 0, sizeof(csum)); /* XXX */

	rbund.rb_iov	= &iov;
	rbund.rb_csum	= &csum;
	return vos_irec_size(&rbund);
}

/**
 * Prepare pmem buffers for the zero-copy update.
 *
 * NB: no cleanup in this function, vos_obj_zc_submit will release all the
 * resources.
 */
static int
vos_vec_zc_update_prep(struct vos_obj_ref *oref, daos_vec_iod_t *vio,
		       struct vos_vec_zbuf *zbuf)
{
	int	i;
	int	rc;

	zbuf->zb_mmid_nr = vio->vd_nr;
	D_ALLOC(zbuf->zb_mmids, zbuf->zb_mmid_nr * sizeof(*zbuf->zb_mmids));
	if (zbuf->zb_mmids == NULL)
		return -DER_NOMEM;

	rc = daos_sgl_init(&zbuf->zb_sgl, vio->vd_nr);
	if (rc != 0)
		return -DER_NOMEM;

	for (i = 0; i < vio->vd_nr; i++) {
		daos_recx_t	*recx = &vio->vd_recxs[i];
		struct vos_irec	*irec;
		umem_id_t	 mmid;

		mmid = umem_alloc(vos_oref2umm(oref), vos_recx2irec_size(recx));
		if (UMMID_IS_NULL(mmid))
			return -DER_NOMEM;

		zbuf->zb_mmids[i] = mmid;

		/* return the pmem address, so upper layer stack can do RMA
		 * update for the record.
		 */
		irec = (struct vos_irec *)umem_id2ptr(vos_oref2umm(oref), mmid);
		daos_iov_set(&zbuf->zb_sgl.sg_iovs[i],
			     vos_irec2data(irec), recx->rx_rsize * recx->rx_nr);
	}
	return 0;
}

static int
vos_oref_zc_update_prep(struct vos_obj_ref *oref, unsigned int vio_nr,
			daos_vec_iod_t *vios, struct vos_zc_context *zcc)
{
	int	i;
	int	rc;

	D_ASSERT(oref == zcc->zc_oref);
	for (i = 0; i < vio_nr; i++) {
		rc = vos_vec_zc_update_prep(oref, &vios[i],
					   &zcc->zc_vec_zbufs[i]);
		if (rc != 0)
			return rc;
	}
	return 0;
}

/**
 * Create zero-copy buffers for the vectors to be updated. After storing data
 * in the returned ZC buffer, user should call vos_obj_zc_submit() to create
 * indices for these data buffers.
 */
int
vos_obj_zc_update_prep(daos_handle_t coh, daos_unit_oid_t oid,
		       daos_epoch_t epoch, daos_dkey_t *dkey,
		       unsigned int vio_nr, daos_vec_iod_t *vios,
		       daos_handle_t *ioh, daos_event_t *ev)
{
	struct vos_zc_context	*zcc;
	PMEMobjpool		*pop;
	int			 rc;

	rc = vos_zcc_create(coh, oid, epoch, vio_nr, vios, &zcc);
	if (rc != 0)
		return rc;

	zcc->zc_is_update = true;
	pop = vos_oref2pop(zcc->zc_oref);

	TX_BEGIN(pop) {
		rc = vos_oref_zc_update_prep(zcc->zc_oref, vio_nr, vios, zcc);
	} TX_ONABORT {
		D_DEBUG(DF_VOS1, "Failed to update object\n");
	} TX_END

	if (rc != 0)
		goto failed;

	D_DEBUG(DF_VOS2, "Prepared zcbufs for updating %d vectors\n", vio_nr);
	*ioh = vos_zcc2ioh(zcc);
	return 0;
 failed:
	vos_obj_zc_submit(vos_zcc2ioh(zcc), dkey, vio_nr, vios, rc, NULL);
	return rc;
}

/**
 * Submit the current zero-copy I/O operation to VOS and release responding
 * resources.
 */
int
vos_obj_zc_submit(daos_handle_t ioh, daos_dkey_t *dkey, unsigned int vio_nr,
		  daos_vec_iod_t *vios, int err, daos_event_t *ev)
{
	struct vos_zc_context	*zcc = vos_ioh2zcc(ioh);
	PMEMobjpool		*pop;

	if (err != 0 || !zcc->zc_is_update)
		goto out;

	D_ASSERT(zcc->zc_oref != NULL);
	pop = vos_oref2pop(zcc->zc_oref);

	TX_BEGIN(pop) {
		D_DEBUG(DF_VOS1, "Submit ZC update\n");
		err = vos_oref_update(zcc->zc_oref, zcc->zc_epoch, dkey,
				      vio_nr, vios, NULL, zcc);

	} TX_ONABORT {
		D_DEBUG(DF_VOS1, "Failed to submit ZC update\n");
	} TX_END
 out:
	vos_zcc_destroy(zcc);
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
