/**
 * (C) Copyright 2018-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include <daos/btree_class.h>
#include "vea_internal.h"

enum vea_free_type {
	VEA_TYPE_COMPOUND,
	VEA_TYPE_AGGREGATE,
	VEA_TYPE_PERSIST,
};

int
free_type(struct vea_space_info *vsi, uint64_t blk_off, uint32_t blk_cnt,
	  struct vea_bitmap_entry **bitmap_entry)
{
	int			 type = VEA_FREE_ENTRY_BITMAP;
	struct vea_free_bitmap	*found;
	daos_handle_t		 btr_hdl = vsi->vsi_bitmap_btr;
	d_iov_t			 key_in, key_out, val;
	uint64_t		 found_end, vfe_end;
	int			 rc, opc = BTR_PROBE_LE;
	struct vea_bitmap_entry	*entry = NULL;

	if (blk_cnt > VEA_BITMAP_MAX_CHUNK_BLKS) {
		type = VEA_FREE_ENTRY_EXTENT;
		goto out;
	}

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	/* Fetch the in-tree record */
	d_iov_set(&key_in, &blk_off, sizeof(blk_off));
	d_iov_set(&key_out, NULL, sizeof(blk_off));
	d_iov_set(&val, NULL, 0);

	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out, &val);
	if (rc == -DER_NONEXIST)
		return VEA_FREE_ENTRY_EXTENT;

	if (rc) {
		D_ERROR("failed to search range ["DF_U64", %u] int bitmap tree\n",
			blk_off, blk_cnt);
		return rc;
	}

	entry = (struct vea_bitmap_entry *)val.iov_buf;
	found = &entry->vbe_bitmap;
	rc = verify_bitmap_entry(found);
	if (rc) {
		D_ERROR("verify bitmap failed in free_type\n");
		return rc;
	}

	found_end = found->vfb_blk_off + found->vfb_blk_cnt - 1;
	vfe_end = blk_off + blk_cnt - 1;
	D_ASSERT(blk_off >= found->vfb_blk_off);
	if (blk_off <= found_end) {
		if (vfe_end <= found_end) {
			if (bitmap_entry)
				*bitmap_entry = entry;
			return VEA_FREE_ENTRY_BITMAP;
		}

		D_CRIT("["DF_U64", %u] should not cross bitmap tree\n",
			found->vfb_blk_off, found->vfb_blk_cnt);
		return -DER_INVAL;
	} else {
		type = VEA_FREE_ENTRY_EXTENT;
	}
out:
	return type;
}

void
extent_free_class_remove(struct vea_space_info *vsi, struct vea_extent_entry *entry)
{
	struct vea_free_class	*vfc = &vsi->vsi_class;
	struct vea_sized_class	*sc = entry->vee_sized_class;
	uint32_t		 blk_cnt;

	if (sc == NULL) {
		blk_cnt = entry->vee_ext.vfe_blk_cnt;
		D_ASSERTF(blk_cnt > vfc->vfc_large_thresh, "%u <= %u",
			  blk_cnt, vfc->vfc_large_thresh);
		D_ASSERT(d_list_empty(&entry->vee_link));

		d_binheap_remove(&vfc->vfc_heap, &entry->vee_node);
		dec_stats(vsi, STAT_FRAGS_LARGE, 1);
	} else {
		d_iov_t		key;
		int		rc;

		blk_cnt = entry->vee_ext.vfe_blk_cnt;
		D_ASSERTF(blk_cnt > 0 && blk_cnt <= vfc->vfc_large_thresh,
			  "%u > %u", blk_cnt, vfc->vfc_large_thresh);
		D_ASSERT(daos_handle_is_valid(vfc->vfc_size_btr));

		d_list_del_init(&entry->vee_link);
		entry->vee_sized_class = NULL;
		/* Remove the sized class when it's empty */
		if (d_list_empty(&sc->vsc_extent_lru)) {
			uint64_t	int_key = blk_cnt;

			d_iov_set(&key, &int_key, sizeof(int_key));
			rc = dbtree_delete(vfc->vfc_size_btr, BTR_PROBE_EQ, &key, NULL);
			if (rc)
				D_ERROR("Remove size class:%u failed, "DF_RC"\n",
					blk_cnt, DP_RC(rc));
		}
		dec_stats(vsi, STAT_FRAGS_SMALL, 1);
	}
}

static int
find_or_create_sized_class(struct vea_space_info *vsi, uint64_t int_key,
			   struct vea_sized_class **ret_sc)
{
	struct vea_free_class	*vfc = &vsi->vsi_class;
	daos_handle_t		 btr_hdl = vfc->vfc_size_btr;
	d_iov_t			 key, val, val_out;
	struct vea_sized_class	 dummy, *sc = NULL;
	int			 rc;

