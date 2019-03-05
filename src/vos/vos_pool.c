/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * Implementation for pool specific functions in VOS
 *
 * vos/vos_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos_srv/vos.h>
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

pthread_mutex_t vos_pmemobj_lock = PTHREAD_MUTEX_INITIALIZER;

static int
umem_get_type(void)
{
	/* NB: BYPASS_PM and BYPASS_PM_SNAP can't coexist */
	if (daos_io_bypass & IOBP_PM) {
		D_PRINT("Running in DRAM mode, all data are volatile.\n");
		return UMEM_CLASS_VMEM;

	} else if (daos_io_bypass & IOBP_PM_SNAP) {
		D_PRINT("Ignore PMDK snapshot, data can be lost on failure.\n");
		return UMEM_CLASS_PMEM_NO_SNAP;

	} else {
		return UMEM_CLASS_PMEM;
	}
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

	if (pool->vp_io_ctxt != NULL) {
		rc = bio_ioctxt_close(pool->vp_io_ctxt);
		if (rc)
			D_ERROR("Closing VOS I/O context:%p pool:"DF_UUID"\n",
				pool->vp_io_ctxt, DP_UUID(pool->vp_id));
		else
			D_DEBUG(DB_MGMT, "Closed VOS I/O context:%p pool:"
				""DF_UUID"\n",
				pool->vp_io_ctxt, DP_UUID(pool->vp_id));
	}

	if (pool->vp_vea_info != NULL)
		vea_unload(pool->vp_vea_info);

	if (!daos_handle_is_inval(pool->vp_cont_th))
		dbtree_close(pool->vp_cont_th);

	if (pool->vp_uma.uma_pool)
		vos_pmemobj_close(pool->vp_uma.uma_pool);

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
	struct umem_attr	 uma;

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		return -DER_NOMEM;

	d_uhash_ulink_init(&pool->vp_hlink, &pool_uuid_hops);
	uuid_copy(pool->vp_id, uuid);

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
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
		D_ERROR("uuid hash table insert failed: %d\n", rc);
		D_GOTO(failed, rc);
	}
	*poh = vos_pool2hdl(pool);
	return 0;
failed:
	return rc;
}

