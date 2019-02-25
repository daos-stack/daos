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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>
#include <abt.h>
#include <spdk/env.h>
#include <spdk/bdev.h>
#include <spdk/io_channel.h>
#include <spdk/blob_bdev.h>
#include <spdk/blob.h>
#include <spdk/copy_engine.h>
#include <spdk/conf.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

/* These Macros should be turned into DAOS configuration in the future */
#define DAOS_MSG_RING_SZ	4096
/* SPDK blob parameters */
#define DAOS_BS_CLUSTER_SZ	(1ULL << 30)	/* 1GB */
#define DAOS_BS_MD_PAGES	(1024 * 20)	/* 20k blobs per device */
/* DMA buffer parameters */
#define DAOS_DMA_CHUNK_MB	32		/* 32MB DMA chunks */
#define DAOS_DMA_CHUNK_CNT_INIT	2		/* Per-xstream init chunks */
#define DAOS_DMA_CHUNK_CNT_MAX	32		/* Per-xstream max chunks */

enum {
	BDEV_CLASS_NVME = 0,
	BDEV_CLASS_MALLOC,
	BDEV_CLASS_AIO,
	BDEV_CLASS_UNKNOWN
};

/* Chunk size of DMA buffer in pages */
unsigned int bio_chk_sz;
/* Per-xstream maximum DMA buffer size (in chunk count) */
unsigned int bio_chk_cnt_max;
/* Per-xstream initial DMA buffer size (in chunk count) */
static unsigned int bio_chk_cnt_init;

struct bio_bdev {
	d_list_t		 bb_link;
	uuid_t			 bb_uuid;
	struct spdk_bdev	*bb_bdev;
	struct bio_blobstore	*bb_blobstore;
	int			 bb_xs_cnt; /* count of xstreams per device */
};

struct bio_nvme_data {
	ABT_mutex		 bd_mutex;
	ABT_cond		 bd_barrier;
	/* SPDK bdev type */
	int			 bd_bdev_class;
	/* How many xstreams has intialized NVMe context */
	int			 bd_xstream_cnt;
	/* The thread responsible for SPDK bdevs init/fini */
	struct spdk_thread	*bd_init_thread;
	/* Default SPDK blobstore options */
	struct spdk_bs_opts	 bd_bs_opts;
	/* All bdevs can be used by DAOS server */
	d_list_t		 bd_bdevs;
	char			*bd_nvme_conf;
	int			 bd_shm_id;
};

static struct bio_nvme_data nvme_glb;
static uint64_t io_stat_period;

/* Print the io stat every few seconds, for debug only */
static void
print_io_stat(struct bio_xs_context *ctxt, uint64_t now)
{
	struct spdk_bdev_io_stat	 stat;
	struct spdk_bdev		*bdev;
	struct spdk_io_channel		*channel;

	if (io_stat_period == 0)
		return;

	if (ctxt->bxc_stat_age + io_stat_period >= now)
		return;

	if (ctxt->bxc_desc != NULL) {
		channel = spdk_bdev_get_io_channel(ctxt->bxc_desc);
		D_ASSERT(channel != NULL);
		spdk_bdev_get_io_stat(NULL, channel, &stat);
		spdk_put_io_channel(channel);

		bdev = spdk_bdev_desc_get_bdev(ctxt->bxc_desc);

		D_PRINT("SPDK IO STAT: xs_id[%d] dev[%s] read_bytes["DF_U64"], "
			"read_ops["DF_U64"], write_bytes["DF_U64"], "
			"write_ops["DF_U64"], read_latency_ticks["DF_U64"], "
			"write_latency_ticks["DF_U64"]\n",
			ctxt->bxc_xs_id, spdk_bdev_get_name(bdev),
			stat.bytes_read, stat.num_read_ops, stat.bytes_written,
			stat.num_write_ops, stat.read_latency_ticks,
			stat.write_latency_ticks);
	}

	ctxt->bxc_stat_age = now;
}

