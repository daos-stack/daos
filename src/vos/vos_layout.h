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
struct vos_dtx_table_df;
struct vos_dtx_entry_df;
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
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_dtx_table_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_dtx_entry_df);
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
	DTX_RT_ILOG	= 1,
	DTX_RT_SVT	= 2,
	DTX_RT_EVT	= 3,
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

/**
 * The agent of the record being modified via the DTX.
 */
struct vos_dtx_record_df {
	/** The DTX record type, see enum vos_dtx_record_types. */
	uint32_t			tr_type;
	/** The DTX record flags, see enum vos_dtx_record_flags. */
	uint32_t			tr_flags;
	/** The record in the related tree in SCM. */
	umem_off_t			tr_record;
	/** The next vos_dtx_record_df for the same DTX. */
	umem_off_t			tr_next;
};

enum vos_dtx_entry_flags {
	/* The DTX shares something with other DTX(s). */
	DTX_EF_SHARES			= (1 << 0),
	/* The DTX is the leader */
	DTX_EF_LEADER			= (1 << 1),
};

/**
 * Persisted DTX entry, it is referenced by btr_record::rec_off
 * of btree VOS_BTR_DTX_TABLE.
 */
struct vos_dtx_entry_df {
	/** The DTX identifier. */
	struct dtx_id			te_xid;
	/** The identifier of the modified object (shard). */
	daos_unit_oid_t			te_oid;
	/** The hashed dkey if applicable. */
	uint64_t			te_dkey_hash;
	/** The epoch# for the DTX. */
	daos_epoch_t			te_epoch;
	/** Pool map version. */
	uint32_t			te_ver;
	/** DTX status, see enum dtx_status. */
	uint32_t			te_state;
	/** DTX flags, see enum vos_dtx_entry_flags. */
	uint32_t			te_flags;
	/** The intent of related modification. */
	uint32_t			te_intent;
	/** The timestamp when handles the transaction. */
	uint64_t			te_time;
	/** The list of vos_dtx_record_df in SCM. */
	umem_off_t			te_records;
	/** The next committed DTX in global list. */
	umem_off_t			te_next;
	/** The prev committed DTX in global list. */
	umem_off_t			te_prev;
};

/**
 * DAOS two-phase commit transaction table.
 */
struct vos_dtx_table_df {
	/** The count of committed DTXs in the table. */
	uint64_t			tt_count;
	/** The list head of committed DTXs. */
	umem_off_t			tt_entry_head;
	/** The list tail of committed DTXs. */
	umem_off_t			tt_entry_tail;
	/** The root of the B+ tree for committed DTXs. */
	struct btr_root			tt_committed_btr;
	/** The root of the B+ tree for active (prepared) DTXs. */
	struct btr_root			tt_active_btr;
};

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
	daos_size_t			cd_used;
	daos_epoch_t			cd_hae;
	struct btr_root			cd_obj_root;
	/** The DTXs table. */
	struct vos_dtx_table_df		cd_dtx_table_df;
	/** Allocation hints for block allocator. */
	struct vea_hint_df		cd_hint_df[VOS_IOS_CNT];
};

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
	/** The DTX entry in SCM. */
	umem_off_t			kr_dtx;
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
	/** Incarnation log for the object */
	struct ilog_df			vo_ilog;
	/** The DTX entry in SCM. */
	umem_off_t			vo_dtx;
	/** VOS dkey btree root */
	struct btr_root			vo_tree;
};

#endif
