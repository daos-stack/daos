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

/**
 * VOS metadata structure declarations
 * TODO: use df (durable format) as postfix of structures.
 *
 * opaque structure expanded inside implementation
 * Container table for holding container UUIDs
 * DAOS two-phase commit transaction table (DTX)
 * Object table for holding object IDs
 * B-Tree for Key Value stores
 * EV-Tree for Byte array stores
 */
struct vos_cont_table_df;
struct vos_cont_df;
struct vos_dtx_table_df;
struct vos_dtx_entry_df;
struct vos_dtx_record_df;
struct vos_obj_table_df;
struct vos_obj_df;
struct vos_krec_df;
struct vos_irec_df;

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
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cont_table_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cont_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_dtx_table_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_dtx_entry_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_dtx_record_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj_table_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_krec_df);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_irec_df);
POBJ_LAYOUT_END(vos_pool_layout);


/**
 * VOS container table
 * PMEM container index in each pool
 */
struct vos_cont_table_df {
	struct btr_root		ctb_btree;
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
	struct vos_cont_table_df		pd_ctab_df;
	/* Free space tracking for NVMe device */
	struct vea_space_df			pd_vea_df;
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
	umem_id_t			tr_record;
	/** The next vos_dtx_record_df for the same DTX. */
	umem_id_t			tr_next;
};

enum vos_dtx_entry_flags {
	/* The DTX contains exchange of some record(s). */
	DTX_EF_EXCHANGE_PENDING		= (1 << 0),
	/* The DTX shares something with other DTX(s). */
	DTX_EF_SHARES			= (1 << 1),
};

/**
 * Persisted DTX entry, it is referenced by btr_record::rec_mmid
 * of btree VOS_BTR_DTX_TABLE.
 */
struct vos_dtx_entry_df {
	/** The DTX identifier. */
	struct daos_tx_id		te_xid;
	/** The identifier of the modified object (shard). */
	daos_unit_oid_t			te_oid;
	/** The hashed dkey if applicable. */
	uint64_t			te_dkey_hash[2];
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
	/** The second timestamp when handles the transaction. */
	uint64_t			te_sec;
	/** The list of vos_dtx_record_df in SCM. */
	umem_id_t			te_records;
	/** The next committed DTX in global list. */
	umem_id_t			te_next;
	/** The prev committed DTX in global list. */
	umem_id_t			te_prev;
};

/**
 * DAOS two-phase commit transaction table.
 */
struct vos_dtx_table_df {
	/** The count of committed DTXs in the table. */
	uint64_t			tt_count;
	/** The time in second when last aggregate the DTXs. */
	uint64_t			tt_time_last_shrink;
	/** The list head of committed DTXs. */
	umem_id_t			tt_entry_head;
	/** The list tail of committed DTXs. */
	umem_id_t			tt_entry_tail;
	/** The root of the B+ tree for committed DTXs. */
	struct btr_root			tt_committed_btr;
	/** The root of the B+ tree for active (prepared) DTXs. */
	struct btr_root			tt_active_btr;
};

/**
 * VOS object table
 * It is just a in-place btree for the time being.
 */
struct vos_obj_table_df {
	struct btr_root			obt_btr;
};

/* VOS Container Value */
struct vos_cont_df {
	uuid_t				cd_id;
	uint64_t			cd_nobjs;
	daos_size_t			cd_used;
	daos_epoch_t			cd_hae;
	struct vos_obj_table_df		cd_otab_df;
	/** The DTXs table. */
	struct vos_dtx_table_df		cd_dtx_table_df;
	/*
	 * Allocation hint for block allocator, it can be turned into
	 * a hint vector when we need to support multiple active epochs.
	 */
	struct vea_hint_df		cd_hint_df;
};

/** btree (d/a-key) record bit flags */
enum vos_krec_bf {
	/* The record has an evtree */
	KREC_BF_EVT			= (1 << 0),
	/* The key is punched at time kr_latest */
	KREC_BF_PUNCHED			= (1 << 1),
	/* The key has been (or will be) removed */
	KREC_BF_REMOVED			= (1 << 2),
};

/**
 * Persisted VOS (d/a)key record, it is referenced by btr_record::rec_mmid
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
	/* Latest known update timestamp or punched timestamp */
	daos_epoch_t			kr_latest;
	/* Earliest known modification timestamp */
	daos_epoch_t			kr_earliest;
	/** The DTX entry in SCM. */
	umem_id_t			kr_dtx;
	/** The count of uncommitted DTXs that share the key. */
	uint32_t			kr_dtx_shares;
	/** For 64-bits alignment. */
	uint32_t			kr_padding;
	/** btree root under the key */
	struct btr_root			kr_btr;
	/** evtree root, which is only used by akey */
	struct evt_root			kr_evt[0];
	/* Checksum and key are stored after tree root */
};

/* Assumptions made about relative placement of these fields so
 * assert that they are true
 */
D_CASSERT(offsetof(struct vos_krec_df, kr_earliest) ==
	  offsetof(struct vos_krec_df, kr_latest) +
	  sizeof(((struct vos_krec_df *)0)->kr_latest));

/**
 * Persisted VOS single value & epoch record, it is referenced by
 * btr_record::rec_mmid of btree VOS_BTR_SINGV.
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
	umem_id_t			ir_dtx;
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
	/** Attributes of object.  See vos_oi_attr */
	uint64_t			vo_oi_attr;
	/** Latest known update timestamp or punched timestamp */
	daos_epoch_t			vo_latest;
	/** Earliest known update timestamp */
	daos_epoch_t			vo_earliest;
	/** Incarnation of the object, it's increased each time it's punched. */
	uint64_t			vo_incarnation;
	/** The DTX entry in SCM. */
	umem_id_t			vo_dtx;
	/** The count of uncommitted DTXs that share the object. */
	uint32_t			vo_dtx_shares;
	/** For 64-bits alignment. */
	uint32_t			vo_padding;
	/** VOS object btree root */
	struct btr_root			vo_tree;
};

/* Assumptions made about relative placement of these fields so
 * assert that they are true
 */
D_CASSERT(offsetof(struct vos_obj_df, vo_earliest) ==
	  offsetof(struct vos_obj_df, vo_latest) +
	  sizeof(((struct vos_obj_df *)0)->vo_latest));
#endif
