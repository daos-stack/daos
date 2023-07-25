/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

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
			D_ERROR("ABT eventual wait failed. %d\n", rc);
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

struct bio_xs_blobstore *
bio_xs_blobstore_by_devid(struct bio_xs_context *xs_ctxt, uuid_t dev_uuid)
{
	enum smd_dev_type	 st;
	struct bio_xs_blobstore *bxb = NULL;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = xs_ctxt->bxc_xs_blobstores[st];
		if (!bxb)
			continue;

		if (uuid_compare(bxb->bxb_blobstore->bb_dev->bb_uuid, dev_uuid) == 0)
			return bxb;
	}

	return NULL;
}

/**
 * Case1: WAL, meta and data share the same blobstore.
 * Case2: WAL and meta share the same blobstore, data on dedicated blobstore.
 * Case3: WAL on dedicated blobstore, meta and data share the same blobstore.
 * Case4: WAL, meta and data are on dedicated blobstore respectively.
 */
struct bio_xs_blobstore *
bio_xs_context2xs_blobstore(struct bio_xs_context *xs_ctxt, enum smd_dev_type st)
{
	struct bio_xs_blobstore *bxb = NULL;

	if (st != SMD_DEV_TYPE_DATA)
		D_ASSERT(bio_nvme_configured(SMD_DEV_TYPE_META));

	switch (st) {
	case SMD_DEV_TYPE_WAL:
		if (xs_ctxt->bxc_xs_blobstores[SMD_DEV_TYPE_WAL] != NULL) {
			bxb = xs_ctxt->bxc_xs_blobstores[SMD_DEV_TYPE_WAL];
			break;
		}
		/* fall through */
	case SMD_DEV_TYPE_META:
		if (xs_ctxt->bxc_xs_blobstores[SMD_DEV_TYPE_META] != NULL) {
			bxb = xs_ctxt->bxc_xs_blobstores[SMD_DEV_TYPE_META];
			break;
		}
		/* fall through */
	case SMD_DEV_TYPE_DATA:
		bxb = xs_ctxt->bxc_xs_blobstores[SMD_DEV_TYPE_DATA];
		break;
	default:
		D_ASSERT(0);
		break;
	}

	return bxb;
}

static int
bio_blob_delete(uuid_t uuid, struct bio_xs_context *xs_ctxt, enum smd_dev_type st,
		spdk_blob_id blob_id, enum bio_mc_flags flags)
{
	struct blob_msg_arg		 bma = { 0 };
	struct blob_cp_arg		*ba = &bma.bma_cp_arg;
	struct bio_blobstore		*bbs;
	struct bio_xs_blobstore		*bxb;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
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
		if (flags & BIO_MC_FL_RDB)
			rc = smd_rdb_del_tgt(uuid, xs_ctxt->bxc_tgt_id, st);
		else
			rc = smd_pool_del_tgt(uuid, xs_ctxt->bxc_tgt_id, st);
		if (rc)
			D_ERROR("Failed to unassign blob:"DF_U64" from pool: "
				""DF_UUID":%d. %d\n", blob_id, DP_UUID(uuid),
				xs_ctxt->bxc_tgt_id, rc);
	}

	bio_bs_unhold(bbs);
	blob_cp_arg_fini(ba);
	return rc;
}

