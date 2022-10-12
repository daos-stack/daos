/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/blob.h>
#include <spdk/thread.h>
#include "bio_internal.h"
#include "bio_wal.h"

#define BIO_BLOB_HDR_MAGIC	(0xb0b51ed5)

struct blob_cp_arg {
	spdk_blob_id		 bca_id;
	struct spdk_blob	*bca_blob;
	/*
	 * Completion could run on different xstream when NVMe
	 * device is shared by multiple xstreams.
	 */
	ABT_eventual		 bca_eventual;
	unsigned int		 bca_inflights;
	int			 bca_rc;
};

struct blob_msg_arg {
	struct spdk_blob_opts	 bma_opts;
	struct spdk_blob_store	*bma_bs;
	struct bio_io_context	*bma_ioc;
	spdk_blob_id		 bma_blob_id;
	struct blob_cp_arg	 bma_cp_arg;
	bool			 bma_async;
};

static inline int
blob_cp_arg_init(struct blob_cp_arg *ba)
{
	int	rc;

	rc = ABT_eventual_create(0, &ba->bca_eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	return 0;
}

static inline void
blob_cp_arg_fini(struct blob_cp_arg *ba)
{
	ABT_eventual_free(&ba->bca_eventual);
}

static void
blob_msg_arg_free(struct blob_msg_arg *bma)
{
	blob_cp_arg_fini(&bma->bma_cp_arg);
	D_FREE(bma);
}

static struct blob_msg_arg *
blob_msg_arg_alloc()
{
	struct blob_msg_arg	*bma;
	int			 rc;

	D_ALLOC_PTR(bma);
	if (bma == NULL)
		return NULL;

	rc = blob_cp_arg_init(&bma->bma_cp_arg);
	if (rc) {
		D_FREE(bma);
		return NULL;
	}

	return bma;
}

static void
blob_common_cb(struct blob_cp_arg *ba, int rc)
{
	ba->bca_rc = daos_errno2der(-rc);

	D_ASSERT(ba->bca_inflights > 0);
	ba->bca_inflights--;
	if (ba->bca_inflights == 0)
		ABT_eventual_set(ba->bca_eventual, NULL, 0);
}

/*
 * The blobstore MD operations such as blob open/close/create/delete are
 * always issued by device owner xstream. When a device is shared by multiple
 * xtreams, the non-owner xstream will have to send the MD operations to the
 * owner xstream by spdk_thread_send_msg().
 *
 * The completion callback is called on owner xstream.
 */
static void
blob_create_cb(void *arg, spdk_blob_id blob_id, int rc)
{
	struct blob_msg_arg	*bma = arg;
	struct blob_cp_arg	*ba = &bma->bma_cp_arg;

	ba->bca_id = blob_id;
	blob_common_cb(ba, rc);
}

/*
 * Async open/close happens only when the blobs are being setup or torn down
 * on device replaced/faulty event. In this stage, the bio_io_context is
 * guaranteed to be accessed by device owner xstream exclusively, so it's ok
 * to change the io context without locking in async mode.
 */
static void
blob_open_cb(void *arg, struct spdk_blob *blob, int rc)
{
	struct blob_msg_arg	*bma = arg;
	struct blob_cp_arg	*ba = &bma->bma_cp_arg;
	bool			 async = bma->bma_async;

	ba->bca_blob = blob;
	blob_common_cb(ba, rc);

	/*
	 * When sync open caller is on different xstream, the 'bma' could
	 * be changed/freed after blob_common_cb(), so we have to use the
	 * saved 'async' here.
	 */
	if (async) {
		struct bio_io_context	*ioc = bma->bma_ioc;

		ioc->bic_opening = 0;
		if (rc == 0)
			ioc->bic_blob = blob;
		blob_msg_arg_free(bma);
	}
}

static void
blob_close_cb(void *arg, int rc)
{
	struct blob_msg_arg	*bma = arg;
	struct blob_cp_arg	*ba = &bma->bma_cp_arg;
	bool			 async = bma->bma_async;

	blob_common_cb(ba, rc);

	/* See comments in blob_open_cb() */
	if (async) {
		struct bio_io_context	*ioc = bma->bma_ioc;

		ioc->bic_closing = 0;
		if (rc == 0)
			ioc->bic_blob = NULL;
		blob_msg_arg_free(bma);
	}
}

static void
blob_unmap_cb(void *arg, int rc)
{
	struct blob_msg_arg	*bma = arg;
	struct blob_cp_arg	*ba = &bma->bma_cp_arg;
	struct bio_xs_blobstore	*bxb;

	bxb = bma->bma_ioc->bic_xs_blobstore;
	D_ASSERT(bxb != NULL);
	D_ASSERT(bxb->bxb_blob_rw > 0);
	bxb->bxb_blob_rw--;

	blob_common_cb(ba, rc);
}

static void
blob_cb(void *arg, int rc)
{
	struct blob_msg_arg	*bma = arg;
	struct blob_cp_arg	*ba = &bma->bma_cp_arg;

	blob_common_cb(ba, rc);
}

static void
blob_wait_completion(struct bio_xs_context *xs_ctxt, struct blob_cp_arg *ba)
{
	int	rc;

	D_ASSERT(xs_ctxt != NULL);
	if (xs_ctxt->bxc_self_polling) {
		D_DEBUG(DB_IO, "Self poll xs_ctxt:%p\n", xs_ctxt);
		rc = xs_poll_completion(xs_ctxt, &ba->bca_inflights, 0);
		D_ASSERT(rc == 0);
	} else {
		rc = ABT_eventual_wait(ba->bca_eventual, NULL);
		if (rc != ABT_SUCCESS)
			D_ERROR("ABT eventual wait failed. %d", rc);
	}
}

static void
blob_msg_create(void *msg_arg)
{
	struct blob_msg_arg *arg = msg_arg;

	spdk_bs_create_blob_ext(arg->bma_bs, &arg->bma_opts, blob_create_cb,
				msg_arg);
}

static void
blob_msg_delete(void *msg_arg)
{
	struct blob_msg_arg *arg = msg_arg;

	spdk_bs_delete_blob(arg->bma_bs, arg->bma_blob_id, blob_cb, msg_arg);
}

static void
blob_msg_open(void *msg_arg)
{
	struct blob_msg_arg *arg = msg_arg;

	spdk_bs_open_blob(arg->bma_bs, arg->bma_blob_id, blob_open_cb, msg_arg);
}

static void
blob_msg_close(void *msg_arg)
{
	struct blob_msg_arg	*arg = msg_arg;
	struct spdk_blob	*blob = arg->bma_ioc->bic_blob;

	spdk_blob_close(blob, blob_close_cb, msg_arg);
}

static void
bio_bs_unhold(struct bio_blobstore *bbs)
{
	D_ASSERT(bbs != NULL);
	ABT_mutex_lock(bbs->bb_mutex);
	D_ASSERT(bbs->bb_holdings > 0);
	bbs->bb_holdings--;
	ABT_mutex_unlock(bbs->bb_mutex);
}

static int
bio_bs_hold(struct bio_blobstore *bbs)
{
	int rc = 0;

	D_ASSERT(bbs != NULL);
	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_bs == NULL) {
		D_ERROR("Blobstore %p is closed, fail request.\n", bbs);
		rc = -DER_NO_HDL;
		goto out;
	}

	if (bbs->bb_state == BIO_BS_STATE_TEARDOWN ||
	    bbs->bb_state == BIO_BS_STATE_OUT ||
	    bbs->bb_state == BIO_BS_STATE_SETUP) {
		D_ERROR("Blobstore %p is in %s state, reject request.\n",
			bbs, bio_state_enum_to_str(bbs->bb_state));
		rc = -DER_DOS;
		goto out;
	}

	/*
	 * Hold the blobstore, and the blobs teardown on faulty reaction taken
	 * by device owner xstream will be deferred until the blobstore is
	 * unheld. That ensures owner xstream's exclusive access to the io
	 * context when tearing down blobs.
	 */
	bbs->bb_holdings++;
out:
	ABT_mutex_unlock(bbs->bb_mutex);
	return rc;
}

