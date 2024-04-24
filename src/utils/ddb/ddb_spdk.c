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
#include <spdk/string.h>
#include <uuid/uuid.h>

#include "ddb_common.h"
#include "ddb_spdk.h"

#define TRACE(...) D_DEBUG(DB_TRACE, __VA_ARGS__)

/*
 * According to https://spdk.io/doc/concurrency.html, the best way to manage concurrency is to
 * use a state machine.
 */

/* States of the machine */
enum DDB_SPDK_ST {
	DDB_SPDK_ST_BDEV,
	DDB_SPDK_ST_BS_OPEN_ASYNC,
	DDB_SPDK_ST_BLOB_ITER_ASYNC,
	DDB_SPDK_ST_BLOB_READ_ASYNC,
	DDB_SPDK_ST_BLOB_CLOSE_ASYNC,
	DDB_SPDK_ST_SEND_INFO,
	DDB_SPDK_ST_BS_CLOSE_ASYNC,
	DDB_SPDK_ST_DONE,
};

/* Just for debugging purposes */
static const char *
state_str(enum DDB_SPDK_ST s)
{
	switch (s) {
	case DDB_SPDK_ST_BDEV:
		return "DDB_SPDK_ST_BDEV";
	case DDB_SPDK_ST_BS_OPEN_ASYNC:
		return "DDB_SPDK_ST_BS_OPEN_ASYNC";
	case DDB_SPDK_ST_BLOB_ITER_ASYNC:
		return "DDB_SPDK_ST_BLOB_ITER_ASYNC";
	case DDB_SPDK_ST_BLOB_READ_ASYNC:
		return "DDB_SPDK_ST_BLOB_READ_ASYNC";
	case DDB_SPDK_ST_SEND_INFO:
		return "DDB_SPDK_ST_SEND_INFO";
	case DDB_SPDK_ST_BLOB_CLOSE_ASYNC:
		return "DDB_SPDK_ST_BLOB_CLOSE_ASYNC";
	case DDB_SPDK_ST_BS_CLOSE_ASYNC:
		return "DDB_SPDK_ST_BS_CLOSE_ASYNC";
	case DDB_SPDK_ST_DONE:
		return "DDB_SPDK_ST_DONE";
	default:
		return "UNKNOWN";
	}
}

static void
print_transition(enum DDB_SPDK_ST a, enum DDB_SPDK_ST b, int rc)
{
	if (a != b)
		TRACE("%s -> %s, rc: "DF_RC"\n", state_str(a), state_str(b), DP_RC(rc));
}

#define BDEV_NAME_MAX 128
struct ddb_spdk_context {
	/* Used for passing info back to the caller */
	struct ddbs_sync_info	 dsc_dsi;
	ddbs_sync_cb		 dsc_cb_func;
	void			*dsc_cb_arg;

	/* For managing the interaction with spdk */
	struct spdk_bdev	*dsc_bdev;
	struct spdk_bs_dev	*dsc_bs_dev;
	struct spdk_blob_store	*dsc_bs;
	struct spdk_blob	*dsc_blob;
	struct spdk_io_channel	*dsc_channel;
	uint8_t			*dsc_read_buf;
	uint64_t		 dsc_io_unit_size;

	/* For managing the state machine */
	enum DDB_SPDK_ST	 dsc_state;
	bool			 dsc_async_state_done;
	bool			 dsc_running;

	/* Capture any error along the way */
	int			 dsc_rc;
};

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
	D_FREE(ctx);
}

