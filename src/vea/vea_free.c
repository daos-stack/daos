/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include "vea_internal.h"

enum vea_free_type {
	VEA_TYPE_COMPOUND,
	VEA_TYPE_AGGREGATE,
	VEA_TYPE_PERSIST,
};

int
free_type(struct vea_space_info *vsi, uint64_t blk_off, uint32_t blk_cnt, bool transient,
	  uint16_t *class)
{
	int			 type = VEA_FREE_ENTRY_BITMAP;
	struct vea_free_bitmap	*found, *last_found = NULL;
	daos_handle_t		 btr_hdl;
	d_iov_t			 key_in, key_out, val;
	uint64_t		 found_end, vfe_end;
	int			 rc, opc = BTR_PROBE_LE;
	bool			 cross_bitmap = false;

	*class = 0;
	if (blk_cnt > vsi->vsi_class.vfc_large_thresh) {
		type = VEA_FREE_ENTRY_EXTENT;
		goto out;
	}

	if (transient)
		btr_hdl = vsi->vsi_bitmap_btr;
	else
		btr_hdl = vsi->vsi_md_bitmap_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));

	/* Fetch the in-tree record */
	d_iov_set(&key_in, &blk_off, sizeof(blk_off));
	d_iov_set(&key_out, NULL, sizeof(blk_off));
	d_iov_set(&val, NULL, 0);

	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out,
			  &val);
	if (rc == -DER_NONEXIST)
		return VEA_FREE_ENTRY_EXTENT;

	if (rc) {
		D_ERROR("failed to search range ["DF_U64", %u] int bitmap tree\n",
			blk_off, blk_cnt);
		return rc;
	}

again:
	if (transient) {
		struct vea_bitmap_entry	*entry;

		entry = (struct vea_bitmap_entry *)val.iov_buf;
		found = &entry->vbe_bitmap;
	} else {
		found = (struct vea_free_bitmap *)val.iov_buf;
	}

	rc = verify_bitmap_entry(found);
	if (rc) {
		D_ERROR("verify bitmap failed in free_type\n");
		return rc;
	}

	found_end = found->vfb_blk_off + found->vfb_blk_cnt - 1;
	vfe_end = blk_off + blk_cnt - 1;
	if (blk_off >= found->vfb_blk_off && blk_off <= found_end) {
		if (cross_bitmap && blk_off != found->vfb_blk_off) {
			D_CRIT("bitmap range overlapped, ["DF_U64", %u] ["DF_U64", %u]\n",
				last_found->vfb_blk_off, last_found->vfb_blk_cnt,
				found->vfb_blk_off, found->vfb_blk_cnt);
			return -DER_IO;
		}
		if (vfe_end <= found_end) {
			*class = found->vfb_class;
			return VEA_FREE_ENTRY_BITMAP;
		}

		if (!transient) {
			D_CRIT("["DF_U64", %u] should not cross bitmap in persistent tree\n",
				found->vfb_blk_off, found->vfb_blk_cnt);
			return -DER_INVAL;
		}
		last_found = found;
		d_iov_set(&key_out, NULL, sizeof(blk_off));
		d_iov_set(&val, NULL, 0);
		rc = dbtree_fetch_next(btr_hdl, &key_out, &val, true);
		if (rc)
			return -DER_IO;
		blk_off = found_end + 1;
		blk_cnt = vfe_end - found_end;
		cross_bitmap = true;
		goto again;
	} else if (cross_bitmap) {
		D_CRIT("bitmap and extent mixed range: ["DF_U64", %u] in bitmap range, "
			"["DF_U64", %u] in extent\n", last_found->vfb_blk_off,
			last_found->vfb_blk_cnt, blk_off, blk_cnt);
		return -DER_IO;
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
		inc_stats(vsi, STAT_FREE_BLKS, free_blks);
	if (free_blks >= int_key)
		d_list_add(&entry->vbe_link, &vsi->vsi_class.vfc_bitmap_lru[int_key - 1]);
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
	       unsigned int type, unsigned int flags)
{
	struct vea_free_extent	*ext, *neighbor = NULL;
	struct vea_free_extent	 merged = *ext_in;
	struct vea_extent_entry	*entry, *neighbor_entry = NULL;
	daos_handle_t		 btr_hdl;
	d_iov_t			 key, key_out, val;
	uint64_t		*off;
	bool			 fetch_prev = true, large_prev = false;
	int			 rc, del_opc = BTR_PROBE_BYPASS;

	if (type == VEA_TYPE_COMPOUND)
		btr_hdl = vsi->vsi_free_btr;
	else if (type == VEA_TYPE_PERSIST)
		btr_hdl = vsi->vsi_md_free_btr;
	else if (type  == VEA_TYPE_AGGREGATE)
		btr_hdl = vsi->vsi_agg_btr;
	else
		return -DER_INVAL;

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
	d_iov_set(&key_out, NULL, 0);
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
		entry = NULL;
		ext = (struct vea_free_extent *)val.iov_buf;
	} else {
		entry = (struct vea_extent_entry *)val.iov_buf;
		ext = &entry->vee_ext;
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
			neighbor_entry = entry;
		} else {
			merged.vfe_blk_cnt += ext->vfe_blk_cnt;

			/*
			 * Prev adjacent extent will be kept, remove the next
			 * adjacent extent.
			 */
			if (neighbor != NULL) {
				undock_extent_entry(vsi, entry, type);
				rc = dbtree_delete(btr_hdl, del_opc, &key_out, NULL);
				if (rc) {
					D_ERROR("Failed to delete: %d\n", rc);
					return rc;
				}
			} else {
				neighbor = ext;
				neighbor_entry = entry;
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
		undock_extent_entry(vsi, neighbor_entry, type);
	}

	/* Adjust in-tree offset & length */
	neighbor->vfe_blk_off = merged.vfe_blk_off;
	neighbor->vfe_blk_cnt = merged.vfe_blk_cnt;

	if (type == VEA_TYPE_AGGREGATE || type == VEA_TYPE_COMPOUND) {
		neighbor->vfe_age = merged.vfe_age;
		rc = dock_extent_entry(vsi, neighbor_entry, type);
		if (rc < 0)
			return rc;
	}

	return 1;
}

