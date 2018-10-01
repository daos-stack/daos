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
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/blob.h>
#include "bio_internal.h"
#include "smd/smd_internal.h"

#define BIO_BLOB_HDR_MAGIC	(0xb0b51ed5)

struct blob_cp_arg {
	spdk_blob_id		 bca_id;
	struct spdk_blob	*bca_blob;
	/*
	 * Completion could run on different xstream when NVMe
	 * device is shared by multiple xstreams.
	 */
	ABT_mutex		 bca_mutex;
	ABT_cond		 bca_done;
	unsigned int		 bca_inflights;
	int			 bca_rc;
};

static struct blob_cp_arg *
alloc_blob_cp_arg(void)
{
	struct blob_cp_arg	*ba;
	int			 rc;

	D_ALLOC_PTR(ba);
	if (ba == NULL)
		return NULL;

	rc = ABT_mutex_create(&ba->bca_mutex);
	if (rc != ABT_SUCCESS) {
		D_FREE_PTR(ba);
		return NULL;
	}

	rc = ABT_cond_create(&ba->bca_done);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&ba->bca_mutex);
		D_FREE_PTR(ba);
		return NULL;
	}

	return ba;
}

static void
free_blob_cp_arg(struct blob_cp_arg *ba)
{
	ABT_cond_free(&ba->bca_done);
	ABT_mutex_free(&ba->bca_mutex);
	D_FREE_PTR(ba);
}

static void
blob_common_cb(struct blob_cp_arg *ba, int rc)
{
	ABT_mutex_lock(ba->bca_mutex);

	ba->bca_rc = rc;

	D_ASSERT(ba->bca_inflights == 1);
	ba->bca_inflights--;
	ABT_cond_broadcast(ba->bca_done);

	ABT_mutex_unlock(ba->bca_mutex);
}

static void
blob_create_cb(void *arg, spdk_blob_id blob_id, int rc)
{
	struct blob_cp_arg	*ba = arg;

	ba->bca_id = blob_id;
	blob_common_cb(ba, rc);
}

static void
blob_open_cb(void *arg, struct spdk_blob *blob, int rc)
{
	struct blob_cp_arg	*ba = arg;

	ba->bca_blob = blob;
	blob_common_cb(ba, rc);
}

static void
blob_cb(void *arg, int rc)
{
	struct blob_cp_arg	*ba = arg;

	blob_common_cb(ba, rc);
}

static void
blob_wait_completion(struct bio_xs_context *xs_ctxt, struct blob_cp_arg *ba)
{
	D_ASSERT(xs_ctxt != NULL);
	if (xs_ctxt->bxc_xs_id == -1) {
		D_DEBUG(DB_IO, "Self poll xs_ctxt:%p\n", xs_ctxt);
		xs_poll_completion(xs_ctxt, &ba->bca_inflights);
	} else {
		ABT_mutex_lock(ba->bca_mutex);
		if (ba->bca_inflights)
			ABT_cond_wait(ba->bca_done, ba->bca_mutex);
		ABT_mutex_unlock(ba->bca_mutex);
	}
}