/* Setup what's needed to do a blob read */
static int
dsc_read_setup(struct ddb_spdk_context *ctx) {
	D_ASSERT(ctx->dsc_bs != NULL);
	ctx->dsc_channel = spdk_bs_alloc_io_channel(ctx->dsc_bs);
	if (ctx->dsc_channel == NULL)
		return -DER_NOMEM;

	ctx->dsc_io_unit_size = spdk_bs_get_io_unit_size(ctx->dsc_bs);
	ctx->dsc_read_buf = spdk_malloc(ctx->dsc_io_unit_size, 0x1000, NULL,
					SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
	if (ctx->dsc_read_buf == NULL)
		return -DER_NOMEM;

	return 0;
}

static void
dsc_read_teardown(struct ddb_spdk_context *ctx)
{
	if (ctx->dsc_channel) {
		spdk_bs_free_io_channel(ctx->dsc_channel);
		ctx->dsc_channel = NULL;
	}
	if (ctx->dsc_read_buf) {
		spdk_free(ctx->dsc_read_buf);
		ctx->dsc_read_buf = NULL;
	}
	ctx->dsc_io_unit_size = 0;
}

static void
dsc_record_error(struct ddb_spdk_context *ctx, int bs_errno)
{
	/* only keep error if there is one and rc isn't already an error code */
	if (bs_errno != 0 && ctx->dsc_rc == 0) {
		ctx->dsc_rc = daos_errno2der(-bs_errno);
		TRACE("Recording error: "DF_RC"\n", DP_RC(ctx->dsc_rc));
	}
}

/*
 * The next section of functions are callback/async pairs. SPDK relies heavily on callback
 * functions. The "async" function executes an SPDK function which takes a callback. In general,
 * the callback function should come right before the function that executes the SPDK function. The
 * callback function will record any error. Due to the async nature, the state machine might exit
 * before the callbacks are executed by SPDK, therefore the callbacks may restart the state
 * machine at the appropriate state if needed. The 'before'/'after' tracing in the async methods
 * with the 'callback' trace is helpful in seeing how the async works which was critical in
 * developing and debugging the state machine as well as viewing the behavior of SPDK.
 */

/* Allow the callback functions to restart the state machine */
static void dsc_continue_state_machine_after_async(struct ddb_spdk_context *ctx);

static void
blob_close_cb(void *cb_arg, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	TRACE("blob close callback\n");

	dsc_record_error(ctx, bs_errno);
	dsc_continue_state_machine_after_async(ctx);
}

static void
dsc_blob_close_async(struct ddb_spdk_context *ctx)
{
	TRACE("blob close (before)\n");
	spdk_blob_close(ctx->dsc_blob, blob_close_cb, ctx);
	TRACE("blob close (after)\n");
}

static void
bs_open_complete_cb(void *cb_arg, struct spdk_blob_store *bs, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	TRACE("bs open callback\n");
	if (!SUCCESS(bs_errno)) {
		dsc_record_error(ctx, bs_errno);
		return;
	}
	ctx->dsc_bs = bs;

	/* now setup for reading */
	ctx->dsc_rc = dsc_read_setup(ctx);
	dsc_continue_state_machine_after_async(ctx);
}

static void
dsc_bs_open_async(struct ddb_spdk_context *ctx)
{
	TRACE("bs open (before)\n");
	spdk_bs_load(ctx->dsc_bs_dev, NULL, bs_open_complete_cb, ctx);
	TRACE("bs open (close)\n");
}

static void
bs_close_cb(void *cb_arg, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	TRACE("bs close callback\n");
	dsc_record_error(ctx, bs_errno);
	ctx->dsc_bs = NULL;

	dsc_continue_state_machine_after_async(ctx);
}

static void
dsc_bs_close_async(struct ddb_spdk_context *ctx) {
	dsc_read_teardown(ctx);

	if (ctx->dsc_bs) {
		TRACE("close bs (before)\n");
		spdk_bs_unload(ctx->dsc_bs, bs_close_cb, ctx);
		TRACE("close bs (after)\n");
	} else {
		TRACE("bs already closed??\n");
	}
}

static void
dsc_blob_iter_cb(void *cb_arg, struct spdk_blob *blb, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	TRACE("blob iter callback\n");

	if (bs_errno != 0) {
		/*
		 * No more blobs to process. This will indicate to
		 * the state machine to close the blobstore.
		 */
		ctx->dsc_blob = NULL;
		if (bs_errno != -ENOENT) {
			dsc_record_error(ctx, bs_errno);
			TRACE("error\n");
		} else {
			TRACE("No more blobs\n");
		}
	} else {
		TRACE("setting blob\n");
		ctx->dsc_blob = blb;
	}
	dsc_continue_state_machine_after_async(ctx);
}

static void
dsc_blob_iter_async(struct ddb_spdk_context *ctx)
{
	D_ASSERT(ctx->dsc_bs != NULL);

	if (ctx->dsc_blob == NULL) {
		TRACE("first blob (before)\n");
		spdk_bs_iter_first(ctx->dsc_bs, dsc_blob_iter_cb, ctx);
		TRACE("first blob (after)\n");
	} else {
		TRACE("next blob (before)\n");
		spdk_bs_iter_next(ctx->dsc_bs, ctx->dsc_blob, dsc_blob_iter_cb, ctx);
		TRACE("next blob (after)\n");
	}
}

static void
blob_read_hdr_cb(void *cb_arg, int bs_errno)
{
	struct ddb_spdk_context *ctx = cb_arg;

	TRACE("read blob callback\n");

	dsc_record_error(ctx, bs_errno);
	if (bs_errno == 0) {
		struct bio_blob_hdr *hdr;

		D_ASSERT(ctx->dsc_read_buf != NULL);
		hdr = (struct bio_blob_hdr *) ctx->dsc_read_buf;
		/* verify the header */
		if (hdr->bbh_magic == BIO_BLOB_HDR_MAGIC) {
			ctx->dsc_dsi.dsi_hdr = hdr;
		} else {
			D_PRINT("BIO_BLOB_HDR_MAGIC is not correct for blob id '%lu'. "
				"Got '%x' but expected '%x'\n",
				spdk_blob_get_id(ctx->dsc_blob), hdr->bbh_magic,
				BIO_BLOB_HDR_MAGIC);
			ctx->dsc_rc = -DER_UNKNOWN;
		}
	}

	dsc_continue_state_machine_after_async(ctx);
}

static void
dsc_blob_read_hdr_async(struct ddb_spdk_context *ctx)
{
	TRACE("reading blob (before)\n");
	spdk_blob_io_read(ctx->dsc_blob, ctx->dsc_channel, ctx->dsc_read_buf, 0, 1,
			  blob_read_hdr_cb, ctx);
	TRACE("reading blob (after)\n");
}

static void
base_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *cb_arg)
{
	D_WARN("Unsupported bdev event type: %d\n", type);
}

