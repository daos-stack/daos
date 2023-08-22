/**
 * (C) Copyright 2020-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/blob.h>
#include <spdk/thread.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>
#include <spdk/string.h>
#include <spdk/likely.h>
#include <spdk/env.h>
#include <spdk/vmd.h>

#include "smd.pb-c.h"

#define LED_STATE_NAME(s) (ctl__led_state__descriptor.values[s].name)
#define LED_ACTION_NAME(a) (ctl__led_action__descriptor.values[a].name)

struct led_opts {
	struct spdk_pci_addr	pci_addr;
	bool			all_devices;
	bool			finished;
	Ctl__LedAction		action;
	Ctl__LedState		led_state;
	int			status;
};

static int
revive_dev(struct bio_xs_context *xs_ctxt, struct bio_bdev *d_bdev)
{
	struct bio_blobstore    *bbs;
	int			 rc;

	D_ASSERT(d_bdev);
	if (d_bdev->bb_removed) {
		D_ERROR("Old dev "DF_UUID"(%s) is hot removed\n", DP_UUID(d_bdev->bb_uuid),
			d_bdev->bb_name);
		return -DER_INVAL;
	}

	rc = smd_dev_set_state(d_bdev->bb_uuid, SMD_DEV_NORMAL);
	if (rc) {
		D_ERROR("Set device state failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	bbs = d_bdev->bb_blobstore;
	D_ASSERT(bbs != NULL);
	D_ASSERT(bbs->bb_state == BIO_BS_STATE_OUT);
	D_ASSERT(owner_thread(bbs) != NULL);

	spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, d_bdev);

	/* Reset the LED of the VMD device once revived */
	rc = bio_led_manage(xs_ctxt, NULL, d_bdev->bb_uuid, (unsigned int)CTL__LED_ACTION__RESET,
			    NULL, 0);
	if (rc != 0)
		/* DER_NOSYS indicates that VMD-LED control is not enabled */
		D_CDEBUG(rc == -DER_NOSYS, DB_MGMT, DLOG_ERR,
			 "Reset LED on device:" DF_UUID " failed, " DF_RC "\n",
			 DP_UUID(d_bdev->bb_uuid), DP_RC(rc));

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

	spdk_blob_opts_init(&blob_opts, sizeof(blob_opts));
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

		for (i = 0; i < pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA]; i++) {
			/* Skip the targets not assigned to old device */
			if (!is_tgt_on_dev(old_info, pool_info->spi_tgts[SMD_DEV_TYPE_DATA][i]))
				continue;

			found_tgt = true;
			rc = create_one_blob(bs, pool_info->spi_blob_sz[SMD_DEV_TYPE_DATA],
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
			pool_info->spi_blobs[SMD_DEV_TYPE_DATA][i] = blob_id;
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
		smd_pool_free_info(pool_info);
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
		D_ERROR("New dev "DF_UUID"(%s) is in replacing\n",
			DP_UUID(new_dev->bb_uuid), new_dev->bb_name);
		return -DER_BUSY;
	}
	/* Avoid re-enter or being destroyed by hot remove callback */
	new_dev->bb_replacing = 1;

	D_INIT_LIST_HEAD(&pool_list);
	D_INIT_LIST_HEAD(&blob_list);

	/* Create existing blobs on new device */
	rc = smd_pool_list(&pool_list, &pool_cnt);
	if (rc) {
		D_ERROR("Failed to list pools in SMD. "DF_RC"\n", DP_RC(rc));
		goto pool_list_out;
	}

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
	new_dev->bb_replacing = 0;
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
	old_dev->bb_trigger_reint = 1;

	/* Transit BS state to SETUP */
	D_ASSERT(owner_thread(bbs) != NULL);
	spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, old_dev);

out:
	free_blob_list(xs_ctxt, &blob_list, new_dev);
pool_list_out:
	free_pool_list(&pool_list);
	if (new_dev)
		new_dev->bb_replacing = 0;
	return rc;
}

