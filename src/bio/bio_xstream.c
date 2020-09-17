/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
#include <spdk/nvme.h>
#include <spdk/vmd.h>
#include <spdk/string.h>
#include <spdk/thread.h>
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
#define DAOS_NVME_MAX_CTRLRS	1024		/* Max read from nvme_conf */

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

/*
 * 'Init' xstream is the first started VOS xstream, it calls
 * spdk_bdev_initialize() on server start to initialize SPDK bdev and scan all
 * the available devices, and the SPDK hotplug poller is registered then.
 *
 * Given the SPDK bdev remove callback is called on 'init' xstream, 'init'
 * xstream is the one responsible for initiating BIO hot plug/remove event,
 * and managing the list of 'bio_bdev'.
 */
struct bio_bdev {
	d_list_t		 bb_link;
	uuid_t			 bb_uuid;
	char			*bb_name;
	/* Prevent the SPDK bdev being freed by device hot remove */
	struct spdk_bdev_desc	*bb_desc;
	struct bio_blobstore	*bb_blobstore;
	/* count of target(VOS xstream) per device */
	int			 bb_tgt_cnt;
};

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
	struct spdk_conf	*bd_nvme_conf;
	int			 bd_shm_id;
	/* When using SPDK primary mode, specifies memory allocation in MB */
	int			 bd_mem_size;
};

static struct bio_nvme_data nvme_glb;
uint64_t io_stat_period;

