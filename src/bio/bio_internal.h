/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __BIO_INTERNAL_H__
#define __BIO_INTERNAL_H__

#include <daos_srv/daos_engine.h>
#include <daos_srv/bio.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include <spdk/bdev.h>
#include <spdk/thread.h>

#define BIO_DMA_PAGE_SHIFT	12	/* 4K */
#define BIO_DMA_PAGE_SZ		(1UL << BIO_DMA_PAGE_SHIFT)
#define BIO_XS_CNT_MAX		48	/* Max VOS xstreams per blobstore */
/*
 * Period to query raw device health stats, auto detect faulty and transition
 * device state. 60 seconds by default. Once FAULTY state has occurred, reduce
 * monitor period to something more reasonable like 10 seconds.
 */
#define NVME_MONITOR_PERIOD	    (60ULL * (NSEC_PER_SEC / NSEC_PER_USEC))
#define NVME_MONITOR_SHORT_PERIOD   (3ULL * (NSEC_PER_SEC / NSEC_PER_USEC))

struct bio_bulk_args {
	void		*ba_bulk_ctxt;
	unsigned int	 ba_bulk_perm;
};

/* Cached bulk handle for avoiding expensive MR */
struct bio_bulk_hdl {
	/* Link to bbg_idle_bulks */
	d_list_t		 bbh_link;
	/* Bulk handle used by upper layer caller */
	void			*bbh_bulk;
	/* DMA chunk the hdl localted on */
	struct bio_dma_chunk	*bbh_chunk;
	/* Page offset (4k pages) within the chunk */
	unsigned int		 bbh_pg_idx;
	/* Bulk offset in bytes */
	unsigned int		 bbh_bulk_off;
	/* Flags */
	unsigned int		 bbh_inuse:1;
};

/* Bulk handle group, categorized by bulk size */
struct bio_bulk_group {
	/* Link to bbc_grp_lru */
	d_list_t		 bbg_lru_link;
	/* All DMA chunks in this group */
	d_list_t		 bbg_dma_chks;
	/* All free bulk handles in this group */
	d_list_t		 bbg_idle_bulks;
	/* Bulk size in pages (4k page) */
	unsigned int		 bbg_bulk_pgs;
	/* How many chunks used for this group */
	unsigned int		 bbg_chk_cnt;
};

/* DMA buffer is managed in chunks */
struct bio_dma_chunk {
	/* Link to edb_idle_list or edb_used_list or bbg_dma_chks */
	d_list_t	 bdc_link;
	/* Base pointer of the chunk address */
	void		*bdc_ptr;
	/* Page offset (4K page) to unused fraction */
	unsigned int	 bdc_pg_idx;
	/* Being used by how many I/O descriptors */
	unsigned int	 bdc_ref;
	/* Chunk type */
	unsigned int	 bdc_type;
	/* == Bulk handle caching related fields == */
	struct bio_bulk_group	*bdc_bulk_grp;
	struct bio_bulk_hdl	*bdc_bulks;
	unsigned int		 bdc_bulk_cnt;
	unsigned int		 bdc_bulk_idle;
};

/* Bulk handle cache for caching various sized bulk handles */
struct bio_bulk_cache {
	/* Bulk group array */
	struct bio_bulk_group	 *bbc_grps;
	struct bio_bulk_group	**bbc_sorted;
	unsigned int		  bbc_grp_max;
	unsigned int		  bbc_grp_cnt;
	/* All groups in LRU */
	d_list_t		  bbc_grp_lru;
};

/*
 * Per-xstream DMA buffer, used as SPDK dma I/O buffer or as temporary
 * RDMA buffer for ZC fetch/update over NVMe devices.
 */
struct bio_dma_buffer {
	d_list_t		 bdb_idle_list;
	d_list_t		 bdb_used_list;
	struct bio_dma_chunk	*bdb_cur_chk[BIO_CHK_TYPE_MAX];
	unsigned int		 bdb_used_cnt[BIO_CHK_TYPE_MAX];
	unsigned int		 bdb_tot_cnt;
	unsigned int		 bdb_active_iods;
	ABT_cond		 bdb_wait_iods;
	ABT_mutex		 bdb_mutex;
	struct bio_bulk_cache	 bdb_bulk_cache;
};