/**
 * There might be few cases with SSD on metata enabled.
 *	DATA		META		WAL
 *	Y		N		N
 *	Y		Y		N
 *	Y		Y		Y
 *	N		Y		Y
 *	N		Y		N
 *
 * Case1: META && WAL share same blobstore with DATA.
 * Case2: WAL share same blobstore with META.
 * Case3: DATA, META, WAL have own blobstore separately.
 * case4: no DATA blobstore, WAL, META have separate blobstore.
 * case5: no DATA blobstore, WAL & META share same blobstore.
 */
struct bio_xs_blobstore *
bio_xs_context2xs_blobstore(struct bio_xs_context *xs_ctxt, enum smd_dev_type st)
{
	struct bio_xs_blobstore *bxb = NULL;

	if (st != SMD_DEV_TYPE_DATA)
		D_ASSERT(xs_ctxt->bxc_meta_on_ssd != 0);

	switch (st) {
	case SMD_DEV_TYPE_WAL:
		if (xs_ctxt->bxc_xs_blobstores[st] != NULL) {
			bxb = xs_ctxt->bxc_xs_blobstores[st];
			break;
		}
		/* fall through */
	case SMD_DEV_TYPE_META:
		if (xs_ctxt->bxc_xs_blobstores[st] != NULL) {
			bxb = xs_ctxt->bxc_xs_blobstores[st];
			break;
		}
		/* fall through */
	case SMD_DEV_TYPE_DATA:
		bxb = xs_ctxt->bxc_xs_blobstores[st];
		break;
	default:
		D_ASSERT(0);
		break;
	}

	return bxb;
}

