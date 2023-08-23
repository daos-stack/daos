/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos. It implements some miscellaneous policy functions
 */
#ifndef __AD_MEM_H__
#define __AD_MEM_H__

#include <daos/common.h>
#include <daos_srv/ad_mem.h>
#include <gurt/heap.h>

typedef void (*ad_tx_cb_t)(int stage, void *data);

struct ad_act {
	d_list_t		it_link;
	struct umem_action	it_act;
};

struct ad_range {
	/* link to ad_tx::tx_redo_ranges */
	d_list_t		 ar_link;
	uint64_t		 ar_off;
	uint64_t		 ar_size;
	bool			 ar_alloc;
};

/** ad-hoc allocator transaction handle */
struct ad_tx {
	struct ad_blob		*tx_blob;
	d_list_t		 tx_undo;
	d_list_t		 tx_redo;
	d_list_t		 tx_ar_pub;
	d_list_t		 tx_gp_pub;
	d_list_t		 tx_gp_reset;
	/** in-flight frees */
	d_list_t		 tx_frees;
	/** in-flight allocations */
	d_list_t		 tx_allocs;
	uint32_t		 tx_redo_act_nr;
	uint32_t		 tx_redo_payload_len;
	struct ad_act		*tx_redo_act_pos;
	/* the number of layer of nested TX, outermost layer is 1, +1 for inner TX */
	int			 tx_layer;
	int			 tx_last_errno;
	ad_tx_cb_t		 tx_stage_cb;
	void			*tx_stage_cb_arg;
	/* tx_add ranges that need to redo when commit */
	d_list_t		 tx_ranges;
};

/** action parameters for allocate or free() */
struct ad_operate {
	d_list_t		 op_link;
	int			 op_at;
	struct ad_group		*op_group;
};

/** bitmap size of group */
#define GRP_UNIT_BMSZ		8
/** 32K is the minimum size of a group */
#define GRP_SIZE_SHIFT		(15)
#define GRP_SIZE_MASK		((1 << GRP_SIZE_SHIFT) - 1)

/** Flags for defragmentation */
enum ad_grp_flags {
	/** relocated group */
	GRP_FL_RELOCATED	= (1 << 0),
	/** sparse group, allocated address are stored in a array */
	GRP_FL_SPARSE		= (1 << 1),
};

#define GRP_UNIT_SZ_MAX		(1U << 20)
#define GRP_UNIT_NR_MAX		(1U << 20)

/** Durable format of group (128 bytes) */
struct ad_group_df {
	/** base address */
	uint64_t		gd_addr;
	/**
	 * Real address of the group, it is set to zero for now.
	 *
	 * This is reserved for future defragmentation support, a group can be moved
	 * within arena or even between arenas, @ad_addr_real is the real address of
	 * the group, @gd_addr is the base logic address of the group.
	 */
	uint64_t		gd_addr_real;
	/** DRAM address for reserve() */
	uint64_t		gd_back_ptr;
	/** incarnation for validity check of gd_back_ptr */
	uint64_t		gd_incarnation;
	/** unit size in bytes, e.g., 64 bytes, 128 bytes */
	int32_t			gd_unit;
	/** number of units in this group */
	int32_t			gd_unit_nr;
	/** number of free units in this group */
	int32_t			gd_unit_free;
	/** flags for future use, see ad_grp_flags */
	uint32_t		gd_flags;
	uint64_t		gd_reserved[2];
	/** used bitmap, 512 units at most so it can fit into 128-byte */
	uint64_t		gd_bmap[GRP_UNIT_BMSZ];
};