int
bio_blob_create(uuid_t uuid, struct bio_xs_context *xs_ctxt, uint64_t blob_sz)
{
	struct blob_cp_arg		*ba;
	struct spdk_blob_opts		 opts;
	struct bio_blobstore		*bbs;
	struct smd_nvme_pool_info	 smd_pool;
	uint64_t			 cluster_sz;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
	bbs = xs_ctxt->bxc_blobstore;
	D_ASSERT(bbs != NULL);

	ABT_mutex_lock(bbs->bb_mutex);
	cluster_sz = bbs->bb_bs != NULL ?
		spdk_bs_get_cluster_size(bbs->bb_bs) : 0;
	ABT_mutex_unlock(bbs->bb_mutex);

	if (cluster_sz == 0) {
		D_ERROR("Blobstore is already closed?\n");
		return -DER_NO_HDL;
	}

	if (blob_sz < cluster_sz) {
		/* Blob needs to be at least 1 cluster */
		D_ERROR("Blob size is less than the size of a cluster "DF_U64""
			" < "DF_U64"\n", blob_sz, cluster_sz);
		return -DER_INVAL;
	}

	spdk_blob_opts_init(&opts);
	opts.num_clusters = (blob_sz + cluster_sz - 1) / cluster_sz;

	/**
	 * Query per-server metadata to make sure the blob for this pool:xstream
	 * hasn't been created yet.
	 */
	rc = smd_nvme_get_pool(uuid, xs_ctxt->bxc_xs_id, &smd_pool);
	if (rc == 0) {
		D_ERROR("Duplicated blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(uuid));
		return -DER_EXIST;
	}

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		return -DER_NOMEM;

	ba->bca_inflights = 1;
	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_bs != NULL)
		spdk_bs_create_blob_ext(bbs->bb_bs, &opts, blob_create_cb, ba);
	else
		blob_create_cb(ba, 0, -DER_NO_HDL);
	ABT_mutex_unlock(bbs->bb_mutex);

	/* Wait for blob creation done */
	blob_wait_completion(xs_ctxt, ba);
	if (ba->bca_rc != 0) {
		D_ERROR("Create blob failed for xs:%p pool:"DF_UUID" rc:%d\n",
			xs_ctxt, DP_UUID(uuid), ba->bca_rc);
		rc = -DER_IO;
	} else {
		D_ASSERT(ba->bca_id != 0);
		D_DEBUG(DB_MGMT, "Successfully created blobID "DF_U64" for xs:"
			"%p pool:"DF_UUID" blob size:"DF_U64" clusters\n",
			ba->bca_id, xs_ctxt, DP_UUID(uuid), opts.num_clusters);
		rc = 0;
		/* Update per-server metadata */
		smd_nvme_set_pool_info(uuid, xs_ctxt->bxc_xs_id, ba->bca_id,
				       &smd_pool);
		rc = smd_nvme_add_pool(&smd_pool);
		if (rc != 0) {
			/* Delete newly created blob */
			D_ERROR("Failure adding SMD pool table entry\n");
			if (bio_blob_delete(uuid, xs_ctxt))
				D_ERROR("Unable to delete newly created blobID "
					""DF_U64" for xs:%p pool:"DF_UUID"\n",
					ba->bca_id, xs_ctxt, DP_UUID(uuid));
			D_GOTO(error, rc);
		}

		D_DEBUG(DB_MGMT, "Successfully added entry to SMD pool table, "
			"pool:"DF_UUID", xs_id:%d, blobID:"DF_U64"\n",
			DP_UUID(uuid), xs_ctxt->bxc_xs_id, ba->bca_id);
	}

error:
	free_blob_cp_arg(ba);
	return rc;
}

int
bio_ioctxt_open(struct bio_io_context **pctxt, struct bio_xs_context *xs_ctxt,
		struct umem_instance *umem, uuid_t uuid)
{
	struct bio_io_context		*ctxt;
	struct blob_cp_arg		*ba;
	struct bio_blobstore		*bbs;
	struct smd_nvme_pool_info	 smd_pool;
	spdk_blob_id			 blob_id;
	int				 rc;

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	ctxt->bic_umem = umem;
	ctxt->bic_pmempool_uuid = umem_get_uuid(umem);
	ctxt->bic_blob = NULL;
	ctxt->bic_xs_ctxt = xs_ctxt;

	/* NVMe isn't configured */
	if (xs_ctxt == NULL) {
		*pctxt = ctxt;
		return 0;
	}

