/**
 * (C) Copyright 2016-2023 Intel Corporation.
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
#include <sys/time.h>
#include <sys/resource.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <daos_pool.h>
#include <daos_srv/policy.h>

static void
vos_iod2bsgl(struct umem_store *store, struct umem_store_iod *iod, struct bio_sglist *bsgl)
{
	struct bio_iov	*biov;
	bio_addr_t	 addr = { 0 };
	uint32_t	 off_bytes;
	int		 i;

	off_bytes = store->stor_hdr_blks * store->stor_blk_size;
	for (i = 0; i < iod->io_nr; i++) {
		biov = &bsgl->bs_iovs[i];

		bio_addr_set(&addr, DAOS_MEDIA_NVME, iod->io_regions[i].sr_addr + off_bytes);
		bio_iov_set(biov, addr, iod->io_regions[i].sr_size);
	}
	bsgl->bs_nr_out = bsgl->bs_nr;
}

static int
vos_meta_rwv(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl, bool update)
{
	struct bio_sglist	bsgl;
	struct bio_iov		local_biov;
	int			rc;

	D_ASSERT(store && store->stor_priv != NULL);
	D_ASSERT(iod->io_nr > 0);
	D_ASSERT(sgl->sg_nr > 0);

	if (iod->io_nr == 1) {
		bsgl.bs_iovs = &local_biov;
		bsgl.bs_nr = 1;
	} else {
		rc = bio_sgl_init(&bsgl, iod->io_nr);
		if (rc)
			return rc;
	}
	vos_iod2bsgl(store, iod, &bsgl);

	if (update)
		rc = bio_writev(bio_mc2ioc(store->stor_priv, SMD_DEV_TYPE_META), &bsgl, sgl);
	else
		rc = bio_readv(bio_mc2ioc(store->stor_priv, SMD_DEV_TYPE_META), &bsgl, sgl);

	if (iod->io_nr > 1)
		bio_sgl_fini(&bsgl);

	return rc;
}

static inline int
vos_meta_readv(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	return vos_meta_rwv(store, iod, sgl, false);
}

static inline int
vos_meta_writev(struct umem_store *store, struct umem_store_iod *iod, d_sg_list_t *sgl)
{
	return vos_meta_rwv(store, iod, sgl, true);
}

static int
vos_meta_flush_prep(struct umem_store *store, struct umem_store_iod *iod, daos_handle_t *fh)
{
	struct bio_desc		*biod;
	struct bio_sglist	*bsgl;
	int			 rc;

	D_ASSERT(store && store->stor_priv != NULL);
	D_ASSERT(iod->io_nr > 0);
	D_ASSERT(fh != NULL);

	biod = bio_iod_alloc(bio_mc2ioc(store->stor_priv, SMD_DEV_TYPE_META),
			     NULL, 1, BIO_IOD_TYPE_UPDATE);
	if (biod == NULL)
		return -DER_NOMEM;

	bsgl = bio_iod_sgl(biod, 0);
	rc = bio_sgl_init(bsgl, iod->io_nr);
	if (rc)
		goto free;

	vos_iod2bsgl(store, iod, bsgl);

	rc = bio_iod_try_prep(biod, BIO_CHK_TYPE_LOCAL, NULL, 0);
	if (rc) {
		D_CDEBUG(rc == -DER_AGAIN, DB_TRACE, DLOG_ERR,
			 "Failed to prepare DMA buffer. "DF_RC"\n", DP_RC(rc));
		goto free;
	}

	fh->cookie = (uint64_t)biod;
	return 0;
free:
	bio_iod_free(biod);
	return rc;
}

static int
vos_meta_flush_copy(daos_handle_t fh, d_sg_list_t *sgl)
{
	struct bio_desc	*biod = (struct bio_desc *)fh.cookie;

	D_ASSERT(sgl->sg_nr > 0);
	return bio_iod_copy(biod, sgl, 1);
}

static int
vos_meta_flush_post(daos_handle_t fh, int err)
{
	struct bio_desc	*biod = (struct bio_desc *)fh.cookie;

	return bio_iod_post(biod, err);
}

static inline int
vos_wal_reserve(struct umem_store *store, uint64_t *tx_id)
{
	D_ASSERT(store && store->stor_priv != NULL);
	return bio_wal_reserve(store->stor_priv, tx_id);
}

static void
vos_wal_on_commit(struct umem_store *store, uint64_t id)
{
	struct chkpt_ctx *ctx = store->chkpt_ctx;

	if (ctx == NULL)
		return; /** Checkpointing not active */

	ctx->cc_commit_id = id;

	if (store->stor_ops->so_wal_id_cmp(store, id, ctx->cc_wait_id) >= 0) {
		ctx->cc_wake_fn(ctx);
		store->chkpt_ctx = NULL;
	}
}

