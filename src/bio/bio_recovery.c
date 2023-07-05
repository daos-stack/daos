/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/thread.h>
#include <spdk/blob.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

/*
 * The BIO blobstore (mapped to one NVMe device) owner xstream polls the
 * state periodically, and takes predefined reaction routines on state
 * transition, all the reaction routines must be non-blocking, otherwise
 * the DAOS progress ULT will be blocked, and NVMe device qpair won't be
 * polled.
 */
static struct bio_reaction_ops	*ract_ops;

void bio_register_ract_ops(struct bio_reaction_ops *ops)
{
	ract_ops = ops;
}

/*
 * Return value:	0: Faulty reaction is done;
 *			1: Faulty reaction is in progress;
 *			-ve: Error;
 */
static int
on_faulty(struct bio_blobstore *bbs)
{
	int	tgt_ids[BIO_XS_CNT_MAX];
	int	tgt_cnt, i, rc;

	/* Transit to next state if faulty reaction isn't registered */
	if (ract_ops == NULL || ract_ops->faulty_reaction == NULL)
		return 0;

	/*
	 * It's safe to access xs context array without locking when the
	 * server is neither in start nor shutdown phase.
	 */
	D_ASSERT(is_server_started());
	tgt_cnt = bbs->bb_ref;
	D_ASSERT(tgt_cnt <= BIO_XS_CNT_MAX && tgt_cnt > 0);

	for (i = 0; i < tgt_cnt; i++)
		tgt_ids[i] = bbs->bb_xs_ctxts[i]->bxc_tgt_id;

	rc = ract_ops->faulty_reaction(tgt_ids, tgt_cnt);
	if (rc < 0)
		D_ERROR("Faulty reaction failed. "DF_RC"\n", DP_RC(rc));

	return rc;
}

static void
teardown_blobstore(struct bio_xs_context *xs_ctxt, enum smd_dev_type st)
{
	struct bio_io_context	*ioc;
	int			 opened_blobs = 0;
	struct bio_xs_blobstore	*bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);

	/* This blobstore is torndown */
	if (bxb->bxb_io_channel == NULL)
		return;

	/* Try to close all blobs */
	d_list_for_each_entry(ioc, &bxb->bxb_io_ctxts, bic_link) {
		if (ioc->bic_blob == NULL && ioc->bic_opening == 0)
			continue;

		opened_blobs++;
		if (ioc->bic_closing || ioc->bic_opening)
			continue;

		bio_blob_close(ioc, true);
	}

	if (opened_blobs) {
		D_DEBUG(DB_MGMT, "blobstore:%p has %d opened blobs\n",
			bxb->bxb_blobstore, opened_blobs);
		return;
	}

	/* Put the io channel */
	if (bxb->bxb_io_channel != NULL) {
		spdk_bs_free_io_channel(bxb->bxb_io_channel);
		bxb->bxb_io_channel = NULL;
	}
}

static void
teardown_xstream(void *arg)
{
	struct bio_xs_context	*xs_ctxt = arg;

	D_ASSERT(xs_ctxt != NULL);
	if (!is_server_started()) {
		D_INFO("Abort xs teardown on server start/shutdown\n");
		return;
	}

	xs_ctxt->bxc_ready = 0;
	/* only teardown data blobstore for now */
	teardown_blobstore(xs_ctxt, SMD_DEV_TYPE_DATA);
}

static void
unload_bs_cp(void *arg, int rc)
{
	struct bio_blobstore *bbs = arg;

	/* Unload blobstore may fail if the device is hot removed */
	if (rc != 0)
		D_ERROR("Failed to unload blobstore:%p, %d\n",
			bbs, rc);

	/* Stop accessing bbs, it could be freed on shutdown */
	if (!is_server_started()) {
		D_INFO("Abort bs unload on server start/shutdown\n");
		return;
	}

	D_ASSERT(!bbs->bb_loading);
	bbs->bb_unloading = false;
	/* SPDK will free blobstore even if load failed */
	bbs->bb_bs = NULL;
	D_ASSERT(init_thread() != NULL);
	spdk_thread_send_msg(init_thread(), bio_release_bdev,
			     bbs->bb_dev);
}