	/*
	 * Query per-server metadata to get blobID for this pool:xstream.
	 */
	rc = smd_nvme_get_pool(uuid, xs_ctxt->bxc_xs_id, &smd_pool);
	if (rc != 0) {
		D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(uuid));
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	blob_id = smd_pool.npi_blob_id;

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "Opening blobID "DF_U64" for xs:%p pool:"DF_UUID"\n",
		blob_id, xs_ctxt, DP_UUID(uuid));

	bbs = xs_ctxt->bxc_blobstore;
	D_ASSERT(bbs != NULL);

	ba->bca_inflights = 1;
	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_bs != NULL)
		spdk_bs_open_blob(bbs->bb_bs, blob_id, blob_open_cb, ba);
	else
		blob_open_cb(ba, NULL, -DER_NO_HDL);
	ABT_mutex_unlock(bbs->bb_mutex);

	/* Wait for blob open done */
	blob_wait_completion(xs_ctxt, ba);
	if (ba->bca_rc != 0) {
		D_ERROR("Open blobID "DF_U64" failed for xs:%p pool:"DF_UUID" "
			"rc:%d\n", blob_id, xs_ctxt, DP_UUID(uuid), ba->bca_rc);
		rc = -DER_IO;
	} else {
		D_ASSERT(ba->bca_blob != NULL);
		D_DEBUG(DB_MGMT, "Successfully opened blobID "DF_U64" for xs:%p"
			" pool:"DF_UUID" blob:%p\n", blob_id, xs_ctxt,
			DP_UUID(uuid), ba->bca_blob);
		ctxt->bic_blob = ba->bca_blob;
		*pctxt = ctxt;
		rc = 0;
	}

	free_blob_cp_arg(ba);
out:
	if (rc != 0)
		D_FREE_PTR(ctxt);
	return rc;
}

int
bio_ioctxt_close(struct bio_io_context *ctxt)
{
	struct blob_cp_arg	*ba;
	struct bio_blobstore	*bbs;
	int			 rc;

	/* NVMe isn't configured */
	if (ctxt->bic_blob == NULL)
		D_GOTO(out, rc = 0);

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "Closing blob %p for xs:%p\n", ctxt->bic_blob,
		ctxt->bic_xs_ctxt);

	D_ASSERT(ctxt->bic_xs_ctxt != NULL);
	bbs = ctxt->bic_xs_ctxt->bxc_blobstore;
	D_ASSERT(bbs != NULL);

	ba->bca_inflights = 1;
	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_bs != NULL)
		spdk_blob_close(ctxt->bic_blob, blob_cb, ba);
	else
		blob_cb(ba, -DER_NO_HDL);
	ABT_mutex_unlock(bbs->bb_mutex);

	/* Wait for blob close done */
	blob_wait_completion(ctxt->bic_xs_ctxt, ba);
	if (ba->bca_rc != 0) {
		D_ERROR("Close blob %p failed for xs:%p rc:%d\n",
			ctxt->bic_blob, ctxt->bic_xs_ctxt, ba->bca_rc);
		rc = -DER_IO;
	} else {
		D_DEBUG(DB_MGMT, "Successfully closed blob %p for xs:%p\n",
			ctxt->bic_blob, ctxt->bic_xs_ctxt);
		rc = 0;
	}

	free_blob_cp_arg(ba);
out:
	D_FREE_PTR(ctxt);
	return rc;
}

int
bio_blob_delete(uuid_t uuid, struct bio_xs_context *xs_ctxt)
{
	struct blob_cp_arg		*ba;
	struct bio_blobstore		*bbs;
	struct smd_nvme_pool_info	 smd_pool;
	spdk_blob_id			 blob_id;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
	bbs = xs_ctxt->bxc_blobstore;
	D_ASSERT(bbs != NULL);

	/**
	 * Query per-server metadata to get blobID for this pool:xstream
	 */
	rc = smd_nvme_get_pool(uuid, xs_ctxt->bxc_xs_id, &smd_pool);
	if (rc != 0) {
		D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(uuid));
		return -DER_NONEXIST;
	}

	blob_id = smd_pool.npi_blob_id;

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		return -DER_NOMEM;

	D_DEBUG(DB_MGMT, "Deleting blobID "DF_U64" for pool:"DF_UUID" xs:%p\n",
		blob_id, DP_UUID(uuid), xs_ctxt);

	ba->bca_inflights = 1;
	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_bs != NULL)
		spdk_bs_delete_blob(bbs->bb_bs, blob_id, blob_cb, ba);
	else
		blob_cb(ba, -DER_NO_HDL);
	ABT_mutex_unlock(bbs->bb_mutex);

	/* Wait for blob delete done */
	blob_wait_completion(xs_ctxt, ba);
	if (ba->bca_rc != 0) {
		D_ERROR("Delete blobID "DF_U64" failed for pool:"DF_UUID" "
			"xs:%p rc:%d\n",
			blob_id, DP_UUID(uuid), xs_ctxt, ba->bca_rc);
		rc = -DER_IO;
	} else {
		D_DEBUG(DB_MGMT, "Successfully deleted blobID "DF_U64" for "
			"pool:"DF_UUID" xs:%p\n",
			blob_id, DP_UUID(uuid), xs_ctxt);
		rc = 0;
	}

	free_blob_cp_arg(ba);
	return rc;
}

