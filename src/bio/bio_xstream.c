/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>
#include <abt.h>
#include <spdk/env.h>
#include <spdk/init.h>
#include <spdk/nvme.h>
#include <spdk/vmd.h>
#include <spdk/thread.h>
#include <spdk/bdev.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

/* These Macros should be turned into DAOS configuration in the future */
#define DAOS_MSG_RING_SZ	4096
/* SPDK blob parameters */
#define DAOS_BS_CLUSTER_SZ	(1ULL << 30)	/* 1GB */
#define DAOS_BS_MD_PAGES	(1024 * 20)	/* 20k blobs per device */
/* DMA buffer parameters */
#define DAOS_DMA_CHUNK_MB	8	/* 8MB DMA chunks */
#define DAOS_DMA_CHUNK_CNT_INIT	32	/* Per-xstream init chunks */
#define DAOS_DMA_CHUNK_CNT_MAX	128	/* Per-xstream max chunks */
#define DAOS_DMA_MIN_UB_BUF_MB	1024	/* 1GB min upper bound DMA buffer */

/* Max inflight blob IOs per io channel */
#define BIO_BS_MAX_CHANNEL_OPS	(4096)
/* Schedule a NVMe poll when so many blob IOs queued for an io channel */
#define BIO_BS_POLL_WATERMARK	(2048)

/* Chunk size of DMA buffer in pages */
unsigned int bio_chk_sz;
/* Per-xstream maximum DMA buffer size (in chunk count) */
unsigned int bio_chk_cnt_max;
/* Per-xstream initial DMA buffer size (in chunk count) */
static unsigned int bio_chk_cnt_init;
/* Diret RDMA over SCM */
bool bio_scm_rdma;

struct bio_nvme_data {
	ABT_mutex		 bd_mutex;
	ABT_cond		 bd_barrier;
	/* SPDK bdev type */
	int			 bd_bdev_class;
	/* How many xstreams has initialized NVMe context */
	int			 bd_xstream_cnt;
	/* The thread responsible for SPDK bdevs init/fini */
	struct spdk_thread	*bd_init_thread;
	/* Default SPDK blobstore options */
	struct spdk_bs_opts	 bd_bs_opts;
	/* All bdevs can be used by DAOS server */
	d_list_t		 bd_bdevs;
	uint64_t		 bd_scan_age;
	/* Path to input SPDK JSON NVMe config file */
	const char		*bd_nvme_conf;
	int			 bd_shm_id;
	/* When using SPDK primary mode, specifies memory allocation in MB */
	int			 bd_mem_size;
	bool			 bd_started;
	bool			 bd_bypass_health_collect;
};

static struct bio_nvme_data nvme_glb;
uint64_t vmd_led_period;