int
bio_nvme_init(const char *storage_path, const char *nvme_conf, int shm_id)
{
	char		*env;
	int		rc, fd;
	uint64_t	size_mb = DAOS_DMA_CHUNK_MB;

	rc = smd_create_initialize(storage_path, NULL, -1);
	if (rc != 0) {
		D_ERROR("Error creating server metadata store: %d\n", rc);
		return rc;
	}

	nvme_glb.bd_xstream_cnt = 0;
	nvme_glb.bd_init_thread = NULL;
	D_INIT_LIST_HEAD(&nvme_glb.bd_bdevs);

	rc = ABT_mutex_create(&nvme_glb.bd_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto fini_smd;
	}

	rc = ABT_cond_create(&nvme_glb.bd_barrier);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto free_mutex;
	}

	fd = open(nvme_conf, O_RDONLY, 0600);
	if (fd < 0) {
		D_WARN("Open %s failed(%d), skip DAOS NVMe setup.\n",
		       nvme_conf, daos_errno2der(errno));
		nvme_glb.bd_nvme_conf = NULL;
		return 0;
	}
	close(fd);

	nvme_glb.bd_nvme_conf = strdup(nvme_conf);
	if (nvme_glb.bd_nvme_conf == NULL) {
		rc = -DER_NOMEM;
		goto free_cond;
	}

	spdk_bs_opts_init(&nvme_glb.bd_bs_opts);
	nvme_glb.bd_bs_opts.cluster_sz = DAOS_BS_CLUSTER_SZ;
	nvme_glb.bd_bs_opts.num_md_pages = DAOS_BS_MD_PAGES;

	bio_chk_cnt_init = DAOS_DMA_CHUNK_CNT_INIT;
	bio_chk_cnt_max = DAOS_DMA_CHUNK_CNT_MAX;

	env = getenv("VOS_BDEV_CLASS");
	if (env && strcasecmp(env, "MALLOC") == 0) {
		D_WARN("Malloc device will be used!\n");
		nvme_glb.bd_bdev_class = BDEV_CLASS_MALLOC;
		nvme_glb.bd_bs_opts.cluster_sz = (1ULL << 20);
		nvme_glb.bd_bs_opts.num_md_pages = 10;
		size_mb = 2;
		bio_chk_cnt_max = 32;
	} else if (env && strcasecmp(env, "AIO") == 0) {
		D_WARN("AIO device will be used!\n");
		nvme_glb.bd_bdev_class = BDEV_CLASS_AIO;
	}

	bio_chk_sz = (size_mb << 20) >> BIO_DMA_PAGE_SHIFT;

	env = getenv("IO_STAT_PERIOD");
	io_stat_period = env ? atoi(env) : 0;
	io_stat_period *= (NSEC_PER_SEC / NSEC_PER_USEC);

	nvme_glb.bd_shm_id = shm_id;
	return 0;

free_cond:
	ABT_cond_free(&nvme_glb.bd_barrier);
free_mutex:
	ABT_mutex_free(&nvme_glb.bd_mutex);
fini_smd:
	smd_fini();
	return rc;
}

void
bio_nvme_fini(void)
{
	ABT_cond_free(&nvme_glb.bd_barrier);
	ABT_mutex_free(&nvme_glb.bd_mutex);
	if (nvme_glb.bd_nvme_conf != NULL) {
		D_FREE(nvme_glb.bd_nvme_conf);
		nvme_glb.bd_nvme_conf = NULL;
	}
	D_ASSERT(nvme_glb.bd_xstream_cnt == 0);
	D_ASSERT(nvme_glb.bd_init_thread == NULL);
	D_ASSERT(d_list_empty(&nvme_glb.bd_bdevs));
	smd_fini();
}

struct bio_msg {
	spdk_thread_fn	 bm_fn;
	void		*bm_arg;
};

/*
 * send_msg() can be called from any thread, the passed function
 * pointer (spdk_thread_fn) must be called on the same thread that
 * spdk_allocate_thread was called from.
 */
static void
send_msg(spdk_thread_fn fn, void *arg, void *ctxt)
{
	struct bio_xs_context *nvme_ctxt = ctxt;
	struct bio_msg *msg;
	size_t count;

	D_ALLOC_PTR(msg);
	if (msg == NULL) {
		D_ERROR("failed to allocate msg\n");
		return;
	}

	msg->bm_fn = fn;
	msg->bm_arg = arg;

	D_ASSERT(nvme_ctxt->bxc_msg_ring != NULL);
	count = spdk_ring_enqueue(nvme_ctxt->bxc_msg_ring, (void **)&msg, 1);
	if (count != 1)
		D_ERROR("failed to enqueue msg %lu\n", count);
};

