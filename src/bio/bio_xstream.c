/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>
#include <abt.h>
#include <spdk/log.h>
#include <spdk/env.h>
#include <spdk/init.h>
#include <spdk/nvme.h>
#include <spdk/vmd.h>
#include <spdk/thread.h>
#include <spdk/bdev.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>
#include <spdk/rpc.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

#include "smd.pb-c.h"

/* These Macros should be turned into DAOS configuration in the future */
#define DAOS_MSG_RING_SZ	4096
/* SPDK blob parameters */
#define DAOS_BS_CLUSTER_SZ	(1ULL << 25)	/* 32MB */
/* DMA buffer parameters */
#define DAOS_DMA_CHUNK_MB	8	/* 8MB DMA chunks */
#define DAOS_DMA_CHUNK_CNT_INIT	24	/* Per-xstream init chunks, 192MB */
#define DAOS_DMA_CHUNK_CNT_MAX	128	/* Per-xstream max chunks, 1GB */
#define DAOS_DMA_CHUNK_CNT_MIN	32	/* Per-xstream min chunks, 256MB */

/* Max in-flight blob IOs per io channel */
#define BIO_BS_MAX_CHANNEL_OPS	(4096)
/* Schedule a NVMe poll when so many blob IOs queued for an io channel */
#define BIO_BS_POLL_WATERMARK	(2048)
/* Stop issuing new IO when queued blob IOs reach a threshold */
#define BIO_BS_STOP_WATERMARK	(4000)

/* Chunk size of DMA buffer in pages */
unsigned int bio_chk_sz;
/* Per-xstream maximum DMA buffer size (in chunk count) */
unsigned int bio_chk_cnt_max;
/* NUMA node affinity */
unsigned int bio_numa_node;
/* Per-xstream initial DMA buffer size (in chunk count) */
static unsigned int bio_chk_cnt_init;
/* Diret RDMA over SCM */
bool bio_scm_rdma;
/* Whether SPDK inited */
bool bio_spdk_inited;
/* SPDK subsystem fini timeout */
unsigned int bio_spdk_subsys_timeout = 25000;	/* ms */
/* How many blob unmap calls can be called in a row */
unsigned int bio_spdk_max_unmap_cnt = 32;
unsigned int bio_max_async_sz = (1UL << 20) /* 1MB */;

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
	char			*bd_nvme_conf;
	/* When using SPDK primary mode, specifies memory allocation in MB */
	int			 bd_mem_size;
	unsigned int		 bd_nvme_roles;
	bool			 bd_started;
	bool			 bd_bypass_health_collect;
	/* Setting to enable SPDK JSON-RPC server */
	bool			 bd_enable_rpc_srv;
	const char		*bd_rpc_srv_addr;
};

static struct bio_nvme_data nvme_glb;

