/**
 * (C) Copyright 2018 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(eio)

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
#include "eio_internal.h"
#include <daos_srv/smd.h>

/* These Macros should be turned into DAOS configuration in the future */
#define DAOS_MSG_RING_SZ	4096
#define DAOS_NVME_CONF		"/etc/daos_nvme.conf"
#define DAOS_BS_CLUSTER_LARGE	(1024 * 1024 * 1024)	/* 1GB */
#define DAOS_BS_CLUSTER_SMALL	(1024 * 1024)		/* 1MB */
#define DAOS_BS_MD_PAGES_LARGE	(1024 * 20)	/* 20k blobs per device */
#define DAOS_BS_MD_PAGES_SMALL	(10)		/* 10 blobs per device */

enum {
	BDEV_CLASS_NVME = 0,
	BDEV_CLASS_MALLOC,
	BDEV_CLASS_UNKNOWN
};

/* Chunk size of DMA buffer in pages */
unsigned int eio_chk_sz;
/* Per-xstream maximum DMA buffer size (in chunk count) */
unsigned int eio_chk_cnt_max;
/* Per-xstream initial DMA buffer size (in chunk count) */
static unsigned int eio_chk_cnt_init;

struct eio_bdev {
	d_list_t		 eb_link;
	uuid_t			 eb_uuid;
	char			*eb_name;
	struct eio_blobstore	*eb_blobstore;
	struct spdk_bdev_desc	*eb_desc; /* for io stat only */
};

struct eio_nvme_data {
	ABT_mutex		 ed_mutex;
	ABT_cond		 ed_barrier;
	/* SPDK bdev type */
	int			 ed_bdev_class;
	/* How many xstreams has intialized NVMe context */
	int			 ed_xstream_cnt;
	/* The thread responsible for SPDK bdevs init/fini */
	struct spdk_thread	*ed_init_thread;
	/* Default SPDK blobstore options */
	struct spdk_bs_opts	 ed_bs_opts;
	/* All bdevs can be used by DAOS server */
	d_list_t		 ed_bdevs;
	unsigned int		 ed_skip_setup:1;
};

static struct eio_nvme_data nvme_glb;
static uint64_t io_stat_period;

/* Print the io stat every few seconds, for debug only */
static void
print_io_stat(uint64_t now)
{
	struct spdk_bdev_io_stat	 stat;
	struct eio_bdev			*d_bdev;
	struct spdk_io_channel		*channel;
	static uint64_t			 stat_age;

	if (io_stat_period == 0)
		return;

	if (stat_age + io_stat_period >= now)
		return;

	d_list_for_each_entry(d_bdev, &nvme_glb.ed_bdevs, eb_link) {
		D_ASSERT(d_bdev->eb_desc != NULL);
		D_ASSERT(d_bdev->eb_name != NULL);

		channel = spdk_bdev_get_io_channel(d_bdev->eb_desc);
		D_ASSERT(channel != NULL);
		spdk_bdev_get_io_stat(NULL, channel, &stat);
		spdk_put_io_channel(channel);

		D_PRINT("SPDK IO STAT: dev[%s] read_bytes["DF_U64"], "
			"read_ops["DF_U64"], write_bytes["DF_U64"], "
			"write_ops["DF_U64"], read_latency_ticks["DF_U64"], "
			"write_latency_ticks["DF_U64"]\n",
			d_bdev->eb_name, stat.bytes_read, stat.num_read_ops,
			stat.bytes_written, stat.num_write_ops,
			stat.read_latency_ticks, stat.write_latency_ticks);
	}

	stat_age = now;
}