#define BIO_PROTO_NVME_STATS_LIST					\
	X(bdh_du_written, "commands/data_units_written",		\
	  "number of 512b data units written to the controller",	\
	  "data units", D_TM_COUNTER)					\
	X(bdh_du_read, "commands/data_units_read",			\
	  "number of 512b data units read from to the controller",	\
	  "data units", D_TM_COUNTER)					\
	X(bdh_write_cmds, "commands/host_write_cmds",			\
	  "number of write commands completed by to the controller",	\
	  "cmds", D_TM_COUNTER)						\
	X(bdh_read_cmds, "commands/host_read_cmds",			\
	  "number of read commands completed by to the controller",	\
	  "cmds", D_TM_COUNTER)						\
	X(bdh_ctrl_busy_time, "commands/ctrl_busy_time",		\
	  "Amount of time the controller is busy with I/O commands",	\
	  "minutes", D_TM_COUNTER)					\
	X(bdh_media_errs, "commands/media_errs",			\
	  "Number of unrecovered data integrity error",			\
	  "errs", D_TM_COUNTER)						\
	X(bdh_read_errs, "commands/read_errs",				\
	  "Number of errors reported to the engine on read commands",	\
	  "errs", D_TM_COUNTER)						\
	X(bdh_write_errs, "commands/write_errs",			\
	  "Number of errors reported to the engine on write commands",	\
	  "errs", D_TM_COUNTER)						\
	X(bdh_unmap_errs, "commands/unmap_errs",			\
	  "Number of errors reported to the engine on unmap/trim commands",\
	  "errs", D_TM_COUNTER)						\
	X(bdh_checksum_errs, "commands/checksum_mismatch",		\
	  "Number of checksum mismatch detected by the engine",		\
	  "errs", D_TM_COUNTER)						\
	X(bdh_power_cycles, "power_cycles",				\
	  "Number of power cycles",					\
	  "cycles", D_TM_COUNTER)					\
	X(bdh_power_on_hours, "power_on_hours",				\
	  "Number of power-on hours cycles",				\
	  "hours", D_TM_COUNTER)					\
	X(bdh_unsafe_shutdowns, "unsafe_shutdowns",			\
	  "Number of unsafe shutdowns (no notification prior to power loss)",  \
	  "shutdowns", D_TM_COUNTER)					\
	X(bdh_temp, "temp/current",					\
	  "Current SSD temperature",					\
	  "kelvins", D_TM_GAUGE)					\
	X(bdh_temp_warn, "temp/warn",					\
	  "Set to 1 if temperature is above threshold",			\
	  "", D_TM_GAUGE)						\
	X(bdh_temp_warn_time, "temp/warn_time",				\
	  "Amount of time the controller operated above warn temp threshold",  \
	  "minutes", D_TM_COUNTER)					\
	X(bdh_temp_crit_time, "temp/crit_time",				\
	  "Amount of time the controller operated above crit temp threshold",  \
	  "minutes", D_TM_COUNTER)					\
	X(bdh_percent_used, "reliability/percentage_used",		\
	  "Estimate of the percentage of NVM subsystem life used based on the "\
	  "actual usage and the manufacturer's prediction of NVM life",	\
	  "%", D_TM_COUNTER)						\
	X(bdh_avail_spare, "reliability/avail_spare",			\
	  "Percentage of remaining spare capacity available",		\
	  "%", D_TM_COUNTER)						\
	X(bdh_avail_spare_thres, "reliability/avail_spare_threshold",	\
	  "Threshold for available spare value",			\
	  "%", D_TM_COUNTER)						\
	X(bdh_avail_spare_warn, "reliability/avail_spare_warn",		\
	  "Set to 1 when available spare has fallen below threshold",	\
	  "", D_TM_GAUGE)						\
	X(bdh_reliability_warn, "reliability/reliability_warn",		\
	  "Set to 1 when NVM subsystem has been degraded due to significant "  \
	  "media-related errors",					\
	  "", D_TM_GAUGE)						\
	X(bdh_read_only_warn, "read_only_warn",				\
	  "Set to 1 when media has been placed in read-only mode",	\
	  "", D_TM_GAUGE)						\
	X(bdh_volatile_mem_warn, "volatile_mem_warn",			\
	  "Set to 1 when volatile memory backup device has failed",	\
	  "", D_TM_GAUGE)

