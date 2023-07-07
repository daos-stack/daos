/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include "vea_internal.h"

int
compound_vec_alloc(struct vea_space_info *vsi, struct vea_ext_vector *vec)
{
	/* TODO Add in in-memory extent vector tree */
	return 0;
}

static int
compound_alloc(struct vea_space_info *vsi, struct vea_free_extent *vfe,
	       struct vea_entry *entry)
{
	struct vea_free_extent	*remain;
	d_iov_t			 key;
	int			 rc;

	remain = &entry->ve_ext;
	D_ASSERT(remain->vfe_blk_cnt >= vfe->vfe_blk_cnt);
	D_ASSERT(remain->vfe_blk_off == vfe->vfe_blk_off);

	/* Remove the found free extent from compound index */
	free_class_remove(vsi, entry);

	if (remain->vfe_blk_cnt == vfe->vfe_blk_cnt) {
		d_iov_set(&key, &vfe->vfe_blk_off, sizeof(vfe->vfe_blk_off));
		rc = dbtree_delete(vsi->vsi_free_btr, BTR_PROBE_EQ, &key, NULL);
	} else {
		/* Adjust in-tree offset & length */
		remain->vfe_blk_off += vfe->vfe_blk_cnt;
		remain->vfe_blk_cnt -= vfe->vfe_blk_cnt;

		rc = free_class_add(vsi, entry);
	}

	return rc;
}

int
reserve_hint(struct vea_space_info *vsi, uint32_t blk_cnt,
	     struct vea_resrvd_ext *resrvd)
{
	struct vea_free_extent vfe;
	struct vea_entry *entry;
	d_iov_t key, val;
	int rc;

	/* No hint offset provided */
	if (resrvd->vre_hint_off == VEA_HINT_OFF_INVAL)
		return 0;

	vfe.vfe_blk_off = resrvd->vre_hint_off;
	vfe.vfe_blk_cnt = blk_cnt;

	/* Fetch & operate on the in-tree record */
	d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
	d_iov_set(&val, NULL, 0);

	D_ASSERT(daos_handle_is_valid(vsi->vsi_free_btr));
	rc = dbtree_fetch(vsi->vsi_free_btr, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT,
			  &key, NULL, &val);
	if (rc)
		return (rc == -DER_NONEXIST) ? 0 : rc;

	entry = (struct vea_entry *)val.iov_buf;
	/* The matching free extent isn't big enough */
	if (entry->ve_ext.vfe_blk_cnt < vfe.vfe_blk_cnt)
		return 0;

	rc = compound_alloc(vsi, &vfe, entry);
	if (rc)
		return rc;

	resrvd->vre_blk_off = vfe.vfe_blk_off;
	resrvd->vre_blk_cnt = vfe.vfe_blk_cnt;

	inc_stats(vsi, STAT_RESRV_HINT, 1);

	D_DEBUG(DB_IO, "["DF_U64", %u]\n", resrvd->vre_blk_off,
		resrvd->vre_blk_cnt);

	return 0;
}

static int
reserve_small(struct vea_space_info *vsi, uint32_t blk_cnt,
	      struct vea_resrvd_ext *resrvd)
{
	daos_handle_t		 btr_hdl;
	struct vea_sized_class	*sc;
	struct vea_free_extent	 vfe;
	struct vea_entry	*entry;
	d_iov_t			 key, val_out;
	uint64_t		 int_key = blk_cnt;
	int			 rc;

	/* Skip huge allocate request */
	if (blk_cnt > vsi->vsi_class.vfc_large_thresh)
		return 0;

