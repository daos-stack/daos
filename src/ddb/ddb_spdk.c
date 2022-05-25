/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <bio_internal.h>
#include <spdk/stdinc.h>
#include <spdk/bdev.h>
#include <spdk/env.h>
#include <spdk/event.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>
#include <spdk/log.h>
#include <spdk/string.h>
#include <uuid/uuid.h>

#include "ddb_common.h"
#include "ddb_spdk.h"

/* [todo-ryon]: why 128? */
#define BDEV_NAME_MAX 128
struct ddb_spdk_context {
	char			 bdev_name[BDEV_NAME_MAX];
	struct spdk_blob_store	*dsc_bs;
	struct spdk_blob	*dsc_blob;
	spdk_blob_id		 dsc_blobid;
	struct spdk_io_channel	*dsc_channel;
	uint8_t			*dsc_read_buf;
	uint64_t		 dsc_io_unit_size;
	ddbs_sync_cb		 dsc_cb_func;
	void			*dsc_cb_arg;
	int			 dsc_rc;
};

static void iter_cb(void *cb_arg, struct spdk_blob *blb, int bs_errno);

static int
dsc_init(struct ddb_spdk_context **ctx)
{
	D_ALLOC_PTR(*ctx);

	if (*ctx == NULL) {
		D_ERROR("Could not alloc ctx\n");
		return -DER_NOMEM;
	}

	return 0;
}

static void
dsc_fini(struct ddb_spdk_context *ctx)
{
	spdk_free(ctx->dsc_read_buf);
	free(ctx);
}

static void
unload_complete(void *cb_arg, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	if (!SUCCESS(bs_errno)) {
		ctx->dsc_rc = daos_errno2der(-bs_errno);
		D_ERROR("Error: "DF_RC"\n", DP_RC(ctx->dsc_rc));
	}

	spdk_app_stop(ctx->dsc_rc);
}

static void
bs_unload(struct ddb_spdk_context *ctx, char *msg, int rc)
{
	if (!SUCCESS(rc)) {
		D_ERROR("%s: "DF_RC"\n", msg, DP_RC(rc));
		ctx->dsc_rc = rc;
	}
	if (ctx->dsc_bs) {
		if (ctx->dsc_channel)
			spdk_bs_free_io_channel(ctx->dsc_channel);
		spdk_bs_unload(ctx->dsc_bs, unload_complete, ctx);
	} else {
		spdk_app_stop(rc);
	}
}

static void
bs_unload_spdk_error(struct ddb_spdk_context *ctx, char *msg, int bs_errno)
{
	bs_unload(ctx, msg, daos_errno2der(-bs_errno));
}

static void
close_blob_cb(void *cb_arg, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	if (bs_errno) {
		bs_unload_spdk_error(ctx, "Error in close completion", bs_errno);
		return;
	}

	/* blob closed so move on to the next */
	spdk_bs_iter_next(ctx->dsc_bs, ctx->dsc_blob, iter_cb, ctx);
}

static void
read_complete_cb(void *cb_arg, int bs_errno)
{
	struct ddb_spdk_context	*ctx = cb_arg;
	struct bio_blob_hdr	*hdr;
	int			 rc;

	if (bs_errno) {
		bs_unload_spdk_error(ctx, "Error in read completion", bs_errno);
		return;
	}

	hdr = (struct bio_blob_hdr *)ctx->dsc_read_buf;

	if (hdr->bbh_magic == BIO_BLOB_HDR_MAGIC) {
		rc = ctx->dsc_cb_func(hdr, ctx->dsc_cb_arg);
		if (!SUCCESS(rc))
			ctx->dsc_rc = rc; /* Record the error, but don't fail */
	} else {
		D_ERROR("BIO Header for blob ID %lu is invalid. Not using to sync.\n",
			ctx->dsc_blobid);
		ctx->dsc_rc = -DER_INVAL;
	}

	spdk_blob_close(ctx->dsc_blob, close_blob_cb, ctx);
}