static inline int
vos_wal_commit(struct umem_store *store, struct umem_wal_tx *wal_tx, void *data_iod)
{
	int rc;

	D_ASSERT(store && store->stor_priv != NULL);
	rc = bio_wal_commit(store->stor_priv, wal_tx, data_iod);

	vos_wal_on_commit(store, wal_tx->utx_id);

	return rc;
}

static inline int
vos_wal_replay(struct umem_store *store,
	       int (*replay_cb)(uint64_t tx_id, struct umem_action *act, void *arg),
	       void *arg)
{
	D_ASSERT(store && store->stor_priv != NULL);
	return bio_wal_replay(store->stor_priv, replay_cb, arg);
}

static inline int
vos_wal_id_cmp(struct umem_store *store, uint64_t id1, uint64_t id2)
{
	D_ASSERT(store && store->stor_priv != NULL);
	return bio_wal_id_cmp(store->stor_priv, id1, id2);
}

static void
chkpt_wait(struct umem_store *store, uint64_t chkpt_tx, uint64_t *committed_tx, void *arg)
{
	struct chkpt_ctx *ctx = arg;

	if (store->stor_ops->so_wal_id_cmp(store, chkpt_tx, ctx->cc_commit_id) <= 0) {
		/** Sometimes we may need to yield here to make progress such as when we need
		 *  more DMA buffers to prepare entries.
		 */
		if (!ctx->cc_is_idle_fn())
			ctx->cc_yield_fn(ctx);
		goto done;
	}

	ctx->cc_wait_id = chkpt_tx;
	store->chkpt_ctx = ctx;
	ctx->cc_wait_fn(ctx);
done:
	*committed_tx = ctx->cc_commit_id;
}

struct umem_store_ops vos_store_ops = {
    .so_read       = vos_meta_readv,
    .so_write      = vos_meta_writev,
    .so_flush_prep = vos_meta_flush_prep,
    .so_flush_copy = vos_meta_flush_copy,
    .so_flush_post = vos_meta_flush_post,
    .so_wal_reserv = vos_wal_reserve,
    .so_wal_submit = vos_wal_commit,
    .so_wal_replay = vos_wal_replay,
    .so_wal_id_cmp = vos_wal_id_cmp,
};

bool
vos_pool_needs_checkpoint(daos_handle_t poh)
{
	struct vos_pool *pool;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	/** TODO: Revisit. */
	return bio_nvme_configured(SMD_DEV_TYPE_META);
}

int
vos_pool_checkpoint(struct chkpt_ctx *ctx)
{
	struct vos_pool      *pool;
	uint64_t              tx_id;
	struct umem_instance *umm;
	struct umem_store    *store;
	struct bio_wal_info   wal_info;
	int                   rc;

	pool = vos_hdl2pool(ctx->cc_vos_pool_hdl);
	D_ASSERT(pool != NULL);

	umm   = vos_pool2umm(pool);
	store = &umm->umm_pool->up_store;

	bio_wal_query(store->stor_priv, &wal_info);
	tx_id = wal_info.wi_commit_id;
	if (tx_id == wal_info.wi_ckp_id) {
		D_DEBUG(DB_TRACE, "No checkpoint needed for "DF_UUID"\n", DP_UUID(pool->vp_id));
		return 0;
	}

	D_INFO("Checkpoint started pool=" DF_UUID ", committed_id=" DF_X64 "\n",
	       DP_UUID(pool->vp_id), tx_id);
	ctx->cc_commit_id = tx_id;

	rc = umem_cache_checkpoint(store, chkpt_wait, ctx, &tx_id);

	if (rc == 0)
		rc = bio_wal_checkpoint(store->stor_priv, tx_id);

	D_INFO("Checkpoint finished pool=" DF_UUID ", committed_id=" DF_X64 ", rc=" DF_RC "\n",
	       DP_UUID(pool->vp_id), tx_id, DP_RC(rc));

	return rc;
}