static inline int
entry_adjacent(struct vea_free_entry *entry1, struct vea_free_entry *entry2)
{
	if (entry1->vfe_type != entry2->vfe_type ||
	    entry1->vfe_class != entry2->vfe_class)
		return 0;

	return ext_adjacent(&entry1->vfe_ext, &entry2->vfe_ext);
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
merge_free_entry(struct vea_space_info *vsi, struct vea_free_entry *entry_in,
		 unsigned int flags)
{
	struct vea_free_extent	*ext, *neighbor = NULL;
	struct vea_free_entry	 merged = *entry_in;
	struct vea_free_entry	*entry, *neighbor_entry = NULL;
	daos_handle_t		 btr_hdl = vsi->vsi_agg_btr;
	d_iov_t			 key, key_out, val;
	uint64_t		*off;
	bool			 fetch_prev = true, large_prev = false;
	int			 rc, del_opc = BTR_PROBE_BYPASS;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &entry_in->vfe_ext.vfe_blk_off, sizeof(entry_in->vfe_ext.vfe_blk_off));
	d_iov_set(&key_out, NULL, 0);
	d_iov_set(&val, NULL, 0);

	/*
	 * Search the one to be merged, the btree trace will be set to proper position for
	 * later potential insertion (when merge failed), so don't change btree trace!
	 */
	rc = dbtree_fetch(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key, &key_out, &val);
	if (rc == 0) {
		D_ERROR("unexpected extent ["DF_U64", %u]\n",
			entry_in->vfe_ext.vfe_blk_off, entry_in->vfe_ext.vfe_blk_cnt);
		return -DER_INVAL;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("search extent with offset "DF_U64" failed. "DF_RC"\n",
			entry_in->vfe_ext.vfe_blk_off, DP_RC(rc));
		return rc;
	}
repeat:
	d_iov_set(&key_out, NULL, 0);
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

	entry = (struct vea_free_entry *)val.iov_buf;
	ext = &entry->vfe_ext;

	off = (uint64_t *)key_out.iov_buf;
	rc = verify_free_entry(off, ext);
	if (rc != 0)
		return rc;

	/* This checks overlapping & duplicated entries as well. */
	rc = fetch_prev ? entry_adjacent(entry, &merged) : entry_adjacent(&merged, entry);
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
	if (rc > 0 && is_aging_frag_large(ext)) {
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
				merged.vfe_ext.vfe_blk_off, merged.vfe_ext.vfe_blk_cnt,
				ext->vfe_blk_off, ext->vfe_blk_cnt);
			return -DER_INVAL;
		}

		if (fetch_prev) {
			merged.vfe_type = entry->vfe_type;
			merged.vfe_ext.vfe_blk_off = ext->vfe_blk_off;
			merged.vfe_ext.vfe_blk_cnt += ext->vfe_blk_cnt;

			neighbor = ext;
			neighbor_entry = entry;
		} else {
			merged.vfe_ext.vfe_blk_cnt += ext->vfe_blk_cnt;

			/*
			 * Prev adjacent extent will be kept, remove the next
			 * adjacent extent.
			 */
			if (neighbor != NULL) {
				d_list_del_init(&entry->vfe_link);
				dec_stats(vsi, STAT_FRAGS_AGING, 1);
				rc = dbtree_delete(btr_hdl, del_opc, &key_out, NULL);
				if (rc) {
					D_ERROR("Failed to delete: %d\n", rc);
					return rc;
				}
			} else {
				neighbor = ext;
				neighbor_entry = entry;
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

	d_list_del_init(&neighbor_entry->vfe_link);
	dec_stats(vsi, STAT_FRAGS_AGING, 1);

	/* Adjust in-tree offset & length */
	neighbor->vfe_blk_off = merged.vfe_ext.vfe_blk_off;
	neighbor->vfe_blk_cnt = merged.vfe_ext.vfe_blk_cnt;
	neighbor->vfe_age = merged.vfe_ext.vfe_age;

	dock_aging_entry(vsi, neighbor_entry);

	return 1;
}

/* insert bitmap entry to in-memory index */
int
bitmap_entry_insert(struct vea_space_info *vsi, struct vea_free_bitmap *vfb,
		    bool new, struct vea_bitmap_entry **ret_entry, unsigned int flags)
{
	struct vea_bitmap_entry	*entry, *dummy;
	d_iov_t			 key, val, val_out;
	int			 rc;
	int			 dummy_size = sizeof(*dummy) + (vfb->vfb_bitmap_sz << 3);

	D_ALLOC(dummy, dummy_size);
	if (!dummy)
		return -DER_NOMEM;

	memset(dummy, 0, sizeof(*dummy));
	dummy->vbe_bitmap = *vfb;
	memcpy(dummy->vbe_bitmap.vfb_bitmaps, vfb->vfb_bitmaps, vfb->vfb_bitmap_sz << 3);
	D_INIT_LIST_HEAD(&dummy->vbe_link);
	dummy->vbe_is_new = new;

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

	D_ASSERT(val_out.iov_buf != NULL);
	entry = (struct vea_bitmap_entry *)val_out.iov_buf;
	D_INIT_LIST_HEAD(&entry->vbe_link);
	D_ASSERT(entry->vbe_bitmap.vfb_class == vfb->vfb_class);

	bitmap_free_class_add(vsi, entry, flags);
	if (ret_entry)
		*ret_entry = entry;
	return rc;

}

int
compound_free_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe,
		     unsigned int flags)
{
	struct vea_extent_entry	*entry, dummy;
	d_iov_t			 key, val, val_out;
	int			 rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_COMPOUND, flags);
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
		inc_stats(vsi, STAT_FREE_BLKS, vfe->vfe_blk_cnt);
	return rc;
}

