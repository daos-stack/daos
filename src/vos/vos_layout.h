/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Layout definition for VOS root object
 * vos/vos_layout.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef _VOS_LAYOUT_H
#define _VOS_LAYOUT_H
#include <daos/btree.h>
#include <daos_srv/evtree.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/bio.h>
#include <daos_srv/vea.h>
#include <daos_srv/dtx_srv.h>
#include "ilog.h"

/** Layout name for vos pool */
#define VOS_POOL_LAYOUT         "vos_pool_layout"

struct vos_gc_bin_df {
	/** address of the first(oldest) bag */
	umem_off_t		bin_bag_first;
	/** address of the last(newest) bag */
	umem_off_t		bin_bag_last;
	/** max bag size in this bin */
	uint16_t		bin_bag_size;
	/** total number of bags within this bin */
	uint16_t		bin_bag_nr;
	/**
	 * reserved: max number of bags within this bin.
	 * TODO: we should set a limit for number of bags per bin, and start
	 * to eagerly run GC and free spaces if there are too many bags and
	 * queued items.
	 */
	uint16_t		bin_bag_max;
	/** reserved */
	uint16_t		bin_pad16;
};

struct vos_gc_bag_df {
	/** index of the first item in FIFO */
	uint16_t		bag_item_first;
	/** index of the last item in FIFO */
	uint16_t		bag_item_last;
	/** number of queued items in FIFO */
	uint16_t		bag_item_nr;
	/** reserved */
	uint16_t		bag_pad16;
	/** next GC bag chained on vos_gc_bin_df */
	umem_off_t		bag_next;
	struct vos_gc_item {
		/* address of the item to be freed */
		umem_off_t		it_addr;
		/** Reserved, argument for GC_VEA/BIO (e.g. size of extent) */
		uint64_t		it_args;
	}			bag_items[0];
};

enum vos_gc_type {
	/* XXX: we could define GC_VEA, which can free NVMe/SCM space.
	 * So svt_rec_free() and evt_desc_bio_free() only need to call
	 * gc_add_item() to register BIO address for GC.
	 *
	 * However, GC_VEA could have extra overhead of reassigning SCM
	 * pointers, but it also has low latency for undo changes.
	 */
	GC_AKEY,
	GC_DKEY,
	GC_OBJ,
	GC_CONT,
	GC_MAX,
};

#define POOL_DF_MAGIC				0x5ca1ab1e

/** Lowest supported durable format version */
#define POOL_DF_VER_1				23

/** Individual version specific featuers are assigned to a release specific durable
 * format version number.  This allows us to add multiple features in a release cycle
 * while keeping checks related to the feature rather than the more ambiguous version
 * number.   Each new feature should be assigned to the latest VOS durable format.
 * Each feature is only enabled if the pool durable format is at least equal to that
 * feature's assigned durable format.  Otherwise, the feature must not be used.
 */

/** Current durable format version */
#define POOL_DF_VERSION                         VOS_POOL_DF_2_4

/** 2.2 features */
#define VOS_POOL_FEAT_2_2                       (VOS_POOL_FEAT_AGG_OPT)

/** 2.4 features */
#define VOS_POOL_FEAT_2_4                       (VOS_POOL_FEAT_CHK | VOS_POOL_FEAT_DYN_ROOT)

/**
 * Durable format for VOS pool
 */
struct vos_pool_df {
	/** Structs stored in LE or BE representation */
	uint32_t				pd_magic;
	/** durable-format version */
	uint32_t				pd_version;
	/** reserved: flags for compatibility features */
	uint64_t				pd_compat_flags;
	/** reserved: flags for incompatibility features */
	uint64_t				pd_incompat_flags;
	/**
	 * Reserved for durable format update, e.g. convert vos_cont_df to
	 * a new format, containers with old format can be attached at here.
	 */
	uint64_t				pd_reserv_upgrade;
	/** Reserved for future usage */
	uint64_t				pd_reserv;
	/** Unique PoolID for each VOS pool assigned on creation */
	uuid_t					pd_id;
	/** Total space in bytes on SCM */
	uint64_t				pd_scm_sz;
	/** Total space in bytes on NVMe */
	uint64_t				pd_nvme_sz;
	/** # of containers in this pool */
	uint64_t				pd_cont_nr;
	/** offset for the btree of the dedup table (placeholder) */
	umem_off_t				pd_dedup;
	/** Typed PMEMoid pointer for the container index table */
	struct btr_root				pd_cont_root;
	/** Free space tracking for NVMe device */
	struct vea_space_df			pd_vea_df;
	/** GC bins for container/object/dkey... */
	struct vos_gc_bin_df			pd_gc_bins[GC_MAX];
};

