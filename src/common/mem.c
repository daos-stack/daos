/**
 * (C) Copyright 2016-2022 Intel Corporation.
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
/** Sets up global settings for the pmem objects.
 *
 *  \return	0 on success, non-zero on failure.
 */
int
umempobj_settings_init(void)
{
	int					rc;
	enum pobj_arenas_assignment_type	atype;

	atype = POBJ_ARENAS_ASSIGNMENT_GLOBAL;

	rc = pmemobj_ctl_set(NULL, "heap.arenas_assignment_type", &atype);
	if (rc != 0)
		D_ERROR("Could not configure PMDK for global arena: %s\n",
			strerror(errno));
	return rc;
}

/* Persistent object allocator functions */
/** Create a persistent memory object.
 *
 *  \param	path[IN]		Name of the memory pool file
 *  \param	layout_name[IN]		Unique name for the layout
 *  \param	flags[IN]		Additional flags
 *  \param	poolsize[IN]		Size of the pool
 *  \param	mode[IN]		Permission for creating the object
 *
 *  \return	A pointer to the pool, NULL if creation fails.
 */
struct umem_pool *
umempobj_create(const char *path, const char *layout_name, int flags,
		size_t poolsize, mode_t mode)
{
	PMEMobjpool     *pop;
	int              enabled = 1;
	int              rc;

	pop = pmemobj_create(path, layout_name, poolsize, mode);
	if (!pop) {
		D_ERROR("Failed to create pool %s, size="DF_U64": %s\n",
			path, poolsize, pmemobj_errormsg());
		return NULL;
	}
	if (flags & UMEMPOBJ_ENABLE_STATS) {
		rc = pmemobj_ctl_set(pop, "stats.enabled", &enabled);
		if (rc) {
			D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
				DP_RC(rc));
			rc = umem_tx_errno(rc);
			pmemobj_close(pop);
			pop = NULL;
		}
	}
	return ((struct umem_pool *)pop);
}

/** Open the given persistent memory object.
 *
 *  \param	path[IN]		Name of the memory pool file
 *  \param	layout_name[IN]		Name of the layout [PMDK]
 *  \param	flags[IN]		Additional flags
 *
 *  \return	A pointer to the pool, NULL if creation fails.
 */
struct umem_pool *
umempobj_open(const char *path, const char *layout_name, int flags)
{
	PMEMobjpool     *pop;
	int              enabled = 1;
	int              rc;

	pop = pmemobj_open(path, layout_name);
	if (!pop) {
		D_ERROR("Error in opening the pool %s: %s\n",
			path, pmemobj_errormsg());
		return NULL;
	}
	if (flags & UMEMPOBJ_ENABLE_STATS) {
		rc = pmemobj_ctl_set(pop, "stats.enabled", &enabled);
		if (rc) {
			D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
				DP_RC(rc));
			rc = umem_tx_errno(rc);
			pmemobj_close(pop);
			pop = NULL;
		}
	}
	return ((struct umem_pool *)pop);
}

/** Close the pmem object.
 *
 *  \param	ph_p[IN]		pointer to the pool object.
 */
void
umempobj_close(struct umem_pool *ph_p)
{
	PMEMobjpool *pop = (PMEMobjpool *)ph_p;

	pmemobj_close(pop);
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
	PMEMobjpool *pop = (PMEMobjpool *)ph_p;
	PMEMoid root;
	void *rootp;

	root = pmemobj_root(pop, size);
	rootp = pmemobj_direct(root);
	return rootp;
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
	int rc;
	PMEMobjpool *pop = (PMEMobjpool *)ph_p;

	rc = pmemobj_ctl_get(pop, "stats.heap.curr_allocated",
			     curr_allocated);
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
	daos_size_t scm_used, scm_active;
	PMEMobjpool *pop = (PMEMobjpool *)ph_p;

	pmemobj_ctl_get(pop, "stats.heap.run_allocated", &scm_used);
	pmemobj_ctl_get(pop, "stats.heap.run_active", &scm_active);

	D_ERROR("Fragmentation info, run_allocated: "
	  DF_U64", run_active: "DF_U64"\n", scm_used, scm_active);
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
	int rc;
	struct pobj_alloc_class_desc pmemslab;
	PMEMobjpool *pop = (PMEMobjpool *)ph_p;

	pmemslab.unit_size = slab->unit_size;
	pmemslab.alignment = 0;
	pmemslab.units_per_block = 1000;
	pmemslab.header_type = POBJ_HEADER_NONE;
	pmemslab.class_id = slab->class_id;
	rc = pmemobj_ctl_set(pop, "heap.alloc_class.new.desc", &pmemslab);
	/* update with the new slab id */
	slab->class_id = pmemslab.class_id;
	return rc;
}