int
eio_nvme_init(const char *storage_path)
{
	char		*env;
	int		rc, fd;
	unsigned int	size_mb = 8;

	rc = smd_create_initialize(storage_path, NULL, -1);
	if (rc != 0) {
		D_ERROR("Error creating server metadata store: %d\n", rc);
		return rc;
	}

	nvme_glb.ed_xstream_cnt = 0;
	nvme_glb.ed_init_thread = NULL;
	D_INIT_LIST_HEAD(&nvme_glb.ed_bdevs);

	rc = ABT_mutex_create(&nvme_glb.ed_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		return rc;
	}

	rc = ABT_cond_create(&nvme_glb.ed_barrier);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&nvme_glb.ed_mutex);
		rc = dss_abterr2der(rc);
		return rc;
	}

	fd = open(DAOS_NVME_CONF, O_RDONLY, 0600);
	if (fd < 0) {
		D_WARN("Open %s failed(%d), skip DAOS NVMe setup.\n",
		       DAOS_NVME_CONF, daos_errno2der(errno));
		nvme_glb.ed_skip_setup = 1;
		return 0;
	}
	close(fd);

	spdk_bs_opts_init(&nvme_glb.ed_bs_opts);
	nvme_glb.ed_bs_opts.cluster_sz = DAOS_BS_CLUSTER_LARGE;
	nvme_glb.ed_bs_opts.num_md_pages = DAOS_BS_MD_PAGES_LARGE;

	eio_chk_cnt_init = 1;
	eio_chk_cnt_max = 16;

	env = getenv("VOS_BDEV_CLASS");
	if (env && strcasecmp(env, "MALLOC") == 0) {
		D_WARN("Malloc device will be used!\n");
		nvme_glb.ed_bdev_class = BDEV_CLASS_MALLOC;
		nvme_glb.ed_bs_opts.cluster_sz = DAOS_BS_CLUSTER_SMALL;
		nvme_glb.ed_bs_opts.num_md_pages = DAOS_BS_MD_PAGES_SMALL;
		size_mb = 2;
		eio_chk_cnt_max = 32;
	}

	eio_chk_sz = (size_mb << 20) >> EIO_DMA_PAGE_SHIFT;

	env = getenv("IO_STAT_PERIOD");
	io_stat_period = env ? atoi(env) : 0;
	io_stat_period *= (NSEC_PER_SEC / NSEC_PER_USEC);

	return 0;
}

void
eio_nvme_fini(void)
{
	ABT_cond_free(&nvme_glb.ed_barrier);
	ABT_mutex_free(&nvme_glb.ed_mutex);
	nvme_glb.ed_skip_setup = 0;
	D_ASSERT(nvme_glb.ed_xstream_cnt == 0);
	D_ASSERT(nvme_glb.ed_init_thread == NULL);
	D_ASSERT(d_list_empty(&nvme_glb.ed_bdevs));
	smd_fini();

}

struct eio_msg {
	spdk_thread_fn	 em_fn;
	void		*em_arg;
};

/*
 * send_msg() can be called from any thread, the passed function
 * pointer (spdk_thread_fn) must be called on the same thread that
 * spdk_allocate_thread was called from.
 */
