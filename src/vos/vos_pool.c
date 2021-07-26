/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Implementation for pool specific functions in VOS
 *
 * vos/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_srv/ras.h>
#include <daos_errno.h>
#include <gurt/hash.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/* NB: None of pmemobj_create/open/close is thread-safe */
pthread_mutex_t vos_pmemobj_lock = PTHREAD_MUTEX_INITIALIZER;

int
vos_pool_settings_init(void)
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

static inline PMEMobjpool *
vos_pmemobj_create(const char *path, const char *layout, size_t poolsize,
		   mode_t mode)
{
	PMEMobjpool *pop;

	D_MUTEX_LOCK(&vos_pmemobj_lock);
	pop = pmemobj_create(path, layout, poolsize, mode);
	D_MUTEX_UNLOCK(&vos_pmemobj_lock);
	return pop;
}

static inline PMEMobjpool *
vos_pmemobj_open(const char *path, const char *layout)
{
	PMEMobjpool *pop;

	D_MUTEX_LOCK(&vos_pmemobj_lock);
	pop = pmemobj_open(path, layout);
	D_MUTEX_UNLOCK(&vos_pmemobj_lock);
	return pop;
}

static inline void
vos_pmemobj_close(PMEMobjpool *pop)
{
	D_MUTEX_LOCK(&vos_pmemobj_lock);
	pmemobj_close(pop);
	D_MUTEX_UNLOCK(&vos_pmemobj_lock);
}

static inline struct vos_pool_df *
vos_pool_pop2df(PMEMobjpool *pop)
{
	TOID(struct vos_pool_df) pool_df;

	pool_df = POBJ_ROOT(pop, struct vos_pool_df);
	return D_RW(pool_df);
}

static struct vos_pool *
pool_hlink2ptr(struct d_ulink *hlink)
{
	D_ASSERT(hlink != NULL);
	return container_of(hlink, struct vos_pool, vp_hlink);
}

static void
pool_hop_free(struct d_ulink *hlink)
{
	struct vos_pool	*pool = pool_hlink2ptr(hlink);
	int		 rc;

	D_ASSERT(pool->vp_opened == 0);
	D_ASSERT(!gc_have_pool(pool));

	if (pool->vp_io_ctxt != NULL) {
		rc = bio_ioctxt_close(pool->vp_io_ctxt,
				      pool->vp_pool_df->pd_nvme_sz == 0);
		if (rc)
			D_ERROR("Closing VOS I/O context:%p pool:"DF_UUID" : "
				DF_RC"\n", pool->vp_io_ctxt,
				DP_UUID(pool->vp_id), DP_RC(rc));
		else
			D_DEBUG(DB_MGMT, "Closed VOS I/O context:%p pool:"
				DF_UUID"\n",
				pool->vp_io_ctxt, DP_UUID(pool->vp_id));
	}

	if (pool->vp_vea_info != NULL)
		vea_unload(pool->vp_vea_info);

	if (daos_handle_is_valid(pool->vp_cont_th))
		dbtree_close(pool->vp_cont_th);

	if (pool->vp_uma.uma_pool)
		vos_pmemobj_close(pool->vp_uma.uma_pool);

	vos_dedup_fini(pool);

	D_FREE(pool);
}

static struct d_ulink_ops   pool_uuid_hops = {
	.uop_free       = pool_hop_free,
};

/** allocate DRAM instance of vos pool */
static int
pool_alloc(uuid_t uuid, struct vos_pool **pool_p)
{
	struct vos_pool		*pool;

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		return -DER_NOMEM;

	d_uhash_ulink_init(&pool->vp_hlink, &pool_uuid_hops);
	D_INIT_LIST_HEAD(&pool->vp_gc_link);
	D_INIT_LIST_HEAD(&pool->vp_gc_cont);
	uuid_copy(pool->vp_id, uuid);

	*pool_p = pool;
	return 0;
}

