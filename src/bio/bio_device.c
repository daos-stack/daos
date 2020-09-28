/**
 * (C) Copyright 2020 Intel Corporation.
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
#include <spdk/thread.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>

static int
revive_dev(struct bio_bdev *d_bdev)
{
	struct bio_blobstore	*bbs;
	int			 rc;

	D_ASSERT(d_bdev);
	if (d_bdev->bb_removed) {
		D_ERROR("Old dev "DF_UUID"(%s) is hot removed\n",
			DP_UUID(d_bdev->bb_uuid), d_bdev->bb_name);
		return -DER_INVAL;
	}

	rc = smd_dev_set_state(d_bdev->bb_uuid, SMD_DEV_NORMAL);
	if (rc) {
		D_ERROR("Set device state failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	bbs = d_bdev->bb_blobstore;
	D_ASSERT(bbs != NULL);

	/* Read bb_state from init xstream */
	if (bbs->bb_state != BIO_BS_STATE_OUT) {
		D_ERROR("Old dev "DF_UUID" isn't in %s state (%s)\n",
			DP_UUID(d_bdev->bb_uuid),
			bio_state_enum_to_str(BIO_BS_STATE_OUT),
			bio_state_enum_to_str(bbs->bb_state));
		return -DER_BUSY;
	}

	D_ASSERT(owner_thread(bbs) != NULL);
	spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, d_bdev);

	return 0;
}

static bool
is_tgt_on_dev(struct smd_dev_info *dev_info, int tgt_idx)
{
	int	i;

	for (i = 0; i < dev_info->sdi_tgt_cnt; i++) {
		if (tgt_idx == dev_info->sdi_tgts[i])
			return true;
	}
	return false;
}

struct blob_ops_arg {
	ABT_eventual	boa_eventual;
	int		boa_rc;
	spdk_blob_id	boa_blob_id;
};

static void
blob_create_cp(void *cb_arg, spdk_blob_id blob_id, int rc)
{
	struct blob_ops_arg	*boa = cb_arg;

	boa->boa_rc = daos_errno2der(-rc);
	boa->boa_blob_id = blob_id;
	ABT_eventual_set(boa->boa_eventual, NULL, 0);
	if (rc)
		D_ERROR("Create blob failed. %d\n", rc);
}

static void
blob_delete_cp(void *cb_arg, int rc)
{
	struct blob_ops_arg	*boa = cb_arg;

	boa->boa_rc = daos_errno2der(-rc);
	ABT_eventual_set(boa->boa_eventual, NULL, 0);
	if (rc)
		D_ERROR("Delete blob failed. %d\n", rc);
}