static int
bio_blob_delete(uuid_t uuid, struct bio_xs_context *xs_ctxt, enum smd_dev_type st)
{
	struct blob_msg_arg		 bma = { 0 };
	struct blob_cp_arg		*ba = &bma.bma_cp_arg;
	struct bio_blobstore		*bbs;
	struct bio_xs_blobstore		*bxb;
	spdk_blob_id			 blob_id;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
	/**
	 * Query per-server metadata to get blobID for this pool:target
	 */
	rc = smd_pool_get_blob(uuid, xs_ctxt->bxc_tgt_id, &blob_id);
	if (rc != 0) {
		D_WARN("Blob for xs:%p, pool:"DF_UUID" doesn't exist\n",
		       xs_ctxt, DP_UUID(uuid));
		/*
		 * User may create a pool w/o NVMe partition even with NVMe
		 * configured.
		 *
		 * TODO: Let's simply return success for this moment, the
		 * pool create & destroy code needs be re-organized later to
		 * handle various middle failure cases, then we should
		 * improve this by checking the 'pd_nvme_sz' and avoid
		 * calling into this function when 'pd_nvme_sz' == 0.
		 */
		return 0;
	}

	rc = blob_cp_arg_init(ba);
	if (rc != 0)
		return rc;

	bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);
	D_ASSERT(bxb != NULL);
	bbs = bxb->bxb_blobstore;
	D_ASSERT(bbs != NULL);
	rc = bio_bs_hold(bbs);
	if (rc) {
		blob_cp_arg_fini(ba);
		return rc;
	}

	D_DEBUG(DB_MGMT, "Deleting blobID "DF_U64" for pool:"DF_UUID" xs:%p\n",
		blob_id, DP_UUID(uuid), xs_ctxt);

	ba->bca_inflights = 1;
	bma.bma_bs = bbs->bb_bs;
	bma.bma_blob_id = blob_id;
	spdk_thread_send_msg(owner_thread(bbs), blob_msg_delete, &bma);

	/* Wait for blob delete done */
	blob_wait_completion(xs_ctxt, ba);
	rc = ba->bca_rc;

	if (rc != 0) {
		D_ERROR("Delete blobID "DF_U64" failed for pool:"DF_UUID" "
			"xs:%p rc:%d\n", blob_id, DP_UUID(uuid), xs_ctxt, rc);
	} else {
		D_DEBUG(DB_MGMT, "Successfully deleted blobID "DF_U64" for "
			"pool:"DF_UUID" xs:%p\n", blob_id, DP_UUID(uuid),
			xs_ctxt);
		rc = smd_pool_del_tgt(uuid, xs_ctxt->bxc_tgt_id);
		if (rc)
			D_ERROR("Failed to unassign blob:"DF_U64" from pool: "
				""DF_UUID":%d. %d\n", blob_id, DP_UUID(uuid),
				xs_ctxt->bxc_tgt_id, rc);
	}

	bio_bs_unhold(bbs);
	blob_cp_arg_fini(ba);
	return rc;
}

int bio_mc_destroy(struct bio_xs_context *xs_ctxt, uuid_t pool_id)
{
	int			 rc = 0;
	enum smd_dev_type	 st;
	struct bio_xs_blobstore	*bxb;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);
		D_ASSERT(bxb != NULL);
		rc = bio_blob_delete(pool_id, xs_ctxt, st);
		if (rc)
			return rc;
		if (xs_ctxt->bxc_meta_on_ssd == 0)
			break;
	}

	return 0;
}

