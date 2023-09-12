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

enum {
	VOF_DKEY	= 0x1,
	VOF_AKEY	= 0x2,
	VOF_SINGV	= 0x3,
	VOF_EXT		= 0x4,
};

#define VOF_KEY_INLINE_SZ	(4)
#define VOF_SINGV_INLINE_SZ	(8)

/** dkey/akey/value for VOS flattened object, 20B each */
struct vof_item_df {
	/** type (dkey/akey/sing-value/array-ext) */
	uint16_t				ofk_type;
	/** number of checksums */
	uint16_t				ofk_cs_nr;
	/** size of the item, for single-value is the local record size */
	uint32_t				ofk_size;
	union {
		/* dkey/akey */
		struct {
			union {
				/** offset of the key */
				uint32_t	ofk_key_off;
				/** inline key (for short key and no key csum) */
				uint8_t		ofk_key[VOF_KEY_INLINE_SZ];
			};
			/** offset of children (akey or value) */
			uint32_t		ofk_child_off;
			/** number of children */
			uint16_t		ofk_child_nr;
			/** type of children */
			uint16_t		ofk_child_type;
		};
		/* array extent */
		struct {
			/** index of the value extent */
			uint64_t		ofk_ext_idx;
			/** offset of value */
			uint32_t		ofk_ext_off;
		};
		/* single-value */
		struct {
			/* global record size of the single-value */
			uint32_t		ofk_singv_gsize;
			union {
				/** offset of the single-value */
				uint32_t		ofk_singv_off;
				/** inline data (for short single-value and no csum) */
				uint8_t			ofk_singv[VOF_SINGV_INLINE_SZ];
			};
		};
	};
} __attribute__ ((aligned(4)));

/** flattened object durable format */
struct vos_obj_flat_df {
	/** format version */
	uint16_t	ofd_version;
	uint16_t	ofd_pad;
	/** number of bytes per index(record) */
	uint32_t	ofd_inob;
	/** number of bytes used to generate each csum */
	uint32_t	ofd_csum_chunk_sz;
	/** type of the csum */
	uint16_t	ofd_csum_type;
	/** length of each csum in bytes */
	uint16_t	ofd_csum_len;
	/** flatten epoch */
	daos_epoch_t	ofd_flat_epoch;
	/** number of dkey */
	uint16_t	ofd_dkey_nr;
	/** number of vof_item_df in payload */
	uint16_t	ofd_item_nr;
	/** number of bytes of payload */
	uint32_t	ofd_len;
	/**
	 * payload's layout -
	 * struct vof_item_df[ofd_item_nr]
	 * followed by each key and value's content and checksums.
	 */
	uint8_t		ofd_payload[0];
} __attribute__ ((aligned(4)));

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
