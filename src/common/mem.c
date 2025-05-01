/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * common/mem.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/mem.h>
#ifdef DAOS_PMEM_BUILD
#include <libpmemobj.h>
#include <daos_srv/ad_mem.h>
#define DAV_V2_BUILD
#include "dav/dav.h"
#include "dav_v2/dav_v2.h"
#endif

#define UMEM_TX_DATA_MAGIC	(0xc01df00d)

#define TXD_CB_NUM		(1 << 5)	/* 32 callbacks */
#define TXD_CB_MAX		(1 << 20)	/* 1 million callbacks */


struct umem_tx_stage_item {
	int		 txi_magic;
	umem_tx_cb_t	 txi_fn;
	void		*txi_data;
};

#ifdef DAOS_PMEM_BUILD

static int daos_md_backend = DAOS_MD_PMEM;
#define UMM_SLABS_CNT 16

/** Initializes global settings for the pmem objects.
 *
 *  \param	md_on_ssd[IN]	Boolean indicating if MD-on-SSD is enabled.
 *
 *  \return	0 on success, non-zero on failure.
 */
int
umempobj_settings_init(bool md_on_ssd)
{
	int					rc;
	enum pobj_arenas_assignment_type	atype;
	unsigned int                            md_mode = DAOS_MD_BMEM_V2;

	if (!md_on_ssd) {
		daos_md_backend = DAOS_MD_PMEM;
		atype = POBJ_ARENAS_ASSIGNMENT_GLOBAL;

		rc = pmemobj_ctl_set(NULL, "heap.arenas_assignment_type", &atype);
		if (rc != 0)
			D_ERROR("Could not configure PMDK for global arena: %s\n",
				strerror(errno));
		return rc;
	}

	d_getenv_uint("DAOS_MD_ON_SSD_MODE", &md_mode);

	switch (md_mode) {
	case DAOS_MD_BMEM:
		D_INFO("UMEM will use Blob Backed Memory as the metadata backend interface\n");
		break;
	case DAOS_MD_ADMEM:
		D_INFO("UMEM will use AD-hoc Memory as the metadata backend interface\n");
		break;
	case DAOS_MD_BMEM_V2:
		D_INFO("UMEM will use Blob Backed Memory v2 as the metadata backend interface\n");
		break;
	default:
		D_ERROR("DAOS_MD_ON_SSD_MODE=%d envar invalid, use %d for BMEM or %d for ADMEM\n",
			md_mode, DAOS_MD_BMEM, DAOS_MD_ADMEM);
		return -DER_INVAL;
	};

	daos_md_backend = md_mode;
	return 0;
}

int
umempobj_get_backend_type(void)
{
	return daos_md_backend;
}

int
umempobj_backend_type2class_id(int backend)
{
	switch (backend) {
	case DAOS_MD_PMEM:
		return UMEM_CLASS_PMEM;
	case DAOS_MD_BMEM:
		return UMEM_CLASS_BMEM;
	case DAOS_MD_ADMEM:
		return UMEM_CLASS_ADMEM;
	case DAOS_MD_BMEM_V2:
		return UMEM_CLASS_BMEM_V2;
	default:
		D_ASSERTF(0,
			  "bad daos_md_backend %d\n", backend);
		return -DER_INVAL;
	}
}

size_t
umempobj_pgsz(int backend)
{
	if (backend != DAOS_MD_PMEM)
		return dav_obj_pgsz_v2();
	else
		return (1UL << 12);
}

/** Define common slabs.  We can refine this for 2.4 pools but that is for next patch */
static const int        slab_map[] = {
    0,          /* 32 bytes */
    1,          /* 64 bytes */
    2,          /* 96 bytes */
    3,          /* 128 bytes */
    4,          /* 160 bytes */
    5,          /* 192 bytes */
    6,          /* 224 bytes */
    7,          /* 256 bytes */
    8,          /* 288 bytes */
    -1, 9,      /* 352 bytes */
    10,         /* 384 bytes */
    11,         /* 416 bytes */
    -1, -1, 12, /* 512 bytes */
    -1, 13,     /* 576 bytes (2.2 compatibility only) */
    -1, -1, 14, /* 672 bytes (2.2 compatibility only) */
    -1, -1, 15, /* 768 bytes (2.2 compatibility only) */
};

/** Create a slab within the pool.
 *
 *  \param	pool[IN]		Pointer to the persistent object.
 *  \param	slab[IN]		Slab description
 *
 *  \return	zero on success, non-zero on failure.
 */
static int
set_slab_desc(struct umem_pool *ph_p, struct umem_slab_desc *slab)
{
	PMEMobjpool			*pop;
	struct pobj_alloc_class_desc	 pmemslab;
	struct dav_alloc_class_desc	 davslab;
	int				 rc = 0;

	switch (ph_p->up_store.store_type) {
	static unsigned class_id = 10;

	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		pmemslab.unit_size = slab->unit_size;
		pmemslab.alignment = 0;
		pmemslab.units_per_block = 1000;
		pmemslab.header_type = POBJ_HEADER_NONE;
		pmemslab.class_id = slab->class_id;
		rc = pmemobj_ctl_set(pop, "heap.alloc_class.new.desc", &pmemslab);
		/* update with the new slab id */
		slab->class_id = pmemslab.class_id;
		break;
	case DAOS_MD_BMEM:
		davslab.unit_size = slab->unit_size;
		davslab.alignment = 0;
		davslab.units_per_block = 1000;
		davslab.header_type = DAV_HEADER_NONE;
		davslab.class_id = slab->class_id;
		rc = dav_class_register((dav_obj_t *)ph_p->up_priv, &davslab);
		/* update with the new slab id */
		slab->class_id = davslab.class_id;
		break;
	case DAOS_MD_BMEM_V2:
		davslab.unit_size = slab->unit_size;
		davslab.alignment = 0;
		davslab.units_per_block = 1000;
		davslab.header_type = DAV_HEADER_NONE;
		davslab.class_id = slab->class_id;
		rc = dav_class_register_v2((dav_obj_t *)ph_p->up_priv, &davslab);
		/* update with the new slab id */
		slab->class_id = davslab.class_id;
		break;
	case DAOS_MD_ADMEM:
		/* NOOP for ADMEM now */
		slab->class_id = class_id++;
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", ph_p->up_store.store_type);
		break;
	}
	return rc;
}

static inline bool
slab_registered(struct umem_pool *pool, unsigned int slab_id)
{
	D_ASSERT(slab_id < UMM_SLABS_CNT);
	return pool->up_slabs[slab_id].class_id != 0;
}

static inline size_t
slab_usize(struct umem_pool *pool, unsigned int slab_id)
{
	D_ASSERT(slab_id < UMM_SLABS_CNT);
	return pool->up_slabs[slab_id].unit_size;
}

static inline uint64_t
slab_flags(struct umem_pool *pool, unsigned int slab_id)
{
	D_ASSERT(slab_id < UMM_SLABS_CNT);
	return (pool->up_store.store_type == DAOS_MD_PMEM) ?
		POBJ_CLASS_ID(pool->up_slabs[slab_id].class_id) :
		DAV_CLASS_ID(pool->up_slabs[slab_id].class_id);
}

static inline void
get_slab(struct umem_instance *umm, uint64_t *flags, size_t *size)
{
	struct umem_pool *pool;
	size_t aligned_size = D_ALIGNUP(*size, 32);
	int slab_idx;
	int slab;

	slab_idx = (aligned_size >> 5) - 1;
	if (slab_idx >= ARRAY_SIZE(slab_map))
		return;

	if (slab_map[slab_idx] == -1) {
		D_DEBUG(DB_MEM, "No slab %zu for allocation of size %zu, idx=%d\n", aligned_size,
			*size, slab_idx);
		return;
	}

	pool = umm->umm_pool;
	D_ASSERT(pool != NULL);

	slab = slab_map[slab_idx];

	D_ASSERTF(!slab_registered(pool, slab) || aligned_size == slab_usize(pool, slab),
		  "registered: %d, id: %d, size: %zu != %zu\n", slab_registered(pool, slab),
		  slab, aligned_size, slab_usize(pool, slab));

	*size = aligned_size;
	*flags |= slab_flags(pool, slab);
}

static int
register_slabs(struct umem_pool *ph_p)
{
	struct umem_slab_desc *slab;
	int                           i, rc;
	int                           defined;
	int                           used = 0;

	for (i = 0; i < ARRAY_SIZE(slab_map); i++) {
		if (slab_map[i] != -1)
			used |= (1 << i);
	}

	for (i = 0; i < UMM_SLABS_CNT; i++) {
		D_ASSERT(used != 0);
		defined               = __builtin_ctz(used);
		slab = &ph_p->up_slabs[i];
		slab->unit_size       = (defined + 1) * 32;

		rc = set_slab_desc(ph_p, slab);
		if (rc) {
			D_ERROR("Failed to register umem slab %d. rc:%d\n", i, rc);
			rc = umem_tx_errno(rc);
			return rc;
		}
		D_ASSERT(slab->class_id != 0);
		D_DEBUG(DB_MEM, "slab registered with size %zu\n", slab->unit_size);

		used &= ~(1 << defined);
	}
	D_ASSERT(used == 0);

	return 0;
}

/* Persistent object allocator functions */
/** Create a persistent memory object.
 *
 *  \param	path[IN]		Name of the memory pool file
 *  \param	layout_name[IN]		Unique name for the layout
 *  \param	flags[IN]		Additional flags
 *  \param	poolsize[IN]		Size of the pool
 *  \param	mode[IN]		Permission for creating the object
 *  \param	store[IN]		umem_store
 *
 *  \return	A pointer to the pool, NULL if creation fails.
 */
struct umem_pool *
umempobj_create(const char *path, const char *layout_name, int flags,
		size_t poolsize, mode_t mode, struct umem_store *store)
{
	struct umem_pool	*umm_pool = NULL;
	PMEMobjpool		*pop;
	dav_obj_t		*dav_hdl;
	struct ad_blob_handle	 bh;
	int			 enabled = 1;
	int			 rc;

	D_ALLOC(umm_pool, sizeof(*umm_pool) + sizeof(umm_pool->up_slabs[0]) * UMM_SLABS_CNT);
	if (umm_pool == NULL)
		return NULL;

	if (store != NULL)
		umm_pool->up_store = *store;
	else
		umm_pool->up_store.store_type = DAOS_MD_PMEM; /* default */

	D_DEBUG(DB_TRACE, "creating path %s, poolsize %zu, store_size %zu ...\n", path, poolsize,
		store != NULL ? store->stor_size : 0);
	switch (umm_pool->up_store.store_type) {
	case DAOS_MD_PMEM:
		pop = pmemobj_create(path, layout_name, poolsize, mode);
		if (!pop) {
			D_ERROR("Failed to create pool %s, size="DF_U64": %s\n",
				path, poolsize, pmemobj_errormsg());
			goto error;
		}
		if (flags & UMEMPOBJ_ENABLE_STATS) {
			rc = pmemobj_ctl_set(pop, "stats.enabled", &enabled);
			if (rc) {
				D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
					DP_RC(rc));
				rc = umem_tx_errno(rc);
				pmemobj_close(pop);
				goto error;
			}
		}

		umm_pool->up_priv = pop;
		break;
	case DAOS_MD_BMEM:
		dav_hdl = dav_obj_create(path, 0, poolsize, mode, &umm_pool->up_store);
		if (!dav_hdl) {
			D_ERROR("Failed to create pool %s, size="DF_U64": errno = %d\n",
				path, poolsize, errno);
			goto error;
		}
		umm_pool->up_priv = dav_hdl;
		break;
	case DAOS_MD_BMEM_V2:
		dav_hdl = dav_obj_create_v2(path, 0, poolsize, mode, &umm_pool->up_store);
		if (!dav_hdl) {
			D_ERROR("Failed to create pool %s, size="DF_U64": errno = %d\n",
				path, poolsize, errno);
			goto error;
		}
		umm_pool->up_priv = dav_hdl;
		break;
	case DAOS_MD_ADMEM:
		rc = ad_blob_create(path, 0, store, &bh);
		if (rc) {
			D_ERROR("ad_blob_create failed, "DF_RC"\n", DP_RC(rc));
			goto error;
		}

		umm_pool->up_priv = bh.bh_blob;
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", store->store_type);
		break;
	};

	rc = register_slabs(umm_pool);
	if (rc != 0)
		goto error;
	return umm_pool;
error:
	D_FREE(umm_pool);
	return NULL;
}

/** Open the given persistent memory object.
 *
 *  \param	path[IN]		Name of the memory pool file
 *  \param	layout_name[IN]		Name of the layout [PMDK]
 *  \param	flags[IN]		Additional flags
 *  \param	store[IN]		umem_store
 *
 *  \return	A pointer to the pool, NULL if creation fails.
 */
struct umem_pool *
umempobj_open(const char *path, const char *layout_name, int flags, struct umem_store *store)
{
	struct umem_pool	*umm_pool;
	PMEMobjpool		*pop;
	dav_obj_t		*dav_hdl;
	struct ad_blob_handle	 bh;
	int			 enabled = 1;
	int			 rc;

	D_ALLOC(umm_pool, sizeof(*umm_pool) + sizeof(umm_pool->up_slabs[0]) * UMM_SLABS_CNT);
	if (umm_pool == NULL)
		return NULL;

	if (store != NULL) {
		umm_pool->up_store = *store;
	} else {
		umm_pool->up_store.store_type = DAOS_MD_PMEM; /* default */
		umm_pool->up_store.store_standalone = true;
	}

	D_DEBUG(DB_TRACE, "opening %s\n", path);
	switch (umm_pool->up_store.store_type) {
	case DAOS_MD_PMEM:
		pop = pmemobj_open(path, layout_name);
		if (!pop) {
			D_ERROR("Error in opening the pool %s: %s\n",
				path, pmemobj_errormsg());
			goto error;
		}
		if (flags & UMEMPOBJ_ENABLE_STATS) {
			rc = pmemobj_ctl_set(pop, "stats.enabled", &enabled);
			if (rc) {
				D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
					DP_RC(rc));
				rc = umem_tx_errno(rc);
				pmemobj_close(pop);
				goto error;
			}
		}

		umm_pool->up_priv = pop;
		break;
	case DAOS_MD_BMEM:
		dav_hdl = dav_obj_open(path, 0, &umm_pool->up_store);
		if (!dav_hdl) {
			D_ERROR("Error in opening the pool %s: errno =%d\n",
				path, errno);
			goto error;
		}

		umm_pool->up_priv = dav_hdl;
		break;
	case DAOS_MD_BMEM_V2:
		dav_hdl = dav_obj_open_v2(path, 0, &umm_pool->up_store);
		if (!dav_hdl) {
			D_ERROR("Error in opening the pool %s: errno =%d\n",
				path, errno);
			goto error;
		}

		umm_pool->up_priv = dav_hdl;
		break;
	case DAOS_MD_ADMEM:
		rc = ad_blob_open(path, 0, store, &bh);
		if (rc) {
			D_ERROR("ad_blob_create failed, "DF_RC"\n", DP_RC(rc));
			goto error;
		}

		umm_pool->up_priv = bh.bh_blob;
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", umm_pool->up_store.store_type);
		break;
	}

	rc = register_slabs(umm_pool);
	if (rc != 0)
		goto error;
	return umm_pool;
error:
	D_FREE(umm_pool);
	return NULL;
}

/** Close the pmem object.
 *
 *  \param	ph_p[IN]		pointer to the pool object.
 */
void
umempobj_close(struct umem_pool *ph_p)
{
	PMEMobjpool		*pop;
	struct ad_blob_handle	 bh;

	switch (ph_p->up_store.store_type) {
	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		pmemobj_close(pop);
		break;
	case DAOS_MD_BMEM:
		dav_obj_close((dav_obj_t *)ph_p->up_priv);
		break;
	case DAOS_MD_BMEM_V2:
		dav_obj_close_v2((dav_obj_t *)ph_p->up_priv);
		break;
	case DAOS_MD_ADMEM:
		bh.bh_blob = (struct ad_blob *)ph_p->up_priv;
		ad_blob_close(bh);
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", ph_p->up_store.store_type);
		break;
	}

	D_FREE(ph_p);
}