/** DRAM format of group, it is referenced by ad_group_df::gd_back_ptr */
struct ad_group {
	struct ad_arena		*gp_arena;
	/** address of durable format */
	struct ad_group_df	*gp_df;
	/** unpublished group */
	unsigned int		 gp_unpub:1,
	/** being published */
				 gp_publishing:1,
	/* group freed and being reset */
				 gp_reset:1;
	int			 gp_frags;
	int			 gp_ref;
	/** number of reserved units */
	int			 gp_unit_rsv;
	/** bit offset in ad_arena_df */
	int			 gp_bit_at;
	/** number of bits consumed by this group */
	int			 gp_bit_nr;
	/** link chain on blob LRU */
	d_list_t		 gp_link;
	/* reserved bits */
	uint64_t		 gp_bmap_rsv[GRP_UNIT_BMSZ];
};

/** metrics of ad-mem group, it includes data from all groups with the same unit size */
struct ad_group_metrics {
	uint32_t		gm_total;
	uint32_t		gm_free;
	uint32_t		gm_failed;
	uint32_t		gm_reserved;
};

#define ARENA_GRP_SPEC_MAX	24
#define ARENA_GRP_BMSZ		8

/** Customized specs for arena. */
struct ad_arena_spec {
	/** arena type, default arena type is 0 */
	uint32_t		as_type;
	/** arena unit size, reserved for future use */
	uint32_t		as_unit;
	/* last active arena of this type, this is not really part of spec... */
	uint32_t		as_last_used;
	/** number of group specs (valid members of as_specs) */
	uint32_t		as_specs_nr;
	/** group sizes and number of units within each group */
	struct ad_group_spec	as_specs[ARENA_GRP_SPEC_MAX];
};

/** Default arena size is 16MB */
#define ARENA_SIZE_BITS		(24)
#define ARENA_SIZE_MASK		((1ULL << ARENA_SIZE_BITS) - 1)
#define ARENA_SIZE		(1ULL << ARENA_SIZE_BITS)

#define ARENA_UNIT_SIZE		(32 << 10)
/** Arena header size, 64KB */
#define ARENA_HDR_SIZE		(2 * ARENA_UNIT_SIZE)
/** Blob header size */
#define BLOB_HDR_SIZE		(32 << 10)
/** Root object size */
#define AD_ROOT_OBJ_SIZE	(32 << 10)
#define AD_ROOT_OBJ_OFF		(ARENA_HDR_SIZE + BLOB_HDR_SIZE)

/**
 * Maximum number of groups within an arena.
 * In order to keep low metadata overhead (64K is 0.4% of default arena size (16MB)), no more
 * than 480 groups within an arena, otherwise please tune ad-hoc allocator specs,
 *
 * It means that if user always allocate 64 bytes, AD allocator cannot fully utilize the space
 * because 16MB arena can be filled with 508 arenas. We reserved some space in header for the
 * future defragmentation.
 */
#define ARENA_GRP_MAX		480
/** estimated average group numbers per arena */
#define ARENA_GRP_AVG		256

#define ARENA_MAGIC		0xcafe

/** Arena header, it should be within 32K */
struct ad_arena_df {
	uint16_t		ad_magic;
	/** for SLAB style allocation */
	uint16_t		ad_type;
	/** Arena ID (its highnest bit is reserved for external arena) */
	uint32_t		ad_id;
	/** arena size, default is 16MB, <= 4GB */
	uint32_t		ad_size;
	/** minimum allocation unit */
	int32_t			ad_unit;
	/** internal offset for locating */
	int64_t			ad_pad64;
	/**
	 * validate @ad_sort_sz_tmp, @ad_sort_ad_tmp and ad_back_ptr, because they are DRAM
	 * pointers.
	 */
	uint64_t		ad_incarnation;
	/** external blob ID, an arena can be created for external blob */
	uint64_t		ad_blob_id;
	/** blob address */
	uint64_t		ad_addr;
	/** for future use */
	uint64_t		ad_reserved[2];
	/** 64 bytes (512 bits) for each, each bit represents 32K(minimum group size) */
	uint64_t		ad_bmap[ARENA_GRP_BMSZ];
	/** it is DRAM reference of arena (the DRAM arena is created on demand) */
	uint64_t		ad_back_ptr;
	/** 128 bytes each, no more than ARENA_GRP_MAX groups */
	struct ad_group_df	ad_groups[0];
};