static void
send_msg(spdk_thread_fn fn, void *arg, void *ctxt)
{
	struct eio_xs_context *nvme_ctxt = ctxt;
	struct eio_msg *msg;
	size_t count;

	D_ALLOC_PTR(msg);
	if (msg == NULL) {
		D_ERROR("failed to allocate msg\n");
		return;
	}

	msg->em_fn = fn;
	msg->em_arg = arg;

	D_ASSERT(nvme_ctxt->exc_msg_ring != NULL);
	count = spdk_ring_enqueue(nvme_ctxt->exc_msg_ring, (void **)&msg, 1);
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
struct eio_nvme_poller {
	spdk_poller_fn	 enp_fn;
	void		*enp_arg;
	uint64_t	 enp_period_us;
	uint64_t	 enp_expire_us;
	d_list_t	 enp_link;
};

/* SPDK bdev will register various poll functions through this callback */
static struct spdk_poller *
start_poller(void *ctxt, spdk_poller_fn fn, void *arg, uint64_t period_us)
{
	struct eio_xs_context *nvme_ctxt = ctxt;
	struct eio_nvme_poller *poller;

	D_ALLOC_PTR(poller);
	if (poller == NULL) {
		D_ERROR("failed to allocate poller\n");
		return NULL;
	}

	poller->enp_fn = fn;
	poller->enp_arg = arg;
	poller->enp_period_us = period_us;
	poller->enp_expire_us = d_timeus_secdiff(0) + period_us;
	d_list_add(&poller->enp_link, &nvme_ctxt->exc_pollers);

	return (struct spdk_poller *)poller;
}

/* SPDK bdev uregister various poll functions through this callback */
static void
stop_poller(struct spdk_poller *poller, void *ctxt)
{
	struct eio_nvme_poller *nvme_poller;

	nvme_poller = (struct eio_nvme_poller *)poller;
	d_list_del_init(&nvme_poller->enp_link);
	D_FREE_PTR(nvme_poller);
}

/*
 * Execute the messages on msg ring, call all registered pollers.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		Executed message count
 */
size_t
eio_nvme_poll(struct eio_xs_context *ctxt)
{
	struct eio_msg *msg;
	struct eio_nvme_poller *poller;
	size_t count;
	uint64_t now = d_timeus_secdiff(0);

	/* NVMe context setup was skipped */
	if (ctxt == NULL)
		return 0;

	/* Process one msg on the msg ring */
	count = spdk_ring_dequeue(ctxt->exc_msg_ring, (void **)&msg, 1);
	if (count > 0) {
		msg->em_fn(msg->em_arg);
		D_FREE_PTR(msg);
	}

	/* Call all registered poller one by one */
	d_list_for_each_entry(poller, &ctxt->exc_pollers, enp_link) {
		if (poller->enp_period_us != 0 && poller->enp_expire_us < now)
			continue;

		poller->enp_fn(poller->enp_arg);

		if (poller->enp_period_us != 0)
			poller->enp_expire_us = now + poller->enp_period_us;
	}

	if (nvme_glb.ed_init_thread == ctxt->exc_thread)
		print_io_stat(now);

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
	cp_arg->cca_rc = rc;
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
	cp_arg->cca_rc = rc;
	cp_arg->cca_bs = bs;
}

void
xs_poll_completion(struct eio_xs_context *ctxt, unsigned int *inflights)
{
	size_t count;

	/* Wait for the completion callback done */
	if (inflights != NULL) {
		while (*inflights != 0)
			eio_nvme_poll(ctxt);
	}

	/* Continue to drain all msgs in the msg ring */
	do {
		count = eio_nvme_poll(ctxt);
	} while (count > 0);
}

static int
get_bdev_type(struct spdk_bdev *bdev)
{
	if (strcmp(spdk_bdev_get_product_name(bdev), "NVMe disk") == 0)
		return BDEV_CLASS_NVME;
	else if (strcmp(spdk_bdev_get_product_name(bdev), "Malloc disk") == 0)
		return BDEV_CLASS_MALLOC;
	else
		return BDEV_CLASS_UNKNOWN;
}

static struct spdk_blob_store *
load_blobstore(struct eio_xs_context *ctxt, struct spdk_bdev *bdev,
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

	bs_opts = nvme_glb.ed_bs_opts;
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
		D_DEBUG(bs_uuid == NULL ? DB_IO : DLOG_ERR,
			"%s blobsotre failed %d\n", create ? "init" : "load",
			cp_arg.cca_rc);
		return NULL;
	}

	D_ASSERT(cp_arg.cca_bs != NULL);
	return cp_arg.cca_bs;
}

static int
unload_blobstore(struct eio_xs_context *ctxt, struct spdk_blob_store *bs)
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
create_eio_bdev(struct eio_xs_context *ctxt, struct spdk_bdev *bdev)
{
	struct eio_bdev *d_bdev;
	struct spdk_blob_store *bs;
	struct spdk_bs_type bstype;
	uuid_t bs_uuid;
	int rc;

	D_ALLOC_PTR(d_bdev);
	if (d_bdev == NULL) {
		D_ERROR("failed to allocate eio_bdev\n");
		return -DER_NOMEM;
	}
	D_INIT_LIST_HEAD(&d_bdev->eb_link);

	/*
	 * TODO: Let's always create new blobstore each time before the
	 * blob deletion & per-server metadata is done, otherwise, the
	 * blobstore will be filled up after many rounds of tests.
	 */
	bs = NULL;

	/* Try to load blobstore without specifying 'bstype' first */
	/* bs = load_blobstore(ctxt, bdev, NULL, false); */
	if (bs == NULL) {
		/* Create blobstore if it wasn't created before */
		uuid_generate(bs_uuid);
		bs = load_blobstore(ctxt, bdev, &bs_uuid, true);
		if (bs == NULL) {
			rc = -DER_INVAL;
			goto error;
		}
	}

	/* Get the 'bstype' (device ID) of blobstore */
	bstype = spdk_bs_get_bstype(bs);
	memcpy(bs_uuid, bstype.bstype, sizeof(bs_uuid));

	rc = unload_blobstore(ctxt, bs);
	if (rc != 0)
		goto error;

	rc = spdk_bdev_open(bdev, false, NULL, NULL, &d_bdev->eb_desc);
	if (rc != 0) {
		D_ERROR("failed to open bdev %s, %d\n",
			spdk_bdev_get_name(bdev), rc);
		goto error;
	}

	d_bdev->eb_name = strdup(spdk_bdev_get_name(bdev));
	if (d_bdev->eb_name == NULL) {
		D_ERROR("failed to allocate eb_name\n");
		rc = -DER_NOMEM;
		goto error;
	}
	uuid_copy(d_bdev->eb_uuid, bs_uuid);
	d_list_add(&d_bdev->eb_link, &nvme_glb.ed_bdevs);
	return 0;
error:
	if (d_bdev->eb_desc != NULL)
		spdk_bdev_close(d_bdev->eb_desc);
	D_FREE_PTR(d_bdev);
	return rc;
}