#define BIO_PROTO_NVME_VENDOR_STATS_LIST				\
	Y(bdh_prog_fail_cnt_norm, "vendor/program_fail_cnt_norm",	\
	  "Percent remaining of allowable program fails",		\
	  "%", D_TM_COUNTER)						\
	Y(bdh_prog_fail_cnt_raw, "vendor/program_fail_cnt_raw",		\
	  "Total count of current program fails",			\
	  "", D_TM_COUNTER)						\
	Y(bdh_erase_fail_cnt_norm, "vendor/erase_fail_cnt_norm",	\
	  "Percent remaining of allowable erase fails",			\
	  "%", D_TM_COUNTER)						\
	Y(bdh_erase_fail_cnt_raw, "vendor/erase_fail_cnt_raw",		\
	  "Total count of current erase fails",				\
	  "", D_TM_COUNTER)						\
	Y(bdh_wear_leveling_cnt_norm, "vendor/wear_leveling_cnt_norm",	\
	  "Wear leveling count remaining, decrements from 100 to 0",	\
	  "", D_TM_COUNTER)						\
	Y(bdh_wear_leveling_cnt_min, "vendor/wear_leveling_cnt_min",	\
	  "Wear leveling minimum erase cycle",				\
	  "", D_TM_COUNTER)						\
	Y(bdh_wear_leveling_cnt_max, "vendor/wear_leveling_cnt_max",    \
	  "Wear leveling maximum erase cycle",                          \
	  "", D_TM_COUNTER)                                             \
	Y(bdh_wear_leveling_cnt_avg, "vendor/wear_leveling_cnt_avg",    \
	  "Wear leveling average erase cycle",                          \
	  "", D_TM_COUNTER)						\
	Y(bdh_endtoend_err_cnt_raw, "vendor/endtoend_err_cnt_raw",	\
	  "End-to-End detected and corrected errors by hardware",	\
	  "", D_TM_COUNTER)						\
	Y(bdh_crc_err_cnt_raw, "vendor/crc_err_cnt_raw",		\
	  "PCIe Interface CRC errors encountered",			\
	  "", D_TM_COUNTER)						\
	Y(bdh_media_wear_raw, "vendor/media_wear_raw",			\
	  "Wear seen by the SSD as a percentage of the maximum rated cycles", \
	  "%", D_TM_COUNTER)						\
	Y(bdh_host_reads_raw, "vendor/host_reads_raw",			\
	  "Percentage of I/O operations that are a read operation",	\
	  "%", D_TM_COUNTER)						\
	Y(bdh_workload_timer_raw, "vendor/crc_workload_timer_raw",	\
	  "The elapsed time since starting the workload timer",		\
	  "minutes", D_TM_COUNTER)					\
	Y(bdh_thermal_throttle_status, "vendor/thermal_throttle_status_raw", \
	  "Thermal throttle status",					\
	  "%", D_TM_COUNTER)						\
	Y(bdh_thermal_throttle_event_cnt, "vendor/thermal_throttle_event_cnt", \
	  "Thermal throttling event count",				\
	  "", D_TM_COUNTER)						\
	Y(bdh_retry_buffer_overflow_cnt, "vendor/retry_buffer_overflow_cnt", \
	  "Retry Buffer overflow count",				\
	  "", D_TM_COUNTER)						\
	Y(bdh_pll_lock_loss_cnt, "vendor/pll_lock_loss_cnt",		\
	  "PCIe Refclock PLL unlock count",				\
	  "", D_TM_COUNTER)						\
	Y(bdh_nand_bytes_written, "vendor/nand_bytes_written",		\
	  "NAND bytes written (1 count = 32 MiB)",			\
	  "", D_TM_COUNTER)						\
	Y(bdh_host_bytes_written, "vendor/host_bytes_written",		\
	  "Host bytes written (1 count = 32 MiB)",			\
	  "", D_TM_COUNTER)

/*
 * SPDK device health monitoring.
 */
struct bio_dev_health {
	struct nvme_stats	 bdh_health_state;
	/* writable open descriptor for health info polling */
	struct spdk_bdev_desc  *bdh_desc;
	struct spdk_io_channel *bdh_io_channel;
	void		       *bdh_health_buf; /* health info logs */
	void		       *bdh_ctrlr_buf; /* controller data */
	void		       *bdh_error_buf; /* device error logs */
	void		       *bdh_intel_smart_buf; /*Intel SMART attributes*/
	uint64_t		bdh_stat_age;
	unsigned int		bdh_inflights;
	uint16_t		bdh_vendor_id; /* PCI vendor ID */