static inline bool
is_xstream_torndown(struct bio_xs_context *xs_ctxt)
{
	struct bio_xs_blobstore	*bxb;

	/* only check data blobstore for now */
	bxb = bio_xs_context2xs_blobstore(xs_ctxt, SMD_DEV_TYPE_DATA);
	if (bxb->bxb_io_channel != NULL)
		return false;

	return true;
}

/*
 * Return value:	0:  Blobstore is torn down;
 *			>0: Blobstore teardown is in progress;
 */
static int
on_teardown(struct bio_blobstore *bbs)
{
	struct bio_dev_health	*bdh = &bbs->bb_dev_health;
	int			 i, rc = 0;

	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_holdings != 0) {
		D_DEBUG(DB_MGMT, "Blobstore %p is inuse:%d, retry later.\n",
			bbs, bbs->bb_holdings);
		ABT_mutex_unlock(bbs->bb_mutex);
		return 1;
	}
	ABT_mutex_unlock(bbs->bb_mutex);

	/*
	 * It's safe to access xs context array without locking when the
	 * server is neither in start nor shutdown phase.
	 */
	D_ASSERT(is_server_started());
	for (i = 0; i < bbs->bb_ref; i++) {
		struct bio_xs_context	*xs_ctxt = bbs->bb_xs_ctxts[i];

		/* This xstream is torndown */
		if (is_xstream_torndown(xs_ctxt))
			continue;

		D_ASSERT(xs_ctxt->bxc_thread != NULL);
		spdk_thread_send_msg(xs_ctxt->bxc_thread, teardown_xstream,
				     xs_ctxt);
		rc += 1;
	}

	if (rc)
		return rc;

	/* Put io channel for health monitor */
	if (bdh->bdh_io_channel != NULL) {
		spdk_put_io_channel(bdh->bdh_io_channel);
		bdh->bdh_io_channel = NULL;
	}

	/* Close open desc for health monitor */
	if (bdh->bdh_desc != NULL) {
		spdk_bdev_close(bdh->bdh_desc);
		bdh->bdh_desc = NULL;
	}

	/*
	 * Unload the blobstore. The blobstore could be still in loading from
	 * the SETUP stage.
	 */
	D_ASSERT(bbs->bb_holdings == 0);
	if (bbs->bb_bs == NULL && !bbs->bb_loading)
		return 0;

	if (bbs->bb_loading || bbs->bb_unloading) {
		D_DEBUG(DB_MGMT, "Blobstore %p is in %s\n", bbs,
			bbs->bb_loading ? "loading" : "unloading");
		return 1;
	}

	bbs->bb_unloading = true;
	spdk_bs_unload(bbs->bb_bs, unload_bs_cp, bbs);
	return 1;
}

static void
setup_blobstore(struct bio_xs_context *xs_ctxt, enum smd_dev_type st,
		int *closed_blobs)
{
	struct bio_io_context	*ioc;
	struct bio_blobstore	*bbs;
	struct bio_xs_blobstore	*bxb;

	bxb = bio_xs_context2xs_blobstore(xs_ctxt, st);
	D_ASSERT(bxb != NULL);

	bbs = bxb->bxb_blobstore;
	if (bbs == NULL)
		return;

	D_ASSERT(bbs->bb_bs != NULL);
	/*
	 * Setup the blobstore io channel. It's must be done as the first step
	 * of xstream setup, since blobstore teardown checks io channel to tell
	 * if everything is torndown for the blobstore.
	 */
	if (bxb->bxb_io_channel == NULL) {
		bxb->bxb_io_channel = spdk_bs_alloc_io_channel(bbs->bb_bs);
		if (bxb->bxb_io_channel == NULL) {
			D_ERROR("Failed to create io channel for %p\n", bbs);
			return;
		}
	}

	/* Try to open all blobs */
	d_list_for_each_entry(ioc, &bxb->bxb_io_ctxts, bic_link) {
		if (ioc->bic_blob != NULL && !ioc->bic_closing)
			continue;

		*closed_blobs = *closed_blobs + 1;
		if (ioc->bic_opening || ioc->bic_closing)
			continue;

		/* fix sysdb */
		bio_blob_open(ioc, true, 0, st, SPDK_BLOBID_INVALID);
	}

	if (*closed_blobs)
		D_DEBUG(DB_MGMT, "blobstore:%p has %d closed blobs\n",
			bbs, *closed_blobs);
	return;

}

