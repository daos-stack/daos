/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __VEA_INTERNAL_H__
#define __VEA_INTERNAL_H__

#include <gurt/list.h>
#include <gurt/heap.h>
#include <daos/mem.h>
#include <daos/btree.h>
#include <daos/common.h>
#include <daos_srv/vea.h>

#define VEA_MAGIC	(0xea201804)
#define VEA_BLK_SZ	(4 * 1024)	/* 4K */
#define VEA_TREE_ODR	20

/* Common free extent structure for both SCM & in-memory index */
struct vea_free_extent {
	uint64_t	vfe_blk_off;	/* Block offset of the extent */
	uint32_t	vfe_blk_cnt;	/* Total blocks of the extent */
	uint32_t	vfe_age;	/* Monotonic timestamp */
};

/* Min bitmap allocation class */
#define VEA_MIN_BITMAP_CLASS	1
/* Max bitmap allocation class */
#define VEA_MAX_BITMAP_CLASS	64

/* Bitmap chunk size */
#define VEA_BITMAP_MIN_CHUNK_BLKS	256				/* 1MiB */
#define VEA_BITMAP_MAX_CHUNK_BLKS	(VEA_MAX_BITMAP_CLASS * 256)	/* 64 MiB */


/* Common free bitmap structure for both SCM & in-memory index */
struct vea_free_bitmap {
	uint64_t	vfb_blk_off;				/* Block offset of the bitmap */
	uint32_t	vfb_blk_cnt;				/* Block count of the bitmap */
	uint16_t	vfb_class;				/* Allocation class of bitmap */
	uint16_t	vfb_bitmap_sz;				/* Bitmap size*/
	uint64_t	vfb_bitmaps[0];				/* Bitmaps of this chunk */
};

/* Per I/O stream hint context */
struct vea_hint_context {
	struct vea_hint_df	*vhc_pd;
	/* In-memory hint block offset */
	uint64_t		 vhc_off;
	/* In-memory hint sequence */
	uint64_t		 vhc_seq;
};

/* Free extent informat stored in the in-memory compound free extent index */
struct vea_extent_entry {
	/*
	 * Always keep it as first item, since vfe_blk_off is the direct key
	 * of DBTREE_CLASS_IV
	 */
	struct vea_free_extent	 vee_ext;
	/* Link to one of vsc_extent_lru */
	d_list_t		 vee_link;
	/* Back reference to sized tree entry */
	struct vea_sized_class	*vee_sized_class;
	/* Link to vfc_heap */
	struct d_binheap_node	 vee_node;
};

enum {
	VEA_BITMAP_STATE_PUBLISHED,
	VEA_BITMAP_STATE_PUBLISHING,
	VEA_BITMAP_STATE_NEW,
};

/* Bitmap entry */
struct vea_bitmap_entry {
	/* Link to one of vfc_bitmap_lru[] */
	d_list_t		 vbe_link;
	/* Bitmap published state */
	int			 vbe_published_state;
	/*
	 * Free entries sorted by offset, for coalescing the just recent
	 * free blocks inside this bitmap chunk.
	 */
	daos_handle_t		 vbe_agg_btr;
	/* Point to persistent free bitmap entry */
	struct vea_free_bitmap	*vbe_md_bitmap;
	/* free bitmap, always keep it as last item*/
	struct vea_free_bitmap	 vbe_bitmap;
};

enum {
	VEA_FREE_ENTRY_EXTENT,
	VEA_FREE_ENTRY_BITMAP,
};

/* freed entry stored in aggregation tree */
struct vea_free_entry {
	struct vea_free_extent	 vfe_ext;
	/* Back pointer bitmap entry */
	struct vea_bitmap_entry	*vfe_bitmap;
	/* Link to one vsi_agg_lru */
	d_list_t		 vfe_link;
};

#define VEA_LARGE_EXT_MB	64	/* Large extent threshold in MB */
#define VEA_HINT_OFF_INVAL	0	/* Invalid hint offset */

/* Value entry of sized free extent tree (vfc_size_btr) */
struct vea_sized_class {
	/* Small extents LRU list */
	d_list_t		vsc_extent_lru;
};

#define VEA_BITMAP_CHUNK_HINT_KEY	(~(0ULL))
/*
 * Large free extents (>VEA_LARGE_EXT_MB) are tracked in max a heap, small
 * free extents (<= VEA_LARGE_EXT_MB) are tracked in a size tree.
 */