/**
 * A DTX record is the object, {a,d}key, single-value or
 * array value that is changed in the transaction (DTX).
 */
enum vos_dtx_record_types {
	DTX_RT_ILOG	= 1,
	DTX_RT_SVT	= 2,
	DTX_RT_EVT	= 3,
};

#define DTX_INLINE_REC_CNT	4

/** Committed DTX entry on-disk layout in both SCM and DRAM. */
struct vos_dtx_cmt_ent_df {
	/** The DTX identifier. */
	struct dtx_id			dce_xid;
	/** The epoch# for the DTX. */
	daos_epoch_t			dce_epoch;
	/**
	 * The time of the DTX being committed on the server.
	 *
	 * XXX: in the future, this field will be moved into
	 *	vos_dtx_blob_df to shrink each committed DTX
	 *	entry size.
	 */
	uint64_t			dce_cmt_time;
};

/** Active DTX entry on-disk layout in both SCM and DRAM. */
struct vos_dtx_act_ent_df {
	/** The DTX identifier. */
	struct dtx_id			dae_xid;
	/** The epoch# for the DTX. */
	daos_epoch_t			dae_epoch;
	/** The identifier of the modified object (shard). */
	daos_unit_oid_t			dae_oid;
	/** The hashed dkey if applicable. */
	uint64_t			dae_dkey_hash;
	/** The allocated local id for the DTX entry */
	uint32_t			dae_lid;
	/** DTX flags, see enum dtx_entry_flags. */
	uint16_t			dae_flags;
	/** DTX flags, see enum dtx_mbs_flags. */
	uint16_t			dae_mbs_flags;
	/** The inlined dtx records. */
	umem_off_t			dae_rec_inline[DTX_INLINE_REC_CNT];
	/** The DTX records count, including inline case. */
	uint32_t			dae_rec_cnt;
	/** For 64-bits alignment. */
	uint32_t			dae_ver;
	/** The offset for the list of dtx records if out of inline. */
	umem_off_t			dae_rec_off;
	/** The DTX targets count, either only inline case or all not inline. */
	uint32_t			dae_tgt_cnt;
	/** The DTX modification groups count. */
	uint32_t			dae_grp_cnt;
	/** Size of the area for dae_mbs_off. */
	uint32_t			dae_mbs_dsize;
	/** The index in the current vos_dtx_blob_df. */
	int32_t				dae_index;
	/**
	 * The inline DTX targets, can hold 3-way replicas for single
	 * RDG that does not contains the original leader information.
	 */
	struct dtx_daos_target		dae_mbs_inline[2];
	/** The offset for the dtx mbs if out of inline. */
	umem_off_t			dae_mbs_off;
};

struct vos_dtx_blob_df {
	/** Magic number, can be used to distinguish active or committed DTX. */
	int					dbd_magic;
	/** The total (filled + free) slots in the blob. */
	int					dbd_cap;
	/** Already filled slots count. */
	int					dbd_count;
	/** The next available slot for active DTX entry in the blob. */
	int					dbd_index;
	/** Prev dtx_scm_blob. */
	umem_off_t				dbd_prev;
	/** Next dtx_scm_blob. */
	umem_off_t				dbd_next;
	/** Append only DTX entries in the blob. */
	union {
		struct vos_dtx_act_ent_df	dbd_active_data[0];
		struct vos_dtx_cmt_ent_df	dbd_committed_data[0];
	};
};

/* Assume dbd_index is next to dbd_count. */
D_CASSERT(offsetof(struct vos_dtx_blob_df, dbd_index) ==
	  offsetof(struct vos_dtx_blob_df, dbd_count) +
	  sizeof(((struct vos_dtx_blob_df *)0)->dbd_count));

enum vos_io_stream {
	/**
	 * I/O stream for generic purpose, like client updates, updates
	 * initiated for rebuild , reintegration or rebalance.
	 */
	VOS_IOS_GENERIC		= 0,
	/** I/O stream for extents coalescing, like aggregation. */
	VOS_IOS_AGGREGATION,
	VOS_IOS_CNT
};