static int
create_one_blob(struct spdk_blob_store *bs, uint64_t blob_sz,
		spdk_blob_id *blob_id)
{
	struct blob_ops_arg	boa = { 0 };
	struct spdk_blob_opts	blob_opts;
	uint64_t		cluster_sz;
	int			rc;

	D_ASSERT(bs != NULL);
	*blob_id = 0;
	cluster_sz = spdk_bs_get_cluster_size(bs);

	if (blob_sz < cluster_sz) {
		D_ERROR("Invalid blob size "DF_U64", cluster size "DF_U64"\n",
			blob_sz, cluster_sz);
		return -DER_INVAL;
	}

	rc = ABT_eventual_create(0, &boa.boa_eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	spdk_blob_opts_init(&blob_opts);
	blob_opts.num_clusters = (blob_sz + cluster_sz - 1) / cluster_sz;

	spdk_bs_create_blob_ext(bs, &blob_opts, blob_create_cp, &boa);

	rc = ABT_eventual_wait(boa.boa_eventual, NULL);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_ERROR("Wait eventual failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = boa.boa_rc;
	if (rc)
		D_ERROR("Create blob failed. "DF_RC"\n", DP_RC(rc));
	else
		*blob_id = boa.boa_blob_id;
out:
	ABT_eventual_free(&boa.boa_eventual);
	return rc;
}

static int
delete_one_blob(struct spdk_blob_store *bs, spdk_blob_id blob_id)
{
	struct blob_ops_arg	boa = { 0 };
	int			rc;

	D_ASSERT(bs != NULL);
	rc = ABT_eventual_create(0, &boa.boa_eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	spdk_bs_delete_blob(bs, blob_id, blob_delete_cp, &boa);

	rc = ABT_eventual_wait(boa.boa_eventual, NULL);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_ERROR("Wait eventual failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = boa.boa_rc;
	if (rc)
		D_ERROR("Delete blob("DF_U64") failed. "DF_RC"\n",
			blob_id, DP_RC(rc));
out:
	ABT_eventual_free(&boa.boa_eventual);
	return rc;
}

struct blob_item {
	d_list_t	bi_link;
	spdk_blob_id	bi_blob_id;
};

static int
create_old_blobs(struct bio_xs_context *xs_ctxt, struct smd_dev_info *old_info,
		 struct bio_bdev *d_bdev, d_list_t *pool_list,
		 d_list_t *blob_list)
{
	struct spdk_blob_store	*bs;
	struct smd_pool_info	*pool_info;
	uint64_t		 blob_id;
	struct blob_item	*created;
	int			 i, rc = 0;

	D_ASSERT(d_bdev && d_bdev->bb_replacing);
	D_ASSERT(d_list_empty(blob_list));

	if (d_list_empty(pool_list))
		return 0;

	bs = load_blobstore(xs_ctxt, d_bdev->bb_name, &d_bdev->bb_uuid,
			    false, false, NULL, NULL);
	if (bs == NULL) {
		D_ERROR("Failed to load blobstore for new dev "DF_UUID"\n",
			DP_UUID(d_bdev->bb_uuid));
		return -DER_INVAL;
	}

	/*
	 * Iterate all pools, create old blobs on new device, replace the
	 * old blob IDs with new blob IDs in the pool info.
	 */
	d_list_for_each_entry(pool_info, pool_list, spi_link) {
		bool	found_tgt = false;

		for (i = 0; i < pool_info->spi_tgt_cnt; i++) {
			/* Skip the targets not assigned to old device */
			if (!is_tgt_on_dev(old_info, pool_info->spi_tgts[i]))
				continue;

			found_tgt = true;
			rc = create_one_blob(bs, pool_info->spi_blob_sz,
					     &blob_id);
			if (rc)
				goto out;

			D_ASSERT(blob_id != 0);
			/* Add to created blob list */
			D_ALLOC_PTR(created);
			if (created == NULL) {
				rc = -DER_NOMEM;
				goto out;
			}
			D_INIT_LIST_HEAD(&created->bi_link);
			created->bi_blob_id = blob_id;
			d_list_add_tail(&created->bi_link, blob_list);

			/* Replace the blob id in pool info */
			pool_info->spi_blobs[i] = blob_id;
		}

		/*
		 * TODO: Pool is created during target is in DOWN state? Let's
		 *	 handle this once DAOS-5134 is fixed.
		 */
		if (!found_tgt) {
			D_ERROR("No blobs from "DF_UUID" on dev "DF_UUID"\n",
				DP_UUID(pool_info->spi_id),
				DP_UUID(d_bdev->bb_uuid));
			rc = -DER_NOSYS;
			goto out;
		}
	}
out:
	unload_blobstore(xs_ctxt, bs);
	return rc;
}

static void
free_blob_list(struct bio_xs_context *xs_ctxt, d_list_t *blob_list,
	       struct bio_bdev *d_bdev)
{
	struct spdk_blob_store	*bs = NULL;
	struct blob_item	*created, *tmp;

	if (d_bdev == NULL)
		goto free;

	D_ASSERT(d_bdev->bb_replacing);
	bs = load_blobstore(xs_ctxt, d_bdev->bb_name, &d_bdev->bb_uuid,
			    false, false, NULL, NULL);
	if (bs == NULL)
		D_ERROR("Failed to load blobstore for new dev "DF_UUID"\n",
			DP_UUID(d_bdev->bb_uuid));

free:
	d_list_for_each_entry_safe(created, tmp, blob_list, bi_link) {
		if (bs != NULL)
			delete_one_blob(bs, created->bi_blob_id);

		d_list_del_init(&created->bi_link);
		D_FREE(created);
	}

	if (bs != NULL)
		unload_blobstore(xs_ctxt, bs);
}

static void
free_pool_list(d_list_t *pool_list)
{
	struct smd_pool_info	*pool_info, *tmp;

	d_list_for_each_entry_safe(pool_info, tmp, pool_list, spi_link) {
		d_list_del_init(&pool_info->spi_link);
		smd_free_pool_info(pool_info);
	}
}

static int
replace_dev(struct bio_xs_context *xs_ctxt, struct smd_dev_info *old_info,
	    struct bio_bdev *old_dev, struct bio_bdev *new_dev)
{
	struct bio_blobstore	*bbs = old_dev->bb_blobstore;
	d_list_t		 pool_list, blob_list;
	int			 pool_cnt = 0, rc;

	D_ASSERT(bbs != NULL);
	D_ASSERT(bbs->bb_state == BIO_BS_STATE_OUT);
	D_ASSERT(new_dev->bb_blobstore == NULL);

	/* Check if the new device is unplugged */
	if (new_dev->bb_removed) {
		D_ERROR("New dev "DF_UUID"(%s) is hot removed\n",
			DP_UUID(new_dev->bb_uuid), new_dev->bb_name);
		return -DER_INVAL;
	} else if (new_dev->bb_replacing) {
		D_ERROR("New dev "DF_UUID"(%s) is being replaced\n",
			DP_UUID(new_dev->bb_uuid), new_dev->bb_name);
		return -DER_BUSY;
	}
	/* Avoid re-enter or being destroyed by hot remove callback */
	new_dev->bb_replacing = true;

	/* Create existing blobs on new device */
	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	if (rc) {
		D_ERROR("Failed to list pools in SMD. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	D_INIT_LIST_HEAD(&blob_list);
	rc = create_old_blobs(xs_ctxt, old_info, new_dev, &pool_list,
			      &blob_list);
	if (rc) {
		D_ERROR("Failed to create old blobs. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	/* Replace old device with new device in SMD */
	rc = smd_dev_replace(old_dev->bb_uuid, new_dev->bb_uuid, &pool_list);
	if (rc) {
		D_ERROR("Failed to replace dev: "DF_UUID" -> "DF_UUID", "
			""DF_RC"\n", DP_UUID(old_dev->bb_uuid),
			DP_UUID(new_dev->bb_uuid), DP_RC(rc));
		goto out;
	}

	/* Replace in-memory bio_bdev */
	replace_bio_bdev(old_dev, new_dev);
	new_dev->bb_replacing = false;
	old_dev = new_dev;
	new_dev = NULL;

	/*
	 * Trigger auto reint only when faulty device is replaced by new hot
	 * plugged device.
	 *
	 * FIXME: A known limitation is that if server restart before reint
	 * is triggered, we'll miss auto reint on the replaced device. It's
	 * supposed to be fixed once incremental reint is ready.
	 */
	old_dev->bb_trigger_reint = true;

	/* Transit BS state to SETUP */
	D_ASSERT(owner_thread(bbs) != NULL);
	spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, old_dev);

out:
	free_blob_list(xs_ctxt, &blob_list, new_dev);
	free_pool_list(&pool_list);
	if (new_dev)
		new_dev->bb_replacing = false;
	return rc;
}

int
bio_replace_dev(struct bio_xs_context *xs_ctxt, uuid_t old_dev_id,
		uuid_t new_dev_id)
{
	struct smd_dev_info	*old_info = NULL, *new_info = NULL;
	struct bio_bdev		*old_dev, *new_dev;
	int			 rc;

	/* Caller ensures the request handling ULT created on init xstream */
	D_ASSERT(is_init_xstream(xs_ctxt));

	/* Sanity check over old device */
	rc = smd_dev_get_by_id(old_dev_id, &old_info);
	if (rc) {
		D_ERROR("Lookup old dev "DF_UUID" in SMD failed. "DF_RC"\n",
			DP_UUID(old_dev_id), DP_RC(rc));
		return rc;
	}

	if (old_info->sdi_state != SMD_DEV_FAULTY) {
		D_ERROR("Old dev "DF_UUID" isn't in faulty state(%d)\n",
			DP_UUID(old_dev_id), old_info->sdi_state);
		rc = -DER_INVAL;
		goto out;
	}

	old_dev = lookup_dev_by_id(old_dev_id);
	if (old_dev == NULL) {
		D_ERROR("Failed to find old dev "DF_UUID"\n",
			DP_UUID(old_dev_id));
		rc = -DER_NONEXIST;
		goto out;
	}

	/* Change a faulty device back to normal, it's usually for testing */
	if (uuid_compare(old_dev_id, new_dev_id) == 0) {
		rc = revive_dev(old_dev);
		goto out;
	}

	if (old_dev->bb_desc != NULL) {
		D_INFO("Old Dev "DF_UUID"(%s) isn't torndown\n",
		       DP_UUID(old_dev->bb_uuid), old_dev->bb_name);
		rc = -DER_BUSY;
		goto out;
	}

	/* Sanity check over new device */
	rc = smd_dev_get_by_id(new_dev_id, &new_info);
	if (rc == 0) {
		D_ERROR("New dev "DF_UUID" is already used by DAOS\n",
			DP_UUID(new_dev_id));

		D_ASSERT(new_info != NULL);
		rc = -DER_INVAL;
		goto out;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("Lookup new dev "DF_UUID" in SMD failed. "DF_RC"\n",
			DP_UUID(new_dev_id), DP_RC(rc));
		goto out;
	}

	new_dev = lookup_dev_by_id(new_dev_id);
	if (new_dev == NULL) {
		D_ERROR("Failed to find new dev "DF_UUID"\n",
			DP_UUID(new_dev_id));
		rc = -DER_INVAL;
		goto out;
	}

	rc = replace_dev(xs_ctxt, old_info, old_dev, new_dev);
out:
	if (old_info)
		smd_free_dev_info(old_info);
	if (new_info)
		smd_free_dev_info(new_info);
	return rc;
}