static int
bitmap_clear_range(struct umem_instance *vsi_umem, struct vea_free_bitmap *bitmap,
		   uint64_t blk_off, uint32_t blk_cnt)
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
	if (!isset_range((uint8_t *)bitmap->vfb_bitmaps,
			 bit_at, bit_at + bits_nr - 1)) {
		D_ERROR("bitmap already cleared in the range.\n");
		return -DER_INVAL;
	}

	if (vsi_umem) {
		/* TODO: optimize range */
		rc = umem_tx_add_ptr(vsi_umem, bitmap->vfb_bitmaps,
				     bitmap->vfb_bitmap_sz << 3);
		if (rc)
			return rc;
	}

	D_ASSERT(bit_at + bits_nr <= bitmap->vfb_bitmap_sz * 64);
	clrbits64(bitmap->vfb_bitmaps, bit_at, bits_nr);

	return 0;
}

static int
compound_free_bitmap(struct vea_space_info *vsi, struct vea_free_extent *vfe_in,
		     unsigned int flags)
{
	struct vea_bitmap_entry	*found;
	daos_handle_t		 btr_hdl;
	d_iov_t			 key_in, key_out, val;
	int			 rc, opc = BTR_PROBE_LE;
	struct vea_free_extent	 vfe = *vfe_in;
	int			 blk_off, blk_cnt;

	D_ASSERT(vfe.vfe_blk_off != VEA_HINT_OFF_INVAL);
	D_ASSERT(vfe.vfe_blk_cnt > 0);

	btr_hdl = vsi->vsi_bitmap_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));

