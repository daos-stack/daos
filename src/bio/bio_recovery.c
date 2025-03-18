/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	else if (rc == 0)
		bbs->bb_faulty_done = 1;

	return rc;
}

void
trigger_faulty_reaction(struct bio_blobstore *bbs)
{
	D_ASSERT(!bbs->bb_faulty_done);
	on_faulty(bbs);
}

static void
teardown_xs_bs(void *arg)
{
	struct bio_io_context	*ioc;
	int			 opened_blobs = 0;
	struct bio_xs_blobstore	*bxb = arg;

	D_ASSERT(bxb != NULL);
	if (!is_server_started()) {
		D_INFO("Abort xs teardown on server start/shutdown\n");
		return;
	}

	/* This per-xstream blobstore is torndown */
	if (bxb->bxb_io_channel == NULL)
		return;

	/* When a normal device is unplugged, the opened blobs need be closed here */
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

static struct bio_xs_blobstore *
bs2bxb(struct bio_blobstore *bbs, struct bio_xs_context *xs_ctxt)
{
	struct bio_xs_blobstore	*bxb;
	int			 st;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = xs_ctxt->bxc_xs_blobstores[st];
		if (bxb && bxb->bxb_blobstore && (bxb->bxb_blobstore == bbs))
			return bxb;
	}

	return NULL;
}

static inline int
pause_health_monitor(struct bio_dev_health *bdh)
{
	bdh->bdh_stopping = 1;
	if (bdh->bdh_inflights > 0)
		return 1;

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

	return 0;
}

static inline int
resume_health_monitor(struct bio_bdev *d_bdev, struct bio_dev_health *bdh)
{
	int rc;

	/* Acquire open desc for health monitor */
	if (bdh->bdh_desc == NULL) {
		rc = spdk_bdev_open_ext(d_bdev->bb_name, true, bio_bdev_event_cb, NULL,
					&bdh->bdh_desc);
		if (rc != 0) {
			D_ERROR("Failed to open bdev %s, rc:%d\n", d_bdev->bb_name, rc);
			return 1;
		}
		D_ASSERT(bdh->bdh_desc != NULL);
	}

	/* Get io channel for health monitor */
	if (bdh->bdh_io_channel == NULL) {
		bdh->bdh_io_channel = spdk_bdev_get_io_channel(bdh->bdh_desc);
		if (bdh->bdh_io_channel == NULL) {
			D_ERROR("Failed to get health channel for bdev %s\n", d_bdev->bb_name);
			return 1;
		}
	}

	bdh->bdh_stopping = 0;
	return 0;
}

/*
 * Return value:	0:  Blobstore is torn down;
 *			>0: Blobstore teardown is in progress;
 */
static int
on_teardown(struct bio_blobstore *bbs)
{
	struct bio_dev_health	*bdh = &bbs->bb_dev_health;
	struct bio_xs_blobstore	*bxb;
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

		bxb = bs2bxb(bbs, xs_ctxt);
		D_ASSERT(bxb != NULL);

		/* This per-xstream blobstore is torndown */
		if (bxb->bxb_io_channel == NULL)
			continue;

		D_ASSERT(xs_ctxt->bxc_thread != NULL);
		bxb->bxb_ready = false;
		spdk_thread_send_msg(xs_ctxt->bxc_thread, teardown_xs_bs, bxb);
		rc += 1;
	}

	if (rc)
		return rc;

	rc = pause_health_monitor(bdh);
	if (rc)
		return rc;

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
setup_xs_bs(void *arg)
{
	struct bio_io_context   *ioc, *tmp;
	struct bio_xs_blobstore	*bxb = arg;
	struct bio_blobstore	*bbs;
	int			 closed_blobs = 0;

	D_ASSERT(bxb != NULL);
	if (!is_server_started()) {
		D_INFO("Abort xs setup on server start/shutdown\n");
		return;
	}

	bbs = bxb->bxb_blobstore;
	D_ASSERT(bbs != NULL);
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

	/* If reint will be tirggered later, blobs will be opened in reint reaction */
	if (bbs->bb_dev->bb_trigger_reint) {
		/*
		 * There could be leftover io contexts if TEARDOWN is performed on an
		 * unplugged device before it's marked as FAULTY.
		 */
		d_list_for_each_entry_safe(ioc, tmp, &bxb->bxb_io_ctxts, bic_link) {
			/* The blob must have been closed on teardown */
			D_ASSERT(ioc->bic_blob == NULL);
			d_list_del_init(&ioc->bic_link);
			D_FREE(ioc);
		}
		goto done;
	}

	/* Open all blobs when reint won't be tirggered */
	d_list_for_each_entry(ioc, &bxb->bxb_io_ctxts, bic_link) {
		if (ioc->bic_blob != NULL && !ioc->bic_closing)
			continue;

		closed_blobs += 1;
		if (ioc->bic_opening || ioc->bic_closing)
			continue;

		D_ASSERT(ioc->bic_blob_id != SPDK_BLOBID_INVALID);
		/* device type and flags will be ignored in bio_blob_open() */
		bio_blob_open(ioc, true, 0, SMD_DEV_TYPE_MAX, ioc->bic_blob_id);
	}

	if (closed_blobs) {
		D_DEBUG(DB_MGMT, "blobstore:%p has %d closed blobs\n",
			bbs, closed_blobs);
		return;
	}
done:
	bxb->bxb_ready = true;
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
	struct bio_xs_blobstore	*bxb;
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
	rc = resume_health_monitor(d_bdev, bdh);
	if (rc)
		return rc;

	/*
	 * It's safe to access xs context array without locking when the
	 * server is neither in start nor shutdown phase.
	 */
	D_ASSERT(is_server_started());
	for (i = 0; i < bbs->bb_ref; i++) {
		struct bio_xs_context	*xs_ctxt = bbs->bb_xs_ctxts[i];

		bxb = bs2bxb(bbs, xs_ctxt);
		D_ASSERT(bxb != NULL);

		/* Setup for the per-xsteam blobstore is done */
		if (bxb->bxb_ready)
			continue;

		D_ASSERT(xs_ctxt->bxc_thread != NULL);
		spdk_thread_send_msg(xs_ctxt->bxc_thread, setup_xs_bs, bxb);
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
			D_ASSERT(bbs->bb_dev != NULL);
			rc = smd_dev_set_state(bbs->bb_dev->bb_uuid, SMD_DEV_FAULTY);
			if (rc)
				D_ERROR("Set device state failed. "DF_RC"\n",
					DP_RC(rc));
		}
	}
	ABT_mutex_unlock(bbs->bb_mutex);

	return rc;
}