/** Obtain the root memory address for the pmem object.
 *
 *  \param	ph_p[IN]		Pointer to the persistent object.
 *  \param	size[IN]		size of the structure that root is
 *					pointing to.
 *
 *  \return	A memory pointer to the pool's root location.
 */
void *
umempobj_get_rootptr(struct umem_pool *ph_p, size_t size)
{
	PMEMobjpool		*pop;
	void			*rootp;
	PMEMoid			 root;
	struct ad_blob_handle	 bh;
	uint64_t		 off;

	switch (ph_p->up_store.store_type) {
	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		root = pmemobj_root(pop, size);
		rootp = pmemobj_direct(root);
		return rootp;
	case DAOS_MD_BMEM:
		off = dav_root((dav_obj_t *)ph_p->up_priv, size);
		return (char *)dav_get_base_ptr((dav_obj_t *)ph_p->up_priv) + off;
	case DAOS_MD_BMEM_V2:
		off = dav_root_v2((dav_obj_t *)ph_p->up_priv, size);
		return (char *)umem_cache_off2ptr(&ph_p->up_store, off);
	case DAOS_MD_ADMEM:
		bh.bh_blob = (struct ad_blob *)ph_p->up_priv;
		return ad_root(bh, size);
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", ph_p->up_store.store_type);
		break;
	}

	return NULL;
}

/** Obtain the usage statistics for the pmem object
 *
 *  \param	pool[IN]		Pointer to the persistent object.
 *  \param	curr_allocated[IN|OUT]	Total bytes currently allocated
 *
 *  \return	zero on success and non-zero on failure.
 */
int
umempobj_get_heapusage(struct umem_pool *ph_p, daos_size_t *curr_allocated)
{
	PMEMobjpool		*pop;
	struct dav_heap_stats	 st;
	int			 rc = 0;

	switch (ph_p->up_store.store_type) {
	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		rc = pmemobj_ctl_get(pop, "stats.heap.curr_allocated",
			     curr_allocated);
		break;
	case DAOS_MD_BMEM:
		rc = dav_get_heap_stats((dav_obj_t *)ph_p->up_priv, &st);
		if (rc == 0)
			*curr_allocated = st.curr_allocated;
		break;
	case DAOS_MD_BMEM_V2:
		rc = dav_get_heap_stats_v2((dav_obj_t *)ph_p->up_priv, &st);
		if (rc == 0)
			*curr_allocated = st.curr_allocated;
		break;
	case DAOS_MD_ADMEM:
		*curr_allocated = 40960; /* TODO */
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", ph_p->up_store.store_type);
		break;
	}

	return rc;
}

/** Obtain the usage statistics for the memory bucket. Note that the usage
 *  statistics for an evictable memory bucket can be approximate value if
 *  memory bucket is not yet loaded on to the umem cache.
 *
 *  \param	ph_p[IN]		Pointer to the persistent object.
 *  \param	mb_id[IN]		memory bucket id.
 *  \param	curr_allocated[IN|OUT]	Total bytes currently allocated
 *  \param	maxsz[IN|OUT]	        Max size the memory bucket can grow.
 *
 *  \return	zero on success and non-zero on failure.
 */
int
umempobj_get_mbusage(struct umem_pool *ph_p, uint32_t mb_id, daos_size_t *curr_allocated,
		     daos_size_t *maxsz)
{
	struct dav_heap_mb_stats st;
	int                      rc = 0;

	switch (ph_p->up_store.store_type) {
	case DAOS_MD_PMEM:
	case DAOS_MD_BMEM:
	case DAOS_MD_ADMEM:
		rc = -DER_INVAL;
		break;
	case DAOS_MD_BMEM_V2:
		rc = dav_get_heap_mb_stats_v2((dav_obj_t *)ph_p->up_priv, mb_id, &st);
		if (rc == 0) {
			*curr_allocated = st.dhms_allocated;
			*maxsz          = st.dhms_maxsz;
		} else
			rc = daos_errno2der(errno);
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", ph_p->up_store.store_type);
		break;
	}

	return rc;
}

/** Force GC within the heap to optimize umem_cache usage. This is only
 *  with DAV v2 allocator.
 *
 *  \param	ph_p[IN]		Pointer to the persistent object.
 *
 *  \return	zero on success and non-zero on failure.
 */
int
umem_heap_gc(struct umem_instance *umm)
{
	struct umem_pool *ph_p = umm->umm_pool;

	if (ph_p->up_store.store_type == DAOS_MD_BMEM_V2)
		return dav_force_gc_v2((dav_obj_t *)ph_p->up_priv);
	return 0;
}

/** Log fragmentation related info for the pool.
 *
 *  \param	pool[IN]		Pointer to the persistent object.
 *
 */
void
umempobj_log_fraginfo(struct umem_pool *ph_p)
{
	PMEMobjpool		*pop;
	daos_size_t		 scm_used, scm_active;
	struct dav_heap_stats	 st;

	switch (ph_p->up_store.store_type) {
	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		pmemobj_ctl_get(pop, "stats.heap.run_allocated", &scm_used);
		pmemobj_ctl_get(pop, "stats.heap.run_active", &scm_active);

		D_ERROR("Fragmentation info, run_allocated: "
		  DF_U64", run_active: "DF_U64"\n", scm_used, scm_active);
		break;
	case DAOS_MD_BMEM:
		dav_get_heap_stats((dav_obj_t *)ph_p->up_priv, &st);
		D_ERROR("Fragmentation info, run_allocated: "
		  DF_U64", run_active: "DF_U64"\n",
		  st.run_allocated, st.run_active);
		break;
	case DAOS_MD_BMEM_V2:
		dav_get_heap_stats_v2((dav_obj_t *)ph_p->up_priv, &st);
		D_ERROR("Fragmentation info, run_allocated: "
		  DF_U64", run_active: "DF_U64"\n",
		  st.run_allocated, st.run_active);
		break;
	case DAOS_MD_ADMEM:
		/* TODO */
		D_ERROR("Fragmentation info, not implemented in ADMEM yet.\n");
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", ph_p->up_store.store_type);
		break;
	}
}

bool
umem_tx_none(struct umem_instance *umm)
{
	return (umem_tx_stage(umm) == UMEM_STAGE_NONE);
}

bool
umem_tx_inprogress(struct umem_instance *umm)
{
	return (umem_tx_stage(umm) == UMEM_STAGE_WORK);
}

/** Convert an offset to an id.   No invalid flags will be maintained
 *  in the conversion.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	umoff[in]	The offset to convert
 *
 *  \return	The oid.
 */
static inline PMEMoid
umem_off2id(const struct umem_instance *umm, umem_off_t umoff)
{
	PMEMoid	oid;

	if (UMOFF_IS_NULL(umoff))
		return OID_NULL;

	oid.pool_uuid_lo = umm->umm_pool_uuid_lo;
	oid.off = umem_off2offset(umoff);

	return oid;
}

/** Convert an id to an offset.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	oid[in]		The oid to convert
 *
 *  \return	The offset in the PMEM pool.
 */
static inline umem_off_t
umem_id2off(const struct umem_instance *umm, PMEMoid oid)
{
	if (OID_IS_NULL(oid))
		return UMOFF_NULL;

	return oid.off;
}

/** persistent memory operations (depends on pmdk) */

static int
pmem_tx_free(struct umem_instance *umm, umem_off_t umoff)
{
	/*
	 * This free call could be on error cleanup code path where
	 * the transaction is already aborted due to previous failed
	 * pmemobj_tx call. Let's just skip it in this case.
	 *
	 * The reason we don't fix caller to avoid calling tx_free()
	 * in an aborted transaction is that the caller code could be
	 * shared by both transactional and non-transactional (where
	 * UMEM_CLASS_VMEM is used, see btree code) interfaces, and
	 * the explicit umem_free() on error cleanup is necessary for
	 * non-transactional case.
	 */
	if (pmemobj_tx_stage() == TX_STAGE_ONABORT)
		return 0;

	if (!UMOFF_IS_NULL(umoff)) {
		int	rc;

		rc = pmemobj_tx_free(umem_off2id(umm, umoff));
		return rc ? umem_tx_errno(rc) : 0;
	}

	return 0;
}

static umem_off_t
pmem_tx_alloc(struct umem_instance *umm, size_t size, uint64_t flags, unsigned int type_num,
	      unsigned int unused)
{
	uint64_t pflags = 0;

	get_slab(umm, &pflags, &size);

	if (flags & UMEM_FLAG_ZERO)
		pflags |= POBJ_FLAG_ZERO;
	if (flags & UMEM_FLAG_NO_FLUSH)
		pflags |= POBJ_FLAG_NO_FLUSH;
	return umem_id2off(umm, pmemobj_tx_xalloc(size, type_num, pflags));
}

static int
pmem_tx_add(struct umem_instance *umm, umem_off_t umoff,
	    uint64_t offset, size_t size)
{
	int	rc;

	rc = pmemobj_tx_add_range(umem_off2id(umm, umoff), offset, size);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
pmem_tx_xadd(struct umem_instance *umm, umem_off_t umoff, uint64_t offset,
	     size_t size, uint64_t flags)
{
	int	rc;
	uint64_t pflags = 0;

	if (flags & UMEM_XADD_NO_SNAPSHOT)
		pflags |= POBJ_XADD_NO_SNAPSHOT;

	rc = pmemobj_tx_xadd_range(umem_off2id(umm, umoff), offset, size,
				   pflags);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
pmem_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	int	rc;

	rc = pmemobj_tx_add_range_direct(ptr, size);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
pmem_tx_abort(struct umem_instance *umm, int err)
{
	/*
	 * obj_tx_abort() may have already been called in the error
	 * handling code of pmemobj APIs.
	 */
	if (pmemobj_tx_stage() != TX_STAGE_ONABORT)
		pmemobj_tx_abort(err);

	err = pmemobj_tx_end();
	return err ? umem_tx_errno(err) : 0;
}

static void
umem_process_cb_vec(struct umem_tx_stage_item *vec, unsigned int *cnt, bool noop)
{
	struct umem_tx_stage_item	*txi, *txi_arr;
	unsigned int			 i, num = *cnt;

	if (num == 0)
		return;

	/* @vec & @cnt could be changed by other ULT while txi_fn yielding */
	D_ALLOC_ARRAY(txi_arr, num);
	if (txi_arr == NULL) {
		return;
	}
	memcpy(txi_arr, vec, sizeof(*txi) * num);
	*cnt = 0;
	memset(vec, 0, sizeof(*txi) * num);

	for (i = 0; i < num; i++) {
		txi = &txi_arr[i];

		D_ASSERT(txi->txi_magic == UMEM_TX_DATA_MAGIC);
		D_ASSERT(txi->txi_fn != NULL);

		/* When 'noop' is true, callback will only free txi_data */
		txi->txi_fn(txi->txi_data, noop);
	}

	D_FREE(txi_arr);
}

D_CASSERT((int)UMEM_STAGE_NONE		== (int)TX_STAGE_NONE);
D_CASSERT((int)UMEM_STAGE_WORK		== (int)TX_STAGE_WORK);
D_CASSERT((int)UMEM_STAGE_ONCOMMIT	== (int)TX_STAGE_ONCOMMIT);
D_CASSERT((int)UMEM_STAGE_ONABORT	== (int)TX_STAGE_ONABORT);
D_CASSERT((int)UMEM_STAGE_FINALLY	== (int)TX_STAGE_FINALLY);
D_CASSERT((int)MAX_UMEM_TX_STAGE	== (int)MAX_TX_STAGE);

D_CASSERT((int)UMEM_STAGE_NONE		== (int)DAV_TX_STAGE_NONE);
D_CASSERT((int)UMEM_STAGE_WORK		== (int)DAV_TX_STAGE_WORK);
D_CASSERT((int)UMEM_STAGE_ONCOMMIT	== (int)DAV_TX_STAGE_ONCOMMIT);
D_CASSERT((int)UMEM_STAGE_ONABORT	== (int)DAV_TX_STAGE_ONABORT);
D_CASSERT((int)UMEM_STAGE_FINALLY	== (int)DAV_TX_STAGE_FINALLY);
D_CASSERT((int)MAX_UMEM_TX_STAGE	== (int)DAV_MAX_TX_STAGE);

/*
 * This callback will be called on the outermost transaction commit (stage
 * == TX_STAGE_ONCOMMIT), abort (stage == TX_STAGE_ONABORT) and end (stage
 * == TX_STAGE_NONE).
 */
void
umem_stage_callback(int stage, void *data)
{
	struct umem_tx_stage_data	*txd = data;
	struct umem_tx_stage_item	*vec;
	unsigned int			*cnt;

	D_ASSERTF(stage >= UMEM_STAGE_NONE && stage < MAX_UMEM_TX_STAGE,
		  "bad stage %d\n", stage);
	D_ASSERT(txd != NULL);
	D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);

	switch (stage) {
	case UMEM_STAGE_ONCOMMIT:
		vec = txd->txd_commit_vec;
		cnt = &txd->txd_commit_cnt;
		/* Abandon the abort callbacks */
		umem_process_cb_vec(txd->txd_abort_vec, &txd->txd_abort_cnt, true);
		break;
	case UMEM_STAGE_ONABORT:
		vec = txd->txd_abort_vec;
		cnt = &txd->txd_abort_cnt;
		/* Abandon the commit callbacks */
		umem_process_cb_vec(txd->txd_commit_vec, &txd->txd_commit_cnt, true);
		break;
	case UMEM_STAGE_NONE:
		D_ASSERT(txd->txd_commit_cnt == 0);
		D_ASSERT(txd->txd_abort_cnt == 0);
		vec = txd->txd_end_vec;
		cnt = &txd->txd_end_cnt;
		break;
	default:
		/* Ignore all other stages */
		return;
	}

	umem_process_cb_vec(vec, cnt, false);
}

static void
pmem_stage_callback(PMEMobjpool *pop, int stage, void *data)
{
	umem_stage_callback(stage, data);
}

static int
pmem_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	int rc;
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;

	if (txd != NULL) {
		D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);
		rc = pmemobj_tx_begin(pop, NULL, TX_PARAM_CB, pmem_stage_callback,
				      txd, TX_PARAM_NONE);
	} else {
		rc = pmemobj_tx_begin(pop, NULL, TX_PARAM_NONE);
	}

	if (rc != 0) {
		/*
		 * pmemobj_tx_end() needs be called to re-initialize the
		 * tx state when pmemobj_tx_begin() failed.
		 */
		rc = pmemobj_tx_end();
		return rc ? umem_tx_errno(rc) : 0;
	}
	return 0;
}

static int
pmem_tx_commit(struct umem_instance *umm, void *data)
{
	int rc;

	pmemobj_tx_commit();
	rc = pmemobj_tx_end();

	return rc ? umem_tx_errno(rc) : 0;
}

static void
pmem_defer_free(struct umem_instance *umm, umem_off_t off, void *act)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;
	PMEMoid	id = umem_off2id(umm, off);

	pmemobj_defer_free(pop, id, (struct pobj_action *)act);
}

static int
pmem_tx_stage(void)
{
	return pmemobj_tx_stage();
}

static umem_off_t
pmem_reserve(struct umem_instance *umm, void *act, size_t size, unsigned int type_num,
	     unsigned int unused)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;

	return umem_id2off(umm, pmemobj_reserve(pop, (struct pobj_action *)act, size, type_num));
}

static void
pmem_cancel(struct umem_instance *umm, void *actv, int actv_cnt)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;

	pmemobj_cancel(pop, (struct pobj_action *)actv, actv_cnt);
}

static int
pmem_tx_publish(struct umem_instance *umm, void *actv, int actv_cnt)
{
	int	rc;

	rc = pmemobj_tx_publish((struct pobj_action *)actv, actv_cnt);
	return rc ? umem_tx_errno(rc) : 0;
}

