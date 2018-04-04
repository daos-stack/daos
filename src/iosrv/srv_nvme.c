/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
/**
 * This file is part of the DAOS server. It implements the per-xstream NVMe
 * context initialization & finalization.
 */
#define D_LOGFAC	DD_FAC(server)

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
#include "srv_internal.h"

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

int bdev_class = BDEV_CLASS_NVME;
bool skip_nvme_setup;
struct spdk_bs_opts default_bs_opts;

struct daos_bdev {
	d_list_t	 db_link;
	uuid_t		 db_uuid;
	char		*db_name;
};

struct daos_nvme_data {
	ABT_mutex		 dnd_mutex;
	ABT_cond		 dnd_barrier;
	/* How many xstreams has intialized NVMe context */
	int			 dnd_xstream_cnt;
	/* The thread responsible for bdevs init/fini */
	struct spdk_thread	*dnd_init_thread;
	/* All bdevs can be used by DAOS server */
	d_list_t		 dnd_bdevs;
};

static struct daos_nvme_data nvme_glb;

int dss_nvme_init(void)
{
	char *env;
	int rc, fd;

	nvme_glb.dnd_xstream_cnt = 0;
	nvme_glb.dnd_init_thread = NULL;
	D_INIT_LIST_HEAD(&nvme_glb.dnd_bdevs);

	rc = ABT_mutex_create(&nvme_glb.dnd_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		return rc;
	}

	rc = ABT_cond_create(&nvme_glb.dnd_barrier);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&nvme_glb.dnd_mutex);
		rc = dss_abterr2der(rc);
		return rc;
	}

	fd = open(DAOS_NVME_CONF, O_RDONLY, 0600);
	if (fd < 0) {
		D_WARN("Open %s failed(%d), skip DAOS NVMe setup.\n",
		       DAOS_NVME_CONF, daos_errno2der(errno));
		skip_nvme_setup = true;
		return 0;
	}
	close(fd);

	spdk_bs_opts_init(&default_bs_opts);
	default_bs_opts.cluster_sz = DAOS_BS_CLUSTER_LARGE;
	default_bs_opts.num_md_pages = DAOS_BS_MD_PAGES_LARGE;

	env = getenv("VOS_BDEV_CLASS");
	if (env && strcasecmp(env, "MALLOC") == 0) {
		D_WARN("Malloc device will be used!\n");
		bdev_class = BDEV_CLASS_MALLOC;
		default_bs_opts.cluster_sz = DAOS_BS_CLUSTER_SMALL;
		default_bs_opts.num_md_pages = DAOS_BS_MD_PAGES_SMALL;
	}
	return 0;
}

void dss_nvme_fini(void)
{
	ABT_cond_free(&nvme_glb.dnd_barrier);
	ABT_mutex_free(&nvme_glb.dnd_mutex);
	skip_nvme_setup = false;
	D_ASSERT(nvme_glb.dnd_xstream_cnt == 0);
	D_ASSERT(nvme_glb.dnd_init_thread == NULL);
	D_ASSERT(d_list_empty(&nvme_glb.dnd_bdevs));
}

struct daos_msg {
	spdk_thread_fn	 dm_fn;
	void		*dm_arg;
};

/*
 * send_msg() can be called from any thread, the passed function
 * pointer (spdk_thread_fn) must be called on the same thread that
 * spdk_allocate_thread was called from.
 */
