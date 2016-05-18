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

	eprange.epr_lo = epoch;
	eprange.epr_hi = DAOS_EPOCH_MAX;

	vos_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key	= dkey;

	vos_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= dkey; /* XXX dkey only */
	rbund.rb_csum	= &checksum;

	rc = dbtree_lookup(oref->or_toh, &kiov, &riov);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Failed to find the key: %d\n", rc);
		return rc;
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

	rc = dbtree_open_inplace(rbund.rb_btr, oref->or_vpuma, &toh);
	if (rc != 0) {
		D_DEBUG(DF_VOS1, "Failed to open subtree %d: %d\n",
			rbund.rb_btr->tr_class, rc);
		return rc;
	}

	/* XXX lazy assumption: value of each rex is stored in one iov */
	D_ASSERT(vio->vd_nr == sgl->sg_nr.num);

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

/**
 * Fetch an array of vectors from the specified object.
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_dkey_t *dkey, unsigned int nr, daos_vec_iod_t *vios,
	      daos_sg_list_t *sgls, daos_event_t *ev)
{
	struct vos_obj_ref *oref;
	int		    i;
	int		    rc;

	D_DEBUG(DF_VOS2, "Fetch "DF_UOID", desc_nr %d\n", DP_UOID(oid), nr);

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &oref);
	if (rc != 0)
		return rc;

	if (vos_obj_is_new(oref->or_obj)) {
		D_DEBUG(DF_VOS2, "New object, nothing to fetch\n");
		goto out;
	}

	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		goto out;

	for (i = 0; i < nr; i++) {
		rc = vos_vec_fetch(oref, epoch, dkey, &vios[i], &sgls[i]);
		if (rc != 0) {
			D_DEBUG(DF_VOS1, "Failed to fetch rec %d: %d\n", i, rc);
			goto out;
		}
	}
 out:
	vos_obj_ref_release(vos_obj_cache_current(), oref);
	return rc;
}

/** update a record extent */
static int
vos_recx_update(daos_handle_t toh, daos_epoch_range_t *epr, daos_dkey_t *dkey,
		daos_akey_t *rex_name, daos_recx_t *rex, daos_iov_t *iov,
		daos_csum_buf_t *csum)
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
	rbund.rb_mmid	= UMMID_NULL;

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

	epr->epr_lo = eprange.epr_hi;
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

	rc = dbtree_open_inplace(rbund.rb_btr, oref->or_vpuma, &toh);
	if (rc != 0)
		return rc;

	/* XXX lazy assumption: value of each rex is stored in one iov */
	D_ASSERT(sgl->sg_nr.num == vio->vd_nr);

	for (i = 0; i < vio->vd_nr; i++) {
		daos_epoch_range_t *epr	 = &eprange;
		daos_csum_buf_t	   *csum = &checksum;

		if (vio->vd_eprs != NULL)
			epr = &vio->vd_eprs[i];

		if (vio->vd_csums != NULL)
			csum = &vio->vd_csums[i];

		rc = vos_recx_update(toh, epr, dkey, &vio->vd_name,
				     &vio->vd_recxs[i], &sgl->sg_iovs[i],
				     csum);
		if (rc != 0)
			goto failed;
	}
 failed:
	rc = dbtree_close(toh);
	return rc;
}

static int
vos_obj_update_vecs(struct vos_obj_ref *oref, daos_epoch_t epoch,
		    daos_dkey_t *dkey, unsigned int nr, daos_vec_iod_t *vios,
		    daos_sg_list_t *sgls)
{
	int	rc;
	int	i;

	rc = vos_obj_tree_init(oref);
	if (rc != 0)
		return rc;

	for (i = 0; rc == 0 && i < nr; i++) {
		rc = vos_vec_update(oref, epoch, dkey, &vios[i], &sgls[i]);
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
	int			 rc;

	D_DEBUG(DF_VOS2, "Update "DF_UOID", desc_nr %d\n", DP_UOID(oid), nr);

	rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, oid, &oref);
	if (rc != 0)
		return rc;

	TX_BEGIN(oref->or_vphdl) {
		rc = vos_obj_update_vecs(oref, epoch, dkey, nr, vios, sgls);
	} TX_ONABORT {
		D_DEBUG(DF_MISC, "Failed to update object\n");
	} TX_END

	vos_obj_ref_release(vos_obj_cache_current(), oref);
	return rc;
}
