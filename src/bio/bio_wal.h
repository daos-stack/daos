/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __BIO_WAL_H__
#define __BIO_WAL_H__

#include "bio_internal.h"

enum meta_hdr_flags {
	META_HDR_FL_EMPTY	= (1UL << 0),
};

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
	uint32_t	mh_flags;		/* Meta header flags */
	uint32_t	mh_padding[5];		/* Reserved */
	uint32_t	mh_csum;		/* Checksum of this header */
};

enum wal_hdr_flags {
	WAL_HDR_FL_NO_TAIL	= (1 << 0),	/* No tail checksum */
};

/* WAL blob header */
struct wal_header {
	uint32_t	wh_magic;
	uint32_t	wh_version;
	uint32_t	wh_gen;		/* WAL re-format timestamp */
	uint16_t	wh_blk_bytes;	/* WAL block size in bytes, usually 4k */
	uint16_t	wh_flags;	/* WAL header flags */
	uint64_t	wh_tot_blks;	/* WAL blob capacity, in blocks */
	uint64_t	wh_ckp_id;	/* Last check-pointed transaction ID */
	uint64_t	wh_commit_id;	/* Last committed transaction ID */
	uint32_t	wh_ckp_blks;	/* blocks used by last check-pointed transaction */
	uint32_t	wh_commit_blks;	/* blocks used by last committed transaction */
	uint64_t	wh_padding2;	/* Reserved */
	uint32_t	wh_padding3;	/* Reserved */
	uint32_t	wh_csum;	/* Checksum of this header */
};

/*
 * WAL transaction starts with a header (wal_trans_head), and there will one or multiple
 * entries (wal_trans_entry) being placed immediately after the header, the payload data
 * from entries are concatenated after the last entry, the tail (wal_trans_tail) will be
 * placed after the payload (or the last entry when there is no payload).
 *
 * When the transaction spans multiple WAL blocks, the header will be duplicated to the
 * head of each block.
 */

/* WAL transaction header */
struct wal_trans_head {
	uint32_t	th_magic;
	uint32_t	th_gen;		/* WAL re-format timestamp */
	uint64_t	th_id;		/* Transaction ID */
	uint32_t	th_tot_ents;	/* Total entries */
	uint32_t	th_tot_payload;	/* Total payload size in bytes */
} __attribute__((packed));

/* WAL transaction entry */
struct wal_trans_entry {
	uint64_t	te_off;		/* Offset within meta blob, in bytes */
	uint32_t	te_len;		/* Data length in bytes */
	uint32_t	te_data;	/* Various inline data */
	uint16_t	te_type;	/* Operation type, see UMEM_ACT_XXX */
} __attribute__((packed));

/* WAL transaction tail */
struct wal_trans_tail {
	uint32_t	tt_csum;	/* Checksum of WAL transaction */
} __attribute__((packed));

/* In-memory WAL super information */
struct wal_super_info {
	struct wal_header	si_header;	/* WAL blob header */
	uint64_t		si_ckp_id;	/* Last check-pointed ID */
	uint64_t		si_commit_id;	/* Last committed ID */
	uint32_t		si_ckp_blks;	/* Blocks used by last check-pointed ID */
	uint32_t		si_commit_blks;	/* Blocks used by last committed ID */
	uint64_t                si_unused_id;   /* Next unused ID */
	d_list_t		si_pending_list;/* Pending transactions */
	ABT_cond		si_rsrv_wq;	/* FIFO waitqueue for WAL ID reserving */
	ABT_mutex		si_mutex;	/* For si_rsrv_wq */
	unsigned int		si_rsrv_waiters;/* Number of waiters in reserve waitqueue */
	unsigned int		si_tx_failed:1;	/* Indicating some transaction failed */
};

/* In-memory Meta context, exported as opaque data structure */
struct bio_meta_context {
	struct bio_io_context	*mc_data;	/* Data blob I/O context */
	struct bio_io_context	*mc_meta;	/* Meta blob I/O context */
	struct bio_io_context	*mc_wal;	/* WAL blob I/O context */
	struct meta_header	 mc_meta_hdr;	/* Meta blob header */
	struct wal_super_info	 mc_wal_info;	/* WAL blob super information */
	struct hash_ft		*mc_csum_algo;
	void			*mc_csum_ctx;
};

struct meta_fmt_info {
	uuid_t		fi_pool_id;		/* Pool UUID */
	uuid_t		fi_meta_devid;		/* Meta SSD device ID */
	uuid_t		fi_wal_devid;		/* WAL SSD device ID */
	uuid_t		fi_data_devid;		/* Data SSD device ID */
	uint64_t	fi_meta_blobid;		/* Meta blob ID */
	uint64_t	fi_wal_blobid;		/* WAL blob ID */
	uint64_t	fi_data_blobid;		/* Data blob ID */
	uint64_t	fi_meta_size;		/* Meta blob size in bytes */
	uint64_t	fi_wal_size;		/* WAL blob size in bytes */
	uint64_t	fi_data_size;		/* Data blob size in bytes */
	uint32_t	fi_vos_id;		/* Associated per-engine target ID */
};

int meta_format(struct bio_meta_context *mc, struct meta_fmt_info *fi, bool force);
int meta_open(struct bio_meta_context *mc);
void meta_close(struct bio_meta_context *mc);
int wal_open(struct bio_meta_context *mc);
void wal_close(struct bio_meta_context *mc);

#endif /* __BIO_WAL_H__*/
