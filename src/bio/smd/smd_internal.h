/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * smd : Internal Declarations
 *
 * This file contains all declarations that are only used by
 * nvme persistent metadata.
 */

#ifndef __SMD_INTERNAL_H__
#define __SMD_INTERNAL_H__

#include <abt.h>
#include <daos_types.h>
#include <daos/btree.h>
#include <daos/mem.h>
#include <daos_srv/smd.h>

POBJ_LAYOUT_BEGIN(smd_md_layout);
POBJ_LAYOUT_ROOT(smd_md_layout, struct smd_df);
POBJ_LAYOUT_END(smd_md_layout);

/* Maximum target(VOS xstream) count */
#define SMD_MAX_TGT_CNT		64

#define SMD_DF_MAGIC		0x5eaf00d

/* The lowest supported version */
#define SMD_DF_VER_1		2
/* The current SMD DF version */
#define SMD_DF_VERSION		SMD_DF_VER_1

/* SMD root durable format */
struct smd_df {
	/** magic number to idenfity durable formatn */
	uint32_t	smd_magic;
	/** the current version of durable format */
	uint32_t	smd_version;
	struct btr_root	smd_dev_tab;	/* device table */
	struct btr_root	smd_pool_tab;	/* pool table */
	struct btr_root	smd_tgt_tab;	/* target table */
};

/* SMD store (DRAM structure) */
struct smd_store {
	struct umem_instance	ss_umm;
	ABT_mutex		ss_mutex;
	daos_handle_t		ss_dev_hdl;
	daos_handle_t		ss_pool_hdl;
	daos_handle_t		ss_tgt_hdl;
};

static inline struct smd_df *
smd_pop2df(PMEMobjpool *pop)
{
	TOID(struct smd_df) smd_df;

	smd_df = POBJ_ROOT(pop, struct smd_df);
	return D_RW(smd_df);
}

static inline int
smd_tx_begin(struct smd_store *store)
{
	return umem_tx_begin(&store->ss_umm, NULL);
}

static inline int
smd_tx_end(struct smd_store *store, int rc)
{
	if (rc != 0)
		return umem_tx_abort(&store->ss_umm, rc);

	return umem_tx_commit(&store->ss_umm);
}

static inline void
smd_lock(struct smd_store *store)
{
	ABT_mutex_lock(store->ss_mutex);
}

static inline void
smd_unlock(struct smd_store *store)
{
	ABT_mutex_unlock(store->ss_mutex);
}

extern struct smd_store		smd_store;

/* smd_pool.c */
int
smd_replace_blobs(struct smd_pool_info *info, uint32_t tgt_cnt, int *tgts);

#endif /** __SMD_INTERNAL_H__ */
