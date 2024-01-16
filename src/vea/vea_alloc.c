/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include "vea_internal.h"

static int
compound_alloc_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe,
		      struct vea_extent_entry *entry)
{
	struct vea_free_extent	*remain;
	d_iov_t			 key;
	int			 rc;

	remain = &entry->vee_ext;
	D_ASSERT(remain->vfe_blk_cnt >= vfe->vfe_blk_cnt);
	D_ASSERT(remain->vfe_blk_off == vfe->vfe_blk_off);

	/* Remove the found free extent from compound index */
	extent_free_class_remove(vsi, entry);

	if (remain->vfe_blk_cnt == vfe->vfe_blk_cnt) {
		d_iov_set(&key, &vfe->vfe_blk_off, sizeof(vfe->vfe_blk_off));
		rc = dbtree_delete(vsi->vsi_free_btr, BTR_PROBE_EQ, &key, NULL);
	} else {
		/* Adjust in-tree offset & length */
		remain->vfe_blk_off += vfe->vfe_blk_cnt;
		remain->vfe_blk_cnt -= vfe->vfe_blk_cnt;

		rc = extent_free_class_add(vsi, entry);
	}

	return rc;
}

int
reserve_hint(struct vea_space_info *vsi, uint32_t blk_cnt,
	     struct vea_resrvd_ext *resrvd)
{
	struct vea_free_extent vfe;
	struct vea_extent_entry *entry;
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

	entry = (struct vea_extent_entry *)val.iov_buf;
	/* The matching free extent isn't big enough */
	if (entry->vee_ext.vfe_blk_cnt < vfe.vfe_blk_cnt)
		return 0;

	rc = compound_alloc_extent(vsi, &vfe, entry);
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
	      struct vea_resrvd_ext *resrvd);
static int
reserve_size_tree(struct vea_space_info *vsi, uint32_t blk_cnt,
		  struct vea_resrvd_ext *resrvd);

static int
reserve_extent(struct vea_space_info *vsi, uint32_t blk_cnt,
	       struct vea_resrvd_ext *resrvd)
{
	struct vea_free_class *vfc = &vsi->vsi_class;
	struct vea_free_extent vfe;
	struct vea_extent_entry *entry;
	struct d_binheap_node *root;
	int rc;

	if (d_binheap_is_empty(&vfc->vfc_heap))
		return 0;

	root = d_binheap_root(&vfc->vfc_heap);
	entry = container_of(root, struct vea_extent_entry, vee_node);

	D_ASSERT(entry->vee_ext.vfe_blk_cnt > vfc->vfc_large_thresh);
	D_DEBUG(DB_IO, "largest free extent ["DF_U64", %u]\n",
	       entry->vee_ext.vfe_blk_off, entry->vee_ext.vfe_blk_cnt);

	/* The largest free extent can't satisfy huge allocate request */
	if (entry->vee_ext.vfe_blk_cnt < blk_cnt)
		return 0;

