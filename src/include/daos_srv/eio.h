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

/*
 * Extent I/O library provides NVMe or SCM extent I/O functionality, PMDK and
 * SPDK are used for SCM and NVMe I/O respectively.
 */

#ifndef __EIO_API_H__
#define __EIO_API_H__

#include <daos/mem.h>
#include <daos/common.h>

/* Address types for various medias */
enum {
	EIO_ADDR_SCM	= 0,
	EIO_ADDR_NVME,
};

struct eio_addr {
	/*
	 * Byte offset within PMDK pmemobj pool for SCM;
	 * Byte offset within SPDK blob for NVMe.
	 */
	uint64_t	ea_off;
	/* EIO_ADDR_SCM or EIO_ADDR_NVME */
	uint16_t	ea_type;
	/* Is the address a hole ? */
	uint16_t	ea_hole;
	uint32_t	ea_padding;
};

struct eio_iov {
	/*
	 * For SCM, it's direct memory address of 'ea_off';
	 * For NVMe, it's a DMA buffer allocated by SPDK malloc API.
	 */
	void		*ei_buf;
	/* Data length in bytes */
	size_t		 ei_data_len;
	struct eio_addr	 ei_addr;
};

struct eio_sglist {
	struct eio_iov	*es_iovs;
	unsigned int	 es_nr;
	unsigned int	 es_nr_out;
};

/* Opaque I/O descriptor */
struct eio_desc;
/* Opaque I/O context */
struct eio_io_context;
/* Opaque per-xstream context */
struct eio_xs_context;

static inline bool
eio_addr_is_hole(struct eio_addr *addr)
{
	return addr->ea_hole != 0;
}

static inline void
eio_addr_set_hole(struct eio_addr *addr, uint16_t hole)
{
	addr->ea_hole = hole;
}

static inline int
eio_sgl_init(struct eio_sglist *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->es_nr = nr;
	D_ALLOC(sgl->es_iovs, nr * sizeof(*sgl->es_iovs));
	return sgl->es_iovs == NULL ? -DER_NOMEM : 0;
}

static inline void
eio_sgl_fini(struct eio_sglist *sgl)
{
	if (sgl->es_iovs == NULL)
		return;

	D_FREE(sgl->es_iovs);
	memset(sgl, 0, sizeof(*sgl));
}

/**
 * Global NVMe initialization.
 *
 * \return		Zero on success, negative value on error
 */
int eio_nvme_init(void);

/**
 * Global NVMe finilization.
 *
 * \return		N/A
 */
void eio_nvme_fini(void);

/*
 * Initialize SPDK env and per-xstream NVMe context.
 *
 * \param[OUT] pctxt	Per-xstream NVMe context to be returned
 * \param[IN] xs_id	xstream ID
 *
 * \returns		Zero on success, negative value on error
 */
int eio_xsctxt_alloc(struct eio_xs_context **pctxt, int xs_id);

/*
 * Finalize per-xstream NVMe context and SPDK env.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \returns		N/A
 */
void eio_xsctxt_free(struct eio_xs_context *ctxt);

/**
 * NVMe poller to poll NVMe I/O completions.
 *
 * \param[IN] ctxt	Per-xstream NVMe context
 *
 * \return		Executed message count
 */
size_t eio_nvme_poll(struct eio_xs_context *ctxt);

/*
 * Create per VOS instance I/O context.
 *
 * \param[IN] uuid	Pool UUID
 * \param[IN] xs_ctxt	Per-xstream NVMe context
 *
 * \returns		Zero on success, negative value on error
 */
int eio_ioctxt_create(uuid_t uuid, struct eio_xs_context *xs_ctxt);
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
int eio_ioctxt_open(struct eio_io_context **pctxt,
		    struct eio_xs_context *xs_ctxt,
		    struct umem_instance *umem, uuid_t uuid);

/*
 * Finalize per VOS instance I/O context.
 *
 * \param[IN] ctxt	I/O context
 *
 * \returns		N/A
 */
void eio_ioctxt_close(struct eio_io_context *ctxt);

/**
 * Allocate & initialize an io descriptor
 *
 * \param ctxt       [IN]	I/O context
 * \param sgl_cnt    [IN]	SG list count
 * \param update     [IN]	update or fetch operation?
 *
 * \return			Opaque io descriptor or NULL on error
 */
struct eio_desc *eio_iod_alloc(struct eio_io_context *ctxt,
			       unsigned int sgl_cnt, bool update);
/**
 * Free an io descriptor
 *
 * \param eiod       [IN]	io descriptor to be freed
 *
 * \return			N/A
 */
void eio_iod_free(struct eio_desc *eiod);

/**
 * Prepare all the SG lists of an io descriptor.
 *
 * For SCM IOV, it needs only to convert the PMDK pmemobj offset into direct
 * memory address; For NVMe IOV, it maps the SPDK blob page offset to an
 * internally maintained DMA buffer, it also needs fill the buffer for fetch
 * operation.
 *
 * \param eiod       [IN]	io descriptor
 *
 * \return			Zero on success, negative value on error
 */
int eio_iod_prep(struct eio_desc *eiod);

/*
 * Post operation after the RDMA transfer or local copy done for the io
 * descriptor.
 *
 * For SCM IOV, it's a noop operation; For NVMe IOV, it releases the DMA buffer
 * held in eio_iod_prep(), it also needs to write back the data from DMA buffer
 * to the NVMe device for update operation.
 *
 * \param eiod       [IN]	io descriptor
 *
 * \return			Zero on success, negative value on error
 */
int eio_iod_post(struct eio_desc *eiod);

/*
 * Helper function to copy data between SG lists of io descriptor and user
 * specified DRAM SG lists.
 *
 * \param eiod       [IN]	io descriptor
 * \param sgls       [IN]	DRAM SG lists
 * \param nr_sgl     [IN]	Number of SG lists
 *
 * \return			Zero on success, negative value on error
 */
int eio_iod_copy(struct eio_desc *eiod, d_sg_list_t *sgls, unsigned int nr_sgl);

/*
 * Helper function to get the specified SG list of an io descriptor
 *
 * \param eiod       [IN]	io descriptor
 * \param idx        [IN]	Index of the SG list
 *
 * \return			SG list, or NULL on error
 */
struct eio_sglist *eio_iod_sgl(struct eio_desc *eiod, unsigned int idx);

#endif /* __EIO_API_H__ */