static void
dsc_bdev(struct ddb_spdk_context *ctx)
{
	char	bdev_name[BDEV_NAME_MAX];
	int	err;
	int	rc = 0;

	if (ctx->dsc_bdev == NULL)
		ctx->dsc_bdev = spdk_bdev_first();
	else
		ctx->dsc_bdev = spdk_bdev_next(ctx->dsc_bdev);

	if (ctx->dsc_bdev == NULL)
		return;

	strncpy(bdev_name, spdk_bdev_get_name(ctx->dsc_bdev), sizeof(bdev_name) - 1);
	bdev_name[sizeof(bdev_name) - 1] = '\0';

	TRACE("Creating bs dev for device name: %s\n", bdev_name);
	err = spdk_bdev_create_bs_dev_ext(bdev_name, base_bdev_event_cb, NULL, &ctx->dsc_bs_dev);
	if (err != 0) {
		rc = daos_errno2der(-err);
		D_ERROR("Could not create blob bdev: %s\n", spdk_strerror(-err));
	}
	ctx->dsc_rc = rc;
}

static void
dsc_get_dev_id(struct ddb_spdk_context *ctx)
{
	struct spdk_bs_type bstype = spdk_bs_get_bstype(ctx->dsc_bs);

	memcpy(ctx->dsc_dsi.dsi_dev_id, bstype.bstype, sizeof(ctx->dsc_dsi.dsi_dev_id));
	ctx->dsc_dsi.dsi_cluster_size = spdk_bs_get_cluster_size(ctx->dsc_bs);
	ctx->dsc_dsi.dsi_cluster_nr = spdk_blob_get_num_clusters(ctx->dsc_blob);
	TRACE("Got device id: "DF_UUID"\n", DP_UUID(ctx->dsc_dsi.dsi_dev_id));
}