	/*
	 * If the largest free extent is large enough for splitting, divide it in
	 * half-and-half then reserve from the second half, otherwise, try to
	 * reserve from the small extents first, if it fails, reserve from the
	 * largest free extent.
	 */
	if (entry->vee_ext.vfe_blk_cnt <= (max(blk_cnt, vfc->vfc_large_thresh) * 2)) {
		vfe.vfe_blk_off = entry->vee_ext.vfe_blk_off;
		vfe.vfe_blk_cnt = blk_cnt;

		rc = compound_alloc_extent(vsi, &vfe, entry);
		if (rc)
			return rc;

	} else {
		uint32_t half_blks, tot_blks;
		uint64_t blk_off;

		blk_off = entry->vee_ext.vfe_blk_off;
		tot_blks = entry->vee_ext.vfe_blk_cnt;
		half_blks = tot_blks >> 1;
		D_ASSERT(tot_blks >= (half_blks + blk_cnt));

		/* Shrink the original extent to half size */
		extent_free_class_remove(vsi, entry);
		entry->vee_ext.vfe_blk_cnt = half_blks;
		rc = extent_free_class_add(vsi, entry);
		if (rc)
			return rc;

		/* Add the remaining part of second half */
		if (tot_blks > (half_blks + blk_cnt)) {
			vfe.vfe_blk_off = blk_off + half_blks + blk_cnt;
			vfe.vfe_blk_cnt = tot_blks - half_blks - blk_cnt;
			vfe.vfe_age = 0;	/* Not used */

			rc = compound_free_extent(vsi, &vfe, VEA_FL_NO_MERGE |
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

static int
reserve_size_tree(struct vea_space_info *vsi, uint32_t blk_cnt,
		  struct vea_resrvd_ext *resrvd)
{
	daos_handle_t		 btr_hdl;
	struct vea_sized_class	*sc;
	struct vea_free_extent	 vfe;
	struct vea_extent_entry	*extent_entry;
	d_iov_t			 key, val_out;
	uint64_t		 int_key = blk_cnt;
	int			 rc;

	btr_hdl = vsi->vsi_class.vfc_size_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));

	d_iov_set(&key, &int_key, sizeof(int_key));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_fetch(btr_hdl, BTR_PROBE_GE, DAOS_INTENT_DEFAULT, &key, NULL, &val_out);
	if (rc == -DER_NONEXIST)
		return 0;
	else if (rc)
		return rc;

	sc = (struct vea_sized_class *)val_out.iov_buf;
	D_ASSERT(sc != NULL);

	/* Get the least used item from head */
	extent_entry = d_list_entry(sc->vsc_extent_lru.next, struct vea_extent_entry, vee_link);
	D_ASSERT(extent_entry->vee_sized_class == sc);
	D_ASSERT(extent_entry->vee_ext.vfe_blk_cnt >= blk_cnt);

	vfe.vfe_blk_off = extent_entry->vee_ext.vfe_blk_off;
	vfe.vfe_blk_cnt = blk_cnt;

	rc = compound_alloc_extent(vsi, &vfe, extent_entry);
	if (rc)
		return rc;
	resrvd->vre_blk_off = vfe.vfe_blk_off;
	resrvd->vre_blk_cnt = blk_cnt;
	resrvd->vre_private = NULL;
	inc_stats(vsi, STAT_RESRV_SMALL, 1);

	return 0;
}

static int
reserve_bitmap_chunk(struct vea_space_info *vsi, uint32_t blk_cnt,
		     struct vea_resrvd_ext *resrvd)
{
	int			 rc;

	/* Get hint offset */
	hint_get(vsi->vsi_bitmap_hint_context, &resrvd->vre_hint_off);

	/* Reserve from hint offset */
	if (resrvd->vre_hint_off != VEA_HINT_OFF_INVAL) {
		rc = reserve_hint(vsi, blk_cnt, resrvd);
		if (rc != 0)
			return rc;
		else if (resrvd->vre_blk_cnt != 0)
			goto done;
	}

	if (blk_cnt >= vsi->vsi_class.vfc_large_thresh)
		goto extent;

	rc = reserve_size_tree(vsi, blk_cnt, resrvd);
	if (rc)
		return rc;

	if (resrvd->vre_blk_cnt > 0)
		goto done;

extent:
	rc = reserve_extent(vsi, blk_cnt, resrvd);
	if (resrvd->vre_blk_cnt <= 0)
		return -DER_NOSPACE;
done:
	D_ASSERT(resrvd->vre_blk_off != VEA_HINT_OFF_INVAL);
	D_ASSERT(resrvd->vre_blk_cnt == blk_cnt);
	dec_stats(vsi, STAT_FREE_EXTENT_BLKS, blk_cnt);

	/* Update hint offset */
	hint_update(vsi->vsi_bitmap_hint_context, resrvd->vre_blk_off + blk_cnt,
		    &resrvd->vre_hint_seq);
	return rc;
}

#define LARGE_EXT_FREE_BLKS	((32UL << 30) / VEA_BLK_SZ)

static inline uint32_t
get_bitmap_chunk_blks(struct vea_space_info *vsi, uint32_t blk_cnt)
{
	uint32_t chunk_blks = VEA_BITMAP_MIN_CHUNK_BLKS;

	D_ASSERT(blk_cnt <= VEA_MAX_BITMAP_CLASS);
	chunk_blks *= blk_cnt;

	D_ASSERT(chunk_blks <= VEA_BITMAP_MAX_CHUNK_BLKS);
	/*
	 * Always try to allocate large bitmap chunk if there
	 * is enough free extent blocks.
	 */
	if (vsi->vsi_stat[STAT_FREE_EXTENT_BLKS] >= LARGE_EXT_FREE_BLKS) {
		int times = VEA_BITMAP_MAX_CHUNK_BLKS / chunk_blks;

		if (times > 1)
			chunk_blks *= times;
	}

	/* should be aligned with 64 bits */
	D_ASSERT(chunk_blks % (blk_cnt * 64) == 0);

	return chunk_blks;
}

static inline int
get_bitmap_sz(uint32_t chunk_blks, uint16_t class)
{
	int bits = chunk_blks / class;

	D_ASSERT(chunk_blks % class == 0);
	D_ASSERT(bits % 64 == 0);

	return bits / 64;
}

static int
reserve_bitmap(struct vea_space_info *vsi, uint32_t blk_cnt,
	      struct vea_resrvd_ext *resrvd)
{
	struct vea_bitmap_entry *bitmap_entry, *tmp_entry;
	struct vea_bitmap_entry *entry;
	int			 rc;
	struct vea_free_bitmap	*vfb;
	struct vea_free_bitmap	 new_vfb = { 0 };
	int			 bits = 1;
	uint32_t		 chunk_blks;
	int			 bitmap_sz;
	d_list_t		*list_head;

	if (!is_bitmap_feature_enabled(vsi))
		return 0;

	if (blk_cnt > VEA_MAX_BITMAP_CLASS)
		return 0;

	D_ASSERT(blk_cnt > 0);
	/* reserve from bitmap */
	d_list_for_each_entry_safe(bitmap_entry, tmp_entry,
				   &vsi->vsi_class.vfc_bitmap_lru[blk_cnt - 1], vbe_link) {
		vfb = &bitmap_entry->vbe_bitmap;
		D_ASSERT(vfb->vfb_class == blk_cnt);
		/* Only assert in server mode */
		if (vsi->vsi_unmap_ctxt.vnc_ext_flush)
			D_ASSERT(bitmap_entry->vbe_published_state != VEA_BITMAP_STATE_PUBLISHING);
		rc = daos_find_bits(vfb->vfb_bitmaps, NULL, vfb->vfb_bitmap_sz, 1, &bits);
		if (rc < 0) {
			d_list_del_init(&bitmap_entry->vbe_link);
			continue;
		}

		D_ASSERT(rc * blk_cnt + blk_cnt <= vfb->vfb_blk_cnt);
		resrvd->vre_blk_off = vfb->vfb_blk_off + (rc * blk_cnt);
		resrvd->vre_blk_cnt = blk_cnt;
		resrvd->vre_private = (void *)bitmap_entry;
		setbits64(vfb->vfb_bitmaps, rc, 1);
		rc = 0;
		inc_stats(vsi, STAT_RESRV_BITMAP, 1);
		return 0;
	}

	list_head = &vsi->vsi_class.vfc_bitmap_empty[blk_cnt - 1];
	if (!d_list_empty(list_head)) {
		bitmap_entry = d_list_entry(list_head->next, struct vea_bitmap_entry,
					    vbe_link);
		if (vsi->vsi_unmap_ctxt.vnc_ext_flush)
			D_ASSERT(bitmap_entry->vbe_published_state != VEA_BITMAP_STATE_PUBLISHING);
		vfb = &bitmap_entry->vbe_bitmap;
		D_ASSERT(vfb->vfb_class == blk_cnt);
		resrvd->vre_blk_off = vfb->vfb_blk_off;
		resrvd->vre_blk_cnt = blk_cnt;
		resrvd->vre_private = (void *)bitmap_entry;
		setbits64(vfb->vfb_bitmaps, 0, 1);
		inc_stats(vsi, STAT_RESRV_BITMAP, 1);
		d_list_move_tail(&bitmap_entry->vbe_link,
				 &vsi->vsi_class.vfc_bitmap_lru[blk_cnt - 1]);
		return 0;
	}

	chunk_blks = get_bitmap_chunk_blks(vsi, blk_cnt);
	bitmap_sz = get_bitmap_sz(chunk_blks, blk_cnt);
	rc = reserve_bitmap_chunk(vsi, chunk_blks, resrvd);
	if (resrvd->vre_blk_cnt <= 0)
		return 0;

	resrvd->vre_new_bitmap_chunk = 1;

	new_vfb.vfb_blk_off = resrvd->vre_blk_off;
	new_vfb.vfb_class = blk_cnt;
	new_vfb.vfb_blk_cnt = chunk_blks;
	new_vfb.vfb_bitmap_sz = bitmap_sz;
	rc = bitmap_entry_insert(vsi, &new_vfb, VEA_BITMAP_STATE_NEW,
				 &entry, VEA_FL_NO_ACCOUNTING);
	if (rc)
		return rc;

	resrvd->vre_blk_cnt = blk_cnt;
	resrvd->vre_private = (void *)entry;

	D_DEBUG(DB_IO, "["DF_U64", %u]\n", resrvd->vre_blk_off, resrvd->vre_blk_cnt);
	inc_stats(vsi, STAT_FREE_BITMAP_BLKS, chunk_blks);
	inc_stats(vsi, STAT_RESRV_BITMAP, 1);

	return rc;
}

static int
reserve_small(struct vea_space_info *vsi, uint32_t blk_cnt,
	      struct vea_resrvd_ext *resrvd)
{
	int			 rc;

	/* Skip huge allocate request */
	if (blk_cnt >= vsi->vsi_class.vfc_large_thresh)
		return 0;

	rc = reserve_bitmap(vsi, blk_cnt, resrvd);
	if (rc || resrvd->vre_blk_cnt > 0)
		return rc;

	return reserve_size_tree(vsi, blk_cnt, resrvd);
}

int
reserve_single(struct vea_space_info *vsi, uint32_t blk_cnt,
	       struct vea_resrvd_ext *resrvd)
{
	struct vea_free_class	*vfc = &vsi->vsi_class;
	int			 rc;

	/* No large free extent available */
	if (d_binheap_is_empty(&vfc->vfc_heap))
		return reserve_small(vsi, blk_cnt, resrvd);

	if (blk_cnt < vsi->vsi_class.vfc_large_thresh) {
		rc = reserve_small(vsi, blk_cnt, resrvd);
		if (rc || resrvd->vre_blk_cnt > 0)
			return rc;
	}

	return reserve_extent(vsi, blk_cnt, resrvd);
}

static int
persistent_alloc_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe)
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

int
bitmap_tx_add_ptr(struct umem_instance *vsi_umem, uint64_t *bitmap,
		  uint32_t bit_at, uint32_t bits_nr)
{
       uint32_t bitmap_off = bit_at / 8;
       uint32_t bitmap_sz = 0;

       if (bit_at % 8)
	       bitmap_sz = 1;

       if (bits_nr > (bit_at % 8))
	       bitmap_sz += (bits_nr - (bit_at % 8) + 7) / 8;

	return umem_tx_add_ptr(vsi_umem, (char *)bitmap + bitmap_off, bitmap_sz);
}

int
bitmap_set_range(struct umem_instance *vsi_umem, struct vea_free_bitmap *bitmap,
		 uint64_t blk_off, uint32_t blk_cnt, bool clear)
{
	uint32_t	bit_at, bits_nr;
	int		rc;

	if (blk_off < bitmap->vfb_blk_off ||
	    blk_off + blk_cnt > bitmap->vfb_blk_off + bitmap->vfb_blk_cnt) {
		D_ERROR("range ["DF_U64", %u] is not within bitmap ["DF_U64", %u]\n",
			blk_off, blk_cnt, bitmap->vfb_blk_off, bitmap->vfb_blk_cnt);
		return -DER_INVAL;
	}

	bit_at = blk_off - bitmap->vfb_blk_off;
	if (bit_at % bitmap->vfb_class != 0) {
		D_ERROR("invalid block offset: "DF_U64" which is not times of %u\n",
			blk_off, bitmap->vfb_class);
		return -DER_INVAL;
	}
	if (blk_cnt % bitmap->vfb_class != 0) {
		D_ERROR("invalid block count: %u which is not times of %u\n",
			blk_cnt, bitmap->vfb_class);
		return -DER_INVAL;
	}
	bit_at /= bitmap->vfb_class;
	bits_nr = blk_cnt / bitmap->vfb_class;
	if (clear) {
		if (!isset_range((uint8_t *)bitmap->vfb_bitmaps,
				 bit_at, bit_at + bits_nr - 1)) {
			D_ERROR("bitmap already cleared in the range.\n");
			return -DER_INVAL;
		}
	} else {
		if (!isclr_range((uint8_t *)bitmap->vfb_bitmaps,
				 bit_at, bit_at + bits_nr - 1)) {
			D_ERROR("bitmap already set in the range.["DF_U64", %u]\n",
				 blk_off, blk_cnt);
			return -DER_INVAL;
		}
	}

	if (vsi_umem) {
		rc = bitmap_tx_add_ptr(vsi_umem, bitmap->vfb_bitmaps, bit_at, bits_nr);
		if (rc)
			return rc;
	}

	D_ASSERT(bit_at + bits_nr <= bitmap->vfb_bitmap_sz * 64);
	if (clear)
		clrbits64(bitmap->vfb_bitmaps, bit_at, bits_nr);
	else
		setbits64(bitmap->vfb_bitmaps, bit_at, bits_nr);

	return 0;
}

static void
new_chunk_commit_cb(void *data, bool noop)
{
	struct vea_bitmap_entry	*bitmap_entry = (struct vea_bitmap_entry *)data;

	if (noop)
		return;

	bitmap_entry->vbe_published_state = VEA_BITMAP_STATE_PUBLISHED;
}

static void
new_chunk_abort_cb(void *data, bool noop)
{
	struct vea_bitmap_entry	*bitmap_entry = (struct vea_bitmap_entry *)data;

	if (noop)
		return;

	bitmap_entry->vbe_published_state = VEA_BITMAP_STATE_NEW;
}

int
persistent_alloc(struct vea_space_info *vsi, struct vea_free_entry *vfe)
{
	struct vea_bitmap_entry *bitmap_entry = vfe->vfe_bitmap;

	if (bitmap_entry == NULL)
		return persistent_alloc_extent(vsi, &vfe->vfe_ext);

	D_ASSERT(bitmap_entry != NULL);

	/* if this bitmap is new */
	if (bitmap_entry->vbe_published_state == VEA_BITMAP_STATE_NEW) {
		d_iov_t			 key, val, val_out;
		struct vea_free_bitmap	*bitmap;
		int			 rc;
		struct vea_free_extent   extent;
		daos_handle_t		 btr_hdl = vsi->vsi_md_bitmap_btr;
		rc = umem_tx_begin(vsi->vsi_umem, vsi->vsi_txd);
		if (rc != 0)
			return rc;

		rc = umem_tx_add_callback(vsi->vsi_umem, vsi->vsi_txd, UMEM_STAGE_ONABORT,
					  new_chunk_abort_cb, bitmap_entry);
		if (rc) {
			D_ERROR("add chunk abort callback failed. "DF_RC"\n", DP_RC(rc));
			goto out;
		}

		bitmap_entry->vbe_published_state = VEA_BITMAP_STATE_PUBLISHING;

		rc = umem_tx_add_callback(vsi->vsi_umem, vsi->vsi_txd, UMEM_STAGE_ONCOMMIT,
					  new_chunk_commit_cb, bitmap_entry);
		if (rc) {
			D_ERROR("add chunk commit callback failed. "DF_RC"\n", DP_RC(rc));
			goto out;
		}

		extent = vfe->vfe_ext;
		extent.vfe_blk_off = bitmap_entry->vbe_bitmap.vfb_blk_off;
		extent.vfe_blk_cnt = bitmap_entry->vbe_bitmap.vfb_blk_cnt;
		rc = persistent_alloc_extent(vsi, &extent);
		if (rc)
			goto out;

		D_ALLOC(bitmap, alloc_free_bitmap_size(bitmap_entry->vbe_bitmap.vfb_bitmap_sz));
		if (!bitmap) {
			rc = -DER_NOMEM;
			goto out;
		}

		D_ASSERT(vfe->vfe_ext.vfe_blk_cnt != 0);
		bitmap->vfb_blk_off = extent.vfe_blk_off;
		bitmap->vfb_class = bitmap_entry->vbe_bitmap.vfb_class;
		bitmap->vfb_blk_cnt = bitmap_entry->vbe_bitmap.vfb_blk_cnt;
		bitmap->vfb_bitmap_sz = bitmap_entry->vbe_bitmap.vfb_bitmap_sz;
		rc = bitmap_set_range(NULL, bitmap, vfe->vfe_ext.vfe_blk_off,
				      vfe->vfe_ext.vfe_blk_cnt, false);
		if (rc) {
			D_FREE(bitmap);
			goto out;
		}
		/* Add to persistent bitmap tree */
		D_ASSERT(daos_handle_is_valid(btr_hdl));
		d_iov_set(&key, &bitmap->vfb_blk_off, sizeof(bitmap->vfb_blk_off));
		d_iov_set(&val, bitmap, alloc_free_bitmap_size(bitmap->vfb_bitmap_sz));
		d_iov_set(&val_out, NULL, 0);

		rc = dbtree_upsert(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &key,
				   &val, &val_out);
		D_FREE(bitmap);
		if (rc)
			D_ERROR("Insert persistent bitmap failed. "DF_RC"\n", DP_RC(rc));
		else
			bitmap_entry->vbe_md_bitmap = (struct vea_free_bitmap *)val_out.iov_buf;
out:
		/* Commit/Abort transaction on success/error */
		rc = rc ? umem_tx_abort(vsi->vsi_umem, rc) : umem_tx_commit(vsi->vsi_umem);

		return rc;
	}

	return bitmap_set_range(vsi->vsi_umem, vfe->vfe_bitmap->vbe_md_bitmap,
				vfe->vfe_ext.vfe_blk_off, vfe->vfe_ext.vfe_blk_cnt, false);
}