struct vea_free_class {
	/* Max heap for tracking the largest free extent */
	struct d_binheap	vfc_heap;
	/* Small free extent tree */
	daos_handle_t		vfc_size_btr;
	/* Size threshold for large extent */
	uint32_t		vfc_large_thresh;
	/* Bitmap LRU list for different bitmap allocation class*/
	d_list_t		vfc_bitmap_lru[VEA_MAX_BITMAP_CLASS];
	/* Empty bitmap list for different allocation class */
	d_list_t		vfc_bitmap_empty[VEA_MAX_BITMAP_CLASS];
};

enum {
	/* Number of hint reserve */
	STAT_RESRV_HINT		= 0,
	/* Number of large reserve */
	STAT_RESRV_LARGE	= 1,
	/* Number of small extents reserve */
	STAT_RESRV_SMALL	= 2,
	/* Number of bitmap reserve */
	STAT_RESRV_BITMAP	= 3,
	/* Max reserve type */
	STAT_RESRV_TYPE_MAX	= 4,
	/* Number of large(> VEA_LARGE_EXT_MB) free frags available for allocation */
	STAT_FRAGS_LARGE	= 4,
	/* Number of small free extent frags available for allocation */
	STAT_FRAGS_SMALL	= 5,
	/* Number of frags in aging buffer (to be unmapped) */
	STAT_FRAGS_AGING	= 6,
	/* Number of bitmaps */
	STAT_FRAGS_BITMAP	= 7,
	/* Max frag type */
	STAT_FRAGS_TYPE_MAX	= 4,
	/* Number of extent blocks available for allocation */
	STAT_FREE_EXTENT_BLKS	= 8,
	/* Number of bitmap blocks available for allocation */
	STAT_FREE_BITMAP_BLKS	= 9,
	STAT_MAX		= 10,
};

struct vea_metrics {
	struct d_tm_node_t	*vm_rsrv[STAT_RESRV_TYPE_MAX];
	struct d_tm_node_t	*vm_frags[STAT_FRAGS_TYPE_MAX];
	struct d_tm_node_t	*vm_free_blks;
};

#define MAX_FLUSH_FRAGS	256

/* In-memory compound index */
struct vea_space_info {
	/* Instance for the pmemobj pool on SCM */
	struct umem_instance		*vsi_umem;
	/*
	 * Stage callback data used by PMDK transaction.
	 *
	 * No public API offered by PMDK to get transaction stage callback
	 * data, so we have to pass it around.
	 */
	struct umem_tx_stage_data	*vsi_txd;
	/* Free space information stored on SCM */
	struct vea_space_df		*vsi_md;
	/* Open handles for the persistent free extent tree */
	daos_handle_t			 vsi_md_free_btr;
	/* Open handles for the persistent bitmap tree */
	daos_handle_t			 vsi_md_bitmap_btr;
	/* Free extent tree sorted by offset, for all free extents. */
	daos_handle_t			 vsi_free_btr;
	/* Bitmap tree, for small allocation */
	daos_handle_t			 vsi_bitmap_btr;
	/* Hint context for bitmap chunk allocation */
	struct vea_hint_context		*vsi_bitmap_hint_context;
	/* Index for searching free extent by size & age */
	struct vea_free_class		 vsi_class;
	/* LRU to aggergate just recent freed extents or bitmap blocks */
	d_list_t			 vsi_agg_lru;
	/*
	 * Free entries sorted by offset, for coalescing the just recent
	 * free extents.
	 */
	daos_handle_t			 vsi_agg_btr;
	/* Unmap context to perform unmap against freed extent */
	struct vea_unmap_context	 vsi_unmap_ctxt;
	/* Statistics */
	uint64_t			 vsi_stat[STAT_MAX];
	/* Metrics */
	struct vea_metrics		*vsi_metrics;
	/* Last aging buffer flush timestamp */
	uint32_t			 vsi_flush_time;
	bool				 vsi_flush_scheduled;
};

struct free_commit_cb_arg {
	struct vea_space_info	*fca_vsi;
	struct vea_free_entry	 fca_vfe;
};

static inline uint32_t
get_current_age(void)
{
	uint64_t age = 0;

	age = daos_gettime_coarse();
	return (uint32_t)age;
}

enum vea_free_flags {
	VEA_FL_NO_MERGE		= (1 << 0),
	VEA_FL_NO_ACCOUNTING	= (1 << 1),
};

static inline bool
is_bitmap_feature_enabled(struct vea_space_info *vsi)
{
	return vsi->vsi_md->vsd_compat & VEA_COMPAT_FEATURE_BITMAP;
}