static void *
pmem_atomic_copy(struct umem_instance *umm, void *dest, const void *src,
		 size_t len, enum acopy_hint hint)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;

	return pmemobj_memcpy_persist(pop, dest, src, len);
}

static umem_off_t
pmem_atomic_alloc(struct umem_instance *umm, size_t size, unsigned int type_num,
		  unsigned int unused)
{
	PMEMoid oid;
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;
	int rc;

	rc = pmemobj_alloc(pop, &oid, size, type_num, NULL, NULL);
	if (rc)
		return UMOFF_NULL;
	return umem_id2off(umm, oid);
}

static int
pmem_atomic_free(struct umem_instance *umm, umem_off_t umoff)
{
	if (!UMOFF_IS_NULL(umoff)) {
		PMEMoid oid;

		oid = umem_off2id(umm, umoff);
		pmemobj_free(&oid);
	}
	return 0;
}

static void
pmem_atomic_flush(struct umem_instance *umm, void *addr, size_t len)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool->up_priv;

	pmemobj_flush(pop, addr, len);
}

int
umem_tx_add_cb(struct umem_instance *umm, struct umem_tx_stage_data *txd,
	       int stage, umem_tx_cb_t cb, void *data)
{
	struct umem_tx_stage_item	*txi, **pvec;
	unsigned int			*cnt, *cnt_max;

	D_ASSERT(txd != NULL);
	D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);
	D_ASSERT(umem_tx_inprogress(umm));

	if (cb == NULL)
		return -DER_INVAL;

	switch (stage) {
	case UMEM_STAGE_ONCOMMIT:
		pvec = &txd->txd_commit_vec;
		cnt = &txd->txd_commit_cnt;
		cnt_max = &txd->txd_commit_max;
		break;
	case UMEM_STAGE_ONABORT:
		pvec = &txd->txd_abort_vec;
		cnt = &txd->txd_abort_cnt;
		cnt_max = &txd->txd_abort_max;
		break;
	case UMEM_STAGE_NONE:
		pvec = &txd->txd_end_vec;
		cnt = &txd->txd_end_cnt;
		cnt_max = &txd->txd_end_max;
		break;
	default:
		D_ERROR("Invalid stage %d\n", stage);
		return -DER_INVAL;
	}

	D_ASSERT(*cnt <= TXD_CB_MAX);
	if (*cnt == *cnt_max) {
		unsigned int new_max;

		if (*cnt_max == TXD_CB_MAX) {
			D_ERROR("Too many transaction callbacks "
				"cnt:%u, stage:%d\n", *cnt, stage);
			return -DER_OVERFLOW;
		}

		new_max = min((*cnt_max) << 1, TXD_CB_MAX);
		D_REALLOC_ARRAY(txi, *pvec, *cnt_max, new_max);
		if (txi == NULL)
			return -DER_NOMEM;

		*pvec = txi;
		*cnt_max = new_max;
	}

	txi = &(*pvec)[*cnt];
	(*cnt)++;
	txi->txi_magic = UMEM_TX_DATA_MAGIC;
	txi->txi_fn = cb;
	txi->txi_data = data;

	return 0;
}

static umem_ops_t	pmem_ops = {
	.mo_tx_free		= pmem_tx_free,
	.mo_tx_alloc		= pmem_tx_alloc,
	.mo_tx_add		= pmem_tx_add,
	.mo_tx_xadd		= pmem_tx_xadd,
	.mo_tx_add_ptr		= pmem_tx_add_ptr,
	.mo_tx_abort		= pmem_tx_abort,
	.mo_tx_begin		= pmem_tx_begin,
	.mo_tx_commit		= pmem_tx_commit,
	.mo_tx_stage		= pmem_tx_stage,
	.mo_reserve		= pmem_reserve,
	.mo_defer_free		= pmem_defer_free,
	.mo_cancel		= pmem_cancel,
	.mo_tx_publish		= pmem_tx_publish,
	.mo_atomic_copy		= pmem_atomic_copy,
	.mo_atomic_alloc	= pmem_atomic_alloc,
	.mo_atomic_free		= pmem_atomic_free,
	.mo_atomic_flush	= pmem_atomic_flush,
	.mo_tx_add_callback	= umem_tx_add_cb,
};


/** BMEM operations (depends on dav) */

static int
bmem_tx_free(struct umem_instance *umm, umem_off_t umoff)
{
	/*
	 * This free call could be on error cleanup code path where
	 * the transaction is already aborted due to previous failed
	 * pmemobj_tx call. Let's just skip it in this case.
	 *
	 * The reason we don't fix caller to avoid calling tx_free()
	 * in an aborted transaction is that the caller code could be
	 * shared by both transactional and non-transactional (where
	 * UMEM_CLASS_VMEM is used, see btree code) interfaces, and
	 * the explicit umem_free() on error cleanup is necessary for
	 * non-transactional case.
	 */
	if (dav_tx_stage() == DAV_TX_STAGE_ONABORT)
		return 0;

	if (!UMOFF_IS_NULL(umoff)) {
		int	rc;

		rc = dav_tx_free(umem_off2offset(umoff));
		return rc ? umem_tx_errno(rc) : 0;
	}

	return 0;
}

static umem_off_t
bmem_tx_alloc(struct umem_instance *umm, size_t size, uint64_t flags, unsigned int type_num,
	      unsigned int mbkt_id)
{
	uint64_t pflags = 0;

	get_slab(umm, &pflags, &size);

	if (flags & UMEM_FLAG_ZERO)
		pflags |= DAV_FLAG_ZERO;
	if (flags & UMEM_FLAG_NO_FLUSH)
		pflags |= DAV_FLAG_NO_FLUSH;
	return dav_tx_xalloc(size, type_num, pflags);
}

static int
bmem_tx_add(struct umem_instance *umm, umem_off_t umoff,
	    uint64_t offset, size_t size)
{
	int	rc;

	rc = dav_tx_add_range(umem_off2offset(umoff), size);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
bmem_tx_xadd(struct umem_instance *umm, umem_off_t umoff, uint64_t offset,
	     size_t size, uint64_t flags)
{
	int	rc;
	uint64_t pflags = 0;

	if (flags & UMEM_XADD_NO_SNAPSHOT)
		pflags |= DAV_XADD_NO_SNAPSHOT;

	rc = dav_tx_xadd_range(umem_off2offset(umoff), size, pflags);
	return rc ? umem_tx_errno(rc) : 0;
}


static int
bmem_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	int	rc;

	rc = dav_tx_add_range_direct(ptr, size);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
bmem_tx_abort(struct umem_instance *umm, int err)
{
	/*
	 * obj_tx_abort() may have already been called in the error
	 * handling code of pmemobj APIs.
	 */
	if (dav_tx_stage() != DAV_TX_STAGE_ONABORT)
		dav_tx_abort(err);

	err = dav_tx_end(NULL);
	return err ? umem_tx_errno(err) : 0;
}

static int
bmem_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	int rc;
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	if (txd != NULL) {
		D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);
		rc = dav_tx_begin(pop, NULL, DAV_TX_PARAM_CB, pmem_stage_callback,
				      txd, DAV_TX_PARAM_NONE);
	} else {
		rc = dav_tx_begin(pop, NULL, DAV_TX_PARAM_NONE);
	}

	if (rc != 0) {
		/*
		 * dav_tx_end() needs be called to re-initialize the
		 * tx state when dav_tx_begin() failed.
		 */
		rc = dav_tx_end(NULL);
		return rc ? umem_tx_errno(rc) : 0;
	}
	return 0;
}

static int
bmem_tx_commit(struct umem_instance *umm, void *data)
{
	int rc;

	dav_tx_commit();
	rc = dav_tx_end(data);

	return rc ? umem_tx_errno(rc) : 0;
}

static int
bmem_tx_stage(void)
{
	return dav_tx_stage();
}

static void
bmem_defer_free(struct umem_instance *umm, umem_off_t off, void *act)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	dav_defer_free(pop, umem_off2offset(off),
			(struct dav_action *)act);
}

static umem_off_t
bmem_reserve(struct umem_instance *umm, void *act, size_t size, unsigned int type_num,
	     unsigned int mbkt_id)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	return dav_reserve(pop, (struct dav_action *)act, size, type_num);
}

static void
bmem_cancel(struct umem_instance *umm, void *actv, int actv_cnt)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	dav_cancel(pop, (struct dav_action *)actv, actv_cnt);
}

static int
bmem_tx_publish(struct umem_instance *umm, void *actv, int actv_cnt)
{
	int	rc;

	rc = dav_tx_publish((struct dav_action *)actv, actv_cnt);
	return rc ? umem_tx_errno(rc) : 0;
}

static void *
bmem_atomic_copy(struct umem_instance *umm, void *dest, const void *src,
		 size_t len, enum acopy_hint hint)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	if (hint == UMEM_RESERVED_MEM) {
		memcpy(dest, src, len);
		return dest;
	} else { /* UMEM_COMMIT_IMMEDIATE */
		return dav_memcpy_persist(pop, dest, src, len);
	}
}

static umem_off_t
bmem_atomic_alloc(struct umem_instance *umm, size_t size, unsigned int type_num,
		  unsigned int mbkt_id)
{
	uint64_t off;
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;
	int rc;

	rc = dav_alloc(pop, &off, size, type_num, NULL, NULL);
	if (rc)
		return UMOFF_NULL;
	return off;
}

static int
bmem_atomic_free(struct umem_instance *umm, umem_off_t umoff)
{
	if (!UMOFF_IS_NULL(umoff)) {
		uint64_t off = umem_off2offset(umoff);

		dav_free((dav_obj_t *)umm->umm_pool->up_priv, off);
	}
	return 0;
}

static void
bmem_atomic_flush(struct umem_instance *umm, void *addr, size_t len)
{
	/* REVISIT: We need to update the WAL with this info
	 * dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;
	 * dav_flush(pop, addr, len);
	 */
}

static umem_ops_t	bmem_ops = {
	.mo_tx_free		= bmem_tx_free,
	.mo_tx_alloc		= bmem_tx_alloc,
	.mo_tx_add		= bmem_tx_add,
	.mo_tx_xadd		= bmem_tx_xadd,
	.mo_tx_add_ptr		= bmem_tx_add_ptr,
	.mo_tx_abort		= bmem_tx_abort,
	.mo_tx_begin		= bmem_tx_begin,
	.mo_tx_commit		= bmem_tx_commit,
	.mo_tx_stage		= bmem_tx_stage,
	.mo_reserve		= bmem_reserve,
	.mo_defer_free		= bmem_defer_free,
	.mo_cancel		= bmem_cancel,
	.mo_tx_publish		= bmem_tx_publish,
	.mo_atomic_copy		= bmem_atomic_copy,
	.mo_atomic_alloc	= bmem_atomic_alloc,
	.mo_atomic_free		= bmem_atomic_free,
	.mo_atomic_flush	= bmem_atomic_flush,
	.mo_tx_add_callback	= umem_tx_add_cb,
};

/** BMEM v2 operations (depends on dav) */

static int
bmem_tx_free_v2(struct umem_instance *umm, umem_off_t umoff)
{
	/*
	 * This free call could be on error cleanup code path where
	 * the transaction is already aborted due to previous failed
	 * pmemobj_tx call. Let's just skip it in this case.
	 *
	 * The reason we don't fix caller to avoid calling tx_free()
	 * in an aborted transaction is that the caller code could be
	 * shared by both transactional and non-transactional (where
	 * UMEM_CLASS_VMEM is used, see btree code) interfaces, and
	 * the explicit umem_free() on error cleanup is necessary for
	 * non-transactional case.
	 */
	if (dav_tx_stage_v2() == DAV_TX_STAGE_ONABORT)
		return 0;

	if (!UMOFF_IS_NULL(umoff)) {
		int	rc;

		rc = dav_tx_free_v2(umem_off2offset(umoff));
		return rc ? umem_tx_errno(rc) : 0;
	}

	return 0;
}

static umem_off_t
bmem_tx_alloc_v2(struct umem_instance *umm, size_t size, uint64_t flags, unsigned int type_num,
	      unsigned int mbkt_id)
{
	uint64_t pflags = 0;

	get_slab(umm, &pflags, &size);

	if (flags & UMEM_FLAG_ZERO)
		pflags |= DAV_FLAG_ZERO;
	if (flags & UMEM_FLAG_NO_FLUSH)
		pflags |= DAV_FLAG_NO_FLUSH;
	if (mbkt_id != 0)
		pflags |= DAV_EZONE_ID(mbkt_id);
	return dav_tx_alloc_v2(size, type_num, pflags);
}

static int
bmem_tx_add_v2(struct umem_instance *umm, umem_off_t umoff,
	    uint64_t offset, size_t size)
{
	int	rc;

	rc = dav_tx_add_range_v2(umem_off2offset(umoff), size);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
bmem_tx_xadd_v2(struct umem_instance *umm, umem_off_t umoff, uint64_t offset,
	     size_t size, uint64_t flags)
{
	int	rc;
	uint64_t pflags = 0;

	if (flags & UMEM_XADD_NO_SNAPSHOT)
		pflags |= DAV_XADD_NO_SNAPSHOT;

	rc = dav_tx_xadd_range_v2(umem_off2offset(umoff), size, pflags);
	return rc ? umem_tx_errno(rc) : 0;
}


static int
bmem_tx_add_ptr_v2(struct umem_instance *umm, void *ptr, size_t size)
{
	int	rc;

	rc = dav_tx_add_range_direct_v2(ptr, size);
	return rc ? umem_tx_errno(rc) : 0;
}

static int
bmem_tx_abort_v2(struct umem_instance *umm, int err)
{
	/*
	 * obj_tx_abort() may have already been called in the error
	 * handling code of pmemobj APIs.
	 */
	if (dav_tx_stage_v2() != DAV_TX_STAGE_ONABORT)
		dav_tx_abort_v2(err);

	err = dav_tx_end_v2(NULL);
	return err ? umem_tx_errno(err) : 0;
}

static int
bmem_tx_begin_v2(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	int rc;
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	if (txd != NULL) {
		D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);
		rc = dav_tx_begin_v2(pop, NULL, DAV_TX_PARAM_CB, pmem_stage_callback,
				      txd, DAV_TX_PARAM_NONE);
	} else {
		rc = dav_tx_begin_v2(pop, NULL, DAV_TX_PARAM_NONE);
	}

	if (rc != 0) {
		/*
		 * dav_tx_end() needs be called to re-initialize the
		 * tx state when dav_tx_begin() failed.
		 */
		rc = dav_tx_end_v2(NULL);
		return rc ? umem_tx_errno(rc) : 0;
	}
	return 0;
}

static int
bmem_tx_commit_v2(struct umem_instance *umm, void *data)
{
	int rc;

	dav_tx_commit_v2();
	rc = dav_tx_end_v2(data);

	return rc ? umem_tx_errno(rc) : 0;
}

static int
bmem_tx_stage_v2(void)
{
	return dav_tx_stage_v2();
}

static void
bmem_defer_free_v2(struct umem_instance *umm, umem_off_t off, void *act)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	dav_defer_free_v2(pop, umem_off2offset(off),
			(struct dav_action *)act);
}

static umem_off_t
bmem_reserve_v2(struct umem_instance *umm, void *act, size_t size, unsigned int type_num,
	     unsigned int mbkt_id)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;
	uint64_t   flags = DAV_EZONE_ID(mbkt_id);

	return dav_reserve_v2(pop, (struct dav_action *)act, size, type_num, flags);
}

static void
bmem_cancel_v2(struct umem_instance *umm, void *actv, int actv_cnt)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	dav_cancel_v2(pop, (struct dav_action *)actv, actv_cnt);
}

static int
bmem_tx_publish_v2(struct umem_instance *umm, void *actv, int actv_cnt)
{
	int	rc;

	rc = dav_tx_publish_v2((struct dav_action *)actv, actv_cnt);
	return rc ? umem_tx_errno(rc) : 0;
}

