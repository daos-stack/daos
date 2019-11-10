/**
 * (C) Copyright 2016-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * Layout definition for VOS root object
 * vos/vos_layout.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef _VOS_LAYOUT_H
#define _VOS_LAYOUT_H
#include <libpmemobj.h>
#include <daos/btree.h>
#include <daos_srv/evtree.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/bio.h>
#include <daos_srv/vea.h>
#include <daos_srv/dtx_srv.h>
#include "ilog.h"

/**
 * VOS metadata structure declarations
 * TODO: use df (durable format) as postfix of structures.
 *
 * opaque structure expanded inside implementation
 * DAOS two-phase commit transaction table (DTX)
 * Object table for holding object IDs
 * B-Tree for Key Value stores
 * EV-Tree for Byte array stores
 * Garbage collection bin
 * Garbage collection bag
 */
struct vos_cont_df;
struct vos_dtx_act_ent_df;
struct vos_dtx_cmt_ent_df;
struct vos_dtx_record_df;
struct vos_obj_df;
struct vos_krec_df;
struct vos_irec_df;
struct vos_gc_bin_df;
struct vos_gc_bag_df;

/**
 * Typed Layout named using Macros from libpmemobj
 * Each structure is assigned a type number internally
 * in the macro libpmemobj has its own pointer type (PMEMoid)
 * and uses named unions to assign types to such pointers.
 * Type-number help identify the PMEMoid  pointer types
 * with an ID. This layout structure is used to consicely
 * represent the types associated with the vos_pool_layout.
 * In this case consecutive typeIDs are assigned to the
 * different pointers in the pool.
 */
POBJ_LAYOUT_BEGIN(vos_pool_layout);

POBJ_LAYOUT_ROOT(vos_pool_layout, struct vos_pool_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cont_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_dtx_record_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_krec_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_irec_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_gc_bin_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_gc_bag_df);

POBJ_LAYOUT_END(vos_pool_layout);

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

/**
 * VOS Pool root object
 */
struct vos_pool_df {
	/* Structs stored in LE or BE representation */
	uint32_t				pd_magic;
	/* Unique PoolID for each VOS pool assigned on creation */
	uuid_t					pd_id;
	/* Flags for compatibility features */
	uint64_t				pd_compat_flags;
	/* Flags for incompatibility features */
	uint64_t				pd_incompat_flags;
	/* Total space in bytes on SCM */
	uint64_t				pd_scm_sz;
	/* Total space in bytes on NVMe */
	uint64_t				pd_nvme_sz;
	/* # of containers in this pool */
	uint64_t				pd_cont_nr;
	/* Typed PMEMoid pointer for the container index table */
	struct btr_root				pd_cont_root;
	/* Free space tracking for NVMe device */
	struct vea_space_df			pd_vea_df;
	struct vos_gc_bin_df			pd_gc_bins[GC_MAX];
};

/**
 * A DTX record is the object, {a,d}key, single-value or
 * array value that is changed in the transaction (DTX).
 */
enum vos_dtx_record_types {
	DTX_RT_OBJ	= 1,
	DTX_RT_KEY	= 2,
	DTX_RT_SVT	= 3,
	DTX_RT_EVT	= 4,
	DTX_RT_ILOG	= 5,
};

enum vos_dtx_record_flags {
	/* We make some special handling for the punch of object/key.
	 * The basic idea is that:
	 *
	 * Firstly, we insert a new record for the target (object/key)
	 * to be punched with the given epoch.
	 *
	 * Then exchange the sub-tree(s) under the original record that
	 * has DAOS_EPOCH_MAX for the target to be punched and the new
	 * created record.
	 *
	 * And then remove the original record that has DAOS_EPOCH_MAX
	 * for the target to be punched.
	 */

	/* The source of exchange the record that has the DAOS_EPOCH_MAX. */
	DTX_RF_EXCHANGE_SRC	= 1,

	/* The target of exchange the record that has the given epoch. */
	DTX_RF_EXCHANGE_TGT	= 2,
};

enum vos_dtx_entry_flags {
	/* The DTX contains exchange of some record(s). */
	DTX_EF_EXCHANGE_PENDING		= (1 << 0),
	/* The DTX shares something with other DTX(s). */
	DTX_EF_SHARES			= (1 << 1),
	/* The DTX is the leader */
	DTX_EF_LEADER			= (1 << 2),
	/* The DTX entry is invalid. */
	DTX_EF_INVALID			= (1 << 3),
};

/** The agent of the record being modified via the DTX in both SCM and DRAM. */
struct vos_dtx_record_df {
	/** The DTX record type, see enum vos_dtx_record_types. */
	uint32_t			dr_type;
	/** The DTX record flags, see enum vos_dtx_record_flags. */
	uint32_t			dr_flags;
	/** The modified record in the related tree in SCM. */
	umem_off_t			dr_record;
};

#define DTX_INLINE_REC_CNT	4
#define DTX_REC_CAP_DEFAULT	4