again:
	D_DEBUG(DB_IO, "Compound free bits ["DF_U64", %u]\n",
			vfe.vfe_blk_off, vfe.vfe_blk_cnt);

	/* Fetch & operate on the in-tree record */
	d_iov_set(&key_in, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
	d_iov_set(&key_out, NULL, sizeof(uint64_t));
	d_iov_set(&val, NULL, 0);

	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out,
			  &val);
	if (rc) {
		D_ERROR("failed to find extent ["DF_U64", %u]\n",
			vfe.vfe_blk_off, vfe.vfe_blk_cnt);
		return rc;
	}

	/* verify val_out.len with calculated */
	found = (struct vea_bitmap_entry *)val.iov_buf;
	rc = verify_bitmap_entry(&found->vbe_bitmap);
	if (rc) {
		D_ERROR("compound_free_bitmap verified failed\n");
		return rc;
	}

	if (found->vbe_bitmap.vfb_blk_off > vfe.vfe_blk_off ||
	    found->vbe_bitmap.vfb_blk_off + found->vbe_bitmap.vfb_blk_cnt < vfe.vfe_blk_off) {
		D_ERROR("failed to find extent ["DF_U64", %u]\n",
			vfe.vfe_blk_off, vfe.vfe_blk_cnt);
		return -DER_INVAL;
	}

	if (d_list_empty(&found->vbe_link)) {
		D_ASSERT(found->vbe_bitmap.vfb_class <= VEA_MAX_BITMAP_CLASS);
		d_list_add_tail(&found->vbe_link,
				&vsi->vsi_class.vfc_bitmap_lru[found->vbe_bitmap.vfb_class - 1]);
	}

	blk_off = vfe.vfe_blk_off - found->vbe_bitmap.vfb_blk_off;
	if (found->vbe_bitmap.vfb_blk_cnt - blk_off >= vfe.vfe_blk_cnt) {
		rc = bitmap_clear_range(NULL, &found->vbe_bitmap,
					vfe.vfe_blk_off, vfe.vfe_blk_cnt);
		if (rc)
			return rc;
	} else {
		blk_cnt = found->vbe_bitmap.vfb_blk_cnt - blk_off;
		rc = bitmap_clear_range(NULL, &found->vbe_bitmap,
					vfe.vfe_blk_off, blk_cnt);
		if (rc)
			return rc;
		vfe.vfe_blk_off = found->vbe_bitmap.vfb_blk_off + found->vbe_bitmap.vfb_blk_cnt;
		vfe.vfe_blk_cnt = vfe.vfe_blk_cnt - blk_cnt;
		goto again;
	}

	if (!(flags & VEA_FL_NO_ACCOUNTING))
		inc_stats(vsi, STAT_FREE_BLKS, vfe_in->vfe_blk_cnt);

	return 0;
}