	/* Add to a sized class */
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &int_key, sizeof(int_key));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_fetch(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key, NULL, &val_out);
	if (rc == 0) {
		/* Found an existing sized class */
		sc = (struct vea_sized_class *)val_out.iov_buf;
		D_ASSERT(sc != NULL);
	} else if (rc == -DER_NONEXIST) {
		/* Create a new sized class */
		memset(&dummy, 0, sizeof(dummy));
		d_iov_set(&val, &dummy, sizeof(dummy));
		d_iov_set(&val_out, NULL, 0);

		rc = dbtree_upsert(btr_hdl, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key, &val,
				   &val_out);
		if (rc != 0) {
			D_ERROR("Insert size class:%llu failed. "DF_RC"\n",
				(unsigned long long)int_key, DP_RC(rc));
			return rc;
		}
		sc = (struct vea_sized_class *)val_out.iov_buf;
		D_ASSERT(sc != NULL);
		D_INIT_LIST_HEAD(&sc->vsc_extent_lru);
	} else {
		D_ERROR("Lookup size class:%llu failed. "DF_RC"\n",
			(unsigned long long)int_key, DP_RC(rc));
		return rc;
	}
	*ret_sc = sc;

	return rc;
}

int
extent_free_class_add(struct vea_space_info *vsi, struct vea_extent_entry *entry)
{
	struct vea_free_class	*vfc = &vsi->vsi_class;
	uint64_t		 int_key;
	struct vea_sized_class	*sc;
	int			 rc;

	D_ASSERT(entry->vee_sized_class == NULL);
	D_ASSERT(d_list_empty(&entry->vee_link));

	int_key = entry->vee_ext.vfe_blk_cnt;
	/* Add to heap if it's a free extent */
	if (int_key > vfc->vfc_large_thresh) {
		rc = d_binheap_insert(&vfc->vfc_heap, &entry->vee_node);
		if (rc != 0) {
			D_ERROR("Failed to insert heap: %d\n", rc);
			return rc;
		}
		inc_stats(vsi, STAT_FRAGS_LARGE, 1);
		return 0;
	}

	rc = find_or_create_sized_class(vsi, int_key, &sc);
	if (rc)
		return rc;

	entry->vee_sized_class = sc;
	d_list_add_tail(&entry->vee_link, &sc->vsc_extent_lru);

	inc_stats(vsi, STAT_FRAGS_SMALL, 1);
	return 0;
}

static void
bitmap_free_class_add(struct vea_space_info *vsi, struct vea_bitmap_entry *entry,
		      int flags)
{
	uint64_t		 int_key;
	int			 free_blks;

	D_ASSERT(d_list_empty(&entry->vbe_link));

	int_key = entry->vbe_bitmap.vfb_class;
	D_ASSERT(int_key <= VEA_MAX_BITMAP_CLASS && int_key > 0);

	free_blks = bitmap_free_blocks(&entry->vbe_bitmap);
	if (!(flags & VEA_FL_NO_ACCOUNTING))
		inc_stats(vsi, STAT_FREE_BITMAP_BLKS, free_blks);
	if (free_blks >= int_key) {
		if (free_blks == entry->vbe_bitmap.vfb_blk_cnt)
			d_list_add(&entry->vbe_link,
				   &vsi->vsi_class.vfc_bitmap_empty[int_key - 1]);
		else
			d_list_add(&entry->vbe_link,
				   &vsi->vsi_class.vfc_bitmap_lru[int_key - 1]);
	}
	inc_stats(vsi, STAT_FRAGS_BITMAP, 1);
}

static void
undock_extent_entry(struct vea_space_info *vsi, struct vea_extent_entry *entry,
		    unsigned int type)
{
	if (type == VEA_TYPE_PERSIST)
		return;

	D_ASSERT(entry != NULL);
	if (type == VEA_TYPE_COMPOUND) {
		extent_free_class_remove(vsi, entry);
	} else {
		d_list_del_init(&entry->vee_link);
		dec_stats(vsi, STAT_FRAGS_AGING, 1);
	}
}

static void
undock_free_entry(struct vea_space_info *vsi, struct vea_free_entry *entry,
		  unsigned int type)
{
	if (type == VEA_TYPE_PERSIST || type == VEA_TYPE_COMPOUND)
		return;

	d_list_del_init(&entry->vfe_link);
	dec_stats(vsi, STAT_FRAGS_AGING, 1);
}

#define LARGE_AGING_FRAG_BLKS	8192

static inline bool
is_aging_frag_large(struct vea_free_extent *vfe)
{
	return vfe->vfe_blk_cnt >= LARGE_AGING_FRAG_BLKS;
}

static inline void
dock_aging_entry(struct vea_space_info *vsi, struct vea_free_entry *entry)
{
	d_list_add_tail(&entry->vfe_link, &vsi->vsi_agg_lru);
	inc_stats(vsi, STAT_FRAGS_AGING, 1);
}

static int
dock_extent_entry(struct vea_space_info *vsi, struct vea_extent_entry *entry, unsigned int type)
{

	D_ASSERT(entry != NULL);
	D_ASSERT(type == VEA_TYPE_COMPOUND);

	return extent_free_class_add(vsi, entry);
}

