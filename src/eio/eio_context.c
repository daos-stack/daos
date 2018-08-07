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
#define D_LOGFAC	DD_FAC(eio)

#include <spdk/blob.h>
#include "eio_internal.h"
#include "smd/smd_internal.h"

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
blob_close_or_delete_cb(void *arg, int rc)
{
	struct blob_cp_arg	*ba = arg;

	blob_common_cb(ba, rc);
}

static void
blob_wait_completion(struct eio_xs_context *xs_ctxt, struct blob_cp_arg *ba)
{
	D_ASSERT(xs_ctxt != NULL);
	if (xs_ctxt->exc_xs_id == -1) {
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
eio_blob_create(uuid_t uuid, struct eio_xs_context *xs_ctxt, uint64_t blob_sz)
{
	struct blob_cp_arg		*ba;
	struct spdk_blob_opts		 opts;
	struct eio_blobstore		*ebs;
	struct smd_nvme_pool_info	 smd_pool;
	uint64_t			 cluster_sz;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
	ebs = xs_ctxt->exc_blobstore;
	D_ASSERT(ebs != NULL);

	ABT_mutex_lock(ebs->eb_mutex);
	cluster_sz = ebs->eb_bs != NULL ?
		spdk_bs_get_cluster_size(ebs->eb_bs) : 0;
	ABT_mutex_unlock(ebs->eb_mutex);

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
	rc = smd_nvme_get_pool(uuid, xs_ctxt->exc_xs_id, &smd_pool);
	if (rc == 0) {
		D_ERROR("Duplicated blob for xs:%p pool:"DF_UUID"\n",
			xs_ctxt, DP_UUID(uuid));
		return -DER_EXIST;
	}

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		return -DER_NOMEM;

	ba->bca_inflights = 1;
	ABT_mutex_lock(ebs->eb_mutex);
	if (ebs->eb_bs != NULL)
		spdk_bs_create_blob_ext(ebs->eb_bs, &opts, blob_create_cb, ba);
	else
		blob_create_cb(ba, 0, -DER_NO_HDL);
	ABT_mutex_unlock(ebs->eb_mutex);

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
		smd_nvme_set_pool_info(uuid, xs_ctxt->exc_xs_id, ba->bca_id,
				       &smd_pool);
		rc = smd_nvme_add_pool(&smd_pool);
		if (rc != 0) {
			/* Delete newly created blob */
			D_ERROR("Failure adding SMD pool table entry\n");
			if (eio_blob_delete(uuid, xs_ctxt))
				D_ERROR("Unable to delete newly created blobID "
					""DF_U64" for xs:%p pool:"DF_UUID"\n",
					ba->bca_id, xs_ctxt, DP_UUID(uuid));
			D_GOTO(error, rc);
		}

		D_DEBUG(DB_MGMT, "Successfully added entry to SMD pool table, "
			"pool:"DF_UUID", xs_id:%d, blobID:"DF_U64"\n",
			DP_UUID(uuid), xs_ctxt->exc_xs_id, ba->bca_id);
	}

error:
	free_blob_cp_arg(ba);
	return rc;
}

int
eio_ioctxt_open(struct eio_io_context **pctxt, struct eio_xs_context *xs_ctxt,
		struct umem_instance *umem, uuid_t uuid)
{
	struct eio_io_context		*ctxt;
	struct blob_cp_arg		*ba;
	struct eio_blobstore		*ebs;
	struct smd_nvme_pool_info	 smd_pool;
	spdk_blob_id			 blob_id;
	int				 rc;

	D_ALLOC_PTR(ctxt);
	if (ctxt == NULL)
		return -DER_NOMEM;

	ctxt->eic_umem = umem;
	ctxt->eic_pmempool_uuid = umem_get_uuid(umem);
	ctxt->eic_blob = NULL;
	ctxt->eic_xs_ctxt = xs_ctxt;

	/* NVMe isn't configured */
	if (xs_ctxt == NULL) {
		*pctxt = ctxt;
		return 0;
	}

	/*
	 * Query per-server metadata to get blobID for this pool:xstream.
	 */
	rc = smd_nvme_get_pool(uuid, xs_ctxt->exc_xs_id, &smd_pool);
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

	ebs = xs_ctxt->exc_blobstore;
	D_ASSERT(ebs != NULL);

	ba->bca_inflights = 1;
	ABT_mutex_lock(ebs->eb_mutex);
	if (ebs->eb_bs != NULL)
		spdk_bs_open_blob(ebs->eb_bs, blob_id, blob_open_cb, ba);
	else
		blob_open_cb(ba, NULL, -DER_NO_HDL);
	ABT_mutex_unlock(ebs->eb_mutex);

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
		ctxt->eic_blob = ba->bca_blob;
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
eio_ioctxt_close(struct eio_io_context *ctxt)
{
	struct blob_cp_arg	*ba;
	struct eio_blobstore	*ebs;
	int			 rc;

	/* NVMe isn't configured */
	if (ctxt->eic_blob == NULL)
		D_GOTO(out, rc = 0);

	ba = alloc_blob_cp_arg();
	if (ba == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "Closing blob %p for xs:%p\n", ctxt->eic_blob,
		ctxt->eic_xs_ctxt);

	D_ASSERT(ctxt->eic_xs_ctxt != NULL);
	ebs = ctxt->eic_xs_ctxt->exc_blobstore;
	D_ASSERT(ebs != NULL);

	ba->bca_inflights = 1;
	ABT_mutex_lock(ebs->eb_mutex);
	if (ebs->eb_bs != NULL)
		spdk_blob_close(ctxt->eic_blob, blob_close_or_delete_cb, ba);
	else
		blob_close_or_delete_cb(ba, -DER_NO_HDL);
	ABT_mutex_unlock(ebs->eb_mutex);

	/* Wait for blob close done */
	blob_wait_completion(ctxt->eic_xs_ctxt, ba);
	if (ba->bca_rc != 0) {
		D_ERROR("Close blob %p failed for xs:%p rc:%d\n",
			ctxt->eic_blob, ctxt->eic_xs_ctxt, ba->bca_rc);
		rc = -DER_IO;
	} else {
		D_DEBUG(DB_MGMT, "Successfully closed blob %p for xs:%p\n",
			ctxt->eic_blob, ctxt->eic_xs_ctxt);
		rc = 0;
	}

	free_blob_cp_arg(ba);
out:
	D_FREE_PTR(ctxt);
	return rc;
}

int
eio_blob_delete(uuid_t uuid, struct eio_xs_context *xs_ctxt)
{
	struct blob_cp_arg		*ba;
	struct eio_blobstore		*ebs;
	struct smd_nvme_pool_info	 smd_pool;
	spdk_blob_id			 blob_id;
	int				 rc;

	D_ASSERT(xs_ctxt != NULL);
	ebs = xs_ctxt->exc_blobstore;
	D_ASSERT(ebs != NULL);

	/**
	 * Query per-server metadata to get blobID for this pool:xstream
	 */
	rc = smd_nvme_get_pool(uuid, xs_ctxt->exc_xs_id, &smd_pool);
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
	ABT_mutex_lock(ebs->eb_mutex);
	if (ebs->eb_bs != NULL)
		spdk_bs_delete_blob(ebs->eb_bs, blob_id,
				    blob_close_or_delete_cb, ba);
	else
		blob_close_or_delete_cb(ba, -DER_NO_HDL);
	ABT_mutex_unlock(ebs->eb_mutex);

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