int
vos_pool_settings_init(bool md_on_ssd)
{
	return umempobj_settings_init(md_on_ssd);
}

static inline enum bio_mc_flags
vos2mc_flags(unsigned int vos_flags)
{
	enum bio_mc_flags mc_flags = 0;

	if (vos_flags & VOS_POF_RDB)
		mc_flags |= BIO_MC_FL_RDB;

	return mc_flags;
}

static int
vos_pmemobj_create(const char *path, uuid_t pool_id, const char *layout,
		   size_t scm_sz, size_t nvme_sz, size_t wal_sz, unsigned int flags,
		   struct umem_pool **ph)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct umem_store	 store = { 0 };
	struct bio_meta_context	*mc;
	struct umem_pool	*pop = NULL;
	enum bio_mc_flags	 mc_flags = vos2mc_flags(flags);
	size_t			 meta_sz = scm_sz;
	int			 rc, ret;

	*ph = NULL;
	/* No NVMe is configured or current xstream doesn't have NVMe context */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX) || xs_ctxt == NULL)
		goto umem_create;

	if (!scm_sz) {
		struct stat lstat;

		rc = stat(path, &lstat);
		if (rc != 0)
			return daos_errno2der(errno);
		meta_sz = lstat.st_size;
	}

	D_DEBUG(DB_MGMT, "Create BIO meta context for xs:%p pool:"DF_UUID" "
		"meta_sz: %zu, nvme_sz: %zu wal_sz:%zu\n",
		xs_ctxt, DP_UUID(pool_id), meta_sz, nvme_sz, wal_sz);

	rc = bio_mc_create(xs_ctxt, pool_id, meta_sz, wal_sz, nvme_sz, mc_flags);
	if (rc != 0) {
		D_ERROR("Failed to create BIO meta context for xs:%p pool:"DF_UUID". "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}

	rc = bio_mc_open(xs_ctxt, pool_id, mc_flags, &mc);
	if (rc != 0) {
		D_ERROR("Failed to open BIO meta context for xs:%p pool:"DF_UUID". "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));

		ret = bio_mc_destroy(xs_ctxt, pool_id, mc_flags);
		if (ret)
			D_ERROR("Failed to destroy BIO meta context. "DF_RC"\n", DP_RC(ret));

		return rc;
	}

	bio_meta_get_attr(mc, &store.stor_size, &store.stor_blk_size, &store.stor_hdr_blks);
	store.stor_priv = mc;
	store.stor_ops = &vos_store_ops;

umem_create:
	pop = umempobj_create(path, layout, UMEMPOBJ_ENABLE_STATS, scm_sz, 0600, &store);
	if (pop != NULL) {
		*ph = pop;
		return 0;
	}
	rc = daos_errno2der(errno);
	D_ASSERT(rc != 0);

	if (store.stor_priv != NULL) {
		ret = bio_mc_close(store.stor_priv);
		if (ret) {
			D_ERROR("Failed to close BIO meta context. "DF_RC"\n", DP_RC(ret));
			return rc;
		}
		ret = bio_mc_destroy(xs_ctxt, pool_id, mc_flags);
		if (ret)
			D_ERROR("Failed to destroy BIO meta context. "DF_RC"\n", DP_RC(ret));
	}

	return rc;
}