static int
bio_spdk_env_init(void)
{
	struct spdk_env_opts	 opts;
	int			 rc;

	D_ASSERT(nvme_glb.bd_nvme_conf != NULL);

	spdk_env_opts_init(&opts);
	opts.name = "daos";

	/*
	 * TODO: Set opts.mem_size to nvme_glb.bd_mem_size
	 * Currently we can't guarantee clean shutdown (no hugepages leaked).
	 * Setting mem_size could cause EAL: Not enough memory available error,
	 * and DPDK will fail to initialize.
	 */

	if (nvme_glb.bd_shm_id != DAOS_NVME_SHMID_NONE)
		opts.shm_id = nvme_glb.bd_shm_id;

	/*
	 * TODO: Find a way to set multiple overrides, currently only single
	 * option can be overridden with opts.env_context.
	 */

	/*
	 * Quiet DPDK logging by setting level to ERROR
	 *  opts.env_context = "--log-level=lib.eal:4";
	 */
	opts.env_context = "--no-telemetry";

	rc = spdk_env_init(&opts);
	if (rc != 0) {
		rc = -DER_INVAL; /* spdk_env_init() returns -1 */
		D_ERROR("Failed to initialize SPDK env, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	spdk_unaffinitize_thread();

	rc = spdk_thread_lib_init(NULL, 0);
	if (rc != 0) {
		rc = -DER_INVAL;
		D_ERROR("Failed to init SPDK thread lib, "DF_RC"\n", DP_RC(rc));
		spdk_env_fini();
		return rc;
	}

	return rc;
}

bool
bio_nvme_configured(void)
{
	return nvme_glb.bd_nvme_conf != NULL;
}

bool
bypass_health_collect()
{
	return nvme_glb.bd_bypass_health_collect;
}

int
bio_nvme_init(const char *nvme_conf, int shm_id, int mem_size,
	      int hugepage_size, int tgt_nr, struct sys_db *db,
	      bool bypass_health_collect)
{
	char		*env;
	int		 rc, fd;
	unsigned int	 size_mb = DAOS_DMA_CHUNK_MB;

	nvme_glb.bd_xstream_cnt = 0;
	nvme_glb.bd_init_thread = NULL;
	nvme_glb.bd_nvme_conf = NULL;
	nvme_glb.bd_bypass_health_collect = bypass_health_collect;
	D_INIT_LIST_HEAD(&nvme_glb.bd_bdevs);

	rc = ABT_mutex_create(&nvme_glb.bd_mutex);
	if (rc != ABT_SUCCESS) {
		return dss_abterr2der(rc);
	}

	rc = ABT_cond_create(&nvme_glb.bd_barrier);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto free_mutex;
	}

	bio_chk_cnt_init = DAOS_DMA_CHUNK_CNT_INIT;
	bio_chk_cnt_max = DAOS_DMA_CHUNK_CNT_MAX;
	bio_chk_sz = ((uint64_t)size_mb << 20) >> BIO_DMA_PAGE_SHIFT;

	d_getenv_bool("DAOS_SCM_RDMA_ENABLED", &bio_scm_rdma);
	D_INFO("RDMA to SCM is %s\n", bio_scm_rdma ? "enabled" : "disabled");

	if (nvme_conf == NULL || strlen(nvme_conf) == 0) {
		D_INFO("NVMe config isn't specified, skip NVMe setup.\n");
		return 0;
	}

	fd = open(nvme_conf, O_RDONLY, 0600);
	if (fd < 0) {
		D_WARN("Open %s failed, skip DAOS NVMe setup "DF_RC"\n",
		       nvme_conf, DP_RC(daos_errno2der(errno)));
		return 0;
	}
	close(fd);

	D_ASSERT(tgt_nr > 0);
	D_ASSERT(mem_size > 0);
	D_ASSERT(hugepage_size > 0);
	/*
	 * Hugepages are not enough to sustain average I/O workload
	 * (~1GB per xstream).
	 */
	if ((mem_size / tgt_nr) < DAOS_DMA_MIN_UB_BUF_MB) {
		D_ERROR("Per-xstream DMA buffer upper bound limit < 1GB!\n");
		D_DEBUG(DB_MGMT, "mem_size:%dMB, DMA upper bound:%dMB\n",
			mem_size, (mem_size / tgt_nr));
		return -DER_INVAL;
	}

	bio_chk_cnt_max = (mem_size / tgt_nr) / size_mb;
	D_INFO("Set per-xstream DMA buffer upper bound to %u %uMB chunks\n",
	       bio_chk_cnt_max, size_mb);

	rc = smd_init(db);
	if (rc != 0) {
		D_ERROR("Initialize SMD store failed. "DF_RC"\n", DP_RC(rc));
		goto free_cond;
	}

	spdk_bs_opts_init(&nvme_glb.bd_bs_opts, sizeof(nvme_glb.bd_bs_opts));
	nvme_glb.bd_bs_opts.cluster_sz = DAOS_BS_CLUSTER_SZ;
	nvme_glb.bd_bs_opts.num_md_pages = DAOS_BS_MD_PAGES;
	nvme_glb.bd_bs_opts.max_channel_ops = BIO_BS_MAX_CHANNEL_OPS;

	env = getenv("VOS_BDEV_CLASS");
	if (env && strcasecmp(env, "AIO") == 0) {
		D_WARN("AIO device(s) will be used!\n");
		nvme_glb.bd_bdev_class = BDEV_CLASS_AIO;
	}

	env = getenv("VMD_LED_PERIOD");
	vmd_led_period = env ? atoi(env) : 0;
	vmd_led_period *= (NSEC_PER_SEC / NSEC_PER_USEC);

	nvme_glb.bd_shm_id = shm_id;
	nvme_glb.bd_mem_size = mem_size;
	nvme_glb.bd_nvme_conf = nvme_conf;

	rc = bio_spdk_env_init();
	if (rc) {
		nvme_glb.bd_nvme_conf = NULL;
		goto fini_smd;
	}

	return 0;

fini_smd:
	smd_fini();
free_cond:
	ABT_cond_free(&nvme_glb.bd_barrier);
free_mutex:
	ABT_mutex_free(&nvme_glb.bd_mutex);

	return rc;
}

static void
bio_spdk_env_fini(void)
{
	if (nvme_glb.bd_nvme_conf != NULL) {
		spdk_thread_lib_fini();
		spdk_env_fini();
	}
}

void
bio_nvme_fini(void)
{
	bio_spdk_env_fini();
	ABT_cond_free(&nvme_glb.bd_barrier);
	ABT_mutex_free(&nvme_glb.bd_mutex);
	D_ASSERT(nvme_glb.bd_xstream_cnt == 0);
	D_ASSERT(nvme_glb.bd_init_thread == NULL);
	D_ASSERT(d_list_empty(&nvme_glb.bd_bdevs));
	smd_fini();
}

static inline bool
is_bbs_owner(struct bio_xs_context *ctxt, struct bio_blobstore *bbs)
{
	D_ASSERT(ctxt != NULL);
	D_ASSERT(bbs != NULL);
	return bbs->bb_owner_xs == ctxt;
}

inline struct spdk_thread *
init_thread(void)
{
	return nvme_glb.bd_init_thread;
}

inline bool
is_server_started(void)
{
	return nvme_glb.bd_started;
}

inline d_list_t *
bio_bdev_list(void)
{
	return &nvme_glb.bd_bdevs;
}

inline bool
is_init_xstream(struct bio_xs_context *ctxt)
{
	D_ASSERT(ctxt != NULL);
	return ctxt->bxc_thread == nvme_glb.bd_init_thread;
}

bool
bio_need_nvme_poll(struct bio_xs_context *ctxt)
{
	if (ctxt == NULL)
		return false;
	return ctxt->bxc_blob_rw > BIO_BS_POLL_WATERMARK;
}

struct common_cp_arg {
	unsigned int		 cca_inflights;
	int			 cca_rc;
	struct spdk_blob_store	*cca_bs;
};

static void
common_prep_arg(struct common_cp_arg *arg)
{
	arg->cca_inflights = 1;
	arg->cca_rc = 0;
	arg->cca_bs = NULL;
}

static void
common_init_cb(void *arg, int rc)
{
	struct common_cp_arg *cp_arg = arg;

	D_ASSERT(cp_arg->cca_inflights == 1);
	D_ASSERT(cp_arg->cca_rc == 0);
	cp_arg->cca_inflights--;
	cp_arg->cca_rc = daos_errno2der(-rc);
}

static void
subsys_init_cb(int rc, void *arg)
{
	common_init_cb(arg, rc);
}

static void
common_fini_cb(void *arg)
{
	struct common_cp_arg *cp_arg = arg;

	D_ASSERT(cp_arg->cca_inflights == 1);
	cp_arg->cca_inflights--;
}

static void
common_bs_cb(void *arg, struct spdk_blob_store *bs, int rc)
{
	struct common_cp_arg *cp_arg = arg;

	D_ASSERT(cp_arg->cca_inflights == 1);
	D_ASSERT(cp_arg->cca_rc == 0);
	D_ASSERT(cp_arg->cca_bs == NULL);
	cp_arg->cca_inflights--;
	cp_arg->cca_rc = daos_errno2der(-rc);
	cp_arg->cca_bs = bs;
}

int
xs_poll_completion(struct bio_xs_context *ctxt, unsigned int *inflights,
		   uint64_t timeout)
{
	uint64_t	start_time, cur_time;

	D_ASSERT(inflights != NULL);
	D_ASSERT(ctxt != NULL);

	if (timeout != 0)
		start_time = daos_getmtime_coarse();

	/* Wait for the completion callback done or timeout */
	while (*inflights != 0) {
		spdk_thread_poll(ctxt->bxc_thread, 0, 0);

		/* Completion is executed */
		if (*inflights == 0)
			return 0;

		/* Timeout */
		if (timeout != 0) {
			cur_time = daos_getmtime_coarse();

			D_ASSERT(cur_time >= start_time);
			if (cur_time - start_time > timeout)
				return -DER_TIMEDOUT;
		}
	}

	return 0;
}

struct spdk_blob_store *
load_blobstore(struct bio_xs_context *ctxt, char *bdev_name, uuid_t *bs_uuid,
	       bool create, bool async,
	       void (*async_cb)(void *arg, struct spdk_blob_store *bs, int rc),
	       void *async_arg)
{
	struct spdk_bs_dev	*bs_dev;
	struct spdk_bs_opts	 bs_opts;
	struct common_cp_arg	 cp_arg;
	int			 rc;

	/*
	 * bdev will be closed and bs_dev will be freed during
	 * spdk_bs_unload(), or in the internal error handling code of
	 * spdk_bs_init/load().
	 */
	rc = spdk_bdev_create_bs_dev_ext(bdev_name, bio_bdev_event_cb, NULL,
					 &bs_dev);
	if (rc != 0) {
		D_ERROR("failed to create bs_dev %s, %d\n", bdev_name, rc);
		return NULL;
	}

	bs_opts = nvme_glb.bd_bs_opts;
	/*
	 * A little hack here, we store a UUID in the 16 bytes 'bstype' and
	 * use it as the block device ID.
	 */
	D_ASSERT(SPDK_BLOBSTORE_TYPE_LENGTH == 16);
	if (bs_uuid == NULL)
		strncpy(bs_opts.bstype.bstype, "", SPDK_BLOBSTORE_TYPE_LENGTH);
	else
		memcpy(bs_opts.bstype.bstype, bs_uuid,
		       SPDK_BLOBSTORE_TYPE_LENGTH);

	if (async) {
		D_ASSERT(async_cb != NULL);

		if (create)
			spdk_bs_init(bs_dev, &bs_opts, async_cb, async_arg);
		else
			spdk_bs_load(bs_dev, &bs_opts, async_cb, async_arg);

		return NULL;
	}

	common_prep_arg(&cp_arg);
	if (create)
		spdk_bs_init(bs_dev, &bs_opts, common_bs_cb, &cp_arg);
	else
		spdk_bs_load(bs_dev, &bs_opts, common_bs_cb, &cp_arg);
	rc = xs_poll_completion(ctxt, &cp_arg.cca_inflights, 0);
	D_ASSERT(rc == 0);

	if (cp_arg.cca_rc != 0) {
		D_CDEBUG(bs_uuid == NULL, DB_IO, DLOG_ERR,
			 "%s blobstore failed %d\n", create ? "init" : "load",
			 cp_arg.cca_rc);
		return NULL;
	}

	D_ASSERT(cp_arg.cca_bs != NULL);
	return cp_arg.cca_bs;
}

int
unload_blobstore(struct bio_xs_context *ctxt, struct spdk_blob_store *bs)
{
	struct common_cp_arg	cp_arg;
	int			rc;

	common_prep_arg(&cp_arg);
	spdk_bs_unload(bs, common_init_cb, &cp_arg);
	rc = xs_poll_completion(ctxt, &cp_arg.cca_inflights, 0);
	D_ASSERT(rc == 0);

	if (cp_arg.cca_rc != 0)
		D_ERROR("failed to unload blobstore %d\n", cp_arg.cca_rc);

	return cp_arg.cca_rc;
}

static void
free_bio_blobstore(struct bio_blobstore *bb)
{
	D_ASSERT(bb->bb_bs == NULL);
	D_ASSERT(bb->bb_ref == 0);

	ABT_cond_free(&bb->bb_barrier);
	ABT_mutex_free(&bb->bb_mutex);
	D_FREE(bb->bb_xs_ctxts);

	D_FREE(bb);
}

void
destroy_bio_bdev(struct bio_bdev *d_bdev)
{
	D_ASSERT(d_list_empty(&d_bdev->bb_link));
	D_ASSERT(!d_bdev->bb_replacing);

	if (d_bdev->bb_desc != NULL) {
		spdk_bdev_close(d_bdev->bb_desc);
		d_bdev->bb_desc = NULL;
	}

	if (d_bdev->bb_blobstore != NULL) {
		free_bio_blobstore(d_bdev->bb_blobstore);
		d_bdev->bb_blobstore = NULL;
	}

	if (d_bdev->bb_name != NULL)
		D_FREE(d_bdev->bb_name);

	D_FREE(d_bdev);
}

struct bio_bdev *
lookup_dev_by_id(uuid_t dev_id)
{
	struct bio_bdev	*d_bdev;

	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		if (uuid_compare(d_bdev->bb_uuid, dev_id) == 0)
			return d_bdev;
	}
	return NULL;
}