static int
bio_blob_create(uuid_t uuid, struct bio_xs_context *xs_ctxt, uint64_t blob_sz,
		enum smd_dev_type st)
{
	struct blob_msg_arg		 bma = { 0 };
	struct blob_cp_arg		*ba = &bma.bma_cp_arg;
	struct bio_xs_blobstore		*bxb;
	struct bio_blobstore		*bbs;
	uint64_t			 cluster_sz;
	spdk_blob_id			 blob_id;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
	bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);
	D_ASSERT(bxb != NULL);

	bbs = bxb->bxb_blobstore;
	cluster_sz = bbs->bb_bs != NULL ?
		spdk_bs_get_cluster_size(bbs->bb_bs) : 0;

	if (cluster_sz == 0) {
		D_ERROR("Blobstore is already closed?\n");
		return -DER_NO_HDL;
	}

	if (blob_sz < cluster_sz) {
		/* Blob needs to be at least 1 cluster */
		D_ERROR("Blob size is less than the size of a cluster "DF_U64""
			" < "DF_U64"\n", blob_sz, cluster_sz);
		return -DER_INVAL;
	}

	spdk_blob_opts_init(&bma.bma_opts, sizeof(bma.bma_opts));
	bma.bma_opts.num_clusters = (blob_sz + cluster_sz - 1) / cluster_sz;

	/**
	 * Query per-server metadata to make sure the blob for this pool:target
	 * hasn't been created yet.
	 */
	rc = smd_pool_get_blob(uuid, xs_ctxt->bxc_tgt_id, &blob_id);
	if (rc == 0) {
		D_ERROR("Duplicated blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(uuid));
		return -DER_EXIST;
	}

	rc = blob_cp_arg_init(ba);
	if (rc != 0)
		return rc;

	rc = bio_bs_hold(bbs);
	if (rc) {
		blob_cp_arg_fini(ba);
		return rc;
	}

	ba->bca_inflights = 1;
	bma.bma_bs = bbs->bb_bs;
	spdk_thread_send_msg(owner_thread(bbs), blob_msg_create, &bma);

	/* Wait for blob creation done */
	blob_wait_completion(xs_ctxt, ba);
	rc = ba->bca_rc;

	if (rc != 0) {
		D_ERROR("Create blob failed for xs:%p pool:"DF_UUID" rc:%d\n",
			xs_ctxt, DP_UUID(uuid), rc);
	} else {
		D_ASSERT(ba->bca_id != 0);
		D_DEBUG(DB_MGMT, "Successfully created blobID "DF_U64" for xs:"
			"%p pool:"DF_UUID" blob size:"DF_U64" clusters\n",
			ba->bca_id, xs_ctxt, DP_UUID(uuid),
			bma.bma_opts.num_clusters);

		rc = smd_pool_add_tgt(uuid, xs_ctxt->bxc_tgt_id, ba->bca_id,
				      blob_sz);
		if (rc != 0) {
			D_ERROR("Failed to assign pool blob:"DF_U64" to pool: "
				""DF_UUID":%d. %d\n", ba->bca_id, DP_UUID(uuid),
				xs_ctxt->bxc_tgt_id, rc);
			/* Delete newly created blob */
			if (bio_blob_delete(uuid, xs_ctxt, st))
				D_ERROR("Unable to delete newly created blobID "
					""DF_U64" for xs:%p pool:"DF_UUID"\n",
					ba->bca_id, xs_ctxt, DP_UUID(uuid));
		} else {
			D_DEBUG(DB_MGMT, "Successfully assign blob:"DF_U64" "
				"to pool:"DF_UUID":%d\n", ba->bca_id,
				DP_UUID(uuid), xs_ctxt->bxc_tgt_id);
		}
	}

	bio_bs_unhold(bbs);
	blob_cp_arg_fini(ba);
	return rc;
}

int bio_mc_create(struct bio_xs_context *xs_ctxt, uuid_t pool_id, uint64_t meta_sz,
		  uint64_t wal_sz, uint64_t data_sz, enum bio_mc_flags flags)
{
	int rc = 0;

	if (data_sz > 0) {
		rc = bio_blob_create(pool_id, xs_ctxt, data_sz, SMD_DEV_TYPE_DATA);
		if (rc)
			return rc;
	}

	if (meta_sz > 0) {
		rc = bio_blob_create(pool_id, xs_ctxt, meta_sz, SMD_DEV_TYPE_META);
		if (rc)
			goto delete_data;
	}

	if (wal_sz > 0) {
		rc = bio_blob_create(pool_id, xs_ctxt, wal_sz, SMD_DEV_TYPE_META);
		if (rc)
			goto delete_meta;
	}

	return rc;
delete_meta:
	if (bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_META))
		D_ERROR("Unable to delete newly created meta blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(pool_id));
delete_data:
	if (bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_DATA))
		D_ERROR("Unable to delete newly created data blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(pool_id));
	return rc;
}