int bio_mc_destroy(struct bio_xs_context *xs_ctxt, uuid_t pool_id, enum bio_mc_flags flags)
{
	struct bio_meta_context	*mc;
	spdk_blob_id		 data_blobid, wal_blobid, meta_blobid;
	int			 rc;

	D_ASSERT(xs_ctxt != NULL);
	if (!bio_nvme_configured(SMD_DEV_TYPE_META)) {
		/* No data blob for rdb */
		if (flags & BIO_MC_FL_RDB)
			return 0;

		/* Query SMD to see if data blob exists */
		rc = smd_pool_get_blob(pool_id, xs_ctxt->bxc_tgt_id, SMD_DEV_TYPE_DATA,
				       &data_blobid);
		if (rc == -DER_NONEXIST) {
			return 0;
		} else if (rc) {
			D_ERROR("Qeury data blob for pool "DF_UUID" tgt:%u failed. "DF_RC"\n",
				DP_UUID(pool_id), xs_ctxt->bxc_tgt_id, DP_RC(rc));
			return rc;
		}

		D_ASSERT(data_blobid != SPDK_BLOBID_INVALID);
		rc = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_DATA, data_blobid, flags);
		if (rc) {
			D_ERROR("Delete data blob "DF_U64" failed. "DF_RC"\n",
				data_blobid, DP_RC(rc));
			return rc;
		}
		return 0;
	}

	rc = bio_mc_open(xs_ctxt, pool_id, flags, &mc);
	if (rc) {
		D_ERROR("Failed to open meta context for "DF_UUID". "DF_RC"\n",
			DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}

	D_ASSERT(mc != NULL);
	meta_blobid = mc->mc_meta_hdr.mh_meta_blobid;
	D_ASSERT(meta_blobid != SPDK_BLOBID_INVALID);
	wal_blobid = mc->mc_meta_hdr.mh_wal_blobid;
	data_blobid = mc->mc_meta_hdr.mh_data_blobid;

	rc = bio_mc_close(mc);
	if (rc) {
		D_ERROR("Failed to close meta context for "DF_UUID". "DF_RC"\n",
			DP_UUID(pool_id), DP_RC(rc));
		return rc;
	}

	if (data_blobid != SPDK_BLOBID_INVALID) {
		rc = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_DATA, data_blobid, flags);
		if (rc) {
			D_ERROR("Failed to delete data blob "DF_U64". "DF_RC"\n",
				data_blobid, DP_RC(rc));
			return rc;
		}
	}

	if (wal_blobid != SPDK_BLOBID_INVALID) {
		rc = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_WAL, wal_blobid, flags);
		if (rc) {
			D_ERROR("Failed to delete WAL blob "DF_U64". "DF_RC"\n",
				wal_blobid, DP_RC(rc));
			return rc;
		}
	}

	rc = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_META, meta_blobid, flags);
	if (rc)
		D_ERROR("Failed to delete meta blob "DF_U64". "DF_RC"\n", wal_blobid, DP_RC(rc));

	return rc;
}

static int
bio_blob_create(uuid_t uuid, struct bio_xs_context *xs_ctxt, uint64_t blob_sz,
		enum smd_dev_type st, enum bio_mc_flags flags, spdk_blob_id *blob_id)
{
	struct blob_msg_arg		 bma = { 0 };
	struct blob_cp_arg		*ba = &bma.bma_cp_arg;
	struct bio_xs_blobstore		*bxb;
	struct bio_blobstore		*bbs;
	uint64_t			 cluster_sz;
	spdk_blob_id			 blob_id1;
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
	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		if (flags & BIO_MC_FL_RDB)
			rc = smd_rdb_get_blob(uuid, xs_ctxt->bxc_tgt_id, st, &blob_id1);
		else
			rc = smd_pool_get_blob(uuid, xs_ctxt->bxc_tgt_id, st, &blob_id1);
	} else {
		rc = smd_pool_get_blob(uuid, xs_ctxt->bxc_tgt_id, st, &blob_id1);
	}

	if (rc == 0) {
		D_ERROR("Duplicated blob for xs:%p pool:"DF_UUID"\n", xs_ctxt, DP_UUID(uuid));
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
		if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
			if (flags & BIO_MC_FL_RDB)
				rc = smd_rdb_add_tgt(uuid, xs_ctxt->bxc_tgt_id, ba->bca_id, st,
						     blob_sz);
			else
				rc = smd_pool_add_tgt(uuid, xs_ctxt->bxc_tgt_id, ba->bca_id, st,
						      blob_sz);
		} else {
			rc = smd_pool_add_tgt(uuid, xs_ctxt->bxc_tgt_id, ba->bca_id, st, blob_sz);
		}

		if (rc != 0) {
			D_ERROR("Failed to assign pool blob:"DF_U64" to pool: "
				""DF_UUID":%d. %d\n", ba->bca_id, DP_UUID(uuid),
				xs_ctxt->bxc_tgt_id, rc);
			/* Delete newly created blob */
			if (bio_blob_delete(uuid, xs_ctxt, st, ba->bca_id, flags))
				D_ERROR("Unable to delete newly created blobID "
					""DF_U64" for xs:%p pool:"DF_UUID"\n",
					ba->bca_id, xs_ctxt, DP_UUID(uuid));
		} else {
			D_DEBUG(DB_MGMT, "Successfully assign blob:"DF_U64" "
				"to pool:"DF_UUID":%d\n", ba->bca_id,
				DP_UUID(uuid), xs_ctxt->bxc_tgt_id);
			*blob_id = ba->bca_id;
		}
	}

	bio_bs_unhold(bbs);
	blob_cp_arg_fini(ba);
	return rc;
}

