/**
 * (C) Copyright 2022 Intel Corporation.
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

/** ad-hoc allocator transaction handle */
struct ad_tx {
	struct ad_blob		*tx_blob;
	uint64_t		 tx_id;
	d_list_t		 tx_undo;
	d_list_t		 tx_redo;
	d_list_t		 tx_ar_pub;
	d_list_t		 tx_gp_pub;
	d_list_t		 tx_gp_free;
	uint32_t		 tx_redo_act_nr;
	uint32_t		 tx_redo_payload_len;
	struct umem_act_item	*tx_redo_act_pos;
};

/** action parameters for free() */
struct ad_free_act {
	d_list_t		 fa_link;
	int			 fa_at;
	struct ad_group		*fa_group;
};

/** bitmap size of group */
#define GRP_UNIT_BMSZ		8
/** 32K is the minimum size of a group */
#define GRP_SIZE_SHIFT		(15)
#define GRP_SIZE_MIN		(1 << GRP_SIZE_SHIFT)

/** durable format of group (128 bytes) */
struct ad_group_df {
	/** base address */
	uint64_t		gd_addr;
	/** DRAM address for reserve() */
	uint64_t		gd_back_ptr;
	/** incarnation for validity check of gd_back_ptr */
	uint64_t		gd_incarnation;
	/** unit size in bytes, e.g., 64 bytes, 128 bytes */
	uint32_t		gd_unit;
	/** number of units in this group */
	int32_t			gd_unit_nr;
	/** number of free units in this group */
	int32_t			gd_unit_free;
	uint32_t		gd_pad32;
	uint64_t		gd_reserved[3];
	/** used bitmap, 512 units at most so it can fit into 128-byte */
	uint64_t		gd_bmap[GRP_UNIT_BMSZ];
};

/** DRAM format of group, it is referenced by ad_group_df::gd_back_ptr */
struct ad_group {
	struct ad_arena		*gp_arena;
	/** address of durable format */
	struct ad_group_df	*gp_df;
	/** unpublished group */
	bool			 gp_unpub;
	/** being published */
	bool			 gp_publishing;
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
#define ARENA_GRP_BMSZ		16

/** Customized specs for arena. */
struct ad_arena_spec {
	/** arena type, default arena type is 0 */
	uint32_t		as_type;
	/** arena unit size, reserved for future use */
	uint32_t		as_unit;
	/* last active arena of this type, this is not really part of spec... */
	uint32_t		as_arena_last;
	/** number of group specs (valid members of as_specs) */
	uint32_t		as_specs_nr;
	/** group sizes and number of units within each group */
	struct ad_group_spec	as_specs[ARENA_GRP_SPEC_MAX];
};

/** Default arena size is 16MB */
#define ARENA_SIZE_BITS		(24)
#define ARENA_SIZE_MASK		((1ULL << ARENA_SIZE_BITS) - 1)
#define ARENA_SIZE		(1ULL << ARENA_SIZE_BITS)

/** Arena header size, 32KB */
#define ARENA_HDR_SIZE		(32 << 10)
#define ARENA_UNIT_SIZE		(32 << 10)

/**
 * Maximum number of groups within an arena.
 * In order to Keep metadata overhead low (32K is 0.2% of default arena size (16MB)), no more
 * than 252 groups within an arena, otherwise please tune ad-hoc allocator specs,
 */
#define ARENA_GRP_MAX		252

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
	/** number of groups */
	int32_t			ad_grp_nr;
	/** internal offset for locating */
	int32_t			ad_pad32;
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
	/** 128 bytes (1024 bits) for each, each bit represents 32K(minimum group size) */
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
	/** arena type */
	int			  ar_type;
	/** refcount */
	int			  ar_ref;
	/** number of groups */
	int			  ar_grp_nr;
	/** unpublished arena */
	bool			  ar_unpub;
	/** being published */
	bool			  ar_publishing;
	/**
	 * Arena is full, it's set to true when any type of group failed to allocate memory
	 * and create more groups.
	 * XXX: this is not enough, we should save failed allocatoin counter in matrics and
	 * set arenea as full only if it encounters multiple failures.
	 */
	bool			  ar_full;
	/** pointers for size binary search, it is only used by the DRAM mirror*/
	struct ad_group_df	**ar_size_sorter;
	/** pointers for address binary search, it is only used by the DRAM mirror*/
	struct ad_group_df	**ar_addr_sorter;
	/** reserved bits for groups */
	uint64_t		  ar_bmap_rsv[ARENA_GRP_BMSZ];
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

#define BLOB_HDR_SIZE		(32 << 10)
#define BLOB_MAGIC		0xbabecafe

#define AD_MEM_VERSION		1

/** root data structure of ad-hoc allocator */
struct ad_blob_df {
	/** magic number */
	uint32_t		bd_magic;
	/** version number */
	uint32_t		bd_version;
	/** alignment */
	int32_t			bd_unused;
	/** number of registered arena types */
	int32_t			bd_asp_nr;
	/** specifications of registered arena types */
	struct ad_arena_spec	bd_asp[ARENA_SPEC_MAX];
	/** loading incarnation */
	uint64_t		bd_incarnation;
	/** it is DRAM reference of blob */
	uint64_t		bd_back_ptr;
	/** start address in the SPDK blob */
	uint64_t		bd_addr;
	/** capacity managed by the allocator */
	uint64_t		bd_size;
	/** size of each arena, default size is 16MB */
	uint64_t		bd_arena_size;
	/** allocated bits */
	uint64_t		bd_bmap[0];
};

#define DUMMY_BLOB	"dummy.blob"

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
	int			 bb_gps_lru_size;
	/** file descriptor of MD file */
	int			 bb_fd;
	/** reference counter */
	int			 bb_ref;
	/** is dummy blob, for unit test */
	bool			 bb_dummy;
	/** opened blob */
	bool			 bb_opened;
	/** number of pages */
	unsigned int		 bb_pgs_nr;
	/**
	 * XXX: last used arena ID, the current implementation chooses arena ID incrementally,
	 * because freeing arena is not supported yet. In the future, it should scan bitmap to
	 * choose ID for arena allocation.
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
	return blob->bb_store.stor_addr;
}

void blob_addref(struct ad_blob *blob);
void blob_decref(struct ad_blob *blob);
void *blob_addr2ptr(struct ad_blob *blob, daos_off_t addr);
daos_off_t blob_ptr2addr(struct ad_blob *blob, void *ptr);

int tx_complete(struct ad_tx *tx, int err);

#endif /* __AD_MEM_H__ */