static int
bio_spdk_env_init(void)
{
	struct spdk_env_opts	opts;
	bool			enable_rpc_srv = false;
	int			rc;
	int			roles = 0;

	/* Only print error and more severe to stderr. */
	spdk_log_set_print_level(SPDK_LOG_ERROR);

	spdk_env_opts_init(&opts);
	opts.name = "daos_engine";
	opts.env_context = (char *)dpdk_cli_override_opts;

	/**
	 * TODO: Set opts.mem_size to nvme_glb.bd_mem_size
	 * Currently we can't guarantee clean shutdown (no hugepages leaked).
	 * Setting mem_size could cause EAL: Not enough memory available error,
	 * and DPDK will fail to initialize.
	 */

	if (bio_nvme_configured(SMD_DEV_TYPE_MAX)) {
		rc = bio_add_allowed_alloc(nvme_glb.bd_nvme_conf, &opts, &roles);
		if (rc != 0) {
			D_ERROR("Failed to add allowed devices to SPDK env, "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
		nvme_glb.bd_nvme_roles = roles;

		rc = bio_set_hotplug_filter(nvme_glb.bd_nvme_conf);
		if (rc != 0) {
			D_ERROR("Failed to set hotplug filter, "DF_RC"\n", DP_RC(rc));
			goto out;
		}

		rc = bio_read_accel_props(nvme_glb.bd_nvme_conf);
		if (rc != 0) {
			D_ERROR("Failed to read acceleration properties, "DF_RC"\n", DP_RC(rc));
			goto out;
		}

		/**
		 * Read flag to indicate whether to enable the SPDK JSON-RPC server and the
		 * socket file address from the JSON config used to initialize SPDK subsystems.
		 */
		rc = bio_read_rpc_srv_settings(nvme_glb.bd_nvme_conf, &enable_rpc_srv,
					       &nvme_glb.bd_rpc_srv_addr);
		if (rc != 0) {
			D_ERROR("Failed to read SPDK JSON-RPC server settings, "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
#ifdef DAOS_BUILD_RELEASE
		if (enable_rpc_srv) {
			D_ERROR("SPDK JSON-RPC server may not be enabled for release builds.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
#endif
		nvme_glb.bd_enable_rpc_srv = enable_rpc_srv;
	}

	rc = spdk_env_init(&opts);
	if (rc != 0) {
		rc = -DER_INVAL; /* spdk_env_init() returns -1 */
		D_ERROR("Failed to initialize SPDK env, "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	spdk_unaffinitize_thread();

	rc = spdk_thread_lib_init(NULL, 0);
	if (rc != 0) {
		rc = -DER_INVAL;
		D_ERROR("Failed to init SPDK thread lib, "DF_RC"\n", DP_RC(rc));
		spdk_env_fini();
	}
out:
	D_FREE(opts.pci_allowed);
	return rc;
}

bool
bypass_health_collect()
{
	return nvme_glb.bd_bypass_health_collect;
}

struct bio_faulty_criteria	glb_criteria;

/* TODO: Make it configurable through control plane */
static inline void
set_faulty_criteria(void)
{
	glb_criteria.fc_enabled = true;
	glb_criteria.fc_max_io_errs = 10;
	/*
	 * FIXME: Don't enable csum error criterion for now, otherwise, targets
	 *	  be unexpectedly down in CSUM tests.
	 */
	glb_criteria.fc_max_csum_errs = UINT32_MAX;

	d_getenv_bool("DAOS_NVME_AUTO_FAULTY_ENABLED", &glb_criteria.fc_enabled);
	d_getenv_int("DAOS_NVME_AUTO_FAULTY_IO", &glb_criteria.fc_max_io_errs);
	d_getenv_int("DAOS_NVME_AUTO_FAULTY_CSUM", &glb_criteria.fc_max_csum_errs);

	D_INFO("NVMe auto faulty is %s. Criteria: max_io_errs:%u, max_csum_errs:%u\n",
	       glb_criteria.fc_enabled ? "enabled" : "disabled",
	       glb_criteria.fc_max_io_errs, glb_criteria.fc_max_csum_errs);
}

int
bio_nvme_init(const char *nvme_conf, int numa_node, unsigned int mem_size,
	      unsigned int hugepage_size, unsigned int tgt_nr, bool bypass_health_collect)
{
	char		*env;
	int		 rc, fd;
	unsigned int	 size_mb = DAOS_DMA_CHUNK_MB;

	if (tgt_nr <= 0) {
		D_ERROR("tgt_nr: %u should be > 0\n", tgt_nr);
		return -DER_INVAL;
	}

	if (nvme_conf && strlen(nvme_conf) > 0 && mem_size == 0) {
		D_ERROR("Hugepages must be configured when NVMe SSD is configured\n");
		return -DER_INVAL;
	}

	bio_numa_node = 0;
	nvme_glb.bd_xstream_cnt = 0;
	nvme_glb.bd_init_thread = NULL;
	nvme_glb.bd_nvme_conf = NULL;
	nvme_glb.bd_bypass_health_collect = bypass_health_collect;
	nvme_glb.bd_enable_rpc_srv = false;
	nvme_glb.bd_rpc_srv_addr = NULL;
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

	d_getenv_int("DAOS_SPDK_SUBSYS_TIMEOUT", &bio_spdk_subsys_timeout);
	D_INFO("SPDK subsystem fini timeout is %u ms\n", bio_spdk_subsys_timeout);

	d_getenv_int("DAOS_SPDK_MAX_UNMAP_CNT", &bio_spdk_max_unmap_cnt);
	if (bio_spdk_max_unmap_cnt == 0)
		bio_spdk_max_unmap_cnt = UINT32_MAX;
	D_INFO("SPDK batch blob unmap call count is %u\n", bio_spdk_max_unmap_cnt);

	d_getenv_int("DAOS_MAX_ASYNC_SZ", &bio_max_async_sz);
	D_INFO("Max async data size is set to %u bytes\n", bio_max_async_sz);

	/* Hugepages disabled */
	if (mem_size == 0) {
		D_INFO("Set per-xstream DMA buffer upper bound to %u %uMB chunks\n",
			bio_chk_cnt_max, size_mb);
		D_INFO("Hugepages are not specified, skip NVMe setup.\n");
		return 0;
	}

	if (nvme_conf && strlen(nvme_conf) > 0) {
		fd = open(nvme_conf, O_RDONLY, 0600);
		if (fd < 0)
			D_WARN("Open %s failed, skip DAOS NVMe setup "DF_RC"\n",
			       nvme_conf, DP_RC(daos_errno2der(errno)));
		else
			close(fd);
	}

	D_ASSERT(hugepage_size > 0);
	bio_chk_cnt_max = (mem_size / tgt_nr) / size_mb;
	if (bio_chk_cnt_max < DAOS_DMA_CHUNK_CNT_MIN) {
		D_ERROR("%uMB hugepages are not enough for %u targets (256MB per target)\n",
			mem_size, tgt_nr);
		return -DER_INVAL;
	}
	D_INFO("Set per-xstream DMA buffer upper bound to %u %uMB chunks\n",
	       bio_chk_cnt_max, size_mb);

	spdk_bs_opts_init(&nvme_glb.bd_bs_opts, sizeof(nvme_glb.bd_bs_opts));
	nvme_glb.bd_bs_opts.cluster_sz = DAOS_BS_CLUSTER_SZ;
	nvme_glb.bd_bs_opts.max_channel_ops = BIO_BS_MAX_CHANNEL_OPS;

	env = getenv("VOS_BDEV_CLASS");
	if (env && strcasecmp(env, "AIO") == 0) {
		D_WARN("AIO device(s) will be used!\n");
		nvme_glb.bd_bdev_class = BDEV_CLASS_AIO;
	}

	if (numa_node > 0) {
		bio_numa_node = (unsigned int)numa_node;
	} else if (numa_node == -1) {
		D_WARN("DMA buffer will be allocated from any NUMA node available\n");
		bio_numa_node = SPDK_ENV_SOCKET_ID_ANY;
	}

	nvme_glb.bd_mem_size = mem_size;
	if (nvme_conf) {
		D_STRNDUP(nvme_glb.bd_nvme_conf, nvme_conf, strlen(nvme_conf));
		if (nvme_glb.bd_nvme_conf == NULL) {
			rc = -DER_NOMEM;
			goto free_cond;
		}
	}

	rc = bio_spdk_env_init();
	if (rc) {
		D_ERROR("Failed to init SPDK environment\n");
		D_FREE(nvme_glb.bd_nvme_conf);
		nvme_glb.bd_nvme_conf = NULL;
		goto free_cond;
	}

	/*
	 * Let's keep using large cluster size(1GB) for pmem mode, the SPDK blobstore
	 * loading time is unexpected long for smaller cluster size(32MB), see DAOS-13694.
	 */
	if (!bio_nvme_configured(SMD_DEV_TYPE_META))
		nvme_glb.bd_bs_opts.cluster_sz = (1UL << 30);	/* 1GB */

	D_INFO("MD on SSD is %s\n",
	       bio_nvme_configured(SMD_DEV_TYPE_META) ? "enabled" : "disabled");

	bio_spdk_inited = true;
	set_faulty_criteria();

	return 0;

free_cond:
	ABT_cond_free(&nvme_glb.bd_barrier);
free_mutex:
	ABT_mutex_free(&nvme_glb.bd_mutex);

	return rc;
}

static void
bio_spdk_env_fini(void)
{
	if (bio_spdk_inited) {
		spdk_thread_lib_fini();
		spdk_env_fini();
		bio_spdk_inited = false;
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
	D_FREE(nvme_glb.bd_nvme_conf);
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

inline uint32_t
default_cluster_sz(void)
{
	return nvme_glb.bd_bs_opts.cluster_sz;
}

bool
bio_need_nvme_poll(struct bio_xs_context *ctxt)
{
	enum smd_dev_type	 st;
	struct bio_xs_blobstore	*bxb;

	if (ctxt == NULL)
		return false;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = ctxt->bxc_xs_blobstores[st];
		if (bxb && bxb->bxb_blob_rw > BIO_BS_POLL_WATERMARK)
			return true;
	}

	return false;
}

void
drain_inflight_ios(struct bio_xs_context *ctxt, struct bio_xs_blobstore *bxb)
{

	if (ctxt == NULL || bxb == NULL || bxb->bxb_blob_rw <= BIO_BS_POLL_WATERMARK)
		return;

	do {
		if (ctxt->bxc_self_polling)
			spdk_thread_poll(ctxt->bxc_thread, 0, 0);
		else
			bio_yield(NULL);
	} while (bxb->bxb_blob_rw >= BIO_BS_STOP_WATERMARK);
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

			if (cur_time > (start_time + timeout))
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
	d_bdev->bb_removed = 1;

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
		old_dev->bb_faulty = 1;
	}
}

int
bdev_name2roles(const char *name)
{
	const char	*dst = strrchr(name, '_');
	char		*ptr_parse_end = NULL;
	unsigned	 int value;

	if (dst == NULL)
		return -DER_NONEXIST;

	dst++;
	value = strtoul(dst, &ptr_parse_end, 0);
	if (ptr_parse_end && *ptr_parse_end != 'n' && *ptr_parse_end != '\0') {
		D_ERROR("invalid numeric value: %s (name %s)\n", dst, name);
		return -DER_INVAL;
	}

	if (value & (~NVME_ROLE_ALL))
		return -DER_INVAL;

	D_INFO("bdev name:%s, bdev role:%u\n", name, value);
	return value;
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
	struct spdk_bdev		*bdev;
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
		return -DER_NOMEM;
	}

	D_INIT_LIST_HEAD(&d_bdev->bb_link);
	rc = bdev_name2roles(bdev_name);
	if (rc < 0) {
		D_ERROR("Failed to get role from bdev name, "DF_RC"\n", DP_RC(rc));
		goto error;
	}

	d_bdev->bb_roles = rc;
	D_STRNDUP(d_bdev->bb_name, bdev_name, strlen(bdev_name));
	if (d_bdev->bb_name == NULL) {
		D_GOTO(error, rc = -DER_NOMEM);
	}

	bdev = spdk_bdev_get_by_name(d_bdev->bb_name);
	D_ASSERT(bdev != NULL);
	d_bdev->bb_unmap_supported = spdk_bdev_io_type_supported(bdev, SPDK_BDEV_IO_TYPE_UNMAP);

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

	D_DEBUG(DB_MGMT, "Create DAOS bdev "DF_UUID", role:%u\n",
		DP_UUID(bs_uuid), d_bdev->bb_roles);

	d_list_add_tail(&d_bdev->bb_link, &nvme_glb.bd_bdevs);

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
		D_ERROR("No SPDK bdevs found!\n");
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
put_bio_blobstore(struct bio_xs_blobstore *bxb, struct bio_xs_context *ctxt)
{
	struct bio_blobstore	*bbs = bxb->bxb_blobstore;
	struct spdk_blob_store	*bs = NULL;
	struct bio_io_context	*ioc, *tmp;
	int			i, xs_cnt_max = BIO_XS_CNT_MAX;

	d_list_for_each_entry_safe(ioc, tmp, &bxb->bxb_io_ctxts, bic_link) {
		d_list_del_init(&ioc->bic_link);
		if (ioc->bic_blob != NULL)
			D_WARN("Pool isn't closed. tgt:%d\n", ctxt->bxc_tgt_id);
	}

	ABT_mutex_lock(bbs->bb_mutex);
	/* Unload the blobstore in the same xstream where it was loaded. */
	if (is_bbs_owner(ctxt, bbs) && bbs->bb_bs != NULL) {
		if (!bbs->bb_unloading)
			bs = bbs->bb_bs;
		bbs->bb_bs = NULL;
	}

	for (i = 0; i < xs_cnt_max; i++) {
		if (bbs->bb_xs_ctxts[i] == ctxt) {
			bbs->bb_xs_ctxts[i] = NULL;
			break;
		}
	}
	D_ASSERT(i < xs_cnt_max);

	D_ASSERT(bbs->bb_ref > 0);
	bbs->bb_ref--;

	/* Wait for other xstreams to put_bio_blobstore() first */
	if (bs != NULL && bbs->bb_ref)
		ABT_cond_wait(bbs->bb_barrier, bbs->bb_mutex);
	else if (bbs->bb_ref == 0)
		ABT_cond_broadcast(bbs->bb_barrier);

	ABT_mutex_unlock(bbs->bb_mutex);

	if (bs != NULL) {
		D_ASSERT(bbs->bb_holdings == 0);
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

static inline unsigned int
dev_type2role(enum smd_dev_type st)
{
	switch (st) {
	case SMD_DEV_TYPE_DATA:
		return NVME_ROLE_DATA;
	case SMD_DEV_TYPE_META:
		return NVME_ROLE_META;
	case SMD_DEV_TYPE_WAL:
		return NVME_ROLE_WAL;
	default:
		D_ASSERT(0);
		return NVME_ROLE_DATA;
	}
}

static inline bool
is_role_match(unsigned int roles, unsigned int req_role)
{
	if (roles == 0)
		return NVME_ROLE_DATA & req_role;

	return roles & req_role;
}

bool
bio_nvme_configured(enum smd_dev_type type)
{
	if (nvme_glb.bd_nvme_conf == NULL)
		return false;

	if (type >= SMD_DEV_TYPE_MAX)
		return true;

	return is_role_match(nvme_glb.bd_nvme_roles, dev_type2role(type));
}

static struct bio_bdev *
choose_device(int tgt_id, enum smd_dev_type st)
{
	struct bio_bdev		*d_bdev;
	struct bio_bdev		*chosen_bdev = NULL;
	int			 lowest_tgt_cnt = 1 << 30, rc;
	struct smd_dev_info	*dev_info = NULL;

	D_ASSERT(!d_list_empty(&nvme_glb.bd_bdevs));
	/*
	 * Traverse the list and return the device with the least amount of
	 * mapped targets.
	 */
	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		/* Find the initial target count per device */
		if (!d_bdev->bb_tgt_cnt_init) {
			rc = smd_dev_get_by_id(d_bdev->bb_uuid, &dev_info);
			if (rc == 0) {
				D_ASSERT(dev_info != NULL && dev_info->sdi_tgt_cnt != 0);
				d_bdev->bb_tgt_cnt = dev_info->sdi_tgt_cnt;
				smd_dev_free_info(dev_info);
			} else if (rc == -DER_NONEXIST) {
				/* Device isn't in SMD, not used by DAOS yet */
				d_bdev->bb_tgt_cnt = 0;
			} else {
				D_ERROR("Unable to get dev info for "DF_UUID"\n",
					DP_UUID(d_bdev->bb_uuid));
				return NULL;
			}
			d_bdev->bb_tgt_cnt_init = 1;
		}
		/* Choose the least used one */
		if (is_role_match(d_bdev->bb_roles, dev_type2role(st)) &&
		    d_bdev->bb_tgt_cnt < lowest_tgt_cnt) {
			lowest_tgt_cnt = d_bdev->bb_tgt_cnt;
			chosen_bdev = d_bdev;
		}
	}

	return chosen_bdev;
}

struct bio_xs_blobstore *
alloc_xs_blobstore(void)
{
	struct bio_xs_blobstore *bxb;

	D_ALLOC_PTR(bxb);
	if (bxb == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&bxb->bxb_io_ctxts);

	return bxb;
}

static int
assign_roles(struct bio_bdev *d_bdev, unsigned int tgt_id)
{
	enum smd_dev_type	st, failed_st;
	bool			assigned = false;
	int			rc;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		if (!is_role_match(d_bdev->bb_roles, dev_type2role(st)))
			continue;

		rc = smd_dev_add_tgt(d_bdev->bb_uuid, tgt_id, st);
		if (rc) {
			D_ERROR("Failed to map dev "DF_UUID" type:%u to tgt %d. "DF_RC"\n",
				DP_UUID(d_bdev->bb_uuid), st, tgt_id, DP_RC(rc));
			failed_st = st;
			goto error;
		}
		assigned = true;
		/*
		 * Now a device will be assigned to SYS_TGT_ID for RDB
		 * (the mapping will be recorded in target table), but we should not
		 * treat the SYS_TGT mapping equally with other VOS targets mappings.
		 *
		 * Let's take an example, if there is a config having 4 meta SSDs and 3 targets,
		 * how should we assign SSDs?
		 *
		 * 1. Assign 3 SSDs to 3 VOS targets and sys target (sys target share SSD with
		 * one of VOS target), leave one SSD unused, or;
		 * 2. Assign 1 SSD to sys target, assign the other 3 SSDs to VOS targets
		 *
		 * We use the 1st policy to assign SSDs and @bb_tgt_cnt won't be increased for
		 * sys tgt id.
		 *
		 */
		if (tgt_id != BIO_SYS_TGT_ID)
			d_bdev->bb_tgt_cnt++;

		D_DEBUG(DB_MGMT, "Successfully mapped dev "DF_UUID"/%d/%u to tgt %d role %u\n",
			DP_UUID(d_bdev->bb_uuid), d_bdev->bb_tgt_cnt, d_bdev->bb_roles,
			tgt_id, dev_type2role(st));

		if (!bio_nvme_configured(SMD_DEV_TYPE_META))
			break;
	}

	return assigned ? 0 : -DER_INVAL;
error:
	for (st = SMD_DEV_TYPE_DATA; st < failed_st; st++) {
		if (!is_role_match(d_bdev->bb_roles, dev_type2role(st)))
			continue;
		/* TODO Error cleanup by smd_dev_del_tgt() */
	}
	return rc;
}

static struct bio_bdev *
assign_xs_bdev(struct bio_xs_context *ctxt, int tgt_id, enum smd_dev_type st,
	       unsigned int *dev_state)
{
	struct bio_bdev		*d_bdev;
	struct smd_dev_info	*dev_info = NULL;
	int			 rc;

	*dev_state = SMD_DEV_NORMAL;
	rc = smd_dev_get_by_tgt(tgt_id, st, &dev_info);
	if (rc == -DER_NONEXIST) {
		d_bdev = choose_device(tgt_id, st);
		if (d_bdev == NULL) {
			D_ERROR("Failed to choose bdev for tgt:%u type:%u\n", tgt_id, st);
			return NULL;
		}

		rc = assign_roles(d_bdev, tgt_id);
		if (rc) {
			D_ERROR("Failed to assign roles. "DF_RC"\n", DP_RC(rc));
			return NULL;
		}

		return d_bdev;
	} else if (rc) {
		D_ERROR("Failed to get device info for tgt:%u type:%u, "DF_RC"\n",
			tgt_id, st, DP_RC(rc));
		return NULL;
	}

	D_ASSERT(dev_info != NULL);
	*dev_state = dev_info->sdi_state;
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
	if (d_bdev == NULL)
		D_ERROR("Device "DF_UUID" for target %d type %d isn't plugged or the "
			"SMD table is stale/corrupted.\n",
			DP_UUID(dev_info->sdi_id), tgt_id, st);
	smd_dev_free_info(dev_info);

	return d_bdev;
}

static int
init_xs_blobstore_ctxt(struct bio_xs_context *ctxt, int tgt_id, enum smd_dev_type st)
{
	struct bio_bdev		*d_bdev;
	struct bio_blobstore	*bbs;
	struct spdk_blob_store	*bs;
	struct bio_xs_blobstore	*bxb;
	unsigned int		 dev_state;
	int			 rc;

	D_ASSERT(!ctxt->bxc_ready);
	D_ASSERT(ctxt->bxc_xs_blobstores[st] == NULL);

	if (d_list_empty(&nvme_glb.bd_bdevs)) {
		D_ERROR("No available SPDK bdevs, please check whether "
			"VOS_BDEV_CLASS is set properly.\n");
		return -DER_UNINIT;
	}

	ctxt->bxc_xs_blobstores[st] = alloc_xs_blobstore();
	if (ctxt->bxc_xs_blobstores[st] == NULL) {
		D_ERROR("Failed to allocate memory for xs blobstore\n");
		return -DER_NOMEM;
	}

	d_bdev = assign_xs_bdev(ctxt, tgt_id, st, &dev_state);
	if (d_bdev == NULL)
		return -DER_NONEXIST;

	D_ASSERT(d_bdev->bb_name != NULL);
	/*
	 * If no bbs (BIO blobstore) is attached to the device, attach one and
	 * set current xstream as bbs owner.
	 */
	if (d_bdev->bb_blobstore == NULL) {
		d_bdev->bb_blobstore = alloc_bio_blobstore(ctxt, d_bdev);
		if (d_bdev->bb_blobstore == NULL)
			return -DER_NOMEM;
	}

	bxb = ctxt->bxc_xs_blobstores[st];
	/* Hold bbs refcount for current xstream */
	bxb->bxb_blobstore = get_bio_blobstore(d_bdev->bb_blobstore, ctxt);
	if (bxb->bxb_blobstore == NULL)
		return -DER_NOMEM;

	bbs = bxb->bxb_blobstore;

	/*
	 * bbs owner xstream is responsible to initialize monitoring context
	 * and open SPDK blobstore.
	 */
	if (is_bbs_owner(ctxt, bbs)) {
		/* Initialize BS state according to SMD state */
		if (dev_state == SMD_DEV_NORMAL) {
			bbs->bb_state = BIO_BS_STATE_NORMAL;
		} else if (dev_state == SMD_DEV_FAULTY) {
			bbs->bb_state = BIO_BS_STATE_OUT;
		} else {
			D_ERROR("Invalid SMD state:%d\n", dev_state);
			return -DER_INVAL;
		}

		/* Initialize health monitor */
		rc = bio_init_health_monitoring(bbs, d_bdev->bb_name);
		if (rc != 0) {
			D_ERROR("BIO health monitor init failed. "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		if (bbs->bb_state == BIO_BS_STATE_OUT)
			return 0;

		/* Load blobstore with bstype specified for sanity check */
		bs = load_blobstore(ctxt, d_bdev->bb_name, &d_bdev->bb_uuid,
				    false, false, NULL, NULL);
		if (bs == NULL)
			return -DER_INVAL;
		bbs->bb_bs = bs;

		D_DEBUG(DB_MGMT, "Loaded bs, tgt_id:%d, xs:%p dev:%s\n",
			tgt_id, ctxt, d_bdev->bb_name);
	}

	if (bbs->bb_state == BIO_BS_STATE_OUT)
		return 0;

	/* Open IO channel for current xstream */
	bs = bbs->bb_bs;
	D_ASSERT(bs != NULL);
	D_ASSERT(bxb->bxb_io_channel == NULL);
	bxb->bxb_io_channel = spdk_bs_alloc_io_channel(bs);
	if (bxb->bxb_io_channel == NULL) {
		D_ERROR("Failed to create io channel\n");
		return -DER_NOMEM;
	}

	return 0;
}

static void
bio_blobstore_free(struct bio_xs_blobstore *bxb, struct bio_xs_context *ctxt)
{

	struct bio_blobstore *bbs = bxb->bxb_blobstore;

	if (bbs == NULL)
		return;

	put_bio_blobstore(bxb, ctxt);
	if (is_bbs_owner(ctxt, bbs))
		bio_fini_health_monitoring(ctxt, bbs);
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
	int			 rc = 0;
	enum smd_dev_type	 st;
	struct bio_xs_blobstore	*bxb;

	/* NVMe context setup was skipped */
	if (ctxt == NULL)
		return;

	ctxt->bxc_ready = 0;
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = ctxt->bxc_xs_blobstores[st];
		if (bxb == NULL)
			continue;

		if (bxb->bxb_io_channel != NULL) {
			spdk_bs_free_io_channel(bxb->bxb_io_channel);
			bxb->bxb_io_channel = NULL;
		}

		/*
		 * Clear bxc_xs_blobstore[st] before bio_blobstore_free() to prevent the health
		 * monitor from issuing health data collecting request, see cb_arg2dev_health().
		 */
		ctxt->bxc_xs_blobstores[st] = NULL;

		if (bxb->bxb_blobstore != NULL) {
			bio_blobstore_free(bxb, ctxt);
			bxb->bxb_blobstore = NULL;
		}
		D_FREE(bxb);
	}

	ABT_mutex_lock(nvme_glb.bd_mutex);
	if (nvme_glb.bd_xstream_cnt > 0)
		nvme_glb.bd_xstream_cnt--;

	if (nvme_glb.bd_init_thread != NULL) {
		if (is_init_xstream(ctxt)) {
			struct common_cp_arg	cp_arg;

			/* Close SPDK JSON-RPC server if it has been enabled. */
			if (nvme_glb.bd_enable_rpc_srv)
				spdk_rpc_finish();

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
						bio_spdk_subsys_timeout);
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
		while (rc == 0 && !spdk_thread_is_exited(ctxt->bxc_thread))
			spdk_thread_poll(ctxt->bxc_thread, 0, 0);
		spdk_thread_destroy(ctxt->bxc_thread);
		ctxt->bxc_thread = NULL;
	}

	if (ctxt->bxc_dma_buf != NULL) {
		dma_buffer_destroy(ctxt->bxc_dma_buf);
		ctxt->bxc_dma_buf = NULL;
	}

	D_FREE(ctxt);
}

int
bio_xsctxt_alloc(struct bio_xs_context **pctxt, int tgt_id, bool self_polling)
{
	struct bio_xs_context	*ctxt;
	struct bio_xs_blobstore	*bxb;
	struct bio_blobstore	*bbs;
	struct bio_bdev		*d_bdev;
	char			 th_name[32];
	int			 rc = 0;
	enum smd_dev_type	 st;

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	ctxt->bxc_tgt_id = tgt_id;
	ctxt->bxc_self_polling = self_polling;

	/* Skip NVMe context setup if the daos_nvme.conf isn't present */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX)) {
		ctxt->bxc_dma_buf = dma_buffer_create(bio_chk_cnt_init, tgt_id);
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
	 * it's a prerequisite for all per-xstream blobstore initialization.
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

		/* After bio_bdevs are initialized, restart SPDK JSON-RPC server if required. */
		if (nvme_glb.bd_enable_rpc_srv) {
			if ((!nvme_glb.bd_rpc_srv_addr) || (strlen(nvme_glb.bd_rpc_srv_addr) == 0))
				nvme_glb.bd_rpc_srv_addr = SPDK_DEFAULT_RPC_ADDR;

			rc = spdk_rpc_initialize(nvme_glb.bd_rpc_srv_addr);
			if (rc != 0) {
				D_ERROR("failed to start SPDK JSON-RPC server at %s, "DF_RC"\n",
					nvme_glb.bd_rpc_srv_addr, DP_RC(daos_errno2der(-rc)));
				goto out;
			}

			/* Set SPDK JSON-RPC server state to receive and process RPCs */
			spdk_rpc_set_state(SPDK_RPC_RUNTIME);
			D_DEBUG(DB_MGMT, "SPDK JSON-RPC server listening at %s\n",
				nvme_glb.bd_rpc_srv_addr);
		}
	}

	d_bdev = NULL;
	/* Initialize per-xstream blobstore context */
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		/* No Data blobstore for sys xstream */
		if (st == SMD_DEV_TYPE_DATA && tgt_id == BIO_SYS_TGT_ID)
			continue;
		/* Share the same device/blobstore used by previous type */
		if (d_bdev && is_role_match(d_bdev->bb_roles, dev_type2role(st)))
			continue;
		/* No Meta/WAL blobstore if Metadata on SSD is not configured */
		if (st != SMD_DEV_TYPE_DATA && !bio_nvme_configured(SMD_DEV_TYPE_META))
			break;

		rc = init_xs_blobstore_ctxt(ctxt, tgt_id, st);
		if (rc)
			goto out;

		bxb = ctxt->bxc_xs_blobstores[st];
		D_ASSERT(bxb != NULL);
		bbs = bxb->bxb_blobstore;
		D_ASSERT(bbs != NULL);
		d_bdev = bbs->bb_dev;
		D_ASSERT(d_bdev != NULL);
	}

	ctxt->bxc_dma_buf = dma_buffer_create(bio_chk_cnt_init, tgt_id);
	if (ctxt->bxc_dma_buf == NULL) {
		D_ERROR("failed to initialize dma buffer\n");
		rc = -DER_NOMEM;
		goto out;
	}
	ctxt->bxc_ready = 1;

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

		/* don't support hot plug for sys device yet */
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
	struct bio_bdev		*d_bdev;
	unsigned int		 led_state;
	int			 rc;

	/* Scan all devices present in bio_bdev list */
	d_list_for_each_entry(d_bdev, bio_bdev_list(), bb_link) {
		if ((d_bdev->bb_led_expiry_time != 0) && (d_bdev->bb_led_expiry_time < now)) {
			D_DEBUG(DB_MGMT, "Clearing LED QUICK_BLINK state for "DF_UUID"\n",
				DP_UUID(d_bdev->bb_uuid));

			/* LED will be reset to faulty or normal state based on SSDs bio_bdevs */
			rc = bio_led_manage(ctxt, NULL, d_bdev->bb_uuid,
					    (unsigned int)CTL__LED_ACTION__RESET, &led_state, 0);
			if (rc != 0)
				D_ERROR("Reset LED identify state after timeout failed on device:"
					DF_UUID", "DF_RC"\n", DP_UUID(d_bdev->bb_uuid), DP_RC(rc));
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
	uint64_t		 now = d_timeus_secdiff(0);
	enum smd_dev_type	 st;
	int			 rc;
	struct bio_xs_blobstore	*bxb;

	/* NVMe context setup was skipped */
	if (!bio_nvme_configured(SMD_DEV_TYPE_MAX))
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
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		bxb = ctxt->bxc_xs_blobstores[st];
		if (bxb && bxb->bxb_blobstore &&
		    is_bbs_owner(ctxt, bxb->bxb_blobstore))
			bio_bs_monitor(ctxt, st, now);
	}

	if (is_init_xstream(ctxt)) {
		scan_bio_bdevs(ctxt, now);
		bio_led_event_monitor(ctxt, now);
	}

	return rc;
}
