/**
 * (C) Copyright 2016-2023 Intel Corporation.
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
#include "dav/dav.h"
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

enum {
	DAOS_MD_PMEM	= 0,
	DAOS_MD_BMEM	= 1,
	DAOS_MD_ADMEM	= 2,
};

static int daos_md_backend = DAOS_MD_PMEM;

/** Sets up global settings for the pmem objects.
 *
 *  \return	0 on success, non-zero on failure.
 */
int
umempobj_settings_init(void)
{
	int					rc;
	enum pobj_arenas_assignment_type	atype;
	unsigned int				val = DAOS_MD_PMEM;

	d_getenv_int("DAOS_MD_ON_SSD", &val);
	switch (val) {
	case DAOS_MD_PMEM:
		daos_md_backend = DAOS_MD_PMEM;
		atype = POBJ_ARENAS_ASSIGNMENT_GLOBAL;

		rc = pmemobj_ctl_set(NULL, "heap.arenas_assignment_type", &atype);
		if (rc != 0)
			D_ERROR("Could not configure PMDK for global arena: %s\n",
				strerror(errno));
		return rc;
	case DAOS_MD_BMEM:
		daos_md_backend = DAOS_MD_BMEM;
		D_INFO("UMEM will use Blob Backed Memory as the metadata backend interface\n");
		return 0;
	case DAOS_MD_ADMEM:
		daos_md_backend = DAOS_MD_ADMEM;
		D_INFO("UMEM will use AD-hoc Memory as the metadata backend interface\n");
		return 0;
	default:
		D_ERROR("invalid DAOS_MD_ON_SSD value %d\n", val);
		return -DER_INVAL;
	};
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
	struct umem_pool	*umm_pool;
	PMEMobjpool		*pop;
	dav_obj_t		*dav_hdl;
	struct ad_blob_handle	 bh;
	int			 enabled = 1;
	int			 rc;

	D_ALLOC_PTR(umm_pool);
	if (umm_pool == NULL)
		return NULL;

	if (store != NULL)
		umm_pool->up_store = *store;

	D_DEBUG(DB_TRACE, "creating path %s, poolsize %zu, store_size %zu ...\n", path, poolsize,
		store != NULL ? store->stor_size : 0);
	switch (daos_md_backend) {
	case DAOS_MD_PMEM:
		pop = pmemobj_create(path, layout_name, poolsize, mode);
		if (!pop) {
			D_ERROR("Failed to create pool %s, size="DF_U64": %s\n",
				path, poolsize, pmemobj_errormsg());
			D_FREE(umm_pool);
			return NULL;
		}
		if (flags & UMEMPOBJ_ENABLE_STATS) {
			rc = pmemobj_ctl_set(pop, "stats.enabled", &enabled);
			if (rc) {
				D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
					DP_RC(rc));
				rc = umem_tx_errno(rc);
				pmemobj_close(pop);
				D_FREE(umm_pool);
				return NULL;
			}
		}

		umm_pool->up_priv = pop;
		return umm_pool;
	case DAOS_MD_BMEM:
		dav_hdl = dav_obj_create(path, 0, poolsize, mode, &umm_pool->up_store);
		if (!dav_hdl) {
			D_ERROR("Failed to create pool %s, size="DF_U64": errno = %d\n",
				path, poolsize, errno);
			D_FREE(umm_pool);
			return NULL;
		}
		umm_pool->up_priv = dav_hdl;

		/* TODO: Do checkpoint here to write back allocator heap */
		return umm_pool;
	case DAOS_MD_ADMEM:
		rc = ad_blob_create(path, 0, store, &bh);
		if (rc) {
			D_ERROR("ad_blob_create failed, "DF_RC"\n", DP_RC(rc));
			D_FREE(umm_pool);
			return NULL;
		}

		umm_pool->up_priv = bh.bh_blob;
		return umm_pool;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
		break;
	};

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

	D_ALLOC_PTR(umm_pool);
	if (umm_pool == NULL)
		return NULL;

	if (store != NULL)
		umm_pool->up_store = *store;

	D_DEBUG(DB_TRACE, "opening %s\n", path);
	switch (daos_md_backend) {
	case DAOS_MD_PMEM:
		pop = pmemobj_open(path, layout_name);
		if (!pop) {
			D_ERROR("Error in opening the pool %s: %s\n",
				path, pmemobj_errormsg());
			D_FREE(umm_pool);
			return NULL;
		}
		if (flags & UMEMPOBJ_ENABLE_STATS) {
			rc = pmemobj_ctl_set(pop, "stats.enabled", &enabled);
			if (rc) {
				D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
					DP_RC(rc));
				rc = umem_tx_errno(rc);
				pmemobj_close(pop);
				D_FREE(umm_pool);
				return NULL;
			}
		}

		umm_pool->up_priv = pop;
		return umm_pool;
	case DAOS_MD_BMEM:
		/* TODO mmap tmpfs file */
		/* TODO Load all meta pages from SSD */
		/* TODO Replay WAL */

		dav_hdl = dav_obj_open(path, 0, &umm_pool->up_store);
		if (!dav_hdl) {
			D_ERROR("Error in opening the pool %s: errno =%d\n",
				path, errno);
			D_FREE(umm_pool);
			return NULL;
		}

		umm_pool->up_priv = dav_hdl;
		return umm_pool;
	case DAOS_MD_ADMEM:
		rc = ad_blob_open(path, 0, store, &bh);
		if (rc) {
			D_ERROR("ad_blob_create failed, "DF_RC"\n", DP_RC(rc));
			D_FREE(umm_pool);
			return NULL;
		}

		umm_pool->up_priv = bh.bh_blob;
		return umm_pool;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
		break;
	}

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

	switch (daos_md_backend) {
	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		pmemobj_close(pop);
		break;
	case DAOS_MD_BMEM:
		dav_obj_close((dav_obj_t *)ph_p->up_priv);
		break;
	case DAOS_MD_ADMEM:
		bh.bh_blob = (struct ad_blob *)ph_p->up_priv;
		ad_blob_close(bh);
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
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

	switch (daos_md_backend) {
	case DAOS_MD_PMEM:
		pop = (PMEMobjpool *)ph_p->up_priv;

		root = pmemobj_root(pop, size);
		rootp = pmemobj_direct(root);
		return rootp;
	case DAOS_MD_BMEM:
		off = dav_root((dav_obj_t *)ph_p->up_priv, size);
		return (char *)dav_get_base_ptr((dav_obj_t *)ph_p->up_priv) + off;
	case DAOS_MD_ADMEM:
		bh.bh_blob = (struct ad_blob *)ph_p->up_priv;
		return ad_root(bh, size);
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
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

	switch (daos_md_backend) {
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
	case DAOS_MD_ADMEM:
		*curr_allocated = 40960; /* TODO */
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
		break;
	}

	return rc;
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

	switch (daos_md_backend) {
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
	case DAOS_MD_ADMEM:
		/* TODO */
		D_ERROR("Fragmentation info, not implemented in ADMEM yet.\n");
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
		break;
	}
}

/** Create a slab within the pool.
 *
 *  \param	pool[IN]		Pointer to the persistent object.
 *  \param	slab[IN]		Slab description
 *
 *  \return	zero on success, non-zero on failure.
 */
int
umempobj_set_slab_desc(struct umem_pool *ph_p, struct umem_slab_desc *slab)
{
	PMEMobjpool			*pop;
	struct pobj_alloc_class_desc	 pmemslab;
	struct dav_alloc_class_desc	 davslab;
	int				 rc = 0;

	switch (daos_md_backend) {
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
	case DAOS_MD_ADMEM:
		/* NOOP for ADMEM now */
		slab->class_id = class_id++;
		break;
	default:
		D_ASSERTF(0, "bad daos_md_backend %d\n", daos_md_backend);
		break;
	}
	return rc;
}

static inline uint64_t
umem_slab_flags(struct umem_instance *umm, unsigned int slab_id)
{
	D_ASSERT(slab_id < UMM_SLABS_CNT);
	return (daos_md_backend == DAOS_MD_PMEM) ?
		POBJ_CLASS_ID(umm->umm_slabs[slab_id].class_id) :
		DAV_CLASS_ID(umm->umm_slabs[slab_id].class_id);
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
pmem_tx_alloc(struct umem_instance *umm, size_t size, int slab_id,
	      uint64_t flags, unsigned int type_num)
{
	uint64_t pflags = 0;

	if (flags & UMEM_FLAG_ZERO)
		pflags |= POBJ_FLAG_ZERO;
	if (flags & UMEM_FLAG_NO_FLUSH)
		pflags |= POBJ_FLAG_NO_FLUSH;
	if (slab_id != SLAB_ID_ANY)
		pflags |= umem_slab_flags(umm, slab_id);
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
pmem_reserve(struct umem_instance *umm, void *act, size_t size, unsigned int type_num)
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
pmem_atomic_alloc(struct umem_instance *umm, size_t size,
		  unsigned int type_num)
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
bmem_tx_alloc(struct umem_instance *umm, size_t size, int slab_id,
	      uint64_t flags, unsigned int type_num)
{
	uint64_t pflags = 0;

	if (flags & UMEM_FLAG_ZERO)
		pflags |= DAV_FLAG_ZERO;
	if (flags & UMEM_FLAG_NO_FLUSH)
		pflags |= DAV_FLAG_NO_FLUSH;
	if (slab_id != SLAB_ID_ANY)
		pflags |= umem_slab_flags(umm, slab_id);
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
bmem_reserve(struct umem_instance *umm, void *act, size_t size, unsigned int type_num)
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
	} else if (hint == UMEM_COMMIT_IMMEDIATE) {
		return dav_memcpy_persist(pop, dest, src, len);
	} else { /* UMEM_COMMIT_DEFER */
		return dav_memcpy_persist_relaxed(pop, dest, src, len);
	}
}

static umem_off_t
bmem_atomic_alloc(struct umem_instance *umm, size_t size,
		  unsigned int type_num)
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
vmem_alloc(struct umem_instance *umm, size_t size, int slab_id,
	   uint64_t flags, unsigned int type_num)
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
#ifdef DAOS_PMEM_BUILD
	if (uma->uma_id == UMEM_CLASS_PMEM) {
		if (daos_md_backend == DAOS_MD_BMEM)
			uma->uma_id = UMEM_CLASS_BMEM;
		else if (daos_md_backend == DAOS_MD_ADMEM)
			uma->uma_id = UMEM_CLASS_ADMEM;
		else
			D_ASSERTF(daos_md_backend == DAOS_MD_PMEM,
				  "bad daos_md_backend %d\n", daos_md_backend);
	}
#endif
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
#ifdef DAOS_PMEM_BUILD
	memcpy(umm->umm_slabs, uma->uma_slabs,
	       sizeof(struct umem_slab_desc) * UMM_SLABS_CNT);
#endif

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
umem_reserve(struct umem_instance *umm, struct umem_rsrvd_act *rsrvd_act,
	     size_t size)
{
	if (umm->umm_ops->mo_reserve) {
		void			*act;
		size_t			 act_size = umem_rsrvd_item_size(umm);
		umem_off_t		 off;

		D_ASSERT(rsrvd_act != NULL);
		D_ASSERT(rsrvd_act->rs_actv_cnt > rsrvd_act->rs_actv_at);

		act = rsrvd_act->rs_actv + act_size * rsrvd_act->rs_actv_at;
		off = umm->umm_ops->mo_reserve(umm, act, size,
					       UMEM_TYPE_ANY);
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

int
umem_cache_alloc(struct umem_store *store, uint64_t max_mapped)
{
	struct umem_cache *cache;
	uint64_t           num_pages;
	int                rc = 0;
	int                idx;

	D_ASSERT(store != NULL);

	num_pages = (store->stor_size + UMEM_CACHE_PAGE_SZ - 1) >> UMEM_CACHE_PAGE_SZ_SHIFT;

	if (max_mapped != 0) {
		D_ERROR("Setting max_mapped is unsupported at present\n");
		return -DER_NOTSUPPORTED;
	}

	D_ALLOC(cache, sizeof(*cache) + num_pages * sizeof(cache->ca_pages[0]));
	if (cache == NULL)
		D_GOTO(error, rc = -DER_NOMEM);

	D_DEBUG(DB_IO,
		"Allocated page cache for stor->stor_size=" DF_U64 ", " DF_U64 " pages at %p\n",
		store->stor_size, num_pages, cache);

	cache->ca_store      = store;
	cache->ca_num_pages  = num_pages;
	cache->ca_max_mapped = num_pages;

	D_INIT_LIST_HEAD(&cache->ca_pgs_dirty);
	D_INIT_LIST_HEAD(&cache->ca_pgs_copying);
	D_INIT_LIST_HEAD(&cache->ca_pgs_waiting);
	D_INIT_LIST_HEAD(&cache->ca_pgs_lru);

	for (idx = 0; idx < num_pages; idx++)
		cache->ca_pages[idx].pg_id = idx;

	store->cache = cache;

	return 0;

error:
	D_FREE(cache);
	return rc;
}

int
umem_cache_free(struct umem_store *store)
{
	/** XXX: check reference counts? */
	D_FREE(store->cache);
	return 0;
}

int
umem_cache_check(struct umem_store *store, uint64_t num_pages)
{
	struct umem_cache *cache = store->cache;

	D_ASSERT(num_pages + cache->ca_mapped <= cache->ca_num_pages);

	if (num_pages > cache->ca_max_mapped - cache->ca_mapped)
		return num_pages - (cache->ca_max_mapped - cache->ca_mapped);

	return 0;
}

int
umem_cache_evict(struct umem_store *store, uint64_t num_pages)
{
	/** XXX: Not yet implemented */
	return 0;
}

int
umem_cache_map_range(struct umem_store *store, umem_off_t offset, void *start_addr,
		     uint64_t num_pages)
{
	struct umem_cache *cache = store->cache;
	struct umem_page *page;
	struct umem_page *end_page;
	uint64_t          current_addr = (uint64_t)start_addr;

	page     = umem_cache_off2page(cache, offset);
	end_page = page + num_pages;

	D_ASSERTF(page->pg_id + num_pages <= cache->ca_num_pages,
		  "pg_id=%d, num_pages=" DF_U64 ", cache pages=" DF_U64 "\n", page->pg_id,
		  num_pages, cache->ca_num_pages);

	while (page != end_page) {
		D_ASSERT(page->pg_addr == NULL);
		page->pg_addr = (void *)current_addr;
		current_addr += UMEM_CACHE_PAGE_SZ;

		d_list_add_tail(&page->pg_link, &cache->ca_pgs_lru);
		page++;
	}

	cache->ca_mapped += num_pages;

	return 0;
}

int
umem_cache_pin(struct umem_store *store, umem_off_t addr, daos_size_t size)
{
	struct umem_cache *cache     = store->cache;
	struct umem_page *page      = umem_cache_off2page(cache, addr);
	struct umem_page *end_page  = umem_cache_off2page(cache, addr + size - 1) + 1;

	while (page != end_page) {
		page->pg_ref++;
		page++;
	}

	return 0;
}

int
umem_cache_unpin(struct umem_store *store, umem_off_t addr, daos_size_t size)
{
	struct umem_cache *cache    = store->cache;
	struct umem_page *page     = umem_cache_off2page(cache, addr);
	struct umem_page *end_page = umem_cache_off2page(cache, addr + size - 1) + 1;

	while (page != end_page) {
		D_ASSERT(page->pg_ref >= 1);
		page->pg_ref--;
		page++;
	}

	return 0;
}

static inline void
touch_page(struct umem_store *store, struct umem_page *page, uint64_t wr_tx, umem_off_t first_byte,
	   umem_off_t last_byte)
{
	struct umem_cache *cache = store->cache;
	int start_bit = (first_byte & UMEM_CACHE_PAGE_SZ_MASK) >> UMEM_CACHE_PAGE_SZ_SHIFT;
	int end_bit   = (last_byte & UMEM_CACHE_PAGE_SZ_MASK) >> UMEM_CACHE_PAGE_SZ_SHIFT;

	setbit_range((uint8_t *)&page->pg_bmap[0], start_bit, end_bit);

	if (!page->pg_waiting && page->pg_last_checkpoint == page->pg_last_inflight) {
		/** Keep the page in the waiting list if it's waiting for a transaction to
		 *  be committed to the WAL before it can be flushed.
		 */
		d_list_del(&page->pg_link);
		d_list_add_tail(&page->pg_link, &cache->ca_pgs_dirty);
	}

	if (page->pg_last_inflight == wr_tx)
		return;

	if (store->stor_ops->so_wal_id_cmp(store, wr_tx, page->pg_last_inflight) > 0)
		page->pg_last_inflight = wr_tx;
}

int
umem_cache_touch(struct umem_store *store, uint64_t wr_tx, umem_off_t addr, daos_size_t size)
{
	struct umem_cache *cache     = store->cache;
	struct umem_page *page      = umem_cache_off2page(cache, addr);
	umem_off_t        end_addr  = addr + size - 1;
	struct umem_page *end_page  = umem_cache_off2page(cache, end_addr);
	umem_off_t        start_addr;

	if (page->pg_copying)
		return -DER_CHKPT_BUSY;

	if (page != end_page) {
		/** Eventually, we can just assert equal here.  But until we have a guarantee that
		 * no allocation will span a page boundary, we have to handle this case.  We should
		 * never have to span multiple pages though.
		 */
		if (end_page->pg_copying)
			return -DER_CHKPT_BUSY;
		D_ASSERT((page + 1) == end_page);
		start_addr = end_addr & ~UMEM_CACHE_PAGE_SZ_MASK;

		touch_page(store, end_page, wr_tx, start_addr, end_addr);
		end_addr = start_addr - 1;
	}

	touch_page(store, page, wr_tx, addr, end_addr);

	return 0;
}

/** We can look into other methods later. For now, only handle a few in-flight pages
 *  being checkpointed at a time.  This will limit how much memory we use in each
 *  xstream for checkpointing.
 */
#define MAX_INFLIGHT 8
struct umem_checkpoint_data {
	d_list_t                 cd_link;
	struct umem_store_iod    cd_store_iod;
	d_sg_list_t              cd_sg_list;
	/** Each page can have at most every other chunk so reserve enough space up front to
	 *  handle that.
	 */
	struct umem_store_region cd_regions[UMEM_CACHE_BMAP_SZ / 2];
	d_iov_t                  cd_iovs[UMEM_CACHE_BMAP_SZ / 2];
	/** Handle for the underlying I/O operations */
	daos_handle_t            cd_fh;
};

static void
page2chkpt(struct umem_page *page, struct umem_checkpoint_data *chkpt_data)
{
	uint64_t *bits = &page->pg_bmap[0];
	struct umem_store_iod *store_iod = &chkpt_data->cd_store_iod;
	d_sg_list_t           *sgl       = &chkpt_data->cd_sg_list;
	uint64_t               bmap;
	int       i;
	int                    lead_bits;
	uint64_t  offset    = (uint64_t)page->pg_id << UMEM_CACHE_PAGE_SZ_SHIFT;
	uint64_t               map_offset;
	uint8_t  *page_addr = page->pg_addr;
	int       nr        = 0;
	int                    count;
	uint64_t               mask;
	uint64_t               bit;

	page->pg_chkpt_data = chkpt_data;

	for (i = 0; i < UMEM_CACHE_BMAP_SZ; i++) {
		if (bits[i] == 0)
			goto next_bmap;

		bmap = bits[i];
		do {
			lead_bits  = __builtin_clzll(bmap);
			map_offset = lead_bits << UMEM_CACHE_CHUNK_SZ_SHIFT;
			count      = 0;
			mask       = 0;
			for (;;) {
				bit = 1ULL << (63 - lead_bits);
				if ((bmap & bit) == 0)
					break;
				mask |= bit;
				count++;
			}

			store_iod->io_regions[nr].sr_addr = offset + map_offset;
			store_iod->io_regions[nr].sr_size = count << UMEM_CACHE_CHUNK_SZ_SHIFT;
			sgl->sg_iovs[nr].iov_len          = sgl->sg_iovs[nr].iov_buf_len =
			    count << UMEM_CACHE_CHUNK_SZ_SHIFT;
			sgl->sg_iovs[nr].iov_buf = page_addr + map_offset;
			nr++;

			bmap &= ~mask;
		} while (bmap != 0);

next_bmap:
		offset += UMEM_CACHE_CHUNK_SZ << 6;
		page_addr += UMEM_CACHE_CHUNK_SZ << 6;
	}
	sgl->sg_nr_out = sgl->sg_nr = nr;
	store_iod->io_nr            = nr;
}

/** This is O(n) but the list is tiny so let's keep it simple */
static void
page_insert_sorted(struct umem_store *store, struct umem_page *page, d_list_t *list)
{
	struct umem_page *other;

	d_list_for_each_entry(other, list, pg_link) {
		if (store->stor_ops->so_wal_id_cmp(store, page->pg_last_checkpoint,
						   other->pg_last_checkpoint) < 0) {
			d_list_add(&page->pg_link, &other->pg_link);
			return;
		}
	}

	d_list_add_tail(&page->pg_link, list);
}

int
umem_cache_checkpoint(struct umem_store *store, umem_cache_wait_cb_t wait_cb, void *arg,
		      uint64_t *out_id)
{
	struct umem_cache           *cache    = store->cache;
	struct umem_page            *page     = NULL;
	struct umem_checkpoint_data *chkpt_data_all;
	struct umem_checkpoint_data *chkpt_data;
	uint64_t                     committed_tx = 0;
	uint64_t                     chkpt_id     = 0;
	d_list_t                     free_list;
	int                          i;
	int                          rc;
	int                          inflight = 0;

	if (d_list_empty(&cache->ca_pgs_dirty))
		return 0;

	D_ASSERT(store != NULL);

	D_INIT_LIST_HEAD(&free_list);
	D_ALLOC_ARRAY(chkpt_data_all, MAX_INFLIGHT);
	if (chkpt_data_all == NULL)
		return -DER_NOMEM;

	/** Setup the inflight IODs */
	for (i = 0; i < MAX_INFLIGHT; i++) {
		chkpt_data = &chkpt_data_all[i];
		d_list_add_tail(&chkpt_data->cd_link, &free_list);
		chkpt_data->cd_store_iod.io_regions = &chkpt_data->cd_regions[0];
		chkpt_data->cd_sg_list.sg_iovs      = &chkpt_data->cd_iovs[0];
	}

	d_list_splice_init(&cache->ca_pgs_dirty, &cache->ca_pgs_copying);

	/** First mark all pages in the new list so they won't be moved by an I/O thread.  This
	 *  will enable us to continue the algorithm in relative isolation from I/O threads.
	 */
	d_list_for_each_entry(page, &cache->ca_pgs_copying, pg_link) {
		/** Mark all pages in copying list first.  Marking them as waiting will prevent
		 *  them from being moved to another list by an I/O operation.
		 */
		page->pg_waiting = 1;
		if (store->stor_ops->so_wal_id_cmp(store, page->pg_last_inflight, chkpt_id) > 0)
			page->pg_last_inflight = chkpt_id;
	}

	do {
		/** first try to add up to MAX_INFLIGHT pages to the waiting queue */
		while (inflight < MAX_INFLIGHT &&
		       (page = d_list_pop_entry(&cache->ca_pgs_copying, struct umem_page,
						pg_link)) != NULL) {
			chkpt_data =
			    d_list_pop_entry(&free_list, struct umem_checkpoint_data, cd_link);
			D_ASSERT(chkpt_data != NULL);
			page2chkpt(page, chkpt_data);

			/** Presently, the flush API can yield.  Yielding is fine but ideally,
			 *  we would like it to fail in such cases so we can run page2chkpt again.
			 *  As it stands, we must set the copying bit here to avoid changes to the
			 *  page.
			 */
			page->pg_copying = 1;
			rc = store->stor_ops->so_flush_prep(store, &chkpt_data->cd_store_iod,
							    &chkpt_data->cd_fh);

			/** Need to figure out what errors are possible here and how to handle
			 *  them.  This is wrong but will suffice to get the system kicking
			 */
			D_ASSERT(rc == 0);

			page->pg_last_checkpoint = page->pg_last_inflight;

			rc = store->stor_ops->so_flush_copy(chkpt_data->cd_fh,
							    &chkpt_data->cd_sg_list);
			/** Same comment as above.  Fix this later */
			D_ASSERT(rc == 0);

			page->pg_copying = 0;

			page_insert_sorted(store, page, &cache->ca_pgs_waiting);

			memset(&page->pg_bmap[0], 0, sizeof(page->pg_bmap));
			inflight++;
		}

		page = d_list_pop_entry(&cache->ca_pgs_waiting, struct umem_page, pg_link);

		wait_cb(store, page->pg_last_checkpoint, &committed_tx, arg);

		D_ASSERT(store->stor_ops->so_wal_id_cmp(store, committed_tx,
							page->pg_last_checkpoint) >= 0);

		/** Since the flush API only allows one at a time, let's just do one at a time
		 *  before copying another page.  We can revisit this later if the API allows
		 *  to pass more than one fh.
		 */
		chkpt_data = page->pg_chkpt_data;
		rc         = store->stor_ops->so_flush_post(chkpt_data->cd_fh, 0);
		D_ASSERT(rc == 0);
		if (page->pg_last_inflight != page->pg_last_checkpoint)
			d_list_add_tail(&page->pg_link, &cache->ca_pgs_dirty);
		else
			d_list_add_tail(&page->pg_link, &cache->ca_pgs_lru);
		page->pg_waiting = 0;
		inflight--;
		d_list_add(&chkpt_data->cd_link, &free_list);

	} while (inflight != 0 || !d_list_empty(&cache->ca_pgs_copying));

	D_FREE(chkpt_data_all);

	*out_id = chkpt_id;

	return 0;
}
#endif