static struct bio_bdev *
lookup_dev_by_name(const char *bdev_name)
{
	struct bio_bdev	*d_bdev;

	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		if (strcmp(d_bdev->bb_name, bdev_name) == 0)
			return d_bdev;
	}
	return NULL;
}

void
bio_release_bdev(void *arg)
{
	struct bio_bdev	*d_bdev = arg;

	if (!is_server_started()) {
		D_INFO("Skip device release on server start/shutdown\n");
		return;
	}

	D_ASSERT(d_bdev != NULL);
	if (d_bdev->bb_desc == NULL)
		return;

	/*
	 * It could be called from faulty device teardown procedure, where
	 * the device is still plugged.
	 */
	if (d_bdev->bb_removed) {
		spdk_bdev_close(d_bdev->bb_desc);
		d_bdev->bb_desc = NULL;
	}
}

static void
teardown_bio_bdev(void *arg)
{
	struct bio_bdev		*d_bdev = arg;
	struct bio_blobstore	*bbs = d_bdev->bb_blobstore;
	int			 rc;

	if (!is_server_started()) {
		D_INFO("Skip device teardown on server start/shutdown\n");
		return;
	}

	switch (bbs->bb_state) {
	case BIO_BS_STATE_NORMAL:
	case BIO_BS_STATE_SETUP:
		rc = bio_bs_state_set(bbs, BIO_BS_STATE_TEARDOWN);
		D_ASSERT(rc == 0);
		break;
	case BIO_BS_STATE_OUT:
		bio_release_bdev(d_bdev);
		/* fallthrough */
	case BIO_BS_STATE_FAULTY:
	case BIO_BS_STATE_TEARDOWN:
		D_DEBUG(DB_MGMT, "Device "DF_UUID"(%s) is already in "
			"%s state\n", DP_UUID(d_bdev->bb_uuid),
			d_bdev->bb_name, bio_state_enum_to_str(bbs->bb_state));
		break;
	default:
		D_ERROR("Invalid BS state %d\n", bbs->bb_state);
		break;
	}
}

void
bio_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		  void *event_ctx)
{
	struct bio_bdev		*d_bdev = event_ctx;
	struct bio_blobstore	*bbs;

	if (d_bdev == NULL || type != SPDK_BDEV_EVENT_REMOVE)
		return;

	D_DEBUG(DB_MGMT, "Got SPDK event(%d) for dev %s\n", type,
		spdk_bdev_get_name(bdev));

	if (!is_server_started()) {
		D_INFO("Skip device remove cb on server start/shutdown\n");
		return;
	}

	D_ASSERT(d_bdev->bb_desc != NULL);
	d_bdev->bb_removed = true;

	/* The bio_bdev is still under construction */
	if (d_list_empty(&d_bdev->bb_link)) {
		D_ASSERT(d_bdev->bb_blobstore == NULL);
		D_DEBUG(DB_MGMT, "bio_bdev for "DF_UUID"(%s) is still "
			"under construction\n", DP_UUID(d_bdev->bb_uuid),
			d_bdev->bb_name);
		return;
	}

	bbs = d_bdev->bb_blobstore;
	/* A new device isn't used by DAOS yet */
	if (bbs == NULL && !d_bdev->bb_replacing) {
		D_DEBUG(DB_MGMT, "Removed device "DF_UUID"(%s)\n",
			DP_UUID(d_bdev->bb_uuid), d_bdev->bb_name);

		d_list_del_init(&d_bdev->bb_link);
		destroy_bio_bdev(d_bdev);
		return;
	}

	if (bbs != NULL)
		spdk_thread_send_msg(owner_thread(bbs), teardown_bio_bdev,
				     d_bdev);
}

