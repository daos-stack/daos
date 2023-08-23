/**
 * (C) Copyright 2018-2023 Intel Corporation.
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
#include <daos_srv/control.h>
#include <daos_srv/smd.h>
#include <abt.h>

#define BIO_ADDR_IS_HOLE(addr) ((addr)->ba_flags & BIO_FLAG_HOLE)
#define BIO_ADDR_SET_HOLE(addr) ((addr)->ba_flags |= BIO_FLAG_HOLE)
#define BIO_ADDR_CLEAR_HOLE(addr) ((addr)->ba_flags &= ~(BIO_FLAG_HOLE))
#define BIO_ADDR_IS_DEDUP(addr) ((addr)->ba_flags & BIO_FLAG_DEDUP)
#define BIO_ADDR_SET_DEDUP(addr) ((addr)->ba_flags |= BIO_FLAG_DEDUP)
#define BIO_ADDR_CLEAR_DEDUP(addr) ((addr)->ba_flags &= ~(BIO_FLAG_DEDUP))
#define BIO_ADDR_IS_DEDUP_BUF(addr) ((addr)->ba_flags == BIO_FLAG_DEDUP_BUF)
#define BIO_ADDR_SET_DEDUP_BUF(addr) ((addr)->ba_flags |= BIO_FLAG_DEDUP_BUF)
#define BIO_ADDR_SET_NOT_DEDUP_BUF(addr)	\
			((addr)->ba_flags &= ~(BIO_FLAG_DEDUP_BUF))
#define BIO_ADDR_IS_CORRUPTED(addr) ((addr)->ba_flags & BIO_FLAG_CORRUPTED)
#define BIO_ADDR_SET_CORRUPTED(addr) ((addr)->ba_flags |= BIO_FLAG_CORRUPTED)

/* Can support up to 16 flags for a BIO address */
enum BIO_FLAG {
	/* The address is a hole */
	BIO_FLAG_HOLE = (1 << 0),
	/* The address is a deduped extent */
	BIO_FLAG_DEDUP = (1 << 1),
	/* The address is a buffer for dedup verify */
	BIO_FLAG_DEDUP_BUF = (1 << 2),
	BIO_FLAG_CORRUPTED = (1 << 3),
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

/** Max number of vos targets per engine */
#define			 BIO_MAX_VOS_TGT_CNT	96
/* System xstream target ID */
#define			 BIO_SYS_TGT_ID		1024
/* for standalone VOS */
#define			 BIO_STANDALONE_TGT_ID	-1

/* Opaque I/O descriptor */
struct bio_desc;
/* Opaque I/O context */
struct bio_io_context;
/* Opaque per-xstream context */
struct bio_xs_context;
/* Opaque BIO copy descriptor */
struct bio_copy_desc;

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
		BIO_ADDR_CLEAR_HOLE(addr);
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
	if (biov->bi_buf == NULL)
		return NULL;
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
	uint32_t		bdi_dev_roles;	/* reserved */
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
 * \param[IN] numa_node		NUMA node that engine is assigned to
 * \param[IN] mem_size		SPDK memory alloc size when using primary mode
 * \param[IN] hugepage_size	Configured hugepage size on system
 * \paran[IN] tgt_nr		Number of targets
 * \param[IN] bypass		Set to bypass health data collection
 *
 * \return		Zero on success, negative value on error
 */
int bio_nvme_init(const char *nvme_conf, int numa_node, unsigned int mem_size,
		  unsigned int hugepage_size, unsigned int tgt_nr, bool bypass);

/**
 * Global NVMe finilization.
 *
 * \return		N/A
 */
void bio_nvme_fini(void);

/**
 * Check if the specified type of NVMe device is configured, when SMD_DEV_TYPE_MAX
 * is specified, return true if any type of device is configured.
 */
bool bio_nvme_configured(enum smd_dev_type type);

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
 * \param[OUT] pctxt		Per-xstream NVMe context to be returned
 * \param[IN] tgt_id		Target ID (mapped to a VOS xstream)
 * \param[IN] self_polling	self polling enabled or not
 *
 * \returns		Zero on success, negative value on error
 */
int bio_xsctxt_alloc(struct bio_xs_context **pctxt, int tgt_id, bool self_polling);

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
 * Open per VOS instance data I/O blob context.
 *
 * \param[OUT] pctxt	I/O context to be returned
 * \param[IN] xs_ctxt	Per-xstream NVMe context
 * \param[IN] uuid	Pool UUID
 * \param[IN] dummy	Create a dummy I/O context
 *
 * \returns		Zero on success, negative value on error
 */
int bio_ioctxt_open(struct bio_io_context **pctxt, struct bio_xs_context *xs_ctxt,
		    uuid_t uuid, bool dummy);

/*
 * Finalize per VOS instance I/O context.
 *
 * \param[IN] ctxt	I/O context
 *
 * \returns		Zero on success, negative value on error
 */
int bio_ioctxt_close(struct bio_io_context *ctxt);

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

/*
 * Unmap (TRIM) a SGL consists of freed extents.
 *
 * \param[IN] ctxt	I/O context
 * \param[IN] unmap_sgl	The SGL to be unmapped (offset & length are in blocks)
 * \param[IN] blk_sz	Block size
 *
 * \returns		Zero on success, negative value on error
 */
int bio_blob_unmap_sgl(struct bio_io_context *ctxt, d_sg_list_t *unmap_sgl, uint32_t blk_sz);

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
 * \param umem       [IN]	umem instance
 * \param sgl_cnt    [IN]	SG list count
 * \param type       [IN]	IOD type
 *
 * \return			Opaque io descriptor or NULL on error
 */
struct bio_desc *bio_iod_alloc(struct bio_io_context *ctxt, struct umem_instance *umem,
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
 * For direct accessed SCM IOV, it needs only to convert the PMDK pmemobj
 * offset into direct memory address; For NVMe IOV (or SCM IOV being accessed
 * through DMA buffer, it maps the SPDK blob page offset (or SCM address) to
 * an internally maintained DMA buffer, it also needs fill the buffer for fetch
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

/**
 * Non-blocking version, instead of "wait & retry" internally when DMA buffer is
 * under pressure, it'll immediately return -DER_AGAIN to the caller.
 *
 * BIO_IOD_TYPE_FETCH is not supported.
 */
int bio_iod_try_prep(struct bio_desc *biod, unsigned int type, void *bulk_ctxt,
		     unsigned int bulk_perm);

/*
 * Post operation after the RDMA transfer or local copy done for the io
 * descriptor.
 *
 * For direct accessed SCM IOV, it's a noop operation; For NVMe IOV (or SCM IOV
 * being accessed through DMA buffer), it releases the DMA buffer held in
 * bio_iod_prep(), it also needs to write back the data from DMA buffer to the
 * NVMe device (or SCM) for update operation.
 *
 * \param biod       [IN]	io descriptor
 * \param err        [IN]	Error code of prior data transfer operation
 *
 * \return			Zero on success, negative value on error
 */
int bio_iod_post(struct bio_desc *biod, int err);

/* Asynchronous bio_iod_post(), don't wait NVMe I/O completion */
int bio_iod_post_async(struct bio_desc *biod, int err);

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
bio_yield(struct umem_instance *umm)
{
#ifdef DAOS_PMEM_BUILD
	D_ASSERT(umm == NULL || umem_tx_none(umm));
#endif
	ABT_thread_yield();
}

/*
 * Helper function to get the device health state for a given xstream.
 * Used for querying the BIO health information from the control plane command.
 *
 * \param dev_state	[OUT]	BIO device health state
 * \param dev_uuid	[IN]	uuid of device
 * \param st		[IN]	smd dev type
 * \param meta_size	[IN]	Metadata blob size
 * \param rdb_size	[IN]	RDB blob size
 *
 * \return			Zero on success, negative value on error
 */
int bio_get_dev_state(struct nvme_stats *dev_state, uuid_t dev_uuid,
		      struct bio_xs_context *xs, uint64_t meta_size,
		      uint64_t rdb_size);

/*
 * Helper function to get the internal blobstore state for a given xstream.
 * Used for daos_test validation in the daos_mgmt_get_bs_state() C API.
 *
 * \param dev_state	[OUT]	BIO blobstore state
 * \param dev_id	[IN]	UUID of device
 * \param xs		[IN]	xstream context
 *
 * \return			Zero on success, negative value on error
 */
int bio_get_bs_state(int *blobstore_state, uuid_t dev_uuid, struct bio_xs_context *xs);


/*
 * Helper function to set the device health state to FAULTY, and trigger device
 * state transition.
 *
 * \param xs		[IN]	xstream context
 * \param dev_id	[IN]	uuid of device
 *
 * \return			Zero on success, negative value on error
 */
int bio_dev_set_faulty(struct bio_xs_context *xs, uuid_t dev_id);

/* Function to increment data CSUM media error. */
void bio_log_data_csum_err(struct bio_xs_context *xs);

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
 * Manage the LED on a VMD device.
 *
 * \param xs		[IN]		xstream context
 * \param tr_addr	[IN,OUT]	PCI address of the VMD backing SSD, update if empty
 * \param dev_uuid	[IN]		UUID of the VMD device
 * \param action	[IN]		Action to perform on the VMD device
 * \param state		[IN,OUT]	State to set the LED to (i.e. identify, off, fault/on)
 *					Update to reflect transition after action
 * \param duration	[IN]		Time period to blink (identify) the LED for
 *
 * \return				Zero on success, negative value on error
 */
int bio_led_manage(struct bio_xs_context *xs_ctxt, char *tr_addr, uuid_t dev_uuid,
		   unsigned int action, unsigned int *state, uint64_t duration);

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

/*
 * Prepare source and target bio SGLs for direct data copy between these two
 * SGLs. bio_copy_post() must be called after a success bio_copy_prep() call.
 *
 * \param ioctxt	[IN]	BIO io context
 * \param umem		[IN]	umem instance
 * \param bsgl_src	[IN]	Source BIO SGL
 * \param bsgl_dst	[IN]	Target BIO SGL
 * \param desc		[OUT]	Returned BIO copy descriptor
 *
 * \return			Zero on success, negative value on error
 */
int bio_copy_prep(struct bio_io_context *ioctxt, struct umem_instance *umem,
		  struct bio_sglist *bsgl_src, struct bio_sglist *bsgl_dst,
		  struct bio_copy_desc **desc);

struct bio_csum_desc {
	uint8_t		*bmd_csum_buf;
	uint32_t	 bmd_csum_buf_len;
	uint32_t	 bmd_chunk_sz;
	uint16_t	 bmd_csum_len;
	uint16_t	 bmd_csum_type;
};

/*
 * Copy data between source and target bio SGLs prepared in @copy_desc
 *
 * \param copy_desc	[IN]	Copy descriptor created by bio_copy_prep()
 * \param copy_size	[IN]	Specified copy size, the size must be aligned
 *				with source IOVs. 0 means copy all source IOVs
 * \param csum_desc	[IN]	Checksum descriptor for csum generation
 *
 * \return			0 on success, negative value on error
 */
int bio_copy_run(struct bio_copy_desc *copy_desc, unsigned int copy_size,
		 struct bio_csum_desc *csum_desc);

/*
 * Release resource held by bio_copy_prep(), write data back to NVMe if the
 * copy target is located on NVMe.
 *
 * \param copy_desc	[IN]	Copy descriptor created by bio_copy_prep()
 * \param err		[IN]	Error code of prior copy operation
 *
 * \return			0 on success, negative value on error
 */
int bio_copy_post(struct bio_copy_desc *copy_desc, int err);

/*
 * Get the prepared source or target BIO SGL from a copy descriptor.
 *
 * \param copy_desc	[IN]	Copy descriptor created by bio_copy_prep()
 * \param src		[IN]	Return Source or target BIO SGL?
 *
 * \return			Source/Target BIO SGL
 */
struct bio_sglist *bio_copy_get_sgl(struct bio_copy_desc *copy_desc, bool src);

/*
 * Copy data from source BIO SGL to target BIO SGL.
 *
 * \param ioctxt	[IN]	BIO io context
 * \param umem		[IN]	umem instance
 * \param bsgl_src	[IN]	Source BIO SGL
 * \param bsgl_dst	[IN]	Target BIO SGL
 * \param copy_size	[IN]	Specified copy size, the size must be aligned
 *				with source IOVs. 0 means copy all source IOVs
 * \param csum_desc	[IN]	Checksum descriptor for csum generation
 *
 * \return			0 on success, negative value on error
 */
int bio_copy(struct bio_io_context *ioctxt, struct umem_instance *umem,
	     struct bio_sglist *bsgl_src, struct bio_sglist *bsgl_dst, unsigned int copy_size,
	     struct bio_csum_desc *csum_desc);

enum bio_mc_flags {
	BIO_MC_FL_RDB		= (1UL << 0),	/* for RDB */
};

/*
 * Create Meta/Data/WAL blobs, format Meta & WAL blob
 *
 * \param[in]	xs_ctxt		Per-xstream NVMe context
 * \param[in]	pool_id		Pool UUID
 * \param[in]	meta_sz		Meta blob size in bytes
 * \param[in]	wal_sz		WAL blob in bytes
 * \param[in]	data_sz		Data blob in bytes
 * \param[in]	flags		bio_mc_flags
 *
 * \return			Zero on success, negative value on error.
 */
int bio_mc_create(struct bio_xs_context *xs_ctxt, uuid_t pool_id, uint64_t meta_sz,
		  uint64_t wal_sz, uint64_t data_sz, enum bio_mc_flags flags);

/*
 * Destroy Meta/Data/WAL blobs
 *
 * \param[in]	xs_ctxt		Per-xstream NVMe context
 * \param[in]	pool_id		Pool UUID
 * \param[in]	flags		bio_mc_flags
 *
 * \return			Zero on success, negative value on error.
 */
int bio_mc_destroy(struct bio_xs_context *xs_ctxt, uuid_t pool_id, enum bio_mc_flags flags);

/* Opaque meta context */
struct bio_meta_context;

/*
 * Open Meta/Data/WAL blobs, load WAL header, alloc meta context
 *
 * \param[in]	xs_ctxt		Per-xstream NVMe context
 * \param[in]	pool_id		Pool UUID
 * \param[in]	flags		bio_mc_flags
 * \param[out]	mc		BIO meta context
 *
 * \return			Zero on success, negative value on error
 */
int bio_mc_open(struct bio_xs_context *xs_ctxt, uuid_t pool_id,
		enum bio_mc_flags flags, struct bio_meta_context **mc);

/*
 * Close Meta/Data/WAL blobs, free meta context
 *
 * \param[in]	mc		BIO meta context
 *
 * \return			N/A
 */
int bio_mc_close(struct bio_meta_context *mc);

/* Function to return io context for data/meta/wal blob */
struct bio_io_context *bio_mc2ioc(struct bio_meta_context *mc, enum smd_dev_type type);

/*
 * Reserve WAL log space for current transaction
 *
 * \param[in]	mc		BIO meta context
 * \param[out]	tx_id		Reserved transaction ID
 *
 * \return			Zero on success, negative value on error
 */
int bio_wal_reserve(struct bio_meta_context *mc, uint64_t *tx_id);

/*
 * Submit WAL I/O and wait for completion
 *
 * \param[in]	mc		BIO meta context
 * \param[in]	tx		umem_tx pointer
 * \param[in]	biod_data	BIO descriptor for data update (optional)
 *
 * \return			Zero on success, negative value on error
 */
int bio_wal_commit(struct bio_meta_context *mc, struct umem_wal_tx *tx, struct bio_desc *biod_data);

/*
 * Compare two WAL transaction IDs from same WAL instance
 *
 * \param[in]	mc		BIO meta context
 * \param[in]	id1		Transaction ID1
 * \param[in]	id2		Transaction ID2
 *
 * \return			0	: ID1 == ID2
 *				-1	: ID1 < ID2
 *				+1	: ID1 > ID2
 */
int bio_wal_id_cmp(struct bio_meta_context *mc, uint64_t id1, uint64_t id2);

/* WAL replay stats */
struct bio_wal_rp_stats {
	uint64_t	wrs_tm;		/* rehydration time */
	uint64_t	wrs_sz;		/* bytes replayed */
	uint64_t	wrs_entries;	/* replayed entries count */
	uint64_t	wrs_tx_cnt;	/* total transactions */
};

/*
 * Replay committed transactions in the WAL on post-crash recovery
 *
 * \param[in]	mc		BIO meta context
 * \param[in]	replay_cb	Replay callback for individual action
 * \param[in]	arg		The callback function's private data
 *
 * \return			Zero on success, negative value on error
 */
int bio_wal_replay(struct bio_meta_context *mc, struct bio_wal_rp_stats *stats,
		   int (*replay_cb)(uint64_t tx_id, struct umem_action *act, void *data),
		   void *arg);

/*
 * Flush back WAL header
 *
 * \param[in]	mc		BIO meta context
 *
 * \return			Zero on success, negative value on error
 */
int bio_wal_flush_header(struct bio_meta_context *mc);

/*
 * After checkpointing, set highest checkpointed transaction ID, reclaim WAL space
 *
 * \param[in]	mc		BIO meta context
 * \param[in]	tx_id		The highest checkpointed transaction ID
 * \param[out]	purge_size	Purged WAL blocks
 *
 * \return			Zero on success, negative value on error
 */
int bio_wal_checkpoint(struct bio_meta_context *mc, uint64_t tx_id, uint64_t *purge_size);

/*
 * Query meta capacity & meta block size & meta blob header blocks.
 */
void bio_meta_get_attr(struct bio_meta_context *mc, uint64_t *capacity, uint32_t *blk_sz,
		       uint32_t *hdr_blks);

struct bio_wal_info {
	uint32_t	wi_tot_blks;	/* Total blocks */
	uint32_t	wi_used_blks;	/* Used blocks */
	uint64_t	wi_ckp_id;	/* Last check-pointed ID */
	uint64_t	wi_commit_id;	/* Last committed ID */
	uint64_t	wi_unused_id;	/* Next unused ID */
};

/*
 * Qeury WAL total blocks & used blocks.
 */
void bio_wal_query(struct bio_meta_context *mc, struct bio_wal_info *info);

/*
 * Check if the meta blob is empty, paired with bio_meta_clear_empty() for avoid
 * loading a newly created meta blob.
 */
bool bio_meta_is_empty(struct bio_meta_context *mc);

/*
 * Mark the meta blob as non-empty.
 */
int bio_meta_clear_empty(struct bio_meta_context *mc);

#endif /* __BIO_API_H__ */
