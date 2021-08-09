/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Blob I/O library provides functionality of blob I/O over SG list consists
 * of SCM or NVMe IOVs, PMDK & SPDK are used for SCM and NVMe I/O respectively.
 */

#ifndef __BIO_API_H__
#define __BIO_API_H__

#include <daos/mem.h>
#include <daos/common.h>
#include <daos_srv/control.h>
#include <abt.h>

#define BIO_ADDR_IS_HOLE(addr) ((addr)->ba_flags == BIO_FLAG_HOLE)
#define BIO_ADDR_SET_HOLE(addr) ((addr)->ba_flags |= BIO_FLAG_HOLE)
#define BIO_ADDR_SET_NOT_HOLE(addr) ((addr)->ba_flags &= ~(BIO_FLAG_HOLE))
#define BIO_ADDR_IS_DEDUP(addr) ((addr)->ba_flags == BIO_FLAG_DEDUP)
#define BIO_ADDR_SET_DEDUP(addr) ((addr)->ba_flags |= BIO_FLAG_DEDUP)
#define BIO_ADDR_SET_NOT_DEDUP(addr) ((addr)->ba_flags &= ~(BIO_FLAG_DEDUP))
#define BIO_ADDR_IS_DEDUP_BUF(addr) ((addr)->ba_flags == BIO_FLAG_DEDUP_BUF)
#define BIO_ADDR_SET_DEDUP_BUF(addr) ((addr)->ba_flags |= BIO_FLAG_DEDUP_BUF)
#define BIO_ADDR_SET_NOT_DEDUP_BUF(addr)	\
			((addr)->ba_flags &= ~(BIO_FLAG_DEDUP_BUF))

/* Can support up to 16 flags for a BIO address */
enum BIO_FLAG {
	/* The address is a hole */
	BIO_FLAG_HOLE = (1 << 0),
	/* The address is a deduped extent */
	BIO_FLAG_DEDUP = (1 << 1),
	/* The address is a buffer for dedup verify */
	BIO_FLAG_DEDUP_BUF = (1 << 2),
};

typedef struct {
	/*
	 * Byte offset within PMDK pmemobj pool for SCM;
	 * Byte offset within SPDK blob for NVMe.
	 */
	uint64_t	ba_off;
	/* DAOS_MEDIA_SCM or DAOS_MEDIA_NVME */
	uint8_t		ba_type;
	uint8_t		ba_pad1;
	/* See BIO_FLAG enum */
	uint16_t	ba_flags;
	uint32_t	ba_pad2;
} bio_addr_t;

struct sys_db;

/** Ensure this remains compatible */
D_CASSERT(sizeof(((bio_addr_t *)0)->ba_off) == sizeof(umem_off_t));

struct bio_iov {
	/*
	 * For SCM, it's direct memory address of 'ba_off';
	 * For NVMe, it's a DMA buffer allocated by SPDK malloc API.
	 */
	void		*bi_buf;
	/* Data length in bytes */
	size_t		 bi_data_len;
	bio_addr_t	 bi_addr;

	/** can be used to fetch more than actual address. Useful if more
	 * data is needed for processing (like checksums) than requested.
	 * Prefix and suffix are needed because 'extra' needed data might
	 * be before or after actual requested data.
	 */
	size_t		 bi_prefix_len; /** bytes before */
	size_t		 bi_suffix_len; /** bytes after */
};

struct bio_sglist {
	struct bio_iov	*bs_iovs;
	unsigned int	 bs_nr;
	unsigned int	 bs_nr_out;
};

/* Opaque I/O descriptor */
struct bio_desc;
/* Opaque I/O context */
struct bio_io_context;
/* Opaque per-xstream context */
struct bio_xs_context;

/**
 * Header for SPDK blob per VOS pool
 */
struct bio_blob_hdr {
	uint32_t	bbh_magic;
	uint32_t	bbh_blk_sz;
	uint32_t	bbh_hdr_sz; /* blocks reserved for blob header */
	uint32_t	bbh_vos_id; /* service xstream id */
	uint64_t	bbh_blob_id;
	uuid_t		bbh_blobstore;
	uuid_t		bbh_pool;
};

enum bio_bs_state {
	/* Healthy and fully functional */
	BIO_BS_STATE_NORMAL	= 0,
	/* Being detected & marked as faulty */
	BIO_BS_STATE_FAULTY,
	/* Affected targets are marked as DOWN, safe to tear down blobstore */
	BIO_BS_STATE_TEARDOWN,
	/* Blobstore is torn down, all in-memory structures cleared */
	BIO_BS_STATE_OUT,
	/* Setup all in-memory structures, load blobstore */
	BIO_BS_STATE_SETUP,
};