static inline bool
free_entry_type_match(struct vea_free_entry *vfe, int type)
{
	if (vfe->vfe_type == VEA_FREE_ENTRY_BITMAP_EXTENT)
		return type == VEA_FREE_ENTRY_EXTENT;

	return vfe->vfe_type == type;
}

/* Free entry to in-memory compound index */
int
compound_free(struct vea_space_info *vsi, struct vea_free_entry *vfe,
	      unsigned int flags)
{
	uint16_t class;
	int	type = free_type(vsi, vfe->vfe_ext.vfe_blk_off,
				 vfe->vfe_ext.vfe_blk_cnt, true, &class);

	if (type < 0)
		return type;

	if (!free_entry_type_match(vfe, type)) {
		D_ERROR("mismatch free entry type expected: %d, but got: %d\n",
			vfe->vfe_type, type);
		return -DER_INVAL;
	}

	if (vfe->vfe_type == VEA_FREE_ENTRY_EXTENT ||
	    vfe->vfe_type == VEA_FREE_ENTRY_BITMAP_EXTENT)
		return compound_free_extent(vsi, &vfe->vfe_ext, flags);

	return compound_free_bitmap(vsi, &vfe->vfe_ext, flags);
}

/* Free extent to persistent free tree */
static int
persistent_free_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_free_extent	dummy;
	d_iov_t			key, val;
	daos_handle_t		btr_hdl = vsi->vsi_md_free_btr;
	int			rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_PERSIST, 0);
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

static int
persistent_free_bitmap(struct vea_space_info *vsi, struct vea_free_entry *vfe)
{
	struct vea_free_bitmap	*found;
	daos_handle_t		 btr_hdl;
	d_iov_t			 key_in, val;
	int			 rc, opc = BTR_PROBE_LE;

	D_ASSERT(umem_tx_inprogress(vsi->vsi_umem) ||
		 vsi->vsi_umem->umm_id == UMEM_CLASS_VMEM);
	D_ASSERT(vfe->vfe_ext.vfe_blk_off != VEA_HINT_OFF_INVAL);
	D_ASSERT(vfe->vfe_ext.vfe_blk_cnt > 0 &&
		 vfe->vfe_ext.vfe_blk_cnt < vsi->vsi_class.vfc_large_thresh);

	btr_hdl = vsi->vsi_md_bitmap_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));

	D_DEBUG(DB_IO, "Persistent free bitmap ["DF_U64", %u]\n",
		vfe->vfe_ext.vfe_blk_off, vfe->vfe_ext.vfe_blk_cnt);

	/* Fetch & operate on the in-tree record */
	d_iov_set(&key_in, &vfe->vfe_ext.vfe_blk_off,
		  sizeof(vfe->vfe_ext.vfe_blk_off));
	d_iov_set(&val, NULL, 0);
	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key_in, NULL,
			  &val);
	if (rc) {
		D_ERROR("failed to find bitmap ["DF_U64", %u]\n",
			vfe->vfe_ext.vfe_blk_off, vfe->vfe_ext.vfe_blk_cnt);
		return rc;
	}

	found = (struct vea_free_bitmap *)val.iov_buf;
	rc = verify_bitmap_entry(found);
	if (rc) {
		D_ERROR("persistent free bitmap verify failed\n");
		return rc;
	}

	return bitmap_clear_range(vsi->vsi_umem, found, vfe->vfe_ext.vfe_blk_off,
				  vfe->vfe_ext.vfe_blk_cnt);
}

int
persistent_free(struct vea_space_info *vsi, struct vea_free_entry *vfe)
{

	if (vfe->vfe_type == VEA_FREE_ENTRY_EXTENT ||
	    vfe->vfe_type == VEA_FREE_ENTRY_BITMAP_EXTENT)
		return persistent_free_extent(vsi, &vfe->vfe_ext);

	D_ASSERT(vfe->vfe_type == VEA_FREE_ENTRY_BITMAP);

	return persistent_free_bitmap(vsi, vfe);
}

