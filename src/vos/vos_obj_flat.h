/**
 * (C) Copyright 2023-2024 Intel Corporation.
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

#define VOF_KEY_IDX_NONE	((uint32_t)-1)

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

static inline bool
vos_obj_flattened(struct vos_obj_df *obj_df)
{
	return obj_df->vo_sync == DAOS_EPOCH_MAX;
}

struct vos_object;
struct vos_svt_key;
struct vos_rec_bundle;
struct evt_filter;
struct evt_entry_array;
void vof_init(struct vos_object *obj);
int vof_fetch_single(struct vos_object *obj, daos_key_t *dkey, daos_key_t *akey,
		     struct vos_svt_key *key, struct vos_rec_bundle *rbund, uint32_t *dkey_idx,
		     uint32_t *akey_idx);
int vof_fetch_array(struct vos_object *obj, daos_key_t *dkey, daos_key_t *akey,
		    const struct evt_filter *filter, struct evt_entry_array *ent_array,
		    uint32_t *dkey_idx, uint32_t *akey_idx);
int vof_dkey_exist(struct vos_object *obj, daos_key_t *dkey, uint32_t *dkey_idx,
		   daos_epoch_range_t *epr);
int vof_akey_exist(struct vos_object *obj, daos_key_t *dkey, daos_key_t *akey, uint32_t *dkey_idx,
		   uint32_t *akey_idx, daos_epoch_range_t *epr);

#endif
