/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <spdk/blob.h>
#include <spdk/thread.h>
#include "bio_internal.h"
#include <daos_srv/smd.h>
#include <spdk/env.h>
#include <spdk/vmd.h>

static int
revive_dev(struct bio_xs_context *xs_ctxt, struct bio_bdev *d_bdev)
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
	D_ASSERT(bbs->bb_state == BIO_BS_STATE_OUT);
	D_ASSERT(owner_thread(bbs) != NULL);

	spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, d_bdev);

	/* Set the LED of the VMD device to OFF state */
	rc = bio_set_led_state(xs_ctxt, d_bdev->bb_uuid, "off", false/*reset*/);
	if (rc != 0)
		D_CDEBUG(rc == -DER_NOSYS, DB_MGMT, DLOG_ERR,
			 "Set LED on device:"DF_UUID" failed, "DF_RC"\n",
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
	DABT_EVENTUAL_SET(boa->boa_eventual, NULL, 0);
	if (rc)
		D_ERROR("Create blob failed. %d\n", rc);
}

static void
blob_delete_cp(void *cb_arg, int rc)
{
	struct blob_ops_arg	*boa = cb_arg;

	boa->boa_rc = daos_errno2der(-rc);
	DABT_EVENTUAL_SET(boa->boa_eventual, NULL, 0);
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

	DABT_EVENTUAL_WAIT(boa.boa_eventual, NULL);

	rc = boa.boa_rc;
	if (rc)
		D_ERROR("Create blob failed. "DF_RC"\n", DP_RC(rc));
	else
		*blob_id = boa.boa_blob_id;
	DABT_EVENTUAL_FREE(&boa.boa_eventual);
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

	DABT_EVENTUAL_WAIT(boa.boa_eventual, NULL);

	rc = boa.boa_rc;
	if (rc)
		D_ERROR("Delete blob("DF_U64") failed. "DF_RC"\n",
			blob_id, DP_RC(rc));
	DABT_EVENTUAL_FREE(&boa.boa_eventual);
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
	new_dev->bb_replacing = true;

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
pool_list_out:
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
	if (rc) {
		D_ERROR("Failed to dump config from SPDK bdev. %d\n", rc);
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
		if (rc) {
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

int
bio_set_led_state(struct bio_xs_context *xs_ctxt, uuid_t dev_uuid,
		  const char *led_state, bool reset)
{
	struct spdk_pci_addr	pci_addr;
	struct spdk_pci_device *pci_device;
	struct bio_bdev	       *bio_dev;
	struct bio_dev_info	b_info = { 0 };
	enum spdk_vmd_led_state current_led_state;
	int			new_led_state;
	int			rc = 0;
	bool			found = false;

	D_ASSERT(is_init_xstream(xs_ctxt));

	bio_dev = lookup_dev_by_id(dev_uuid);
	if (bio_dev == NULL) {
		D_ERROR("Failed to find dev "DF_UUID"\n",
			DP_UUID(dev_uuid));
		return -DER_NONEXIST;
	}

	/* LED will be reset to the original saved state */
	if (reset) {
		new_led_state = bio_dev->bb_led_state;
		D_GOTO(skip_led_str, rc = 0);
	}

	if (led_state == NULL)
		return -DER_INVAL;

	/* Determine SPDK LED state based on led_state string */
	if (strcasecmp(led_state, "identify") == 0) {
		new_led_state = SPDK_VMD_LED_STATE_IDENTIFY;
	} else if (strcasecmp(led_state, "on") == 0) {
		new_led_state = SPDK_VMD_LED_STATE_FAULT;
	} else if (strcasecmp(led_state, "fault") == 0) {
		new_led_state = SPDK_VMD_LED_STATE_FAULT;
	} else if (strcasecmp(led_state, "off") == 0) {
		new_led_state = SPDK_VMD_LED_STATE_OFF;
	} else {
		D_ERROR("LED state is not valid or supported\n");
		return -DER_NOSYS;
	}

skip_led_str:
	rc = fill_in_traddr(&b_info, bio_dev->bb_name);
	if (rc) {
		D_ERROR("Unable to get traddr for device:%s\n",
			bio_dev->bb_name);
		return -DER_INVAL;
	}


	if (spdk_pci_addr_parse(&pci_addr, b_info.bdi_traddr)) {
		D_ERROR("Unable to parse PCI address: %s\n", b_info.bdi_traddr);
		D_GOTO(free_traddr, rc = -DER_INVAL);
	}

	for (pci_device = spdk_pci_get_first_device(); pci_device != NULL;
	     pci_device = spdk_pci_get_next_device(pci_device)) {
		if (spdk_pci_addr_compare(&pci_addr, &pci_device->addr) == 0) {
			found = true;
			break;
		}
	}

	if (found) {
		if (strcmp(spdk_pci_device_get_type(pci_device), "vmd") != 0) {
			D_DEBUG(DB_MGMT, "%s is not a VMD device\n",
				b_info.bdi_traddr);
			D_GOTO(free_traddr, rc = -DER_NOSYS);
		}
	} else {
		D_ERROR("Unable to set led state, VMD device not found\n");
		D_GOTO(free_traddr, rc = -DER_INVAL);
	}

	/* First check the current state of the VMD LED */
	rc = spdk_vmd_get_led_state(pci_device, &current_led_state);
	if (rc) {
		D_ERROR("Failed to get the VMD LED state\n");
		D_GOTO(free_traddr, rc = -DER_INVAL);
	}

	/* If the current state of a device is FAULTY we do not want to reset */
	if ((current_led_state == SPDK_VMD_LED_STATE_FAULT) && reset)
		D_GOTO(state_set, rc);

	if (!reset)
		D_DEBUG(DB_MGMT, "Setting VMD device:%s LED state to %s(%d)\n",
			b_info.bdi_traddr, led_state, new_led_state);
	else
		D_DEBUG(DB_MGMT, "Resetting VMD device:%s LED state to %d\n",
			b_info.bdi_traddr, bio_dev->bb_led_state);

	/* Save the current state in bio_bdev, will be restored by init xs */
	if (!reset)
		bio_dev->bb_led_state = current_led_state;

	if (current_led_state == new_led_state)
		D_GOTO(state_set, rc);

	/* Set the LED to the new state */
	rc = spdk_vmd_set_led_state(pci_device, new_led_state);
	if (rc) {
		D_ERROR("Failed to set LED state to %s\n", led_state);
		D_GOTO(free_traddr, rc = -DER_INVAL);
	}

	rc = spdk_vmd_get_led_state(pci_device, &current_led_state);
	if (rc) {
		D_ERROR("Failed to get the VMD LED state\n");
	} else {
		/* Verify the correct state is set */
		if (current_led_state != new_led_state)
			D_ERROR("LED of device:%s is in an unexpected state:"
				"%d\n", b_info.bdi_traddr, current_led_state);
	}

state_set:
	if (!reset && (current_led_state != SPDK_VMD_LED_STATE_OFF)) {
		/*
		 * Init the start time for the LED for a new event.
		 */
		bio_dev->bb_led_start_time = d_timeus_secdiff(0);
	} else {
		/*
		 * Reset the LED start time to indicate a LED event has
		 * completed.
		 */
		bio_dev->bb_led_start_time = 0;
	}

free_traddr:
	if (b_info.bdi_traddr != NULL)
		D_FREE(b_info.bdi_traddr);

	return rc;
}