static int
init_eio_bdevs(struct eio_xs_context *ctxt)
{
	struct spdk_bdev *bdev;
	int rc = 0;

	for (bdev = spdk_bdev_first(); bdev != NULL;
	     bdev = spdk_bdev_next(bdev)) {
		if (nvme_glb.ed_bdev_class != get_bdev_type(bdev))
			continue;

		rc = create_eio_bdev(ctxt, bdev);
		if (rc)
			break;
	}
	return rc;
}

static void
put_eio_blobstore(struct eio_blobstore *eb, struct eio_xs_context *ctxt)
{
	struct spdk_blob_store *bs = NULL;
	bool last = false;

	/*
	 * Unload the blobstore within the same thread where is't loaded,
	 * all server xstreams which should have stopped using the blobstore.
	 */
	ABT_mutex_lock(eb->eb_mutex);
	if (eb->eb_ctxt == ctxt && eb->eb_bs != NULL) {
		bs = eb->eb_bs;
		eb->eb_bs = NULL;
	}

	D_ASSERT(eb->eb_ref > 0);
	eb->eb_ref--;
	if (eb->eb_ref == 0)
		last = true;
	ABT_mutex_unlock(eb->eb_mutex);

	if (bs != NULL)
		unload_blobstore(ctxt, bs);

	if (last) {
		ABT_mutex_free(&eb->eb_mutex);
		D_FREE_PTR(eb);
	}
}

static void
fini_eio_bdevs(struct eio_xs_context *ctxt)
{
	struct eio_bdev *d_bdev, *tmp;

	d_list_for_each_entry_safe(d_bdev, tmp, &nvme_glb.ed_bdevs, eb_link) {
		d_list_del_init(&d_bdev->eb_link);

		if (d_bdev->eb_desc != NULL)
			spdk_bdev_close(d_bdev->eb_desc);

		if (d_bdev->eb_name != NULL)
			free(d_bdev->eb_name);

		if (d_bdev->eb_blobstore != NULL)
			put_eio_blobstore(d_bdev->eb_blobstore, ctxt);

		D_FREE_PTR(d_bdev);
	}
}

static struct eio_blobstore *
alloc_eio_blobstore(struct eio_xs_context *ctxt)
{
	struct eio_blobstore *eb;
	int rc;

	D_ASSERT(ctxt != NULL);
	D_ALLOC_PTR(eb);
	if (eb == NULL)
		return NULL;

	rc = ABT_mutex_create(&eb->eb_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE_PTR(eb);
		return NULL;
	}

	eb->eb_ref = 1;
	eb->eb_ctxt = ctxt;

	return eb;
}

static struct eio_blobstore *
get_eio_blobstore(struct eio_blobstore *eb)
{
	ABT_mutex_lock(eb->eb_mutex);
	eb->eb_ref++;
	ABT_mutex_unlock(eb->eb_mutex);
	return eb;
}