static int
pool_link(struct vos_pool *pool, struct d_uuid *ukey, daos_handle_t *poh)
{
	int	rc;

	rc = d_uhash_link_insert(vos_pool_hhash_get(), ukey, NULL,
				 &pool->vp_hlink);
	if (rc) {
		D_ERROR("uuid hash table insert failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(failed, rc);
	}
	*poh = vos_pool2hdl(pool);
	return 0;
failed:
	return rc;
}

static int
pool_lookup(struct d_uuid *ukey, struct vos_pool **pool)
{
	struct d_ulink *hlink;

	hlink = d_uhash_link_lookup(vos_pool_hhash_get(), ukey, NULL);
	if (hlink == NULL) {
		D_DEBUG(DB_MGMT, "can't find "DF_UUID"\n", DP_UUID(ukey->uuid));
		return -DER_NONEXIST;
	}

	*pool = pool_hlink2ptr(hlink);
	return 0;
}

static int
vos_blob_format_cb(void *cb_data, struct umem_instance *umem)
{
	struct bio_blob_hdr	*blob_hdr = cb_data;
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct bio_io_context	*ioctxt;
	int			 rc;

	/* Create a bio_io_context to get the blob */
	rc = bio_ioctxt_open(&ioctxt, xs_ctxt, umem, blob_hdr->bbh_pool, false);
	if (rc) {
		D_ERROR("Failed to create an I/O context for writing blob "
			"header: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Write the blob header info to blob offset 0 */
	rc = bio_write_blob_hdr(ioctxt, blob_hdr);
	if (rc)
		D_ERROR("Failed to write header for blob:"DF_U64" : "DF_RC"\n",
			blob_hdr->bbh_blob_id, DP_RC(rc));

	rc = bio_ioctxt_close(ioctxt, false);
	if (rc)
		D_ERROR("Failed to free I/O context: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Unmap (TRIM) the extent being freed
 */
static int
vos_blob_unmap_cb(uint64_t off, uint64_t cnt, void *data)
{
	struct bio_io_context	*ioctxt = data;
	int			 rc;

	/* unmap unused pages for NVMe media to perform more efficiently */
	rc = bio_blob_unmap(ioctxt, off, cnt);
	if (rc)
		D_ERROR("Failed to unmap blob\n");

	return rc;
}

static int pool_open(PMEMobjpool *ph, struct vos_pool_df *pool_df, uuid_t uuid,
		     unsigned int flags, daos_handle_t *poh);

int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t scm_sz,
		daos_size_t nvme_sz, unsigned int flags, daos_handle_t *poh)
{
	PMEMobjpool		*ph;
	struct umem_attr	 uma = {0};
	struct umem_instance	 umem = {0};
	struct vos_pool_df	*pool_df;
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct bio_blob_hdr	 blob_hdr;
	daos_handle_t		 hdl;
	struct d_uuid		 ukey;
	struct vos_pool		*pool = NULL;
	int			 rc = 0, enabled = 1;

	if (!path || uuid_is_null(uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Pool Path: %s, size: "DF_U64":"DF_U64", "
		"UUID: "DF_UUID"\n", path, scm_sz, nvme_sz, DP_UUID(uuid));

	if (flags & VOS_POF_SMALL)
		flags |= VOS_POF_EXCL;

	uuid_copy(ukey.uuid, uuid);
	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D_ASSERT(pool != NULL);
		D_DEBUG(DB_MGMT, "Found already opened(%d) pool : %p\n",
			pool->vp_opened, pool);
		vos_pool_decref(pool);
		return -DER_EXIST;
	}

	/* Path must be a file with a certain size when size argument is 0 */
	if (!scm_sz && access(path, F_OK) == -1) {
		D_ERROR("File not accessible (%d) when size is 0\n", errno);
		return daos_errno2der(errno);
	}

	ph = vos_pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout), scm_sz,
				0600);
	if (!ph) {
		rc = errno;
		D_ERROR("Failed to create pool %s, size="DF_U64": %s\n", path,
			scm_sz, pmemobj_errormsg());
		return daos_errno2der(rc);
	}

	rc = pmemobj_ctl_set(ph, "stats.enabled", &enabled);
	if (rc) {
		D_ERROR("Enable SCM usage statistics failed. "DF_RC"\n",
			DP_RC(rc));
		rc = umem_tx_errno(rc);
		goto close;
	}

	pool_df = vos_pool_pop2df(ph);

	/* If the file is fallocated separately we need the fallocated size
	 * for setting in the root object.
	 */
	if (!scm_sz) {
		struct stat lstat;

		rc = stat(path, &lstat);
		if (rc != 0)
			D_GOTO(close, rc = daos_errno2der(errno));
		scm_sz = lstat.st_size;
	}

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_pool = ph;

	rc = umem_class_init(&uma, &umem);
	if (rc != 0)
		goto close;

	rc = umem_tx_begin(&umem, NULL);
	if (rc != 0)
		goto close;

	rc = umem_tx_add_ptr(&umem, pool_df, sizeof(*pool_df));
	if (rc != 0)
		goto end;

	memset(pool_df, 0, sizeof(*pool_df));
	rc = dbtree_create_inplace(VOS_BTR_CONT_TABLE, 0, VOS_CONT_ORDER,
				   &uma, &pool_df->pd_cont_root, &hdl);
	if (rc != 0)
		goto end;

	dbtree_close(hdl);

	uuid_copy(pool_df->pd_id, uuid);
	pool_df->pd_scm_sz	= scm_sz;
	pool_df->pd_nvme_sz	= nvme_sz;
	pool_df->pd_magic	= POOL_DF_MAGIC;
	if (DAOS_FAIL_CHECK(FLC_POOL_DF_VER))
		pool_df->pd_version = 0;
	else
		pool_df->pd_version = POOL_DF_VERSION;

	gc_init_pool(&umem, pool_df);
end:
	/**
	 * The transaction can in reality be aborted
	 * only when there is no memory, either due
	 * to loss of power or no more memory in pool
	 */
	if (rc == 0)
		rc = umem_tx_commit(&umem);
	else
		rc = umem_tx_abort(&umem, rc);

	if (rc != 0) {
		D_ERROR("Initialize pool root error: "DF_RC"\n", DP_RC(rc));
		goto close;
	}

	/* SCM only pool or NVMe device isn't configured */
	if (nvme_sz == 0 || !bio_nvme_configured())
		goto open;

	/* Create SPDK blob on NVMe device */
	D_DEBUG(DB_MGMT, "Creating blob for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(uuid));
	rc = bio_blob_create(uuid, xs_ctxt, nvme_sz);
	if (rc != 0) {
		D_ERROR("Error creating blob for xs:%p pool:"DF_UUID" "
			""DF_RC"\n", xs_ctxt, DP_UUID(uuid), DP_RC(rc));
		goto close;
	}

	/* Format SPDK blob header */
	blob_hdr.bbh_blk_sz = VOS_BLK_SZ;
	blob_hdr.bbh_hdr_sz = VOS_BLOB_HDR_BLKS;
	uuid_copy(blob_hdr.bbh_pool, uuid);

	/* Format SPDK blob*/
	rc = vea_format(&umem, vos_txd_get(), &pool_df->pd_vea_df, VOS_BLK_SZ,
			VOS_BLOB_HDR_BLKS, nvme_sz, vos_blob_format_cb,
			&blob_hdr, false);
	if (rc) {
		D_ERROR("Format blob error for xs:%p pool:"DF_UUID" "DF_RC"\n",
			xs_ctxt, DP_UUID(uuid), DP_RC(rc));
		/* Destroy the SPDK blob on error */
		rc = bio_blob_delete(uuid, xs_ctxt);
		goto close;
	}

open:
	/* If the caller does not want a VOS pool handle, we're done. */
	if (poh == NULL)
		goto close;

	/* Create a VOS pool handle using ph. */
	rc = pool_open(ph, pool_df, uuid, flags, poh);
	if (rc != 0)
		goto close;
	ph = NULL;

close:
	/* Close this local handle, if it hasn't been consumed by pool_open. */
	if (ph != NULL)
		vos_pmemobj_close(ph);
	return rc;
}

/**
 * kill the pool before destroy:
 * - detach from GC, delete SPDK blob
 */
int
vos_pool_kill(uuid_t uuid, bool force)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct d_uuid		 ukey;
	int			 rc;

	uuid_copy(ukey.uuid, uuid);
	while (1) {
		struct vos_pool	*pool = NULL;

		rc = pool_lookup(&ukey, &pool);
		if (rc) {
			D_ASSERT(rc == -DER_NONEXIST);
			rc = 0;
			break;
		}

		D_ASSERT(pool != NULL);
		pool->vp_dying = 1;
		if (gc_have_pool(pool)) {
			/* still pinned by GC, un-pin it because there is no
			 * need to run GC for this pool anymore.
			 */
			gc_del_pool(pool);
			vos_pool_decref(pool); /* -1 for lookup */
			continue;	/* try again */
		}
		vos_pool_decref(pool); /* -1 for lookup */

		if (force) {
			D_ERROR("Open reference exists, force kill\n");
			/* NB: rebuild might still take refcount vos_pool,
			 * we don't want to fail destroy or locking space.
			 */
			break;
		}
		D_ERROR("Open reference exists, cannot kill pool\n");
		return -DER_BUSY;
	}
	D_DEBUG(DB_MGMT, "No open handles, OK to delete\n");

	/* NVMe device is configured */
	if (bio_nvme_configured() && xs_ctxt) {
		D_DEBUG(DB_MGMT, "Deleting blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(uuid));
		rc = bio_blob_delete(uuid, xs_ctxt);
		if (rc) {
			D_ERROR("Destroy blob for pool="DF_UUID" rc=%s\n",
				DP_UUID(uuid), d_errstr(rc));
			/* do not return the error, nothing we can do */
		}
	}
	return 0;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP) and revoke all its handles
 */
int
vos_pool_destroy(const char *path, uuid_t uuid)
{
	int	rc;

	D_DEBUG(DB_MGMT, "delete path: %s UUID: "DF_UUID"\n",
		path, DP_UUID(uuid));

	rc = vos_pool_kill(uuid, false);
	if (rc)
		return rc;

	/**
	 * NB: no need to explicitly destroy container index table because
	 * pool file removal will do this for free.
	 */
	if (daos_file_is_dax(path)) {
		int	 fd;
		int	 len = 2 * (1 << 20UL);
		void	*addr;

		fd = open(path, O_RDWR);
		if (fd < 0) {
			if (errno == ENOENT)
				D_GOTO(exit, rc = 0);

			D_ERROR("Failed to open %s: %d\n", path, errno);
			D_GOTO(exit, rc = daos_errno2der(errno));
		}

		addr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (addr == MAP_FAILED) {
			close(fd);
			D_ERROR("Failed to mmap %s, len:%d: %d\n", path, len,
				errno);
			D_GOTO(exit, rc = daos_errno2der(errno));
		}
		memset((char *)addr, 0, len);

		rc = munmap(addr, len);
		if (rc) {
			close(fd);
			D_ERROR("Failed to munmap %s: %d\n", path, errno);
			D_GOTO(exit, rc = daos_errno2der(errno));
		}
		close(fd);
	} else {
		rc = remove(path);
		if (rc) {
			if (errno == ENOENT)
				D_GOTO(exit, rc = 0);
			D_ERROR("Failure deleting file from PMEM: %s\n",
				strerror(errno));
		}
	}
exit:
	return rc;
}

static int
set_slab_prop(int id, struct pobj_alloc_class_desc *slab)
{
	struct daos_tree_overhead	ovhd = { 0 };
	int				tclass, *size, rc;

	if (id == VOS_SLAB_OBJ_DF) {
		slab->unit_size = sizeof(struct vos_obj_df);
		goto done;
	}

	size = &ovhd.to_leaf_overhead.no_size;

	switch (id) {
	case VOS_SLAB_OBJ_NODE:
		tclass = VOS_TC_OBJECT;
		break;
	case VOS_SLAB_KEY_NODE:
		tclass = VOS_TC_DKEY;
		break;
	case VOS_SLAB_SV_NODE:
		tclass = VOS_TC_SV;
		break;
	case VOS_SLAB_EVT_NODE:
		tclass = VOS_TC_ARRAY;
		break;
	case VOS_SLAB_EVT_NODE_SM:
		tclass = VOS_TC_ARRAY;
		size = &ovhd.to_int_node_size;
		break;
	case VOS_SLAB_EVT_DESC:
		tclass = VOS_TC_ARRAY;
		size = &ovhd.to_record_msize;
		break;
	default:
		D_ERROR("Invalid slab ID: %d\n", id);
		return -DER_INVAL;
	}

	rc = vos_tree_get_overhead(0, tclass, 0, &ovhd);
	if (rc)
		return rc;

	slab->unit_size = *size;
done:
	D_ASSERT(slab->unit_size > 0);
	D_DEBUG(DB_MGMT, "Slab ID:%d, Size:%lu\n", id, slab->unit_size);

	slab->alignment = 0;
	slab->units_per_block = 1000;
	slab->header_type = POBJ_HEADER_NONE;

	return 0;
}

static int
vos_register_slabs(struct umem_attr *uma)
{
	struct pobj_alloc_class_desc	*slab;
	int				 i, rc;

	D_ASSERT(uma->uma_pool != NULL);
	for (i = 0; i < VOS_SLAB_MAX; i++) {
		slab = &uma->uma_slabs[i];

		D_ASSERT(slab->class_id == 0);
		rc = set_slab_prop(i, slab);
		if (rc) {
			D_ERROR("Failed to get unit size %d. rc:%d\n", i, rc);
			return rc;
		}

		rc = pmemobj_ctl_set(uma->uma_pool, "heap.alloc_class.new.desc",
				     slab);
		if (rc) {
			D_ERROR("Failed to register VOS slab %d. rc:%d\n",
				i, rc);
			rc = umem_tx_errno(rc);
			return rc;
		}
		D_ASSERT(slab->class_id != 0);
	}

	return 0;
}

/*
 * If successful, this function consumes ph, which the caller shall not close
 * in this case.
 */
static int
pool_open(PMEMobjpool *ph, struct vos_pool_df *pool_df, uuid_t uuid,
	  unsigned int flags, daos_handle_t *poh)
{
	struct bio_xs_context	*xs_ctxt;
	struct vos_pool		*pool = NULL;
	struct umem_attr	*uma;
	struct d_uuid		 ukey;
	int			 rc;

	/* Create a new handle during open */
	rc = pool_alloc(uuid, &pool); /* returned with refcount=1 */
	if (rc != 0) {
		D_ERROR("Error allocating pool handle\n");
		return rc;
	}

	uma = &pool->vp_uma;
	uma->uma_id = UMEM_CLASS_PMEM;
	uma->uma_pool = ph;

	rc = vos_register_slabs(uma);
	if (rc) {
		D_ERROR("Register slabs failed. rc:%d\n", rc);
		D_GOTO(failed, rc);
	}

	/* initialize a umem instance for later btree operations */
	rc = umem_class_init(uma, &pool->vp_umm);
	if (rc != 0) {
		D_ERROR("Failed to instantiate umem: "DF_RC"\n", DP_RC(rc));
		D_GOTO(failed, rc);
	}

	/* Cache container table btree hdl */
	rc = dbtree_open_inplace_ex(&pool_df->pd_cont_root, &pool->vp_uma,
				    DAOS_HDL_INVAL, pool, &pool->vp_cont_th);
	if (rc) {
		D_ERROR("Container Tree open failed\n");
		D_GOTO(failed, rc);
	}

	xs_ctxt = vos_xsctxt_get();

	D_DEBUG(DB_MGMT, "Opening VOS I/O context for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(uuid));
	rc = bio_ioctxt_open(&pool->vp_io_ctxt, xs_ctxt, &pool->vp_umm, uuid,
			     pool_df->pd_nvme_sz == 0);
	if (rc) {
		D_ERROR("Failed to open VOS I/O context for xs:%p "
			"pool:"DF_UUID" rc="DF_RC"\n", xs_ctxt, DP_UUID(uuid),
			DP_RC(rc));
		goto failed;
	}

	if (bio_nvme_configured() && pool_df->pd_nvme_sz != 0) {
		struct vea_unmap_context unmap_ctxt;

		/* set unmap callback fp */
		unmap_ctxt.vnc_unmap = vos_blob_unmap_cb;
		unmap_ctxt.vnc_data = pool->vp_io_ctxt;
		rc = vea_load(&pool->vp_umm, vos_txd_get(), &pool_df->pd_vea_df,
			      &unmap_ctxt, &pool->vp_vea_info);
		if (rc) {
			D_ERROR("Failed to load block space info: "DF_RC"\n",
				DP_RC(rc));
			goto failed;
		}
	}

	rc = vos_dedup_init(pool);
	if (rc)
		goto failed;

	/* Insert the opened pool to the uuid hash table */
	uuid_copy(ukey.uuid, uuid);
	rc = pool_link(pool, &ukey, poh);
	if (rc) {
		D_ERROR("Error inserting into vos DRAM hash\n");
		D_GOTO(failed, rc);
	}

	pool->vp_dtx_committed_count = 0;
	pool->vp_pool_df = pool_df;
	pool->vp_opened = 1;
	pool->vp_excl = !!(flags & VOS_POF_EXCL);
	pool->vp_small = !!(flags & VOS_POF_SMALL);
	vos_space_sys_init(pool);
	/* Ensure GC is triggered after server restart */
	gc_add_pool(pool);
	D_DEBUG(DB_MGMT, "Opened pool %p\n", pool);
	return 0;
failed:
	vos_pool_decref(pool); /* -1 for myself */
	return rc;
}

int
vos_pool_open(const char *path, uuid_t uuid, unsigned int flags,
	      daos_handle_t *poh)
{
	struct vos_pool_df	*pool_df;
	struct vos_pool		*pool = NULL;
	struct d_uuid		 ukey;
	PMEMobjpool		*ph;
	int			 rc, enabled = 1;

	if (path == NULL || poh == NULL) {
		D_ERROR("Invalid parameters.\n");
		return -DER_INVAL;
	}

	uuid_copy(ukey.uuid, uuid);
	D_DEBUG(DB_MGMT, "Pool Path: %s, UUID: "DF_UUID"\n", path,
		DP_UUID(uuid));

	if (flags & VOS_POF_SMALL)
		flags |= VOS_POF_EXCL;

	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D_ASSERT(pool != NULL);
		D_DEBUG(DB_MGMT, "Found already opened(%d) pool : %p\n",
			pool->vp_opened, pool);
		if ((flags & VOS_POF_EXCL) || pool->vp_excl) {
			vos_pool_decref(pool);
			return -DER_BUSY;
		}
		pool->vp_opened++;
		*poh = vos_pool2hdl(pool);
		return 0;
	}

	ph = vos_pmemobj_open(path, POBJ_LAYOUT_NAME(vos_pool_layout));
	if (ph == NULL) {
		rc = errno;
		D_ERROR("Error in opening the pool "DF_UUID": %s\n",
			DP_UUID(uuid), pmemobj_errormsg());
		return daos_errno2der(rc);
	}

	rc = pmemobj_ctl_set(ph, "stats.enabled", &enabled);
	if (rc) {
		D_ERROR("Enable SCM usage statistics failed. rc:%d\n",
			umem_tx_errno(rc));
		goto out;
	}

	pool_df = vos_pool_pop2df(ph);
	if (pool_df->pd_magic != POOL_DF_MAGIC) {
		D_CRIT("Unknown DF magic %x\n", pool_df->pd_magic);
		rc = -DER_DF_INVAL;
		goto out;
	}

	if (pool_df->pd_version > POOL_DF_VERSION ||
	    pool_df->pd_version < POOL_DF_VER_1) {
		D_ERROR("Unsupported DF version %x\n", pool_df->pd_version);
		/** Send a RAS notification */
		vos_report_layout_incompat("VOS pool", pool_df->pd_version,
					   POOL_DF_VER_1, POOL_DF_VERSION,
					   &ukey.uuid);
		rc = -DER_DF_INCOMPT;
		goto out;
	}

	if (uuid_compare(uuid, pool_df->pd_id)) {
		D_ERROR("Mismatch uuid, user="DF_UUIDF", pool="DF_UUIDF"\n",
			DP_UUID(uuid), DP_UUID(pool_df->pd_id));
		rc = -DER_IO;
		goto out;
	}

	rc = pool_open(ph, pool_df, uuid, flags, poh);
	if (rc != 0)
		goto out;
	ph = NULL;

out:
	/* Close this local handle, if it hasn't been consumed by pool_open. */
	if (ph != NULL)
		vos_pmemobj_close(ph);
	return rc;
}

/**
 * Close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 */
int
vos_pool_close(daos_handle_t poh)
{
	struct vos_pool	*pool;

	pool = vos_hdl2pool(poh);
	if (pool == NULL) {
		D_ERROR("Cannot close a NULL handle\n");
		return -DER_NO_HDL;
	}
	D_DEBUG(DB_MGMT, "Close opened(%d) pool "DF_UUID" (%p).\n",
		pool->vp_opened, DP_UUID(pool->vp_id), pool);

	D_ASSERT(pool->vp_opened > 0);
	pool->vp_opened--;

	/* If the last reference is holding by GC */
	if (pool->vp_opened == 1 && gc_have_pool(pool))
		gc_del_pool(pool);
	else if (pool->vp_opened == 0)
		vos_pool_hash_del(pool);

	vos_pool_decref(pool); /* -1 for myself */
	return 0;
}

/**
 * Query attributes and statistics of the current pool
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo)
{
	struct vos_pool		*pool;
	struct vos_pool_df	*pool_df;
	int			 rc;

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	pool_df = pool->vp_pool_df;

	D_ASSERT(pinfo != NULL);
	pinfo->pif_cont_nr = pool_df->pd_cont_nr;
	pinfo->pif_gc_stat = pool->vp_gc_stat;

	rc = vos_space_query(pool, &pinfo->pif_space, true);
	if (rc)
		D_ERROR("Query pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(pool->vp_id), DP_RC(rc));
	return rc;
}

int
vos_pool_query_space(uuid_t pool_id, struct vos_pool_space *vps)
{
	struct vos_pool	*pool = NULL;
	struct d_uuid	 ukey;
	int		 rc;

	uuid_copy(ukey.uuid, pool_id);
	rc = pool_lookup(&ukey, &pool);
	if (rc)
		return rc;

	D_ASSERT(pool != NULL);
	rc = vos_space_query(pool, vps, false);
	vos_pool_decref(pool);
	return rc;
}

int
vos_pool_space_sys_set(daos_handle_t poh, daos_size_t *space_sys)
{
	struct vos_pool	*pool = vos_hdl2pool(poh);

	if (pool == NULL)
		return -DER_NO_HDL;
	if (space_sys == NULL)
		return -DER_INVAL;

	return vos_space_sys_set(pool, space_sys);
}

int
vos_pool_ctl(daos_handle_t poh, enum vos_pool_opc opc)
{
	struct vos_pool		*pool;

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	switch (opc) {
	default:
		return -DER_NOSYS;
	case VOS_PO_CTL_RESET_GC:
		memset(&pool->vp_gc_stat, 0, sizeof(pool->vp_gc_stat));
		break;
	case VOS_PO_CTL_VEA_PLUG:
		if (pool->vp_vea_info != NULL)
			vea_flush(pool->vp_vea_info, true);
		break;
	case VOS_PO_CTL_VEA_UNPLUG:
		if (pool->vp_vea_info != NULL)
			vea_flush(pool->vp_vea_info, false);
		break;
	}

	return 0;
}