static int
bio_rw_iov(struct bio_io_context *ioctxt, bio_addr_t addr, daos_iov_t *iov,
	   bool update)
{
	struct bio_desc		*biod;
	struct bio_sglist	*bsgl;
	unsigned int		 iod_cnt = 1;
	int			 rc;

	/* allocate blob I/O descriptor */
	biod = bio_iod_alloc(ioctxt, iod_cnt, update);
	if (biod == NULL)
		return -DER_NOMEM;

	/* setup bio sgl in bio descriptor */
	bsgl = bio_iod_sgl(biod, 0); /* bsgl = &biod->bd_sgls[0] */
	rc = bio_sgl_init(bsgl, iod_cnt); /* sets up bsgl->bs_iovs */
	if (rc)
		goto out; /* rc = -DER_NOMEM */

	/* store byte offset and device type */
	bsgl->bs_iovs[0].bi_addr = addr;
	bsgl->bs_iovs[0].bi_data_len = iov->iov_len;
	bsgl->bs_nr_out++;

	/* map the biov to DMA safe buffer, fill DMA buffer if read operation */
	rc = bio_iod_prep(biod);
	if (rc)
		goto out;
	D_ASSERT(bsgl->bs_iovs[0].bi_buf != NULL);

	/* copy data from/to iov and DMA safe buffer for write/read */
	bio_memcpy(biod, addr.ba_type, bsgl->bs_iovs[0].bi_buf,
		   iov->iov_buf, iov->iov_len);

	/* release DMA buffer, write data back to NVMe device for write */
	rc = bio_iod_post(biod);

out:
	bio_iod_free(biod); /* also calls bio_sgl_fini */

	return rc;
}