	/**
	 * NVMe statistics exported via telemetry framework
	 */
#define	X(field, fname, desc, unit, type) struct d_tm_node_t *field;
	 BIO_PROTO_NVME_STATS_LIST
#undef X

#define	Y(field, fname, desc, unit, type) struct d_tm_node_t *field;
	 BIO_PROTO_NVME_VENDOR_STATS_LIST
#undef Y

};

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
	/*
	 * If a VMD LED event takes place, the original LED state and start
	 * time will be saved in order to restore the LED to its original
	 * state after allotted time.
	 */
	int			 bb_led_state;
	uint64_t		 bb_led_start_time;
	bool			 bb_removed;
	bool			 bb_replacing;
	bool			 bb_trigger_reint;
	/*
	 * If a faulty device is replaced but still plugged, we'll keep
	 * the 'faulty' information here, so that we know this device was
	 * marked as faulty (at least before next server restart).
	 */
	bool			 bb_faulty;
};


/*
 * SPDK blobstore isn't thread safe and there can be only one SPDK
 * blobstore for certain NVMe device.
 */
struct bio_blobstore {
	ABT_mutex		 bb_mutex;
	ABT_cond		 bb_barrier;
	/* Back pointer to bio_bdev */
	struct bio_bdev		*bb_dev;
	struct spdk_blob_store	*bb_bs;
	/*
	 * The xstream responsible for blobstore load/unload, monitor
	 * and faulty/reint reaction.
	 */
	struct bio_xs_context	*bb_owner_xs;
	/* All the xstreams using the blobstore */
	struct bio_xs_context	**bb_xs_ctxts;
	/* Device/blobstore health monitoring info */
	struct bio_dev_health	 bb_dev_health;
	enum bio_bs_state	 bb_state;
	/* Blobstore used by how many xstreams */
	int			 bb_ref;
	/*
	 * Blobstore is held and being accessed by requests from upper
	 * layer, teardown procedure needs be postponed.
	 */
	int			 bb_holdings;
	/* Flags indicating blobstore load/unload is in-progress */
	unsigned		 bb_loading:1,
				 bb_unloading:1;
};

/* Per-xstream NVMe context */
struct bio_xs_context {
	int			 bxc_tgt_id;
	unsigned int		 bxc_blob_rw;	/* inflight blob read/write */
	struct spdk_thread	*bxc_thread;
	struct bio_blobstore	*bxc_blobstore;
	struct spdk_io_channel	*bxc_io_channel;
	struct bio_dma_buffer	*bxc_dma_buf;
	d_list_t		 bxc_io_ctxts;
};

/* Per VOS instance I/O context */
struct bio_io_context {
	d_list_t		 bic_link; /* link to bxc_io_ctxts */
	struct umem_instance	*bic_umem;
	uint64_t		 bic_pmempool_uuid;
	struct spdk_blob	*bic_blob;
	struct bio_xs_context	*bic_xs_ctxt;
	uint32_t		 bic_inflight_dmas;
	uint32_t		 bic_io_unit;
	uuid_t			 bic_pool_id;
	unsigned int		 bic_opening:1,
				 bic_closing:1;
};

/* A contiguous DMA buffer region reserved by certain io descriptor */
struct bio_rsrvd_region {
	/* The DMA chunk where the region is located */
	struct bio_dma_chunk	*brr_chk;
	/* Start page idx within the DMA chunk */
	unsigned int		 brr_pg_idx;
	/* Offset within the SPDK blob in bytes */
	uint64_t		 brr_off;
	/* End (not included) in bytes */
	uint64_t		 brr_end;
	/* Media type this DMA region mapped to */
	uint8_t			 brr_media;
};

/* Reserved DMA buffer for certain io descriptor */
struct bio_rsrvd_dma {
	/* DMA regions reserved by the io descriptor */
	struct bio_rsrvd_region	 *brd_regions;
	/* Capacity of the region array */
	unsigned int		  brd_rg_max;
	/* Total number of reserved regions */
	unsigned int		  brd_rg_cnt;
	/* Pointer array for all referenced DMA chunks */
	struct bio_dma_chunk	**brd_dma_chks;
	/* Capacity of the pointer array */
	unsigned int		  brd_chk_max;
	/* Total number of chunks being referenced */
	unsigned int		  brd_chk_cnt;
};

