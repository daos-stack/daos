/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * VOS Object/Key incarnation log
 * vos/ilog_internal.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __ILOG_INTERNAL_H__
#define __ILOG_INTERNAL_H__

/* 4 bit magic number + version */
#define ILOG_MAGIC              0x00000006
#define ILOG_MAGIC_BITS         4
#define ILOG_MAGIC_MASK         ((1 << ILOG_MAGIC_BITS) - 1)
#define ILOG_VERSION_INC        (1 << ILOG_MAGIC_BITS)
#define ILOG_VERSION_MASK       ~(ILOG_VERSION_INC - 1)
#define ILOG_MAGIC_VALID(magic) (((magic)&ILOG_MAGIC_MASK) == ILOG_MAGIC)

/** The ilog is split into two parts.   If there is one entry, the ilog
 *  is embedded into the root df struct.   If not, a b+tree is used.
 *  The tree is used more like a set where only the key is used.
 */

struct ilog_tree {
	umem_off_t it_root;
	uint64_t   it_embedded;
};

struct ilog_root {
	union {
		struct ilog_id   lr_id;
		struct ilog_tree lr_tree;
	};
	uint32_t lr_ts_idx;
	uint32_t lr_magic;
};

static inline bool
ilog_empty(struct ilog_root *root)
{
	return !root->lr_tree.it_embedded && root->lr_tree.it_root == UMOFF_NULL;
}

struct ilog_array {
	/** Current length of array */
	uint32_t       ia_len;
	/** Allocated length of array */
	uint32_t       ia_max_len;
	/** Pad to 16 bytes */
	uint64_t       ia_pad;
	/** Entries in array */
	struct ilog_id ia_id[0];
};

#endif /* __ILOG_INTERNAL_H__ */