	btr_hdl = vsi->vsi_class.vfc_size_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));

	d_iov_set(&key, &int_key, sizeof(int_key));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_fetch(btr_hdl, BTR_PROBE_GE, DAOS_INTENT_DEFAULT, &key, NULL, &val_out);
	if (rc == -DER_NONEXIST) {
		return 0;
	} else if (rc) {
		D_ERROR("Search size class:%u failed. "DF_RC"\n", blk_cnt, DP_RC(rc));
		return rc;
	}

	sc = (struct vea_sized_class *)val_out.iov_buf;
	D_ASSERT(sc != NULL);
	D_ASSERT(!d_list_empty(&sc->vsc_lru));

	/* Get the least used item from head */
	entry = d_list_entry(sc->vsc_lru.next, struct vea_entry, ve_link);
	D_ASSERT(entry->ve_sized_class == sc);
	D_ASSERT(entry->ve_ext.vfe_blk_cnt >= blk_cnt);

	vfe.vfe_blk_off = entry->ve_ext.vfe_blk_off;
	vfe.vfe_blk_cnt = blk_cnt;

	rc = compound_alloc(vsi, &vfe, entry);
	if (rc)
		return rc;

	resrvd->vre_blk_off = vfe.vfe_blk_off;
	resrvd->vre_blk_cnt = blk_cnt;
	inc_stats(vsi, STAT_RESRV_SMALL, 1);

	D_DEBUG(DB_IO, "["DF_U64", %u]\n", resrvd->vre_blk_off, resrvd->vre_blk_cnt);

	return rc;
}

int
reserve_single(struct vea_space_info *vsi, uint32_t blk_cnt,
	       struct vea_resrvd_ext *resrvd)
{
	struct vea_free_class *vfc = &vsi->vsi_class;
	struct vea_free_extent vfe;
	struct vea_entry *entry;
	struct d_binheap_node *root;
	int rc;

	/* No large free extent available */
	if (d_binheap_is_empty(&vfc->vfc_heap))
		return reserve_small(vsi, blk_cnt, resrvd);

	root = d_binheap_root(&vfc->vfc_heap);
	entry = container_of(root, struct vea_entry, ve_node);

	D_ASSERT(entry->ve_ext.vfe_blk_cnt > vfc->vfc_large_thresh);
	D_DEBUG(DB_IO, "largest free extent ["DF_U64", %u]\n",
	       entry->ve_ext.vfe_blk_off, entry->ve_ext.vfe_blk_cnt);

	/* The largest free extent can't satisfy huge allocate request */
	if (entry->ve_ext.vfe_blk_cnt < blk_cnt)
		return 0;

	/*
	 * If the largest free extent is large enough for splitting, divide it in
	 * half-and-half then reserve from the second half, otherwise, try to
	 * reserve from the small extents first, if it fails, reserve from the
	 * largest free extent.
	 */
	if (entry->ve_ext.vfe_blk_cnt <= (max(blk_cnt, vfc->vfc_large_thresh) * 2)) {
		/* Try small extents first */
		rc = reserve_small(vsi, blk_cnt, resrvd);
		if (rc != 0 || resrvd->vre_blk_cnt != 0)
			return rc;

		vfe.vfe_blk_off = entry->ve_ext.vfe_blk_off;
		vfe.vfe_blk_cnt = blk_cnt;

		rc = compound_alloc(vsi, &vfe, entry);
		if (rc)
			return rc;

	} else {
		uint32_t half_blks, tot_blks;
		uint64_t blk_off;

		blk_off = entry->ve_ext.vfe_blk_off;
		tot_blks = entry->ve_ext.vfe_blk_cnt;
		half_blks = tot_blks >> 1;
		D_ASSERT(tot_blks >= (half_blks + blk_cnt));

		/* Shrink the original extent to half size */
		free_class_remove(vsi, entry);
		entry->ve_ext.vfe_blk_cnt = half_blks;
		rc = free_class_add(vsi, entry);
		if (rc)
			return rc;

		/* Add the remaining part of second half */
		if (tot_blks > (half_blks + blk_cnt)) {
			vfe.vfe_blk_off = blk_off + half_blks + blk_cnt;
			vfe.vfe_blk_cnt = tot_blks - half_blks - blk_cnt;
			vfe.vfe_age = 0;	/* Not used */

			rc = compound_free(vsi, &vfe, VEA_FL_NO_MERGE |
						VEA_FL_NO_ACCOUNTING);
			if (rc)
				return rc;
		}
		vfe.vfe_blk_off = blk_off + half_blks;
	}

	resrvd->vre_blk_off = vfe.vfe_blk_off;
	resrvd->vre_blk_cnt = blk_cnt;