static void
setup_xstream(void *arg)
{
	struct bio_xs_context	*xs_ctxt = arg;
	int			 closed_blobs;

	D_ASSERT(xs_ctxt != NULL);
	if (!is_server_started()) {
		D_INFO("Abort xs setup on server start/shutdown\n");
		return;
	}

	/* only support data blobstore for now */
	closed_blobs = 0;
	setup_blobstore(xs_ctxt, SMD_DEV_TYPE_DATA, &closed_blobs);
	/*
	 * It doesn't mean setup failed when there is any closed blob,
	 * it means some blob is still busy and we can't move forward to
	 * next state yet, we'll retry setup_xstream() later.
	 */
	if (closed_blobs > 0)
		return;

	xs_ctxt->bxc_ready = 1;
	return;

}

static void
load_bs_cp(void *arg, struct spdk_blob_store *bs, int rc)
{
	struct bio_blobstore *bbs = arg;

	if (rc != 0)
		D_ERROR("Failed to load blobstore:%p, %d\n",
			bbs, rc);

	/* Stop accessing bbs since it could be freed on shutdown */
	if (!is_server_started()) {
		D_INFO("Abort bs load on server start/shutdown\n");
		return;
	}

	D_ASSERT(!bbs->bb_unloading);
	D_ASSERT(bbs->bb_bs == NULL);
	bbs->bb_loading = false;
	if (rc == 0)
		bbs->bb_bs = bs;
}

/*
 * Return value:	0:  Blobstore loaded, all blobs opened;
 *			>0: Blobstore or blobs are in loading/opening;
 */
static int
on_setup(struct bio_blobstore *bbs)
{
	struct bio_bdev		*d_bdev = bbs->bb_dev;
	struct bio_dev_health	*bdh = &bbs->bb_dev_health;
	int			 i, rc = 0;

	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_holdings != 0) {
		D_DEBUG(DB_MGMT, "Blobstore %p is inuse:%d, retry later.\n",
			bbs, bbs->bb_holdings);
		ABT_mutex_unlock(bbs->bb_mutex);
		return 1;
	}
	ABT_mutex_unlock(bbs->bb_mutex);

	D_ASSERT(!bbs->bb_unloading);
	/* Blobstore is already loaded */
	if (bbs->bb_bs != NULL)
		goto bs_loaded;

	if (bbs->bb_loading) {
		D_DEBUG(DB_MGMT, "Blobstore %p is in loading\n", bbs);
		return 1;
	}

	D_ASSERT(d_bdev != NULL);
	D_ASSERT(d_bdev->bb_name != NULL);
	D_ASSERT(d_bdev->bb_uuid != NULL);

	bbs->bb_loading = true;
	load_blobstore(NULL, d_bdev->bb_name, &d_bdev->bb_uuid, false, true,
		       load_bs_cp, bbs);
	return 1;

bs_loaded:
	/* Acquire open desc for health monitor */
	if (bdh->bdh_desc == NULL) {
		rc = spdk_bdev_open_ext(d_bdev->bb_name, true,
					bio_bdev_event_cb, NULL,
					&bdh->bdh_desc);
		if (rc != 0) {
			D_ERROR("Failed to open bdev %s, for %p, %d\n",
				d_bdev->bb_name, bbs, rc);
			return 1;
		}
		D_ASSERT(bdh->bdh_desc != NULL);
	}

	/* Get io channel for health monitor */
	if (bdh->bdh_io_channel == NULL) {
		bdh->bdh_io_channel = spdk_bdev_get_io_channel(bdh->bdh_desc);
		if (bdh->bdh_io_channel == NULL) {
			D_ERROR("Failed to get health channel for %p\n", bbs);
			return 1;
		}
	}

	/*
	 * It's safe to access xs context array without locking when the
	 * server is neither in start nor shutdown phase.
	 */
	D_ASSERT(is_server_started());
	for (i = 0; i < bbs->bb_ref; i++) {
		struct bio_xs_context	*xs_ctxt = bbs->bb_xs_ctxts[i];

		/* Setup for the xsteam is done */
		if (xs_ctxt->bxc_ready)
			continue;

		D_ASSERT(xs_ctxt->bxc_thread != NULL);
		spdk_thread_send_msg(xs_ctxt->bxc_thread, setup_xstream,
				     xs_ctxt);
		rc += 1;
	}

	return rc;
}