/* I/O descriptor */
struct bio_desc {
	struct bio_io_context	*bd_ctxt;
	/* DMA buffers reserved by this io descriptor */
	struct bio_rsrvd_dma	 bd_rsrvd;
	/* Report blob i/o completion */
	ABT_eventual		 bd_dma_done;
	/* Inflight SPDK DMA transfers */
	unsigned int		 bd_inflights;
	int			 bd_result;
	unsigned int		 bd_chk_type;
	unsigned int		 bd_type;
	/* Flags */
	unsigned int		 bd_buffer_prep:1,
				 bd_dma_issued:1,
				 bd_retry:1,
				 bd_rdma:1;
	/* Cached bulk handles being used by this IOD */
	struct bio_bulk_hdl    **bd_bulk_hdls;
	unsigned int		 bd_bulk_max;
	unsigned int		 bd_bulk_cnt;
	/* SG lists involved in this io descriptor */
	unsigned int		 bd_sgl_cnt;
	struct bio_sglist	 bd_sgls[0];
};

static inline struct spdk_thread *
owner_thread(struct bio_blobstore *bbs)
{
	return bbs->bb_owner_xs->bxc_thread;
}

static inline bool
is_blob_valid(struct bio_io_context *ctxt)
{
	return ctxt->bic_blob != NULL && !ctxt->bic_closing;
}

static inline uint64_t
page2io_unit(struct bio_io_context *ctxt, uint64_t page)
{
	return page * (BIO_DMA_PAGE_SZ / ctxt->bic_io_unit);
}

enum {
	BDEV_CLASS_NVME = 0,
	BDEV_CLASS_MALLOC,
	BDEV_CLASS_AIO,
	BDEV_CLASS_UNKNOWN
};

static inline int
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

static inline char *
bio_state_enum_to_str(enum bio_bs_state state)
{
	switch (state) {
	case BIO_BS_STATE_NORMAL: return "NORMAL";
	case BIO_BS_STATE_FAULTY: return "FAULTY";
	case BIO_BS_STATE_TEARDOWN: return "TEARDOWN";
	case BIO_BS_STATE_OUT: return "OUT";
	case BIO_BS_STATE_SETUP: return "SETUP";
	}

	return "Undefined state";
}

struct media_error_msg {
	struct bio_blobstore	*mem_bs;
	int			 mem_err_type;
	int			 mem_tgt_id;
};

/* bio_xstream.c */
extern bool		bio_scm_rdma;
extern unsigned int	bio_chk_sz;
extern unsigned int	bio_chk_cnt_max;
int xs_poll_completion(struct bio_xs_context *ctxt, unsigned int *inflights,
		       uint64_t timeout);
void bio_bdev_event_cb(enum spdk_bdev_event_type type, struct spdk_bdev *bdev,
		       void *event_ctx);
struct spdk_thread *init_thread(void);
void bio_release_bdev(void *arg);
bool is_server_started(void);
d_list_t *bio_bdev_list(void);
struct spdk_blob_store *
load_blobstore(struct bio_xs_context *ctxt, char *bdev_name, uuid_t *bs_uuid,
	       bool create, bool async,
	       void (*async_cb)(void *arg, struct spdk_blob_store *bs, int rc),
	       void *async_arg);
int
unload_blobstore(struct bio_xs_context *ctxt, struct spdk_blob_store *bs);
bool is_init_xstream(struct bio_xs_context *ctxt);
struct bio_bdev *lookup_dev_by_id(uuid_t dev_id);
void setup_bio_bdev(void *arg);
void destroy_bio_bdev(struct bio_bdev *d_bdev);
void replace_bio_bdev(struct bio_bdev *old_dev, struct bio_bdev *new_dev);
bool bypass_health_collect(void);

/* bio_buffer.c */
void dma_buffer_destroy(struct bio_dma_buffer *buf);
struct bio_dma_buffer *dma_buffer_create(unsigned int init_cnt);
void bio_memcpy(struct bio_desc *biod, uint16_t media, void *media_addr,
		void *addr, ssize_t n);
int dma_map_one(struct bio_desc *biod, struct bio_iov *biov, void *arg);
int iod_add_region(struct bio_desc *biod, struct bio_dma_chunk *chk,
		   unsigned int chk_pg_idx, uint64_t off, uint64_t end,
		   uint8_t media);
