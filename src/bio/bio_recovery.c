/**
 * (C) Copyright 2018-2019 Intel Corporation.
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
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/io_channel.h>
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
struct bio_reaction_ops	*ract_ops;

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

	ABT_mutex_lock(bbs->bb_mutex);
	tgt_cnt = bbs->bb_ref;
	D_ASSERT(tgt_cnt <= BIO_XS_CNT_MAX && tgt_cnt > 0);

	for (i = 0; i < tgt_cnt; i++)
		tgt_ids[i] = bbs->bb_xs_ctxts[i]->bxc_tgt_id;
	ABT_mutex_unlock(bbs->bb_mutex);

	rc = ract_ops->faulty_reaction(tgt_ids, tgt_cnt);
	if (rc < 0)
		D_ERROR("Faulty reaction failed. %d\n", rc);

	return rc;
}

/*
 * Return value:	0: Per-xstream context is torn down;
 *			1: Per-xstream context teardown is in progress;
 */
static int
teardown_xstream(struct bio_xs_context *xs_ctxt)
{
	struct bio_io_context	*ioc;
	int			 opened_blobs = 0;

	D_ASSERT(xs_ctxt != NULL);
	/* Teardown work for this xstream is done */
	if (xs_ctxt->bxc_io_channel == NULL)
		return 0;

	/* Try to close all blobs */
	d_list_for_each_entry(ioc, &xs_ctxt->bxc_io_ctxts, bic_link) {
		D_ASSERT(ioc->bic_opening == 0);

		if (ioc->bic_blob == NULL)
			continue;

		opened_blobs++;
		if (ioc->bic_closing)
			continue;

		bio_blob_close(ioc, true);
	}

	if (opened_blobs)
		return 1;

	/* Put the io channel */
	D_ASSERT(xs_ctxt->bxc_io_channel != NULL);
	spdk_bs_free_io_channel(xs_ctxt->bxc_io_channel);
	xs_ctxt->bxc_io_channel = NULL;

	return 0;
}

static void
unload_bs_cp(void *arg, int rc)
{
	struct bio_blobstore *bbs = arg;

	if (rc != 0)
		D_ERROR("Failed to unload bs:%p, %d\n", bbs, rc);
	else
		bbs->bb_bs = NULL;
}

/*
 * Return value:	0: Blobstore is torn down;
 *			1: Blobstore teardown is in progress;
 */
static int
on_teardown(struct bio_blobstore *bbs)
{
	int	i, rc = 0, ret;

	/*
	 * The blobstore is already closed, transit to next state.
	 * TODO: Need to cleanup bdev when supporting reintegration.
	 */
	if (bbs->bb_bs == NULL)
		return 0;

	ABT_mutex_lock(bbs->bb_mutex);

	if (bbs->bb_holdings != 0) {
		D_DEBUG(DB_MGMT, "Blobstore %p is inuse:%d, retry later.\n",
			bbs, bbs->bb_holdings);
		ABT_mutex_unlock(bbs->bb_mutex);
		return 1;
	}

	/* Hold the lock to prevent other xstreams from exiting */
	for (i = 0; i < bbs->bb_ref; i++) {
		ret = teardown_xstream(bbs->bb_xs_ctxts[i]);
		rc += ret;
	}

	ABT_mutex_unlock(bbs->bb_mutex);

	if (rc == 0) {
		/* Unload the blobstore */
		D_ASSERT(bbs->bb_bs != NULL);
		D_ASSERT(bbs->bb_holdings == 0);
		spdk_bs_unload(bbs->bb_bs, unload_bs_cp, bbs);
	}

	return 1;
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
		if (bbs->bb_state != BIO_BS_STATE_REINT)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_FAULTY:
		if (bbs->bb_state != BIO_BS_STATE_NORMAL &&
		    bbs->bb_state != BIO_BS_STATE_REPLACED &&
		    bbs->bb_state != BIO_BS_STATE_REINT)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_TEARDOWN:
		if (bbs->bb_state != BIO_BS_STATE_FAULTY)
			rc = -DER_INVAL;
		break;
	case BIO_BS_STATE_OUT:
		if (bbs->bb_state != BIO_BS_STATE_TEARDOWN)
			rc = -DER_INVAL;
	case BIO_BS_STATE_REPLACED:
	case BIO_BS_STATE_REINT:
		rc = -DER_NOSYS;
		break;
	default:
		rc = -DER_INVAL;
		D_ASSERTF(0, "Invalid bs state: %u\n", new_state);
		break;
	}

	if (rc) {
		D_ERROR("BS state transit error! tgt: %d, %u -> %u\n",
			bbs->bb_owner_xs->bxc_tgt_id, bbs->bb_state, new_state);
	} else {
		D_DEBUG(DB_MGMT, "BS state transited. tgt: %d, %u -> %u\n",
			bbs->bb_owner_xs->bxc_tgt_id, bbs->bb_state, new_state);
		bbs->bb_state = new_state;

		if (new_state == BIO_BS_STATE_NORMAL ||
		    new_state == BIO_BS_STATE_FAULTY) {
			struct spdk_bs_type	bstype;
			uuid_t			dev_id;
			enum smd_dev_state	dev_state;

			bstype = spdk_bs_get_bstype(bbs->bb_bs);
			memcpy(dev_id, bstype.bstype, sizeof(dev_id));
			dev_state = new_state == BIO_BS_STATE_NORMAL ?
					SMD_DEV_NORMAL : SMD_DEV_FAULTY;

			rc = smd_dev_set_state(dev_id, dev_state);
			if (rc)
				D_ERROR("Set device state failed. %d\n", rc);
		}
	}
	ABT_mutex_unlock(bbs->bb_mutex);

	return rc;
}

int
bio_bs_state_transit(struct bio_blobstore *bbs)
{
	int	rc;

	D_ASSERT(bbs != NULL);

	switch (bbs->bb_state) {
	case BIO_BS_STATE_NORMAL:
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
	case BIO_BS_STATE_REPLACED:
	case BIO_BS_STATE_REINT:
		rc = -DER_NOSYS;
		break;
	default:
		rc = -DER_INVAL;
		D_ASSERTF(0, "Invalid bs state:%u\n", bbs->bb_state);
		break;
	}

	return (rc < 0) ? rc : 0;
}