static int
vos_pmemobj_open(const char *path, uuid_t pool_id, const char *layout, unsigned int flags,
		 struct umem_pool **ph)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct umem_store	 store = { 0 };
	struct bio_meta_context	*mc;
	struct umem_pool	*pop;
	enum bio_mc_flags	 mc_flags = vos2mc_flags(flags);
	int			 rc, ret;

	*ph = NULL;
	/* No NVMe is configured or current xstream doesn't have NVMe context */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX) || xs_ctxt == NULL)
		goto umem_open;

	D_DEBUG(DB_MGMT, "Open BIO meta context for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(pool_id));

	rc = bio_mc_open(xs_ctxt, pool_id, mc_flags, &mc);
	if (rc) {
		D_ERROR("Failed to open BIO meta context for xs:%p pool:"DF_UUID", "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}

	bio_meta_get_attr(mc, &store.stor_size, &store.stor_blk_size, &store.stor_hdr_blks);
	store.stor_priv = mc;
	store.stor_ops = &vos_store_ops;

umem_open:
	pop = umempobj_open(path, layout, UMEMPOBJ_ENABLE_STATS, &store);
	if (pop != NULL) {
		*ph = pop;
		return 0;
	}
	rc = daos_errno2der(errno);
	D_ASSERT(rc != 0);

	if (store.stor_priv != NULL) {
		ret = bio_mc_close(store.stor_priv);
		if (ret)
			D_ERROR("Failed to close BIO meta context. "DF_RC"\n", DP_RC(ret));
	}

	return rc;
}

static inline void
vos_pmemobj_close(struct umem_pool *pop)
{
	struct umem_store	store;
	int			rc;

	store = pop->up_store;

	umempobj_close(pop);

	if (store.stor_priv != NULL) {
		rc = bio_mc_close(store.stor_priv);
		if (rc)
			D_ERROR("Failed to close BIO meta context. "DF_RC"\n", DP_RC(rc));
	}
}

static inline struct vos_pool_df *
vos_pool_pop2df(struct umem_pool *pop)
{
	return (struct vos_pool_df *)
		umempobj_get_rootptr(pop, sizeof(struct vos_pool_df));
}

static struct vos_pool *
pool_hlink2ptr(struct d_ulink *hlink)
{
	D_ASSERT(hlink != NULL);
	return container_of(hlink, struct vos_pool, vp_hlink);
}

static void
vos_delete_blob(uuid_t pool_uuid, unsigned int flags)
{
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	enum bio_mc_flags	 mc_flags = vos2mc_flags(flags);
	int			 rc;

	/* NVMe device isn't configured */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX) || xs_ctxt == NULL)
		return;

	D_DEBUG(DB_MGMT, "Deleting blob for xs:%p pool:"DF_UUID"\n",
		xs_ctxt, DP_UUID(pool_uuid));

	rc = bio_mc_destroy(xs_ctxt, pool_uuid, mc_flags);
	if (rc)
		D_ERROR("Destroying meta context blob for xs:%p pool="DF_UUID" failed: "DF_RC"\n",
			xs_ctxt, DP_UUID(pool_uuid), DP_RC(rc));

	return;
}