static int
__bio_ioctxt_open(struct bio_io_context **pctxt, struct bio_xs_context *xs_ctxt,
		  uuid_t uuid, enum bio_mc_flags flags, enum smd_dev_type st,
		  spdk_blob_id open_blobid)
{
	struct bio_io_context	*ctxt;
	int			 rc;
	struct bio_xs_blobstore	*bxb;

	D_ASSERT(xs_ctxt != NULL);
	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ctxt->bic_link);
	ctxt->bic_xs_ctxt = xs_ctxt;
	uuid_copy(ctxt->bic_pool_id, uuid);

	bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);
	D_ASSERT(bxb != NULL);
	rc = bio_bs_hold(bxb->bxb_blobstore);
	if (rc) {
		D_FREE(ctxt);
		return rc;
	}

	ctxt->bic_xs_blobstore = bxb;
	rc = bio_blob_open(ctxt, false, flags, st, open_blobid);
	if (rc) {
		D_FREE(ctxt);
	} else {
		d_list_add_tail(&ctxt->bic_link, &bxb->bxb_io_ctxts);
		*pctxt = ctxt;
	}

	bio_bs_unhold(bxb->bxb_blobstore);
	return rc;
}

/*
 * Calculate a reasonable WAL size based on following assumptions:
 * - Single target update IOPS can be up to 65k;
 * - Each TX consumes 2 WAL blocks in average;
 * - Checkpointing interval is 5 seconds, and the WAL should have at least
 *   half free space before next checkpoint;
 */
uint64_t
default_wal_sz(uint64_t meta_sz)
{
	uint64_t wal_sz = (6ULL << 30);	/* 6GB */

	/* The WAL size could be larger than meta size for tiny pool */
	if ((meta_sz * 2) <= wal_sz)
		return meta_sz * 2;

	return wal_sz;
}