	inc_stats(vsi, STAT_RESRV_LARGE, 1);

	D_DEBUG(DB_IO, "["DF_U64", %u]\n", resrvd->vre_blk_off,
		resrvd->vre_blk_cnt);

	return 0;
}

int
reserve_vector(struct vea_space_info *vsi, uint32_t blk_cnt,
	       struct vea_resrvd_ext *resrvd)
{
	/* TODO reserve extent vector for non-contiguous allocation */
	return -DER_NOSPACE;
}

int
persistent_alloc(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_free_extent *found, frag = {0};
	daos_handle_t btr_hdl;
	d_iov_t key_in, key_out, val;
	uint64_t *blk_off, found_end, vfe_end;
	int rc, opc = BTR_PROBE_LE;

	D_ASSERT(umem_tx_inprogress(vsi->vsi_umem) ||
		 vsi->vsi_umem->umm_id == UMEM_CLASS_VMEM);
	D_ASSERT(vfe->vfe_blk_off != VEA_HINT_OFF_INVAL);
	D_ASSERT(vfe->vfe_blk_cnt > 0);

	btr_hdl = vsi->vsi_md_free_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));

	D_DEBUG(DB_IO, "Persistent alloc ["DF_U64", %u]\n",
		vfe->vfe_blk_off, vfe->vfe_blk_cnt);

	/* Fetch & operate on the in-tree record */
	d_iov_set(&key_in, &vfe->vfe_blk_off, sizeof(vfe->vfe_blk_off));
	d_iov_set(&key_out, NULL, sizeof(*blk_off));
	d_iov_set(&val, NULL, sizeof(*found));

	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out,
			  &val);
	if (rc) {
		D_ERROR("failed to find extent ["DF_U64", %u]\n",
			vfe->vfe_blk_off, vfe->vfe_blk_cnt);
		return rc;
	}

	found = (struct vea_free_extent *)val.iov_buf;
	blk_off = (uint64_t *)key_out.iov_buf;

	rc = verify_free_entry(blk_off, found);
	if (rc)
		return rc;

	found_end = found->vfe_blk_off + found->vfe_blk_cnt;
	vfe_end = vfe->vfe_blk_off + vfe->vfe_blk_cnt;

	if (found->vfe_blk_off > vfe->vfe_blk_off || found_end < vfe_end) {
		D_ERROR("mismatched extent ["DF_U64", %u] ["DF_U64", %u]\n",
			found->vfe_blk_off, found->vfe_blk_cnt,
			vfe->vfe_blk_off, vfe->vfe_blk_cnt);
		return -DER_INVAL;
	}

	if (found->vfe_blk_off < vfe->vfe_blk_off) {
		/* Adjust the in-tree free extent length */
		rc = umem_tx_add_ptr(vsi->vsi_umem, &found->vfe_blk_cnt,
				     sizeof(found->vfe_blk_cnt));
		if (rc)
			return rc;

		found->vfe_blk_cnt = vfe->vfe_blk_off - found->vfe_blk_off;

		/* Add back the rear part of free extent */
		if (found_end > vfe_end) {
			frag.vfe_blk_off = vfe->vfe_blk_off + vfe->vfe_blk_cnt;
			frag.vfe_blk_cnt = found_end - vfe_end;

			d_iov_set(&key_in, &frag.vfe_blk_off,
				  sizeof(frag.vfe_blk_off));
			d_iov_set(&val, &frag, sizeof(frag));
			rc = dbtree_update(btr_hdl, &key_in, &val);
			if (rc)
				return rc;
		}
	} else if (found_end > vfe_end) {
		/* Adjust the in-tree extent offset & length */
		rc = umem_tx_add_ptr(vsi->vsi_umem, found, sizeof(*found));
		if (rc)
			return rc;

		found->vfe_blk_off = vfe->vfe_blk_off + vfe->vfe_blk_cnt;
		found->vfe_blk_cnt = found_end - vfe_end;
	} else {
		/* Remove the original free extent from persistent tree */
		rc = dbtree_delete(btr_hdl, BTR_PROBE_BYPASS, &key_out, NULL);
		if (rc)
			return rc;
	}

	return 0;
}