static void *
bmem_atomic_copy_v2(struct umem_instance *umm, void *dest, const void *src,
		 size_t len, enum acopy_hint hint)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	if (hint == UMEM_RESERVED_MEM) {
		memcpy(dest, src, len);
		return dest;
	} else { /* UMEM_COMMIT_IMMEDIATE */
		return dav_memcpy_persist_v2(pop, dest, src, len);
	}
}

static umem_off_t
bmem_atomic_alloc_v2(struct umem_instance *umm, size_t size, unsigned int type_num,
		  unsigned int mbkt_id)
{
	uint64_t off;
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;
	int rc;
	uint64_t   flags = DAV_EZONE_ID(mbkt_id);

	rc = dav_alloc_v2(pop, &off, size, type_num, flags, NULL, NULL);
	if (rc)
		return UMOFF_NULL;
	return off;
}

static int
bmem_atomic_free_v2(struct umem_instance *umm, umem_off_t umoff)
{
	if (!UMOFF_IS_NULL(umoff)) {
		uint64_t off = umem_off2offset(umoff);

		dav_free_v2((dav_obj_t *)umm->umm_pool->up_priv, off);
	}
	return 0;
}

static void
bmem_atomic_flush_v2(struct umem_instance *umm, void *addr, size_t len)
{
	/* NOP */
}

static uint32_t
bmem_allot_mb_evictable_v2(struct umem_instance *umm, int flags)
{
	dav_obj_t *pop = (dav_obj_t *)umm->umm_pool->up_priv;

	return dav_allot_mb_evictable_v2(pop, flags);
}

static umem_ops_t bmem_v2_ops = {
	.mo_tx_free            = bmem_tx_free_v2,
	.mo_tx_alloc           = bmem_tx_alloc_v2,
	.mo_tx_add             = bmem_tx_add_v2,
	.mo_tx_xadd            = bmem_tx_xadd_v2,
	.mo_tx_add_ptr         = bmem_tx_add_ptr_v2,
	.mo_tx_abort           = bmem_tx_abort_v2,
	.mo_tx_begin           = bmem_tx_begin_v2,
	.mo_tx_commit          = bmem_tx_commit_v2,
	.mo_tx_stage           = bmem_tx_stage_v2,
	.mo_reserve            = bmem_reserve_v2,
	.mo_defer_free         = bmem_defer_free_v2,
	.mo_cancel             = bmem_cancel_v2,
	.mo_tx_publish         = bmem_tx_publish_v2,
	.mo_atomic_copy        = bmem_atomic_copy_v2,
	.mo_atomic_alloc       = bmem_atomic_alloc_v2,
	.mo_atomic_free        = bmem_atomic_free_v2,
	.mo_atomic_flush       = bmem_atomic_flush_v2,
	.mo_allot_evictable_mb = bmem_allot_mb_evictable_v2,
	.mo_tx_add_callback    = umem_tx_add_cb,
};

int
umem_tx_errno(int err)
{
	if (err < 0) {
		if (err < -DER_ERR_GURT_BASE)
			return err; /* aborted by DAOS */

		D_ERROR("pmdk returned negative errno %d\n", err);
		err = -err;
	}

	if (err == ENOMEM) /* pmdk returns ENOMEM for out of space */
		err = ENOSPC;

	return daos_errno2der(err);
}
#endif

/* volatile memory operations */
static int
vmem_free(struct umem_instance *umm, umem_off_t umoff)
{
	free(umem_off2ptr(umm, umoff));

	return 0;
}

umem_off_t
vmem_alloc(struct umem_instance *umm, size_t size, uint64_t flags, unsigned int type_num,
	   unsigned int unused)
{
	return (uint64_t)((flags & UMEM_FLAG_ZERO) ?
			  calloc(1, size) : malloc(size));
}

static int
vmem_tx_add_callback(struct umem_instance *umm, struct umem_tx_stage_data *txd,
		     int stage, umem_tx_cb_t cb, void *data)
{
	if (cb == NULL)
		return -DER_INVAL;

	/*
	 * vmem doesn't support transaction, so we just execute the commit
	 * callback & end callback instantly and drop the abort callback.
	 */
	if (stage == UMEM_STAGE_ONCOMMIT || stage == UMEM_STAGE_NONE)
		cb(data, false);
	else if (stage == UMEM_STAGE_ONABORT)
		cb(data, true);
	else
		return -DER_INVAL;

	return 0;
}

static umem_ops_t	vmem_ops = {
	.mo_tx_free	= vmem_free,
	.mo_tx_alloc	= vmem_alloc,
	.mo_tx_add	= NULL,
	.mo_tx_abort	= NULL,
	.mo_tx_add_callback = vmem_tx_add_callback,
};

/** Unified memory class definition */
struct umem_class {
	umem_class_id_t           umc_id;
	umem_ops_t              *umc_ops;
	char                    *umc_name;
};

/** all defined memory classes */
static struct umem_class umem_class_defined[] = {
	{
		.umc_id		= UMEM_CLASS_VMEM,
		.umc_ops	= &vmem_ops,
		.umc_name	= "vmem",
	},
#ifdef DAOS_PMEM_BUILD
	{
		.umc_id		= UMEM_CLASS_PMEM,
		.umc_ops	= &pmem_ops,
		.umc_name	= "pmem",
	},
	{
		.umc_id		= UMEM_CLASS_BMEM,
		.umc_ops	= &bmem_ops,
		.umc_name	= "bmem",
	},
	{
		.umc_id		= UMEM_CLASS_BMEM_V2,
		.umc_ops	= &bmem_v2_ops,
		.umc_name	= "bmem_v2",
	},
	{
		.umc_id		= UMEM_CLASS_ADMEM,
		.umc_ops	= &ad_mem_ops,
		.umc_name	= "ad-hoc",
	},
#endif
	{
		.umc_id		= UMEM_CLASS_UNKNOWN,
		.umc_ops	= NULL,
		.umc_name	= "unknown",
	},
};

/** Workout the necessary offsets and base address for the pool */
static void
set_offsets(struct umem_instance *umm)
{
#ifdef DAOS_PMEM_BUILD
	PMEMobjpool		*pop;
	char			*root;
	PMEMoid			 root_oid;
	dav_obj_t		*dav_pop;
	struct ad_blob_handle	 bh;
#endif
	if (umm->umm_id == UMEM_CLASS_VMEM) {
		umm->umm_base = 0;
		umm->umm_pool_uuid_lo = 0;
		return;
	}

#ifdef DAOS_PMEM_BUILD
	switch (umm->umm_id) {
	case UMEM_CLASS_PMEM:
		pop = (PMEMobjpool *)umm->umm_pool->up_priv;

		root_oid = pmemobj_root(pop, 0);
		D_ASSERTF(!OID_IS_NULL(root_oid),
			  "You must call pmemobj_root before umem_class_init\n");

		root = pmemobj_direct(root_oid);

		umm->umm_pool_uuid_lo = root_oid.pool_uuid_lo;
		umm->umm_base = (uint64_t)root - root_oid.off;
		break;
	case UMEM_CLASS_BMEM:
		dav_pop = (dav_obj_t *)umm->umm_pool->up_priv;

		umm->umm_base = (uint64_t)dav_get_base_ptr(dav_pop);
		break;
	case UMEM_CLASS_BMEM_V2:
		dav_pop = (dav_obj_t *)umm->umm_pool->up_priv;

		umm->umm_base = (uint64_t)dav_get_base_ptr_v2(dav_pop);
		break;
	case UMEM_CLASS_ADMEM:
		bh.bh_blob = (struct ad_blob *)umm->umm_pool->up_priv;
		umm->umm_base = (uint64_t)ad_base(bh);
		break;
	default:
		D_ASSERTF(0, "bad umm->umm_id %d\n", umm->umm_id);
		break;
	}
#endif
}

/**
 * Instantiate a memory class \a umm by attributes in \a uma
 *
 * \param uma [IN]	Memory attributes to instantiate the memory class.
 * \param umm [OUT]	The instantiated memory class.
 */
int
umem_class_init(struct umem_attr *uma, struct umem_instance *umm)
{
	struct umem_class *umc;
	bool		   found;

	found = false;
	for (umc = &umem_class_defined[0];
	     umc->umc_id != UMEM_CLASS_UNKNOWN; umc++) {
		if (umc->umc_id == uma->uma_id) {
			found = true;
			break;
		}
	}
	if (!found) {
		D_DEBUG(DB_MEM, "Cannot find memory class %d\n", uma->uma_id);
		return -DER_ENOENT;
	}

	umm->umm_id		= umc->umc_id;
	umm->umm_ops		= umc->umc_ops;
	umm->umm_name		= umc->umc_name;
	umm->umm_pool		= uma->uma_pool;
	umm->umm_nospc_rc	= umc->umc_id == UMEM_CLASS_VMEM ?
		-DER_NOMEM : -DER_NOSPACE;

	set_offsets(umm);

	D_DEBUG(DB_MEM, "Instantiate memory class %s id=%d nospc_rc=%d pool=%p pool_uuid_lo="DF_X64
		" base="DF_X64"\n", umc->umc_name, umm->umm_id, umm->umm_nospc_rc, umm->umm_pool,
		umm->umm_pool_uuid_lo, umm->umm_base);

	return 0;
}

/**
 * Get attributes of a memory class instance.
 */
void
umem_attr_get(struct umem_instance *umm, struct umem_attr *uma)
{
	uma->uma_id = umm->umm_id;
	uma->uma_pool = umm->umm_pool;
}

/*
 * To avoid allocating stage data for each transaction, umem user should
 * prepare per-xstream stage data and initialize it by umem_init_txd(),
 * this per-xstream stage data will be used for all transactions within
 * the same xstream.
 */
int
umem_init_txd(struct umem_tx_stage_data *txd)
{
	D_ASSERT(txd != NULL);
	txd->txd_magic = UMEM_TX_DATA_MAGIC;

	D_ALLOC_ARRAY(txd->txd_commit_vec, TXD_CB_NUM);
	txd->txd_commit_max = txd->txd_commit_vec != NULL ? TXD_CB_NUM : 0;
	txd->txd_commit_cnt = 0;

	D_ALLOC_ARRAY(txd->txd_abort_vec, TXD_CB_NUM);
	txd->txd_abort_max = txd->txd_abort_vec != NULL ? TXD_CB_NUM : 0;
	txd->txd_abort_cnt = 0;

	D_ALLOC_ARRAY(txd->txd_end_vec, TXD_CB_NUM);
	txd->txd_end_max = txd->txd_end_vec != NULL ? TXD_CB_NUM : 0;
	txd->txd_end_cnt = 0;

	if (txd->txd_commit_vec != NULL &&
	    txd->txd_abort_vec  != NULL &&
	    txd->txd_end_vec    != NULL)
		return 0;

	umem_fini_txd(txd);
	return -DER_NOMEM;
}

void
umem_fini_txd(struct umem_tx_stage_data *txd)
{
	D_ASSERT(txd != NULL);
	D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);

	D_ASSERT(txd->txd_commit_cnt == 0);
	D_ASSERT(txd->txd_abort_cnt == 0);
	D_ASSERT(txd->txd_end_cnt == 0);

	if (txd->txd_commit_max) {
		D_ASSERT(txd->txd_commit_vec != NULL);
		D_FREE(txd->txd_commit_vec);
		txd->txd_commit_vec = NULL;
		txd->txd_commit_max = 0;
	}

	if (txd->txd_abort_max) {
		D_ASSERT(txd->txd_abort_vec != NULL);
		D_FREE(txd->txd_abort_vec);
		txd->txd_abort_vec = NULL;
		txd->txd_abort_max = 0;
	}

	if (txd->txd_end_max) {
		D_ASSERT(txd->txd_end_vec != NULL);
		D_FREE(txd->txd_end_vec);
		txd->txd_end_vec = NULL;
		txd->txd_end_max = 0;
	}
}

#ifdef	DAOS_PMEM_BUILD

struct umem_rsrvd_act {
	unsigned int		 rs_actv_cnt;
	unsigned int		 rs_actv_at;
	/* "struct pobj_action" or "struct ad_reserv_act", "struct dav_action" type array */
	void			*rs_actv;
};

static size_t
umem_rsrvd_item_size(struct umem_instance *umm)
{
	switch (umm->umm_id) {
	case UMEM_CLASS_PMEM:
		return sizeof(struct pobj_action);
	case UMEM_CLASS_ADMEM:
		return sizeof(struct ad_reserv_act);
	case UMEM_CLASS_BMEM:
	case UMEM_CLASS_BMEM_V2:
		return sizeof(struct dav_action);
	default:
		D_ERROR("bad umm_id %d\n", umm->umm_id);
		return 0;
	};
	return 0;
}

int
umem_rsrvd_act_cnt(struct umem_rsrvd_act *rsrvd_act)
{
	if (rsrvd_act == NULL)
		return 0;
	return rsrvd_act->rs_actv_at;
}

int
umem_rsrvd_act_alloc(struct umem_instance *umm, struct umem_rsrvd_act **rsrvd_act, int cnt)
{
	size_t	act_size = umem_rsrvd_item_size(umm);
	size_t	size;
	void	*buf;

	size = sizeof(struct umem_rsrvd_act) + act_size * cnt;
	D_ALLOC(buf, size);
	if (buf == NULL)
		return -DER_NOMEM;

	*rsrvd_act = buf;
	(*rsrvd_act)->rs_actv_cnt = cnt;
	(*rsrvd_act)->rs_actv = buf + sizeof(struct umem_rsrvd_act);
	return 0;
}

int
umem_rsrvd_act_realloc(struct umem_instance *umm, struct umem_rsrvd_act **rsrvd_act, int max_cnt)
{
	if (*rsrvd_act == NULL ||
	    (*rsrvd_act)->rs_actv_cnt < max_cnt) {
		struct umem_rsrvd_act	*tmp_rsrvd_act;
		size_t			 act_size = umem_rsrvd_item_size(umm);
		size_t			 size;

		size = sizeof(struct umem_rsrvd_act) + act_size * max_cnt;

		D_REALLOC_Z(tmp_rsrvd_act, *rsrvd_act, size);
		if (tmp_rsrvd_act == NULL)
			return -DER_NOMEM;

		*rsrvd_act = tmp_rsrvd_act;
		(*rsrvd_act)->rs_actv_cnt = max_cnt;
		(*rsrvd_act)->rs_actv = (void *)&tmp_rsrvd_act[1];
	}
	return 0;
}

int
umem_rsrvd_act_free(struct umem_rsrvd_act **rsrvd_act)
{
	D_FREE(*rsrvd_act);
	return 0;
}

umem_off_t
umem_reserve_common(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act, size_t size,
		    unsigned int mbkt_id)
{
	if (umm->umm_ops->mo_reserve) {
		void			*act;
		size_t			 act_size = umem_rsrvd_item_size(umm);
		umem_off_t		 off;

		D_ASSERT(rsrvd_act != NULL);
		D_ASSERT(rsrvd_act->rs_actv_cnt > rsrvd_act->rs_actv_at);

		act = rsrvd_act->rs_actv + act_size * rsrvd_act->rs_actv_at;
		off = umm->umm_ops->mo_reserve(umm, act, size, UMEM_TYPE_ANY, mbkt_id);
		if (!UMOFF_IS_NULL(off))
			rsrvd_act->rs_actv_at++;
		D_ASSERTF(umem_off2flags(off) == 0,
			  "Invalid assumption about allocnot using flag bits");
		D_DEBUG(DB_MEM,
			"reserve %s umoff=" UMOFF_PF " size=%zu base=" DF_X64
			" pool_uuid_lo=" DF_X64 "\n",
			(umm)->umm_name, UMOFF_P(off), (size_t)(size),
			(umm)->umm_base, (umm)->umm_pool_uuid_lo);
		return off;
	}
	return UMOFF_NULL;
}