void
replace_bio_bdev(struct bio_bdev *old_dev, struct bio_bdev *new_dev)
{
	D_ASSERT(old_dev->bb_blobstore != NULL);

	new_dev->bb_blobstore = old_dev->bb_blobstore;
	new_dev->bb_blobstore->bb_dev = new_dev;
	old_dev->bb_blobstore = NULL;

	new_dev->bb_tgt_cnt = old_dev->bb_tgt_cnt;
	old_dev->bb_tgt_cnt = 0;

	if (old_dev->bb_removed) {
		d_list_del_init(&old_dev->bb_link);
		destroy_bio_bdev(old_dev);
	} else {
		old_dev->bb_faulty = true;
	}
}

/*
 * Create bio_bdev from SPDK bdev. It checks if the bdev has existing
 * blobstore, if it doesn't have, it'll create one automatically.
 *
 * This function is only called by 'Init' xstream on server start or
 * a device is hot plugged, so it has to do self poll since the poll
 * xstream for this device hasn't been established yet.
 */
static int
create_bio_bdev(struct bio_xs_context *ctxt, const char *bdev_name,
		struct bio_bdev **dev_out)
{
	struct bio_bdev			*d_bdev, *old_dev;
	struct spdk_blob_store		*bs = NULL;
	struct spdk_bs_type		 bstype;
	struct smd_dev_info		*dev_info;
	uuid_t				 bs_uuid;
	int				 rc;
	bool				 new_bs = false;

	/*
	 * SPDK guarantees uniqueness of bdev name. When a device is hot
	 * removed then plugged back to same slot, a new bdev with different
	 * name will be generated.
	 */
	d_bdev = lookup_dev_by_name(bdev_name);
	if (d_bdev != NULL) {
		D_ERROR("Device %s is already created\n", bdev_name);
		return -DER_EXIST;
	}

	D_ALLOC_PTR(d_bdev);
	if (d_bdev == NULL) {
		D_ERROR("failed to allocate bio_bdev\n");
		return -DER_NOMEM;
	}

	D_INIT_LIST_HEAD(&d_bdev->bb_link);
	D_STRNDUP(d_bdev->bb_name, bdev_name, strlen(bdev_name));
	if (d_bdev->bb_name == NULL) {
		D_ERROR("Failed to allocate bdev name for %s\n", bdev_name);
		rc = -DER_NOMEM;
		goto error;
	}

	/*
	 * Hold the SPDK bdev by an open descriptor, otherwise, the bdev
	 * could be deconstructed by SPDK on device hot remove.
	 */
	rc = spdk_bdev_open_ext(d_bdev->bb_name, false, bio_bdev_event_cb,
				d_bdev, &d_bdev->bb_desc);
	if (rc != 0) {
		D_ERROR("Failed to hold bdev %s, %d\n", d_bdev->bb_name, rc);
		rc = daos_errno2der(-rc);
		goto error;
	}

	D_ASSERT(d_bdev->bb_desc != NULL);
	/* Try to load blobstore without specifying 'bstype' first */
	bs = load_blobstore(ctxt, d_bdev->bb_name, NULL, false, false,
			    NULL, NULL);
	if (bs == NULL) {
		D_DEBUG(DB_MGMT, "Creating bs for %s\n", d_bdev->bb_name);

		/* Create blobstore if it wasn't created before */
		uuid_generate(bs_uuid);
		bs = load_blobstore(ctxt, d_bdev->bb_name, &bs_uuid, true,
				    false, NULL, NULL);
		if (bs == NULL) {
			D_ERROR("Failed to create blobstore on dev: "
				""DF_UUID"\n", DP_UUID(bs_uuid));
			rc = -DER_INVAL;
			goto error;
		}
		new_bs = true;
	}

	/* Get the 'bstype' (device ID) of blobstore */
	bstype = spdk_bs_get_bstype(bs);
	memcpy(bs_uuid, bstype.bstype, sizeof(bs_uuid));
	D_DEBUG(DB_MGMT, "%s :"DF_UUID"\n",
		new_bs ? "Created new blobstore" : "Loaded blobstore",
		DP_UUID(bs_uuid));

	rc = unload_blobstore(ctxt, bs);
	if (rc != 0) {
		D_ERROR("Unable to unload blobstore\n");
		goto error;
	}

	/* Verify if the blobstore was created by DAOS */
	if (uuid_is_null(bs_uuid)) {
		D_ERROR("The bdev has old blobstore not created by DAOS!\n");
		rc = -DER_INVAL;
		goto error;
	}

	uuid_copy(d_bdev->bb_uuid, bs_uuid);
	/* Verify if any duplicated device ID */
	old_dev = lookup_dev_by_id(bs_uuid);
	if (old_dev != NULL) {
		/* If it's in server xstreams start phase, report error */
		if (!is_server_started()) {
			D_ERROR("Dup device "DF_UUID" detected!\n",
				DP_UUID(bs_uuid));
			rc = -DER_EXIST;
			goto error;
		}
		/* Old device is plugged back */
		D_INFO("Device "DF_UUID" is plugged back\n", DP_UUID(bs_uuid));

		if (old_dev->bb_desc != NULL) {
			D_INFO("Device "DF_UUID"(%s) isn't torndown\n",
			       DP_UUID(old_dev->bb_uuid), old_dev->bb_name);
			destroy_bio_bdev(d_bdev);
		} else {
			D_ASSERT(old_dev->bb_removed);
			replace_bio_bdev(old_dev, d_bdev);
			d_list_add(&d_bdev->bb_link, &nvme_glb.bd_bdevs);
			/* Inform caller to trigger device setup */
			D_ASSERT(dev_out != NULL);
			*dev_out = d_bdev;
		}

		return 0;
	}