int
bio_xsctxt_health_check(struct bio_xs_context *xs_ctxt, bool log_err, bool update)
{
	struct bio_xs_blobstore	*bxb;
	struct media_error_msg	*mem;
	enum smd_dev_type	 st;

	/* sys xstream in pmem mode doesn't have NVMe context */
	if (xs_ctxt == NULL)
		return 0;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = xs_ctxt->bxc_xs_blobstores[st];

		if (!bxb || !bxb->bxb_blobstore)
			continue;

		if (bxb->bxb_blobstore->bb_state != BIO_BS_STATE_NORMAL) {
			if (log_err && bxb->bxb_blobstore->bb_state != BIO_BS_STATE_SETUP) {
				D_ALLOC_PTR(mem);
				if (mem == NULL) {
					D_ERROR("Failed to allocate media error msg.\n");
					return -DER_NVME_IO;
				}

				mem->mem_err_type = update ? MET_WRITE : MET_READ;
				mem->mem_bs = bxb->bxb_blobstore;
				mem->mem_tgt_id = xs_ctxt->bxc_tgt_id;
				spdk_thread_send_msg(owner_thread(mem->mem_bs), bio_media_error, mem);
			}
			return -DER_NVME_IO;
		}
	}

	return 0;
}

static inline bool
is_reint_ready(struct bio_blobstore *bbs)
{
	struct bio_xs_context	*xs_ctxt;
	int			 i;

	for (i = 0; i < bbs->bb_ref; i++) {
		xs_ctxt = bbs->bb_xs_ctxts[i];

		D_ASSERT(xs_ctxt != NULL);
		if (bio_xsctxt_health_check(xs_ctxt, false, false))
			return false;
	}
	return true;
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

	D_ASSERT(is_server_started());
	/*
	 * xstream could be backed by multiple SSDs when roles are assigned to separated devices,
	 * reintegration should only be triggered when all the backed SSDs are in normal state.
	 */
	if (!is_reint_ready(bbs))
		return;

	/*
	 * It's safe to access xs context array without locking when the
	 * server is neither in start nor shutdown phase.
	 */
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
	char				 err_str[DAOS_RAS_STR_FIELD_SIZE];

	bdh = &mem->mem_bs->bb_dev_health;
	dev_state = &bdh->bdh_health_state;

	switch (mem->mem_err_type) {
	case MET_UNMAP:
		/* Update unmap error counter */
		dev_state->bio_unmap_errs++;
		d_tm_inc_counter(bdh->bdh_unmap_errs, 1);
		snprintf(err_str, DAOS_RAS_STR_FIELD_SIZE,
			 "Device: "DF_UUID" unmap error logged from tgt_id:%d\n",
			 DP_UUID(mem->mem_bs->bb_dev->bb_uuid), mem->mem_tgt_id);
		break;
	case MET_WRITE:
		/* Update write I/O error counter */
		dev_state->bio_write_errs++;
		d_tm_inc_counter(bdh->bdh_write_errs, 1);
		snprintf(err_str, DAOS_RAS_STR_FIELD_SIZE,
			 "Device: "DF_UUID" write error logged from tgt_id:%d\n",
			 DP_UUID(mem->mem_bs->bb_dev->bb_uuid), mem->mem_tgt_id);
		break;
	case MET_READ:
		/* Update read I/O error counter */
		dev_state->bio_read_errs++;
		d_tm_inc_counter(bdh->bdh_read_errs, 1);
		snprintf(err_str, DAOS_RAS_STR_FIELD_SIZE,
			 "Device: "DF_UUID" read error logged from tgt_id:%d\n",
			 DP_UUID(mem->mem_bs->bb_dev->bb_uuid), mem->mem_tgt_id);
		break;
	case MET_CSUM:
		/* Update CSUM error counter */
		dev_state->checksum_errs++;
		d_tm_inc_counter(bdh->bdh_checksum_errs, 1);
		snprintf(err_str, DAOS_RAS_STR_FIELD_SIZE,
			 "Device: "DF_UUID" csum error logged from tgt_id:%d\n",
			 DP_UUID(mem->mem_bs->bb_dev->bb_uuid), mem->mem_tgt_id);
		break;
	}

	ras_notify_event(RAS_DEVICE_MEDIA_ERROR, err_str, RAS_TYPE_INFO, RAS_SEV_ERROR,
			 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	auto_faulty_detect(mem->mem_bs);

	D_FREE(mem);
}