static void
pool_unlink(struct vos_pool *pool)
{
	d_uhash_link_delete(vos_pool_hhash_get(), &pool->vp_hlink);
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
	rc = bio_ioctxt_open(&ioctxt, xs_ctxt, umem, blob_hdr->bbh_pool);
	if (rc) {
		D_ERROR("Failed to create an ioctxt for writing blob header\n");
		return rc;
	}

	/* Write the blob header info to blob offset 0 */
	rc = bio_write_blob_hdr(ioctxt, blob_hdr);
	if (rc)
		D_ERROR("Failed to write header for blob:"DF_U64"\n",
			blob_hdr->bbh_blob_id);

	rc = bio_ioctxt_close(ioctxt);
	if (rc)
		D_ERROR("Failed to free ioctxt\n");

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

/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t scm_sz,
		daos_size_t nvme_sz)
{
	struct vea_space_df	*vea_md = NULL;
	PMEMobjpool		*ph;
	struct umem_attr	 uma;
	struct umem_instance	 umem;
	struct vos_pool_df	*pool_df;
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct bio_blob_hdr	 blob_hdr;
	int			 rc = 0, enabled = 1;

	if (!path || uuid_is_null(uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Pool Path: %s, size: "DF_U64":"DF_U64", "
		"UUID: "DF_UUID"\n", path, scm_sz, nvme_sz, DP_UUID(uuid));

	/* Path must be a file with a certain size when size argument is 0 */
	if (!scm_sz && access(path, F_OK) == -1) {
		D_ERROR("File not accessible (%d) when size is 0\n", errno);
		return daos_errno2der(errno);
	}

	ph = vos_pmemobj_create(path, POBJ_LAYOUT_NAME(vos_pool_layout), scm_sz,
				0666);
	if (!ph) {
		D_ERROR("Failed to create pool %s, size="DF_U64", errno=%d\n",
			path, scm_sz, errno);
		return daos_errno2der(errno);
	}

	rc = pmemobj_ctl_set(ph, "stats.enabled", &enabled);
	if (rc) {
		D_ERROR("Enable SCM usage statistics failed. rc:%d\n", rc);
		rc = umem_tx_errno(rc);
		goto close;
	}

	/* If the file is fallocated seperately we need the fallocated size
	 * for setting in the root object.
	 */
	if (!scm_sz) {
		struct stat lstat;

		stat(path, &lstat);
		scm_sz = lstat.st_size;
	}

	pool_df = vos_pool_pop2df(ph);
	TX_BEGIN(ph) {
		pmemobj_tx_add_range_direct(pool_df, sizeof(*pool_df));
		memset(pool_df, 0, sizeof(*pool_df));

		memset(&uma, 0, sizeof(uma));
		uma.uma_id = umem_get_type();
		uma.uma_pool = ph;

		rc = vos_cont_tab_create(&uma, &pool_df->pd_ctab_df);
		if (rc != 0)
			pmemobj_tx_abort(EFAULT);

		uuid_copy(pool_df->pd_id, uuid);
		pool_df->pd_scm_sz = scm_sz;
		pool_df->pd_nvme_sz = nvme_sz;
		vea_md = &pool_df->pd_vea_df;

	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Initialize pool root error: %d\n", rc);
		/**
		 * The transaction can in reality be aborted
		 * only when there is no memory, either due
		 * to loss of power or no more memory in pool
		 */
	} TX_END

	if (rc != 0)
		goto close;

	/* SCM only pool or NVMe device isn't configured */
	if (nvme_sz == 0 || xs_ctxt == NULL)
		goto close;

	rc = umem_class_init(&uma, &umem);
	if (rc != 0)
		goto close;

	/* Create SPDK blob on NVMe device */
	D_DEBUG(DB_MGMT, "Creating blob for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(uuid));
	rc = bio_blob_create(uuid, xs_ctxt, nvme_sz);
	if (rc != 0) {
		D_ERROR("Error creating blob for xs:%p pool:"DF_UUID" rc:%d\n",
			xs_ctxt, DP_UUID(uuid), rc);
		goto close;
	}

	/* Format SPDK blob header */
	blob_hdr.bbh_blk_sz = VOS_BLK_SZ;
	blob_hdr.bbh_hdr_sz = VOS_BLOB_HDR_BLKS;
	uuid_copy(blob_hdr.bbh_pool, uuid);

	/* Format SPDK blob*/
	D_ASSERT(vea_md != NULL);
	rc = vea_format(&umem, vos_txd_get(), vea_md, VOS_BLK_SZ,
			VOS_BLOB_HDR_BLKS, nvme_sz, vos_blob_format_cb,
			&blob_hdr, false);
	if (rc) {
		D_ERROR("Format blob error for xs:%p pool:"DF_UUID" rc:%d\n",
			xs_ctxt, DP_UUID(uuid), rc);
		/* Destroy the SPDK blob on error */
		rc = bio_blob_delete(uuid, xs_ctxt);
	}
close:
	/* Close this local handle, opened using pool_open */
	vos_pmemobj_close(ph);
	return rc;
}

/**
 * Destroy SPDK blob
 */
int
vos_blob_destroy(void *uuid)
{
	uuid_t			*pool_uuid = uuid;
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	int			 rc;

	/* NVMe device isn't configured */
	if (xs_ctxt == NULL)
		return 0;

	D_DEBUG(DB_MGMT, "Deleting blob for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(*pool_uuid));
	rc = bio_blob_delete(*pool_uuid, xs_ctxt);

	return rc;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP) and revoke all its handles
 */
int
vos_pool_destroy(const char *path, uuid_t uuid)
{

	struct vos_pool		*pool;
	struct d_uuid		 ukey;
	int			 rc;

	uuid_copy(ukey.uuid, uuid);
	D_DEBUG(DB_MGMT, "Destroy path: %s UUID: "DF_UUID"\n",
		path, DP_UUID(uuid));

	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D_ERROR("Open reference exists, cannot destroy pool\n");
		vos_pool_decref(pool);
		D_GOTO(exit, rc = -DER_BUSY);
	}

	D_DEBUG(DB_MGMT, "No open handles. OK to destroy\n");

	rc = vos_blob_destroy(uuid);
	if (rc)
		D_ERROR("Destroy blob path: %s UUID: "DF_UUID"\n",
			path, DP_UUID(uuid));

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
		if (rc)
			D_ERROR("Failure deleting file from PMEM: %s\n",
				strerror(errno));
	}

exit:
	return rc;
}

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh)
{
	struct bio_xs_context	*xs_ctxt;
	struct vos_pool_df	*pool_df;
	struct vos_pool		*pool;
	struct umem_attr	*uma;
	struct d_uuid		 ukey;
	int			 rc, enabled = 1;

	if (path == NULL || poh == NULL) {
		D_ERROR("Invalid parameters.\n");
		return -DER_INVAL;
	}

	uuid_copy(ukey.uuid, uuid);
	D_DEBUG(DB_MGMT, "Pool Path: %s, UUID: "DF_UUID"\n", path,
		DP_UUID(uuid));

	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D_DEBUG(DB_MGMT, "Found already opened(%d) pool : %p\n",
			pool->vp_opened, pool);
		pool->vp_opened++;
		*poh = vos_pool2hdl(pool);
		return 0;
	}

	/* Create a new handle during open */
	rc = pool_alloc(uuid, &pool); /* returned with refcount=1 */
	if (rc != 0) {
		D_ERROR("Error allocating pool handle\n");
		return rc;
	}

	uma = &pool->vp_uma;
	uma->uma_id = umem_get_type();
	uma->uma_pool = vos_pmemobj_open(path,
				   POBJ_LAYOUT_NAME(vos_pool_layout));
	if (uma->uma_pool == NULL) {
		D_ERROR("Error in opening the pool: %s\n", pmemobj_errormsg());
		D_GOTO(failed, rc = -DER_NO_HDL);
	}

	/* initialize a umem instance for later btree operations */
	rc = umem_class_init(uma, &pool->vp_umm);
	if (rc != 0) {
		D_ERROR("Failed to instantiate umem: %d\n", rc);
		D_GOTO(failed, rc);
	}

	rc = pmemobj_ctl_set(uma->uma_pool, "stats.enabled", &enabled);
	if (rc) {
		D_ERROR("Enable SCM usage statistics failed. rc:%d\n",
			umem_tx_errno(rc));
		D_GOTO(failed, rc);
	}

	pool_df = vos_pool_ptr2df(pool);
	if (uuid_compare(uuid, pool_df->pd_id)) {
		D_ERROR("Mismatch uuid, user="DF_UUID", pool="DF_UUID"\n",
			DP_UUID(uuid), DP_UUID(pool_df->pd_id));
		D_GOTO(failed, rc = -DER_IO);
	}

	/* Cache container table btree hdl */
	rc = dbtree_open_inplace(&pool_df->pd_ctab_df.ctb_btree,
				 &pool->vp_uma, &pool->vp_cont_th);
	if (rc) {
		D_ERROR("Container Tree open failed\n");
		D_GOTO(failed, rc);
	}

	xs_ctxt = pool_df->pd_nvme_sz == 0 ? NULL : vos_xsctxt_get();

	D_DEBUG(DB_MGMT, "Opening VOS I/O context for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(uuid));
	rc = bio_ioctxt_open(&pool->vp_io_ctxt, xs_ctxt, &pool->vp_umm, uuid);
	if (rc) {
		D_ERROR("Failed to open VOS I/O context for xs:%p "
			"pool:"DF_UUID" rc=%d\n", xs_ctxt, DP_UUID(uuid), rc);
		goto failed;
	}

	if (xs_ctxt != NULL) {
		struct vea_unmap_context unmap_ctxt;

		/* set unmap callback fp */
		unmap_ctxt.vnc_unmap = vos_blob_unmap_cb;
		unmap_ctxt.vnc_data = pool->vp_io_ctxt;
		rc = vea_load(&pool->vp_umm, vos_txd_get(), &pool_df->pd_vea_df,
			      &unmap_ctxt, &pool->vp_vea_info);
		if (rc) {
			D_ERROR("Failed to load block space info: %d\n", rc);
			goto failed;
		}
	}

	/* Insert the opened pool to the uuid hash table */
	rc = pool_link(pool, &ukey, poh);
	if (rc) {
		D_ERROR("Error inserting into vos DRAM hash\n");
		D_GOTO(failed, rc);
	}

	pool->vp_opened = 1;
	D_DEBUG(DB_MGMT, "Opened pool %p\n", pool);
failed:
	vos_pool_decref(pool); /* -1 for myself */
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
	if (pool->vp_opened == 0)
		pool_unlink(pool);

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
	daos_size_t		 scm_used;
	struct vea_attr		 attr;
	struct vea_stat		 stat;
	int			 rc;

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	pool_df = vos_pool_ptr2df(pool);

	D_ASSERT(pinfo != NULL);
	pinfo->pif_scm_sz = pool_df->pd_scm_sz;
	pinfo->pif_nvme_sz = pool_df->pd_nvme_sz;
	pinfo->pif_cont_nr = pool_df->pd_cont_nr;

	/* query SCM free space */
	rc = pmemobj_ctl_get(pool->vp_umm.umm_pool,
			     "stats.heap.curr_allocated", &scm_used);
	if (rc) {
		D_ERROR("Failed to get SCM usage. rc:%d\n", rc);
		return umem_tx_errno(rc);
	}

	/*
	 * FIXME: pmemobj_ctl_get() sometimes return an insane large
	 * value, I suspect it's a PMDK defect. Let's ignore the error
	 * and return success for this moment.
	 */
	if (pinfo->pif_scm_sz < scm_used) {
		D_CRIT("scm_sz:"DF_U64" < scm_used:"DF_U64"\n",
		       pinfo->pif_scm_sz, scm_used);
		pinfo->pif_scm_free = 0;
	} else {
		pinfo->pif_scm_free = pinfo->pif_scm_sz - scm_used;
	}

	/* NVMe isn't configured for this VOS */
	if (pool->vp_vea_info == NULL) {
		pinfo->pif_nvme_free = 0;
		return 0;
	}

	/* query NVMe free space */
	rc = vea_query(pool->vp_vea_info, &attr, &stat);
	if (rc) {
		D_ERROR("Failed to get NVMe usage. rc:%d\n", rc);
		return rc;
	}
	D_ASSERT(attr.va_blk_sz != 0);
	pinfo->pif_nvme_free = attr.va_blk_sz * stat.vs_free_persistent;
	D_ASSERTF(pinfo->pif_nvme_free <= pinfo->pif_nvme_sz,
		  "nvme_free:"DF_U64", nvme_sz:"DF_U64", blk_sz:%u\n",
		  pinfo->pif_nvme_free, pinfo->pif_nvme_sz, attr.va_blk_sz);

	return 0;
}
