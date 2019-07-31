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
#include <spdk/bdev.h>

#define BIO_DMA_PAGE_SHIFT	12	/* 4K */
#define BIO_DMA_PAGE_SZ		(1UL << BIO_DMA_PAGE_SHIFT)
#define BIO_XS_CNT_MAX		48	/* Max VOS xstreams per blobstore */

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

enum bio_bs_state {
	/* Healthy and fully functional */
	BIO_BS_STATE_NORMAL	= 0,
	/* Being detected as faulty */
	BIO_BS_STATE_FAULTY,
	/* Affected targets are marked as DOWN, safe to tear down blobstore */
	BIO_BS_STATE_TEARDOWN,
	/* Blobstore is torn down */
	BIO_BS_STATE_OUT,
	/* New device hotplugged, start to initialize blobstore & blobs */
	BIO_BS_STATE_REPLACED,
	/* Blobstore & blobs initialized, start to reint affected targets */
	BIO_BS_STATE_REINT
};

/*
 * SPDK device health monitoring.
 */
struct bio_dev_health {
	struct bio_dev_state	 bdh_health_state;
	/* writable open descriptor for health info polling */
	struct spdk_bdev_desc	*bdh_desc;
	struct spdk_io_channel	*bdh_io_channel;
	void			*bdh_health_buf; /* health info logs */
	void			*bdh_ctrlr_buf; /* controller data */
	void			*bdh_error_buf; /* device error logs */
	uint64_t		 bdh_stat_age;
	unsigned int		 bdh_inflights;
};

/*
 * SPDK blobstore isn't thread safe and there can be only one SPDK
 * blobstore for certain NVMe device.
 */
struct bio_blobstore {
	ABT_mutex		 bb_mutex;
	ABT_cond		 bb_barrier;
	struct spdk_blob_store	*bb_bs;
	/*
	 * The xstream resposible for blobstore load/unload, monitor
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
	d_list_t		 bxc_io_ctxts;
	struct spdk_bdev_desc	*bxc_desc; /* for io stat only, read-only */
	uint64_t		 bxc_io_stat_age;
};

/* Per VOS instance I/O context */
struct bio_io_context {
	d_list_t		 bic_link; /* link to bxc_io_ctxts */
	struct umem_instance	*bic_umem;
	uint64_t		 bic_pmempool_uuid;
	struct spdk_blob	*bic_blob;
	struct bio_xs_context	*bic_xs_ctxt;
	uint32_t		 bic_inflight_dmas;
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

enum {
	BDEV_CLASS_NVME = 0,
	BDEV_CLASS_MALLOC,
	BDEV_CLASS_AIO,
	BDEV_CLASS_UNKNOWN
};

/* bio_xstream.c */
extern unsigned int	bio_chk_sz;
extern unsigned int	bio_chk_cnt_max;
extern uint64_t		io_stat_period;
void xs_poll_completion(struct bio_xs_context *ctxt, unsigned int *inflights);
int get_bdev_type(struct spdk_bdev *bdev);

/* bio_buffer.c */
void dma_buffer_destroy(struct bio_dma_buffer *buf);
struct bio_dma_buffer *dma_buffer_create(unsigned int init_cnt);
void bio_memcpy(struct bio_desc *biod, uint16_t media, void *media_addr,
		void *addr, ssize_t n);

/* bio_monitor.c */
int bio_init_health_monitoring(struct bio_blobstore *bb,
			       struct spdk_bdev *bdev);
void bio_fini_health_monitoring(struct bio_blobstore *bb);
void bio_xs_io_stat(struct bio_xs_context *ctxt, uint64_t now);
void bio_bs_monitor(struct bio_xs_context *ctxt, uint64_t now);

/* bio_context.c */
int bio_blob_close(struct bio_io_context *ctxt, bool async);

/* bio_recovery.c */
extern struct bio_reaction_ops *ract_ops;
int bio_bs_state_transit(struct bio_blobstore *bbs);

#endif /* __BIO_INTERNAL_H__ */