int
bio_blob_open(struct bio_io_context *ctxt, bool async)
{
	struct bio_xs_context		*xs_ctxt = ctxt->bic_xs_ctxt;
	spdk_blob_id			 blob_id;
	struct blob_msg_arg		*bma;
	struct blob_cp_arg		*ba;
	struct bio_blobstore		*bbs;
	int				 rc;

	if (ctxt->bic_blob != NULL) {
		D_ERROR("Blob %p is already opened\n", ctxt->bic_blob);
		return -DER_ALREADY;
	} else if (ctxt->bic_opening) {
		D_ERROR("Blob is in opening\n");
		return -DER_AGAIN;
	}
	D_ASSERT(!ctxt->bic_closing);

	D_ASSERT(xs_ctxt != NULL);
	bbs = ctxt->bic_xs_blobstore->bxb_blobstore;
	ctxt->bic_io_unit = spdk_bs_get_io_unit_size(bbs->bb_bs);
	D_ASSERT(ctxt->bic_io_unit > 0 && ctxt->bic_io_unit <= BIO_DMA_PAGE_SZ);

	/*
	 * Query per-server metadata to get blobID for this pool:target
	 */
	rc = smd_pool_get_blob(ctxt->bic_pool_id, xs_ctxt->bxc_tgt_id,
			       &blob_id);
	if (rc != 0) {
		D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID", tgt:%d\n",
			xs_ctxt, DP_UUID(ctxt->bic_pool_id), xs_ctxt->bxc_tgt_id);
		return -DER_NONEXIST;
	}

	bma = blob_msg_arg_alloc();
	if (bma == NULL)
		return -DER_NOMEM;
	ba = &bma->bma_cp_arg;

	D_DEBUG(DB_MGMT, "Opening blobID "DF_U64" for xs:%p pool:"DF_UUID"\n",
		blob_id, xs_ctxt, DP_UUID(ctxt->bic_pool_id));

	ctxt->bic_opening = 1;
	ba->bca_inflights = 1;
	bma->bma_bs = bbs->bb_bs;
	bma->bma_blob_id = blob_id;
	bma->bma_async = async;
	bma->bma_ioc = ctxt;
	spdk_thread_send_msg(owner_thread(bbs), blob_msg_open, bma);

	if (async)
		return 0;

	/* Wait for blob open done */
	blob_wait_completion(xs_ctxt, ba);
	rc = ba->bca_rc;
	ctxt->bic_opening = 0;

	if (rc != 0) {
		D_ERROR("Open blobID "DF_U64" failed for xs:%p pool:"DF_UUID" "
			"rc:%d\n", blob_id, xs_ctxt, DP_UUID(ctxt->bic_pool_id),
			rc);
	} else {
		D_ASSERT(ba->bca_blob != NULL);
		D_DEBUG(DB_MGMT, "Successfully opened blobID "DF_U64" for xs:%p"
			" pool:"DF_UUID" blob:%p\n", blob_id, xs_ctxt,
			DP_UUID(ctxt->bic_pool_id), ba->bca_blob);
		ctxt->bic_blob = ba->bca_blob;
	}

	blob_msg_arg_free(bma);
	return rc;
}

void
bio_mc_init_umem(struct bio_meta_context *mc, struct umem_instance *umem)
{
	if (mc->mc_data != NULL)
		mc->mc_data->bic_umem = umem;

	if (mc->mc_meta != NULL)
		mc->mc_meta->bic_umem = umem;

	if (mc->mc_wal != NULL)
		mc->mc_wal->bic_umem = umem;

}

static int
__bio_ioctxt_open(struct bio_io_context **pctxt, struct bio_xs_context *xs_ctxt,
		  uuid_t uuid, enum bio_mc_flags flags, enum smd_dev_type st)
{
	struct bio_io_context	*ctxt;
	int			 rc;
	struct bio_xs_blobstore	*bxb;

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ctxt->bic_link);
	ctxt->bic_xs_ctxt = xs_ctxt;
	uuid_copy(ctxt->bic_pool_id, uuid);

	/* NVMe isn't configured or pool doesn't have NVMe partition */
	if (!bio_nvme_configured() || flags & BIO_MC_FL_NO_DATABLOB) {
		*pctxt = ctxt;
		return 0;
	}

	bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);
	D_ASSERT(bxb != NULL);
	rc = bio_bs_hold(bxb->bxb_blobstore);
	if (rc) {
		D_FREE(ctxt);
		return rc;
	}

	ctxt->bic_xs_blobstore = bxb;
	rc = bio_blob_open(ctxt, false);
	if (rc) {
		D_FREE(ctxt);
	} else {
		d_list_add_tail(&ctxt->bic_link, &bxb->bxb_io_ctxts);
		*pctxt = ctxt;
	}

	bio_bs_unhold(bxb->bxb_blobstore);
	return rc;
}

int bio_ioctxt_open(struct bio_io_context **pctxt, struct bio_xs_context *xs_ctxt,
		    struct umem_instance *umem, uuid_t uuid)
{
	int rc;

	rc = __bio_ioctxt_open(pctxt, xs_ctxt, uuid, 0, SMD_DEV_TYPE_DATA);
	if (rc)
		return rc;

	(*pctxt)->bic_umem = umem;

	return rc;
}

int bio_mc_open(struct bio_xs_context *xs_ctxt, uuid_t pool_id,
		enum bio_mc_flags flags, struct bio_meta_context **mc)
{
	struct bio_meta_context	*bio_mc;
	int			 rc;

	D_ALLOC_PTR(bio_mc);
	if (bio_mc == NULL)
		return -DER_NOMEM;

	if (xs_ctxt == NULL)
		goto out;

