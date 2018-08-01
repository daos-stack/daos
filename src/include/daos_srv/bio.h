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
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/*
 * Blob I/O library provides functionality of blob I/O over SG list consists
 * of SCM or NVMe IOVs, PMDK & SPDK are used for SCM and NVMe I/O respectively.
 */

#ifndef __BIO_API_H__
#define __BIO_API_H__

#include <daos/mem.h>
#include <daos/common.h>

/* Address types for various medias */
enum {
	BIO_ADDR_SCM	= 0,
	BIO_ADDR_NVME,
};

typedef struct {
	/*
	 * Byte offset within PMDK pmemobj pool for SCM;
	 * Byte offset within SPDK blob for NVMe.
	 */
	uint64_t	ba_off;
	/* BIO_ADDR_SCM or BIO_ADDR_NVME */
	uint16_t	ba_type;
	/* Is the address a hole ? */
	uint16_t	ba_hole;
	uint32_t	ba_padding;
} bio_addr_t;

struct bio_iov {
	/*
	 * For SCM, it's direct memory address of 'ba_off';
	 * For NVMe, it's a DMA buffer allocated by SPDK malloc API.
	 */
	void		*bi_buf;
	/* Data length in bytes */
	size_t		 bi_data_len;
	bio_addr_t	 bi_addr;
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

static inline void
bio_addr_set(bio_addr_t *addr, uint16_t type, uint64_t off)
{
	addr->ba_type = type;
	addr->ba_off = off;
}

static inline bool
bio_addr_is_hole(bio_addr_t *addr)
{
	return addr->ba_hole != 0;
}

static inline void
bio_addr_set_hole(bio_addr_t *addr, uint16_t hole)
{
	addr->ba_hole = hole;
}

static inline uint64_t
bio_iov2off(struct bio_iov *biov)
{
	return biov->bi_addr.ba_off;
}

static inline int
bio_sgl_init(struct bio_sglist *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->bs_nr = nr;
	D_ALLOC(sgl->bs_iovs, nr * sizeof(*sgl->bs_iovs));
	return sgl->bs_iovs == NULL ? -DER_NOMEM : 0;
}

static inline void
bio_sgl_fini(struct bio_sglist *sgl)
{
	if (sgl->bs_iovs == NULL)
		return;

	D_FREE(sgl->bs_iovs);
	memset(sgl, 0, sizeof(*sgl));
}

/*
 * Convert bio_sglist into daos_sg_list_t, caller is responsible to
 * call daos_sgl_fini(sgl, false) to free iovs.
 */
static inline int
bio_sgl_convert(struct bio_sglist *bsgl, daos_sg_list_t *sgl)
{
	int i, rc;

	D_ASSERT(sgl != NULL);
	D_ASSERT(bsgl != NULL);

	rc = daos_sgl_init(sgl, bsgl->bs_nr_out);
	if (rc != 0)
		return -DER_NOMEM;

	sgl->sg_nr_out = bsgl->bs_nr_out;

	for (i = 0; i < sgl->sg_nr_out; i++) {
		struct bio_iov	*biov = &bsgl->bs_iovs[i];
		daos_iov_t	*iov = &sgl->sg_iovs[i];

		iov->iov_buf = biov->bi_buf;
		iov->iov_len = biov->bi_data_len;
		iov->iov_buf_len = biov->bi_data_len;
	}

	return 0;
}

/**
 * Global NVMe initialization.
 *
 * \param[IN] storage_path	daos storage directory path
 *
 * \return		Zero on success, negative value on error
 */
int bio_nvme_init(const char *storage_path);

/**
 * Global NVMe finilization.
 *
 * \return		N/A
 */
void bio_nvme_fini(void);

/*
 * Initialize SPDK env and per-xstream NVMe context.
 *
 * \param[OUT] pctxt	Per-xstream NVMe context to be returned
 * \param[IN] xs_id	xstream ID
 *
 * \returns		Zero on success, negative value on error
 */
int bio_xsctxt_alloc(struct bio_xs_context **pctxt, int xs_id);

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
 * \return		Executed message count
 */
size_t bio_nvme_poll(struct bio_xs_context *ctxt);

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
 *
 * \returns		Zero on success, negative value on error
 */
int bio_ioctxt_open(struct bio_io_context **pctxt,
		    struct bio_xs_context *xs_ctxt,
		    struct umem_instance *umem, uuid_t uuid);

/*
 * Finalize per VOS instance I/O context.
 *
 * \param[IN] ctxt	I/O context
 *
 * \returns		Zero on success, negative value on error
 */
int bio_ioctxt_close(struct bio_io_context *ctxt);

/**
 * Allocate & initialize an io descriptor
 *
 * \param ctxt       [IN]	I/O context
 * \param sgl_cnt    [IN]	SG list count
 * \param update     [IN]	update or fetch operation?
 *
 * \return			Opaque io descriptor or NULL on error
 */
struct bio_desc *bio_iod_alloc(struct bio_io_context *ctxt,
			       unsigned int sgl_cnt, bool update);
/**
 * Free an io descriptor
 *
 * \param biod       [IN]	io descriptor to be freed
 *
 * \return			N/A
 */
void bio_iod_free(struct bio_desc *biod);

/**
 * Prepare all the SG lists of an io descriptor.
 *
 * For SCM IOV, it needs only to convert the PMDK pmemobj offset into direct
 * memory address; For NVMe IOV, it maps the SPDK blob page offset to an
 * internally maintained DMA buffer, it also needs fill the buffer for fetch
 * operation.
 *
 * \param biod       [IN]	io descriptor
 *
 * \return			Zero on success, negative value on error
 */
int bio_iod_prep(struct bio_desc *biod);

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
 * Helper function to get the specified SG list of an io descriptor
 *
 * \param biod       [IN]	io descriptor
 * \param idx        [IN]	Index of the SG list
 *
 * \return			SG list, or NULL on error
 */
struct bio_sglist *bio_iod_sgl(struct bio_desc *biod, unsigned int idx);

#endif /* __BIO_API_H__ */