/*
 * Make sure there is no overlapping or duplicated extents in the free
 * extent tree. The passed in @ext_in will be merged with adjacent extents
 * and being inserted in the tree.
 *
 * Return value:	0	- Not merged
 *			1	- Merged in tree
 *			-ve	- Error
 */
static int
merge_free_ext(struct vea_space_info *vsi, struct vea_free_extent *ext_in,
	       unsigned int type, unsigned int flags, daos_handle_t btr_hdl)
{
	struct vea_free_extent	*ext, *neighbor = NULL;
	struct vea_free_extent	 merged = *ext_in;
	struct vea_extent_entry	*extent_entry, *neighbor_extent_entry = NULL;
	struct vea_free_entry	*free_entry, *neighbor_free_entry = NULL;
	d_iov_t			 key, key_out, val;
	uint64_t		*off;
	bool			 fetch_prev = true, large_prev = false;
	int			 rc, del_opc = BTR_PROBE_BYPASS;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &ext_in->vfe_blk_off, sizeof(ext_in->vfe_blk_off));
	d_iov_set(&key_out, NULL, 0);
	d_iov_set(&val, NULL, 0);

	/*
	 * Search the one to be merged, the btree trace will be set to proper position for
	 * later potential insertion (when merge failed), so don't change btree trace!
	 */
	rc = dbtree_fetch(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key, &key_out, &val);
	if (rc == 0) {
		D_ERROR("unexpected extent ["DF_U64", %u]\n",
			ext_in->vfe_blk_off, ext_in->vfe_blk_cnt);
		return -DER_INVAL;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("search extent with offset "DF_U64" failed. "DF_RC"\n",
			ext_in->vfe_blk_off, DP_RC(rc));
		return rc;
	}