static void
dsc_send_info(struct ddb_spdk_context *ctx)
{
	dsc_get_dev_id(ctx);

	TRACE("sending info to callback\n");
	ctx->dsc_cb_func(&ctx->dsc_dsi, ctx->dsc_cb_arg);
}

static void
dsc_if_error_handle_state_change(struct ddb_spdk_context *ctx)
{
	enum DDB_SPDK_ST prev_state = ctx->dsc_state;

	if (ctx->dsc_rc == 0)
		return;
	switch (ctx->dsc_state) {
	case DDB_SPDK_ST_BDEV:
		ctx->dsc_state = DDB_SPDK_ST_DONE;
		break;
	case DDB_SPDK_ST_BS_OPEN_ASYNC:
	case DDB_SPDK_ST_BLOB_ITER_ASYNC:
		ctx->dsc_state = DDB_SPDK_ST_BS_CLOSE_ASYNC;
		break;
	case DDB_SPDK_ST_BLOB_READ_ASYNC:
		ctx->dsc_state = DDB_SPDK_ST_BLOB_CLOSE_ASYNC;
		break;
	case DDB_SPDK_ST_SEND_INFO:
	case DDB_SPDK_ST_BLOB_CLOSE_ASYNC:
	case DDB_SPDK_ST_BS_CLOSE_ASYNC:
	case DDB_SPDK_ST_DONE:
		break;
	}
	if (prev_state != ctx->dsc_state)
		/* Forced a transition so reset async state */
		ctx->dsc_async_state_done = false;

	TRACE("Error State ("DF_RC"): Transitioning from %s --> %s\n",
	      DP_RC(ctx->dsc_rc), state_str(prev_state), state_str(ctx->dsc_state));
}

/* Macros to help define the function for each state and what the next state should be */
#define ST_TSN(ctx, fn, next_state) \
	do { \
		fn; \
		ctx->dsc_state = next_state; \
	} while (0)
#define ST_TSN_COND(ctx, fn, cond, true_state, false_state) \
	do { \
		fn; \
		if ((cond)) \
			ctx->dsc_state = true_state; \
		else \
			ctx->dsc_state = false_state; \
	} while (0)
#define ST_TSN_ASYNC(ctx, fn, next_state) \
	do { \
		if (ctx->dsc_async_state_done) { \
			ctx->dsc_async_state_done = false; \
			ctx->dsc_state = next_state; \
		} else { \
		fn; \
		} \
	} while (0)
#define ST_TSN_COND_ASYNC(ctx, fn, cond, true_state, false_state) \
	do { \
		if (ctx->dsc_async_state_done) { \
			ctx->dsc_async_state_done = false; \
			if ((cond)) \
				ctx->dsc_state = true_state; \
			else \
				ctx->dsc_state = false_state; \
		} else { \
			fn; \
		} \
	} while (0)

