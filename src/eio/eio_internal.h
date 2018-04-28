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

#ifndef __EIO_INTERNAL_H__
#define __EIO_INTERNAL_H__

#include <daos_srv/daos_server.h>
#include <daos_srv/eio.h>

#define	EIO_DMA_PAGE_SZ		(4UL << 10)	/* 4K */

/* DMA buffer is managed in chunks */
struct eio_dma_chunk {
	/* Link to edb_idle_list or edb_used_list */
	d_list_t	 edc_link;
	/* Base pointer of the chunk address */
	void		*edc_ptr;
	/* Page offset (4K page)  to unused fraction */
	unsigned int	 edc_pg_idx;
	/* Being used by how many I/O descriptors */
	unsigned int	 edc_ref;
};

/*
 * Per-xstream DMA buffer, used as SPDK dma I/O buffer or as temporary
 * RDMA buffer for ZC fetch/update over NVMe devices.
 */
struct eio_dma_buffer {
	d_list_t		 edb_idle_list;
	d_list_t		 edb_used_list;
	struct eio_dma_chunk	*edb_cur_chk;
	unsigned int		 edb_tot_cnt;
};

/* Per-xstream NVMe context */
struct eio_xs_context {
	struct spdk_ring	*exc_msg_ring;
	struct spdk_thread	*exc_thread;
	struct spdk_blob_store	*exc_blobstore;
	struct spdk_io_channel	*exc_io_channel;
	d_list_t		 exc_pollers;
	struct eio_dma_buffer	*exc_dma_buf;
};

/* Per VOS instance I/O context */
struct eio_io_context {
	struct umem_instance	*eic_umem;
	uint64_t		 eic_pmempool_uuid;
	struct spdk_blob	*eic_blob;
	struct eio_xs_context	*eic_xs_ctxt;
};

/* A contiguous DMA buffer region reserved by certain io descriptor */
struct eio_rsrvd_region {
	/* The DMA chunk where the region is located */
	struct eio_dma_chunk	*err_chk;
	/* Start page idx within the DMA chunk */
	unsigned int		 err_pg_idx;
	/* Offset within the SPDK blob in bytes */
	uint64_t		 err_off;
	/* End (not included) in bytes */
	uint64_t		 err_end;
};

/* Reserved DMA buffer for certain io descriptor */
struct eio_rsrvd_dma {
	/* DMA regions reserved by the io descriptor */
	struct eio_rsrvd_region	 *erd_regions;
	/* Capacity of the region array */
	unsigned int		  erd_rg_max;
	/* Total number of reserved regions */
	unsigned int		  erd_rg_cnt;
	/* Pointer array for all referenced DMA chunks */
	struct eio_dma_chunk	**erd_dma_chks;
	/* Capacity of the pointer array */
	unsigned int		  erd_chk_max;
	/* Total number of chunks being referenced */
	unsigned int		  erd_chk_cnt;
};

/* I/O descriptor */
struct eio_desc {
	struct eio_io_context	*ed_ctxt;
	/* SG lists involved in this io descriptor */
	unsigned int		 ed_sgl_cnt;
	struct eio_sglist	*ed_sgls;
	/* DMA buffers reserved by this io descriptor */
	struct eio_rsrvd_dma	 ed_rsrvd;
	/*
	 * We currently always issue SPDK I/O from the channel
	 * created within same thread. The mutex is just in case
	 * of support multiple I/O channels in the future.
	 */
	ABT_mutex		 ed_mutex;
	ABT_cond		 ed_dma_done;
	/* Inflight SPDK DMA transfers */
	unsigned int		 ed_inflights;
	int			 ed_result;
	/* Flags */
	unsigned int		 ed_buffer_prep:1,
				 ed_update:1,
				 ed_dma_issued:1;
};

/* eio_xstream.c */
extern unsigned int	eio_chk_sz;
extern unsigned int	eio_chk_cnt_max;

/* eio_buffer.c */
void dma_buffer_destroy(struct eio_dma_buffer *buf);
struct eio_dma_buffer *dma_buffer_create(unsigned int init_cnt);

#endif /* __EIO_INTERNAL_H__ */