repeat:
	d_iov_set(&key_out, NULL, sizeof(ext_in->vfe_blk_off));
	d_iov_set(&val, NULL, 0);

	if (fetch_prev) {
		rc = dbtree_fetch_prev(btr_hdl, &key_out, &val, false);
		if (rc == -DER_NONEXIST) {
			fetch_prev = false;
			goto repeat;
		} else if (rc) {
			D_ERROR("search prev extent failed. "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	} else {
		/*
		 * The btree trace was set to the position for inserting the searched
		 * one. If there is an extent in current position, let's try to merge
		 * it with the searched one; otherwise, we'd lookup next to see if any
		 * extent can be merged.
		 */
		rc = dbtree_fetch_cur(btr_hdl, &key_out, &val);
		if (rc == -DER_NONEXIST) {
			del_opc = BTR_PROBE_EQ;
			rc = dbtree_fetch_next(btr_hdl, &key_out, &val, false);
		}

		if (rc == -DER_NONEXIST) {
			goto done; /* Merge done */
		} else if (rc) {
			D_ERROR("search next extent failed. "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	if (type == VEA_TYPE_PERSIST) {
		extent_entry = NULL;
		free_entry = NULL;
		ext = (struct vea_free_extent *)val.iov_buf;
	} else if (type == VEA_TYPE_COMPOUND) {
		free_entry = NULL;
		extent_entry = (struct vea_extent_entry *)val.iov_buf;
		ext = &extent_entry->vee_ext;
	} else {
		extent_entry = NULL;
		free_entry = (struct vea_free_entry *)val.iov_buf;
		ext = &free_entry->vfe_ext;
	}

	off = (uint64_t *)key_out.iov_buf;
	rc = verify_free_entry(off, ext);
	if (rc != 0)
		return rc;

	/* This checks overlapping & duplicated extents as well. */
	rc = fetch_prev ? ext_adjacent(ext, &merged) : ext_adjacent(&merged, ext);
	if (rc < 0)
		return rc;

	/*
	 * When the in-tree aging frag is large enough, we'd stop merging with them,
	 * otherwise, the large aging frag could keep growing and stay in aging buffer
	 * for too long time.
	 *
	 * When the 'prev' and 'next' frags are both large, the freed frag will be
	 * merged with 'next'.
	 */
	if (rc > 0 && type == VEA_TYPE_AGGREGATE && is_aging_frag_large(ext)) {
		if (fetch_prev) {
			rc = 0;
			large_prev = true;
		} else if (!large_prev) {
			rc = 0;
		}
	}

	if (rc > 0) {
		if (flags & VEA_FL_NO_MERGE) {
			D_ERROR("unexpected adjacent extents:"
				" ["DF_U64", %u], ["DF_U64", %u]\n",
				merged.vfe_blk_off, merged.vfe_blk_cnt,
				ext->vfe_blk_off, ext->vfe_blk_cnt);
			return -DER_INVAL;
		}

		if (fetch_prev) {
			merged.vfe_blk_off = ext->vfe_blk_off;
			merged.vfe_blk_cnt += ext->vfe_blk_cnt;

			neighbor = ext;
			neighbor_extent_entry = extent_entry;
			neighbor_free_entry = free_entry;
		} else {
			merged.vfe_blk_cnt += ext->vfe_blk_cnt;

			/*
			 * Prev adjacent extent will be kept, remove the next
			 * adjacent extent.
			 */
			if (neighbor != NULL) {
				if (extent_entry)
					undock_extent_entry(vsi, extent_entry, type);
				else if (free_entry)
					undock_free_entry(vsi, free_entry, type);
				rc = dbtree_delete(btr_hdl, del_opc, &key_out, NULL);
				if (rc) {
					D_ERROR("Failed to delete: %d\n", rc);
					return rc;
				}
			} else {
				neighbor = ext;
				neighbor_extent_entry = extent_entry;
				neighbor_free_entry = free_entry;
			}
		}
	}

	if (fetch_prev) {
		fetch_prev = false;
		goto repeat;
	}
done:
	if (neighbor == NULL)
		return 0;

	if (type == VEA_TYPE_PERSIST) {
		rc = umem_tx_add_ptr(vsi->vsi_umem, neighbor,
				     sizeof(*neighbor));
		if (rc) {
			D_ERROR("Failed add ptr into tx: %d\n", rc);
			return rc;
		}
	} else {
		if (neighbor_extent_entry)
			undock_extent_entry(vsi, neighbor_extent_entry, type);
		else if (neighbor_free_entry)
			undock_free_entry(vsi, neighbor_free_entry, type);
	}

	/* Adjust in-tree offset & length */
	neighbor->vfe_blk_off = merged.vfe_blk_off;
	neighbor->vfe_blk_cnt = merged.vfe_blk_cnt;

	if (type == VEA_TYPE_AGGREGATE || type == VEA_TYPE_COMPOUND) {
		neighbor->vfe_age = merged.vfe_age;
		if (neighbor_extent_entry) {
			rc = dock_extent_entry(vsi, neighbor_extent_entry, type);
			if (rc < 0)
				return rc;
		} else if (neighbor_free_entry) {
			D_ASSERT(type == VEA_TYPE_AGGREGATE);
			D_ASSERT(d_list_empty(&neighbor_free_entry->vfe_link));
			dock_aging_entry(vsi, neighbor_free_entry);
		}
	}

	return 1;
}

/* insert bitmap entry to in-memory index */
int
bitmap_entry_insert(struct vea_space_info *vsi, struct vea_free_bitmap *vfb,
		    int state, struct vea_bitmap_entry **ret_entry, unsigned int flags)
{
	struct vea_bitmap_entry	*entry, *dummy;
	d_iov_t			 key, val, val_out;
	int			 rc, ret;
	struct umem_attr	 uma;
	int			 dummy_size = sizeof(*dummy) + (vfb->vfb_bitmap_sz << 3);

	D_ALLOC(dummy, dummy_size);
	if (!dummy)
		return -DER_NOMEM;

	memset(dummy, 0, sizeof(*dummy));
	dummy->vbe_bitmap = *vfb;
	dummy->vbe_agg_btr = DAOS_HDL_INVAL;
	if (state == VEA_BITMAP_STATE_NEW)
		setbits64(dummy->vbe_bitmap.vfb_bitmaps, 0, 1);
	else
		memcpy(dummy->vbe_bitmap.vfb_bitmaps, vfb->vfb_bitmaps, vfb->vfb_bitmap_sz << 3);
	dummy->vbe_published_state = state;

	/* Add to in-memory bitmap tree */
	D_ASSERT(daos_handle_is_valid(vsi->vsi_free_btr));
	d_iov_set(&key, &dummy->vbe_bitmap.vfb_blk_off, sizeof(dummy->vbe_bitmap.vfb_blk_off));
	d_iov_set(&val, dummy, dummy_size);
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_upsert(vsi->vsi_bitmap_btr, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &key,
			   &val, &val_out);
	D_FREE(dummy);
	if (rc != 0) {
		D_ERROR("Insert bitmap failed. "DF_RC" %llu\n", DP_RC(rc),
			(unsigned long long)vfb->vfb_blk_off);
		return rc;
	}

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;

	D_ASSERT(val_out.iov_buf != NULL);
	entry = (struct vea_bitmap_entry *)val_out.iov_buf;
	rc = dbtree_create(DBTREE_CLASS_IFV, BTR_FEAT_DIRECT_KEY, VEA_TREE_ODR, &uma, NULL,
			   &entry->vbe_agg_btr);
	if (rc != 0)
		goto error;

	D_INIT_LIST_HEAD(&entry->vbe_link);
	D_ASSERT(entry->vbe_bitmap.vfb_class == vfb->vfb_class);

	bitmap_free_class_add(vsi, entry, flags);
	if (ret_entry)
		*ret_entry = entry;
	return rc;

error:
	ret = dbtree_delete(vsi->vsi_bitmap_btr, BTR_PROBE_EQ, &key, NULL);
	if (ret)
		D_ERROR("Failed to clean bitmap failed. "DF_RC" "DF_U64"\n",
			DP_RC(rc), vfb->vfb_blk_off);
	return rc;
}

static int
bitmap_entry_remove(struct vea_space_info *vsi, struct vea_bitmap_entry *bitmap,
		    unsigned int flags)
{
	d_iov_t			 key;
	int			 rc;

	rc = dbtree_destroy(bitmap->vbe_agg_btr, NULL);
	if (rc) {
		D_ERROR("Failed to destroy bitmap agg tree. "DF_RC" "DF_U64"\n",
			DP_RC(rc), bitmap->vbe_bitmap.vfb_blk_off);
		return rc;
	}
	bitmap->vbe_agg_btr = DAOS_HDL_INVAL;

	if (!(flags & VEA_FL_NO_ACCOUNTING))
		dec_stats(vsi, STAT_FREE_BITMAP_BLKS, bitmap->vbe_bitmap.vfb_blk_cnt);
	d_list_del_init(&bitmap->vbe_link);
	dec_stats(vsi, STAT_FRAGS_BITMAP, 1);

	d_iov_set(&key, &bitmap->vbe_bitmap.vfb_blk_off, sizeof(bitmap->vbe_bitmap.vfb_blk_off));
	rc = dbtree_delete(vsi->vsi_bitmap_btr, BTR_PROBE_EQ, &key, NULL);
	if (rc)
		D_ERROR("Failed to clean bitmap failed. "DF_RC" "DF_U64"\n",
			DP_RC(rc), bitmap->vbe_bitmap.vfb_blk_off);

	return rc;
}

int
compound_free_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe,
		     unsigned int flags)
{
	struct vea_extent_entry	*entry, dummy;
	d_iov_t			 key, val, val_out;
	int			 rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_COMPOUND, flags, vsi->vsi_free_btr);
	if (rc < 0) {
		return rc;
	} else if (rc > 0) {
		rc = 0;	/* extent merged in tree */
		goto accounting;
	}

	memset(&dummy, 0, sizeof(dummy));
	D_INIT_LIST_HEAD(&dummy.vee_link);
	dummy.vee_ext = *vfe;

	/* Add to in-memory free extent tree */
	D_ASSERT(daos_handle_is_valid(vsi->vsi_free_btr));
	d_iov_set(&key, &dummy.vee_ext.vfe_blk_off, sizeof(dummy.vee_ext.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_upsert(vsi->vsi_free_btr, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key,
			   &val, &val_out);
	if (rc != 0) {
		D_ERROR("Insert compound extent failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(val_out.iov_buf != NULL);
	entry = (struct vea_extent_entry *)val_out.iov_buf;
	D_INIT_LIST_HEAD(&entry->vee_link);

	rc = extent_free_class_add(vsi, entry);

accounting:
	if (!rc && !(flags & VEA_FL_NO_ACCOUNTING))
		inc_stats(vsi, STAT_FREE_EXTENT_BLKS, vfe->vfe_blk_cnt);
	return rc;
}

/* Free entry to in-memory compound index */
int
compound_free(struct vea_space_info *vsi, struct vea_free_entry *vfe,
	      unsigned int flags)
{
	int			 rc;
	struct vea_bitmap_entry	*found = vfe->vfe_bitmap;

	if (found == NULL)
		return compound_free_extent(vsi, &vfe->vfe_ext, flags);

	rc = bitmap_set_range(NULL, &found->vbe_bitmap,
			      vfe->vfe_ext.vfe_blk_off, vfe->vfe_ext.vfe_blk_cnt, true);
	if (rc)
		return rc;

	if (!(flags & VEA_FL_NO_ACCOUNTING))
		inc_stats(vsi, STAT_FREE_BITMAP_BLKS, vfe->vfe_ext.vfe_blk_cnt);

	/* if bitmap is not published and clear, then remove it */
	if (found->vbe_published_state == VEA_BITMAP_STATE_NEW) {
		if (is_bitmap_empty(found->vbe_bitmap.vfb_bitmaps,
				    found->vbe_bitmap.vfb_bitmap_sz)) {
			struct vea_free_extent	ext;

			ext.vfe_blk_cnt = found->vbe_bitmap.vfb_blk_cnt;
			ext.vfe_blk_off = found->vbe_bitmap.vfb_blk_off;
			rc = bitmap_entry_remove(vsi, found, flags);
			if (rc)
				return rc;
			return compound_free_extent(vsi, &ext, flags);
		}
	}

	if (is_bitmap_empty(found->vbe_bitmap.vfb_bitmaps,
			    found->vbe_bitmap.vfb_bitmap_sz)) {
		if (d_list_empty(&found->vbe_link))
			d_list_add_tail(&found->vbe_link,
				&vsi->vsi_class.vfc_bitmap_empty[found->vbe_bitmap.vfb_class - 1]);
		else
			d_list_move_tail(&found->vbe_link,
				&vsi->vsi_class.vfc_bitmap_empty[found->vbe_bitmap.vfb_class - 1]);
		return 0;
	}

	if (d_list_empty(&found->vbe_link)) {
		D_ASSERT(found->vbe_bitmap.vfb_class <= VEA_MAX_BITMAP_CLASS);
		d_list_add_tail(&found->vbe_link,
				&vsi->vsi_class.vfc_bitmap_lru[found->vbe_bitmap.vfb_class - 1]);
	}

	return 0;
}

/* Free extent to persistent free tree */
static int
persistent_free_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_free_extent	dummy;
	d_iov_t			key, val;
	daos_handle_t		btr_hdl = vsi->vsi_md_free_btr;
	int			rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_PERSIST, 0, vsi->vsi_md_free_btr);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		return 0;	/* extent merged in tree */

	memset(&dummy, 0, sizeof(dummy));
	dummy = *vfe;
	dummy.vfe_age = 0; /* Not used */

	/* Add to persistent free extent tree */
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &dummy.vfe_blk_off, sizeof(dummy.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));

	rc = dbtree_upsert(btr_hdl, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key, &val, NULL);
	if (rc)
		D_ERROR("Insert persistent extent failed. "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
persistent_free(struct vea_space_info *vsi, struct vea_free_entry *vfe)
{
	int	type;

	D_ASSERT(umem_tx_inprogress(vsi->vsi_umem) ||
		 vsi->vsi_umem->umm_id == UMEM_CLASS_VMEM);
	D_ASSERT(vfe->vfe_ext.vfe_blk_off != VEA_HINT_OFF_INVAL);
	type = free_type(vsi, vfe->vfe_ext.vfe_blk_off, vfe->vfe_ext.vfe_blk_cnt,
			 &vfe->vfe_bitmap);
	if (type < 0)
		return type;

	if (vfe->vfe_bitmap == NULL)
		return persistent_free_extent(vsi, &vfe->vfe_ext);

	D_ASSERT(type == VEA_FREE_ENTRY_BITMAP);

	D_ASSERT(vfe->vfe_ext.vfe_blk_cnt > 0 &&
		 vfe->vfe_ext.vfe_blk_cnt < vsi->vsi_class.vfc_large_thresh);
	return bitmap_set_range(vsi->vsi_umem, vfe->vfe_bitmap->vbe_md_bitmap,
				vfe->vfe_ext.vfe_blk_off, vfe->vfe_ext.vfe_blk_cnt, true);
}

/* Free extent to the aggregate free tree */
int
aggregated_free(struct vea_space_info *vsi, struct vea_free_entry *vfe)
{
	struct vea_free_entry	*entry, dummy;
	d_iov_t			 key, val, val_out;
	daos_handle_t		 btr_hdl = vsi->vsi_agg_btr;
	int			 rc;

	/* free entry bitmap */
	if (vfe->vfe_bitmap == NULL)
		btr_hdl = vsi->vsi_agg_btr;
	else
		btr_hdl = vfe->vfe_bitmap->vbe_agg_btr;

	vfe->vfe_ext.vfe_age = get_current_age();
	rc = merge_free_ext(vsi, &vfe->vfe_ext, VEA_TYPE_AGGREGATE, 0, btr_hdl);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		return 0;	/* entry merged in tree */

	dummy = *vfe;
	D_INIT_LIST_HEAD(&dummy.vfe_link);

	/* Add to in-memory aggregate free extent tree */
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &dummy.vfe_ext.vfe_blk_off, sizeof(dummy.vfe_ext.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_upsert(btr_hdl, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key, &val, &val_out);
	if (rc) {
		D_ERROR("Insert aging entry failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(val_out.iov_buf != NULL);
	entry = (struct vea_free_entry *)val_out.iov_buf;
	D_INIT_LIST_HEAD(&entry->vfe_link);

	dock_aging_entry(vsi, entry);
	return 0;
}

#define EXPIRE_INTVL            3               /* seconds */
#define UNMAP_SIZE_THRESH	(1UL << 20)	/* 1MB */

static int
flush_internal(struct vea_space_info *vsi, bool force, uint32_t cur_time, d_sg_list_t *unmap_sgl,
	       d_sg_list_t *free_sgl)
{
	struct vea_free_entry	*entry, *tmp;
	struct vea_free_extent	 vfe;
	struct vea_free_entry	 free_entry;
	d_iov_t			*ext_iov;
	int			 i, rc = 0;
	d_iov_t			 key;
	struct vea_bitmap_entry	*bitmap;
	struct vea_bitmap_entry **flush_bitmaps;
	daos_handle_t		 btr_hdl;

	D_ASSERT(umem_tx_none(vsi->vsi_umem));
	D_ASSERT(unmap_sgl->sg_nr_out == 0);
	D_ASSERT(free_sgl->sg_nr_out == 0);

	D_ALLOC_ARRAY(flush_bitmaps, MAX_FLUSH_FRAGS);
	if (!flush_bitmaps)
		return -DER_NOMEM;

	d_list_for_each_entry_safe(entry, tmp, &vsi->vsi_agg_lru, vfe_link) {
		vfe = entry->vfe_ext;
		if (!force && cur_time < (vfe.vfe_age + EXPIRE_INTVL))
			break;

		/* Remove entry from aggregate LRU list */
		d_list_del_init(&entry->vfe_link);
		dec_stats(vsi, STAT_FRAGS_AGING, 1);

		bitmap = entry->vfe_bitmap;
		if (bitmap)
			btr_hdl = bitmap->vbe_agg_btr;
		else
			btr_hdl = vsi->vsi_agg_btr;
		/* Remove entry from aggregate tree, entry will be freed on deletion */
		d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
		D_ASSERT(daos_handle_is_valid(btr_hdl));
		rc = dbtree_delete(btr_hdl, BTR_PROBE_EQ, &key, NULL);
		if (rc) {
			D_ERROR("Remove ["DF_U64", %u] from aggregated tree error: "DF_RC"\n",
				vfe.vfe_blk_off, vfe.vfe_blk_cnt, DP_RC(rc));
			break;
		}

		flush_bitmaps[free_sgl->sg_nr_out] = bitmap;
		ext_iov = &free_sgl->sg_iovs[free_sgl->sg_nr_out];
		ext_iov->iov_buf = (void *)vfe.vfe_blk_off;
		ext_iov->iov_len = vfe.vfe_blk_cnt;
		free_sgl->sg_nr_out++;
		/*
		 * Unmap callback may yield, so we can't call it directly in this tight loop.
		 *
		 * Given that unmap a tiny extent (much smaller than a SSD erase block) doesn't
		 * make sense and too many parallel unmap calls could impact I/O performance, we
		 * opt to unmap only large enough extent.
		 */
		if (vfe.vfe_blk_cnt >= (UNMAP_SIZE_THRESH / vsi->vsi_md->vsd_blk_sz)) {
			ext_iov = &unmap_sgl->sg_iovs[unmap_sgl->sg_nr_out];
			ext_iov->iov_buf = (void *)vfe.vfe_blk_off;
			ext_iov->iov_len = vfe.vfe_blk_cnt;
			unmap_sgl->sg_nr_out++;
		}

		if (free_sgl->sg_nr_out == MAX_FLUSH_FRAGS)
			break;
	}

	vsi->vsi_flush_time = cur_time;

	/*
	 * According to NVMe spec, unmap isn't an expensive non-queue command anymore, so we
	 * should just unmap as soon as the extent is freed.
	 *
	 * Since unmap could yield, it must be called before the compound_free(), otherwise,
	 * the extent could be visible for allocation before unmap done.
	 */
	if (vsi->vsi_unmap_ctxt.vnc_unmap != NULL && unmap_sgl->sg_nr_out > 0) {
		rc = vsi->vsi_unmap_ctxt.vnc_unmap(unmap_sgl, vsi->vsi_md->vsd_blk_sz,
						   vsi->vsi_unmap_ctxt.vnc_data);
		if (rc)
			D_ERROR("Unmap %u frags failed: "DF_RC"\n",
				unmap_sgl->sg_nr_out, DP_RC(rc));
	}

	for (i = 0; i < free_sgl->sg_nr_out; i++) {
		ext_iov = &free_sgl->sg_iovs[i];

		free_entry.vfe_ext.vfe_blk_off = (uint64_t)ext_iov->iov_buf;
		free_entry.vfe_ext.vfe_blk_cnt = ext_iov->iov_len;
		free_entry.vfe_ext.vfe_age = cur_time;
		free_entry.vfe_bitmap = flush_bitmaps[i];

		rc = compound_free(vsi, &free_entry, 0);
		if (rc)
			D_ERROR("Compound free ["DF_U64", %u] error: "DF_RC"\n",
				free_entry.vfe_ext.vfe_blk_off, free_entry.vfe_ext.vfe_blk_cnt,
				DP_RC(rc));
	}
	D_FREE(flush_bitmaps);

	return rc;
}

void
free_commit_cb(void *data, bool noop)
{
	struct free_commit_cb_arg *fca = data;
	int rc;

	/* Transaction aborted, only need to free callback arg */
	if (noop)
		goto free;

	/*
	 * Aggregated free will be executed on outermost transaction
	 * commit.
	 *
	 * If it fails, the freed space on persistent free tree won't
	 * be added in in-memory free tree, hence the space won't be
	 * visible for allocation until the tree sync up on next server
	 * restart. Such temporary space leak is tolerable, what we must
	 * avoid is the contrary case: in-memory tree update succeeds
	 * but persistent tree update fails, which risks data corruption.
	 */
	rc = aggregated_free(fca->fca_vsi, &fca->fca_vfe);

	D_CDEBUG(rc, DLOG_ERR, DB_IO, "Aggregated free on vsi:%p rc %d\n",
		 fca->fca_vsi, rc);
free:
	D_FREE(fca);
}

static int
reclaim_unused_bitmap(struct vea_space_info *vsi, uint32_t nr_reclaim)
{
	int				 i;
	struct vea_bitmap_entry		*bitmap_entry;
	struct vea_free_bitmap		*vfb;
	d_iov_t				 key;
	int				 rc = 0;
	struct free_commit_cb_arg	*fca;
	struct umem_instance		*umem = vsi->vsi_umem;
	int				 nr = 0;
	uint64_t			 blk_off;
	uint32_t			 blk_cnt;

	for (i = 0; i < VEA_MAX_BITMAP_CLASS; i++) {
		while ((bitmap_entry = d_list_pop_entry(&vsi->vsi_class.vfc_bitmap_empty[i],
							struct vea_bitmap_entry, vbe_link))) {
			vfb = &bitmap_entry->vbe_bitmap;
			D_ASSERT(vfb->vfb_class == i + 1);
			D_ASSERT(is_bitmap_empty(vfb->vfb_bitmaps, vfb->vfb_bitmap_sz));
			D_ALLOC_PTR(fca);
			if (!fca)
				return -DER_NOMEM;

			blk_off = vfb->vfb_blk_off;
			blk_cnt = vfb->vfb_blk_cnt;
			fca->fca_vsi = vsi;
			fca->fca_vfe.vfe_ext.vfe_blk_off = blk_off;
			fca->fca_vfe.vfe_ext.vfe_blk_cnt = blk_cnt;
			fca->fca_vfe.vfe_ext.vfe_age = 0; /* not used */

			rc = umem_tx_begin(umem, vsi->vsi_txd);
			if (rc != 0) {
				D_FREE(fca);
				return rc;
			}

			/*
			 * Even in-memory bitmap failed to remove from tree, it is ok
			 * because this bitmap chunk has been removed from allocation LRU list.
			 */
			d_iov_set(&key, &fca->fca_vfe.vfe_ext.vfe_blk_off,
				  sizeof(fca->fca_vfe.vfe_ext.vfe_blk_off));
			dbtree_destroy(bitmap_entry->vbe_agg_btr, NULL);
			rc = dbtree_delete(fca->fca_vsi->vsi_bitmap_btr, BTR_PROBE_EQ, &key, NULL);
			if (rc) {
				D_ERROR("Remove ["DF_U64", %u] from bitmap tree "
					"error: "DF_RC"\n", fca->fca_vfe.vfe_ext.vfe_blk_off,
					fca->fca_vfe.vfe_ext.vfe_blk_cnt, DP_RC(rc));
				goto abort;
			}
			dec_stats(fca->fca_vsi, STAT_FRAGS_BITMAP, 1);
			dec_stats(fca->fca_vsi, STAT_FREE_BITMAP_BLKS, blk_cnt);

			d_iov_set(&key, &blk_off, sizeof(blk_off));
			rc = dbtree_delete(vsi->vsi_md_bitmap_btr, BTR_PROBE_EQ, &key, NULL);
			if (rc) {
				D_ERROR("Remove ["DF_U64", %u] from persistent bitmap "
					"tree error: "DF_RC"\n", blk_off, blk_cnt, DP_RC(rc));
				goto abort;
			}

			rc = persistent_free_extent(vsi, &fca->fca_vfe.vfe_ext);
			if (rc) {
				D_ERROR("Remove ["DF_U64", %u] from persistent "
					"extent tree error: "DF_RC"\n", blk_off,
					blk_cnt, DP_RC(rc));
				goto abort;
			}
			rc = umem_tx_add_callback(umem, vsi->vsi_txd, UMEM_STAGE_ONCOMMIT,
						  free_commit_cb, fca);
			if (rc == 0)
				fca = NULL;
abort:
			D_FREE(fca);
			/* Commit/Abort transaction on success/error */
			rc = rc ? umem_tx_abort(umem, rc) : umem_tx_commit(umem);
			if (rc)
				return rc;
			nr++;
			if (nr >= nr_reclaim)
				return rc;
		}
	}

	return rc;
}

int
trigger_aging_flush(struct vea_space_info *vsi, bool force, uint32_t nr_flush, uint32_t *nr_flushed)
{
	d_sg_list_t	 unmap_sgl, free_sgl;
	uint32_t	 cur_time, tot_flushed = 0;
	int		 rc;

	D_ASSERT(nr_flush > 0);
	D_ASSERT(umem_tx_none(vsi->vsi_umem));

	cur_time = get_current_age();
	rc = reclaim_unused_bitmap(vsi, MAX_FLUSH_FRAGS);
	if (rc)
		goto out;

	rc = d_sgl_init(&unmap_sgl, MAX_FLUSH_FRAGS);
	if (rc)
		goto out;
	rc = d_sgl_init(&free_sgl, MAX_FLUSH_FRAGS);
	if (rc) {
		d_sgl_fini(&unmap_sgl, false);
		goto out;
	}

	while (tot_flushed < nr_flush) {
		rc = flush_internal(vsi, force, cur_time, &unmap_sgl, &free_sgl);

		tot_flushed += free_sgl.sg_nr_out;
		if (rc || free_sgl.sg_nr_out < MAX_FLUSH_FRAGS)
			break;

		unmap_sgl.sg_nr_out = 0;
		free_sgl.sg_nr_out = 0;
	}

	d_sgl_fini(&unmap_sgl, false);
	d_sgl_fini(&free_sgl, false);

out:
	if (nr_flushed != NULL)
		*nr_flushed = tot_flushed;

	return rc;
}