static void
blob_open_complete_cb(void *cb_arg, struct spdk_blob *blob, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	if (bs_errno) {
		bs_unload_spdk_error(ctx, "Error in open completion", bs_errno);
		return;
	}

	ctx->dsc_blob = blob;

	/* Read the first block ... that's where the bio header is */
	spdk_blob_io_read(ctx->dsc_blob, ctx->dsc_channel, ctx->dsc_read_buf, 0, 1,
			  read_complete_cb, ctx);
}

static void
iter_cb(void *cb_arg, struct spdk_blob *blb, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	if (!SUCCESS(bs_errno)) {
		if (bs_errno == -ENOENT)
			/* No more ... so unload */
			bs_unload(ctx, "", 0);
		else
			bs_unload_spdk_error(ctx, "Error in blob iter callback", bs_errno);
		return;
	}

	ctx->dsc_blobid = spdk_blob_get_id(blb);
	spdk_bs_open_blob(ctx->dsc_bs, ctx->dsc_blobid, blob_open_complete_cb, ctx);
}

static void
bs_init_complete_cb(void *cb_arg, struct spdk_blob_store *bs, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	if (!SUCCESS(bs_errno)) {
		bs_unload_spdk_error(ctx, "Error initializing the blobstore", bs_errno);
		return;
	}

	ctx->dsc_bs = bs;
	ctx->dsc_io_unit_size = spdk_bs_get_io_unit_size(ctx->dsc_bs);
	ctx->dsc_read_buf = spdk_malloc(ctx->dsc_io_unit_size, 0x1000, NULL,
					SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (ctx->dsc_read_buf == NULL) {
		bs_unload(ctx, "Error in memory allocation", -DER_NOMEM);
		return;
	}

	ctx->dsc_channel = spdk_bs_alloc_io_channel(ctx->dsc_bs);
	if (ctx->dsc_channel == NULL) {
		bs_unload(ctx, "Error in allocating channel", -DER_NOMEM);
		return;
	}
	spdk_bs_iter_first(bs, iter_cb, ctx);
}

static void
base_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *cb_arg)
{
	D_WARN("Unsupported bdev event type: %d\n", type);
}

static void
dsc_start_cb(void *arg)
{
	struct ddb_spdk_context *ctx = arg;
	struct spdk_bs_dev	*bs_dev = NULL;
	struct spdk_bdev	*bdev;
	int			 rc;

	for (bdev = spdk_bdev_first(); bdev != NULL; bdev = spdk_bdev_next(bdev)) {
		strncpy(ctx->bdev_name, spdk_bdev_get_name(bdev), sizeof(ctx->bdev_name));
		rc = spdk_bdev_create_bs_dev_ext(ctx->bdev_name, base_bdev_event_cb, NULL, &bs_dev);
		if (rc != 0) {
			D_ERROR("Could not create blob bdev: %s\n", spdk_strerror(-rc));
			spdk_app_stop(daos_errno2der(-rc));
			return;
		}
	}

	spdk_bs_load(bs_dev, NULL, bs_init_complete_cb, ctx);
}

int
ddbs_for_each_bio_blob_hdr(char *nvme_json, ddbs_sync_cb cb, void *cb_arg)
{
	struct spdk_app_opts	opts = {0};
	struct ddb_spdk_context *ctx = NULL;
	int			rc;

	D_ASSERT(cb != NULL);

	rc = dsc_init(&ctx);
	if (!SUCCESS(rc))
		return rc;
	ctx->dsc_cb_func = cb;
	ctx->dsc_cb_arg = cb_arg;

	spdk_app_opts_init(&opts, sizeof(opts));

	opts.print_level = SPDK_LOG_ERROR;
	opts.name = "ddb_spdk";
	opts.json_config_file = nvme_json;

	rc = spdk_app_start(&opts, dsc_start_cb, ctx);
	if (!SUCCESS(rc))
		D_ERROR("Failed: "DF_RC"\n", DP_RC(rc));

	dsc_fini(ctx);

	spdk_app_fini();
	return rc;
}