static void
pool_hop_free(struct d_ulink *hlink)
{
	struct vos_pool		*pool = pool_hlink2ptr(hlink);
	int			 rc;

	D_ASSERT(pool->vp_opened == 0);
	D_ASSERT(!gc_have_pool(pool));

	if (pool->vp_vea_info != NULL)
		vea_unload(pool->vp_vea_info);

	if (daos_handle_is_valid(pool->vp_cont_th))
		dbtree_close(pool->vp_cont_th);

	if (pool->vp_size != 0) {
		rc = munlock((void *)pool->vp_umm.umm_base, pool->vp_size);
		if (rc != 0)
			D_WARN("Failed to unlock pool memory at "DF_X64": errno=%d (%s)\n",
			       pool->vp_umm.umm_base, errno, strerror(errno));
		else
			D_DEBUG(DB_MGMT, "Unlocked VOS pool memory: "DF_U64" bytes at "DF_X64"\n",
				pool->vp_size, pool->vp_umm.umm_base);
	}

	if (pool->vp_uma.uma_pool)
		vos_pmemobj_close(pool->vp_uma.uma_pool);

	vos_dedup_fini(pool);

	if (pool->vp_dummy_ioctxt)
		bio_ioctxt_close(pool->vp_dummy_ioctxt);

	if (pool->vp_dying)
		vos_delete_blob(pool->vp_id, 0);

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
vos_blob_format_cb(void *cb_data)
{
	struct bio_blob_hdr	*blob_hdr = cb_data;
	struct bio_xs_context	*xs_ctxt = vos_xsctxt_get();
	struct bio_io_context	*ioctxt;
	int			 rc;

	/* Create a bio_io_context to get the blob */
	rc = bio_ioctxt_open(&ioctxt, xs_ctxt, blob_hdr->bbh_pool, false);
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

	rc = bio_ioctxt_close(ioctxt);
	if (rc)
		D_ERROR("Failed to free I/O context: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Unmap (TRIM) the extent being freed
 */
static int
vos_blob_unmap_cb(d_sg_list_t *unmap_sgl, uint32_t blk_sz, void *data)
{
	struct bio_io_context	*ioctxt = data;
	int			 rc;

	/* unmap unused pages for NVMe media to perform more efficiently */
	rc = bio_blob_unmap_sgl(ioctxt, unmap_sgl, blk_sz);
	if (rc)
		D_ERROR("Blob unmap SGL failed. "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int pool_open(void *ph, struct vos_pool_df *pool_df,
		     unsigned int flags, void *metrics, daos_handle_t *poh);

int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t scm_sz,
		daos_size_t nvme_sz, unsigned int flags, daos_handle_t *poh)
{
	struct umem_pool	*ph;
	struct umem_attr	 uma = {0};
	struct umem_instance	 umem = {0};
	struct vos_pool_df	*pool_df;
	struct bio_blob_hdr	 blob_hdr;
	daos_handle_t		 hdl;
	struct d_uuid		 ukey;
	struct vos_pool		*pool = NULL;
	int			 rc = 0;

	if (!path || uuid_is_null(uuid) || daos_file_is_dax(path))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Pool Path: %s, size: "DF_U64":"DF_U64", "
		"UUID: "DF_UUID"\n", path, scm_sz, nvme_sz, DP_UUID(uuid));

	if (flags & VOS_POF_SMALL)
		flags |= VOS_POF_EXCL;

	uuid_copy(ukey.uuid, uuid);
	rc = pool_lookup(&ukey, &pool);
	if (rc == 0) {
		D_ASSERT(pool != NULL);
		D_ERROR("Found already opened(%d) pool:%p dying(%d)\n",
			pool->vp_opened, pool, pool->vp_dying);
		vos_pool_decref(pool);
		return -DER_EXIST;
	}

	/* Path must be a file with a certain size when size argument is 0 */
	if (!scm_sz && access(path, F_OK) == -1) {
		D_ERROR("File not accessible (%d) when size is 0\n", errno);
		return daos_errno2der(errno);
	}

	rc = vos_pmemobj_create(path, uuid, VOS_POOL_LAYOUT, scm_sz, nvme_sz, 0, flags, &ph);
	if (rc) {
		D_ERROR("Failed to create pool %s, scm_sz="DF_U64", nvme_sz="DF_U64". "DF_RC"\n",
			path, scm_sz, nvme_sz, DP_RC(rc));
		return rc;
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

	/* SCM only pool or data blob isn't configured */
	if (nvme_sz == 0 || !bio_nvme_configured(SMD_DEV_TYPE_DATA))
		goto open;

	/* Format SPDK blob header */
	blob_hdr.bbh_blk_sz = VOS_BLK_SZ;
	blob_hdr.bbh_hdr_sz = VOS_BLOB_HDR_BLKS;
	uuid_copy(blob_hdr.bbh_pool, uuid);

	/* Format SPDK blob*/
	rc = vea_format(&umem, vos_txd_get(), &pool_df->pd_vea_df, VOS_BLK_SZ,
			VOS_BLOB_HDR_BLKS, nvme_sz, vos_blob_format_cb,
			&blob_hdr, false);
	if (rc) {
		D_ERROR("Format blob error for pool:"DF_UUID". "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto close;
	}

open:
	/* If the caller does not want a VOS pool handle, we're done. */
	if (poh == NULL)
		goto close;

	/* Create a VOS pool handle using ph. */
	rc = pool_open(ph, pool_df, flags, NULL, poh);
	ph = NULL;

close:
	/* Close this local handle, if it hasn't been consumed nor already
	 * been closed by pool_open upon error.
	 */
	if (ph != NULL)
		vos_pmemobj_close(ph);
	return rc;
}

/**
 * kill the pool before destroy:
 * - detach from GC, delete SPDK blob
 */
int
vos_pool_kill(uuid_t uuid, unsigned int flags)
{
	struct d_uuid	ukey;
	int		rc;

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
		if (gc_have_pool(pool)) {
			/* still pinned by GC, un-pin it because there is no
			 * need to run GC for this pool anymore.
			 */
			gc_del_pool(pool);
			vos_pool_decref(pool); /* -1 for lookup */
			continue;	/* try again */
		}
		pool->vp_dying = 1;
		vos_pool_decref(pool); /* -1 for lookup */

		D_WARN(DF_UUID": Open reference exists, pool destroy is deferred\n",
		       DP_UUID(uuid));
		VOS_NOTIFY_RAS_EVENTF(RAS_POOL_DEFER_DESTROY, RAS_TYPE_INFO, RAS_SEV_WARNING,
				      NULL, NULL, NULL, NULL, &ukey.uuid, NULL, NULL, NULL, NULL,
				      "pool:"DF_UUID" destroy is deferred", DP_UUID(uuid));
		/* Blob destroy will be deferred to last vos_pool ref drop */
		return -DER_BUSY;
	}
	D_DEBUG(DB_MGMT, "No open handles, OK to delete\n");

	vos_delete_blob(uuid, flags);
	return 0;
}

/**
 * Destroy a Versioning Object Storage Pool (VOSP) and revoke all its handles
 */
int
vos_pool_destroy_ex(const char *path, uuid_t uuid, unsigned int flags)
{
	int	rc;

	D_DEBUG(DB_MGMT, "delete path: %s UUID: "DF_UUID"\n",
		path, DP_UUID(uuid));

	rc = vos_pool_kill(uuid, flags);
	if (rc)
		return rc;

	if (daos_file_is_dax(path))
		return -DER_INVAL;

	/**
	 * NB: no need to explicitly destroy container index table because
	 * pool file removal will do this for free.
	 */
	rc = remove(path);
	if (rc) {
		if (errno == ENOENT)
			D_GOTO(exit, rc = 0);
		D_ERROR("Failure deleting file from PMEM: %s\n",
			strerror(errno));
	}
exit:
	return rc;
}

int
vos_pool_destroy(const char *path, uuid_t uuid)
{
	return vos_pool_destroy_ex(path, uuid, 0);
}

enum {
	/** Memory locking flag not initialized */
	LM_FLAG_UNINIT,
	/** Memory locking disabled */
	LM_FLAG_DISABLED,
	/** Memory locking enabled */
	LM_FLAG_ENABLED
};

static void
lock_pool_memory(struct vos_pool *pool)
{
	static		 int lock_mem = LM_FLAG_UNINIT;
	struct rlimit	 rlim;
	int		 rc;

	if (lock_mem == LM_FLAG_UNINIT) {
		rc = getrlimit(RLIMIT_MEMLOCK, &rlim);
		if (rc != 0) {
			D_WARN("getrlimit() failed; errno=%d (%s)\n", errno, strerror(errno));
			lock_mem = LM_FLAG_DISABLED;
			return;
		}

		if (rlim.rlim_cur != RLIM_INFINITY || rlim.rlim_max != RLIM_INFINITY) {
			D_WARN("Infinite rlimit not detected, not locking VOS pool memory\n");
			lock_mem = LM_FLAG_DISABLED;
			return;
		}

		lock_mem = LM_FLAG_ENABLED;
	}

	if (lock_mem == LM_FLAG_DISABLED)
		return;

	rc = mlock((void *)pool->vp_umm.umm_base, pool->vp_pool_df->pd_scm_sz);
	if (rc != 0) {
		D_WARN("Could not lock memory for VOS pool "DF_U64" bytes at "DF_X64
		       "; errno=%d (%s)\n", pool->vp_pool_df->pd_scm_sz, pool->vp_umm.umm_base,
		       errno, strerror(errno));
		return;
	}

	/* Only save the size if the locking was successful */
	pool->vp_size = pool->vp_pool_df->pd_scm_sz;
	D_DEBUG(DB_MGMT, "Locking VOS pool in memory "DF_U64" bytes at "DF_X64"\n", pool->vp_size,
		pool->vp_umm.umm_base);
}

/*
 * If successful, this function consumes ph, and closes it upon any error.
 * So the caller shall not close ph in any case.
 */
static int
pool_open(void *ph, struct vos_pool_df *pool_df, unsigned int flags, void *metrics,
	  daos_handle_t *poh)
{
	struct vos_pool		*pool = NULL;
	struct umem_attr	*uma;
	struct d_uuid		 ukey;
	int			 rc;

	/* Create a new handle during open */
	rc = pool_alloc(pool_df->pd_id, &pool); /* returned with refcount=1 */
	if (rc != 0) {
		D_ERROR("Error allocating pool handle\n");
		vos_pmemobj_close(ph);
		return rc;
	}

	uma = &pool->vp_uma;
	uma->uma_id = UMEM_CLASS_PMEM;
	uma->uma_pool = ph;

	/* Initialize dummy data I/O context */
	rc = bio_ioctxt_open(&pool->vp_dummy_ioctxt, vos_xsctxt_get(), pool->vp_id, true);
	if (rc) {
		D_ERROR("Failed to open dummy I/O context. "DF_RC"\n", DP_RC(rc));
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

	pool->vp_metrics = metrics;
	if (bio_nvme_configured(SMD_DEV_TYPE_DATA) && pool_df->pd_nvme_sz != 0) {
		struct vea_unmap_context	 unmap_ctxt;
		struct vos_pool_metrics		*vp_metrics = metrics;
		void				*vea_metrics = NULL;

		if (vp_metrics)
			vea_metrics = vp_metrics->vp_vea_metrics;
		/* set unmap callback fp */
		unmap_ctxt.vnc_unmap = vos_blob_unmap_cb;
		unmap_ctxt.vnc_data = vos_data_ioctxt(pool);
		unmap_ctxt.vnc_ext_flush = flags & VOS_POF_EXTERNAL_FLUSH;
		rc = vea_load(&pool->vp_umm, vos_txd_get(), &pool_df->pd_vea_df,
			      &unmap_ctxt, vea_metrics, &pool->vp_vea_info);
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
	uuid_copy(ukey.uuid, pool_df->pd_id);
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
	pool->vp_rdb = !!(flags & VOS_POF_RDB);
	if (pool_df->pd_version >= VOS_POOL_DF_2_2)
		pool->vp_feats |= VOS_POOL_FEAT_2_2;
	if (pool_df->pd_version >= VOS_POOL_DF_2_4)
		pool->vp_feats |= VOS_POOL_FEAT_2_4;

	vos_space_sys_init(pool);
	/* Ensure GC is triggered after server restart */
	gc_add_pool(pool);
	lock_pool_memory(pool);
	D_DEBUG(DB_MGMT, "Opened pool %p\n", pool);
	return 0;
failed:
	vos_pool_decref(pool); /* -1 for myself */
	return rc;
}

int
vos_pool_open_metrics(const char *path, uuid_t uuid, unsigned int flags, void *metrics,
		      daos_handle_t *poh)
{
	struct vos_pool_df	*pool_df;
	struct vos_pool		*pool = NULL;
	struct d_uuid		 ukey;
	struct umem_pool	*ph;
	int			 rc;
	bool			 skip_uuid_check = flags & VOS_POF_SKIP_UUID_CHECK;

	if (path == NULL || poh == NULL) {
		D_ERROR("Invalid parameters.\n");
		return -DER_INVAL;
	}

	D_DEBUG(DB_MGMT, "Pool Path: %s, UUID: "DF_UUID"\n", path,
		DP_UUID(uuid));

	if (flags & VOS_POF_SMALL)
		flags |= VOS_POF_EXCL;

	if (!skip_uuid_check) {
		uuid_copy(ukey.uuid, uuid);
		rc = pool_lookup(&ukey, &pool);
		if (rc == 0) {
			D_ASSERT(pool != NULL);
			D_DEBUG(DB_MGMT, "Found already opened(%d) pool : %p\n",
				pool->vp_opened, pool);
			if (pool->vp_dying) {
				D_ERROR("Found dying pool : %p\n", pool);
				vos_pool_decref(pool);
				return -DER_BUSY;
			}
			if ((flags & VOS_POF_EXCL) || pool->vp_excl) {
				vos_pool_decref(pool);
				return -DER_BUSY;
			}
			pool->vp_opened++;
			*poh = vos_pool2hdl(pool);
			return 0;
		}
	}

	rc = vos_pmemobj_open(path, uuid, VOS_POOL_LAYOUT, flags, &ph);
	if (rc) {
		D_ERROR("Error in opening the pool "DF_UUID". "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		return rc;
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

	if (!skip_uuid_check && uuid_compare(uuid, pool_df->pd_id)) {
		D_ERROR("Mismatch uuid, user="DF_UUIDF", pool="DF_UUIDF"\n",
			DP_UUID(uuid), DP_UUID(pool_df->pd_id));
		rc = -DER_ID_MISMATCH;
		goto out;
	}

	rc = pool_open(ph, pool_df, flags, metrics, poh);
	ph = NULL;

out:
	/* Close this local handle, if it hasn't been consumed nor already
	 * been closed by pool_open upon error.
	 */
	if (ph != NULL)
		vos_pmemobj_close(ph);
	return rc;
}

int
vos_pool_open(const char *path, uuid_t uuid, unsigned int flags, daos_handle_t *poh)
{
	return vos_pool_open_metrics(path, uuid, flags, NULL, poh);
}

int
vos_pool_upgrade(daos_handle_t poh, uint32_t version)
{
	struct vos_pool    *pool;
	struct vos_pool_df *pool_df;
	int                 rc = 0;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	pool_df = pool->vp_pool_df;

	if (version == pool_df->pd_version)
		return 0;

	D_ASSERTF(version > pool_df->pd_version && version <= POOL_DF_VERSION,
		  "Invalid pool upgrade version %d, current version is %d\n", version,
		  pool_df->pd_version);

	rc = umem_tx_begin(&pool->vp_umm, NULL);
	if (rc != 0)
		return rc;

	rc = umem_tx_add_ptr(&pool->vp_umm, &pool_df->pd_version, sizeof(pool_df->pd_version));
	if (rc != 0)
		goto end;

	pool_df->pd_version = version;

end:
	rc = umem_tx_end(&pool->vp_umm, rc);

	if (rc != 0)
		return rc;

	if (version >= VOS_POOL_DF_2_2)
		pool->vp_feats |= VOS_POOL_FEAT_2_2;
	if (version >= VOS_POOL_DF_2_4)
		pool->vp_feats |= VOS_POOL_FEAT_2_4;

	return 0;
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
	if (rc) {
		return rc;
	} else if (pool->vp_dying) {
		vos_pool_decref(pool);
		return -DER_NONEXIST;
	}

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
vos_pool_ctl(daos_handle_t poh, enum vos_pool_opc opc, void *param)
{
	struct vos_pool		*pool;
	int			i;
	struct policy_desc_t	*p;

	pool = vos_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	switch (opc) {
	default:
		return -DER_NOSYS;
	case VOS_PO_CTL_RESET_GC:
		memset(&pool->vp_gc_stat, 0, sizeof(pool->vp_gc_stat));
		break;
	case VOS_PO_CTL_SET_POLICY:
		if (param == NULL)
			return -DER_INVAL;

		p = param;
		pool->vp_policy_desc.policy = p->policy;

		for (i = 0; i < DAOS_MEDIA_POLICY_PARAMS_MAX; i++)
			pool->vp_policy_desc.params[i] = p->params[i];

		break;
	}

	return 0;
}

/** Convenience function to return address of a bio_addr in pmem.  If it's a hole or NVMe address,
 *  it returns NULL.
 */
const void *
vos_pool_biov2addr(daos_handle_t poh, struct bio_iov *biov)
{
	struct vos_pool *pool;

	pool = vos_hdl2pool(poh);
	D_ASSERT(pool != NULL);

	if (bio_addr_is_hole(&biov->bi_addr))
		return NULL;

	if (bio_iov2media(biov) == DAOS_MEDIA_NVME)
		return NULL;

	return umem_off2ptr(vos_pool2umm(pool), bio_iov2raw_off(biov));
}