int dma_buffer_grow(struct bio_dma_buffer *buf, unsigned int cnt);

static inline struct bio_dma_buffer *
iod_dma_buf(struct bio_desc *biod)
{
	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt);
	D_ASSERT(biod->bd_ctxt->bic_xs_ctxt->bxc_dma_buf);

	return biod->bd_ctxt->bic_xs_ctxt->bxc_dma_buf;
}

static inline void
dma_biov2pg(struct bio_iov *biov, uint64_t *off, uint64_t *end,
	    unsigned int *pg_cnt, unsigned int *pg_off)
{
	*off = bio_iov2raw_off(biov);
	*end = bio_iov2raw_off(biov) + bio_iov2raw_len(biov);

	if (bio_iov2media(biov) == DAOS_MEDIA_SCM) {
		*pg_cnt = (*end - *off + BIO_DMA_PAGE_SZ - 1) >>
				BIO_DMA_PAGE_SHIFT;
		*pg_off = 0;
	} else {
		*pg_cnt = ((*end + BIO_DMA_PAGE_SZ - 1) >> BIO_DMA_PAGE_SHIFT) -
				(*off >> BIO_DMA_PAGE_SHIFT);
		*pg_off = *off & ((uint64_t)BIO_DMA_PAGE_SZ - 1);
	}
	D_ASSERT(*pg_cnt > 0);
}

/* bio_bulk.c */
int bulk_map_one(struct bio_desc *biod, struct bio_iov *biov, void *data);
void bulk_iod_release(struct bio_desc *biod);
int bulk_cache_create(struct bio_dma_buffer *bdb);
void bulk_cache_destroy(struct bio_dma_buffer *bdb);
int bulk_reclaim_chunk(struct bio_dma_buffer *bdb,
		       struct bio_bulk_group *ex_grp);
static inline void
dump_dma_info(struct bio_dma_buffer *bdb)
{
	struct bio_bulk_cache	*bbc = &bdb->bdb_bulk_cache;
	struct bio_bulk_group	*bbg;
	int			 i, bulk_grps = 0, bulk_chunks = 0;

	D_EMIT("chk_size:%u, tot_chk:%u/%u, active_iods:%u, used:%u,%u,%u\n",
		bio_chk_sz, bdb->bdb_tot_cnt, bio_chk_cnt_max,
		bdb->bdb_active_iods, bdb->bdb_used_cnt[BIO_CHK_TYPE_IO],
		bdb->bdb_used_cnt[BIO_CHK_TYPE_LOCAL],
		bdb->bdb_used_cnt[BIO_CHK_TYPE_REBUILD]);

	/* cached bulk info */
	for (i = 0; i < bbc->bbc_grp_cnt; i++) {
		bbg = &bbc->bbc_grps[i];

		if (bbg->bbg_chk_cnt == 0)
			continue;

		bulk_grps++;
		bulk_chunks += bbg->bbg_chk_cnt;

		D_EMIT("bulk_grp %d: bulk_size:%u, chunks:%u\n",
			i, bbg->bbg_bulk_pgs, bbg->bbg_chk_cnt);
	}
	D_EMIT("bulk_grps:%d, bulk_chunks:%d\n", bulk_grps, bulk_chunks);
}

/* bio_monitor.c */
int bio_init_health_monitoring(struct bio_blobstore *bb, char *bdev_name);
void bio_fini_health_monitoring(struct bio_blobstore *bb);
void bio_bs_monitor(struct bio_xs_context *ctxt, uint64_t now);
void bio_media_error(void *msg_arg);
void bio_export_health_stats(struct bio_blobstore *bb, char *bdev_name);
void bio_export_vendor_health_stats(struct bio_blobstore *bb, char *bdev_name);
void bio_set_vendor_id(struct bio_blobstore *bb, char *bdev_name);

/* bio_context.c */
int bio_blob_close(struct bio_io_context *ctxt, bool async);
int bio_blob_open(struct bio_io_context *ctxt, bool async);

/* bio_recovery.c */
int bio_bs_state_transit(struct bio_blobstore *bbs);
int bio_bs_state_set(struct bio_blobstore *bbs, enum bio_bs_state new_state);

/* bio_device.c */
void bio_led_event_monitor(struct bio_xs_context *ctxt, uint64_t now);
int fill_in_traddr(struct bio_dev_info *b_info, char *dev_name);

#endif /* __BIO_INTERNAL_H__ */