	if (xs_ctxt->bxc_meta_on_ssd == 1) {
		rc = __bio_ioctxt_open(&bio_mc->mc_meta, xs_ctxt, pool_id,
				       flags, SMD_DEV_TYPE_META);
		if (rc)
			goto failed;
		/*
		 * Check Meta blob header if pool nvme size is 0,
		 * set BIO_MC_FL_NO_DATABLOB if needed.
		 */
		rc = __bio_ioctxt_open(&bio_mc->mc_wal, xs_ctxt, pool_id,
				       flags, SMD_DEV_TYPE_WAL);
		if (rc)
			goto failed;
	}

out:
	rc = __bio_ioctxt_open(&bio_mc->mc_data, xs_ctxt, pool_id, flags,
			       SMD_DEV_TYPE_DATA);
	if (rc)
		goto failed;

	*mc = bio_mc;
	return 0;

failed:
	bio_mc_close(bio_mc, flags);

	return rc;
}

int
bio_blob_close(struct bio_io_context *ctxt, bool async)
{
	struct blob_msg_arg	*bma;
	struct blob_cp_arg	*ba;
	struct bio_blobstore	*bbs;
	int			 rc;

	D_ASSERT(!ctxt->bic_opening);
	if (ctxt->bic_blob == NULL) {
		D_ERROR("Blob is already closed\n");
		return -DER_ALREADY;
	} else if (ctxt->bic_closing) {
		D_ERROR("The blob is in closing\n");
		return -DER_AGAIN;
	} else if (ctxt->bic_inflight_dmas) {
		D_ERROR("There are %u inflight blob IOs\n",
			ctxt->bic_inflight_dmas);
		return -DER_BUSY;
	}

	bma = blob_msg_arg_alloc();
	if (bma == NULL)
		return -DER_NOMEM;
	ba = &bma->bma_cp_arg;

	D_ASSERT(ctxt->bic_xs_ctxt != NULL);
	bbs = ctxt->bic_xs_blobstore->bxb_blobstore;

	D_DEBUG(DB_MGMT, "Closing blob %p for xs:%p\n", ctxt->bic_blob,
		ctxt->bic_xs_ctxt);

	ctxt->bic_closing = 1;
	ba->bca_inflights = 1;
	bma->bma_ioc = ctxt;
	bma->bma_async = async;
	spdk_thread_send_msg(owner_thread(bbs), blob_msg_close, bma);

	if (async)
		return 0;

	/* Wait for blob close done */
	blob_wait_completion(ctxt->bic_xs_ctxt, ba);
	rc = ba->bca_rc;
	ctxt->bic_closing = 0;

	if (rc != 0) {
		D_ERROR("Close blob %p failed for xs:%p rc:%d\n",
			ctxt->bic_blob, ctxt->bic_xs_ctxt, rc);
	} else {
		D_DEBUG(DB_MGMT, "Successfully closed blob %p for xs:%p\n",
			ctxt->bic_blob, ctxt->bic_xs_ctxt);
		ctxt->bic_blob = NULL;
	}

	blob_msg_arg_free(bma);
	return rc;
}

int
bio_ioctxt_close(struct bio_io_context *ctxt, enum bio_mc_flags flags)
{
	int			 rc;

	/* NVMe isn't configured or pool doesn't have NVMe partition */
	if (!bio_nvme_configured() || flags & BIO_MC_FL_NO_DATABLOB) {
		d_list_del_init(&ctxt->bic_link);
		D_FREE(ctxt);
		return 0;
	}

	rc = bio_bs_hold(ctxt->bic_xs_blobstore->bxb_blobstore);
	if (rc)
		return rc;

	rc = bio_blob_close(ctxt, false);

	/* Free the io context no matter if close succeeded */
	d_list_del_init(&ctxt->bic_link);
	bio_bs_unhold(ctxt->bic_xs_blobstore->bxb_blobstore);
	D_FREE(ctxt);

	return rc;
}

int bio_mc_close(struct bio_meta_context *bio_mc, enum bio_mc_flags flags)
{
	int	rc = 0;
	int	rc1;

	if (bio_mc->mc_data) {
		rc1 = bio_ioctxt_close(bio_mc->mc_data, flags);
		if (rc1)
			rc = rc1;
	}
	if (bio_mc->mc_meta) {
		rc1 = bio_ioctxt_close(bio_mc->mc_meta, flags);
		if (rc1 && !rc)
			rc = rc1;
	}
	if (bio_mc->mc_wal) {
		rc1 = bio_ioctxt_close(bio_mc->mc_wal, flags);
		if (rc1 && !rc)
			rc = rc1;
	}

	D_FREE(bio_mc);

	return rc;
}