void
umem_defer_free(struct umem_instance *umm, umem_off_t off,
		struct umem_rsrvd_act *rsrvd_act)
{
	D_ASSERT(rsrvd_act->rs_actv_at < rsrvd_act->rs_actv_cnt);
	D_DEBUG(DB_MEM,
		"Defer free %s umoff=" UMOFF_PF "base=" DF_X64
		" pool_uuid_lo=" DF_X64 "\n",
		(umm)->umm_name, UMOFF_P(off), (umm)->umm_base,
		(umm)->umm_pool_uuid_lo);
	if (umm->umm_ops->mo_defer_free) {
		void		*act;
		size_t		 act_size = umem_rsrvd_item_size(umm);

		act = rsrvd_act->rs_actv + act_size * rsrvd_act->rs_actv_at;
		umm->umm_ops->mo_defer_free(umm, off, act);
		rsrvd_act->rs_actv_at++;
	} else {
		/** Go ahead and free immediately.  The purpose of this
		 * function is to allow reserve/publish pair to execute
		 * on commit
		 */
		umem_free(umm, off);
	}
}

void
umem_cancel(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act)
{
	if (rsrvd_act == NULL || rsrvd_act->rs_actv_at == 0)
		return;
	D_ASSERT(rsrvd_act->rs_actv_at <= rsrvd_act->rs_actv_cnt);
	if (umm->umm_ops->mo_cancel)
		umm->umm_ops->mo_cancel(umm, rsrvd_act->rs_actv, rsrvd_act->rs_actv_at);
	rsrvd_act->rs_actv_at = 0;
}

int
umem_tx_publish(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act)
{
	int rc = 0;

	if (rsrvd_act == NULL || rsrvd_act->rs_actv_at == 0)
		return 0;
	D_ASSERT(rsrvd_act->rs_actv_at <= rsrvd_act->rs_actv_cnt);
	if (umm->umm_ops->mo_tx_publish)
		rc = umm->umm_ops->mo_tx_publish(umm, rsrvd_act->rs_actv, rsrvd_act->rs_actv_at);
	rsrvd_act->rs_actv_at = 0;
	return rc;
}

/* Memory page */
struct umem_page_info {
	/** Mapped MD page ID */
	uint32_t pi_pg_id;
	/** Reference count */
	uint32_t pi_ref;
	/** Page flags */
	uint64_t pi_io		: 1, /** Page is being flushed/loaded to/from MD-blob */
		 pi_copying	: 1, /** Page is being copied. Blocks writes. */
		 pi_mapped	: 1, /** Page is mapped to a MD page */
		 pi_sys		: 1, /** Page is brought to cache by system internal access */
		 pi_loaded	: 1, /** Page is loaded */
		 pi_evictable	: 1; /** Last known state on whether the page is evictable */
	/** Highest transaction ID checkpointed.  This is set before the page is copied. The
	 *  checkpoint will not be executed until the last committed ID is greater than or
	 *  equal to this value.  If that's not the case immediately, the waiting flag is set
	 *  along with this field.
	 */
	uint64_t pi_last_checkpoint;
	/** Highest transaction ID of writes to the page */
	uint64_t pi_last_inflight;
	/** link to global LRU lists, or global free page list, or global pinned list */
	d_list_t pi_lru_link;
	/** link to global dirty page list, or wait commit list, or temporary list for flushing */
	d_list_t pi_dirty_link;
	/** link to global flushing page list */
	d_list_t pi_flush_link;
	/** Waitqueue for page loading/flushing */
	void	*pi_io_wq;
	/** Waitqueue for page committing */
	void	*pi_commit_wq;
	/** page memory address */
	uint8_t *pi_addr;
	/** Information about in-flight checkpoint */
	void    *pi_chkpt_data;
	/** bitmap for each dirty 4K unit */
	uint64_t *pi_bmap;
};

/* Convert page ID to MD-blob offset */
static inline umem_off_t
cache_id2off(struct umem_cache *cache, uint32_t pg_id)
{
	return ((umem_off_t)pg_id << cache->ca_page_shift) + cache->ca_base_off;
}

/* Convert MD-blob offset to page ID */
static inline uint32_t
cache_off2id(struct umem_cache *cache, umem_off_t offset)
{
	D_ASSERT(offset >= cache->ca_base_off);
	return (offset - cache->ca_base_off) >> cache->ca_page_shift;
}

/* Convert MD-blob offset to MD page */
static inline struct umem_page *
cache_off2page(struct umem_cache *cache, umem_off_t offset)
{
	uint32_t idx = cache_off2id(cache, offset);

	D_ASSERTF(idx < cache->ca_md_pages, "offset=" DF_U64 ", md_pages=%u, idx=%u\n",
		  offset, cache->ca_md_pages, idx);

	return &cache->ca_pages[idx];
}

/* Convert memory pointer to memory page */
static inline struct umem_page_info *
cache_ptr2pinfo(struct umem_cache *cache, const void *ptr)
{
	struct umem_page_info	*pinfo;
	uint32_t idx;

	D_ASSERT(ptr >= cache->ca_base);
	idx = (ptr - cache->ca_base) >> cache->ca_page_shift;

	D_ASSERTF(idx < cache->ca_mem_pages, "ptr=%p, md_pages=%u, idx=%u\n",
		  ptr, cache->ca_mem_pages, idx);
	pinfo = (struct umem_page_info *)&cache->ca_pages[cache->ca_md_pages];

	return &pinfo[idx];
}

/* Convert MD-blob offset to page offset */
static inline uint32_t
cache_off2pg_off(struct umem_cache *cache, umem_off_t offset)
{
	D_ASSERT(offset >= cache->ca_base_off);
	return (offset - cache->ca_base_off) & cache->ca_page_mask;
}

bool
umem_cache_offisloaded(struct umem_store *store, umem_off_t offset)
{
	struct umem_cache *cache = store->cache;
	struct umem_page  *page  = cache_off2page(cache, offset);

	return ((page->pg_info != NULL) && page->pg_info->pi_loaded);
}

/* Convert MD-blob offset to memory pointer */
void *
umem_cache_off2ptr(struct umem_store *store, umem_off_t offset)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page	*page = cache_off2page(cache, offset);

	/* The page must be mapped */
	D_ASSERT(page->pg_info != NULL);
	return (void *)(page->pg_info->pi_addr + cache_off2pg_off(cache, offset));
}

/* Convert memory pointer to MD-blob offset */
umem_off_t
umem_cache_ptr2off(struct umem_store *store, const void *ptr)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page_info	*pinfo = cache_ptr2pinfo(cache, ptr);
	umem_off_t		 offset;

	/* The page must be mapped */
	D_ASSERT(pinfo->pi_mapped);
	offset = cache_id2off(cache, pinfo->pi_pg_id);
	offset += (ptr - cache->ca_base) & cache->ca_page_mask;

	return offset;
}

static int
page_waitqueue_create(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct umem_store	*store = cache->ca_store;
	int			 rc;

	D_ASSERT(store->stor_ops->so_waitqueue_create != NULL);
	if (pinfo->pi_io_wq == NULL) {
		rc = store->stor_ops->so_waitqueue_create(&pinfo->pi_io_wq);
		if (rc)
			return rc;
	}
	if (pinfo->pi_commit_wq == NULL) {
		rc = store->stor_ops->so_waitqueue_create(&pinfo->pi_commit_wq);
		if (rc)
			return rc;
	}

	return 0;
}

static void
page_waitqueue_destroy(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct umem_store	*store = cache->ca_store;

	if (pinfo->pi_io_wq != NULL) {
		store->stor_ops->so_waitqueue_destroy(pinfo->pi_io_wq);
		pinfo->pi_io_wq = NULL;
	}
	if (pinfo->pi_commit_wq != NULL) {
		store->stor_ops->so_waitqueue_destroy(pinfo->pi_commit_wq);
		pinfo->pi_commit_wq = NULL;
	}
}

static inline void
verify_inactive_page(struct umem_page_info *pinfo)
{
	D_ASSERT(d_list_empty(&pinfo->pi_flush_link));
	D_ASSERT(pinfo->pi_ref == 0);
	D_ASSERT(pinfo->pi_io == 0);
	D_ASSERT(pinfo->pi_copying == 0);
}

static inline void
verify_clean_page(struct umem_page_info *pinfo, int mapped)
{
	D_ASSERT(d_list_empty(&pinfo->pi_lru_link));
	D_ASSERT(d_list_empty(&pinfo->pi_dirty_link));
	D_ASSERT(pinfo->pi_mapped == mapped);
	verify_inactive_page(pinfo);
}

int
umem_cache_free(struct umem_store *store)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page_info	*pinfo;
	int			 i;

	if (cache == NULL)
		return 0;

	D_ASSERT(d_list_empty(&cache->ca_pgs_flushing));
	D_ASSERT(d_list_empty(&cache->ca_pgs_wait_commit));
	D_ASSERT(d_list_empty(&cache->ca_pgs_pinned));
	D_ASSERT(cache->ca_pgs_stats[UMEM_PG_STATS_PINNED] == 0);
	D_ASSERT(cache->ca_reserve_waiters == 0);

	pinfo = (struct umem_page_info *)&cache->ca_pages[cache->ca_md_pages];
	for (i = 0; i < cache->ca_mem_pages; i++) {
		verify_inactive_page(pinfo);

		page_waitqueue_destroy(store->cache, pinfo);
		pinfo++;
	}

	if (cache->ca_reserve_wq != NULL) {
		store->stor_ops->so_waitqueue_destroy(cache->ca_reserve_wq);
		cache->ca_reserve_wq = NULL;

	}

	D_FREE(store->cache);
	return 0;
}

/* 1: phase I mode; 2: phase II mode; */
static inline unsigned int
cache_mode(struct umem_cache *cache)
{
	return cache->ca_mode;
}

static inline struct umem_page_info *
cache_pop_free_page(struct umem_cache *cache)
{
	struct umem_page_info	*pinfo;

	pinfo = d_list_pop_entry(&cache->ca_pgs_free, struct umem_page_info, pi_lru_link);
	if (pinfo != NULL) {
		D_ASSERT(cache->ca_pgs_stats[UMEM_PG_STATS_FREE] > 0);
		cache->ca_pgs_stats[UMEM_PG_STATS_FREE] -= 1;
	}
	return pinfo;
}

#define UMEM_CHUNK_IDX_SHIFT 6
#define UMEM_CHUNK_IDX_BITS  (1 << UMEM_CHUNK_IDX_SHIFT)
#define UMEM_CHUNK_IDX_MASK  (UMEM_CHUNK_IDX_BITS - 1)

#define UMEM_CACHE_PAGE_SHIFT_MAX 27	/* 128MB */
#define UMEM_CACHE_BMAP_SZ_MAX    (1 << (UMEM_CACHE_PAGE_SHIFT_MAX - \
					UMEM_CACHE_CHUNK_SZ_SHIFT - UMEM_CHUNK_IDX_SHIFT))
#define UMEM_CACHE_RSRVD_PAGES	4

int
umem_cache_alloc(struct umem_store *store, uint32_t page_sz, uint32_t md_pgs, uint32_t mem_pgs,
		 uint32_t max_ne_pgs, uint32_t base_off, void *base,
		 bool (*is_evictable_fn)(void *arg, uint32_t pg_id),
		 int (*evtcb_fn)(int evt_type, void *arg, uint32_t pg_id), void *fn_arg)
{
	struct umem_cache	*cache;
	struct umem_page_info	*pinfo;
	struct umem_page	*page;
	unsigned int		 page_shift, bmap_sz;
	uint64_t		*bmap;
	void			*cur_addr = base;
	int			 idx, cmode = 1, rc = 0;

	D_ASSERT(store != NULL);
	D_ASSERT(base != NULL);

	page_shift = __builtin_ctz(page_sz);
	if (page_sz != (1 << page_shift)) {
		D_ERROR("Page size (%u) isn't aligned.\n", page_sz);
		return -DER_INVAL;
	} else if (page_shift > UMEM_CACHE_PAGE_SHIFT_MAX) {
		D_ERROR("Page size (%u) > Max page size (%u).\n",
			page_sz, 1 << UMEM_CACHE_PAGE_SHIFT_MAX);
		return -DER_INVAL;
	} else if (page_shift <= (UMEM_CACHE_CHUNK_SZ_SHIFT + UMEM_CHUNK_IDX_SHIFT)) {
		D_ERROR("Page size (%u) <= Min page size (%u)\n",
			page_sz, 1 << (UMEM_CACHE_CHUNK_SZ_SHIFT + UMEM_CHUNK_IDX_SHIFT));
		return -DER_INVAL;
	}

	D_ASSERT(md_pgs > 0 && md_pgs >= mem_pgs);
	if (mem_pgs == 0) {	/* Phase 1 mode */
		mem_pgs = md_pgs;
		max_ne_pgs = md_pgs;
	} else
		cmode = 2;

	bmap_sz = (1 << (page_shift - UMEM_CACHE_CHUNK_SZ_SHIFT - UMEM_CHUNK_IDX_SHIFT));

	D_ALLOC(cache, sizeof(*cache) + sizeof(cache->ca_pages[0]) * md_pgs +
			   sizeof(cache->ca_pages[0].pg_info[0]) * mem_pgs +
			   bmap_sz * sizeof(uint64_t) * mem_pgs);
	if (cache == NULL)
		return -DER_NOMEM;

	D_DEBUG(DB_IO, "Allocated page cache, md-pages(%u), mem-pages(%u), max-ne-pages(%u) %p\n",
		md_pgs, mem_pgs, max_ne_pgs, cache);

	cache->ca_store		= store;
	cache->ca_base		= base;
	cache->ca_base_off	= base_off;
	cache->ca_md_pages	= md_pgs;
	cache->ca_mem_pages	= mem_pgs;
	cache->ca_max_ne_pages	= max_ne_pgs;
	cache->ca_page_sz	= page_sz;
	cache->ca_page_shift	= page_shift;
	cache->ca_page_mask	= page_sz - 1;
	cache->ca_bmap_sz	= bmap_sz;
	cache->ca_evictable_fn	= is_evictable_fn;
	cache->ca_evtcb_fn      = evtcb_fn;
	cache->ca_fn_arg        = fn_arg;
	cache->ca_mode          = cmode;

	D_INIT_LIST_HEAD(&cache->ca_pgs_free);
	D_INIT_LIST_HEAD(&cache->ca_pgs_dirty);
	D_INIT_LIST_HEAD(&cache->ca_pgs_lru[0]);
	D_INIT_LIST_HEAD(&cache->ca_pgs_lru[1]);
	D_INIT_LIST_HEAD(&cache->ca_pgs_flushing);
	D_INIT_LIST_HEAD(&cache->ca_pgs_wait_commit);
	D_INIT_LIST_HEAD(&cache->ca_pgs_pinned);

	pinfo = (struct umem_page_info *)&cache->ca_pages[md_pgs];
	bmap = (uint64_t *)&pinfo[mem_pgs];

	/* Initialize memory page array */
	for (idx = 0; idx < mem_pgs; idx++) {
		pinfo->pi_bmap = bmap;
		pinfo->pi_addr = (void *)cur_addr;
		D_INIT_LIST_HEAD(&pinfo->pi_dirty_link);
		D_INIT_LIST_HEAD(&pinfo->pi_flush_link);
		d_list_add_tail(&pinfo->pi_lru_link, &cache->ca_pgs_free);
		cache->ca_pgs_stats[UMEM_PG_STATS_FREE] += 1;

		pinfo++;
		bmap += bmap_sz;
		cur_addr += page_sz;
	}
	store->cache = cache;

	/* Phase 2 mode */
	if (cache_mode(cache) != 1) {
		D_ASSERT(store->stor_ops->so_waitqueue_create != NULL);
		rc = store->stor_ops->so_waitqueue_create(&cache->ca_reserve_wq);
		if (rc)
			goto error;

		pinfo = (struct umem_page_info *)&cache->ca_pages[cache->ca_md_pages];
		for (idx = 0; idx < cache->ca_mem_pages; idx++) {
			rc = page_waitqueue_create(cache, pinfo);
			if (rc)
				goto error;
			pinfo++;
		}
		return 0;
	}

	/* Map all MD pages to memory pages for phase 1 mode */
	for (idx = 0; idx < md_pgs; idx++) {
		pinfo = cache_pop_free_page(cache);
		D_ASSERT(pinfo != NULL);
		D_ASSERT(pinfo->pi_addr == (base + (uint64_t)idx * page_sz));
		pinfo->pi_pg_id = idx;
		pinfo->pi_mapped = 1;
		pinfo->pi_loaded = 1;

		page = &cache->ca_pages[idx];
		D_ASSERT(page->pg_info == NULL);
		page->pg_info  = pinfo;

		/* Add to non-evictable LRU */
		cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] += 1;
		d_list_add_tail(&pinfo->pi_lru_link, &cache->ca_pgs_lru[0]);
	}

	return 0;