int bio_mc_create(struct bio_xs_context *xs_ctxt, uuid_t pool_id, uint64_t meta_sz,
		  uint64_t wal_sz, uint64_t data_sz, enum bio_mc_flags flags)
{
	int			 rc = 0, rc1;
	spdk_blob_id		 data_blobid = SPDK_BLOBID_INVALID;
	spdk_blob_id		 wal_blobid = SPDK_BLOBID_INVALID;
	spdk_blob_id		 meta_blobid = SPDK_BLOBID_INVALID;
	struct bio_meta_context *mc = NULL;
	struct meta_fmt_info	*fi = NULL;
	struct bio_xs_blobstore *bxb;

	D_ASSERT(xs_ctxt != NULL);
	if (data_sz > 0 && bio_nvme_configured(SMD_DEV_TYPE_DATA)) {
		D_ASSERT(!(flags & BIO_MC_FL_RDB));
		rc = bio_blob_create(pool_id, xs_ctxt, data_sz, SMD_DEV_TYPE_DATA, flags,
				     &data_blobid);
		if (rc)
			return rc;
	}

	if (!bio_nvme_configured(SMD_DEV_TYPE_META))
		return 0;

	D_ASSERT(meta_sz > 0);
	if (meta_sz < default_cluster_sz()) {
		D_ERROR("Meta blob size("DF_U64") is less than minimal size(%u)\n",
			meta_sz, default_cluster_sz());
		rc = -DER_INVAL;
		goto delete_data;
	}

	rc = bio_blob_create(pool_id, xs_ctxt, meta_sz, SMD_DEV_TYPE_META, flags, &meta_blobid);
	if (rc)
		goto delete_data;

	/**
	 * XXX DAOS-12750: At this time the WAL size can not be manually defined and thus wal_sz is
	 * always equal to zero.  However, if such feature is added, then the computation of the
	 * wal_sz in the function bio_get_dev_state_internal() (located in file bio/bio_monitor.c)
	 * should be updated accordingly.
	 */
	if (wal_sz == 0 || wal_sz < default_cluster_sz())
		wal_sz = default_wal_sz(meta_sz);

	rc = bio_blob_create(pool_id, xs_ctxt, wal_sz, SMD_DEV_TYPE_WAL, flags, &wal_blobid);
	if (rc)
		goto delete_meta;

	D_ALLOC_PTR(mc);
	if (mc == NULL) {
		rc = -DER_NOMEM;
		goto delete_wal;
	}
	D_ALLOC_PTR(fi);
	if (fi == NULL) {
		rc = -DER_NOMEM;
		goto delete_wal;
	}

	D_ASSERT(meta_blobid != SPDK_BLOBID_INVALID);
	rc = __bio_ioctxt_open(&mc->mc_meta, xs_ctxt, pool_id, flags, SMD_DEV_TYPE_META,
			       meta_blobid);
	if (rc)
		goto delete_wal;

	D_ASSERT(wal_blobid != SPDK_BLOBID_INVALID);
	rc = __bio_ioctxt_open(&mc->mc_wal, xs_ctxt, pool_id, flags, SMD_DEV_TYPE_WAL,
			       wal_blobid);
	if (rc)
		goto close_meta;

	/* fill meta_fmt_info */
	uuid_copy(fi->fi_pool_id, pool_id);
	bxb = bio_xs_context2xs_blobstore(xs_ctxt, SMD_DEV_TYPE_META);
	uuid_copy(fi->fi_meta_devid, bxb->bxb_blobstore->bb_dev->bb_uuid);
	bxb = bio_xs_context2xs_blobstore(xs_ctxt, SMD_DEV_TYPE_WAL);
	uuid_copy(fi->fi_wal_devid, bxb->bxb_blobstore->bb_dev->bb_uuid);
	bxb = bio_xs_context2xs_blobstore(xs_ctxt, SMD_DEV_TYPE_DATA);
	/* No data blobstore is possible */
	if (bxb != NULL)
		uuid_copy(fi->fi_data_devid, bxb->bxb_blobstore->bb_dev->bb_uuid);
	else
		uuid_clear(fi->fi_data_devid);
	fi->fi_meta_blobid = meta_blobid;
	fi->fi_wal_blobid = wal_blobid;
	fi->fi_data_blobid = data_blobid;
	fi->fi_meta_size = meta_sz;
	fi->fi_wal_size = wal_sz;
	fi->fi_data_size = data_sz;
	fi->fi_vos_id = xs_ctxt->bxc_tgt_id;

	rc = meta_format(mc, fi, true);
	if (rc)
		D_ERROR("Unable to format newly created blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(pool_id));

	rc1 = bio_ioctxt_close(mc->mc_wal);
	if (rc == 0)
		rc = rc1;

close_meta:
	rc1 = bio_ioctxt_close(mc->mc_meta);
	if (rc == 0)
		rc = rc1;
delete_wal:
	D_FREE(mc);
	D_FREE(fi);
	if (rc && wal_blobid != SPDK_BLOBID_INVALID) {
		rc1 = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_WAL, wal_blobid, flags);
		if (rc1)
			D_ERROR("Unable to delete WAL blob for xs:%p pool:"DF_UUID"\n",
				xs_ctxt, DP_UUID(pool_id));
	}
delete_meta:
	if (rc && meta_blobid != SPDK_BLOBID_INVALID) {
		rc1 = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_META, meta_blobid, flags);
		if (rc1)
			D_ERROR("Unable to delete meta blob for xs:%p pool:"DF_UUID"\n",
				xs_ctxt, DP_UUID(pool_id));
	}
delete_data:
	if (rc && data_blobid != SPDK_BLOBID_INVALID) {
		rc1 = bio_blob_delete(pool_id, xs_ctxt, SMD_DEV_TYPE_DATA, data_blobid, flags);
		if (rc1)
			D_ERROR("Unable to delete data blob for xs:%p pool:"DF_UUID"\n",
				xs_ctxt, DP_UUID(pool_id));
	}
	return rc;
}