static int
init_blobstore_ctxt(struct eio_xs_context *ctxt, int xs_id)
{
	struct eio_bdev		*d_bdev;
	struct spdk_bdev	*bdev;
	struct spdk_blob_store	*bs;

	D_ASSERT(ctxt->exc_blobstore == NULL);
	D_ASSERT(ctxt->exc_io_channel == NULL);

	/*
	 * TODO
	 * Lookup @xs_id in the NVMe device table (per-server metadata),
	 * if found, create blobstore on the mapped device.
	 *
	 * For simplicity reason, we can assume the mapping in NVMe device
	 * table is always integrated: either mappings for all xstreams or
	 * empty.
	 */

	/* Assign one device to the xstream if there isn't existing mapping */
	if (d_list_empty(&nvme_glb.ed_bdevs))
		return -DER_UNINIT;

	d_bdev = d_list_entry(nvme_glb.ed_bdevs.next, struct eio_bdev,
			      eb_link);

	if (d_bdev->eb_blobstore == NULL) {
		d_bdev->eb_blobstore = alloc_eio_blobstore(ctxt);
		if (d_bdev->eb_blobstore == NULL)
			return -DER_NOMEM;

		D_ASSERT(d_bdev->eb_name != NULL);
		bdev = spdk_bdev_get_by_name(d_bdev->eb_name);
		if (bdev == NULL) {
			D_ERROR("failed to find bdev named %s\n",
				d_bdev->eb_name);
			return -DER_NONEXIST;
		}

		/* Load blobstore with bstype specified for sanity check */
		bs = load_blobstore(ctxt, bdev, &d_bdev->eb_uuid, false);
		if (bs == NULL)
			return -DER_INVAL;

		d_bdev->eb_blobstore->eb_bs = bs;

		D_DEBUG(DB_MGMT, "Loaded bs, xs_id: %d, xs:%p dev:%s\n",
			xs_id, ctxt, d_bdev->eb_name);
	}

	ctxt->exc_blobstore = get_eio_blobstore(d_bdev->eb_blobstore);
	bs = ctxt->exc_blobstore->eb_bs;
	D_ASSERT(bs != NULL);
	ctxt->exc_io_channel = spdk_bs_alloc_io_channel(bs);
	if (ctxt->exc_io_channel == NULL) {
		D_ERROR("failed to create io channel\n");
		return -DER_NOMEM;
	}

	/*
	 * TODO
	 * Update mapping for this xstream in NVMe device table.
	 */

	/* Move the used device to tail */
	d_list_del_init(&d_bdev->eb_link);
	d_list_add_tail(&d_bdev->eb_link, &nvme_glb.ed_bdevs);

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
eio_xsctxt_free(struct eio_xs_context *ctxt)
{
	/* NVMe context setup was skipped */
	if (ctxt == NULL)
		return;

	if (ctxt->exc_io_channel != NULL) {
		spdk_bs_free_io_channel(ctxt->exc_io_channel);
		ctxt->exc_io_channel = NULL;
	}

	if (ctxt->exc_blobstore != NULL) {
		put_eio_blobstore(ctxt->exc_blobstore, ctxt);
		ctxt->exc_blobstore = NULL;
	}

	ABT_mutex_lock(nvme_glb.ed_mutex);
	nvme_glb.ed_xstream_cnt--;

	if (nvme_glb.ed_init_thread != NULL) {
		if (nvme_glb.ed_init_thread == ctxt->exc_thread) {
			struct common_cp_arg	cp_arg;

			/*
			 * The xstream initialized SPDK env will have to
			 * wait for all other xstreams finalized first.
			 */
			if (nvme_glb.ed_xstream_cnt != 0)
				ABT_cond_wait(nvme_glb.ed_barrier,
					      nvme_glb.ed_mutex);

			fini_eio_bdevs(ctxt);

			common_prep_arg(&cp_arg);
			spdk_copy_engine_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg.cca_inflights);

			common_prep_arg(&cp_arg);
			spdk_bdev_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg.cca_inflights);

			nvme_glb.ed_init_thread = NULL;

		} else if (nvme_glb.ed_xstream_cnt == 0) {
			ABT_cond_broadcast(nvme_glb.ed_barrier);
		}
	}

	ABT_mutex_unlock(nvme_glb.ed_mutex);

	if (ctxt->exc_thread != NULL) {
		xs_poll_completion(ctxt, NULL);
		spdk_free_thread();
		ctxt->exc_thread = NULL;
	}

	if (ctxt->exc_msg_ring != NULL) {
		spdk_ring_free(ctxt->exc_msg_ring);
		ctxt->exc_msg_ring = NULL;
	}
	D_ASSERT(d_list_empty(&ctxt->exc_pollers));

	if (ctxt->exc_dma_buf != NULL) {
		dma_buffer_destroy(ctxt->exc_dma_buf);
		ctxt->exc_dma_buf = NULL;
	}

	D_FREE_PTR(ctxt);
}

