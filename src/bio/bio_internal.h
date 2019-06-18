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

#ifndef __BIO_INTERNAL_H__
#define __BIO_INTERNAL_H__

#include <daos_srv/daos_server.h>
#include <daos_srv/bio.h>

#define BIO_DMA_PAGE_SHIFT	12		/* 4K */
#define	BIO_DMA_PAGE_SZ		(1UL << BIO_DMA_PAGE_SHIFT)

/* DMA buffer is managed in chunks */
struct bio_dma_chunk {
	/* Link to edb_idle_list or edb_used_list */
	d_list_t	 bdc_link;
	/* Base pointer of the chunk address */
	void		*bdc_ptr;
	/* Page offset (4K page) to unused fraction */
	unsigned int	 bdc_pg_idx;
	/* Being used by how many I/O descriptors */
	unsigned int	 bdc_ref;
};

/*
 * Per-xstream DMA buffer, used as SPDK dma I/O buffer or as temporary
 * RDMA buffer for ZC fetch/update over NVMe devices.
 */
struct bio_dma_buffer {
	d_list_t		 bdb_idle_list;
	d_list_t		 bdb_used_list;
	struct bio_dma_chunk	*bdb_cur_chk;
	unsigned int		 bdb_tot_cnt;
	unsigned int		 bdb_active_iods;
	ABT_cond		 bdb_wait_iods;
	ABT_mutex		 bdb_mutex;
};

/*
 * SPDK blobstore isn't thread safe and there can be only one SPDK
 * blobstore for certain NVMe device.
 */
struct bio_blobstore {
	ABT_mutex		 bb_mutex;
	struct spdk_blob_store	*bb_bs;
	struct bio_xs_context	*bb_ctxt;
	int			 bb_ref;
};

/* Per-xstream NVMe context */
struct bio_xs_context {
	int			 bxc_xs_id;
	struct spdk_ring	*bxc_msg_ring;
	struct spdk_thread	*bxc_thread;
	struct bio_blobstore	*bxc_blobstore;
	struct spdk_io_channel	*bxc_io_channel;
	d_list_t		 bxc_pollers;
	struct bio_dma_buffer	*bxc_dma_buf;
	struct spdk_bdev_desc	*bxc_desc; /* for io stat only */
	uint64_t		 bxc_stat_age;
};

/* Per VOS instance I/O context */
struct bio_io_context {
	struct umem_instance	*bic_umem;
	uint64_t		 bic_pmempool_uuid;
	struct spdk_blob	*bic_blob;
	struct bio_xs_context	*bic_xs_ctxt;
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
	/* SG lists involved in this io descriptor */
	unsigned int		 bd_sgl_cnt;
	struct bio_sglist	*bd_sgls;
	/* DMA buffers reserved by this io descriptor */
	struct bio_rsrvd_dma	 bd_rsrvd;
	/*
	 * SPDK blob io completion could run on different xstream
	 * when the NVMe device is shared by multiple xstreams.
	 */
	ABT_mutex		 bd_mutex;
	ABT_cond		 bd_dma_done;
	/* Inflight SPDK DMA transfers */
	unsigned int		 bd_inflights;
	int			 bd_result;
	/* Flags */
	unsigned int		 bd_buffer_prep:1,
				 bd_update:1,
				 bd_dma_issued:1,
				 bd_retry:1;
};

/* bio_xstream.c */
extern unsigned int	bio_chk_sz;
extern unsigned int	bio_chk_cnt_max;
void xs_poll_completion(struct bio_xs_context *ctxt, unsigned int *inflights);

/* bio_buffer.c */
void dma_buffer_destroy(struct bio_dma_buffer *buf);
struct bio_dma_buffer *dma_buffer_create(unsigned int init_cnt);
void bio_memcpy(struct bio_desc *biod, uint16_t media, void *media_addr,
		void *addr, ssize_t n);

#endif /* __BIO_INTERNAL_H__ */