static inline uint64_t
umem_slab_flags(struct umem_instance *umm, unsigned int slab_id)
{
	D_ASSERT(slab_id < UMM_SLABS_CNT);
	return POBJ_CLASS_ID(umm->umm_slabs[slab_id].class_id);
}

bool
umem_tx_none(void)
{
	return (pmemobj_tx_stage() == TX_STAGE_NONE);
}

bool
umem_tx_inprogress(void)
{
	return (pmemobj_tx_stage() == TX_STAGE_WORK);
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
pmem_process_cb_vec(struct umem_tx_stage_item *vec, unsigned int *cnt,
		    bool noop)
{
	struct umem_tx_stage_item	*txi, *txi_arr;
	unsigned int			 i, num = *cnt;

	if (num == 0)
		return;

	/* @vec & @cnt could be changed by other ULT while txi_fn yielding */
	D_ALLOC_ARRAY(txi_arr, num);
	if (txi_arr == NULL) {
		D_ERROR("Failed to allocate txi array\n");
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

/*
 * This callback will be called on the outermost transaction commit (stage
 * == TX_STAGE_ONCOMMIT), abort (stage == TX_STAGE_ONABORT) and end (stage
 * == TX_STAGE_NONE).
 */
static void
pmem_stage_callback(PMEMobjpool *pop, int stage, void *data)
{
	struct umem_tx_stage_data	*txd = data;
	struct umem_tx_stage_item	*vec;
	unsigned int			*cnt;

	D_ASSERT(stage >= TX_STAGE_NONE && stage < MAX_TX_STAGE);
	D_ASSERT(txd != NULL);
	D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);

	switch (stage) {
	case TX_STAGE_ONCOMMIT:
		vec = txd->txd_commit_vec;
		cnt = &txd->txd_commit_cnt;
		/* Abandon the abort callbacks */
		pmem_process_cb_vec(txd->txd_abort_vec, &txd->txd_abort_cnt,
				    true);
		break;
	case TX_STAGE_ONABORT:
		vec = txd->txd_abort_vec;
		cnt = &txd->txd_abort_cnt;
		/* Abandon the commit callbacks */
		pmem_process_cb_vec(txd->txd_commit_vec, &txd->txd_commit_cnt,
				    true);
		break;
	case TX_STAGE_NONE:
		D_ASSERT(txd->txd_commit_cnt == 0);
		D_ASSERT(txd->txd_abort_cnt == 0);
		vec = txd->txd_end_vec;
		cnt = &txd->txd_end_cnt;
		break;
	default:
		/* Ignore all other stages */
		return;
	}

	pmem_process_cb_vec(vec, cnt, false);
}

static int
pmem_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	int rc;
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;

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
pmem_tx_commit(struct umem_instance *umm)
{
	int rc;

	pmemobj_tx_commit();
	rc = pmemobj_tx_end();

	return rc ? umem_tx_errno(rc) : 0;
}

static void
pmem_defer_free(struct umem_instance *umm, umem_off_t off,
		struct pobj_action *act)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;
	PMEMoid	id = umem_off2id(umm, off);

	pmemobj_defer_free(pop, id, act);
}

static umem_off_t
pmem_reserve(struct umem_instance *umm, struct pobj_action *act, size_t size,
	     unsigned int type_num)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;

	return umem_id2off(umm, pmemobj_reserve(pop, act, size, type_num));
}

static void
pmem_cancel(struct umem_instance *umm, struct pobj_action *actv, int actv_cnt)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;

	pmemobj_cancel(pop, actv, actv_cnt);
}

static int
pmem_tx_publish(struct umem_instance *umm, struct pobj_action *actv,
		int actv_cnt)
{
	int	rc;

	rc = pmemobj_tx_publish(actv, actv_cnt);
	return rc ? umem_tx_errno(rc) : 0;
}