static inline int
alloc_free_bitmap_size(uint16_t bitmap_sz)
{
	return sizeof(struct vea_free_bitmap) + (bitmap_sz << 3);
}

static inline uint32_t
bitmap_free_blocks(struct vea_free_bitmap *vfb)
{
	uint32_t	free_blocks;
	int		diff;

	int free_bits = daos_count_free_bits(vfb->vfb_bitmaps, vfb->vfb_bitmap_sz);

	free_blocks = free_bits * vfb->vfb_class;
	diff = vfb->vfb_bitmap_sz * 64 * vfb->vfb_class - vfb->vfb_blk_cnt;

	D_ASSERT(diff == 0);

	return free_blocks;
}

static inline bool
is_bitmap_empty(uint64_t *bitmap, int bitmap_sz)
{
	int i;

	for (i = 0; i < bitmap_sz; i++)
		if (bitmap[i])
			return false;

	return true;
}

/* vea_init.c */
void destroy_free_class(struct vea_free_class *vfc);
int create_free_class(struct vea_free_class *vfc, struct vea_space_df *md);
void unload_space_info(struct vea_space_info *vsi);
int load_space_info(struct vea_space_info *vsi);

/* vea_util.c */
int verify_free_entry(uint64_t *off, struct vea_free_extent *vfe);
int verify_bitmap_entry(struct vea_free_bitmap *vfb);
int ext_adjacent(struct vea_free_extent *cur, struct vea_free_extent *next);
int verify_resrvd_ext(struct vea_resrvd_ext *resrvd);
int vea_dump(struct vea_space_info *vsi, bool transient);
int vea_verify_alloc(struct vea_space_info *vsi, bool transient,
		     uint64_t off, uint32_t cnt, bool is_bitmap);
void dec_stats(struct vea_space_info *vsi, unsigned int type, uint64_t nr);
void inc_stats(struct vea_space_info *vsi, unsigned int type, uint64_t nr);

/* vea_alloc.c */
int reserve_hint(struct vea_space_info *vsi, uint32_t blk_cnt,
		 struct vea_resrvd_ext *resrvd);
int reserve_single(struct vea_space_info *vsi, uint32_t blk_cnt,
		   struct vea_resrvd_ext *resrvd);
int persistent_alloc(struct vea_space_info *vsi, struct vea_free_entry *vfe);
int
bitmap_tx_add_ptr(struct umem_instance *vsi_umem, uint64_t *bitmap,
		  uint32_t bit_at, uint32_t bits_nr);
int
bitmap_set_range(struct umem_instance *vsi_umem, struct vea_free_bitmap *bitmap,
		 uint64_t blk_off, uint32_t blk_cnt, bool clear);

/* vea_free.c */
void extent_free_class_remove(struct vea_space_info *vsi, struct vea_extent_entry *entry);
int extent_free_class_add(struct vea_space_info *vsi, struct vea_extent_entry *entry);
int compound_free_extent(struct vea_space_info *vsi, struct vea_free_extent *vfe,
			 unsigned int flags);
int compound_free(struct vea_space_info *vsi, struct vea_free_entry *vfe, unsigned int flags);
int persistent_free(struct vea_space_info *vsi, struct vea_free_entry *vfe);
int aggregated_free(struct vea_space_info *vsi, struct vea_free_entry *vfe);
int trigger_aging_flush(struct vea_space_info *vsi, bool force,
			uint32_t nr_flush, uint32_t *nr_flushed);
int bitmap_entry_insert(struct vea_space_info *vsi, struct vea_free_bitmap *vfb,
			int state, struct vea_bitmap_entry **ret_entry, unsigned int flags);
int free_type(struct vea_space_info *vsi, uint64_t blk_off, uint32_t blk_cnt,
	      struct vea_bitmap_entry **bitmap_entry);
void
free_commit_cb(void *data, bool noop);

/* vea_hint.c */
void hint_get(struct vea_hint_context *hint, uint64_t *off);
void hint_update(struct vea_hint_context *hint, uint64_t off, uint64_t *seq);
int hint_cancel(struct vea_hint_context *hint, uint64_t off, uint64_t seq_min,
		uint64_t seq_max, unsigned int seq_cnt);
int hint_tx_publish(struct umem_instance *umm, struct vea_hint_context *hint,
		    uint64_t off, uint64_t seq_min, uint64_t seq_max,
		    unsigned int seq_cnt);

#endif /* __VEA_INTERNAL_H__ */