static void send_msg(spdk_thread_fn fn, void *arg, void *ctxt)
{
	struct dss_nvme_context *nvme_ctxt = ctxt;
	struct daos_msg *msg;
	size_t count;

	msg = calloc(1, sizeof(*msg));
	if (msg == NULL) {
		D_ERROR("failed to allocate msg\n");
		return;
	}

	msg->dm_fn = fn;
	msg->dm_arg = arg;

	D_ASSERT(nvme_ctxt->dnc_msg_ring != NULL);
	count = spdk_ring_enqueue(nvme_ctxt->dnc_msg_ring, (void **)&msg, 1);
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
struct daos_nvme_poller {
	spdk_poller_fn	 dnp_fn;
	void		*dnp_arg;
	uint64_t	 dnp_period_us;
	uint64_t	 dnp_expire_us;
	d_list_t	 dnp_link;
};

/* SPDK bdev will register various poll functions through this callback */
static struct spdk_poller *start_poller(void *ctxt, spdk_poller_fn fn,
					void *arg, uint64_t period_us)
{
	struct dss_nvme_context *nvme_ctxt = ctxt;
	struct daos_nvme_poller *poller;

	poller = calloc(1, sizeof(*poller));
	if (poller == NULL) {
		D_ERROR("failed to allocate poller\n");
		return NULL;
	}

	poller->dnp_fn = fn;
	poller->dnp_arg = arg;
	poller->dnp_period_us = period_us;
	poller->dnp_expire_us = d_timeus_secdiff(0) + period_us;
	d_list_add(&poller->dnp_link, &nvme_ctxt->dnc_pollers);

	return (struct spdk_poller *)poller;
}

/* SPDK bdev uregister various poll functions through this callback */
static void stop_poller(struct spdk_poller *poller, void *ctxt)
{
	struct daos_nvme_poller *nvme_poller;

	nvme_poller = (struct daos_nvme_poller *)poller;
	d_list_del_init(&nvme_poller->dnp_link);
	free(nvme_poller);
}

/*
 * Execute the messages on msg ring, call all registered pollers.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		Executed message count
 */
size_t dss_nvme_poll(struct dss_nvme_context *ctxt)
{
	struct daos_msg *msg;
	struct daos_nvme_poller *poller;
	size_t count;
	uint64_t now = d_timeus_secdiff(0);

	/* NVMe context setup was skipped */
	if (ctxt->dnc_msg_ring == NULL)
		return 0;

	/* Process one msg on the msg ring */
	count = spdk_ring_dequeue(ctxt->dnc_msg_ring, (void **)&msg, 1);
	if (count > 0) {
		msg->dm_fn(msg->dm_arg);
		free(msg);
	}

	/* Call all registered poller one by one */
	d_list_for_each_entry(poller, &ctxt->dnc_pollers, dnp_link) {
		if (poller->dnp_period_us != 0 && poller->dnp_expire_us < now)
			continue;

		poller->dnp_fn(poller->dnp_arg);

		if (poller->dnp_period_us != 0)
			poller->dnp_expire_us = now + poller->dnp_period_us;
	}

	return count;
}

struct common_cp_arg {
	int			 cca_rc;
	struct spdk_blob_store	*cca_bs;
	bool			 cca_done;
};

static void common_init_cb(void *arg, int rc)
{
	struct common_cp_arg *cp_arg = arg;

	D_ASSERT(!cp_arg->cca_done);
	D_ASSERT(cp_arg->cca_rc == 0);
	cp_arg->cca_done = true;
	cp_arg->cca_rc = rc;
}

static void common_fini_cb(void *arg)
{
	struct common_cp_arg *cp_arg = arg;

	D_ASSERT(!cp_arg->cca_done);
	cp_arg->cca_done = true;
}

static void common_bs_cb(void *arg, struct spdk_blob_store *bs, int rc)
{
	struct common_cp_arg *cp_arg = arg;

	D_ASSERT(!cp_arg->cca_done);
	D_ASSERT(cp_arg->cca_rc == 0);
	D_ASSERT(cp_arg->cca_bs == NULL);
	cp_arg->cca_done = true;
	cp_arg->cca_rc = rc;
	cp_arg->cca_bs = bs;
}

static void xs_poll_completion(struct dss_nvme_context *ctxt,
			       struct common_cp_arg *cp_arg)
{
	size_t count;

	/* Wait for the completion callback done */
	if (cp_arg != NULL) {
		while (!cp_arg->cca_done)
			dss_nvme_poll(ctxt);
	}

	/* Continue to drain all msgs in the msg ring */
	do {
		count = dss_nvme_poll(ctxt);
	} while (count > 0);
}

static int get_bdev_type(struct spdk_bdev *bdev)
{
	if (strcmp(spdk_bdev_get_product_name(bdev), "NVMe disk") == 0)
		return BDEV_CLASS_NVME;
	else if (strcmp(spdk_bdev_get_product_name(bdev), "Malloc disk") == 0)
		return BDEV_CLASS_MALLOC;
	else
		return BDEV_CLASS_UNKNOWN;
}

static struct spdk_blob_store *load_blobstore(struct dss_nvme_context *ctxt,
					      struct spdk_bdev *bdev,
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

	bs_opts = default_bs_opts;
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

	memset(&cp_arg, 0, sizeof(cp_arg));
	if (create)
		spdk_bs_init(bs_dev, &bs_opts, common_bs_cb, &cp_arg);
	else
		spdk_bs_load(bs_dev, &bs_opts, common_bs_cb, &cp_arg);
	xs_poll_completion(ctxt, &cp_arg);

	if (cp_arg.cca_rc != 0) {
		D_ERROR("%s blobsotre failed %d\n", create ? "init" : "load",
			cp_arg.cca_rc);
		return NULL;
	}

	D_ASSERT(cp_arg.cca_bs != NULL);
	return cp_arg.cca_bs;
}

static int unload_blobstore(struct dss_nvme_context *ctxt,
			    struct spdk_blob_store *bs)
{
	struct common_cp_arg cp_arg;

	memset(&cp_arg, 0, sizeof(cp_arg));
	spdk_bs_unload(bs, common_init_cb, &cp_arg);
	xs_poll_completion(ctxt, &cp_arg);

	if (cp_arg.cca_rc != 0)
		D_ERROR("failed to unload blobstore %d\n", cp_arg.cca_rc);

	return cp_arg.cca_rc;
}

static int create_daos_bdev(struct dss_nvme_context *ctxt,
			    struct spdk_bdev *bdev)
{
	struct daos_bdev *d_bdev;
	struct spdk_blob_store *bs;
	struct spdk_bs_type bstype;
	uuid_t bs_uuid;
	int rc;

	d_bdev = calloc(1, sizeof(*d_bdev));
	if (d_bdev == NULL) {
		D_ERROR("failed to allocate daos_bdev\n");
		return -DER_NOMEM;
	}
	D_INIT_LIST_HEAD(&d_bdev->db_link);

	/* Try to load blobstore without specifying 'bstype' first */
	bs = load_blobstore(ctxt, bdev, NULL, false);
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

	d_bdev->db_name = strdup(spdk_bdev_get_name(bdev));
	if (d_bdev->db_name == NULL) {
		D_ERROR("failed to allocate db_name\n");
		rc = -DER_NOMEM;
		goto error;
	}
	uuid_copy(d_bdev->db_uuid, bs_uuid);
	d_list_add(&d_bdev->db_link, &nvme_glb.dnd_bdevs);
	return 0;
error:
	free(d_bdev);
	return rc;
}

static int init_daos_bdevs(struct dss_nvme_context *ctxt)
{
	struct spdk_bdev *bdev;
	int rc = 0;

	bdev = spdk_bdev_first();
	while (bdev != NULL) {
		if (bdev_class != get_bdev_type(bdev))
			goto next;

		rc = create_daos_bdev(ctxt, bdev);
		if (rc)
			break;
next:
		bdev = spdk_bdev_next(bdev);
	}
	return rc;
}

static void fini_daos_bdevs(void)
{
	struct daos_bdev *d_bdev;

	d_list_for_each_entry(d_bdev, &nvme_glb.dnd_bdevs, db_link) {
		d_list_del_init(&d_bdev->db_link);

		if (d_bdev->db_name != NULL)
			free(d_bdev->db_name);
		free(d_bdev);
	}
}

static int init_blobstore_ctxt(struct dss_nvme_context *ctxt, int xs_id)
{
	struct daos_bdev *d_bdev;
	struct spdk_bdev *bdev;
	struct spdk_blob_store *bs;

	D_ASSERT(ctxt->dnc_blobstore == NULL);
	D_ASSERT(ctxt->dnc_io_channel == NULL);

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
	if (d_list_empty(&nvme_glb.dnd_bdevs))
		return -DER_UNINIT;

	d_bdev = d_list_entry(nvme_glb.dnd_bdevs.next, struct daos_bdev,
			      db_link);

	D_ASSERT(d_bdev->db_name != NULL);
	bdev = spdk_bdev_get_by_name(d_bdev->db_name);
	if (bdev == NULL) {
		D_ERROR("failed to find bdev named %s\n", d_bdev->db_name);
		return -DER_NONEXIST;
	}

	/* Load blobstore with bstype specified for sanity check */
	bs = load_blobstore(ctxt, bdev, &d_bdev->db_uuid, false);
	if (bs == NULL)
		return -DER_INVAL;

	ctxt->dnc_blobstore = bs;
	ctxt->dnc_io_channel = spdk_bs_alloc_io_channel(ctxt->dnc_blobstore);
	if (ctxt->dnc_io_channel == NULL) {
		D_ERROR("failed to create io channel\n");
		return -DER_NOMEM;
	}

	/*
	 * TODO
	 * Update mapping for this xstream in NVMe device table.
	 */

	/* Move the used device to tail */
	d_list_del_init(&d_bdev->db_link);
	d_list_add_tail(&d_bdev->db_link, &nvme_glb.dnd_bdevs);

	return 0;
}

/*
 * Finalize per-xstream NVMe context and SPDK env.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		N/A
 */
void dss_nvme_ctxt_fini(struct dss_nvme_context *ctxt)
{
	struct common_cp_arg cp_arg;

	if (skip_nvme_setup)
		return;

	if (ctxt->dnc_io_channel != NULL) {
		spdk_bs_free_io_channel(ctxt->dnc_io_channel);
		ctxt->dnc_io_channel = NULL;
	}

	if (ctxt->dnc_blobstore != NULL) {
		unload_blobstore(ctxt, ctxt->dnc_blobstore);
		ctxt->dnc_blobstore = NULL;
	}

	ABT_mutex_lock(nvme_glb.dnd_mutex);
	nvme_glb.dnd_xstream_cnt--;

	if (nvme_glb.dnd_init_thread != NULL) {
		if (nvme_glb.dnd_init_thread == ctxt->dnc_thread) {
			/*
			 * The xstream initialized SPDK env will have to
			 * wait for all other xstreams finalized first.
			 */
			if (nvme_glb.dnd_xstream_cnt != 0)
				ABT_cond_wait(nvme_glb.dnd_barrier,
					      nvme_glb.dnd_mutex);

			fini_daos_bdevs();

			memset(&cp_arg, 0, sizeof(cp_arg));
			spdk_copy_engine_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg);

			memset(&cp_arg, 0, sizeof(cp_arg));
			spdk_bdev_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg);

			nvme_glb.dnd_init_thread = NULL;
		} else if (nvme_glb.dnd_xstream_cnt == 0) {
			ABT_cond_broadcast(nvme_glb.dnd_barrier);
		}
	}

	ABT_mutex_unlock(nvme_glb.dnd_mutex);

	if (ctxt->dnc_thread != NULL) {
		xs_poll_completion(ctxt, NULL);
		spdk_free_thread();
		ctxt->dnc_thread = NULL;
	}

	if (ctxt->dnc_msg_ring != NULL) {
		spdk_ring_free(ctxt->dnc_msg_ring);
		ctxt->dnc_msg_ring = NULL;
	}

	D_ASSERT(d_list_empty(&ctxt->dnc_pollers));
}