int
bio_blob_open(struct bio_io_context *ctxt, bool async, enum bio_mc_flags flags,
	      enum smd_dev_type st, spdk_blob_id open_blobid)
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

	bma = blob_msg_arg_alloc();
	if (bma == NULL)
		return -DER_NOMEM;
	ba = &bma->bma_cp_arg;

	if (open_blobid == SPDK_BLOBID_INVALID) {
		/*
		 * Query per-server metadata to get blobID for this pool:target
		 */
		if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
			if (flags & BIO_MC_FL_RDB)
				rc = smd_rdb_get_blob(ctxt->bic_pool_id, xs_ctxt->bxc_tgt_id,
						      st, &blob_id);
			else
				rc = smd_pool_get_blob(ctxt->bic_pool_id, xs_ctxt->bxc_tgt_id,
						       st, &blob_id);
		} else {
			rc = smd_pool_get_blob(ctxt->bic_pool_id, xs_ctxt->bxc_tgt_id, st,
					       &blob_id);
		}

		if (rc != 0) {
			D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID", tgt:%d\n",
				xs_ctxt, DP_UUID(ctxt->bic_pool_id), xs_ctxt->bxc_tgt_id);
			rc = -DER_NONEXIST;
			goto out_free;
		}
	} else {
		blob_id = open_blobid;
	}

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

out_free:
	blob_msg_arg_free(bma);
	return rc;
}

int
bio_ioctxt_open(struct bio_io_context **pctxt, struct bio_xs_context *xs_ctxt,
		uuid_t uuid, bool dummy)
{
	struct bio_io_context	*ctxt;

	if (dummy) {
		D_ALLOC_PTR(ctxt);
		if (ctxt == NULL)
			return -DER_NOMEM;

		ctxt->bic_dummy = true;
		D_INIT_LIST_HEAD(&ctxt->bic_link);
		ctxt->bic_xs_ctxt = xs_ctxt;
		uuid_copy(ctxt->bic_pool_id, uuid);
		*pctxt = ctxt;

		return 0;
	}

	return __bio_ioctxt_open(pctxt, xs_ctxt, uuid, 0, SMD_DEV_TYPE_DATA, SPDK_BLOBID_INVALID);
}

int bio_mc_open(struct bio_xs_context *xs_ctxt, uuid_t pool_id,
		enum bio_mc_flags flags, struct bio_meta_context **mc)
{
	struct bio_meta_context	*bio_mc;
	int			 rc, rc1;
	spdk_blob_id		 data_blobid = SPDK_BLOBID_INVALID;

	D_ASSERT(xs_ctxt != NULL);

	*mc = NULL;
	if (!bio_nvme_configured(SMD_DEV_TYPE_META)) {
		/* No data blob for RDB */
		if (flags & BIO_MC_FL_RDB)
			return 0;

		/* Query SMD to see if data blob exists */
		rc = smd_pool_get_blob(pool_id, xs_ctxt->bxc_tgt_id, SMD_DEV_TYPE_DATA,
				       &data_blobid);
		if (rc == -DER_NONEXIST) {
			D_ASSERT(data_blobid == SPDK_BLOBID_INVALID);
			return 0;
		} else if (rc) {
			D_ERROR("Qeury data blob for pool "DF_UUID" tgt:%u failed. "DF_RC"\n",
				DP_UUID(pool_id), xs_ctxt->bxc_tgt_id, DP_RC(rc));
			return rc;
		}

		D_ASSERT(data_blobid != SPDK_BLOBID_INVALID);
		D_ALLOC_PTR(bio_mc);
		if (bio_mc == NULL)
			return -DER_NOMEM;

		rc = __bio_ioctxt_open(&bio_mc->mc_data, xs_ctxt, pool_id, flags,
				       SMD_DEV_TYPE_DATA, data_blobid);
		if (rc) {
			D_FREE(bio_mc);
			return rc;
		}
		*mc = bio_mc;
		return 0;
	}

	D_ALLOC_PTR(bio_mc);
	if (bio_mc == NULL)
		return -DER_NOMEM;

	rc = __bio_ioctxt_open(&bio_mc->mc_meta, xs_ctxt, pool_id, flags, SMD_DEV_TYPE_META,
			       SPDK_BLOBID_INVALID);
	if (rc)
		goto free_mem;