static inline void
bio_addr_set(bio_addr_t *addr, uint16_t type, uint64_t off)
{
	addr->ba_type = type;
	addr->ba_off = umem_off2offset(off);
}

static inline bool
bio_addr_is_hole(const bio_addr_t *addr)
{
	return BIO_ADDR_IS_HOLE(addr);
}

static inline void
bio_addr_set_hole(bio_addr_t *addr, uint16_t hole)
{
	if (hole == 0)
		BIO_ADDR_SET_NOT_HOLE(addr);
	else
		BIO_ADDR_SET_HOLE(addr);
}

static inline void
bio_iov_set(struct bio_iov *biov, bio_addr_t addr, uint64_t data_len)
{
	biov->bi_addr = addr;
	biov->bi_data_len = data_len;
	biov->bi_buf = NULL;
	biov->bi_prefix_len = 0;
	biov->bi_suffix_len = 0;
}

static inline void
bio_iov_set_extra(struct bio_iov *biov,	uint64_t prefix_len,
		  uint64_t suffix_len)
{
	biov->bi_prefix_len = prefix_len;
	biov->bi_suffix_len = suffix_len;
	biov->bi_addr.ba_off -= prefix_len;
	biov->bi_data_len += prefix_len + suffix_len;
}

static inline uint64_t
bio_iov2off(const struct bio_iov *biov)
{
	D_ASSERT(biov->bi_prefix_len == 0 && biov->bi_suffix_len == 0);
	return biov->bi_addr.ba_off;
}

static inline uint64_t
bio_iov2len(const struct bio_iov *biov)
{
	D_ASSERT(biov->bi_prefix_len == 0 && biov->bi_suffix_len == 0);
	return biov->bi_data_len;
}

static inline void
bio_iov_set_len(struct bio_iov *biov, uint64_t len)
{
	biov->bi_data_len = len;
}

static inline void *
bio_iov2buf(const struct bio_iov *biov)
{
	D_ASSERT(biov->bi_prefix_len == 0 && biov->bi_suffix_len == 0);
	return biov->bi_buf;
}

static inline uint64_t
bio_iov2raw_off(const struct bio_iov *biov)
{
	return biov->bi_addr.ba_off;
}

static inline uint64_t
bio_iov2raw_len(const struct bio_iov *biov)
{
	return biov->bi_data_len;
}

static inline void *
bio_iov2raw_buf(const struct bio_iov *biov)
{
	return biov->bi_buf;
}

static inline void
bio_iov_set_raw_buf(struct bio_iov *biov, void *val)
{
	biov->bi_buf = val;
}

static inline void
bio_iov_alloc_raw_buf(struct bio_iov *biov, uint64_t len)
{
	D_ALLOC(biov->bi_buf, len);
}

static inline void *
bio_iov2req_buf(const struct bio_iov *biov)
{
	return biov->bi_buf + biov->bi_prefix_len;
}

static inline uint64_t
bio_iov2req_off(const struct bio_iov *biov)
{
	return biov->bi_addr.ba_off + biov->bi_prefix_len;
}

static inline uint64_t
bio_iov2req_len(const struct bio_iov *biov)
{
	return biov->bi_data_len - (biov->bi_prefix_len + biov->bi_suffix_len);
}

static inline
uint8_t bio_iov2media(const struct bio_iov *biov)
{
	return biov->bi_addr.ba_type;
}