	/* Find the initial target count per device */
	rc = smd_dev_get_by_id(bs_uuid, &dev_info);
	if (rc == 0) {
		D_ASSERT(dev_info->sdi_tgt_cnt != 0);
		d_bdev->bb_tgt_cnt = dev_info->sdi_tgt_cnt;
		smd_dev_free_info(dev_info);
		/*
		 * Something went wrong in hotplug case: device ID is in SMD
		 * but bio_bdev wasn't created on server start.
		 */
		if (is_server_started()) {
			D_ERROR("bio_bdev for "DF_UUID" wasn't created?\n",
				DP_UUID(bs_uuid));
			rc = -DER_INVAL;
			goto error;
		}
	} else if (rc == -DER_NONEXIST) {
		/* Device isn't in SMD, not used by DAOS yet */
		d_bdev->bb_tgt_cnt = 0;
	} else {
		D_ERROR("Unable to get dev info for "DF_UUID"\n",
			DP_UUID(bs_uuid));
		goto error;
	}
	D_DEBUG(DB_MGMT, "Initial target count for "DF_UUID" set at %d\n",
		DP_UUID(bs_uuid), d_bdev->bb_tgt_cnt);

	d_list_add(&d_bdev->bb_link, &nvme_glb.bd_bdevs);

	return 0;

error:
	destroy_bio_bdev(d_bdev);
	return rc;
}

static int
init_bio_bdevs(struct bio_xs_context *ctxt)
{
	struct spdk_bdev *bdev;
	int rc = 0;

	D_ASSERT(!is_server_started());
	if (spdk_bdev_first() == NULL) {
		D_ERROR("No SPDK bdevs found!");
		rc = -DER_NONEXIST;
	}

	for (bdev = spdk_bdev_first(); bdev != NULL;
	     bdev = spdk_bdev_next(bdev)) {
		if (nvme_glb.bd_bdev_class != get_bdev_type(bdev))
			continue;

		rc = create_bio_bdev(ctxt, spdk_bdev_get_name(bdev), NULL);
		if (rc)
			break;
	}
	return rc;
}

static void
put_bio_blobstore(struct bio_blobstore *bb, struct bio_xs_context *ctxt)
{
	struct spdk_blob_store	*bs = NULL;
	struct bio_io_context	*ioc, *tmp;
	int			i, xs_cnt_max = BIO_XS_CNT_MAX;

	d_list_for_each_entry_safe(ioc, tmp, &ctxt->bxc_io_ctxts, bic_link) {
		d_list_del_init(&ioc->bic_link);
		if (ioc->bic_blob != NULL)
			D_WARN("Pool isn't closed. tgt:%d\n", ctxt->bxc_tgt_id);
	}

	ABT_mutex_lock(bb->bb_mutex);
	/* Unload the blobstore in the same xstream where it was loaded. */
	if (is_bbs_owner(ctxt, bb) && bb->bb_bs != NULL) {
		if (!bb->bb_unloading)
			bs = bb->bb_bs;
		bb->bb_bs = NULL;
	}

	for (i = 0; i < xs_cnt_max; i++) {
		if (bb->bb_xs_ctxts[i] == ctxt) {
			bb->bb_xs_ctxts[i] = NULL;
			break;
		}
	}
	D_ASSERT(i < xs_cnt_max);

	D_ASSERT(bb->bb_ref > 0);
	bb->bb_ref--;

	/* Wait for other xstreams to put_bio_blobstore() first */
	if (bs != NULL && bb->bb_ref)
		ABT_cond_wait(bb->bb_barrier, bb->bb_mutex);
	else if (bb->bb_ref == 0)
		ABT_cond_broadcast(bb->bb_barrier);

	ABT_mutex_unlock(bb->bb_mutex);

	if (bs != NULL) {
		D_ASSERT(bb->bb_holdings == 0);
		unload_blobstore(ctxt, bs);
	}
}

static void
fini_bio_bdevs(struct bio_xs_context *ctxt)
{
	struct bio_bdev *d_bdev, *tmp;

	d_list_for_each_entry_safe(d_bdev, tmp, &nvme_glb.bd_bdevs, bb_link) {
		d_list_del_init(&d_bdev->bb_link);
		destroy_bio_bdev(d_bdev);
	}
}

static struct bio_blobstore *
alloc_bio_blobstore(struct bio_xs_context *ctxt, struct bio_bdev *d_bdev)
{
	struct bio_blobstore	*bb;
	int			 rc, xs_cnt_max = BIO_XS_CNT_MAX;

	D_ASSERT(ctxt != NULL);
	D_ALLOC_PTR(bb);
	if (bb == NULL)
		return NULL;

	D_ALLOC_ARRAY(bb->bb_xs_ctxts, xs_cnt_max);
	if (bb->bb_xs_ctxts == NULL)
		goto out_bb;

	rc = ABT_mutex_create(&bb->bb_mutex);
	if (rc != ABT_SUCCESS)
		goto out_ctxts;

	rc = ABT_cond_create(&bb->bb_barrier);
	if (rc != ABT_SUCCESS)
		goto out_mutex;

	bb->bb_ref = 0;
	bb->bb_owner_xs = ctxt;
	bb->bb_dev = d_bdev;
	return bb;

out_mutex:
	ABT_mutex_free(&bb->bb_mutex);
out_ctxts:
	D_FREE(bb->bb_xs_ctxts);
out_bb:
	D_FREE(bb);
	return NULL;
}

static struct bio_blobstore *
get_bio_blobstore(struct bio_blobstore *bb, struct bio_xs_context *ctxt)
{
	int	i, xs_cnt_max = BIO_XS_CNT_MAX;

	ABT_mutex_lock(bb->bb_mutex);

	for (i = 0; i < xs_cnt_max; i++) {
		if (bb->bb_xs_ctxts[i] == ctxt) {
			D_ERROR("Dup xstream context!\n");
			ABT_mutex_unlock(bb->bb_mutex);
			return NULL;
		} else if (bb->bb_xs_ctxts[i] == NULL) {
			bb->bb_xs_ctxts[i] = ctxt;
			bb->bb_ref++;
			break;
		}
	}

	ABT_mutex_unlock(bb->bb_mutex);

	if (i == xs_cnt_max) {
		D_ERROR("Too many xstreams per device!\n");
		return NULL;
	}
	return bb;
}

/**
 * Assign a device for target->device mapping. Device chosen will be the device
 * with the least amount of mapped targets(VOS xstreams).
 */