error:
	umem_cache_free(store);
	return rc;
}

static inline bool
is_id_evictable(struct umem_cache *cache, uint32_t pg_id)
{
	return cache->ca_evictable_fn && cache->ca_evictable_fn(cache->ca_fn_arg, pg_id);
}

static inline void
cache_push_free_page(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	d_list_add_tail(&pinfo->pi_lru_link, &cache->ca_pgs_free);
	cache->ca_pgs_stats[UMEM_PG_STATS_FREE] += 1;
}

static inline void
cache_unmap_page(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	verify_clean_page(pinfo, 1);
	D_ASSERT(pinfo->pi_pg_id < cache->ca_md_pages);
	D_ASSERT(cache->ca_pages[pinfo->pi_pg_id].pg_info == pinfo);

	pinfo->pi_mapped = 0;
	pinfo->pi_loaded = 0;
	pinfo->pi_last_inflight                  = 0;
	pinfo->pi_last_checkpoint                = 0;
	cache->ca_pages[pinfo->pi_pg_id].pg_info = NULL;

	cache_push_free_page(cache, pinfo);

	if (!is_id_evictable(cache, pinfo->pi_pg_id)) {
		D_ASSERT(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] > 0);
		cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] -= 1;
	}
}

static inline void
cache_map_page(struct umem_cache *cache, struct umem_page_info *pinfo, unsigned int pg_id)
{
	verify_clean_page(pinfo, 0);
	D_ASSERT(pinfo->pi_loaded == 0);

	pinfo->pi_mapped = 1;
	pinfo->pi_pg_id = pg_id;
	cache->ca_pages[pg_id].pg_info = pinfo;
	pinfo->pi_evictable            = is_id_evictable(cache, pg_id);
	if (!pinfo->pi_evictable)
		cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] += 1;
}

static inline void
cache_add2lru(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	D_ASSERT(d_list_empty(&pinfo->pi_lru_link));
	D_ASSERT(pinfo->pi_ref == 0);

	if (pinfo->pi_evictable)
		d_list_add_tail(&pinfo->pi_lru_link, &cache->ca_pgs_lru[1]);
	else
		d_list_add_tail(&pinfo->pi_lru_link, &cache->ca_pgs_lru[0]);
}

static inline void
cache_unpin_page(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	D_ASSERT(pinfo->pi_ref > 0);
	pinfo->pi_ref--;

	if (pinfo->pi_ref == 0) {
		d_list_del_init(&pinfo->pi_lru_link);
		cache_add2lru(cache, pinfo);
		if (is_id_evictable(cache, pinfo->pi_pg_id)) {
			D_ASSERT(cache->ca_pgs_stats[UMEM_PG_STATS_PINNED] > 0);
			cache->ca_pgs_stats[UMEM_PG_STATS_PINNED] -= 1;
		}
	}
}

static inline void
cache_pin_page(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	pinfo->pi_ref++;
	if (pinfo->pi_ref == 1) {
		d_list_del_init(&pinfo->pi_lru_link);
		d_list_add_tail(&pinfo->pi_lru_link, &cache->ca_pgs_pinned);
		if (is_id_evictable(cache, pinfo->pi_pg_id))
			cache->ca_pgs_stats[UMEM_PG_STATS_PINNED] += 1;
	}
}

static inline void
page_wait_io(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct umem_store	*store = cache->ca_store;

	D_ASSERT(pinfo->pi_io == 1);
	if (store->stor_ops->so_waitqueue_create == NULL)
		return;

	D_ASSERT(store->stor_ops->so_waitqueue_wait != NULL);
	D_ASSERT(pinfo->pi_io_wq != NULL);
	store->stor_ops->so_waitqueue_wait(pinfo->pi_io_wq, false);
}

static inline void
page_wait_committed(struct umem_cache *cache, struct umem_page_info *pinfo, bool yield_only)
{
	struct umem_store	*store = cache->ca_store;

	/* The page is must in flushing */
	D_ASSERT(pinfo->pi_io == 1);
	if (store->stor_ops->so_waitqueue_create == NULL)
		return;

	D_ASSERT(store->stor_ops->so_waitqueue_wait != NULL);
	D_ASSERT(pinfo->pi_commit_wq != NULL);
	store->stor_ops->so_waitqueue_wait(pinfo->pi_commit_wq, yield_only);
}

static inline void
page_wakeup_io(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct umem_store	*store = cache->ca_store;

	D_ASSERT(pinfo->pi_io == 0);
	if (store->stor_ops->so_waitqueue_create == NULL)
		return;

	if (cache_mode(cache) == 1)
		return;

	D_ASSERT(store->stor_ops->so_waitqueue_wakeup != NULL);
	D_ASSERT(pinfo->pi_io_wq != NULL);
	store->stor_ops->so_waitqueue_wakeup(pinfo->pi_io_wq, true);
}

static inline void
page_wakeup_commit(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct umem_store	*store = cache->ca_store;

	/* The page is must in flushing */
	D_ASSERT(pinfo->pi_io == 1);
	if (store->stor_ops->so_waitqueue_create == NULL)
		return;

	D_ASSERT(store->stor_ops->so_waitqueue_wakeup != NULL);
	D_ASSERT(pinfo->pi_commit_wq != NULL);
	store->stor_ops->so_waitqueue_wakeup(pinfo->pi_commit_wq, true);
}

static inline bool
is_page_dirty(struct umem_page_info *pinfo)
{
	return (pinfo->pi_last_inflight != pinfo->pi_last_checkpoint);
}

static inline void
touch_page(struct umem_store *store, struct umem_page_info *pinfo, uint64_t wr_tx,
	   umem_off_t first_byte, umem_off_t last_byte)
{
	struct umem_cache *cache = store->cache;
	uint64_t start_bit = (first_byte & cache->ca_page_mask) >> UMEM_CACHE_CHUNK_SZ_SHIFT;
	uint64_t end_bit   = (last_byte & cache->ca_page_mask) >> UMEM_CACHE_CHUNK_SZ_SHIFT;
	uint64_t bit_nr;
	uint64_t bit;
	uint64_t idx;

	D_ASSERT(wr_tx != -1ULL);
	D_ASSERTF(store->stor_ops->so_wal_id_cmp(store, wr_tx, pinfo->pi_last_inflight) >= 0,
		  "cur_tx:"DF_U64" < last_inflight:"DF_U64"\n", wr_tx, pinfo->pi_last_inflight);
	D_ASSERTF(pinfo->pi_last_checkpoint == 0 ||
		  store->stor_ops->so_wal_id_cmp(store, wr_tx, pinfo->pi_last_checkpoint) > 0,
		  "cur_tx:"DF_U64" <= last_checkpoint:"DF_U64"\n",
		  wr_tx, pinfo->pi_last_checkpoint);

	for (bit_nr = start_bit; bit_nr <= end_bit; bit_nr++) {
		idx = bit_nr >> UMEM_CHUNK_IDX_SHIFT; /** uint64_t index */
		bit = bit_nr & UMEM_CHUNK_IDX_MASK;
		pinfo->pi_bmap[idx] |= 1ULL << bit;
	}

	D_ASSERT(pinfo->pi_loaded == 1);
	pinfo->pi_last_inflight = wr_tx;

	/* Don't change the pi_dirty_link while the page is being flushed */
	if (!d_list_empty(&pinfo->pi_flush_link))
		return;

	D_ASSERT(pinfo->pi_io == 0);
	if (d_list_empty(&pinfo->pi_dirty_link))
		d_list_add_tail(&pinfo->pi_dirty_link, &cache->ca_pgs_dirty);
}

/* Convert MD-blob offset to memory page */
static inline struct umem_page_info *
cache_off2pinfo(struct umem_cache *cache, umem_off_t addr)
{
	struct umem_page *page = cache_off2page(cache, addr);

	D_ASSERT(page->pg_info != NULL);
	return page->pg_info;
}

int
umem_cache_touch(struct umem_store *store, uint64_t wr_tx, umem_off_t addr, daos_size_t size)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page_info	*pinfo;
	umem_off_t		 start_addr, end_addr = addr + size - 1;
	struct umem_page_info	*end_pinfo;

	if (cache == NULL)
		return 0; /* TODO: When SMD is supported outside VOS, this will be an error */

	D_ASSERTF(size <= cache->ca_page_sz, "size=" DF_U64 "\n", size);
	pinfo     = cache_off2pinfo(cache, addr);
	end_pinfo = cache_off2pinfo(cache, end_addr);

	if (pinfo->pi_copying)
		return -DER_CHKPT_BUSY;

	/* Convert the MD-blob offset to umem cache offset (exclude the allocator header) */
	D_ASSERT(addr >= cache->ca_base_off);
	addr -= cache->ca_base_off;
	end_addr -= cache->ca_base_off;

	if (pinfo != end_pinfo) {
		D_ASSERT(cache_mode(cache) == 1);

		if (end_pinfo->pi_copying)
			return -DER_CHKPT_BUSY;

		start_addr = end_addr & ~cache->ca_page_mask;
		touch_page(store, end_pinfo, wr_tx, start_addr, end_addr);
		end_addr = start_addr - 1;
	}

	touch_page(store, pinfo, wr_tx, addr, end_addr);

	return 0;
}

/** Maximum number of sets of pages in-flight at a time */
#define MAX_INFLIGHT_SETS 4
/** Maximum contiguous range to checkpoint */
#define MAX_IO_SIZE       (8 * 1024 * 1024)
/** Maximum number of pages that can be in one set */
#define MAX_PAGES_PER_SET 10
/** Maximum number of ranges that can be in one page */
#define MAX_IOD_PER_PAGE  ((UMEM_CACHE_BMAP_SZ_MAX << UMEM_CHUNK_IDX_SHIFT) / 2)
/** Maximum number of IODs a set can handle */
#define MAX_IOD_PER_SET   (2 * MAX_IOD_PER_PAGE)

struct umem_checkpoint_data {
	/** List link for in-flight sets */
	d_list_t                 cd_link;
	/* List of storage ranges being checkpointed */
	struct umem_store_iod    cd_store_iod;
	/** Ranges in the storage to checkpoint */
	struct umem_store_region cd_regions[MAX_IOD_PER_SET];
	/** list of cached page ranges to checkpoint */
	d_sg_list_t              cd_sg_list;
	/** Ranges in memory to checkpoint */
	d_iov_t                  cd_iovs[MAX_IOD_PER_SET];
	/** Handle for the underlying I/O operations */
	daos_handle_t            cd_fh;
	/** Pointer to pages in set */
	struct umem_page_info   *cd_pages[MAX_PAGES_PER_SET];
	/** Highest transaction ID for pages in set */
	uint64_t                 cd_max_tx;
	/** Number of pages included in the set */
	uint32_t                 cd_nr_pages;
	/** Number of dirty chunks included in the set */
	uint32_t                 cd_nr_dchunks;
};

static void
page2chkpt(struct umem_store *store, struct umem_page_info *pinfo,
	   struct umem_checkpoint_data *chkpt_data)
{
	struct umem_cache     *cache     = store->cache;
	uint64_t              *bits      = pinfo->pi_bmap;
	struct umem_store_iod *store_iod = &chkpt_data->cd_store_iod;
	d_sg_list_t           *sgl       = &chkpt_data->cd_sg_list;
	uint64_t               bmap;
	int       i;
	uint64_t               first_bit_shift;
	uint64_t               offset = cache_id2off(cache, pinfo->pi_pg_id);
	uint64_t               map_offset;
	uint8_t               *page_addr = pinfo->pi_addr;
	int                    nr        = sgl->sg_nr_out;
	int                    count;
	uint64_t               mask;
	uint64_t               bit;

	pinfo->pi_chkpt_data                            = chkpt_data;
	chkpt_data->cd_pages[chkpt_data->cd_nr_pages++] = pinfo;
	if (store->stor_ops->so_wal_id_cmp(store, chkpt_data->cd_max_tx, pinfo->pi_last_inflight) <
	    0)
		chkpt_data->cd_max_tx = pinfo->pi_last_inflight;

	for (i = 0; i < cache->ca_bmap_sz; i++) {
		if (bits[i] == 0)
			goto next_bmap;

		bmap = bits[i];
		do {
			first_bit_shift = __builtin_ctzll(bmap);
			map_offset      = first_bit_shift << UMEM_CACHE_CHUNK_SZ_SHIFT;
			count      = 0;
			mask       = 0;
			while (first_bit_shift != 64) {
				bit = 1ULL << first_bit_shift;
				if ((bmap & bit) == 0)
					break;
				mask |= bit;
				count++;
				first_bit_shift++;
				if ((count << UMEM_CACHE_CHUNK_SZ_SHIFT) == MAX_IO_SIZE)
					break;
			}

			store_iod->io_regions[nr].sr_addr = offset + map_offset;
			store_iod->io_regions[nr].sr_size = count << UMEM_CACHE_CHUNK_SZ_SHIFT;
			sgl->sg_iovs[nr].iov_len          = sgl->sg_iovs[nr].iov_buf_len =
			    count << UMEM_CACHE_CHUNK_SZ_SHIFT;
			sgl->sg_iovs[nr].iov_buf = page_addr + map_offset;
			chkpt_data->cd_nr_dchunks += count;
			nr++;

			bmap &= ~mask;
		} while (bmap != 0);

next_bmap:
		offset += UMEM_CACHE_CHUNK_SZ << UMEM_CHUNK_IDX_SHIFT;
		page_addr += UMEM_CACHE_CHUNK_SZ << UMEM_CHUNK_IDX_SHIFT;
	}
	sgl->sg_nr_out = sgl->sg_nr = nr;
	store_iod->io_nr            = nr;
	/** Presently, the flush API can yield. Yielding is fine but ideally,
	 *  we would like it to fail in such cases so we can re-run the checkpoint
	 *  creation for the pages in the set. As it stands, we must set the copying
	 *  bit here to avoid changes to the page.
	 */
	pinfo->pi_copying = 1;
}

/** This is O(n) but the list is tiny so let's keep it simple */
static void
chkpt_insert_sorted(struct umem_store *store, struct umem_checkpoint_data *chkpt_data,
		    d_list_t *list)
{
	struct umem_checkpoint_data *other;

	d_list_for_each_entry(other, list, cd_link) {
		if (store->stor_ops->so_wal_id_cmp(store, chkpt_data->cd_max_tx, other->cd_max_tx) <
		    0) {
			d_list_add(&chkpt_data->cd_link, &other->cd_link);
			return;
		}
	}

	d_list_add_tail(&chkpt_data->cd_link, list);
}

static void
page_flush_completion(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	D_ASSERT(d_list_empty(&pinfo->pi_dirty_link));
	D_ASSERT(pinfo->pi_io == 1);
	pinfo->pi_io = 0;
	D_ASSERT(!d_list_empty(&pinfo->pi_flush_link));
	d_list_del_init(&pinfo->pi_flush_link);

	if (is_page_dirty(pinfo))
		d_list_add_tail(&pinfo->pi_dirty_link, &cache->ca_pgs_dirty);

	page_wakeup_io(cache, pinfo);
}

static int
cache_flush_pages(struct umem_cache *cache, d_list_t *dirty_list,
		  struct umem_checkpoint_data *chkpt_data_all, int chkpt_nr,
		  umem_cache_wait_cb_t wait_commit_cb, void *arg, uint64_t *chkpt_id,
		  struct umem_cache_chkpt_stats *stats)
{
	struct umem_store		*store = cache->ca_store;
	struct umem_checkpoint_data	*chkpt_data;
	struct umem_page_info		*pinfo;
	d_list_t			 free_list;
	d_list_t			 waiting_list;
	uint64_t			 committed_tx = 0;
	unsigned int			 max_iod_per_page;
	unsigned int			 tot_pgs = 0, flushed_pgs = 0;
	int				 inflight = 0;
	int				 i, rc = 0;