static inline int
bio_sgl_init(struct bio_sglist *sgl, unsigned int nr)
{
	sgl->bs_nr_out = 0;
	sgl->bs_nr = nr;

	if (nr == 0) {
		sgl->bs_iovs = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(sgl->bs_iovs, nr);

	return sgl->bs_iovs == NULL ? -DER_NOMEM : 0;
}

static inline void
bio_sgl_fini(struct bio_sglist *sgl)
{
	if (sgl == NULL || sgl->bs_iovs == NULL)
		return;

	D_FREE(sgl->bs_iovs);
	sgl->bs_nr_out = 0;
	sgl->bs_nr = 0;
}

/*
 * Convert bio_sglist into d_sg_list_t, caller is responsible to
 * call d_sgl_fini(sgl, false) to free iovs.
 */
static inline int
bio_sgl_convert(struct bio_sglist *bsgl, d_sg_list_t *sgl)
{
	int i, rc;

	D_ASSERT(sgl != NULL);
	D_ASSERT(bsgl != NULL);

	rc = d_sgl_init(sgl, bsgl->bs_nr_out);
	if (rc != 0)
		return rc;

	sgl->sg_nr_out = bsgl->bs_nr_out;

	for (i = 0; i < sgl->sg_nr_out; i++) {
		struct bio_iov	*biov = &bsgl->bs_iovs[i];
		d_iov_t	*iov = &sgl->sg_iovs[i];

		/* Skip bulk transfer for deduped extent */
		if (BIO_ADDR_IS_DEDUP(&biov->bi_addr))
			iov->iov_buf = NULL;
		else
			iov->iov_buf = bio_iov2req_buf(biov);
		iov->iov_len = bio_iov2req_len(biov);
		iov->iov_buf_len = iov->iov_len;
	}

	return 0;
}

/** Get a specific bio_iov from a bio_sglist if the idx exists. Otherwise
 * return NULL
 */
static inline struct bio_iov *
bio_sgl_iov(struct bio_sglist *bsgl, uint32_t idx)
{
	if (idx >= bsgl->bs_nr_out)
		return NULL;

	return &bsgl->bs_iovs[idx];
}

/** Count the number of biovs that are 'holes' in a bsgl */
static inline uint32_t
bio_sgl_holes(struct bio_sglist *bsgl)
{
	uint32_t result = 0;
	int i;

	for (i = 0; i < bsgl->bs_nr_out; i++) {
		if (bio_addr_is_hole(&bsgl->bs_iovs[i].bi_addr))
			result++;
	}

	return result;
}

/*
 * Device information inquired from BIO. It's almost identical to
 * smd_dev_info currently, but it could be extended in the future.
 *
 * NB. Move it to control.h if it needs be shared by control plane.
 */
struct bio_dev_info {
	d_list_t		bdi_link;
	uuid_t			bdi_dev_id;
	uint32_t		bdi_flags;	/* defined in control.h */
	uint32_t		bdi_tgt_cnt;
	int		       *bdi_tgts;
	char		       *bdi_traddr;
	uint32_t		bdi_dev_type;	/* reserved */
};

static inline void
bio_free_dev_info(struct bio_dev_info *dev_info)
{
	if (dev_info->bdi_tgts != NULL)
		D_FREE(dev_info->bdi_tgts);
	if (dev_info->bdi_traddr != NULL)
		D_FREE(dev_info->bdi_traddr);
	D_FREE(dev_info);
}

/**
 * List all devices.
 *
 * \param[IN] ctxt	Per xstream NVMe context
 * \param[OUT] dev_list	Returned device list
 * \param[OUT] dev_cnt	Device count in the list
 *
 * \return		Zero on success, negative value on error
 */
int bio_dev_list(struct bio_xs_context *ctxt, d_list_t *dev_list, int *dev_cnt);

/**
 * Callbacks called on NVMe device state transition
 *
 * \param tgt_ids[IN]	Affected target IDs
 * \param tgt_cnt[IN]	Target count
 *
 * \return		0: Reaction finished;
 *			1: Reaction is in progress;
 *			-ve: Error happened;
 */
struct bio_reaction_ops {
	int (*faulty_reaction)(int *tgt_ids, int tgt_cnt);
	int (*reint_reaction)(int *tgt_ids, int tgt_cnt);
	int (*ioerr_reaction)(int err_type, int tgt_id);
};

/*
 * Register faulty/reint reaction callbacks.
 *
 * \param ops[IN]	Reaction callback functions
 */
void bio_register_ract_ops(struct bio_reaction_ops *ops);

/*
 * Register bulk operations for bulk cache.
 *
 * \param[IN]	bulk_create	Bulk create operation
 * \param[IN]	bulk_free	Bulk free operation
 */
void bio_register_bulk_ops(int (*bulk_create)(void *ctxt, d_sg_list_t *sgl,
					      unsigned int perm,
					      void **bulk_hdl),
			   int (*bulk_free)(void *bulk_hdl));
/**
 * Global NVMe initialization.
 *
 * \param[IN] nvme_conf		NVMe config file
 * \param[IN] shm_id		shm id to enable multiprocess mode in SPDK
 * \param[IN] mem_size		SPDK memory alloc size when using primary mode
 * \param[IN] hugepage_size	Configured hugepage size on system
 * \paran[IN] tgt_nr		Number of targets
 * \param[IN] db		persistent database to store SMD data
 * \param[IN] bypass		Set to bypass health data collection
 *
 * \return		Zero on success, negative value on error
 */
int bio_nvme_init(const char *nvme_conf, int shm_id, int mem_size,
		  int hugepage_size, int tgt_nr, struct sys_db *db,
		  bool bypass);

/**
 * Global NVMe finilization.
 *
 * \return		N/A
 */
void bio_nvme_fini(void);

/**
 * Check if NVMe is configured
 */
bool bio_nvme_configured(void);

enum {
	/* Notify BIO that all xsxtream contexts created */
	BIO_CTL_NOTIFY_STARTED	= 0,
};

/**
 * Manipulate global NVMe configuration/state.
 *
 * \param[IN] cmd	Ctl command
 * \param[IN] arg	Ctl argument
 *
 * \return		Zero on success, negative value on error
 */
int bio_nvme_ctl(unsigned int cmd, void *arg);

/*
 * Initialize SPDK env and per-xstream NVMe context.
 *
 * \param[OUT] pctxt	Per-xstream NVMe context to be returned
 * \param[IN] tgt_id	Target ID (mapped to a VOS xstream)
 *
 * \returns		Zero on success, negative value on error
 */
int bio_xsctxt_alloc(struct bio_xs_context **pctxt, int tgt_id);

/*
 * Finalize per-xstream NVMe context and SPDK env.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		N/A
 */
void bio_xsctxt_free(struct bio_xs_context *ctxt);

/**
 * NVMe poller to poll NVMe I/O completions.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \return		0: If no work was done
 *			1: If work was done
 *			-1: If thread has exited
 */
int bio_nvme_poll(struct bio_xs_context *ctxt);

/*
 * Create per VOS instance blob.
 *
 * \param[IN] uuid	Pool UUID
 * \param[IN] xs_ctxt	Per-xstream NVMe context
 * \param[IN] blob_sz	Size of the blob to be created
 *
 * \returns		Zero on success, negative value on error
 */
int bio_blob_create(uuid_t uuid, struct bio_xs_context *xs_ctxt,
		    uint64_t blob_sz);

/*
 * Delete per VOS instance blob.
 *
 * \param[IN] uuid	Pool UUID
 * \param[IN] xs_ctxt	Per-xstream NVMe context
 *
 * \returns		Zero on success, negative value on error
 */
int bio_blob_delete(uuid_t uuid, struct bio_xs_context *xs_ctxt);

/*
 * Open per VOS instance I/O context.
 *
 * \param[OUT] pctxt	I/O context to be returned
 * \param[IN] xs_ctxt	Per-xstream NVMe context
 * \param[IN] umem	umem instance
 * \param[IN] uuid	Pool UUID
 * \param[IN] skip_blob	Skip blob open since no NVMe partition
 *
 * \returns		Zero on success, negative value on error
 */
int bio_ioctxt_open(struct bio_io_context **pctxt,
		    struct bio_xs_context *xs_ctxt,
		    struct umem_instance *umem, uuid_t uuid, bool skip_blob);

/*
 * Finalize per VOS instance I/O context.
 *
 * \param[IN] ctxt	I/O context
 * \param[IN] skip_blob	Skip blob close since no NVMe partition
 *
 * \returns		Zero on success, negative value on error
 */
int bio_ioctxt_close(struct bio_io_context *ctxt, bool skip_blob);

/*
 * Unmap (TRIM) the extent being freed.
 *
 * \param[IN] ctxt	I/O context
 * \param[IN] off	Offset in bytes
 * \param[IN] len	Length in bytes
 *
 * \returns		Zero on success, negative value on error
 */
int bio_blob_unmap(struct bio_io_context *ctxt, uint64_t off, uint64_t len);

/**
 * Write to per VOS instance blob.
 *
 * \param[IN] ctxt	VOS instance I/O context
 * \param[IN] addr	SPDK blob addr info including byte offset
 * \param[IN] iov	IO vector containing buffer to be written
 *
 * \returns		Zero on success, negative value on error
 */
int bio_write(struct bio_io_context *ctxt, bio_addr_t addr, d_iov_t *iov);

/**
 * Read from per VOS instance blob.
 *
 * \param[IN] ctxt	VOS instance I/O context
 * \param[IN] addr	SPDK blob addr info including byte offset
 * \param[IN] iov	IO vector containing buffer from read
 *
 * \returns		Zero on success, negative value on error
 */
int bio_read(struct bio_io_context *ctxt, bio_addr_t addr, d_iov_t *iov);

/**
 * Write SGL to per VOS instance blob.
 *
 * \param[IN] ctxt	VOS instance I/O context
 * \param[IN] bsgl	SPDK blob addr SGL
 * \param[IN] sgl	Buffer SGL to be written
 *
 * \returns		Zero on success, negative value on error
 */
int bio_writev(struct bio_io_context *ioctxt, struct bio_sglist *bsgl,
	       d_sg_list_t *sgl);

/**
 * Read SGL from per VOS instance blob.
 *
 * \param[IN] ctxt	VOS instance I/O context
 * \param[IN] bsgl	SPDK blob addr SGL
 * \param[IN] sgl	Buffer SGL for read
 *
 * \returns		Zero on success, negative value on error
 */
int bio_readv(struct bio_io_context *ioctxt, struct bio_sglist *bsgl,
	      d_sg_list_t *sgl);

/*
 * Finish setting up blob header and write info to blob offset 0.
 *
 * \param[IN] ctxt	I/O context
 * \param[IN] hdr	VOS blob header struct
 *
 * \returns		Zero on success, negative value on error
 */
int bio_write_blob_hdr(struct bio_io_context *ctxt, struct bio_blob_hdr *hdr);

/* Note: Do NOT change the order of these types */
enum bio_iod_type {
	BIO_IOD_TYPE_UPDATE = 0,	/* For update request */
	BIO_IOD_TYPE_FETCH,		/* For fetch request */
	BIO_IOD_TYPE_GETBUF,		/* For get buf request */
	BIO_IOD_TYPE_MAX,
};

/**
 * Allocate & initialize an io descriptor
 *
 * \param ctxt       [IN]	I/O context
 * \param sgl_cnt    [IN]	SG list count
 * \param type       [IN]	IOD type
 *
 * \return			Opaque io descriptor or NULL on error
 */
struct bio_desc *bio_iod_alloc(struct bio_io_context *ctxt,
			       unsigned int sgl_cnt, unsigned int type);
/**
 * Free an io descriptor
 *
 * \param biod       [IN]	io descriptor to be freed
 *
 * \return			N/A
 */
void bio_iod_free(struct bio_desc *biod);

enum bio_chunk_type {
	BIO_CHK_TYPE_IO	= 0,	/* For IO request */
	BIO_CHK_TYPE_LOCAL,	/* For local DMA transfer */
	BIO_CHK_TYPE_REBUILD,	/* For rebuild pull */
	BIO_CHK_TYPE_MAX,
};

/**
 * Prepare all the SG lists of an io descriptor.
 *
 * For SCM IOV, it needs only to convert the PMDK pmemobj offset into direct
 * memory address; For NVMe IOV, it maps the SPDK blob page offset to an
 * internally maintained DMA buffer, it also needs fill the buffer for fetch
 * operation.
 *
 * \param biod       [IN]	io descriptor
 * \param type       [IN]	chunk type used by this iod
 * \param bulk_ctxt  [IN]	Bulk context for bulk operations
 * \param bulk_perm  [IN]	Bulk permission
 *
 * \return			Zero on success, negative value on error
 */
int bio_iod_prep(struct bio_desc *biod, unsigned int type, void *bulk_ctxt,
		 unsigned int bulk_perm);

/*
 * Post operation after the RDMA transfer or local copy done for the io
 * descriptor.
 *
 * For SCM IOV, it's a noop operation; For NVMe IOV, it releases the DMA buffer
 * held in bio_iod_prep(), it also needs to write back the data from DMA buffer
 * to the NVMe device for update operation.
 *
 * \param biod       [IN]	io descriptor
 *
 * \return			Zero on success, negative value on error
 */
int bio_iod_post(struct bio_desc *biod);

/*
 * Helper function to copy data between SG lists of io descriptor and user
 * specified DRAM SG lists.
 *
 * \param biod       [IN]	io descriptor
 * \param sgls       [IN]	DRAM SG lists
 * \param nr_sgl     [IN]	Number of SG lists
 *
 * \return			Zero on success, negative value on error
 */
int bio_iod_copy(struct bio_desc *biod, d_sg_list_t *sgls, unsigned int nr_sgl);

/*
 * Helper function to flush memory vectors in SG lists of io descriptor
 *
 * \param biod       [IN]	io descriptor
 *
 * \return			N/A
 */
void bio_iod_flush(struct bio_desc *biod);

/*
 * Helper function to get the specified SG list of an io descriptor
 *
 * \param biod       [IN]	io descriptor
 * \param idx        [IN]	Index of the SG list
 *
 * \return			SG list, or NULL on error
 */
struct bio_sglist *bio_iod_sgl(struct bio_desc *biod, unsigned int idx);

/*
 * Helper function to get the specified bulk for an io descriptor
 *
 * \param biod       [IN]	io descriptor
 * \param sgl_idx    [IN]	Index of the SG list
 * \param iov_idx    [IN]	IOV index within the SG list
 * \param bulk_off   [OUT]	Bulk offset
 *
 * \return			Cached bulk, or NULL if no cached bulk
 */
void *bio_iod_bulk(struct bio_desc *biod, int sgl_idx, int iov_idx,
		   unsigned int *bulk_off);

/*
 * Wrapper of ABT_thread_yield()
 */
static inline void
bio_yield(void)
{
#ifdef DAOS_PMEM_BUILD
	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
#endif
	ABT_thread_yield();
}

/*
 * Helper function to get the device health state for a given xstream.
 * Used for querying the BIO health information from the control plane command.
 *
 * \param dev_state	[OUT]	BIO device health state
 * \param xs		[IN]	xstream context
 *
 * \return			Zero on success, negative value on error
 */
int bio_get_dev_state(struct nvme_stats *dev_state,
		      struct bio_xs_context *xs);

/*
 * Helper function to get the internal blobstore state for a given xstream.
 * Used for daos_test validation in the daos_mgmt_get_bs_state() C API.
 *
 * \param dev_state	[OUT]	BIO blobstore state
 * \param xs		[IN]	xstream context
 *
 */
void bio_get_bs_state(int *blobstore_state, struct bio_xs_context *xs);


/*
 * Helper function to set the device health state to FAULTY, and trigger device
 * state transition.
 *
 * \param xs		[IN]	xstream context
 *
 * \return			Zero on success, negative value on error
 */
int bio_dev_set_faulty(struct bio_xs_context *xs);

/* Function to increment CSUM media error. */
void bio_log_csum_err(struct bio_xs_context *b, int tgt_id);

/* Too many blob IO queued, need to schedule a NVMe poll? */
bool bio_need_nvme_poll(struct bio_xs_context *xs);

/*
 * Replace a device.
 *
 * \param xs		[IN]	xstream context
 * \param old_dev_id	[IN]	UUID of device to be replaced
 * \param new_dev_id	[IN]	UUID of new device to replace with
 *
 * \return			Zero on success, negative value on error
 */
int bio_replace_dev(struct bio_xs_context *xs, uuid_t old_dev_id,
		    uuid_t new_dev_id);

/*
 * Set the LED on a VMD device to new state.
 *
 * \param xs            [IN]    xstream context
 * \param devid		[IN]	UUID of the VMD device
 * \param led_state	[IN]	State to set the LED to
 *				(ie identify, off, fault/on)
 * \param reset		[IN]	Reset flag indicates that the led_state
 * 				will be determined by the saved state in
 * 				bio_bdev (bb_led_state)
 *
 * \return                      Zero on success, negative value on error
 */
int bio_set_led_state(struct bio_xs_context *xs, uuid_t devid,
		      const char *led_state, bool reset);

/*
 * Allocate DMA buffer, the buffer could be from bulk cache if bulk context
 * if specified.
 *
 * \param ioctxt	[IN]	I/O context
 * \param len		[IN]	Requested buffer length
 * \param bulk_ctxt	[IN]	Bulk context
 * \param bulk_perm	[IN]	Bulk permission
 *
 * \return			Buffer descriptor on success, NULL on error
 */
struct bio_desc *bio_buf_alloc(struct bio_io_context *ioctxt,
			       unsigned int len, void *bulk_ctxt,
			       unsigned int bulk_perm);

/*
 * Free allocated DMA buffer.
 *
 * \param biod		[IN]	Buffer descriptor
 *
 * \return			N/A
 */
void bio_buf_free(struct bio_desc *biod);

/*
 * Get the bulk handle of DMA buffer.
 *
 * \param biod		[IN]	Buffer descriptor
 * \param bulk_off	[OUT]	Bulk offset
 *
 * \return			Bulk handle
 */
void *bio_buf_bulk(struct bio_desc *biod, unsigned int *bulk_off);

/*
 * Get the address of DMA buffer.
 *
 * \param biod		[IN]	Buffer descriptor
 *
 * \return			Buffer address
 */
void *bio_buf_addr(struct bio_desc *biod);

#endif /* __BIO_API_H__ */