static int
assign_device(int tgt_id)
{
	struct bio_bdev	*d_bdev;
	struct bio_bdev	*chosen_bdev;
	int		 lowest_tgt_cnt, rc;

	D_ASSERT(!d_list_empty(&nvme_glb.bd_bdevs));
	chosen_bdev = d_list_entry(nvme_glb.bd_bdevs.next, struct bio_bdev,
				  bb_link);
	lowest_tgt_cnt = chosen_bdev->bb_tgt_cnt;

	/*
	 * Traverse the list and return the device with the least amount of
	 * mapped targets.
	 */
	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		if (d_bdev->bb_tgt_cnt < lowest_tgt_cnt) {
			lowest_tgt_cnt = d_bdev->bb_tgt_cnt;
			chosen_bdev = d_bdev;
		}
	}

	/* Update mapping for this target in NVMe device table */
	rc = smd_dev_add_tgt(chosen_bdev->bb_uuid, tgt_id);
	if (rc) {
		D_ERROR("Failed to map dev "DF_UUID" to tgt %d. "DF_RC"\n",
			DP_UUID(chosen_bdev->bb_uuid), tgt_id, DP_RC(rc));
		return rc;
	}

	chosen_bdev->bb_tgt_cnt++;

	D_DEBUG(DB_MGMT, "Successfully mapped dev "DF_UUID"/%d to tgt %d\n",
		DP_UUID(chosen_bdev->bb_uuid), chosen_bdev->bb_tgt_cnt, tgt_id);

	return 0;
}