static void *
pmem_atomic_copy(struct umem_instance *umm, void *dest, const void *src,
		 size_t len)
{
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;

	return pmemobj_memcpy_persist(pop, dest, src, len);
}

static umem_off_t
pmem_atomic_alloc(struct umem_instance *umm, size_t size,
		  unsigned int type_num)
{
	PMEMoid oid;
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;
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
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;

	pmemobj_flush(pop, addr, len);
}

static int
pmem_tx_add_callback(struct umem_instance *umm, struct umem_tx_stage_data *txd,
		     int stage, umem_tx_cb_t cb, void *data)
{
	struct umem_tx_stage_item	*txi, **pvec;
	unsigned int			*cnt, *cnt_max;

	D_ASSERT(txd != NULL);
	D_ASSERT(txd->txd_magic == UMEM_TX_DATA_MAGIC);
	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK);

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
	.mo_reserve		= pmem_reserve,
	.mo_defer_free		= pmem_defer_free,
	.mo_cancel		= pmem_cancel,
	.mo_tx_publish		= pmem_tx_publish,
	.mo_atomic_copy		= pmem_atomic_copy,
	.mo_atomic_alloc	= pmem_atomic_alloc,
	.mo_atomic_free		= pmem_atomic_free,
	.mo_atomic_flush	= pmem_atomic_flush,
	.mo_tx_add_callback	= pmem_tx_add_callback,
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
	char		*root;
	PMEMoid		 root_oid;
	PMEMobjpool *pop = (PMEMobjpool *)umm->umm_pool;
#endif
	if (umm->umm_id == UMEM_CLASS_VMEM) {
		umm->umm_base = 0;
		umm->umm_pool_uuid_lo = 0;
		return;
	}

#ifdef DAOS_PMEM_BUILD
	root_oid = pmemobj_root(pop, 0);
	D_ASSERTF(!OID_IS_NULL(root_oid),
		  "You must call pmemobj_root before umem_class_init\n");

	root = pmemobj_direct(root_oid);

	umm->umm_pool_uuid_lo = root_oid.pool_uuid_lo;
	umm->umm_base = (uint64_t)root - root_oid.off;
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
	unsigned int		rs_actv_cnt;
	unsigned int		rs_actv_at;
	struct pobj_action	rs_actv[0];

};

int
umem_rsrvd_act_cnt(struct umem_rsrvd_act *rsrvd_act)
{
	if (rsrvd_act == NULL)
		return 0;
	return rsrvd_act->rs_actv_at;
}

int
umem_rsrvd_act_alloc(struct umem_rsrvd_act **rsrvd_act, int cnt)
{
	size_t size;

	size = sizeof(struct umem_rsrvd_act) +
		sizeof(struct pobj_action) * cnt;
	D_ALLOC(*rsrvd_act, size);
	if (*rsrvd_act == NULL)
		return -DER_NOMEM;

	(*rsrvd_act)->rs_actv_cnt = cnt;
	return 0;
}

int
umem_rsrvd_act_realloc(struct umem_rsrvd_act **rsrvd_act, int max_cnt)
{
	if (*rsrvd_act == NULL ||
	    (*rsrvd_act)->rs_actv_cnt < max_cnt) {
		struct umem_rsrvd_act    *tmp_rsrvd_act;
		size_t                   size;

		size = sizeof(struct umem_rsrvd_act) *
			sizeof(struct pobj_action) * max_cnt;

		D_REALLOC_Z(tmp_rsrvd_act, *rsrvd_act, size);
		if (tmp_rsrvd_act == NULL)
			return -DER_NOMEM;

		*rsrvd_act = tmp_rsrvd_act;
		(*rsrvd_act)->rs_actv_cnt = max_cnt;
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
		struct pobj_action	*act;
		umem_off_t		 off;

		D_ASSERT(rsrvd_act != NULL);
		D_ASSERT(rsrvd_act->rs_actv_cnt > rsrvd_act->rs_actv_at);

		act = &rsrvd_act->rs_actv[rsrvd_act->rs_actv_at];
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
		struct pobj_action      *act;

		act = &rsrvd_act->rs_actv[rsrvd_act->rs_actv_at];
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
#endif