int
bio_bs_state_set(struct bio_blobstore *bbs, enum bio_bs_state new_state)
{
	int	rc = 0;

	D_ASSERT(bbs != NULL);

	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_state == new_state) {
		ABT_mutex_unlock(bbs->bb_mutex);
		return 0;
	}

	switch (new_state) {
	case BIO_BS_STATE_NORMAL:
		if (bbs->bb_state != BIO_BS_STATE_SETUP)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_FAULTY:
		if (bbs->bb_state != BIO_BS_STATE_NORMAL &&
		    bbs->bb_state != BIO_BS_STATE_SETUP)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_TEARDOWN:
		if (bbs->bb_state != BIO_BS_STATE_NORMAL &&
		    bbs->bb_state != BIO_BS_STATE_FAULTY &&
		    bbs->bb_state != BIO_BS_STATE_SETUP)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_OUT:
		if (bbs->bb_state != BIO_BS_STATE_TEARDOWN)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_SETUP:
		if (bbs->bb_state != BIO_BS_STATE_OUT)
			rc = -DER_INVAL;
		break;
	default:
		rc = -DER_INVAL;
		D_ASSERTF(0, "Invalid blobstore state: %u (%s)\n",
			  new_state, bio_state_enum_to_str(new_state));
		break;
	}

	if (rc) {
		D_ERROR("Blobstore state transition error! tgt: %d, %s -> %s\n",
			bbs->bb_owner_xs->bxc_tgt_id,
			bio_state_enum_to_str(bbs->bb_state),
			bio_state_enum_to_str(new_state));
	} else {
		D_DEBUG(DB_MGMT, "Blobstore state transitioned. "
			"tgt: %d, %s -> %s\n",
			bbs->bb_owner_xs->bxc_tgt_id,
			bio_state_enum_to_str(bbs->bb_state),
			bio_state_enum_to_str(new_state));
		/* Print a console message */
		D_PRINT("Blobstore state transitioned. tgt: %d, %s -> %s\n",
			bbs->bb_owner_xs->bxc_tgt_id,
			bio_state_enum_to_str(bbs->bb_state),
			bio_state_enum_to_str(new_state));

		bbs->bb_state = new_state;

		if (new_state == BIO_BS_STATE_FAULTY) {
			struct spdk_bs_type	bstype;
			uuid_t			dev_id;

			bstype = spdk_bs_get_bstype(bbs->bb_bs);
			memcpy(dev_id, bstype.bstype, sizeof(dev_id));

			rc = smd_dev_set_state(dev_id, SMD_DEV_FAULTY);
			if (rc)
				D_ERROR("Set device state failed. "DF_RC"\n",
					DP_RC(rc));
		}
	}
	ABT_mutex_unlock(bbs->bb_mutex);

	return rc;
}

static void
on_normal(struct bio_blobstore *bbs)
{
	struct bio_bdev	*bdev = bbs->bb_dev;
	int		 tgt_ids[BIO_XS_CNT_MAX];
	int		 tgt_cnt, i, rc;

	/*
	 * Trigger auto reint only when faulty is replaced by new hot
	 * plugged device. See comments in bio_replace_dev().
	 */
	D_ASSERT(bdev != NULL);
	if (!bdev->bb_trigger_reint)
		return;

	/* don't trigger reint if reint reaction isn't registered */
	if (ract_ops == NULL || ract_ops->reint_reaction == NULL)
		return;

	/*
	 * It's safe to access xs context array without locking when the
	 * server is neither in start nor shutdown phase.
	 */
	D_ASSERT(is_server_started());
	tgt_cnt = bbs->bb_ref;
	D_ASSERT(tgt_cnt <= BIO_XS_CNT_MAX && tgt_cnt > 0);

	for (i = 0; i < tgt_cnt; i++)
		tgt_ids[i] = bbs->bb_xs_ctxts[i]->bxc_tgt_id;

	rc = ract_ops->reint_reaction(tgt_ids, tgt_cnt);
	if (rc < 0)
		D_ERROR("Reint reaction failed. "DF_RC"\n", DP_RC(rc));
	else if (rc > 0)
		D_DEBUG(DB_MGMT, "Reint reaction is in-progress.");
	else
		bdev->bb_trigger_reint = false;
}