	rc = meta_open(bio_mc);
	if (rc)
		goto close_meta_ioctxt;


	D_ASSERT(bio_mc->mc_meta_hdr.mh_wal_blobid != SPDK_BLOBID_INVALID);
	rc = __bio_ioctxt_open(&bio_mc->mc_wal, xs_ctxt, pool_id, flags, SMD_DEV_TYPE_WAL,
			       bio_mc->mc_meta_hdr.mh_wal_blobid);
	if (rc)
		goto close_meta;

	rc = wal_open(bio_mc);
	if (rc)
		goto close_wal_ioctxt;

	data_blobid = bio_mc->mc_meta_hdr.mh_data_blobid;
	if (data_blobid != SPDK_BLOBID_INVALID) {
		D_ASSERT(!(flags & BIO_MC_FL_RDB));
		rc = __bio_ioctxt_open(&bio_mc->mc_data, xs_ctxt, pool_id, flags,
				       SMD_DEV_TYPE_DATA, data_blobid);
		if (rc)
			goto close_wal;
	}

	*mc = bio_mc;
	return 0;

close_wal:
	wal_close(bio_mc);
close_wal_ioctxt:
	rc1 = bio_ioctxt_close(bio_mc->mc_wal);
	if (rc1)
		D_ERROR("Failed to close wal ioctxt. %d\n", rc1);
close_meta:
	meta_close(bio_mc);
close_meta_ioctxt:
	rc1 = bio_ioctxt_close(bio_mc->mc_meta);
	if (rc1)
		D_ERROR("Failed to close meta ioctxt. %d\n", rc1);
free_mem:
	D_FREE(bio_mc);

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
		D_ERROR("There are %u in-flight blob IOs\n",
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
bio_ioctxt_close(struct bio_io_context *ctxt)
{
	int	rc;

	if (ctxt->bic_dummy) {
		D_ASSERT(d_list_empty(&ctxt->bic_link));
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

int bio_mc_close(struct bio_meta_context *bio_mc)
{
	int	rc = 0;
	int	rc1;

	if (bio_mc->mc_data) {
		rc1 = bio_ioctxt_close(bio_mc->mc_data);
		if (rc1)
			rc = rc1;
	}
	if (bio_mc->mc_wal) {
		wal_close(bio_mc);
		rc1 = bio_ioctxt_close(bio_mc->mc_wal);
		if (rc1 && !rc)
			rc = rc1;
	}
	if (bio_mc->mc_meta) {
		meta_close(bio_mc);
		rc1 = bio_ioctxt_close(bio_mc->mc_meta);
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
	 * TODO: track in-flight DMA extents and check the tracked extents
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
	struct bio_xs_blobstore	*bxb;
	struct bio_bdev		*d_bdev;
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
	rc = smd_pool_get_blob(bio_bh->bbh_pool, bio_bh->bbh_vos_id, SMD_DEV_TYPE_DATA, &blob_id);
	if (rc) {
		D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID"\n",
			ioctxt->bic_xs_ctxt, DP_UUID(bio_bh->bbh_pool));
		return rc;
	}

	bio_bh->bbh_blob_id = blob_id;

	bxb = bio_xs_context2xs_blobstore(ioctxt->bic_xs_ctxt, SMD_DEV_TYPE_DATA);
	D_ASSERT(bxb != NULL);
	d_bdev = bxb->bxb_blobstore->bb_dev;
	D_ASSERT(d_bdev != NULL);
	uuid_copy(bio_bh->bbh_blobstore, d_bdev->bb_uuid);

	/* Create an iov to store blob header structure */
	d_iov_set(&iov, (void *)bio_bh, sizeof(*bio_bh));

	rc = bio_write(ioctxt, addr, &iov);

	return rc;
}

struct bio_io_context *
bio_mc2ioc(struct bio_meta_context *mc, enum smd_dev_type type)
{
	D_ASSERT(mc != NULL);
	switch (type) {
	case SMD_DEV_TYPE_DATA:
		return mc->mc_data;
	case SMD_DEV_TYPE_META:
		return mc->mc_meta;
	case SMD_DEV_TYPE_WAL:
		return mc->mc_wal;
	default:
		D_ASSERTF(0, "Invalid device type:%u\n", type);
		return NULL;
	}
}