/** DRAM format of arena, it is referenced by ad_arena_df::ad_back_ptr */
struct ad_arena {
	struct ad_blob		 *ar_blob;
	/** point to durable arena */
	struct ad_arena_df	 *ar_df;
	/** link chain on ad_blob LRU */
	d_list_t		  ar_link;
	/** link chain on reorder list */
	d_list_t		  ar_ro_link;
	/** arena type */
	int			  ar_type;
	/** refcount */
	int			  ar_ref;
	/** number of groups */
	int			  ar_grp_nr;
	/** last grp index, all prev-grps are used, following grps possibly used */
	int			  ar_last_grp;
	/** sorter buffer size */
	int			  ar_sorter_sz;
	/** unpublished arena */
	unsigned int		  ar_unpub:1,
	/** being published */
				  ar_publishing:1;
	/** pointers for size binary search, it is only used by the DRAM mirror*/
	struct ad_group_df	**ar_size_sorter;
	/** pointers for address binary search, it is only used by the DRAM mirror*/
	struct ad_group_df	**ar_addr_sorter;
	/** reserved bits for group space */
	uint64_t		  ar_space_rsv[ARENA_GRP_BMSZ];
	/** reserved bits for group index */
	uint64_t		  ar_gpid_rsv[ARENA_GRP_BMSZ];
	/** metrics */
	struct ad_group_metrics   ar_grp_mtcs[ARENA_GRP_SPEC_MAX];
};

/** page for each arena */
struct ad_page {
	/* the page in-use */
	char			*pa_rpg;
	/* reserved for checkpoint */
	char			*pa_cpg;
};

/** reserved for future use */
struct ad_page_extern {
	struct ad_page		 pe_page;
	d_list_t		 pa_link;
	struct umem_store	*pa_store;
};

/* register up to 31 arenas types (type=0 is predefined) */
#define ARENA_SPEC_MAX		32

#define BLOB_MAGIC		0xbabecafe

#define AD_MEM_VERSION		1

/** root data structure of ad-hoc allocator */
struct ad_blob_df {
	/** magic number */
	uint32_t		bd_magic;
	/** version number */
	uint32_t		bd_version;
	/** loading incarnation */
	uint64_t		bd_incarnation;
	/** it is DRAM reference of blob */
	uint64_t		bd_back_ptr;
	/** capacity managed by the allocator */
	uint64_t		bd_size;
	/** size of each arena, default size is 16MB */
	uint64_t		bd_arena_size;
	/** specifications of registered arena types */
	struct ad_arena_spec	bd_asp[ARENA_SPEC_MAX];
	/** reserve some bytes for future usage */
	uint64_t		bd_reserved[4];
	/** allocated bits */
	uint64_t		bd_bmap[0];
};

#define DUMMY_BLOB	"dummy.blob"

struct ad_maxheap_node {
	struct d_binheap_node	mh_node;
	int			mh_weight;
	int			mh_free_size;
	/** unusable padding bytes in groups */
	int			mh_frag_size;
	uint32_t		mh_arena_id;
	unsigned int		mh_in_tree:1,
	/**
	 * Arena is inactive, it's set to true when any type of group failed to allocate memory
	 * and create more groups.
	 * XXX: this is not enough, we should save failed allocatoin counter in matrics and
	 * set arenea as full only if it encounters multiple failures.
	 */
				mh_inactive:1;
};