int
bio_replace_dev(struct bio_xs_context *xs_ctxt, uuid_t old_dev_id,
		uuid_t new_dev_id)
{
	struct smd_dev_info	*old_info = NULL, *new_info = NULL;
	struct bio_bdev		*old_dev, *new_dev;
	struct bio_blobstore	*bbs;
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

	bbs = old_dev->bb_blobstore;
	D_ASSERT(bbs != NULL);

	/* Read bb_state from init xstream */
	if (bbs->bb_state != BIO_BS_STATE_OUT) {
		D_ERROR("Old dev "DF_UUID" isn't in %s state (%s)\n",
			DP_UUID(old_dev->bb_uuid),
			bio_state_enum_to_str(BIO_BS_STATE_OUT),
			bio_state_enum_to_str(bbs->bb_state));
		rc = -DER_BUSY;
		goto out;
	}

	/* Change a faulty device back to normal, it's usually for testing */
	if (uuid_compare(old_dev_id, new_dev_id) == 0) {
		rc = revive_dev(xs_ctxt, old_dev);
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
		smd_dev_free_info(old_info);
	if (new_info)
		smd_dev_free_info(new_info);
	return rc;
}

static int
json_write_cb(void *cb_ctx, const void *data, size_t size)
{
	struct bio_dev_info	*b_info = cb_ctx;
	char			*prefix = "traddr\": \"";
	char			*traddr, *end;

	D_ASSERT(b_info != NULL);
	/* traddr is already generated */
	if (b_info->bdi_traddr != NULL)
		return 0;

	if (size <= strlen(prefix))
		return 0;

	traddr = strstr(data, prefix);
	if (traddr) {
		traddr += strlen(prefix);
		end = strchr(traddr, '"');
		if (end == NULL)
			return 0;

		D_STRNDUP(b_info->bdi_traddr, traddr, end - traddr);
		if (b_info->bdi_traddr == NULL) {
			D_ERROR("Failed to alloc traddr %s\n", traddr);
			return -DER_NOMEM;
		}
	}

	return 0;
}

int
fill_in_traddr(struct bio_dev_info *b_info, char *dev_name)
{
	struct spdk_bdev		*bdev;
	struct spdk_json_write_ctx	*json;
	int				 rc;

	D_ASSERT(dev_name != NULL);
	D_ASSERT(b_info != NULL);
	D_ASSERT(b_info->bdi_traddr == NULL);

	bdev = spdk_bdev_get_by_name(dev_name);
	if (bdev == NULL) {
		D_ERROR("Failed to get SPDK bdev for %s\n", dev_name);
		return -DER_NONEXIST;
	}

	if (get_bdev_type(bdev) != BDEV_CLASS_NVME)
		return 0;

	json = spdk_json_write_begin(json_write_cb, b_info,
				     SPDK_JSON_WRITE_FLAG_FORMATTED);
	if (json == NULL) {
		D_ERROR("Failed to alloc SPDK json context\n");
		return -DER_NOMEM;
	}

	rc = spdk_bdev_dump_info_json(bdev, json);
	if (rc != 0) {
		D_ERROR("Failed to dump config from SPDK bdev (%s)\n", spdk_strerror(-rc));
		rc = daos_errno2der(-rc);
	}

	spdk_json_write_end(json);

	if (!rc && b_info->bdi_traddr == NULL) {
		D_ERROR("Failed to get traddr for %s\n", dev_name);
		rc = -DER_INVAL;
	}

	return rc;
}

static struct bio_dev_info *
alloc_dev_info(uuid_t dev_id, char *dev_name, struct smd_dev_info *s_info)
{
	struct bio_dev_info	*info;
	int			 tgt_cnt = 0, i, rc;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	if (s_info != NULL) {
		tgt_cnt = s_info->sdi_tgt_cnt;
		info->bdi_flags |= NVME_DEV_FL_INUSE;
		if (s_info->sdi_state == SMD_DEV_FAULTY)
			info->bdi_flags |= NVME_DEV_FL_FAULTY;
	}

	if (dev_name != NULL) {
		rc = fill_in_traddr(info, dev_name);
		if (rc != 0) {
			bio_free_dev_info(info);
			return NULL;
		}
	}

	if (tgt_cnt != 0) {
		D_ALLOC_ARRAY(info->bdi_tgts, tgt_cnt);
		if (info->bdi_tgts == NULL) {
			bio_free_dev_info(info);
			return NULL;
		}
	}

	D_INIT_LIST_HEAD(&info->bdi_link);
	uuid_copy(info->bdi_dev_id, dev_id);
	info->bdi_tgt_cnt = tgt_cnt;
	for (i = 0; i < info->bdi_tgt_cnt; i++)
		info->bdi_tgts[i] = s_info->sdi_tgts[i];

	return info;
}

static struct smd_dev_info *
find_smd_dev(uuid_t dev_id, d_list_t *s_dev_list)
{
	struct smd_dev_info	*s_info;

	d_list_for_each_entry(s_info, s_dev_list, sdi_link) {
		if (uuid_compare(s_info->sdi_id, dev_id) == 0)
			return s_info;
	}

	return NULL;
}

int
bio_dev_list(struct bio_xs_context *xs_ctxt, d_list_t *dev_list, int *dev_cnt)
{
	d_list_t		 s_dev_list;
	struct bio_dev_info	*b_info, *b_tmp;
	struct smd_dev_info	*s_info, *s_tmp;
	struct bio_bdev		*d_bdev;
	int			 rc;

	/* Caller ensures the request handling ULT created on init xstream */
	D_ASSERT(is_init_xstream(xs_ctxt));

	D_ASSERT(dev_list != NULL && d_list_empty(dev_list));
	D_INIT_LIST_HEAD(&s_dev_list);

	rc = smd_dev_list(&s_dev_list, dev_cnt);
	if (rc) {
		D_ERROR("Failed to get SMD dev list "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	*dev_cnt = 0;

	/* Scan all devices present in bio_bdev list */
	d_list_for_each_entry(d_bdev, bio_bdev_list(), bb_link) {
		char *dev_name = d_bdev->bb_removed ? NULL : d_bdev->bb_name;

		s_info = find_smd_dev(d_bdev->bb_uuid, &s_dev_list);

		b_info = alloc_dev_info(d_bdev->bb_uuid, dev_name, s_info);
		if (b_info == NULL) {
			D_ERROR("Failed to allocate device info\n");
			rc = -DER_NOMEM;
			goto out;
		}
		b_info->bdi_dev_roles = d_bdev->bb_roles;
		if (!d_bdev->bb_removed)
			b_info->bdi_flags |= NVME_DEV_FL_PLUGGED;
		if (d_bdev->bb_faulty)
			b_info->bdi_flags |= NVME_DEV_FL_FAULTY;
		d_list_add_tail(&b_info->bdi_link, dev_list);
		(*dev_cnt)++;

		/* delete the found device in SMD dev list */
		if (s_info != NULL) {
			d_list_del_init(&s_info->sdi_link);
			smd_dev_free_info(s_info);
		}
	}

	/*
	 * Scan remaining SMD devices not present bio_bdev list.
	 *
	 * As for current implementation, there won't be any device
	 * present in SMD but not in bio_bdev list, here we just do
	 * it for sanity check.
	 */
	d_list_for_each_entry(s_info, &s_dev_list, sdi_link) {
		D_ERROR("Found unexpected device "DF_UUID" in SMD\n",
			DP_UUID(s_info->sdi_id));

		b_info = alloc_dev_info(s_info->sdi_id, NULL, s_info);
		if (b_info == NULL) {
			D_ERROR("Failed to allocate device info\n");
			rc = -DER_NOMEM;
			goto out;
		}
		d_list_add_tail(&b_info->bdi_link, dev_list);
		(*dev_cnt)++;
	}
out:
	d_list_for_each_entry_safe(s_info, s_tmp, &s_dev_list, sdi_link) {
		d_list_del_init(&s_info->sdi_link);
		smd_dev_free_info(s_info);
	}

	if (rc != 0) {
		d_list_for_each_entry_safe(b_info, b_tmp, dev_list, bdi_link) {
			d_list_del_init(&b_info->bdi_link);
			bio_free_dev_info(b_info);
		}
		*dev_cnt = 0;
	}

	return rc;
}

static void
led_device_action(void *ctx, struct spdk_pci_device *pci_device)
{
	struct led_opts		*opts = ctx;
	enum spdk_vmd_led_state	 cur_led_state;
	const char		*pci_dev_type = NULL;
	char			 addr_buf[ADDR_STR_MAX_LEN + 1];
	int			 rc;

	if (opts->status != 0)
		return;
	if (opts->finished)
		return;

	pci_dev_type = spdk_pci_device_get_type(pci_device);
	if (pci_dev_type == NULL) {
		D_ERROR("nil pci device type returned\n");
		opts->status = -DER_MISC;
		return;
	}

	if (strncmp(pci_dev_type, BIO_DEV_TYPE_VMD, strlen(BIO_DEV_TYPE_VMD)) != 0) {
		D_DEBUG(DB_MGMT, "Found non-VMD device type (%s), can't manage LEDs\n",
			pci_dev_type);
		opts->status = -DER_NOSYS;
		return;
	}

	if (!opts->all_devices) {
		if (spdk_pci_addr_compare(&opts->pci_addr, &pci_device->addr) != 0)
			return;
		opts->finished = true;
	}

	rc = spdk_pci_addr_fmt(addr_buf, sizeof(addr_buf), &pci_device->addr);
	if (rc != 0) {
		D_ERROR("Failed to format VMD's PCI address (%s)\n", spdk_strerror(-rc));
		opts->status = -DER_INVAL;
		return;
	}

	/* First check the current state of the VMD LED */
	rc = spdk_vmd_get_led_state(pci_device, &cur_led_state);
	if (spdk_unlikely(rc != 0)) {
		D_ERROR("Failed to retrieve the state of the LED on %s (%s)\n", addr_buf,
			spdk_strerror(-rc));
		opts->status = -DER_NOSYS;
		return;
	}

	D_DEBUG(DB_MGMT, "led on dev %s has state: %s (action: %s, new state: %s)\n", addr_buf,
		LED_STATE_NAME(cur_led_state), LED_ACTION_NAME(opts->action),
		LED_STATE_NAME(opts->led_state));

	switch (opts->action) {
	case CTL__LED_ACTION__GET:
		/* Return early with current device state set */
		opts->led_state = (Ctl__LedState)cur_led_state;
		return;
	case CTL__LED_ACTION__SET:
		break;
	case CTL__LED_ACTION__RESET:
		/* Reset intercepted earlier in call-stack and converted to set */
		D_ERROR("Reset action is not supported\n");
		opts->status = -DER_INVAL;
		return;
	default:
		D_ERROR("Unrecognized LED action requested\n");
		opts->status = -DER_INVAL;
		return;
	}

	if (cur_led_state == (enum spdk_vmd_led_state)opts->led_state) {
		D_DEBUG(DB_MGMT, "VMD device %s LED state already in state %s\n", addr_buf,
			LED_STATE_NAME(opts->led_state));
		return;
	}

	/* Set the LED to the new state */
	rc = spdk_vmd_set_led_state(pci_device, (enum spdk_vmd_led_state)opts->led_state);
	if (spdk_unlikely(rc != 0)) {
		D_ERROR("Failed to set the VMD LED state on %s (%s)\n", addr_buf,
			spdk_strerror(-rc));
		opts->status = -DER_NOSYS;
		return;
	}

	rc = spdk_vmd_get_led_state(pci_device, &cur_led_state);
	if (rc != 0) {
		D_ERROR("Failed to get the VMD LED state on %s (%s)\n", addr_buf,
			spdk_strerror(-rc));
		opts->status = -DER_NOSYS;
		return;
	}

	/* Verify the correct state is set */
	if (cur_led_state != (enum spdk_vmd_led_state)opts->led_state) {
		D_ERROR("Unexpected LED state on %s, want %s got %s\n", addr_buf,
			LED_STATE_NAME(opts->led_state), LED_STATE_NAME(cur_led_state));
		opts->status = -DER_INVAL;
	}
}

static int
set_timer_and_check_faulty(struct bio_xs_context *xs_ctxt, struct spdk_pci_addr pci_addr,
			   uint64_t *expiry_time, bool *is_faulty)
{
	struct bio_dev_info	*dev_info = NULL, *tmp;
	struct bio_bdev		*d_bdev = NULL;
	d_list_t		 dev_list;
	int			 dev_list_cnt, rc;
	char			 tr_addr[ADDR_STR_MAX_LEN + 1];

	D_ASSERT((expiry_time != NULL) || (is_faulty != NULL));

	rc = spdk_pci_addr_fmt(tr_addr, ADDR_STR_MAX_LEN + 1, &pci_addr);
	if (rc != 0) {
		D_ERROR("Failed to format PCI address (%s)\n", spdk_strerror(-rc));
		return -DER_INVAL;
	}

	D_INIT_LIST_HEAD(&dev_list);

	rc = bio_dev_list(xs_ctxt, &dev_list, &dev_list_cnt);
	if (rc != 0) {
		D_ERROR("Error getting BIO device list\n");
		return rc;
	}

	if (is_faulty != NULL)
		*is_faulty = false;

	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, bdi_link) {
		if (dev_info->bdi_traddr == NULL) {
			D_ERROR("No transport address for dev:"DF_UUID", unable to verify state\n",
				DP_UUID(dev_info->bdi_dev_id));
			rc = -DER_INVAL;
			goto out;
		}

		if (strcmp(dev_info->bdi_traddr, tr_addr) == 0) {
			if ((is_faulty != NULL) && (dev_info->bdi_flags & NVME_DEV_FL_FAULTY) != 0)
				*is_faulty = true;

			if (expiry_time != NULL) {
				d_bdev = lookup_dev_by_id(dev_info->bdi_dev_id);
				if (d_bdev == NULL) {
					D_ERROR("Failed to find dev "DF_UUID"\n",
						DP_UUID(dev_info->bdi_dev_id));
					rc = -DER_NONEXIST;
					goto out;
				}

				d_bdev->bb_led_expiry_time = *expiry_time;
			}
		}
	}

out:
	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, bdi_link) {
		d_list_del(&dev_info->bdi_link);
		bio_free_dev_info(dev_info);
	}

	return rc;
}

static int
set_timer(struct bio_xs_context *xs_ctxt, struct spdk_pci_addr pci_addr, uint64_t expiry_time) {
	return set_timer_and_check_faulty(xs_ctxt, pci_addr, &expiry_time, NULL);
}

static int
check_faulty(struct bio_xs_context *xs_ctxt, struct spdk_pci_addr pci_addr, bool *is_faulty) {
	return set_timer_and_check_faulty(xs_ctxt, pci_addr, NULL, is_faulty);
}

static int
led_manage(struct bio_xs_context *xs_ctxt, struct spdk_pci_addr pci_addr, Ctl__LedAction action,
	   Ctl__LedState *state, uint64_t duration) {
	struct led_opts		opts = { 0 };
	bool			is_faulty;
	int			rc;

	D_ASSERT(is_init_xstream(xs_ctxt));

	/* Init context to be used by led_device_action() */
	opts.all_devices = false;
	opts.finished = false;
	opts.led_state = CTL__LED_STATE__NA;
	opts.status = 0;
	opts.pci_addr = pci_addr;

	/* Validate LED action value. */
	switch (action) {
	case CTL__LED_ACTION__GET:
		opts.action = action;
		break;
	case CTL__LED_ACTION__SET:
		opts.action = action;
		if (state == NULL) {
			D_ERROR("LED state not set for SET action\n");
			return -DER_INVAL;
		}
		opts.led_state = *state;
		break;
	case CTL__LED_ACTION__RESET:
		opts.action = CTL__LED_ACTION__SET;
		/* Check if any relevant bdevs are faulty, if yes set faulty, if no set normal */
		is_faulty = false;
		rc = check_faulty(xs_ctxt, pci_addr, &is_faulty);
		if (rc != 0) {
			D_ERROR("Reset LED failed during check for faulty devices (%d)\n", rc);
			return rc;
		}
		if (is_faulty)
			opts.led_state = CTL__LED_STATE__ON;
		else
			opts.led_state = CTL__LED_STATE__OFF;
		break;
	default:
		D_ERROR("invalid action supplied: %d\n", action);
		return -DER_INVAL;
	}

	spdk_pci_for_each_device(&opts, led_device_action);

	if (opts.status != 0) {
		if (opts.status != -DER_NOSYS) {
			if (state != NULL)
				D_ERROR("LED %s failed (target state: %s): %s\n",
					LED_ACTION_NAME(action), LED_STATE_NAME(*state),
					spdk_strerror(opts.status));
			else
				D_ERROR("LED %s failed: %s\n", LED_ACTION_NAME(action),
					spdk_strerror(opts.status));
		}
		return opts.status;
	}

	if (!opts.all_devices && !opts.finished) {
		D_ERROR("Device could not be found\n");
		return -DER_NONEXIST;
	}

	/* Update timer values after action on LED state */
	switch (action) {
	case CTL__LED_ACTION__SET:
		if (*state == CTL__LED_STATE__QUICK_BLINK) {
			/**
			 * If identify state has been set, record LED start time on bdevs
			 * to start timer.
			 */
			rc = set_timer(xs_ctxt, pci_addr,
				       (duration != 0) ? d_timeus_secdiff(0) + duration : 0);
			if (rc != 0) {
				D_ERROR("Recording LED start time failed (%d)\n", rc);
				return rc;
			}
		} else {
			/* Clear LED start time to cancel any previously set timers */
			rc = set_timer(xs_ctxt, pci_addr, 0);
			if (rc != 0) {
				D_ERROR("Clearing LED start time failed (%d)\n", rc);
				return rc;
			}
		}
		break;
	case CTL__LED_ACTION__RESET:
		/* Clear LED start time on bdevs as identify state has been reset */
		rc = set_timer(xs_ctxt, pci_addr, 0);
		if (rc != 0) {
			D_ERROR("Clearing LED start time failed (%d)\n", rc);
			return rc;
		}
		break;
	default:
		break;
	}
	if (!opts.all_devices && !opts.finished) {
		D_ERROR("Device could not be found\n");
		return -DER_NONEXIST;
	}

	if (state != NULL)
		*state = opts.led_state;

	return 0;
}

static int
dev_uuid2pci_addr(struct spdk_pci_addr *pci_addr, uuid_t dev_uuid)
{
	struct bio_bdev		*d_bdev;
	struct bio_dev_info	 b_info = { 0 };
	int			 rc = 0;

	if (pci_addr == NULL)
		return -DER_INVAL;

	d_bdev = lookup_dev_by_id(dev_uuid);
	if (d_bdev == NULL) {
		D_ERROR("Failed to find dev "DF_UUID"\n", DP_UUID(dev_uuid));
		return -DER_NONEXIST;
	}

	rc = fill_in_traddr(&b_info, d_bdev->bb_name);
	if (rc) {
		D_DEBUG(DB_MGMT, "Unable to get traddr for device %s\n", d_bdev->bb_name);
		return -DER_NOSYS;
	}

	rc = spdk_pci_addr_parse(pci_addr, b_info.bdi_traddr);
	if (rc != 0) {
		D_DEBUG(DB_MGMT, "Unable to parse PCI address for device %s (%s)\n",
			b_info.bdi_traddr, spdk_strerror(-rc));
		rc = -DER_NOSYS;
	}

	D_FREE(b_info.bdi_traddr);
	return rc;
}

int
bio_led_manage(struct bio_xs_context *xs_ctxt, char *tr_addr, uuid_t dev_uuid, unsigned int action,
	       unsigned int *state, uint64_t duration)
{
	struct spdk_pci_addr	pci_addr;
	int			rc;

	/**
	 * If tr_addr is already provided, convert to a PCI address. If tr_addr is NULL or empty,
	 * derive PCI address from the provided UUID and if tr_addr is an empty string buffer then
	 * populate with the derived address.
	 */

	if ((tr_addr == NULL) || (strlen(tr_addr) == 0)) {
		rc = dev_uuid2pci_addr(&pci_addr, dev_uuid);
		if (rc != 0)
			return rc;

		if (tr_addr != NULL) {
			rc = spdk_pci_addr_fmt(tr_addr, ADDR_STR_MAX_LEN + 1, &pci_addr);
			if (rc != 0) {
				D_ERROR("Failed to write VMD's PCI address (%s)\n",
					spdk_strerror(-rc));
				return -DER_INVAL;
			}
		}
	} else {
		rc = spdk_pci_addr_parse(&pci_addr, tr_addr);
		if (rc != 0) {
			D_ERROR("Unable to parse PCI address for device %s (%s)\n", tr_addr,
				spdk_strerror(-rc));
			return -DER_INVAL;
		}
	}

	return led_manage(xs_ctxt, pci_addr, (Ctl__LedAction)action, (Ctl__LedState *)state,
			  duration);
}