static int
init_blobstore_ctxt(struct bio_xs_context *ctxt, int tgt_id)
{
	struct bio_bdev		*d_bdev;
	struct bio_blobstore	*bbs;
	struct spdk_blob_store	*bs;
	struct smd_dev_info	*dev_info = NULL;
	bool			 assigned = false;
	int			 rc;

	D_ASSERT(ctxt->bxc_blobstore == NULL);
	D_ASSERT(ctxt->bxc_io_channel == NULL);

	if (d_list_empty(&nvme_glb.bd_bdevs)) {
		D_ERROR("No available SPDK bdevs, please check whether "
			"VOS_BDEV_CLASS is set properly.\n");
		return -DER_UNINIT;
	}

	/*
	 * Lookup device mapped to @tgt_id in the per-server metadata,
	 * if found, create blobstore on the mapped device.
	 */
retry:
	rc = smd_dev_get_by_tgt(tgt_id, &dev_info);
	if (rc == -DER_NONEXIST && !assigned) {
		rc = assign_device(tgt_id);
		if (rc)
			return rc;
		assigned = true;
		goto retry;
	} else if (rc) {
		D_ERROR("Failed to get dev for tgt %d. "DF_RC"\n", tgt_id,
			DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_MGMT, "Get dev "DF_UUID" mapped to tgt %d.\n",
		DP_UUID(dev_info->sdi_id), tgt_id);

	/*
	 * Two cases leading to the inconsistency between SMD information and
	 * in-memory bio_bdev list:
	 * 1. The SMD data is stale (server started with new SSD/Target
	 *    configuration but old SMD data are not erased) or corrupted.
	 * 2. The device is not plugged.
	 *
	 * We can't differentiate these two cases for now, so let's just abort
	 * starting and ask admin to plug the device or fix the SMD manually.
	 */
	d_bdev = lookup_dev_by_id(dev_info->sdi_id);
	if (d_bdev == NULL) {
		D_ERROR("Device "DF_UUID" for target %d isn't plugged or the "
			"SMD table is stale/corrupted.\n",
			DP_UUID(dev_info->sdi_id), tgt_id);
		rc = -DER_NONEXIST;
		goto out;
	}

	D_ASSERT(d_bdev->bb_name != NULL);
	/*
	 * If no bbs (BIO blobstore) is attached to the device, attach one and
	 * set current xstream as bbs owner.
	 */
	if (d_bdev->bb_blobstore == NULL) {
		d_bdev->bb_blobstore = alloc_bio_blobstore(ctxt, d_bdev);
		if (d_bdev->bb_blobstore == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
	}

	/* Hold bbs refcount for current xstream */
	ctxt->bxc_blobstore = get_bio_blobstore(d_bdev->bb_blobstore, ctxt);
	if (ctxt->bxc_blobstore == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	bbs = ctxt->bxc_blobstore;

	/*
	 * bbs owner xstream is responsible to initialize monitoring context
	 * and open SPDK blobstore.
	 */
	if (is_bbs_owner(ctxt, bbs)) {
		/* Initialize BS state according to SMD state */
		if (dev_info->sdi_state == SMD_DEV_NORMAL) {
			bbs->bb_state = BIO_BS_STATE_NORMAL;
		} else if (dev_info->sdi_state == SMD_DEV_FAULTY) {
			bbs->bb_state = BIO_BS_STATE_OUT;
		} else {
			D_ERROR("Invalid SMD state:%d\n", dev_info->sdi_state);
			rc = -DER_INVAL;
			goto out;
		}

		/* Initialize health monitor */
		rc = bio_init_health_monitoring(bbs, d_bdev->bb_name);
		if (rc != 0) {
			D_ERROR("BIO health monitor init failed. "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		if (bbs->bb_state == BIO_BS_STATE_OUT)
			goto out;

		/* Load blobstore with bstype specified for sanity check */
		bs = load_blobstore(ctxt, d_bdev->bb_name, &d_bdev->bb_uuid,
				    false, false, NULL, NULL);
		if (bs == NULL) {
			rc = -DER_INVAL;
			goto out;
		}
		bbs->bb_bs = bs;

		D_DEBUG(DB_MGMT, "Loaded bs, tgt_id:%d, xs:%p dev:%s\n",
			tgt_id, ctxt, d_bdev->bb_name);

	}

	if (bbs->bb_state == BIO_BS_STATE_OUT)
		goto out;

	/* Open IO channel for current xstream */
	bs = bbs->bb_bs;
	D_ASSERT(bs != NULL);
	ctxt->bxc_io_channel = spdk_bs_alloc_io_channel(bs);
	if (ctxt->bxc_io_channel == NULL) {
		D_ERROR("Failed to create io channel\n");
		rc = -DER_NOMEM;
		goto out;
	}

out:
	D_ASSERT(dev_info != NULL);
	smd_dev_free_info(dev_info);
	return rc;
}

/*
 * Finalize per-xstream NVMe context and SPDK env.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		N/A
 */
void
bio_xsctxt_free(struct bio_xs_context *ctxt)
{
	int	rc = 0;

	/* NVMe context setup was skipped */
	if (ctxt == NULL)
		return;

	if (ctxt->bxc_io_channel != NULL) {
		spdk_bs_free_io_channel(ctxt->bxc_io_channel);
		ctxt->bxc_io_channel = NULL;
	}

	if (ctxt->bxc_blobstore != NULL) {
		put_bio_blobstore(ctxt->bxc_blobstore, ctxt);

		if (is_bbs_owner(ctxt, ctxt->bxc_blobstore))
			bio_fini_health_monitoring(ctxt->bxc_blobstore);

		ctxt->bxc_blobstore = NULL;
	}

	ABT_mutex_lock(nvme_glb.bd_mutex);
	if (nvme_glb.bd_xstream_cnt > 0)
		nvme_glb.bd_xstream_cnt--;

	if (nvme_glb.bd_init_thread != NULL) {
		if (is_init_xstream(ctxt)) {
			struct common_cp_arg	cp_arg;

			/*
			 * The xstream initialized SPDK env will have to
			 * wait for all other xstreams finalized first.
			 */
			if (nvme_glb.bd_xstream_cnt != 0) {
				D_DEBUG(DB_MGMT, "Init xs waits\n");
				ABT_cond_wait(nvme_glb.bd_barrier,
					      nvme_glb.bd_mutex);
			}

			fini_bio_bdevs(ctxt);

			common_prep_arg(&cp_arg);
			D_DEBUG(DB_MGMT, "Finalizing SPDK subsystems\n");
			spdk_subsystem_fini(common_fini_cb, &cp_arg);
			/*
			 * spdk_subsystem_fini() won't run to completion if
			 * any bdev is held by open blobs, set a timeout as
			 * temporary workaround.
			 */
			rc = xs_poll_completion(ctxt, &cp_arg.cca_inflights,
						9000 /*ms*/);
			D_CDEBUG(rc == 0, DB_MGMT, DLOG_ERR,
				 "SPDK subsystems finalized. "DF_RC"\n",
				 DP_RC(rc));

			nvme_glb.bd_init_thread = NULL;

		} else if (nvme_glb.bd_xstream_cnt == 0) {
			ABT_cond_broadcast(nvme_glb.bd_barrier);
		}
	}

	ABT_mutex_unlock(nvme_glb.bd_mutex);

	if (ctxt->bxc_thread != NULL) {
		D_DEBUG(DB_MGMT, "Finalizing SPDK thread, tgt_id:%d",
			ctxt->bxc_tgt_id);

		/* Don't drain events if spdk_subsystem_fini() timeout */
		while (rc == 0 && !spdk_thread_is_idle(ctxt->bxc_thread))
			spdk_thread_poll(ctxt->bxc_thread, 0, 0);

		D_DEBUG(DB_MGMT, "SPDK thread finalized, tgt_id:%d",
			ctxt->bxc_tgt_id);

		spdk_thread_exit(ctxt->bxc_thread);
		ctxt->bxc_thread = NULL;
	}

	if (ctxt->bxc_dma_buf != NULL) {
		dma_buffer_destroy(ctxt->bxc_dma_buf);
		ctxt->bxc_dma_buf = NULL;
	}

	D_FREE(ctxt);
}

int
bio_xsctxt_alloc(struct bio_xs_context **pctxt, int tgt_id)
{
	struct bio_xs_context	*ctxt;
	char			 th_name[32];
	int			 rc;

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ctxt->bxc_io_ctxts);
	ctxt->bxc_tgt_id = tgt_id;

	/* Skip NVMe context setup if the daos_nvme.conf isn't present */
	if (!bio_nvme_configured()) {
		ctxt->bxc_dma_buf = dma_buffer_create(bio_chk_cnt_init);
		if (ctxt->bxc_dma_buf == NULL) {
			D_FREE(ctxt);
			*pctxt = NULL;
			return -DER_NOMEM;
		}
		*pctxt = ctxt;
		return 0;
	}

	ABT_mutex_lock(nvme_glb.bd_mutex);

	nvme_glb.bd_xstream_cnt++;

	D_INFO("Initialize NVMe context, tgt_id:%d, init_thread:%p\n",
	       tgt_id, nvme_glb.bd_init_thread);

	/*
	 * Register SPDK thread beforehand, it could be used for poll device
	 * admin commands completions and hotplugged events in following
	 * spdk_subsystem_init_from_json_config() call, it also could be used
	 * for blobstore metadata io channel in init_bio_bdevs() call.
	 */
	snprintf(th_name, sizeof(th_name), "daos_spdk_%d", tgt_id);
	ctxt->bxc_thread = spdk_thread_create((const char *)th_name, NULL);
	if (ctxt->bxc_thread == NULL) {
		D_ERROR("failed to alloc SPDK thread\n");
		rc = -DER_NOMEM;
		goto out;
	}
	spdk_set_thread(ctxt->bxc_thread);

	/*
	 * The first started xstream will scan all bdevs and create blobstores,
	 * it's a prequisite for all per-xstream blobstore initialization.
	 */
	if (nvme_glb.bd_init_thread == NULL) {
		struct common_cp_arg cp_arg;

		D_ASSERTF(nvme_glb.bd_xstream_cnt == 1, "%d",
			  nvme_glb.bd_xstream_cnt);

		/* Initialize all registered subsystems: bdev, vmd, copy. */
		common_prep_arg(&cp_arg);
		spdk_subsystem_init_from_json_config(nvme_glb.bd_nvme_conf,
						     SPDK_DEFAULT_RPC_ADDR,
						     subsys_init_cb, &cp_arg,
						     true);
		rc = xs_poll_completion(ctxt, &cp_arg.cca_inflights, 0);
		D_ASSERT(rc == 0);

		if (cp_arg.cca_rc != 0) {
			rc = cp_arg.cca_rc;
			D_ERROR("failed to init bdevs, rc:%d\n", rc);
			goto out;
		}

		/* Continue poll until no more events */
		while (spdk_thread_poll(ctxt->bxc_thread, 0, 0) > 0)
			;
		D_DEBUG(DB_MGMT, "SPDK bdev initialized, tgt_id:%d", tgt_id);

		nvme_glb.bd_init_thread = ctxt->bxc_thread;
		rc = init_bio_bdevs(ctxt);
		if (rc != 0) {
			D_ERROR("failed to init bio_bdevs, "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	}

	/* Initialize per-xstream blobstore context */
	rc = init_blobstore_ctxt(ctxt, tgt_id);
	if (rc)
		goto out;

	ctxt->bxc_dma_buf = dma_buffer_create(bio_chk_cnt_init);
	if (ctxt->bxc_dma_buf == NULL) {
		D_ERROR("failed to initialize dma buffer\n");
		rc = -DER_NOMEM;
		goto out;
	}
out:
	ABT_mutex_unlock(nvme_glb.bd_mutex);
	if (rc != 0)
		bio_xsctxt_free(ctxt);

	*pctxt = (rc != 0) ? NULL : ctxt;
	return rc;
}

int
bio_nvme_ctl(unsigned int cmd, void *arg)
{
	int	rc = 0;

	switch (cmd) {
	case BIO_CTL_NOTIFY_STARTED:
		ABT_mutex_lock(nvme_glb.bd_mutex);
		nvme_glb.bd_started = *((bool *)arg);
		ABT_mutex_unlock(nvme_glb.bd_mutex);
		break;
	default:
		D_ERROR("Invalid ctl cmd %d\n", cmd);
		rc = -DER_INVAL;
		break;
	}
	return rc;
}

void
setup_bio_bdev(void *arg)
{
	struct smd_dev_info	*dev_info;
	struct bio_bdev		*d_bdev = arg;
	struct bio_blobstore	*bbs = d_bdev->bb_blobstore;
	int			 rc;

	if (!is_server_started()) {
		D_INFO("Skip device setup on server start/shutdown\n");
		return;
	}

	D_ASSERT(bbs->bb_state == BIO_BS_STATE_OUT);

	rc = smd_dev_get_by_id(d_bdev->bb_uuid, &dev_info);
	if (rc != 0) {
		D_ERROR("Original dev "DF_UUID" not in SMD. "DF_RC"\n",
			DP_UUID(d_bdev->bb_uuid), DP_RC(rc));
		return;
	}

	if (dev_info->sdi_state == SMD_DEV_FAULTY) {
		D_INFO("Faulty dev "DF_UUID" is plugged back\n",
		       DP_UUID(d_bdev->bb_uuid));
		goto out;
	} else if (dev_info->sdi_state != SMD_DEV_NORMAL) {
		D_ERROR("Invalid dev state %d\n", dev_info->sdi_state);
		goto out;
	}

	rc = bio_bs_state_set(bbs, BIO_BS_STATE_SETUP);
	D_ASSERT(rc == 0);
out:
	smd_dev_free_info(dev_info);
}

/*
 * Scan the SPDK bdev list and compare it with bio_bdev list to see if any
 * device is hot plugged. This function is periodically called by the 'init'
 * xstream, be careful on using mutex or any blocking functions, that could
 * block the NVMe poll and lead to deadlock at the end.
 */
static void
scan_bio_bdevs(struct bio_xs_context *ctxt, uint64_t now)
{
	struct bio_blobstore	*bbs;
	struct bio_bdev		*d_bdev, *tmp;
	struct spdk_bdev	*bdev;
	static uint64_t		 scan_period = NVME_MONITOR_PERIOD;
	int			 rc;

	if (nvme_glb.bd_scan_age + scan_period >= now)
		return;

	/* Iterate SPDK bdevs to detect hot plugged device */
	for (bdev = spdk_bdev_first(); bdev != NULL;
	     bdev = spdk_bdev_next(bdev)) {
		if (nvme_glb.bd_bdev_class != get_bdev_type(bdev))
			continue;

		d_bdev = lookup_dev_by_name(spdk_bdev_get_name(bdev));
		if (d_bdev != NULL)
			continue;

		D_INFO("Detected hot plugged device %s\n",
		       spdk_bdev_get_name(bdev));
		/* Print a console message */
		D_PRINT("Detected hot plugged device %s\n",
			spdk_bdev_get_name(bdev));

		scan_period = 0;

		rc = create_bio_bdev(ctxt, spdk_bdev_get_name(bdev), &d_bdev);
		if (rc) {
			D_ERROR("Failed to init hot plugged device %s\n",
				spdk_bdev_get_name(bdev));
			break;
		}

		/*
		 * The plugged device is a new device, or teardown procedure for
		 * old bio_bdev isn't finished.
		 */
		if (d_bdev == NULL)
			continue;

		D_ASSERT(d_bdev->bb_desc != NULL);
		bbs = d_bdev->bb_blobstore;
		/* The device isn't used by DAOS yet */
		if (bbs == NULL) {
			D_INFO("New device "DF_UUID" is plugged back\n",
			       DP_UUID(d_bdev->bb_uuid));
			continue;
		}

		spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, d_bdev);
	}

	/* Iterate bio_bdev list to trigger teardown on hot removed device */
	d_list_for_each_entry_safe(d_bdev, tmp, &nvme_glb.bd_bdevs, bb_link) {
		/* Device isn't removed */
		if (!d_bdev->bb_removed)
			continue;

		bbs = d_bdev->bb_blobstore;
		/* Device not used by DAOS */
		if (bbs == NULL && !d_bdev->bb_replacing) {
			D_DEBUG(DB_MGMT, "Removed device "DF_UUID"(%s)\n",
				DP_UUID(d_bdev->bb_uuid), d_bdev->bb_name);
			d_list_del_init(&d_bdev->bb_link);
			destroy_bio_bdev(d_bdev);
			continue;
		}

		/* Device is already torndown */
		if (d_bdev->bb_desc == NULL)
			continue;

		scan_period = 0;
		if (bbs != NULL)
			spdk_thread_send_msg(owner_thread(bbs),
					     teardown_bio_bdev, d_bdev);
	}

	if (scan_period == 0)
		scan_period = NVME_MONITOR_SHORT_PERIOD;
	else
		scan_period = NVME_MONITOR_PERIOD;

	nvme_glb.bd_scan_age = now;
}

void
bio_led_event_monitor(struct bio_xs_context *ctxt, uint64_t now)
{
	struct bio_bdev         *d_bdev;

	/*
	 * Check VMD_LED_PERIOD environment variable, if not set use default
	 * NVME_MONITOR_PERIOD of 60 seconds.
	 */
	if (vmd_led_period == 0)
		vmd_led_period = NVME_MONITOR_PERIOD;

	/* Scan all devices present in bio_bdev list */
	d_list_for_each_entry(d_bdev, bio_bdev_list(), bb_link) {
		if (d_bdev->bb_led_start_time != 0) {
			if (d_bdev->bb_led_start_time + vmd_led_period >= now)
				continue;

			if (bio_set_led_state(ctxt, d_bdev->bb_uuid, NULL,
					      true/*reset*/) != 0)
				D_ERROR("Failed resetting LED state\n");
		}
	}
}

/*
 * Execute the messages on msg ring, call all registered pollers.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		0: If mo work was done
 *			1: If work was done
 *			-1: If thread has exited
 */
int
bio_nvme_poll(struct bio_xs_context *ctxt)
{
	uint64_t now = d_timeus_secdiff(0);
	int rc;

	/* NVMe context setup was skipped */
	if (!bio_nvme_configured())
		return 0;

	D_ASSERT(ctxt != NULL && ctxt->bxc_thread != NULL);
	rc = spdk_thread_poll(ctxt->bxc_thread, 0, 0);

	/*
	 * To avoid complicated race handling (init xstream and starting
	 * VOS xstream concurrently access global device list & xstream
	 * context array), we just simply disable faulty device detection
	 * and hot remove/plug processing during server start/shutdown.
	 */
	if (!is_server_started())
		return 0;

	/*
	 * Query and print the SPDK device health stats for only the device
	 * owner xstream.
	 */
	if (ctxt->bxc_blobstore != NULL &&
	    is_bbs_owner(ctxt, ctxt->bxc_blobstore))
		bio_bs_monitor(ctxt, now);

	if (is_init_xstream(ctxt)) {
		scan_bio_bdevs(ctxt, now);
		bio_led_event_monitor(ctxt, now);
	}

	return rc;
}