/** DRAM blob open handle */
struct ad_blob {
	/** external umem store */
	struct umem_store	 bb_store;
	/** reserved: pages being checkpointed */
	d_list_t		 bb_pgs_ckpt;
	/** reserved: pages for external arenas */
	d_list_t		 bb_pgs_extern;
	/** arenas being reserved (not published), pinned at here */
	d_list_t		 bb_ars_rsv;
	/** unused arena LRU */
	d_list_t		 bb_ars_lru;
	/** group being reserved (not published), pinned at here */
	d_list_t		 bb_gps_rsv;
	/** unused group LRU */
	d_list_t		 bb_gps_lru;
	int			 bb_ars_lru_size;
	int			 bb_ars_lru_cap;
	int			 bb_gps_lru_size;
	int			 bb_gps_lru_cap;
	char			 *bb_path;
	daos_size_t		 bb_stat_sz;
	/** file descriptor of MD file */
	int			 bb_fd;
	/** reference counter */
	int			 bb_ref;
	/** is dummy blob, for unit test */
	bool			 bb_dummy;
	/** open refcount */
	int			 bb_opened;
	/** number of pages */
	unsigned int		 bb_pgs_nr;
	/**
	 * last used arena ID.
	 * TODO: instead of choosing the last used, select arena based on their "weight",
	 * which is computed out from free space of arena.
	 */
	uint32_t		 bb_arena_last[ARENA_SPEC_MAX];
	/** NB: either initialize @bb_mmap or @bb_pages, only support @bb_map in phase-1 */
	void			*bb_mmap;
	/** all the cached pages */
	struct ad_page		*bb_pages;
	/** reference to durable format of the blob */
	struct ad_blob_df	*bb_df;
	/** reserved bits for arena allocation */
	uint64_t		*bb_bmap_rsv;
	/* max heap nodes pointer */
	struct ad_maxheap_node	*bb_mh_nodes;
	/* max heap for free memory of arena */
	struct d_binheap	 bb_arena_free_heap;
};

static inline void
ad_iod_set(struct umem_store_iod *iod, daos_off_t addr, daos_size_t size)
{
	iod->io_nr = 1;
	iod->io_regions = &iod->io_region;
	iod->io_region.sr_addr = addr;
	iod->io_region.sr_size = size;
}

static inline void
ad_sgl_set(d_sg_list_t *sgl, d_iov_t *iov, void *buf, daos_size_t size)
{
	sgl->sg_iovs	= iov;
	sgl->sg_nr	= 1;
	sgl->sg_nr_out	= 0;
	d_iov_set(iov, buf, size);
}

static inline daos_size_t
blob_size(struct ad_blob *blob)
{
	return blob->bb_store.stor_size;
}

static inline daos_size_t
blob_addr(struct ad_blob *blob)
{
	return 0;
}

static inline int
blob_arena_max(struct ad_blob *blob)
{
	return blob->bb_pgs_nr;
}

void blob_addref(struct ad_blob *blob);
void blob_decref(struct ad_blob *blob);
void *blob_addr2ptr(struct ad_blob *blob, daos_off_t addr);
daos_off_t blob_ptr2addr(struct ad_blob *blob, void *ptr);

int tx_complete(struct ad_tx *tx, int err);
int tx_begin(struct ad_blob_handle bh, struct umem_tx_stage_data *txd, struct ad_tx **tx_pp);
int tx_end(struct ad_tx *tx, int err);

static inline struct ad_tx *
umem_tx2ad_tx(struct umem_wal_tx *utx)
{
	return (struct ad_tx *)&utx->utx_private;
}

static inline struct umem_wal_tx *
ad_tx2umem_tx(struct ad_tx *atx)
{
	return container_of(atx, struct umem_wal_tx, utx_private);
}

static inline uint64_t
ad_tx_id(struct ad_tx *atx)
{
	return ad_tx2umem_tx(atx)->utx_id;
}

static inline void
ad_tx_id_set(struct ad_tx *atx, uint64_t id)
{
	ad_tx2umem_tx(atx)->utx_id = id;
}

static inline int
ad_tx_stage(struct ad_tx *atx)
{
	return ad_tx2umem_tx(atx)->utx_stage;
}

static inline void
ad_tx_stage_set(struct ad_tx *atx, int stage)
{
	ad_tx2umem_tx(atx)->utx_stage = stage;
}

void ad_tls_cache_init(void);
void ad_tls_cache_fini(void);

#endif /* __AD_MEM_H__ */