static void
dsc_run_state_machine(struct ddb_spdk_context *ctx)
{
	enum DDB_SPDK_ST prev_state;

	ctx->dsc_running = true;

	TRACE("\nState Machine starting with state: %s\n", state_str(ctx->dsc_state));

	do {
		dsc_if_error_handle_state_change(ctx);
		prev_state = ctx->dsc_state;
		switch (ctx->dsc_state) {
		case DDB_SPDK_ST_BDEV:
			ST_TSN_COND(ctx, dsc_bdev(ctx),
				    /*
				     * if bdev == NULL then no more devices, everything should
				     * already be closed so just finish
				     */
				    ctx->dsc_bdev != NULL, DDB_SPDK_ST_BS_OPEN_ASYNC,
				    DDB_SPDK_ST_DONE);
			break;
		case DDB_SPDK_ST_BS_OPEN_ASYNC:
			ST_TSN_ASYNC(ctx, dsc_bs_open_async(ctx), DDB_SPDK_ST_BLOB_ITER_ASYNC);
			break;
		case DDB_SPDK_ST_BLOB_ITER_ASYNC:
			ST_TSN_COND_ASYNC(ctx, dsc_blob_iter_async(ctx),
					  /* if blob == NULL then there are no more blobs */
					  ctx->dsc_blob == NULL, DDB_SPDK_ST_BS_CLOSE_ASYNC,
					  DDB_SPDK_ST_BLOB_READ_ASYNC);
			break;
		case DDB_SPDK_ST_BLOB_READ_ASYNC:
			ST_TSN_ASYNC(ctx, dsc_blob_read_hdr_async(ctx), DDB_SPDK_ST_SEND_INFO);
			break;
		case DDB_SPDK_ST_SEND_INFO:
			ST_TSN(ctx, dsc_send_info(ctx), DDB_SPDK_ST_BLOB_ITER_ASYNC);
			break;
		case DDB_SPDK_ST_BLOB_CLOSE_ASYNC:
			/* After closing, start the iteration loop over */
			ST_TSN_ASYNC(ctx, dsc_blob_close_async(ctx), DDB_SPDK_ST_BLOB_ITER_ASYNC);
			break;
		case DDB_SPDK_ST_BS_CLOSE_ASYNC:
			ST_TSN_ASYNC(ctx, dsc_bs_close_async(ctx), DDB_SPDK_ST_BDEV);
			break;
		case DDB_SPDK_ST_DONE:
			spdk_app_stop(ctx->dsc_rc);
			break;
		}
		print_transition(prev_state, ctx->dsc_state, ctx->dsc_rc);
	/*
	 * If the state hasn't changed then leave the state machine. Should
	 * get called again by a asynchronous callback if not done. Note: if dsc_async_state_done is
	 * true then the state changed by a callback because the machine always leaves
	 * dsc_async_state_done to false.
	 */
	} while ((prev_state != ctx->dsc_state) || ctx->dsc_async_state_done);
	TRACE("Leaving state machine on state: %s\n\n", state_str(ctx->dsc_state));
	ctx->dsc_running = false;
}

static void
dsc_continue_state_machine_after_async(struct ddb_spdk_context *ctx)
{
	/*
	 * Sometimes the callbacks are run after the state machine leaves and sometimes right after
	 * the "parent" function is called. Callbacks should re-enter the state machine if it's
	 * not already running. Set a callback done flag so the machine knows  if it was already
	 * called.
	 */
	ctx->dsc_async_state_done = true;
	if (!ctx->dsc_running) {
		TRACE("Restarting state machine at state: %s\n", state_str(ctx->dsc_state));
		dsc_run_state_machine(ctx);
	}
}

static void
app_start_cb(void *arg)
{
	struct ddb_spdk_context *ctx = arg;

	/* start by getting the first bdev */
	ctx->dsc_state = DDB_SPDK_ST_BDEV;
	dsc_run_state_machine(ctx);
}

/*
 * This is used by the smd sync command for ddb. Most of the SMD table info can be rebuilt by using
 * information saved in the SPDK blobs used for each target.
 *
 * Using the state machine above, will start an spdk_app that will iterate over the blobs, read
 * the blob header (a daos construct, see struct bio_blob_hdr), gather other information needed
 * from the blob or blobstore and pass to the callback function provided
 */
int
ddbs_for_each_bio_blob_hdr(const char *nvme_json, ddbs_sync_cb cb, void *cb_arg)
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
	opts.print_level = SPDK_LOG_DISABLED;
	opts.name = "ddb_spdk";
	opts.json_config_file = nvme_json;
	rc = spdk_app_start(&opts, app_start_cb, ctx);
	if (!SUCCESS(rc))
		D_ERROR("Failed: "DF_RC"\n", DP_RC(rc));

	dsc_fini(ctx);

	spdk_app_fini();
	return rc;
}