	D_ASSERT(store != NULL);
	D_ASSERT(!d_list_empty(dirty_list));
	max_iod_per_page = ((cache->ca_bmap_sz << UMEM_CHUNK_IDX_SHIFT) / 2);

	D_INIT_LIST_HEAD(&free_list);
	D_INIT_LIST_HEAD(&waiting_list);

	/** Setup the in-flight IODs */
	for (i = 0; i < chkpt_nr; i++) {
		chkpt_data = &chkpt_data_all[i];
		d_list_add_tail(&chkpt_data->cd_link, &free_list);
		chkpt_data->cd_store_iod.io_regions = &chkpt_data->cd_regions[0];
		chkpt_data->cd_sg_list.sg_iovs      = &chkpt_data->cd_iovs[0];
	}

	/** First mark all pages in the new list so they won't be moved by an I/O thread.  This
	 *  will enable us to continue the algorithm in relative isolation from I/O threads.
	 */
	d_list_for_each_entry(pinfo, dirty_list, pi_dirty_link) {
		/** Mark all pages in copying list first.  Marking them as waiting will prevent
		 *  them from being moved to another list by an I/O operation.
		 */
		D_ASSERT(pinfo->pi_io == 0);
		pinfo->pi_io = 1;
		D_ASSERT(d_list_empty(&pinfo->pi_flush_link));
		d_list_add_tail(&pinfo->pi_flush_link, &cache->ca_pgs_flushing);
		tot_pgs++;

		if (store->stor_ops->so_wal_id_cmp(store, pinfo->pi_last_inflight, *chkpt_id) > 0)
			*chkpt_id = pinfo->pi_last_inflight;
	}

	do {
		/** first try to add up to MAX_INFLIGHT_SETS to the waiting queue */
		while (inflight < MAX_INFLIGHT_SETS && !d_list_empty(dirty_list)) {
			chkpt_data =
			    d_list_pop_entry(&free_list, struct umem_checkpoint_data, cd_link);

			D_ASSERT(chkpt_data != NULL);

			chkpt_data->cd_nr_pages      = 0;
			chkpt_data->cd_sg_list.sg_nr = chkpt_data->cd_sg_list.sg_nr_out = 0;
			chkpt_data->cd_store_iod.io_nr                                  = 0;
			chkpt_data->cd_max_tx                                           = 0;
			chkpt_data->cd_nr_dchunks                                       = 0;

			while (chkpt_data->cd_nr_pages < MAX_PAGES_PER_SET &&
			       chkpt_data->cd_store_iod.io_nr <= max_iod_per_page &&
			       (pinfo = d_list_pop_entry(dirty_list, struct umem_page_info,
							 pi_dirty_link)) != NULL) {
				D_ASSERT(chkpt_data != NULL);
				page2chkpt(store, pinfo, chkpt_data);
			}

			rc = store->stor_ops->so_flush_prep(store, &chkpt_data->cd_store_iod,
							    &chkpt_data->cd_fh);
			if (rc != 0) {
				/** Just put the pages back and break the loop */
				for (i = 0; i < chkpt_data->cd_nr_pages; i++) {
					pinfo             = chkpt_data->cd_pages[i];
					pinfo->pi_copying = 0;
					d_list_add(&pinfo->pi_dirty_link, dirty_list);
				}
				d_list_add(&chkpt_data->cd_link, &free_list);
				rc = 0;
				break;
			}

			for (i = 0; i < chkpt_data->cd_nr_pages; i++) {
				pinfo                     = chkpt_data->cd_pages[i];
				pinfo->pi_last_checkpoint = pinfo->pi_last_inflight;
			}

			/*
			 * DAV allocator uses valgrind macros to mark certain portions of
			 * heap as no access for user. Prevent valgrind from reporting
			 * invalid read while checkpointing these address ranges.
			 */
			if (DAOS_ON_VALGRIND) {
				d_sg_list_t  *sgl = &chkpt_data->cd_sg_list;

				for (i = 0; i < sgl->sg_nr; i++)
					VALGRIND_DISABLE_ADDR_ERROR_REPORTING_IN_RANGE(
						sgl->sg_iovs[i].iov_buf, sgl->sg_iovs[i].iov_len);
			}

			rc = store->stor_ops->so_flush_copy(chkpt_data->cd_fh,
							    &chkpt_data->cd_sg_list);
			/** If this fails, it means invalid argument, so assertion here is fine */
			D_ASSERT(rc == 0);

			if (DAOS_ON_VALGRIND) {
				d_sg_list_t  *sgl = &chkpt_data->cd_sg_list;

				for (i = 0; i < sgl->sg_nr; i++)
					VALGRIND_ENABLE_ADDR_ERROR_REPORTING_IN_RANGE(
						sgl->sg_iovs[i].iov_buf, sgl->sg_iovs[i].iov_len);
			}

			for (i = 0; i < chkpt_data->cd_nr_pages; i++) {
				pinfo             = chkpt_data->cd_pages[i];
				pinfo->pi_copying = 0;
				memset(pinfo->pi_bmap, 0, sizeof(uint64_t) * cache->ca_bmap_sz);
			}

			chkpt_insert_sorted(store, chkpt_data, &waiting_list);

			inflight++;
		}

		chkpt_data = d_list_pop_entry(&waiting_list, struct umem_checkpoint_data, cd_link);

		/* Wait for in-flight transactions committed, or yield to make progress */
		wait_commit_cb(arg, chkpt_data ? chkpt_data->cd_max_tx : 0, &committed_tx);

		/* The so_flush_prep() could fail when the DMA buffer is under pressure */
		if (chkpt_data == NULL)
			continue;

		/* WAL commit failed due to faulty SSD */
		if (store->stor_ops->so_wal_id_cmp(store, committed_tx,
						   chkpt_data->cd_max_tx) < 0) {
			D_ASSERT(store->store_faulty);
			D_ERROR("WAL commit failed. "DF_X64" < "DF_X64"\n", committed_tx,
				chkpt_data->cd_max_tx);
			rc = -DER_NVME_IO;
		}

		/** Since the flush API only allows one at a time, let's just do one at a time
		 *  before copying another page.  We can revisit this later if the API allows
		 *  to pass more than one fh.
		 */
		rc = store->stor_ops->so_flush_post(chkpt_data->cd_fh, rc);
		for (i = 0; i < chkpt_data->cd_nr_pages; i++) {
			pinfo = chkpt_data->cd_pages[i];
			page_flush_completion(cache, pinfo);
		}
		inflight--;

		flushed_pgs += chkpt_data->cd_nr_pages;
		if (stats) {
			stats->uccs_nr_pages	+= chkpt_data->cd_nr_pages;
			stats->uccs_nr_dchunks	+= chkpt_data->cd_nr_dchunks;
			stats->uccs_nr_iovs	+= chkpt_data->cd_sg_list.sg_nr_out;
		}
		d_list_add(&chkpt_data->cd_link, &free_list);

		if (rc != 0 || (DAOS_FAIL_CHECK(DAOS_MEM_FAIL_CHECKPOINT) &&
		    flushed_pgs >= tot_pgs / 2)) {
			rc = -DER_AGAIN;
			break;
		}

	} while (inflight != 0 || !d_list_empty(dirty_list));

	return rc;
}

int
umem_cache_checkpoint(struct umem_store *store, umem_cache_wait_cb_t wait_cb, void *arg,
		      uint64_t *out_id, struct umem_cache_chkpt_stats *stats)
{
	struct umem_cache		*cache;
	struct umem_page_info		*pinfo;
	struct umem_checkpoint_data	*chkpt_data_all;
	d_list_t			 dirty_list;
	uint64_t			 chkpt_id = *out_id;
	int				 rc = 0;

	D_ASSERT(store != NULL);
	cache = store->cache;

	if (cache == NULL)
		return 0; /* TODO: When SMD is supported outside VOS, this will be an error */

	if (d_list_empty(&cache->ca_pgs_dirty))
		goto wait;

	D_ALLOC_ARRAY(chkpt_data_all, MAX_INFLIGHT_SETS);
	if (chkpt_data_all == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&dirty_list);
	d_list_splice_init(&cache->ca_pgs_dirty, &dirty_list);

	rc = cache_flush_pages(cache, &dirty_list, chkpt_data_all, MAX_INFLIGHT_SETS, wait_cb, arg,
			       &chkpt_id, stats);

	D_FREE(chkpt_data_all);
	if (!d_list_empty(&dirty_list)) {
		D_ASSERT(rc != 0);
		d_list_move(&dirty_list, &cache->ca_pgs_dirty);
	}
wait:
	/* Wait for the evicting pages (if any) with lower checkpoint id */
	d_list_for_each_entry(pinfo, &cache->ca_pgs_flushing, pi_flush_link) {
		D_ASSERT(pinfo->pi_io == 1);
		if (store->stor_ops->so_wal_id_cmp(store, chkpt_id, pinfo->pi_last_checkpoint) < 0)
			continue;
		page_wait_io(cache, pinfo);
		goto wait;
	}

	*out_id = chkpt_id;

	return rc;
}

static inline void
inc_cache_stats(struct umem_cache *cache, unsigned int op)
{
	cache->ca_cache_stats[op] += 1;
}

static int
cache_load_page(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct umem_store	*store = cache->ca_store;
	uint64_t		 offset;
	daos_size_t		 len;
	int			 rc;

	D_ASSERT(pinfo->pi_mapped == 1);

	if (pinfo->pi_io == 1) {
		page_wait_io(cache, pinfo);
		return pinfo->pi_loaded ? 0 : -DER_IO;
	}

	offset = cache_id2off(cache, pinfo->pi_pg_id);
	D_ASSERT(offset < store->stor_size);
	len = min(cache->ca_page_sz, store->stor_size - offset);
	pinfo->pi_io = 1;

	if (DAOS_ON_VALGRIND)
		VALGRIND_DISABLE_ADDR_ERROR_REPORTING_IN_RANGE((char *)pinfo->pi_addr, len);
	rc = store->stor_ops->so_load(store, (char *)pinfo->pi_addr, offset, len);
	if (DAOS_ON_VALGRIND)
		VALGRIND_ENABLE_ADDR_ERROR_REPORTING_IN_RANGE((char *)pinfo->pi_addr, len);
	pinfo->pi_io = 0;
	if (rc) {
		DL_ERROR(rc, "Read MD blob failed.");
		page_wakeup_io(cache, pinfo);
		return rc;
	} else if (cache->ca_evtcb_fn) {
		rc = cache->ca_evtcb_fn(UMEM_CACHE_EVENT_PGLOAD, cache->ca_fn_arg, pinfo->pi_pg_id);
		if (rc) {
			DL_ERROR(rc, "Pageload callback failed.");
			page_wakeup_io(cache, pinfo);
			return rc;
		}
	}

	pinfo->pi_loaded = 1;
	/* Add to LRU when it's unpinned */
	if (pinfo->pi_ref == 0)
		cache_add2lru(cache, pinfo);

	page_wakeup_io(cache, pinfo);
	inc_cache_stats(cache, UMEM_CACHE_STATS_LOAD);

	return rc;
}

void
umem_cache_commit(struct umem_store *store, uint64_t commit_id)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page_info	*pinfo, *tmp;

	D_ASSERT(store->stor_ops->so_wal_id_cmp(store, cache->ca_commit_id, commit_id) <= 0);
	cache->ca_commit_id = commit_id;

	d_list_for_each_entry_safe(pinfo, tmp, &cache->ca_pgs_wait_commit, pi_dirty_link) {
		if (store->stor_ops->so_wal_id_cmp(store, pinfo->pi_last_checkpoint,
							commit_id) <= 0) {
			d_list_del_init(&pinfo->pi_dirty_link);
			page_wakeup_commit(cache, pinfo);
		}
	}
}

struct wait_page_commit_arg {
	struct umem_cache	*wca_cache;
	struct umem_page_info	*wca_pinfo;
};

static void
wait_page_commit_cb(void *arg, uint64_t wait_tx, uint64_t *committed_tx)
{
	struct wait_page_commit_arg	*wca = arg;
	struct umem_cache		*cache = wca->wca_cache;
	struct umem_store		*store = cache->ca_store;
	struct umem_page_info		*pinfo = wca->wca_pinfo;

	/* Special case, needs to yield to allow progress */
	if (wait_tx == 0) {
		page_wait_committed(cache, pinfo, true);
		*committed_tx = cache->ca_commit_id;
		return;
	}

	D_ASSERT(wait_tx == pinfo->pi_last_checkpoint);
	/* Page is committed */
	if (store->stor_ops->so_wal_id_cmp(store, cache->ca_commit_id, wait_tx) >= 0) {
		*committed_tx = cache->ca_commit_id;
		return;
	}

	D_ASSERT(d_list_empty(&pinfo->pi_dirty_link));
	d_list_add_tail(&pinfo->pi_dirty_link, &cache->ca_pgs_wait_commit);
	page_wait_committed(cache, pinfo, false);
	*committed_tx = cache->ca_commit_id;
}

static int
cache_flush_page(struct umem_cache *cache, struct umem_page_info *pinfo)
{
	struct wait_page_commit_arg	 arg;
	struct umem_checkpoint_data	*chkpt_data_all;
	d_list_t			 dirty_list;
	uint64_t			 chkpt_id = 0;
	int				 rc;

	D_ALLOC_ARRAY(chkpt_data_all, 1);
	if (chkpt_data_all == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&dirty_list);
	d_list_del_init(&pinfo->pi_dirty_link);
	d_list_add_tail(&pinfo->pi_dirty_link, &dirty_list);

	/*
	 * Bump the last checkpoint ID beforehand, since cache_flush_pages() could yield before
	 * bumping the last checkpoint ID.
	 */
	D_ASSERT(is_page_dirty(pinfo));
	pinfo->pi_last_checkpoint = pinfo->pi_last_inflight;

	arg.wca_cache = cache;
	arg.wca_pinfo = pinfo;

	rc = cache_flush_pages(cache, &dirty_list, chkpt_data_all, 1, wait_page_commit_cb, &arg,
			       &chkpt_id, NULL);
	D_FREE(chkpt_data_all);
	D_ASSERT(d_list_empty(&dirty_list));
	inc_cache_stats(cache, UMEM_CACHE_STATS_FLUSH);

	return rc;
}

static int
cache_evict_page(struct umem_cache *cache, bool for_sys)
{
	struct umem_page_info	*pinfo;
	d_list_t		*pg_list = &cache->ca_pgs_lru[1];
	int			 rc;

	if (cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] == cache->ca_mem_pages) {
		D_ERROR("No evictable page.\n");
		return -DER_INVAL;
	} else if (d_list_empty(pg_list)) {
		D_ERROR("All evictable pages are pinned.\n");
		return -DER_BUSY;
	}

	/* Try the most recent used page if it was used for sys */
	if (for_sys) {
		pinfo = d_list_entry(pg_list->prev, struct umem_page_info, pi_lru_link);
		if (pinfo->pi_sys == 1)
			goto evict;
	}

	/* Try evictable pages in LRU order */
	pinfo = d_list_entry(pg_list->next, struct umem_page_info, pi_lru_link);