int
bio_blob_unmap(struct bio_io_context *ioctxt, uint64_t off, uint64_t len)
{
	struct blob_msg_arg	 bma = { 0 };
	struct blob_cp_arg	*ba = &bma.bma_cp_arg;
	struct spdk_io_channel	*channel;
	struct media_error_msg	*mem;
	uint64_t		 pg_off;
	uint64_t		 pg_cnt;
	int			 rc;

	/*
	 * TODO: track inflight DMA extents and check the tracked extents
	 *	 on blob unmap to avoid following very unlikely race:
	 *
	 * 1. VOS fetch locates a blob extent and trigger DMA transfer;
	 * 2. That blob extent is freed by VOS aggregation;
	 * 3. The freed extent stays in VEA aging buffer for few(10) seconds,
	 *    then it's being unmapped before being made available for new
	 *    allocation;
	 * 4. If the DMA transfer for fetch takes a very long time and isn't
	 *    done  before the unmap call, corrupted data could be returned.
	 */
	D_ASSERT(len > 0);

	/* blob unmap can only support page aligned offset and length */
	D_ASSERT((len & (BIO_DMA_PAGE_SZ - 1)) == 0);
	D_ASSERT((off & (BIO_DMA_PAGE_SZ - 1)) == 0);

	/* convert byte to blob/page offset */
	pg_off = off >> BIO_DMA_PAGE_SHIFT;
	pg_cnt = len >> BIO_DMA_PAGE_SHIFT;

	D_ASSERT(ioctxt->bic_xs_ctxt != NULL);
	channel = ioctxt->bic_xs_blobstore->bxb_io_channel;

	if (!is_blob_valid(ioctxt)) {
		D_ERROR("Blobstore is invalid. blob:%p, closing:%d\n",
			ioctxt->bic_blob, ioctxt->bic_closing);
		return -DER_NO_HDL;
	}

	rc = blob_cp_arg_init(ba);
	if (rc)
		return rc;

	D_DEBUG(DB_MGMT, "Unmapping blob %p pgoff:"DF_U64" pgcnt:"DF_U64"\n",
		ioctxt->bic_blob, pg_off, pg_cnt);

	ioctxt->bic_inflight_dmas++;
	ba->bca_inflights = 1;
	spdk_blob_io_unmap(ioctxt->bic_blob, channel,
			   page2io_unit(ioctxt, pg_off, BIO_DMA_PAGE_SZ),
			   page2io_unit(ioctxt, pg_cnt, BIO_DMA_PAGE_SZ),
			   blob_cb, &bma);

	/* Wait for blob unmap done */
	blob_wait_completion(ioctxt->bic_xs_ctxt, ba);
	rc = ba->bca_rc;
	ioctxt->bic_inflight_dmas--;

	if (rc) {
		D_ERROR("Unmap blob %p failed for xs: %p rc:%d\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt, rc);
		D_ALLOC_PTR(mem);
		if (mem == NULL)
			goto skip_media_error;
		mem->mem_err_type = MET_UNMAP;
		mem->mem_bs = ioctxt->bic_xs_blobstore->bxb_blobstore;
		mem->mem_tgt_id = ioctxt->bic_xs_ctxt->bxc_tgt_id;
		spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error,
				     mem);
	} else
		D_DEBUG(DB_MGMT, "Successfully unmapped blob %p for xs:%p\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

skip_media_error:
	blob_cp_arg_fini(ba);

	return rc;
}

static int
blob_unmap_sgl(struct bio_io_context *ioctxt, d_sg_list_t *unmap_sgl, uint32_t blk_sz,
	       unsigned int start_idx, unsigned int unmap_cnt)
{
	struct bio_xs_context	*xs_ctxt;
	struct blob_msg_arg	 bma = { 0 };
	struct blob_cp_arg	*ba = &bma.bma_cp_arg;
	struct spdk_io_channel	*channel;
	d_iov_t			*unmap_iov;
	uint64_t		 pg_off, pg_cnt;
	int			 i, rc;
	struct bio_xs_blobstore *bxb;

	xs_ctxt = ioctxt->bic_xs_ctxt;
	D_ASSERT(xs_ctxt != NULL);
	bxb = ioctxt->bic_xs_blobstore;
	channel = ioctxt->bic_xs_blobstore->bxb_io_channel;

	if (!is_blob_valid(ioctxt)) {
		D_ERROR("Blobstore is invalid. blob:%p, closing:%d\n",
			ioctxt->bic_blob, ioctxt->bic_closing);
		return -DER_NO_HDL;
	}

	rc = blob_cp_arg_init(ba);
	if (rc)
		return rc;

	bma.bma_ioc = ioctxt;
	ioctxt->bic_inflight_dmas++;
	ba->bca_inflights = 1;

	i = start_idx;
	while (unmap_cnt > 0) {
		unmap_iov = &unmap_sgl->sg_iovs[i];
		i++;
		unmap_cnt--;

		drain_inflight_ios(xs_ctxt, bxb);

		ba->bca_inflights++;
		bxb->bxb_blob_rw++;

		pg_off = (uint64_t)unmap_iov->iov_buf;
		pg_cnt = unmap_iov->iov_len;

		D_DEBUG(DB_IO, "Unmapping blob %p pgoff:"DF_U64" pgcnt:"DF_U64"\n",
			ioctxt->bic_blob, pg_off, pg_cnt);

		spdk_blob_io_unmap(ioctxt->bic_blob, channel,
				   page2io_unit(ioctxt, pg_off, blk_sz),
				   page2io_unit(ioctxt, pg_cnt, blk_sz),
				   blob_unmap_cb, &bma);
	}
	ba->bca_inflights--;

	if (ba->bca_inflights > 0)
		blob_wait_completion(xs_ctxt, ba);
	rc = ba->bca_rc;
	ioctxt->bic_inflight_dmas--;

	if (rc) {
		struct media_error_msg	*mem;

		D_ERROR("Unmap blob %p for xs: %p failed. "DF_RC"\n",
			ioctxt->bic_blob, xs_ctxt, DP_RC(rc));

		D_ALLOC_PTR(mem);
		if (mem == NULL)
			goto done;

		mem->mem_err_type = MET_UNMAP;
		mem->mem_bs = bxb->bxb_blobstore;
		mem->mem_tgt_id = xs_ctxt->bxc_tgt_id;
		spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error, mem);
	}
done:
	blob_cp_arg_fini(ba);
	return rc;
}