int
bio_readv(struct bio_io_context *ioctxt, bio_addr_t addr, daos_iov_t *iov)
{
	int	rc;

	D_DEBUG(DB_MGMT, "Reading from blob %p for xs:%p\n", ioctxt->bic_blob,
		ioctxt->bic_xs_ctxt);

	rc = bio_rw_iov(ioctxt, addr, iov, false);
	if (rc != 0)
		D_ERROR("Read from blob:%p failed for xs:%p rc:%d\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt, rc);
	else
		D_DEBUG(DB_MGMT, "Successfully read from blob %p for xs:%p\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

	return rc;

}

int
bio_writev(struct bio_io_context *ioctxt, bio_addr_t addr, daos_iov_t *iov)
{
	int	rc;

	D_DEBUG(DB_MGMT, "Writing to blob %p for xs:%p\n", ioctxt->bic_blob,
		ioctxt->bic_xs_ctxt);

	rc = bio_rw_iov(ioctxt, addr, iov, true);
	if (rc != 0)
		D_ERROR("Write to blob:%p failed for xs:%p rc:%d\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt, rc);
	else
		D_DEBUG(DB_MGMT, "Successfully wrote to blob %p for xs:%p\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

	return rc;
}

int
bio_write_blob_hdr(struct bio_io_context *ioctxt, struct bio_blob_hdr *bio_bh)
{
	struct smd_nvme_pool_info	smd_pool;
	struct smd_nvme_stream_bond	smd_xs_mapping;
	daos_iov_t			iov;
	bio_addr_t			addr;
	uint64_t			off = 0; /* byte offset in SPDK blob */
	uint16_t			dev_type = BIO_ADDR_NVME;
	int				rc = 0;

	D_DEBUG(DB_MGMT, "Writing header blob:%p, xs:%p\n",
		ioctxt->bic_blob, ioctxt->bic_xs_ctxt);

	/* check that all VOS blob header vars are set */
	D_ASSERT(uuid_is_null(bio_bh->bbh_pool) == 0);
	if (bio_bh->bbh_blk_sz == 0 || bio_bh->bbh_hdr_sz == 0)
		return -DER_INVAL;

	bio_addr_set(&addr, dev_type, off);

	/*
	 * Set all BIO-related members of blob header.
	 */
	bio_bh->bbh_magic = BIO_BLOB_HDR_MAGIC;
	bio_bh->bbh_vos_id = (uint32_t)ioctxt->bic_xs_ctxt->bxc_xs_id;
	/* Query per-server metadata to get blobID for this pool:xstream */
	rc = smd_nvme_get_pool(bio_bh->bbh_pool, bio_bh->bbh_vos_id, &smd_pool);
	if (rc) {
		D_ERROR("Failed to find blobID for xs:%p, pool:"DF_UUID"\n",
			ioctxt->bic_xs_ctxt, DP_UUID(bio_bh->bbh_pool));
		return rc;
	}

	bio_bh->bbh_blob_id = smd_pool.npi_blob_id;

	/* Query per-server metadata to get device id for xs */
	rc = smd_nvme_get_stream_bond(bio_bh->bbh_vos_id, &smd_xs_mapping);
	if (rc) {
		D_ERROR("Not able to find device id/blobstore for xs_id:%d\n",
			bio_bh->bbh_vos_id);
		return rc;
	}

	uuid_copy(bio_bh->bbh_blobstore, smd_xs_mapping.nsm_dev_id);

	/* Create an iov to store blob header structure */
	daos_iov_set(&iov, (void *)bio_bh, sizeof(*bio_bh));

	rc = bio_writev(ioctxt, addr, &iov);

	return rc;
}

int
bio_blob_unmap(struct bio_io_context *ioctxt, uint64_t off, uint64_t len)
{
	struct blob_cp_arg	*ba;
	struct bio_blobstore	*bbs;
	struct spdk_io_channel	*channel;
	uint64_t		 pg_off;
	uint64_t		 pg_cnt;
	int			 rc;

	D_ASSERT(len > 0);

	/* blob unmap can only support page aligned offset and length */
	D_ASSERT((len & (BIO_DMA_PAGE_SZ - 1)) == 0);
	D_ASSERT((off & (BIO_DMA_PAGE_SZ - 1)) == 0);

	/* convert byte to blob/page offset */
	pg_off = off >> BIO_DMA_PAGE_SHIFT;
	pg_cnt = len >> BIO_DMA_PAGE_SHIFT;

	D_ASSERT(ioctxt->bic_xs_ctxt != NULL);
	bbs = ioctxt->bic_xs_ctxt->bxc_blobstore;
	channel = ioctxt->bic_xs_ctxt->bxc_io_channel;

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		return -DER_NOMEM;

	D_DEBUG(DB_MGMT, "Unmapping blob %p pgoff:"DF_U64" pgcnt:"DF_U64"\n",
		ioctxt->bic_blob, pg_off, pg_cnt);

	ba->bca_inflights = 1;
	ABT_mutex_lock(bbs->bb_mutex);
	if (bbs->bb_bs != NULL)
		spdk_blob_io_unmap(ioctxt->bic_blob, channel, pg_off, pg_cnt,
				   blob_cb, ba);
	else
		blob_cb(ba, -DER_NO_HDL);
	ABT_mutex_lock(bbs->bb_mutex);

	/* Wait for blob unmap done */
	blob_wait_completion(ioctxt->bic_xs_ctxt, ba);
	if (ba->bca_rc != 0) {
		D_ERROR("Unmap blob %p failed for xs: %p rc:%d\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt, ba->bca_rc);
		rc = -DER_IO;
	} else {
		D_DEBUG(DB_MGMT, "Successfully unmapped blob %p for xs:%p\n",
			ioctxt->bic_blob, ioctxt->bic_xs_ctxt);
		rc = 0;
	}

	free_blob_cp_arg(ba);

	return rc;
}