/*
 * Initialize SPDK env and per-xstream NVMe context.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 * \param[IN] xs_id	xstream ID
 *
 * \returns		Zero on success, negative value on error
 */
int dss_nvme_ctxt_init(struct dss_nvme_context *ctxt, int xs_id)
{
	struct spdk_conf *config = NULL;
	char name[32];
	int rc;

	if (skip_nvme_setup)
		return 0;

	D_INFO("Initialize SPDK context, xs_id:%d, init_thread:%p\n",
	       xs_id, nvme_glb.dnd_init_thread);

	D_INIT_LIST_HEAD(&ctxt->dnc_pollers);

	ABT_mutex_lock(nvme_glb.dnd_mutex);

	nvme_glb.dnd_xstream_cnt++;

	/* Initialize SPDK env in first started xstream */
	if (nvme_glb.dnd_init_thread == NULL) {
		struct spdk_env_opts opts;

		D_ASSERTF(nvme_glb.dnd_xstream_cnt == 1, "%d",
			  nvme_glb.dnd_xstream_cnt);

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
	 * metadata io channel in following init_daos_bdevs() call.
	 */
	D_ASSERT(ctxt->dnc_msg_ring == NULL);
	ctxt->dnc_msg_ring = spdk_ring_create(SPDK_RING_TYPE_MP_SC,
					      DAOS_MSG_RING_SZ,
					      SPDK_ENV_SOCKET_ID_ANY);
	if (ctxt->dnc_msg_ring == NULL) {
		D_ERROR("failed to allocate msg ring\n");
		rc = -DER_NOMEM;
		goto out;
	}

	D_ASSERT(ctxt->dnc_thread == NULL);
	snprintf(name, sizeof(name), "daos_spdk_%d", xs_id);
	ctxt->dnc_thread = spdk_allocate_thread(send_msg, start_poller,
						stop_poller, ctxt, name);
	if (ctxt->dnc_thread == NULL) {
		D_ERROR("failed to alloc SPDK thread\n");
		rc = -DER_NOMEM;
		goto out;
	}

	/*
	 * The first started xstream will scan all bdevs and create blobstores,
	 * it's a prequisite for all per-xstream blobstore initialization.
	 */
	if (nvme_glb.dnd_init_thread == NULL) {
		struct common_cp_arg cp_arg;

		/* The SPDK 'Malloc' device relies on copy engine. */
		rc = spdk_copy_engine_initialize();
		if (rc != 0) {
			D_ERROR("failed to init SPDK copy engine, rc:%d\n", rc);
			goto out;
		}

		/* Initialize all types of devices */
		memset(&cp_arg, 0, sizeof(cp_arg));
		spdk_bdev_initialize(common_init_cb, &cp_arg);
		xs_poll_completion(ctxt, &cp_arg);

		if (cp_arg.cca_rc != 0) {
			rc = cp_arg.cca_rc;
			D_ERROR("failed to init bdevs, rc:%d\n", rc);
			memset(&cp_arg, 0, sizeof(cp_arg));
			spdk_copy_engine_finish(common_fini_cb, &cp_arg);
			xs_poll_completion(ctxt, &cp_arg);
			goto out;
		}

		nvme_glb.dnd_init_thread = ctxt->dnc_thread;
		rc = init_daos_bdevs(ctxt);
		if (rc != 0) {
			D_ERROR("failed to init daos_bdevs, rc:%d\n", rc);
			goto out;
		}
	}

	/* Initialize per-xstream blobstore context */
	rc = init_blobstore_ctxt(ctxt, xs_id);
out:
	ABT_mutex_unlock(nvme_glb.dnd_mutex);
	spdk_conf_free(config);
	if (rc != 0)
		dss_nvme_ctxt_fini(ctxt);
	return rc;
}