int
bio_blob_unmap_sgl(struct bio_io_context *ioctxt, d_sg_list_t *unmap_sgl, uint32_t blk_sz)
{
	unsigned int	start_idx, tot_unmap_cnt, unmap_cnt;
	int		rc = 0;

	D_ASSERT(blk_sz >= ioctxt->bic_io_unit && (blk_sz & (ioctxt->bic_io_unit - 1)) == 0);

	tot_unmap_cnt = unmap_sgl->sg_nr_out;
	start_idx = 0;
	while (tot_unmap_cnt > 0) {
		unmap_cnt = min(tot_unmap_cnt, bio_spdk_max_unmap_cnt);

		rc = blob_unmap_sgl(ioctxt, unmap_sgl, blk_sz, start_idx, unmap_cnt);
		if (rc)
			break;

		tot_unmap_cnt -= unmap_cnt;
		start_idx += unmap_cnt;
	}

	return rc;
}

int
bio_write_blob_hdr(struct bio_io_context *ioctxt, struct bio_blob_hdr *bio_bh)
{
	struct smd_dev_info	*dev_info;
	spdk_blob_id		 blob_id;
	d_iov_t			 iov;
	bio_addr_t		 addr = { 0 };
	uint64_t		 off = 0; /* byte offset in SPDK blob */
	uint16_t		 dev_type = DAOS_MEDIA_NVME;
	int			 rc = 0;

	D_DEBUG(DB_MGMT, "Writing header blob:%p, xs:%p\n",
		ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

	/* check that all VOS blob header vars are set */
	D_ASSERT(uuid_is_null(bio_bh->bbh_pool) == 0);
	if (bio_bh->bbh_blk_sz == 0 || bio_bh->bbh_hdr_sz == 0)
		return -DER_INVAL;

	bio_addr_set(&addr, dev_type, off);

	/*
	 * Set all BIO-related members of blob header.
	 */
	bio_bh->bbh_magic = BIO_BLOB_HDR_MAGIC;
	bio_bh->bbh_vos_id = (uint32_t)ioctxt->bic_xs_ctxt->bxc_tgt_id;
	/* Query per-server metadata to get blobID for this pool:target */
	rc = smd_pool_get_blob(bio_bh->bbh_pool, bio_bh->bbh_vos_id, &blob_id);
	if (rc) {
		D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID"\n",
			ioctxt->bic_xs_ctxt, DP_UUID(bio_bh->bbh_pool));
		return rc;
	}

	bio_bh->bbh_blob_id = blob_id;

	/* Query per-server metadata to get device id for xs */
	rc = smd_dev_get_by_tgt(bio_bh->bbh_vos_id, &dev_info);
	if (rc) {
		D_ERROR("Not able to find device id/blobstore for tgt %d\n",
			bio_bh->bbh_vos_id);
		return rc;
	}

	uuid_copy(bio_bh->bbh_blobstore, dev_info->sdi_id);
	smd_dev_free_info(dev_info);

	/* Create an iov to store blob header structure */
	d_iov_set(&iov, (void *)bio_bh, sizeof(*bio_bh));

	rc = bio_write(ioctxt, addr, &iov);

	return rc;
}

struct bio_io_context *bio_mc2data(struct bio_meta_context *mc)
{
	return mc->mc_data;
}

bool
bio_xs_is_meta_on_ssd(struct bio_xs_context *xs_ctxt)
{
	return xs_ctxt->bxc_meta_on_ssd == 1;
}