int
eio_xsctxt_alloc(struct eio_xs_context **pctxt, int xs_id)
{
	struct spdk_conf *config = NULL;
	struct eio_xs_context *ctxt;
	char name[32];
	int rc;

	/* Skip NVMe context setup if the daos_nvme.conf isn't present */
	if (nvme_glb.ed_skip_setup) {
		*pctxt = NULL;
		return 0;
	}

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ctxt->exc_pollers);
	ctxt->exc_xs_id = xs_id;

	ABT_mutex_lock(nvme_glb.ed_mutex);

	nvme_glb.ed_xstream_cnt++;

	D_INFO("Initialize NVMe context, xs_id:%d, init_thread:%p\n",
	       xs_id, nvme_glb.ed_init_thread);

	/* Initialize SPDK env in first started xstream */
	if (nvme_glb.ed_init_thread == NULL) {
		struct spdk_env_opts opts;

		D_ASSERTF(nvme_glb.ed_xstream_cnt == 1, "%d",
			  nvme_glb.ed_xstream_cnt);

		config = spdk_conf_allocate();
		if (config == NULL) {
			D_ERROR("failed to alloc SPDK config\n");
			rc = -DER_NOMEM;
			goto out;
		}

		rc = spdk_conf_read(config, DAOS_NVME_CONF);
		if (rc != 0) {
			D_ERROR("failed to read %s, rc:%d\n", DAOS_NVME_CONF,
				rc);
			goto out;
		}

		if (spdk_conf_first_section(config) == NULL) {
			D_ERROR("invalid format %s, rc:%d\n", DAOS_NVME_CONF,
				rc);
			rc = -DER_INVAL;
			goto out;
		}

		spdk_conf_set_as_default(config);

		spdk_env_opts_init(&opts);
		opts.name = "daos";
		rc = spdk_env_init(&opts);
		if (rc != 0) {
			D_ERROR("failed to initialize SPDK env, rc:%d\n", rc);
			goto out;
		}
	}

	/*
	 * Register SPDK thread beforehand, it could be used for poll device
	 * admin commands completions and hotplugged events in following
	 * spdk_bdev_initialize() call, it also could be used for blobstore
	 * metadata io channel in following init_eio_bdevs() call.
	 */
	ctxt->exc_msg_ring = spdk_ring_create(SPDK_RING_TYPE_MP_SC,
					      DAOS_MSG_RING_SZ,
					      SPDK_ENV_SOCKET_ID_ANY);
	if (ctxt->exc_msg_ring == NULL) {
		D_ERROR("failed to allocate msg ring\n");
		rc = -DER_NOMEM;
		goto out;
	}

	snprintf(name, sizeof(name), "daos_spdk_%d", xs_id);
	ctxt->exc_thread = spdk_allocate_thread(send_msg, start_poller,
						stop_poller, ctxt, name);
	if (ctxt->exc_thread == NULL) {
		D_ERROR("failed to alloc SPDK thread\n");
		rc = -DER_NOMEM;
		goto out;
	}

	/*
	 * The first started xstream will scan all bdevs and create blobstores,
	 * it's a prequisite for all per-xstream blobstore initialization.
	 */
	if (nvme_glb.ed_init_thread == NULL) {
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

		nvme_glb.ed_init_thread = ctxt->exc_thread;
		rc = init_eio_bdevs(ctxt);
		if (rc != 0) {
			D_ERROR("failed to init eio_bdevs, rc:%d\n", rc);
			goto out;
		}
	}

	/* Initialize per-xstream blobstore context */
	rc = init_blobstore_ctxt(ctxt, xs_id);
	if (rc)
		goto out;

	ctxt->exc_dma_buf = dma_buffer_create(eio_chk_cnt_init);
out:
	ABT_mutex_unlock(nvme_glb.ed_mutex);
	spdk_conf_free(config);
	if (rc != 0)
		eio_xsctxt_free(ctxt);

	*pctxt = (rc != 0) ? NULL : ctxt;
	return rc;
}