/* Free extent to the aggregate free tree */
int
aggregated_free(struct vea_space_info *vsi, struct vea_free_entry *vfe)
{
	struct vea_free_entry	*entry, dummy;
	d_iov_t			 key, val, val_out;
	daos_handle_t		 btr_hdl = vsi->vsi_agg_btr;
	int			 rc;

	vfe->vfe_ext.vfe_age = get_current_age();
	rc = merge_free_entry(vsi, vfe, 0);
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
	D_ASSERT(entry->vfe_type == vfe->vfe_type);

	dock_aging_entry(vsi, entry);
	return 0;
}

#define FLUSH_INTVL		5	/* seconds */
#define EXPIRE_INTVL		10	/* seconds */

static int
flush_internal(struct vea_space_info *vsi, bool force, uint32_t cur_time, d_sg_list_t *unmap_sgl)
{
	struct vea_free_entry	*entry, *tmp;
	struct vea_free_extent	 vfe;
	struct vea_free_entry	 free_entry;
	d_iov_t			*unmap_iov;
	int			 i, rc = 0;
	d_iov_t			 key;
	int			 type;
	uint8_t			 *flush_type;

	D_ASSERT(umem_tx_none(vsi->vsi_umem));
	D_ASSERT(unmap_sgl->sg_nr_out == 0);

	D_ALLOC_ARRAY(flush_type, MAX_FLUSH_FRAGS);
	if (!flush_type)
		return -DER_NOMEM;

	d_list_for_each_entry_safe(entry, tmp, &vsi->vsi_agg_lru, vfe_link) {
		vfe = entry->vfe_ext;
		if (!force && cur_time < (vfe.vfe_age + EXPIRE_INTVL))
			break;

		/* Remove entry from aggregate LRU list */
		d_list_del_init(&entry->vfe_link);
		dec_stats(vsi, STAT_FRAGS_AGING, 1);

		type = entry->vfe_type;
		/* Remove entry from aggregate tree, entry will be freed on deletion */
		d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
		D_ASSERT(daos_handle_is_valid(vsi->vsi_agg_btr));
		rc = dbtree_delete(vsi->vsi_agg_btr, BTR_PROBE_EQ, &key, NULL);
		if (rc) {
			D_ERROR("Remove ["DF_U64", %u] from aggregated tree error: "DF_RC"\n",
				vfe.vfe_blk_off, vfe.vfe_blk_cnt, DP_RC(rc));
			break;
		}

		flush_type[unmap_sgl->sg_nr_out] = type;
		/* Unmap callback may yield, so we can't call it directly in this tight loop */
		unmap_sgl->sg_nr_out++;
		unmap_iov = &unmap_sgl->sg_iovs[unmap_sgl->sg_nr_out - 1];
		unmap_iov->iov_buf = (void *)vfe.vfe_blk_off;
		unmap_iov->iov_len = vfe.vfe_blk_cnt;

		if (unmap_sgl->sg_nr_out == MAX_FLUSH_FRAGS)
			break;
	}

	vsi->vsi_flush_time = cur_time;

	/*
	 * According to NVMe spec, unmap isn't an expensive non-queue command
	 * anymore, so we should just unmap as soon as the extent is freed.
	 *
	 * Since unmap could yield, it must be called before the compound_free(),
	 * otherwise, the extent could be visible for allocation before unmap done.
	 */
	if (vsi->vsi_unmap_ctxt.vnc_unmap != NULL && unmap_sgl->sg_nr_out > 0) {
		rc = vsi->vsi_unmap_ctxt.vnc_unmap(unmap_sgl, vsi->vsi_md->vsd_blk_sz,
						   vsi->vsi_unmap_ctxt.vnc_data);
		if (rc)
			D_ERROR("Unmap %u frags failed: "DF_RC"\n",
				unmap_sgl->sg_nr_out, DP_RC(rc));
	}

	for (i = 0; i < unmap_sgl->sg_nr_out; i++) {
		unmap_iov = &unmap_sgl->sg_iovs[i];

		free_entry.vfe_ext.vfe_blk_off = (uint64_t)unmap_iov->iov_buf;
		free_entry.vfe_ext.vfe_blk_cnt = unmap_iov->iov_len;
		free_entry.vfe_ext.vfe_age = cur_time;
		free_entry.vfe_type = flush_type[i];

		if (free_entry.vfe_type == VEA_FREE_ENTRY_BITMAP_EXTENT)
			rc = compound_free(vsi, &free_entry, VEA_FL_NO_ACCOUNTING);
		else
			rc = compound_free(vsi, &free_entry, 0);
		if (rc)
			D_ERROR("Compound free ["DF_U64", %u] error: "DF_RC"\n",
				free_entry.vfe_ext.vfe_blk_off, free_entry.vfe_ext.vfe_blk_cnt,
				DP_RC(rc));
	}
	D_FREE(flush_type);

	return rc;
}

