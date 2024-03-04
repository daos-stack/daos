/**
 * (C) Copyright 2023-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Object flatten related API and structures
 * src/vos/vos_obj_flat.h
 */

#ifndef __VOS_OBJ_FLAT_H__
#define __VOS_OBJ_FLAT_H__

#include "vos_layout.h"

#define VOF_VERSION		(0)

#define VOF_KEY_INLINE_SZ	(12)
#define VOF_VAL_INLINE_SZ	(8)
#define VOF_SIZE_ROUND		(8)

enum {
	VOF_NONE	= 0x0,
	VOF_DKEY	= 0x1,
	VOF_AKEY	= 0x2,
	VOF_SINGV	= 0x3,
	VOF_EXT		= 0x4,
};

/** dkey/akey/value for VOS flattened object, 32B each */
struct vof_item_df {
	/** type (dkey/akey/sing-value/array-ext) */
	uint16_t				vi_type;
	uint16_t				vi_pad;
	/** size of the item, for single-value is the local record size */
	uint32_t				vi_size;
	union {
		/* dkey/akey */
		struct {
			/** index of first child (akey or value) */
			uint32_t		vi_child_idx;
			/** number of children */
			uint16_t		vi_child_nr;
			/** type of children */
			uint16_t		vi_child_type;
			/** number of bytes per index (only valid for array akey) */
			uint32_t		vi_inob;
			union {
				/** offset of the key in struct vos_obj_flat_df::ofd_payload */
				uint32_t	vi_key_off;
				/** inline key (for short key) */
				uint8_t		vi_key[VOF_KEY_INLINE_SZ];
			};
		};
		/* array extend or single-value */
		struct {
			union {
				/** index of the value extent */
				uint64_t	vi_ext_idx;
				/* global record size of the single-value */
				uint64_t	vi_singv_gsize;
			};
			/** pool map version */
			uint32_t		vi_ver;
			/* DAOS_MEDIA_SCM or DAOS_MEDIA_NVME */
			uint8_t			vi_media_type;
			uint8_t			vi_pad2;
			/* bio_addr_t::ba_flags, see BIO_FLAG enum */
			uint16_t		vi_bio_flags;
			/* For DAOS_MEDIA_SCM type, either stored in struct vos_obj_flat_df's
			 * ofd_payload, or inline in struct vof_item_df::vi_val.
			 * For DAOS_MEDIA_NVME type, store external NVME address.
			 */
			union {
				/* external byte offset within SPDK blob for NVMe */
				uint64_t	vi_ex_addr;
				/* value's offset in struct vos_obj_flat_df::ofd_payload */
				uint32_t	vi_val_off;
				/* inline data for very short value */
				uint8_t		vi_val[VOF_VAL_INLINE_SZ];
			};
		};
	};
};

/** flattened object durable format */
struct vos_obj_flat_df {
	/** format version */
	uint16_t	ofd_version;
	/** type of the csum */
	uint16_t	ofd_csum_type;
	/** length of each csum in bytes */
	uint16_t	ofd_csum_len;
	/** number of checksums */
	uint16_t	ofd_csum_nr;
	/** number of bytes used to generate csum */
	uint32_t	ofd_csum_chunk_sz;
	/** number of dkey */
	uint16_t	ofd_dkey_nr;
	/** number of vof_item_df in payload */
	uint16_t	ofd_item_nr;
	/** aggregated epoch (highest epoch of the object's values */
	daos_epoch_t	ofd_epoch;
	/** number of bytes of payload */
	uint32_t	ofd_len;
	/** 4B padding, reserved for future usage */
	uint32_t	ofd_pad;
	/**
	 * payload's layout -
	 * struct vof_item_df[ofd_item_nr]
	 * each key and value's content (each key/value compactly follow each other, start address
	 * need not align, total length round up to VOF_SIZE_ROUND bytes)
	 * checksums for all above content (ofd_csum_len * ofd_csum_nr bytes)
	 */
	uint8_t		ofd_payload[0];
};

struct vof_item;

/** data structure to help binary search */
struct vof_sorter {
	/** type (dkey/akey/sing-value/array-ext) */
	uint16_t		  vs_type;
	/** number of items */
	uint16_t		  vs_nr;
	/** pointer array for binary search */
	struct vof_item		**vs_items;
};

/** dkey/akey/value in memory structure */
struct vof_item {
	/** durable format in memory address */
	struct vof_item_df	*vid_df;
	/** sorter for child binary search */
	struct vof_sorter	 vid_child_sorter;
};

/** flattened object in memory structure */
struct vos_obj_flat {
	struct vos_obj_flat_df		*vof_df;
	struct vof_item			*vof_items;
	struct vof_sorter		 vof_dkey_sorter;
	uint16_t			 vof_nr;
};

#endif
