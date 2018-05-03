/**
 * (C) Copyright 2018 Intel Corporation.
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
 * smd : Internal Declarations
 *
 * This file contains all declarations that are only used by
 * nvme persistent metadata.
 */

#ifndef __SMD_INTERNAL_H__
#define __SMD_INTERNAL_H__

#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos/common.h>
#include <daos/btree_class.h>
#include <libpmemobj.h>
#include <abt.h>
#include <daos_srv/smd.h>

/**
 * Common file names used by each layer to store persistent data
 */
#define	SRV_NVME_META	"nvme-meta"
#define SMD_FILE_SIZE (256 * 1024 * 1024UL)

enum {
	SMD_PTAB_LOCK,
	SMD_DTAB_LOCK,
	SMD_STAB_LOCK,
};

struct smd_params {
	/** directory for per-server metadata */
	char			*smp_path;
	/** nvme per-server metadata file */
	char			*smp_file;
	/** pool uuid for the server metadata file */
	char			*smp_pool_id;
	/** memory class for metadata pool */
	umem_class_id_t		smp_mem_class;
	/** ABT mutex for device table */
	ABT_mutex		smp_dtab_mutex;
	/** ABT mutex for pool table */
	ABT_mutex		smp_ptab_mutex;
	/** ABT mutex for stream table */
	ABT_mutex		smp_stab_mutex;
};

POBJ_LAYOUT_BEGIN(smd_md_layout);
POBJ_LAYOUT_ROOT(smd_md_layout, struct smd_df);
POBJ_LAYOUT_TOID(smd_md_layout, struct smd_nvme_dev_tab_df);
POBJ_LAYOUT_TOID(smd_md_layout, struct smd_nvme_dev_df);
POBJ_LAYOUT_TOID(smd_md_layout, struct smd_nvme_pool_tab_df);
POBJ_LAYOUT_TOID(smd_md_layout, struct smd_nvme_pool_df);
POBJ_LAYOUT_TOID(smd_md_layout, struct smd_nvme_stream_tab_df);
POBJ_LAYOUT_TOID(smd_md_layout, struct smd_nvme_stream_df);
POBJ_LAYOUT_END(smd_md_layout);

/**
 * Create a new nvme device table
 * for Device to Xstream mapping
 */
struct smd_nvme_stream_tab_df {
	struct btr_root			nst_btr;
};

struct smd_nvme_dev_tab_df {
	struct btr_root			ndt_btr;
};

/**
 * Create a new nvme pool table which
 * maps nvme device, xstream ID, blob ID
 */
struct smd_nvme_pool_tab_df {
	struct btr_root		npt_btr;
};

struct smd_nvme_pool_df {
	struct smd_nvme_pool_info np_info;
};

struct smd_nvme_dev_df {
	struct smd_nvme_device_info	nd_info;
};

struct smd_nvme_stream_df {
	struct smd_nvme_stream_bond	ns_map;
};

struct smd_df {
	/*  NVMe metadata pool assigned on creation */
	uuid_t				smd_id;
	/* Flags for compatibility features */
	uint64_t			smd_compat_flags;
	/* Flags for incompatibility features */
	uint64_t			smd_incompat_flags;
	/*
	 * Pointer to the nvme device metadata table
	 * Device to device status mapping
	 */
	struct smd_nvme_dev_tab_df	smd_dev_tab_df;
	/*
	 * Pointer to the nvme pool table (Xstream to NMVe blob
	 * mapping)
	 */
	struct smd_nvme_pool_tab_df	smd_pool_tab_df;
	/*
	 * Pointer to the nvme stream to device mapping table
	 */
	struct smd_nvme_stream_tab_df	smd_stream_tab_df;
};

/** Pool table key type */
struct pool_tab_key {
	uuid_t	ptk_pid;
	int	ptk_sid;
};

/**
 * Device Persistent metadata pool handle
 */
struct smd_store {
	struct umem_attr	sms_uma;
	struct umem_instance	sms_umm;
	daos_handle_t		sms_dev_tab;
	daos_handle_t		sms_pool_tab;
	daos_handle_t		sms_stream_tab;
};

struct smd_store *get_sm_obj();
/** PMEM to direct point conversion of md ROOT */
static inline struct smd_df *
pmempool_pop2df(PMEMobjpool *pop)
{
	TOID(struct smd_df) pool_df;

	pool_df = POBJ_ROOT(pop, struct smd_df);
	return D_RW(pool_df);
}

static inline PMEMobjpool *
smd_store_ptr2pop(struct smd_store *sms_obj)
{
	return sms_obj->sms_uma.uma_u.pmem_pool;
}

static inline struct smd_df *
smd_store_ptr2df(struct smd_store *sms_obj)
{
	return pmempool_pop2df(smd_store_ptr2pop(sms_obj));
}

void	smd_lock(int table_type);
void	smd_unlock(int table_type);
int	smd_nvme_md_tables_register(void);

/* Server Metadata Library destroy a Metadata store */
int	smd_nvme_md_dtab_create(struct umem_attr *d_umem_attr,
				struct smd_nvme_dev_tab_df *table);
int	smd_nvme_md_ptab_create(struct umem_attr *p_umem_attr,
				struct smd_nvme_pool_tab_df *table);
int	smd_nvme_md_stab_create(struct umem_attr *p_umem_attr,
				struct smd_nvme_stream_tab_df *table);

#endif /** __SMD_INTERNAL_H__ */