evict:
	D_ASSERT(pinfo->pi_ref == 0);

	/*
	 * To minimize page eviction, let's evict page one by one for this moment, we
	 * may consider to allow N concurrent pages eviction in the future.
	 */
	if (pinfo->pi_io == 1) {
		D_ASSERT(!d_list_empty(&pinfo->pi_flush_link));
		page_wait_io(cache, pinfo);
		return -DER_AGAIN;
	}

	if (is_page_dirty(pinfo)) {
		rc = cache_flush_page(cache, pinfo);
		if (rc) {
			DL_ERROR(rc, "Flush page failed.");
			return rc;
		}

		/* The page is referenced by others while flushing */
		if ((pinfo->pi_ref > 0) || is_page_dirty(pinfo) || pinfo->pi_io == 1)
			return -DER_AGAIN;

		/* The status of the page changed to non-evictable? */
		if (!pinfo->pi_evictable)
			return -DER_AGAIN;
	}

	if (cache->ca_evtcb_fn) {
		rc = cache->ca_evtcb_fn(UMEM_CACHE_EVENT_PGEVICT, cache->ca_fn_arg,
					pinfo->pi_pg_id);
		if (rc)
			DL_ERROR(rc, "Page evict callback failed.");
	}
	d_list_del_init(&pinfo->pi_lru_link);
	cache_unmap_page(cache, pinfo);
	inc_cache_stats(cache, UMEM_CACHE_STATS_EVICT);

	return 0;
}

static inline bool
need_reserve(struct umem_cache *cache, uint32_t extra_pgs)
{
	uint32_t	page_nr = 0;

	if (cache->ca_replay_done) {
		/* Few free pages are always reserved for potential non-evictable zone grow */
		D_ASSERT(cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] <= cache->ca_max_ne_pages);
		page_nr = cache->ca_max_ne_pages - cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE];
		if (page_nr > UMEM_CACHE_RSRVD_PAGES)
			page_nr = UMEM_CACHE_RSRVD_PAGES;
	}
	page_nr += extra_pgs;

	if (page_nr == 0)
		return false;

	return cache->ca_pgs_stats[UMEM_PG_STATS_FREE] < page_nr ? true : false;
}

static inline bool
need_evict(struct umem_cache *cache)
{
	if (d_list_empty(&cache->ca_pgs_free))
		return true;

	return need_reserve(cache, 1);
}

static int
cache_get_free_page(struct umem_cache *cache, struct umem_page_info **ret_pinfo, int pinned_nr,
		    bool for_sys)
{
	struct umem_page_info	*pinfo;
	int			 rc, retry_cnt = 0;

	while (need_evict(cache)) {
		rc = cache_evict_page(cache, for_sys);
		if (rc && rc != -DER_AGAIN && rc != -DER_BUSY) {
			DL_ERROR(rc, "Evict page failed.");
			return rc;
		}

		/* All pinned pages are from current caller */
		if (rc == -DER_BUSY && pinned_nr == cache->ca_pgs_stats[UMEM_PG_STATS_PINNED]) {
			D_ERROR("Not enough evictable pages.\n");
			return -DER_INVAL;
		}

		D_CDEBUG(retry_cnt == 10, DLOG_ERR, DB_TRACE,
			 "Retry get free page, %d times\n", retry_cnt);
		retry_cnt++;
	}

	pinfo = cache_pop_free_page(cache);
	D_ASSERT(pinfo != NULL);
	*ret_pinfo = pinfo;

	return 0;
}

/*
 * Only allow map empty pages. It could yield when mapping an evictable page,
 * so when caller tries to map non-evictable page, the page_nr must be 1.
 * If a page is already mapped, check and update the evictability status.
 */
static int
cache_map_pages(struct umem_cache *cache, uint32_t *pages, int page_nr)
{
	struct umem_page_info	*pinfo, *free_pinfo = NULL;
	uint32_t		 pg_id;
	int			 i, rc = 0;

	for (i = 0; i < page_nr; i++) {
		pg_id = pages[i];

		if (is_id_evictable(cache, pg_id) && page_nr != 1) {
			D_ERROR("Can only map single evictable page.\n");
			return -DER_INVAL;
		}
retry:
		pinfo = cache->ca_pages[pg_id].pg_info;
		/* The page is already mapped */
		if (pinfo != NULL) {
			D_ASSERT(pinfo->pi_pg_id == pg_id);
			D_ASSERT(pinfo->pi_mapped == 1);
			D_ASSERT(pinfo->pi_loaded == 1);
			if (free_pinfo != NULL) {
				cache_push_free_page(cache, free_pinfo);
				free_pinfo = NULL;
			}
			if (is_id_evictable(cache, pg_id) != pinfo->pi_evictable) {
				pinfo->pi_evictable = is_id_evictable(cache, pg_id);
				D_ASSERT(!pinfo->pi_evictable ||
					 (cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] > 0));
				cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] +=
				    pinfo->pi_evictable ? (-1) : 1;
				d_list_del_init(&pinfo->pi_lru_link);
				cache_add2lru(cache, pinfo);
			}
			continue;
		}

		if (is_id_evictable(cache, pg_id)) {
			if (free_pinfo == NULL) {
				rc = cache_get_free_page(cache, &free_pinfo, 0, false);
				if (rc) {
					DL_ERROR(rc, "Failed to get free page.");
					break;
				}
				goto retry;
			} else {
				pinfo = free_pinfo;
				free_pinfo = NULL;
			}
		} else {
			pinfo = cache_pop_free_page(cache);
			if (pinfo == NULL) {
				D_ERROR("No free pages.\n");
				rc = -DER_BUSY;
				break;
			}
		}

		cache_map_page(cache, pinfo, pg_id);
		cache_add2lru(cache, pinfo);
		/* Map an empty page, doesn't need to load page */
		pinfo->pi_loaded = 1;
	}

	return rc;
}

static int
cache_pin_pages(struct umem_cache *cache, uint32_t *pages, int page_nr, bool for_sys)
{
	struct umem_page_info	*pinfo, *free_pinfo = NULL;
	uint32_t		 pg_id;
	int			 i, processed = 0, pinned = 0, rc = 0;

	for (i = 0; i < page_nr; i++) {
		pg_id = pages[i];
retry:
		pinfo = cache->ca_pages[pg_id].pg_info;
		/* The page is already mapped */
		if (pinfo != NULL) {
			D_ASSERT(pinfo->pi_pg_id == pg_id);
			D_ASSERT(pinfo->pi_mapped == 1);
			inc_cache_stats(cache, UMEM_CACHE_STATS_HIT);
			if (free_pinfo != NULL) {
				cache_push_free_page(cache, free_pinfo);
				free_pinfo = NULL;
			}
			goto next;
		}

		if (free_pinfo == NULL) {
			rc = cache_get_free_page(cache, &free_pinfo, pinned, for_sys);
			if (rc)
				goto error;
			/* Above cache_get_free_page() could yield, need re-check mapped status */
			goto retry;
		} else {
			pinfo = free_pinfo;
			free_pinfo = NULL;
		}

		inc_cache_stats(cache, UMEM_CACHE_STATS_MISS);
		cache_map_page(cache, pinfo, pg_id);
next:
		cache_pin_page(cache, pinfo);
		processed++;
		if (is_id_evictable(cache, pinfo->pi_pg_id))
			pinned++;
	}

	for (i = 0; i < page_nr; i++) {
		pg_id = pages[i];
		pinfo = cache->ca_pages[pg_id].pg_info;

		D_ASSERT(pinfo != NULL);
		if (pinfo->pi_loaded == 0) {
			rc = cache_load_page(cache, pinfo);
			if (rc)
				goto error;
		}
		pinfo->pi_sys = for_sys;
	}

	return 0;
error:
	for (i = 0; i < processed; i++) {
		pg_id = pages[i];
		pinfo = cache->ca_pages[pg_id].pg_info;

		D_ASSERT(pinfo != NULL);
		cache_unpin_page(cache, pinfo);

	}
	return rc;
}

#define DF_RANGE		\
	DF_U64", "DF_U64
#define DP_RANGE(range)		\
	(range)->cr_off, (range)->cr_size

static int
cache_rgs2pgs(struct umem_cache *cache, struct umem_cache_range *ranges, int range_nr,
	      uint32_t *in_pages, int *page_nr, uint32_t **out_pages)
{
	struct umem_cache_range	range;
	uint32_t		page_id, *pages = in_pages, *old_pages = NULL, len = 0;
	int			rc = 0, i, page_idx = 0, tot_pages = *page_nr;

	for (i = 0; i < range_nr; i++) {
		range = ranges[i];
		/* Assume the ranges are sorted & no overlapping */
		if (i > 0) {
			if (range.cr_off < ranges[i - 1].cr_off + ranges[i - 1].cr_size) {
				D_ERROR("Invalid ranges ["DF_RANGE"], ["DF_RANGE"]\n",
					DP_RANGE(&ranges[i - 1]), DP_RANGE(&range));
				rc = -DER_INVAL;
				goto error;
			}
		}

		D_ASSERT(range.cr_size > 0);
		while (range.cr_size > 0) {
			page_id = cache_off2id(cache, range.cr_off);

			if (len != 0 && page_id != pages[page_idx]) {
				page_idx++;
				if (page_idx == tot_pages) {
					D_REALLOC_ARRAY(pages, old_pages, tot_pages, tot_pages * 2);
					if (pages == NULL) {
						D_ERROR("Alloc array(%d) failed.\n", tot_pages * 2);
						rc = -DER_NOMEM;
						goto error;
					}
					old_pages = pages;
					tot_pages = tot_pages * 2;
				}
			}

			pages[page_idx] = page_id;
			len = cache->ca_page_sz - cache_off2pg_off(cache, range.cr_off);
			range.cr_off += len;
			if (range.cr_size >= len)
				range.cr_size -= len;
			else
				range.cr_size = 0;
		}
	}

	D_ASSERT(page_idx < tot_pages);
	*out_pages = pages;
	*page_nr = page_idx + 1;

	return 0;
error:
	if (old_pages)
		D_FREE(old_pages);
	return rc;
}

#define UMEM_PAGES_ON_STACK	16

void
umem_cache_post_replay(struct umem_store *store)
{
	struct umem_cache     *cache = store->cache;
	int                    cnt   = 0;
	int                    idx;
	struct umem_page_info *pinfo;

	pinfo = (struct umem_page_info *)&cache->ca_pages[cache->ca_md_pages];
	for (idx = 0; idx < cache->ca_mem_pages; idx++) {
		if (pinfo[idx].pi_loaded == 0)
			continue;

		if (!is_id_evictable(cache, pinfo[idx].pi_pg_id)) {
			d_list_del_init(&pinfo[idx].pi_lru_link);
			d_list_add_tail(&pinfo[idx].pi_lru_link, &cache->ca_pgs_lru[0]);
			pinfo[idx].pi_evictable = 0;
			cnt++;
		}
	}
	cache->ca_pgs_stats[UMEM_PG_STATS_NONEVICTABLE] = cnt;
	cache->ca_replay_done                           = 1;
}

int
umem_cache_map(struct umem_store *store, struct umem_cache_range *ranges, int range_nr)
{
	struct umem_cache	*cache = store->cache;
	uint32_t		 in_pages[UMEM_PAGES_ON_STACK], *out_pages;
	int			 rc, page_nr = UMEM_PAGES_ON_STACK;

	rc = cache_rgs2pgs(cache, ranges, range_nr, &in_pages[0], &page_nr, &out_pages);
	if (rc)
		return rc;

	rc = cache_map_pages(cache, out_pages, page_nr);
	if (rc)
		DL_ERROR(rc, "Map page failed.");

	if (out_pages != &in_pages[0])
		D_FREE(out_pages);

	return rc;
}

int
umem_cache_load(struct umem_store *store, struct umem_cache_range *ranges, int range_nr,
		bool for_sys)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page_info	*pinfo;
	uint32_t		 in_pages[UMEM_PAGES_ON_STACK], *out_pages;
	int			 i, rc, page_nr = UMEM_PAGES_ON_STACK;

	rc = cache_rgs2pgs(cache, ranges, range_nr, &in_pages[0], &page_nr, &out_pages);
	if (rc)
		return rc;

	rc = cache_pin_pages(cache, out_pages, page_nr, for_sys);
	if (rc) {
		DL_ERROR(rc, "Load page failed.");
	} else {
		for (i = 0; i < page_nr; i++) {
			uint32_t	pg_id = out_pages[i];

			pinfo = cache->ca_pages[pg_id].pg_info;
			D_ASSERT(pinfo != NULL);
			cache_unpin_page(cache, pinfo);
		}
	}

	if (out_pages != &in_pages[0])
		D_FREE(out_pages);

	return rc;
}

struct umem_pin_handle {
	uint32_t	ph_page_nr;
	uint32_t	ph_pages[0];
};

int
umem_cache_pin(struct umem_store *store, struct umem_cache_range *ranges, int range_nr,
	       bool for_sys, struct umem_pin_handle **pin_handle)
{
	struct umem_cache	*cache = store->cache;
	struct umem_pin_handle	*handle;
	uint32_t		 in_pages[UMEM_PAGES_ON_STACK], *out_pages;
	int			 rc, page_nr = UMEM_PAGES_ON_STACK;

	rc = cache_rgs2pgs(cache, ranges, range_nr, &in_pages[0], &page_nr, &out_pages);
	if (rc)
		return rc;

	rc = cache_pin_pages(cache, out_pages, page_nr, for_sys);
	if (rc) {
		DL_ERROR(rc, "Load page failed.");
		goto out;
	}

	D_ALLOC(handle, sizeof(struct umem_pin_handle) + sizeof(uint32_t) * page_nr);
	if (handle == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	handle->ph_page_nr = page_nr;
	memcpy(&handle->ph_pages[0], out_pages, sizeof(uint32_t) * page_nr);
	*pin_handle = handle;
out:
	if (out_pages != &in_pages[0])
		D_FREE(out_pages);

	return rc;
}

void
umem_cache_unpin(struct umem_store *store, struct umem_pin_handle *pin_handle)
{
	struct umem_cache	*cache = store->cache;
	struct umem_page_info	*pinfo;
	int			 i;

	D_ASSERT(pin_handle != NULL);
	D_ASSERT(pin_handle->ph_page_nr > 0);

	for (i = 0; i < pin_handle->ph_page_nr; i++) {
		uint32_t	pg_id = pin_handle->ph_pages[i];

		pinfo = cache->ca_pages[pg_id].pg_info;
		D_ASSERT(pinfo != NULL);
		cache_unpin_page(cache, pinfo);
	}

	D_FREE(pin_handle);
}

int
umem_cache_reserve(struct umem_store *store)
{
	struct umem_cache	*cache = store->cache;
	int			 rc = 0, retry_cnt = 0;

	if (cache_mode(cache) == 1)
		return rc;

	/* MUST ensure the FIFO order */
	if (!need_reserve(cache, 0) && !cache->ca_reserve_waiters)
		return rc;

	D_ASSERT(cache->ca_reserve_wq != NULL);
	cache->ca_reserve_waiters++;
	if (cache->ca_reserve_waiters > 1) {
		D_ASSERT(store->stor_ops->so_waitqueue_wait != NULL);
		store->stor_ops->so_waitqueue_wait(cache->ca_reserve_wq, false);
	}

	while (need_reserve(cache, 0)) {
		rc = cache_evict_page(cache, false);
		if (rc && rc != -DER_AGAIN && rc != -DER_BUSY) {
			DL_ERROR(rc, "Evict page failed.");
			break;
		}
		rc = 0;

		D_CDEBUG(retry_cnt == 10, DLOG_ERR, DB_TRACE,
			 "Retry reserve free page, %d times\n", retry_cnt);
		retry_cnt++;
	}

	D_ASSERT(cache->ca_reserve_waiters > 0);
	cache->ca_reserve_waiters--;
	if (cache->ca_reserve_waiters > 0) {
		D_ASSERT(store->stor_ops->so_waitqueue_wakeup != NULL);
		store->stor_ops->so_waitqueue_wakeup(cache->ca_reserve_wq, false);
	}

	return rc;
}

uint32_t
umem_get_mb_from_offset(struct umem_instance *umm, umem_off_t off)
{
	uint32_t           page_id;
	struct umem_cache *cache = umm->umm_pool->up_store.cache;

	page_id = cache_off2id(cache, off);
	if (is_id_evictable(cache, page_id))
		return page_id;
	return 0;
}

umem_off_t
umem_get_mb_base_offset(struct umem_instance *umm, uint32_t id)
{
	struct umem_cache *cache = umm->umm_pool->up_store.cache;

	return cache_id2off(cache, id);
}

#endif