static inline bool
need_aging_flush(struct vea_space_info *vsi, uint32_t cur_time, bool force)
{
	if (d_list_empty(&vsi->vsi_agg_lru))
		return false;

	/* External flush controls the flush rate externally */
	if (vsi->vsi_unmap_ctxt.vnc_ext_flush)
		return true;

	if (!force && cur_time < (vsi->vsi_flush_time + FLUSH_INTVL))
		return false;

	return true;
}

int
trigger_aging_flush(struct vea_space_info *vsi, bool force, uint32_t nr_flush,
		    uint32_t *nr_flushed)
{
	d_sg_list_t	 unmap_sgl;
	uint32_t	 cur_time, tot_flushed = 0;
	int		 rc;

	D_ASSERT(nr_flush > 0);
	if (!umem_tx_none(vsi->vsi_umem)) {
		rc = -DER_INVAL;
		goto out;
	}

	cur_time = get_current_age();
	if (!need_aging_flush(vsi, cur_time, force)) {
		rc = 0;
		goto out;
	}

	rc = d_sgl_init(&unmap_sgl, MAX_FLUSH_FRAGS);
	if (rc)
		goto out;

	while (tot_flushed < nr_flush) {
		rc = flush_internal(vsi, force, cur_time, &unmap_sgl);

		tot_flushed += unmap_sgl.sg_nr_out;
		if (rc || unmap_sgl.sg_nr_out < MAX_FLUSH_FRAGS)
			break;

		unmap_sgl.sg_nr_out = 0;
	}

	d_sgl_fini(&unmap_sgl, false);
out:
	if (nr_flushed != NULL)
		*nr_flushed = tot_flushed;

	return rc;
}

static void
flush_end_cb(void *data, bool noop)
{
	struct vea_space_info	*vsi = data;

	if (!noop)
		trigger_aging_flush(vsi, false, MAX_FLUSH_FRAGS * 20, NULL);

	vsi->vsi_flush_scheduled = false;
}

int
schedule_aging_flush(struct vea_space_info *vsi)
{
	int	rc;

	D_ASSERT(vsi != NULL);

	if (vsi->vsi_unmap_ctxt.vnc_ext_flush)
		return 0;

	/* Check flush condition in advance to avoid unnecessary umem_tx_add_callback() */
	if (!need_aging_flush(vsi, get_current_age(), false))
		return 0;

	/* Schedule one transaction end callback flush is enough */
	if (vsi->vsi_flush_scheduled)
		return 0;

	/*
	 * Perform the flush in transaction end callback, since the flush operation
	 * could yield on blob unmap.
	 */
	rc = umem_tx_add_callback(vsi->vsi_umem, vsi->vsi_txd, UMEM_STAGE_NONE,
				  flush_end_cb, vsi);
	if (rc) {
		D_ERROR("Add transaction end callback error "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	vsi->vsi_flush_scheduled = true;

	return 0;
}