/*
 * SPDK can register various pollers for the service xstream, the registered
 * poll functions will be called periodically in dss_srv_handler().
 *
 * For example, when the spdk_get_io_channel(nvme_bdev) is called in the
 * context of a service xstream, a SPDK I/O channel mapping to the xstream
 * will be created for submitting I/O requests against the nvme_bdev, and the
 * device completion poller will be registered on channel creation callback.
 */
struct bio_nvme_poller {
	spdk_poller_fn	 bnp_fn;
	void		*bnp_arg;
	uint64_t	 bnp_period_us;
	uint64_t	 bnp_expire_us;
	d_list_t	 bnp_link;
};

/* SPDK bdev will register various poll functions through this callback */
static struct spdk_poller *
start_poller(void *ctxt, spdk_poller_fn fn, void *arg, uint64_t period_us)
{
	struct bio_xs_context *nvme_ctxt = ctxt;
	struct bio_nvme_poller *poller;

	D_ALLOC_PTR(poller);
	if (poller == NULL) {
		D_ERROR("failed to allocate poller\n");
		return NULL;
	}

	poller->bnp_fn = fn;
	poller->bnp_arg = arg;
	poller->bnp_period_us = period_us;
	poller->bnp_expire_us = d_timeus_secdiff(0) + period_us;
	d_list_add(&poller->bnp_link, &nvme_ctxt->bxc_pollers);

	return (struct spdk_poller *)poller;
}

/* SPDK bdev uregister various poll functions through this callback */
static void
stop_poller(struct spdk_poller *poller, void *ctxt)
{
	struct bio_nvme_poller *nvme_poller;

	nvme_poller = (struct bio_nvme_poller *)poller;
	d_list_del_init(&nvme_poller->bnp_link);
	D_FREE(nvme_poller);
}

/*
 * Execute the messages on msg ring, call all registered pollers.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		Executed message count
 */
size_t
bio_nvme_poll(struct bio_xs_context *ctxt)
{
	struct bio_msg *msg;
	struct bio_nvme_poller *poller;
	size_t count;
	uint64_t now = d_timeus_secdiff(0);

	/* NVMe context setup was skipped */
	if (ctxt == NULL)
		return 0;

	/* Process one msg on the msg ring */
	count = spdk_ring_dequeue(ctxt->bxc_msg_ring, (void **)&msg, 1);
	if (count > 0) {
		msg->bm_fn(msg->bm_arg);
		D_FREE(msg);
	}

	/* Call all registered poller one by one */
	d_list_for_each_entry(poller, &ctxt->bxc_pollers, bnp_link) {
		if (poller->bnp_period_us != 0 && poller->bnp_expire_us < now)
			continue;

		poller->bnp_fn(poller->bnp_arg);

		if (poller->bnp_period_us != 0)
			poller->bnp_expire_us = now + poller->bnp_period_us;
	}

	print_io_stat(ctxt, now);

	return count;
}

struct common_cp_arg {
	unsigned int		 cca_inflights;
	int			 cca_rc;
	struct spdk_blob_store	*cca_bs;
};