int
bio_bs_state_transit(struct bio_blobstore *bbs)
{
	int	rc;

	D_ASSERT(bbs != NULL);

	switch (bbs->bb_state) {
	case BIO_BS_STATE_NORMAL:
		on_normal(bbs);
		/* fallthrough */
	case BIO_BS_STATE_OUT:
		rc = 0;
		break;
	case BIO_BS_STATE_FAULTY:
		rc = on_faulty(bbs);
		if (rc == 0)
			rc = bio_bs_state_set(bbs, BIO_BS_STATE_TEARDOWN);
		break;
	case BIO_BS_STATE_TEARDOWN:
		rc = on_teardown(bbs);
		if (rc == 0)
			rc = bio_bs_state_set(bbs, BIO_BS_STATE_OUT);
		break;
	case BIO_BS_STATE_SETUP:
		rc = on_setup(bbs);
		if (rc == 0) {
			rc = bio_bs_state_set(bbs, BIO_BS_STATE_NORMAL);
			if (rc == 0)
				on_normal(bbs);
		}
		break;
	default:
		rc = -DER_INVAL;
		D_ASSERTF(0, "Invalid blobstore state:%u (%s)\n",
			 bbs->bb_state, bio_state_enum_to_str(bbs->bb_state));
		break;
	}

	return (rc < 0) ? rc : 0;
}

/*
 * MEDIA ERROR event.
 * Store BIO I/O error in in-memory device state. Called from device owner
 * xstream only.
 */
void
bio_media_error(void *msg_arg)
{
	struct media_error_msg		*mem = msg_arg;
	struct bio_dev_health		*bdh;
	struct nvme_stats		*dev_state;
	int				 rc;

	bdh = &mem->mem_bs->bb_dev_health;
	dev_state = &bdh->bdh_health_state;

	switch (mem->mem_err_type) {
	case MET_UNMAP:
		/* Update unmap error counter */
		dev_state->bio_unmap_errs++;
		d_tm_inc_counter(bdh->bdh_unmap_errs, 1);
		D_ERROR("Unmap error logged from tgt_id:%d\n", mem->mem_tgt_id);
		break;
	case MET_WRITE:
		/* Update write I/O error counter */
		dev_state->bio_write_errs++;
		d_tm_inc_counter(bdh->bdh_write_errs, 1);
		D_ERROR("Write error logged from tgt_id:%d\n", mem->mem_tgt_id);
		break;
	case MET_READ:
		/* Update read I/O error counter */
		dev_state->bio_read_errs++;
		d_tm_inc_counter(bdh->bdh_read_errs, 1);
		D_ERROR("Read error logged from tgt_id:%d\n", mem->mem_tgt_id);
		break;
	case MET_CSUM:
		/* Update CSUM error counter */
		dev_state->checksum_errs++;
		d_tm_inc_counter(bdh->bdh_checksum_errs, 1);
		D_ERROR("CSUM error logged from tgt_id:%d\n", mem->mem_tgt_id);
		break;
	}

	auto_faulty_detect(mem->mem_bs);

	if (ract_ops == NULL || ract_ops->ioerr_reaction == NULL)
		goto out;
	/*
	 * Notify admin through Control Plane of BIO error callback.
	 * TODO: CSUM errors not currently supported by Control Plane.
	 */
	if (mem->mem_err_type != MET_CSUM) {
		rc = ract_ops->ioerr_reaction(mem->mem_err_type,
					      mem->mem_tgt_id);
		if (rc < 0)
			D_ERROR("Blobstore I/O error notification error. %d\n",
				rc);
	}

out:
	D_FREE(mem);
}