static int
is_addr_in_whitelist(char *pci_addr, const struct spdk_pci_addr *whitelist,
		     int num_whitelist_devices)
{
	int			i;
	struct spdk_pci_addr    tmp;

	if (spdk_pci_addr_parse(&tmp, pci_addr) != 0) {
		D_ERROR("Invalid address %s\n", pci_addr);
		return -DER_INVAL;
	}

	for (i = 0; i < num_whitelist_devices; i++) {
		if (spdk_pci_addr_compare(&tmp, &whitelist[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

/*
 * Add PCI address to spdk_env_opts whitelist, ignoring any duplicates.
 */
static int
opts_add_pci_addr(struct spdk_env_opts *opts, struct spdk_pci_addr **list,
		  char *traddr)
{
	int			rc;
	size_t			count = opts->num_pci_addr;
	struct spdk_pci_addr   *tmp = *list;

	rc = is_addr_in_whitelist(traddr, *list, count);
	if (rc < 0) {
		return rc;
	}
	if (rc == 1) {
		return 0;
	}

	tmp = realloc(tmp, sizeof(struct spdk_pci_addr) * (count + 1));
	if (tmp == NULL) {
		D_ERROR("realloc error\n");
		return -DER_NOMEM;
	}

	*list = tmp;
	if (spdk_pci_addr_parse(*list + count, traddr) < 0) {
		D_ERROR("Invalid address %s\n", traddr);
		return -DER_INVAL;
	}

	opts->num_pci_addr++;
	return 0;
}

/*
 * Convert a transport id in the BDF form of "5d0505:01:00.0" or something
 * similar to the VMD address in the form of "0000:5d:05.5" that can be parsed
 * by DPDK.
 *
 * \param dst String to be populated as output.
 * \param src Input bdf.
 */
static int
traddr_to_vmd(char *dst, const char *src)
{
	char traddr_tmp[SPDK_NVMF_TRADDR_MAX_LEN + 1];
	char vmd_addr[SPDK_NVMF_TRADDR_MAX_LEN + 1] = "0000:";
	char *ptr;
	const char ch = ':';
	char addr_split[3];
	int position, iteration;
	int n;

	n = snprintf(traddr_tmp, SPDK_NVMF_TRADDR_MAX_LEN, "%s", src);
	if (n < 0 || n > SPDK_NVMF_TRADDR_MAX_LEN) {
		D_ERROR("snprintf failed\n");
		return -DER_INVAL;
	}

	/* Only the first chunk of data from the traddr is useful */
	ptr = strchr(traddr_tmp, ch);
	if (ptr == NULL) {
		D_ERROR("Transport id not valid\n");
		return -DER_INVAL;
	}
	position = ptr - traddr_tmp;
	traddr_tmp[position] = '\0';

	ptr = traddr_tmp;
	iteration = 0;
	while (*ptr != '\0') {
		n = snprintf(addr_split, sizeof(addr_split), "%s", ptr);
		if (n < 0) {
			D_ERROR("snprintf failed\n");
			return -DER_INVAL;
		}
		strcat(vmd_addr, addr_split);

		if (iteration != 0) {
			strcat(vmd_addr, ".");
			ptr = ptr + 3;
			/** Hack alert!  Reuse existing buffer to ensure new
			 *  string is null terminated.
			 */
			addr_split[0] = ptr[0];
			addr_split[1] = '\0';
			strcat(vmd_addr, addr_split);
			break;
		}
		strcat(vmd_addr, ":");
		ptr = ptr + 2;
		iteration++;
	}
	n = snprintf(dst, SPDK_NVMF_TRADDR_MAX_LEN, "%s", vmd_addr);
	if (n < 0 || n > SPDK_NVMF_TRADDR_MAX_LEN) {
		D_ERROR("snprintf failed\n");
		return -DER_INVAL;
	}

	return 0;
}

static int
populate_whitelist(struct spdk_env_opts *opts)
{
	struct spdk_nvme_transport_id	*trid;
	struct spdk_conf_section	*sp;
	const char			*val;
	size_t				 i;
	int				 rc = 0;
	bool				 vmd_enabled = false;

	/* Don't need to pass whitelist for non-NVMe devices */
	if (nvme_glb.bd_bdev_class != BDEV_CLASS_NVME)
		return 0;

	/*
	 * Optionally VMD devices will be used, and will require a different
	 * transport id to pass to whitelist for DPDK.
	 */
	if (spdk_conf_find_section(NULL, "Vmd") != NULL)
		vmd_enabled = true;

	sp = spdk_conf_find_section(NULL, "Nvme");
	if (sp == NULL) {
		D_ERROR("unexpected empty config\n");
		return -DER_INVAL;
	}

	D_ALLOC_PTR(trid);
	if (trid == NULL)
		return -DER_NOMEM;

	for (i = 0; i < DAOS_NVME_MAX_CTRLRS; i++) {
		memset(trid, 0, sizeof(struct spdk_nvme_transport_id));

		val = spdk_conf_section_get_nmval(sp, "TransportID", i, 0);
		if (val == NULL) {
			break;
		}

		rc = spdk_nvme_transport_id_parse(trid, val);
		if (rc < 0) {
			D_ERROR("Unable to parse TransportID: %s\n", val);
			rc = -DER_INVAL;
			break;
		}

		if (trid->trtype != SPDK_NVME_TRANSPORT_PCIE) {
			D_ERROR("unexpected non-PCIE transport\n");
			rc = -DER_INVAL;
			break;
		}

		if (vmd_enabled) {
			if (strncmp(trid->traddr, "0", 1) != 0) {
				/*
				 * We can assume this is the transport id of the
				 * backing NVMe SSD behind the VMD. DPDK will
				 * not recognize this transport ID, instead need
				 * to pass VMD address as the whitelist param.
				 */
				rc = traddr_to_vmd(trid->traddr, trid->traddr);
				if (rc < 0) {
					D_ERROR("Invalid traddr=%s\n",
						trid->traddr);
					rc = -DER_INVAL;
					break;
				}
			}
		}

		rc = opts_add_pci_addr(opts, &opts->pci_whitelist,
				       trid->traddr);
		if (rc < 0) {
			D_ERROR("Invalid traddr=%s\n", trid->traddr);
			rc = -DER_INVAL;
			break;
		}
	}

	D_FREE(trid);
	if (rc && opts->pci_whitelist != NULL) {
		D_FREE(opts->pci_whitelist);
		opts->pci_whitelist = NULL;
	}

	return rc;
}

static int
bio_spdk_env_init(void)
{
	struct spdk_env_opts	 opts;
	int			 rc;

	D_ASSERT(nvme_glb.bd_nvme_conf != NULL);
	if (spdk_conf_first_section(nvme_glb.bd_nvme_conf) == NULL) {
		D_ERROR("Invalid NVMe conf format\n");
		return -DER_INVAL;
	}

	spdk_conf_set_as_default(nvme_glb.bd_nvme_conf);

	spdk_env_opts_init(&opts);
	opts.name = "daos";
	if (nvme_glb.bd_mem_size != DAOS_NVME_MEM_PRIMARY)
		opts.mem_size = nvme_glb.bd_mem_size;

	rc = populate_whitelist(&opts);
	if (rc != 0)
		return rc;

	if (nvme_glb.bd_shm_id != DAOS_NVME_SHMID_NONE)
		opts.shm_id = nvme_glb.bd_shm_id;

	/* quiet DPDK logging by setting level to ERROR */
	opts.env_context = "--log-level=lib.eal:4";

	rc = spdk_env_init(&opts);
	if (opts.pci_whitelist != NULL)
		D_FREE(opts.pci_whitelist);
	if (rc != 0) {
		rc = -DER_INVAL; /* spdk_env_init() returns -1 */
		D_ERROR("Failed to initialize SPDK env, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (spdk_conf_find_section(NULL, "Vmd") != NULL) {
		/**
		 * Enumerate VMD devices and hook them into the SPDK PCI
		 * subsystem.
		 */
		rc = spdk_vmd_init();
		if (rc != 0) {
			rc = -DER_INVAL; /* spdk_vmd_init() returns -1 */
			D_ERROR("Failed to initialize VMD env, "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		/**
		 * TODO spdk_vmd_hotplug_monitor() will need to be called
		 * periodically on 'init' xstream to monitor VMD hotremove/
		 * hotplug events.
		 */
	}

	spdk_unaffinitize_thread();

	rc = spdk_thread_lib_init(NULL, 0);
	if (rc != 0) {
		rc = -DER_INVAL;
		D_ERROR("Failed to init SPDK thread lib, %s (%d)\n",
			spdk_strerror(rc), rc);
		spdk_env_fini();
		return rc;
	}

	return rc;
}

int
bio_nvme_init(const char *storage_path, const char *nvme_conf, int shm_id,
	      int mem_size)
{
	char		*env;
	int		rc, fd;
	uint64_t	size_mb = DAOS_DMA_CHUNK_MB;

	rc = smd_init(storage_path);
	if (rc != 0) {
		D_ERROR("Initialize SMD store failed. "DF_RC"\n", DP_RC(rc));
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
		D_WARN("Open %s failed("DF_RC"), skip DAOS NVMe setup.\n",
		       nvme_conf, DP_RC(daos_errno2der(errno)));
		nvme_glb.bd_nvme_conf = NULL;
		return 0;
	}
	close(fd);

	nvme_glb.bd_nvme_conf = spdk_conf_allocate();
	if (nvme_glb.bd_nvme_conf == NULL) {
		D_ERROR("Failed to alloc SPDK config\n");
		rc = -DER_NOMEM;
		goto free_cond;
	}

	rc = spdk_conf_read(nvme_glb.bd_nvme_conf, nvme_conf);
	if (rc != 0) {
		rc = -DER_INVAL; /* spdk_conf_read() returns -1 */
		D_ERROR("Failed to read %s, "DF_RC"\n", nvme_conf, DP_RC(rc));
		goto free_conf;
	}

	spdk_bs_opts_init(&nvme_glb.bd_bs_opts);
	nvme_glb.bd_bs_opts.cluster_sz = DAOS_BS_CLUSTER_SZ;
	nvme_glb.bd_bs_opts.num_md_pages = DAOS_BS_MD_PAGES;
	nvme_glb.bd_bs_opts.max_channel_ops = BIO_BS_MAX_CHANNEL_OPS;

	bio_chk_cnt_init = DAOS_DMA_CHUNK_CNT_INIT;
	bio_chk_cnt_max = DAOS_DMA_CHUNK_CNT_MAX;

	env = getenv("VOS_BDEV_CLASS");
	if (env && strcasecmp(env, "MALLOC") == 0) {
		D_WARN("Malloc device(s) will be used!\n");
		nvme_glb.bd_bdev_class = BDEV_CLASS_MALLOC;
		nvme_glb.bd_bs_opts.cluster_sz = (1ULL << 20);
		nvme_glb.bd_bs_opts.num_md_pages = 10;
		size_mb = 2;
		bio_chk_cnt_max = 32;
	} else if (env && strcasecmp(env, "AIO") == 0) {
		D_WARN("AIO device(s) will be used!\n");
		nvme_glb.bd_bdev_class = BDEV_CLASS_AIO;
	}

	bio_chk_sz = (size_mb << 20) >> BIO_DMA_PAGE_SHIFT;

	env = getenv("IO_STAT_PERIOD");
	io_stat_period = env ? atoi(env) : 0;
	io_stat_period *= (NSEC_PER_SEC / NSEC_PER_USEC);

	nvme_glb.bd_shm_id = shm_id;
	nvme_glb.bd_mem_size = mem_size;

	rc = bio_spdk_env_init();
	if (rc)
		goto free_conf;

	return 0;

free_conf:
	spdk_conf_free(nvme_glb.bd_nvme_conf);
	nvme_glb.bd_nvme_conf = NULL;
free_cond:
	ABT_cond_free(&nvme_glb.bd_barrier);
free_mutex:
	ABT_mutex_free(&nvme_glb.bd_mutex);
fini_smd:
	smd_fini();
	return rc;
}

static void
bio_spdk_env_fini(void)
{
	if (nvme_glb.bd_nvme_conf != NULL) {
		spdk_thread_lib_fini();
		spdk_env_fini();
		spdk_conf_free(nvme_glb.bd_nvme_conf);
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
	return bbs->bb_owner_xs == ctxt;
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
	if (ctxt == NULL)
		return 0;

	rc = spdk_thread_poll(ctxt->bxc_thread, 0, 0);

	/* Print SPDK I/O stats for each xstream */
	bio_xs_io_stat(ctxt, now);

	/*
	 * Query and print the SPDK device health stats for only the device
	 * owner xstream.
	 */
	if (ctxt->bxc_blobstore != NULL &&
	    is_bbs_owner(ctxt, ctxt->bxc_blobstore))
		bio_bs_monitor(ctxt, now);

	return rc;
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
	D_ASSERT(inflights != NULL);
	/* Wait for the completion callback done */
	while (*inflights != 0)
		bio_nvme_poll(ctxt);
}

int
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
load_blobstore(struct bio_xs_context *ctxt, char *bdev_name, uuid_t *bs_uuid,
	       bool create)
{
	struct spdk_bdev_desc	*desc = NULL;
	struct spdk_bs_dev	*bs_dev;
	struct spdk_bs_opts	 bs_opts;
	struct common_cp_arg	 cp_arg;
	int			 rc;

	rc = spdk_bdev_open_ext(bdev_name, true, bio_bdev_event_cb, NULL,
				&desc);
	if (rc != 0) {
		D_ERROR("Failed to open bdev %s, %d\n", bdev_name, rc);
		return NULL;
	}

	/*
	 * bdev will be closed and bs_dev will be freed during
	 * spdk_bs_unload(), or in the internal error handling code of
	 * spdk_bs_init/load().
	 */
	D_ASSERT(desc != NULL);
	bs_dev = spdk_bdev_create_bs_dev_from_desc(desc);
	if (bs_dev == NULL) {
		D_ERROR("failed to create bs_dev\n");
		spdk_bdev_close(desc);
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

static void
destroy_bio_bdev(struct bio_bdev *d_bdev)
{
	D_ASSERT(d_list_empty(&d_bdev->bb_link));

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

static struct bio_bdev *
lookup_dev_by_id(uuid_t dev_id)
{
	struct bio_bdev	*d_bdev;

	d_list_for_each_entry(d_bdev, &nvme_glb.bd_bdevs, bb_link) {
		if (uuid_compare(d_bdev->bb_uuid, dev_id) == 0)
			return d_bdev;
	}
	return NULL;
}

void
bio_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		  void *event_ctx)
{
	if (event_ctx == NULL)
		return;

	/* TODO: Process hot remove event */
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
create_bio_bdev(struct bio_xs_context *ctxt, struct spdk_bdev *bdev)
{
	struct bio_bdev			*d_bdev;
	struct spdk_blob_store		*bs = NULL;
	struct spdk_bs_type		 bstype;
	struct smd_dev_info		*dev_info;
	uuid_t				 bs_uuid;
	int				 rc;
	bool				 new_bs = false;

	D_ALLOC_PTR(d_bdev);
	if (d_bdev == NULL) {
		D_ERROR("failed to allocate bio_bdev\n");
		return -DER_NOMEM;
	}

	D_INIT_LIST_HEAD(&d_bdev->bb_link);
	D_STRNDUP(d_bdev->bb_name, spdk_bdev_get_name(bdev),
		  strlen(spdk_bdev_get_name(bdev)));
	if (d_bdev->bb_name == NULL) {
		D_ERROR("Failed to allocate bdev name for %s\n",
			spdk_bdev_get_name(bdev));
		rc = -DER_NOMEM;
		goto error;
	}

	/*
	 * Hold the SPDK bdev by an open descriptor, otherwise, the bdev
	 * could be deconstructed by SPDK on device hot remove.
	 */
	rc = spdk_bdev_open_ext(d_bdev->bb_name, true, bio_bdev_event_cb,
				d_bdev, &d_bdev->bb_desc);
	if (rc != 0) {
		D_ERROR("Failed to hold bdev %s, %d\n", d_bdev->bb_name, rc);
		rc = daos_errno2der(-rc);
		goto error;
	}

	D_ASSERT(d_bdev->bb_desc != NULL);
	/* Try to load blobstore without specifying 'bstype' first */
	bs = load_blobstore(ctxt, d_bdev->bb_name, NULL, false);
	if (bs == NULL) {
		D_DEBUG(DB_MGMT, "Creating bs for %s\n", d_bdev->bb_name);

		/* Create blobstore if it wasn't created before */
		uuid_generate(bs_uuid);
		bs = load_blobstore(ctxt, d_bdev->bb_name, &bs_uuid, true);
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

	/* Verify if any duplicated device ID */
	if (lookup_dev_by_id(bs_uuid) != NULL) {
		D_ERROR("Dup device "DF_UUID" detected!\n", DP_UUID(bs_uuid));
		rc = -DER_EXIST;
		goto error;
	}

	/* Find the initial target count per device */
	rc = smd_dev_get_by_id(bs_uuid, &dev_info);
	if (rc == 0) {
		D_ASSERT(dev_info->sdi_tgt_cnt != 0);
		d_bdev->bb_tgt_cnt = dev_info->sdi_tgt_cnt;
		smd_free_dev_info(dev_info);
	} else if (rc == -DER_NONEXIST) {
		/* device not present in table, first target mapped to dev */
		d_bdev->bb_tgt_cnt = 0;
	} else {
		D_ERROR("Unable to get dev info for "DF_UUID"\n",
			DP_UUID(bs_uuid));
		goto error;
	}
	D_DEBUG(DB_MGMT, "Initial target count for "DF_UUID" set at %d\n",
		DP_UUID(bs_uuid), d_bdev->bb_tgt_cnt);

	uuid_copy(d_bdev->bb_uuid, bs_uuid);
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

	if (spdk_bdev_first() == NULL) {
		D_ERROR("No SPDK bdevs found!");
		rc = -DER_NONEXIST;
	}

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
	struct spdk_blob_store	*bs = NULL;
	struct bio_io_context	*ioc, *tmp;
	int			i, xs_cnt_max = BIO_XS_CNT_MAX;

	d_list_for_each_entry_safe(ioc, tmp, &ctxt->bxc_io_ctxts, bic_link) {
		d_list_del_init(&ioc->bic_link);
		if (ioc->bic_blob != NULL)
			D_WARN("Pool isn't closed. xs:%p\n", ctxt);
	}

	ABT_mutex_lock(bb->bb_mutex);
	/* Unload the blobstore in the same xstream where it was loaded. */
	if (is_bbs_owner(ctxt, bb) && bb->bb_bs != NULL) {
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
alloc_bio_blobstore(struct bio_xs_context *ctxt)
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
	rc = smd_dev_assign(chosen_bdev->bb_uuid, tgt_id);
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

	D_ASSERT(ctxt->bxc_desc == NULL);
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
		d_bdev->bb_blobstore = alloc_bio_blobstore(ctxt);
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
				    false);
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

	/* generic read only descriptor (currently used for IO stats) */
	rc = spdk_bdev_open_ext(d_bdev->bb_name, false, bio_bdev_event_cb,
				NULL, &ctxt->bxc_desc);
	if (rc != 0) {
		D_ERROR("Failed to open bdev %s, %d\n", d_bdev->bb_name, rc);
		rc = daos_errno2der(-rc);
		goto out;
	}

out:
	D_ASSERT(dev_info != NULL);
	smd_free_dev_info(dev_info);
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
		D_DEBUG(DB_MGMT, "Finalizing SPDK thread, tgt_id:%d",
			ctxt->bxc_tgt_id);

		while (!spdk_thread_is_idle(ctxt->bxc_thread))
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

	/* Skip NVMe context setup if the daos_nvme.conf isn't present */
	if (nvme_glb.bd_nvme_conf == NULL) {
		*pctxt = NULL;
		return 0;
	}

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ctxt->bxc_io_ctxts);
	ctxt->bxc_tgt_id = tgt_id;

	ABT_mutex_lock(nvme_glb.bd_mutex);

	nvme_glb.bd_xstream_cnt++;

	D_INFO("Initialize NVMe context, tgt_id:%d, init_thread:%p\n",
	       tgt_id, nvme_glb.bd_init_thread);

	/*
	 * Register SPDK thread beforehand, it could be used for poll device
	 * admin commands completions and hotplugged events in following
	 * spdk_bdev_initialize() call, it also could be used for blobstore
	 * metadata io channel in following init_bio_bdevs() call.
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