static void
common_prep_arg(struct common_cp_arg *arg)
{
	memset(arg, 0, sizeof(*arg));
	arg->cca_inflights = 1;
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

void
xs_poll_completion(struct bio_xs_context *ctxt, unsigned int *inflights)
{
	size_t count;

	/* Wait for the completion callback done */
	if (inflights != NULL) {
		while (*inflights != 0)
			bio_nvme_poll(ctxt);
	}

	/* Continue to drain all msgs in the msg ring */
	do {
		count = bio_nvme_poll(ctxt);
	} while (count > 0);
}

static int
get_bdev_type(struct spdk_bdev *bdev)
{
	if (strcmp(spdk_bdev_get_product_name(bdev), "NVMe disk") == 0)
		return BDEV_CLASS_NVME;
	else if (strcmp(spdk_bdev_get_product_name(bdev), "Malloc disk") == 0)
		return BDEV_CLASS_MALLOC;
	else if (strcmp(spdk_bdev_get_product_name(bdev), "AIO disk") == 0)
		return BDEV_CLASS_AIO;
	else
		return BDEV_CLASS_UNKNOWN;
}

static struct spdk_blob_store *
load_blobstore(struct bio_xs_context *ctxt, struct spdk_bdev *bdev,
	       uuid_t *bs_uuid, bool create)
{
	struct spdk_bs_dev *bs_dev;
	struct spdk_bs_opts bs_opts;
	struct common_cp_arg cp_arg;

	/*
	 * bs_dev will be freed during spdk_bs_unload(), or in the
	 * internal error handling code of spdk_bs_init/load().
	 */
	bs_dev = spdk_bdev_create_bs_dev(bdev, NULL, NULL);
	if (bs_dev == NULL) {
		D_ERROR("failed to create bs_dev\n");
		return NULL;
	}

	bs_opts = nvme_glb.bd_bs_opts;
	/*
	 * A little bit hacke here, we store a UUID in the 16 bytes 'bstype'
	 * and use it as the block device ID.
	 */
	D_ASSERT(SPDK_BLOBSTORE_TYPE_LENGTH == 16);
	if (bs_uuid == NULL)
		strncpy(bs_opts.bstype.bstype, "", SPDK_BLOBSTORE_TYPE_LENGTH);
	else
		memcpy(bs_opts.bstype.bstype, bs_uuid,
		       SPDK_BLOBSTORE_TYPE_LENGTH);

	common_prep_arg(&cp_arg);
	if (create)
		spdk_bs_init(bs_dev, &bs_opts, common_bs_cb, &cp_arg);
	else
		spdk_bs_load(bs_dev, &bs_opts, common_bs_cb, &cp_arg);
	xs_poll_completion(ctxt, &cp_arg.cca_inflights);

	if (cp_arg.cca_rc != 0) {
		D_CDEBUG(bs_uuid == NULL, DB_IO, DLOG_ERR,
			 "%s blobstore failed %d\n", create ? "init" : "load",
			 cp_arg.cca_rc);
		return NULL;
	}

	D_ASSERT(cp_arg.cca_bs != NULL);
	return cp_arg.cca_bs;
}

static int
unload_blobstore(struct bio_xs_context *ctxt, struct spdk_blob_store *bs)
{
	struct common_cp_arg cp_arg;

	common_prep_arg(&cp_arg);
	spdk_bs_unload(bs, common_init_cb, &cp_arg);
	xs_poll_completion(ctxt, &cp_arg.cca_inflights);

	if (cp_arg.cca_rc != 0)
		D_ERROR("failed to unload blobstore %d\n", cp_arg.cca_rc);

	return cp_arg.cca_rc;
}

static int
create_bio_bdev(struct bio_xs_context *ctxt, struct spdk_bdev *bdev)
{
	struct bio_bdev			*d_bdev;
	struct spdk_blob_store		*bs = NULL;
	struct spdk_bs_type		 bstype;
	struct smd_nvme_device_info	 info;
	uuid_t				 bs_uuid;
	int				 rc;
	bool				 new_bs = false;

	D_ALLOC_PTR(d_bdev);
	if (d_bdev == NULL) {
		D_ERROR("failed to allocate bio_bdev\n");
		return -DER_NOMEM;
	}
	D_INIT_LIST_HEAD(&d_bdev->bb_link);

	/* Try to load blobstore without specifying 'bstype' first */
	bs = load_blobstore(ctxt, bdev, NULL, false);
	if (bs == NULL) {
		/* Create blobstore if it wasn't created before */
		uuid_generate(bs_uuid);
		bs = load_blobstore(ctxt, bdev, &bs_uuid, true);
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

	/* Find the initial xstream count per device */
	rc = smd_nvme_get_device(bs_uuid, &info);
	if (rc == 0) {
		d_bdev->bb_xs_cnt = info.ndi_xs_cnt;
	} else if (rc == -DER_NONEXIST) {
		/* device not present in table, first xstream mapped to dev */
		d_bdev->bb_xs_cnt = 0;
	} else {
		D_ERROR("Unable to get dev info for "DF_UUID"\n",
			DP_UUID(bs_uuid));
		goto error;
	}
	D_DEBUG(DB_MGMT, "Initial xstream count for "DF_UUID" set at %d\n",
		DP_UUID(bs_uuid), d_bdev->bb_xs_cnt);

	d_bdev->bb_bdev = bdev;
	uuid_copy(d_bdev->bb_uuid, bs_uuid);
	d_list_add(&d_bdev->bb_link, &nvme_glb.bd_bdevs);

	return 0;

error:
	D_FREE(d_bdev);
	return rc;
}

static int
init_bio_bdevs(struct bio_xs_context *ctxt)
{
	struct spdk_bdev *bdev;
	int rc = 0;

	for (bdev = spdk_bdev_first(); bdev != NULL;
	     bdev = spdk_bdev_next(bdev)) {
		if (nvme_glb.bd_bdev_class != get_bdev_type(bdev))
			continue;

		rc = create_bio_bdev(ctxt, bdev);
		if (rc)
			break;
	}
	return rc;
}

static void
put_bio_blobstore(struct bio_blobstore *bb, struct bio_xs_context *ctxt)
{
	struct spdk_blob_store *bs = NULL;
	bool last = false;

	/*
	 * Unload the blobstore within the same thread where is't loaded,
	 * all server xstreams which should have stopped using the blobstore.
	 */
	ABT_mutex_lock(bb->bb_mutex);
	if (bb->bb_ctxt == ctxt && bb->bb_bs != NULL) {
		bs = bb->bb_bs;
		bb->bb_bs = NULL;
	}

	D_ASSERT(bb->bb_ref > 0);
	bb->bb_ref--;
	if (bb->bb_ref == 0)
		last = true;
	ABT_mutex_unlock(bb->bb_mutex);

	if (bs != NULL)
		unload_blobstore(ctxt, bs);

	if (last) {
		ABT_mutex_free(&bb->bb_mutex);
		D_FREE(bb);
	}
}

static void
fini_bio_bdevs(struct bio_xs_context *ctxt)
{
	struct bio_bdev *d_bdev, *tmp;

	d_list_for_each_entry_safe(d_bdev, tmp, &nvme_glb.bd_bdevs, bb_link) {
		d_list_del_init(&d_bdev->bb_link);

		if (d_bdev->bb_blobstore != NULL)
			put_bio_blobstore(d_bdev->bb_blobstore, ctxt);

		D_FREE(d_bdev);
	}
}

static struct bio_blobstore *
alloc_bio_blobstore(struct bio_xs_context *ctxt)
{
	struct bio_blobstore *bb;
	int rc;

	D_ASSERT(ctxt != NULL);
	D_ALLOC_PTR(bb);
	if (bb == NULL)
		return NULL;

	rc = ABT_mutex_create(&bb->bb_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE(bb);
		return NULL;
	}

	bb->bb_ref = 1;
	bb->bb_ctxt = ctxt;

	return bb;
}

static struct bio_blobstore *
get_bio_blobstore(struct bio_blobstore *bb)
{
	ABT_mutex_lock(bb->bb_mutex);
	bb->bb_ref++;
	ABT_mutex_unlock(bb->bb_mutex);
	return bb;
}

/**
 * Assign a device for xstream->device mapping. Device chosen will be the device
 * with the least amount of mapped xstreams.
 */
static int
assign_dev_to_xs(int xs_id)
{
	struct bio_bdev			*d_bdev;
	struct bio_bdev			*chosen_bdev;
	struct smd_nvme_stream_bond	 xs_bond;
	int				 lowest_xs_cnt;
	int				 rc;

	D_ASSERT(!d_list_empty(&nvme_glb.bd_bdevs));
	chosen_bdev = d_list_entry(nvme_glb.bd_bdevs.next, struct bio_bdev,
				  bb_link);
	lowest_xs_cnt = chosen_bdev->bb_xs_cnt;

	/*
	 * Traverse the list and return the device with the least amount of
	 * mapped xstreams.
	 */
	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		if (d_bdev->bb_xs_cnt < lowest_xs_cnt) {
			lowest_xs_cnt = d_bdev->bb_xs_cnt;
			chosen_bdev = d_bdev;
		}
	}

	/* Update mapping for this xstream in NVMe device table */
	smd_nvme_set_stream_bond(xs_id, chosen_bdev->bb_uuid, &xs_bond);
	rc = smd_nvme_add_stream_bond(&xs_bond);
	if (rc) {
		D_ERROR("Failure adding entry to SMD stream table\n");
		return rc;
	}

	chosen_bdev->bb_xs_cnt++;

	D_DEBUG(DB_MGMT, "Successfully added entry to SMD stream table,"
		" xs_id:%d, dev:"DF_UUID", xs_cnt:%d\n",
		xs_id, DP_UUID(xs_bond.nsm_dev_id), chosen_bdev->bb_xs_cnt);

	return 0;
}

static int
init_blobstore_ctxt(struct bio_xs_context *ctxt, int xs_id)
{
	struct bio_bdev			*d_bdev;
	struct spdk_blob_store		*bs;
	struct smd_nvme_stream_bond	 xs_bond;
	int				 rc;
	bool				 found = false;

	D_ASSERT(ctxt->bxc_desc == NULL);
	D_ASSERT(ctxt->bxc_blobstore == NULL);
	D_ASSERT(ctxt->bxc_io_channel == NULL);

	/*
	 * Lookup @xs_id in the NVMe device table (per-server metadata),
	 * if found, create blobstore on the mapped device.
	 */
	if (d_list_empty(&nvme_glb.bd_bdevs)) {
		D_ERROR("No available SPDK bdevs, please check whether "
			"VOS_BDEV_CLASS is set properly.\n");
		return -DER_UNINIT;
	}

	rc = smd_nvme_get_stream_bond(xs_id, &xs_bond);
	if (rc && rc != -DER_NONEXIST)
		return rc;

	if (rc == -DER_NONEXIST) {
		/*
		 * Assign a device to the xstream if there isn't existing
		 * mapping. Device chosen will be current device that is mapped
		 * to the least amount of xstreams.
		 */
		rc = assign_dev_to_xs(xs_id);
		if (rc) {
			D_ERROR("No device assigned to xs_id:%d\n", xs_id);
			return rc;
		}

		rc = smd_nvme_get_stream_bond(xs_id, &xs_bond);
		if (rc)
			return rc;

	}

	D_DEBUG(DB_MGMT, "SMD stream table entry found, xs_id:%d, "
		"dev:"DF_UUID"\n", xs_id, DP_UUID(xs_bond.nsm_dev_id));

	/* Iterate thru device list to find matching dev */
	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		if (uuid_compare(d_bdev->bb_uuid, xs_bond.nsm_dev_id) == 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		/* TODO
		 * Mapping between device table entry and device list
		 * is inconsistent, either device currently mapped to
		 * the xstream is not present in the device list or
		 * stream table entry is invalid. Call per-server
		 * metadata management tool to rectify.
		 */
		D_ERROR("Failure finding dev: "DF_UUID"\n",
			DP_UUID(xs_bond.nsm_dev_id));
		return -DER_NONEXIST;
	}

	D_ASSERT(d_bdev->bb_bdev != NULL);
	rc = spdk_bdev_open(d_bdev->bb_bdev, false, NULL, NULL,
			    &ctxt->bxc_desc);
	if (rc != 0) {
		D_ERROR("Failed to open bdev %s, %d\n",
			spdk_bdev_get_name(d_bdev->bb_bdev), rc);
		return daos_errno2der(-rc);
	}

	if (d_bdev->bb_blobstore == NULL) {
		d_bdev->bb_blobstore = alloc_bio_blobstore(ctxt);
		if (d_bdev->bb_blobstore == NULL)
			return -DER_NOMEM;

		/* Load blobstore with bstype specified for sanity check */
		bs = load_blobstore(ctxt, d_bdev->bb_bdev, &d_bdev->bb_uuid,
				    false);
		if (bs == NULL)
			return -DER_INVAL;

		d_bdev->bb_blobstore->bb_bs = bs;

		D_DEBUG(DB_MGMT, "Loaded bs, xs_id:%d, xs:%p dev:%s\n",
			xs_id, ctxt, spdk_bdev_get_name(d_bdev->bb_bdev));
	}

	ctxt->bxc_blobstore = get_bio_blobstore(d_bdev->bb_blobstore);
	bs = ctxt->bxc_blobstore->bb_bs;
	D_ASSERT(bs != NULL);
	ctxt->bxc_io_channel = spdk_bs_alloc_io_channel(bs);
	if (ctxt->bxc_io_channel == NULL) {
		D_ERROR("Failed to create io channel\n");
		return -DER_NOMEM;
	}

	return 0;
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
	/* NVMe context setup was skipped */
	if (ctxt == NULL)
		return;

	if (ctxt->bxc_io_channel != NULL) {
		spdk_bs_free_io_channel(ctxt->bxc_io_channel);
		ctxt->bxc_io_channel = NULL;
	}

	if (ctxt->bxc_blobstore != NULL) {
		put_bio_blobstore(ctxt->bxc_blobstore, ctxt);
		ctxt->bxc_blobstore = NULL;
	}

	if (ctxt->bxc_desc != NULL) {
		spdk_bdev_close(ctxt->bxc_desc);
		ctxt->bxc_desc = NULL;
	}

	ABT_mutex_lock(nvme_glb.bd_mutex);
	nvme_glb.bd_xstream_cnt--;

	if (nvme_glb.bd_init_thread != NULL) {
		if (nvme_glb.bd_init_thread == ctxt->bxc_thread) {
			struct common_cp_arg	cp_arg;

			/*
			 * The xstream initialized SPDK env will have to
			 * wait for all other xstreams finalized first.
			 */
			if (nvme_glb.bd_xstream_cnt != 0)
				ABT_cond_wait(nvme_glb.bd_barrier,
					      nvme_glb.bd_mutex);

			fini_bio_bdevs(ctxt);

			common_prep_arg(&cp_arg);
			spdk_copy_engine_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg.cca_inflights);

			common_prep_arg(&cp_arg);
			spdk_bdev_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg.cca_inflights);

			nvme_glb.bd_init_thread = NULL;

		} else if (nvme_glb.bd_xstream_cnt == 0) {
			ABT_cond_broadcast(nvme_glb.bd_barrier);
		}
	}

	ABT_mutex_unlock(nvme_glb.bd_mutex);

	if (ctxt->bxc_thread != NULL) {
		xs_poll_completion(ctxt, NULL);
		spdk_free_thread();
		ctxt->bxc_thread = NULL;
	}

	if (ctxt->bxc_msg_ring != NULL) {
		spdk_ring_free(ctxt->bxc_msg_ring);
		ctxt->bxc_msg_ring = NULL;
	}
	D_ASSERT(d_list_empty(&ctxt->bxc_pollers));

	if (ctxt->bxc_dma_buf != NULL) {
		dma_buffer_destroy(ctxt->bxc_dma_buf);
		ctxt->bxc_dma_buf = NULL;
	}

	D_FREE(ctxt);
}

