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


#define DBTREE_CLASS_SMD_DTAB (DBTREE_SMD_BEGIN + 0)
#define DBTREE_CLASS_SMD_PTAB (DBTREE_SMD_BEGIN + 1)
#define DBTREE_CLASS_SMD_STAB (DBTREE_SMD_BEGIN + 2)

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
	/** device ID of the NVMe SSD device */
	uuid_t			nd_dev_id;
	/** status of this device */
	uint32_t		nd_status;
	/** padding bytes */
	uint32_t		nd_padding;
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

struct smd_store *get_smd_store();
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
	return sms_obj->sms_uma.uma_pool;
}

static inline struct smd_df *
smd_store_ptr2df(struct smd_store *sms_obj)
{
	return pmempool_pop2df(smd_store_ptr2pop(sms_obj));
}

void	smd_lock(int table_type);
void	smd_unlock(int table_type);

extern  btr_ops_t dtab_ops;
extern  btr_ops_t ptab_ops;
extern  btr_ops_t stab_ops;

static inline int
smd_nvme_md_tables_register(void)
{
	int	rc;

	D_DEBUG(DB_DF, "Register persistent metadata device index: %d\n",
		DBTREE_CLASS_SMD_DTAB);

	rc = dbtree_class_register(DBTREE_CLASS_SMD_DTAB, 0, &dtab_ops);
	if (rc)
		D_ERROR("DBTREE DTAB creation failed\n");

	D_DEBUG(DB_DF, "Register peristent metadata pool index: %d\n",
		DBTREE_CLASS_SMD_PTAB);

	rc = dbtree_class_register(DBTREE_CLASS_SMD_PTAB, 0, &ptab_ops);
	if (rc)
		D_ERROR("DBTREE PTAB creation failed\n");

	rc = dbtree_class_register(DBTREE_CLASS_SMD_STAB, 0, &stab_ops);
	if (rc)
		D_ERROR("DBTREE STAB creation failed\n");

	return rc;

}

/* Server Metadata Library destroy a Metadata store */
int	smd_nvme_md_dtab_create(struct umem_attr *d_umem_attr,
				struct smd_nvme_dev_tab_df *table);
int	smd_nvme_md_ptab_create(struct umem_attr *p_umem_attr,
				struct smd_nvme_pool_tab_df *table);
int	smd_nvme_md_stab_create(struct umem_attr *p_umem_attr,
				struct smd_nvme_stream_tab_df *table);

/** device lookup internal function -- find and update in single transaction */
int	smd_dtab_df_find_update(struct smd_store *nvme_obj, struct d_uuid *ukey,
				uint32_t status);

/**
 * List all the Xstreams from the stream table
 *
 * \param nr		[IN, OUT]	[in]:	number of stream mappings.
 *					[out]:	number of stream mappings
 *						returned.
 *
 * \param streams	[IN, OUT]	[in]:	preallocated array of \nr
 *						stream mappings.
 *					[out]:	returned list of out \nr
 *						stream mappings.
 *
 * \param anchor	[IN, OUT]		hash anchor for the next call,
 *					        it must be set to zeroes for the
 *						first call, it should not be
 *						changed by caller between calls.
 *
 * \return					Zero on success, negative value
 *						on error.
 */
int
smd_nvme_list_streams(uint32_t *nr, struct smd_nvme_stream_bond *streams,
		      daos_anchor_t *anchor);



#endif /** __SMD_INTERNAL_H__ */