/** Active DTX entry on-disk layout in both SCM and DRAM. */
struct vos_dtx_act_ent_df {
	/** The DTX identifier. */
	struct dtx_id			dae_xid;
	/** The identifier of the modified object (shard). */
	daos_unit_oid_t			dae_oid;
	/** The hashed dkey if applicable. */
	uint64_t			dae_dkey_hash;
	/** The epoch# for the DTX. */
	daos_epoch_t			dae_epoch;
	/** The server generation when handles the DTX. */
	uint64_t			dae_srv_gen;
	/** The active DTX entry on-disk layout generation. */
	uint64_t			dae_layout_gen;
	/** The intent of related modification. */
	uint32_t			dae_intent;
	/** The index in the current vos_dtx_scm_blob. */
	uint32_t			dae_index;
	/** The inlined dtx records. */
	struct vos_dtx_record_df	dae_rec_inline[DTX_INLINE_REC_CNT];
	/** DTX flags, see enum vos_dtx_entry_flags. */
	uint32_t			dae_flags;
	/** The DTX records count, including inline case. */
	uint32_t			dae_rec_cnt;
	/** The offset for the list of vos_dtx_record_df if out of inline. */
	umem_off_t			dae_rec_off;
};

/* Assume dae_rec_cnt is next to dae_flags. */
D_CASSERT(offsetof(struct vos_dtx_act_ent_df, dae_rec_cnt) ==
	  offsetof(struct vos_dtx_act_ent_df, dae_flags) +
	  sizeof(((struct vos_dtx_act_ent_df *)0)->dae_flags));

/* Assume dae_rec_off is next to dae_rec_cnt. */
D_CASSERT(offsetof(struct vos_dtx_act_ent_df, dae_rec_off) ==
	  offsetof(struct vos_dtx_act_ent_df, dae_rec_cnt) +
	  sizeof(((struct vos_dtx_act_ent_df *)0)->dae_rec_cnt));

/** Committed DTX entry on-disk layout in both SCM and DRAM. */
struct vos_dtx_cmt_ent_df {
	struct dtx_id			dce_xid;
	daos_epoch_t			dce_epoch;
};

struct vos_dtx_scm_blob {
	/** Magic number, can be used to distinguish active or committed DTX. */
	int					dsb_magic;
	/** The total (filled + free) slots in the blob. */
	int					dsb_cap;
	/** Already filled slots count. */
	int					dsb_count;
	/** The next available slot for active DTX entry in the blob. */
	int					dsb_index;
	/** Prev dtx_scm_blob. */
	umem_off_t				dsb_prev;
	/** Next dtx_scm_blob. */
	umem_off_t				dsb_next;
	/** Append only DTX entries in the blob. */
	union {
		struct vos_dtx_act_ent_df	dsb_active_data[0];
		struct vos_dtx_cmt_ent_df	dsb_commmitted_data[0];
	};
};

/* Assume dsb_index is next to dsb_count. */
D_CASSERT(offsetof(struct vos_dtx_scm_blob, dsb_index) ==
	  offsetof(struct vos_dtx_scm_blob, dsb_count) +
	  sizeof(((struct vos_dtx_scm_blob *)0)->dsb_count));

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
	uint64_t			cd_dtx_resync_gen;
	daos_size_t			cd_used;
	daos_epoch_t			cd_hae;
	struct btr_root			cd_obj_root;
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
		/** btree root under the key */
		struct btr_root			kr_btr;
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
	umem_off_t			ir_dtx;
	/** length of value */
	uint64_t			ir_size;
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
	/** Attributes of object.  See vos_oi_attr */
	uint64_t			vo_oi_attr;
	/** Latest known update timestamp or punched timestamp */
	daos_epoch_t			vo_latest;
	/** Earliest known update timestamp */
	daos_epoch_t			vo_earliest;
	/** The DTX entry in SCM. */
	umem_off_t			vo_dtx;
	/** The count of uncommitted DTXs that share the object. */
	uint32_t			vo_dtx_shares;
	/** Incarnation of the object, it's increased each time it's punched. */
	uint32_t			vo_incarnation;
	/** VOS dkey btree root */
	struct btr_root			vo_tree;
};

/* Assumptions made about relative placement of these fields so
 * assert that they are true
 */
D_CASSERT(offsetof(struct vos_obj_df, vo_earliest) ==
	  offsetof(struct vos_obj_df, vo_latest) +
	  sizeof(((struct vos_obj_df *)0)->vo_latest));

D_CASSERT(offsetof(struct vos_obj_df, vo_dtx_shares) ==
	  offsetof(struct vos_obj_df, vo_dtx) +
	  sizeof(((struct vos_obj_df *)0)->vo_dtx));

#define VOS_OBJ_DTX_SIZE	sizeof(((struct vos_obj_df *)0)->vo_dtx)
#define VOS_OBJ_SHARES_SIZE	sizeof(((struct vos_obj_df *)0)->vo_dtx_shares)
#define VOS_OBJ_SIZE_PARTIAL	(VOS_OBJ_DTX_SIZE + VOS_OBJ_SHARES_SIZE)

#endif