int
bio_xsctxt_alloc(struct bio_xs_context **pctxt, int xs_id)
{
	struct spdk_conf *config = NULL;
	struct bio_xs_context *ctxt;
	char name[32];
	int rc;

	/* Skip NVMe context setup if the daos_nvme.conf isn't present */
	if (nvme_glb.bd_nvme_conf == NULL) {
		*pctxt = NULL;
		return 0;
	}

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ctxt->bxc_pollers);
	ctxt->bxc_xs_id = xs_id;

	ABT_mutex_lock(nvme_glb.bd_mutex);

	nvme_glb.bd_xstream_cnt++;

	D_INFO("Initialize NVMe context, xs_id:%d, init_thread:%p\n",
	       xs_id, nvme_glb.bd_init_thread);

	/* Initialize SPDK env in first started xstream */
	if (nvme_glb.bd_init_thread == NULL) {
		struct spdk_env_opts opts;

		D_ASSERTF(nvme_glb.bd_xstream_cnt == 1, "%d",
			  nvme_glb.bd_xstream_cnt);

		config = spdk_conf_allocate();
		if (config == NULL) {
			D_ERROR("failed to alloc SPDK config\n");
			rc = -DER_NOMEM;
			goto out;
		}

		rc = spdk_conf_read(config, nvme_glb.bd_nvme_conf);
		if (rc != 0) {
			D_ERROR("failed to read %s, rc:%d\n",
				nvme_glb.bd_nvme_conf, rc);
			rc = -DER_INVAL; /* spdk_conf_read() returns -1 */
			goto out;
		}

		if (spdk_conf_first_section(config) == NULL) {
			D_ERROR("invalid format %s, rc:%d\n",
				nvme_glb.bd_nvme_conf, rc);
			rc = -DER_INVAL;
			goto out;
		}

		spdk_conf_set_as_default(config);

		spdk_env_opts_init(&opts);
		opts.name = "daos";
		if (nvme_glb.bd_shm_id != DAOS_NVME_SHMID_NONE)
			opts.shm_id = nvme_glb.bd_shm_id;

		rc = spdk_env_init(&opts);
		if (rc != 0) {
			D_ERROR("failed to initialize SPDK env, rc:%d\n", rc);
			rc = -DER_INVAL; /* spdk_env_init() returns -1 */
			goto out;
		}
	}

	/*
	 * Register SPDK thread beforehand, it could be used for poll device
	 * admin commands completions and hotplugged events in following
	 * spdk_bdev_initialize() call, it also could be used for blobstore
	 * metadata io channel in following init_bio_bdevs() call.
	 */
	ctxt->bxc_msg_ring = spdk_ring_create(SPDK_RING_TYPE_MP_SC,
					      DAOS_MSG_RING_SZ,
					      SPDK_ENV_SOCKET_ID_ANY);
	if (ctxt->bxc_msg_ring == NULL) {
		D_ERROR("failed to allocate msg ring\n");
		rc = -DER_NOMEM;
		goto out;
	}

	snprintf(name, sizeof(name), "daos_spdk_%d", xs_id);
	ctxt->bxc_thread = spdk_allocate_thread(send_msg, start_poller,
						stop_poller, ctxt, name);
	if (ctxt->bxc_thread == NULL) {
		D_ERROR("failed to alloc SPDK thread\n");
		rc = -DER_NOMEM;
		goto out;
	}

	/*
	 * The first started xstream will scan all bdevs and create blobstores,
	 * it's a prequisite for all per-xstream blobstore initialization.
	 */
	if (nvme_glb.bd_init_thread == NULL) {
		struct common_cp_arg cp_arg;

		/* The SPDK 'Malloc' device relies on copy engine. */
		rc = spdk_copy_engine_initialize();
		if (rc != 0) {
			D_ERROR("failed to init SPDK copy engine, rc:%d\n", rc);
			goto out;
		}

		/* Initialize all types of devices */
		common_prep_arg(&cp_arg);
		spdk_bdev_initialize(common_init_cb, &cp_arg);
		xs_poll_completion(ctxt, &cp_arg.cca_inflights);

		if (cp_arg.cca_rc != 0) {
			rc = cp_arg.cca_rc;
			D_ERROR("failed to init bdevs, rc:%d\n", rc);
			common_prep_arg(&cp_arg);
			spdk_copy_engine_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg.cca_inflights);
			goto out;
		}

		nvme_glb.bd_init_thread = ctxt->bxc_thread;
		rc = init_bio_bdevs(ctxt);
		if (rc != 0) {
			D_ERROR("failed to init bio_bdevs, rc:%d\n", rc);
			goto out;
		}
	}

	/* Initialize per-xstream blobstore context */
	rc = init_blobstore_ctxt(ctxt, xs_id);
	if (rc)
		goto out;

	ctxt->bxc_dma_buf = dma_buffer_create(bio_chk_cnt_init);
out:
	ABT_mutex_unlock(nvme_glb.bd_mutex);
	spdk_conf_free(config);
	if (rc != 0)
		bio_xsctxt_free(ctxt);

	*pctxt = (rc != 0) ? NULL : ctxt;
	return rc;
}
