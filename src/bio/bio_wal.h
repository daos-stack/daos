/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __BIO_WAL_H__
#define __BIO_WAL_H__

#include "bio_internal.h"

/* Meta blob header */
struct meta_header {
	uint32_t	mh_magic;
	uint32_t	mh_version;
	uuid_t		mh_meta_devid;		/* Meta SSD device ID */
	uuid_t		mh_wal_devid;		/* WAL SSD device ID */
	uuid_t		mh_data_devid;		/* Data SSD device ID */
	uint64_t	mh_meta_blobid;		/* Meta blob ID */
	uint64_t	mh_wal_blobid;		/* WAL blob ID */
	uint64_t	mh_data_blobid;		/* Data blob ID */
	uint32_t	mh_blk_bytes;		/* Block size for meta, in bytes */
	uint32_t	mh_hdr_blks;		/* Meta blob header size, in blocks */
	uint64_t	mh_tot_blks;		/* Meta blob capacity, in blocks */
	uint32_t	mh_vos_id;		/* Associated per-engine target ID */
	uint32_t	mh_padding[6];		/* Reserved */
	uint32_t	mh_csum;		/* Checksum of this header */
};

/* WAL blob header */
struct wal_header {
	uint32_t	wh_magic;
	uint32_t	wh_version;
	uint32_t	wh_gen;		/* WAL re-format timestamp */
	uint16_t	wh_blk_bytes;	/* WAL block size in bytes, usually 4k */
	uint16_t	wh_padding1;	/* Reserved */
	uint64_t	wh_tot_blks;	/* WAL blob capacity, in blocks */
	uint64_t	wh_ckp_id;	/* Last check-pointed transaction ID */
	uint64_t	wh_commit_id;	/* Last committed transaction ID */
	uint64_t	wh_unused_id;	/* Next unused transaction ID */
	uint64_t	wh_padding2;	/* Reserved */
	uint32_t	wh_padding3;	/* Reserved */
	uint32_t	wh_csum;	/* Checksum of this header */
};

enum wal_hdr_flags {
	WAL_HDR_FL_TAIL	= (1UL << 0),	/* The tail entry is in current block */
};

/* WAL transaction header */
struct wal_trans_head {
	uint32_t	th_magic;
	uint32_t	th_gen;		/* WAL re-format timestamp */
	uint32_t	th_len;		/* Total length of transaction entries in WAL */
	uint32_t	th_flags;	/* See wal_hdr_flags */
	uint64_t	th_id;		/* Transaction ID */
};

/* XXX to be adjusted according to umem action types */
enum wal_op_type {
	WAL_OP_MEMCPY	= 0,		/* memcpy data to meta blob */
	WAL_OP_MEMMOVE,			/* memmove data on meta blob */
	WAL_OP_MEMSET,			/* memset data on meta blob */
	WAL_OP_DATA_CSUM,		/* checksum of data on data blob */
	WAL_OP_MAX,
};

/* WAL transaction entry */
struct wal_trans_entry {
	uint64_t	te_off;		/* Offset within meta blob, in bytes */
	uint32_t	te_type;	/* Operation type, see wal_op_type */
	uint32_t	te_len;		/* Data length in bytes */
	/* Copied data for OP_MEMCPY, Offset for OP_MEMMOVE, Checksum for DATA_CSUM */
	uint8_t		te_data[0];
};

/* WAL transaction tail */
struct wal_trans_tail {
	uint32_t	tt_csum;	/* Checksum of WAL transaction */
};

/* In-memory WAL super information */
struct wal_super_info {
	struct wal_header	si_header;
	uint64_t		si_ckp_id;	/* Last check-pointed ID */
	uint64_t		si_commit_id;	/* Last committed ID */
	uint32_t		si_ckp_blks;	/* Blocks used by last check-pointed ID */
	uint32_t		si_commit_blks;	/* Blocks used by last committed ID */
	uint64_t		si_unused_id;	/* Next unused ID */
	ABT_cond		si_submit_wq;	/* Wait-queue for WAL trans submission */
	ABT_cond		si_reserve_wq;	/* Wait-queue for WAL ID reserving */
	unsigned int		si_dirty:1;	/* Header is dirty or not */
};

/* In-memory Meta context, exported as opaque data structure */
struct bio_meta_context {
	struct bio_io_context	*mc_data;	/* Data blob I/O context */
	struct bio_io_context	*mc_meta;	/* Meta blob I/O context */
	struct bio_io_context	*mc_wal;	/* WAL blob I/O context */
	struct wal_super_info	 mc_wal_info;
};

#endif /* __BIO_WAL_H__*/