/* VOS Container Value */
struct vos_cont_df {
	uuid_t				cd_id;
	uint64_t			cd_nobjs;
	uint32_t			cd_ts_idx;
	uint32_t			cd_pad;
	daos_size_t			cd_used;
	daos_epoch_t			cd_hae;
	struct btr_root			cd_obj_root;
	/** reserved for placement algorithm upgrade */
	uint64_t			cd_reserv_upgrade;
	/** reserved for future usage */
	uint64_t			cd_reserv;
	/** The active DTXs blob head. */
	umem_off_t			cd_dtx_active_head;
	/** The active DTXs blob tail. */
	umem_off_t			cd_dtx_active_tail;
	/** The committed DTXs blob head. */
	umem_off_t			cd_dtx_committed_head;
	/** The committed DTXs blob tail. */
	umem_off_t			cd_dtx_committed_tail;
	/** Allocation hints for block allocator. */
	struct vea_hint_df		cd_hint_df[VOS_IOS_CNT];
	/** GC bins for object/dkey...Don't need GC_CONT entry */
	struct vos_gc_bin_df		cd_gc_bins[GC_CONT];
	/* The epoch for the most new DTX entry that is aggregated. */
	uint64_t			cd_newest_aggregated;
};

/* Assume cd_dtx_active_tail is just after cd_dtx_active_head. */
D_CASSERT(offsetof(struct vos_cont_df, cd_dtx_active_tail) ==
	  offsetof(struct vos_cont_df, cd_dtx_active_head) +
	  sizeof(((struct vos_cont_df *)0)->cd_dtx_active_head));

/* Assume cd_dtx_committed_tail is just after cd_dtx_committed_head. */
D_CASSERT(offsetof(struct vos_cont_df, cd_dtx_committed_tail) ==
	  offsetof(struct vos_cont_df, cd_dtx_committed_head) +
	  sizeof(((struct vos_cont_df *)0)->cd_dtx_committed_head));

/** btree (d/a-key) record bit flags */
enum vos_krec_bf {
	/* Array value (evtree) */
	KREC_BF_EVT			= (1 << 0),
	/* Single Value or Key (btree) */
	KREC_BF_BTR			= (1 << 1),
	/* it's a dkey, otherwise is akey */
	KREC_BF_DKEY			= (1 << 2),
};

/**
 * Persisted VOS (d/a)key record, it is referenced by btr_record::rec_off
 * of btree VOS_BTR_DKEY/VOS_BTR_AKEY.
 */
struct vos_krec_df {
	/** record bitmap, e.g. has evtree, see vos_krec_bf */
	uint8_t				kr_bmap;
	/** checksum type */
	uint8_t				kr_cs_type;
	/** key checksum size (in bytes) */
	uint8_t				kr_cs_size;
	/** padding bytes */
	uint8_t				kr_pad_8;
	/** key length */
	uint32_t			kr_size;
	/** Incarnation log for key */
	struct ilog_df			kr_ilog;
	union {
		struct {
			/** btree root under the key */
			struct btr_root			kr_btr;
			/** Offset of known existing akey */
			umem_off_t			kr_known_akey;
		};
		/** evtree root, which is only used by akey */
		struct evt_root			kr_evt;
	};
	/* Checksum and key are stored after tree root */
};

/**
 * Persisted VOS single value & epoch record, it is referenced by
 * btr_record::rec_off of btree VOS_BTR_SINGV.
 */
struct vos_irec_df {
	/** key checksum size (in bytes) */
	uint16_t			ir_cs_size;
	/** key checksum type */
	uint8_t				ir_cs_type;
	/** padding bytes */
	uint8_t				ir_pad8;
	/** pool map version */
	uint32_t			ir_ver;
	/** The DTX entry in SCM. */
	uint32_t			ir_dtx;
	/** Minor epoch */
	uint16_t			ir_minor_epc;
	/** padding bytes */
	uint16_t			ir_pad16;
	/** length of value */
	uint64_t			ir_size;
	/**
	 * global length of value, it is needed for single value of EC object
	 * class that the data will be distributed to multiple data cells.
	 */
	uint64_t			ir_gsize;
	/** external payload address */
	bio_addr_t			ir_ex_addr;
	/** placeholder for the key checksum & internal value */
	char				ir_body[0];
};

/**
 * VOS object, assume all objects are KV store...
 * NB: PMEM data structure.
 */
struct vos_obj_df {
	daos_unit_oid_t			vo_id;
	/** The latest sync epoch */
	daos_epoch_t			vo_sync;
	/** Offset of known existing dkey */
	umem_off_t			vo_known_dkey;
	/** Attributes for future use */
	daos_epoch_t			vo_max_write;
	/** Incarnation log for the object */
	struct ilog_df			vo_ilog;
	/** VOS dkey btree root */
	struct btr_root			vo_tree;
};

#endif
