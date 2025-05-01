/**
 * (C) Copyright 2020-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <spdk/nvme.h>

#include "smd.pb-c.h"

#define LED_STATE_NAME(s) (ctl__led_state__descriptor.values[s].name)
#define LED_ACTION_NAME(a) (ctl__led_action__descriptor.values[a].name)

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

	d_bdev->bb_trigger_reint = 1;
	spdk_thread_send_msg(owner_thread(bbs), setup_bio_bdev, d_bdev);

	/**
	 * Reset the LED of the VMD device once revived, a DER_NOTSUPPORTED indicates that VMD-LED
	 * control is not enabled on device.
	 */
	rc = bio_led_manage(xs_ctxt, NULL, d_bdev->bb_uuid, (unsigned int)CTL__LED_ACTION__RESET,
			    NULL, 0);
	if ((rc != 0) && (rc != -DER_NOTSUPPORTED))
		DL_ERROR(rc, "Reset LED on device:" DF_UUID " failed", DP_UUID(d_bdev->bb_uuid));

	return 0;
}

static int
replace_dev(struct bio_xs_context *xs_ctxt, struct smd_dev_info *old_info,
	    struct bio_bdev *old_dev, struct bio_bdev *new_dev)
{
	struct bio_blobstore	*bbs = old_dev->bb_blobstore;
	unsigned int		 old_roles;
	int			 rc;

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

	old_roles = bio_nvme_configured(SMD_DEV_TYPE_META) ? old_dev->bb_roles : NVME_ROLE_DATA;
	/* Replace old device with new device in SMD */
	rc = smd_dev_replace(old_dev->bb_uuid, new_dev->bb_uuid, old_roles);
	if (rc) {
		DL_ERROR(rc, "Failed to replace dev: "DF_UUID" -> "DF_UUID". roles(%u)",
			 DP_UUID(old_dev->bb_uuid), DP_UUID(new_dev->bb_uuid), old_roles);
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
		goto out;
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
	if (rc == 0)
		ras_notify_eventf(RAS_DEVICE_REPLACE, RAS_TYPE_INFO,
				  RAS_SEV_NOTICE, NULL, NULL, NULL,
				  NULL, NULL, NULL, NULL, NULL, NULL,
				  "Replaced device: "DF_UUID" with device "DF_UUID"\n",
				  DP_UUID(old_dev_id), DP_UUID(new_dev_id));
	else
		ras_notify_eventf(RAS_DEVICE_REPLACE, RAS_TYPE_INFO,
				  RAS_SEV_ERROR, NULL, NULL, NULL,
				  NULL, NULL, NULL, NULL, NULL, NULL,
				  "Replaced device: "DF_UUID" with device: "DF_UUID" failed: %d\n",
				  DP_UUID(old_dev_id), DP_UUID(new_dev_id), rc);

	if (old_info)
		smd_dev_free_info(old_info);
	if (new_info)
		smd_dev_free_info(new_info);
	return rc;
}

static int
json_write_bdev_cb(void *cb_ctx, const void *json, size_t json_size)
{
	struct bio_dev_info *b_info = cb_ctx;

	return bio_decode_bdev_params(b_info, json, (int)json_size);
}

static int
json_find_bdev_params(struct spdk_bdev *bdev, struct bio_dev_info *b_info)
{
	struct spdk_json_write_ctx *json;
	int                         rc;
	int                         rc2;

	json = spdk_json_write_begin(json_write_bdev_cb, b_info, SPDK_JSON_WRITE_FLAG_FORMATTED);
	if (json == NULL) {
		D_ERROR("Failed to alloc SPDK json context\n");
		return -DER_NOMEM;
	}

	rc = spdk_bdev_dump_info_json(bdev, json);
	if (rc != 0)
		D_ERROR("Failed to dump config from SPDK bdev (%s)\n", spdk_strerror(-rc));

	rc2 = spdk_json_write_end(json);
	if (rc2 != 0)
		D_ERROR("Failed to write JSON (%s)\n", spdk_strerror(-rc2));

	if (rc != 0)
		return daos_errno2der(-rc);
	if (rc2 != 0)
		return daos_errno2der(-rc2);

	return rc;
}

static int
json_write_traddr_cb(void *cb_ctx, const void *data, size_t size)
{
	struct bio_dev_info *b_info = cb_ctx;
	char                *prefix = "traddr\": \"";
	char                *traddr, *end;

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
	struct spdk_json_write_ctx      *json;
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

	json = spdk_json_write_begin(json_write_traddr_cb, b_info, SPDK_JSON_WRITE_FLAG_FORMATTED);
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
	int                      tgt_cnt = 0, i;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	if (s_info != NULL) {
		tgt_cnt = s_info->sdi_tgt_cnt;
		info->bdi_flags |= NVME_DEV_FL_INUSE;
		if (s_info->sdi_state == SMD_DEV_FAULTY)
			info->bdi_flags |= NVME_DEV_FL_FAULTY;
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

struct pci_dev_opts {
	struct spdk_pci_addr pci_addr;
	bool                 finished;
	int                 *socket_id;
	char               **pci_type;
	char               **pci_cfg;
	int                  status;
};

static void
pci_device_cb(void *ctx, struct spdk_pci_device *pci_device)
{
	struct pci_dev_opts *opts = ctx;
	const char          *device_type;
	int                  len;
	int                  rc;

	if (opts->status != 0)
		return;
	if (opts->finished)
		return;

	if (spdk_pci_addr_compare(&opts->pci_addr, &pci_device->addr) != 0)
		return;
	opts->finished = true;

	/* Populate pci_dev_type and socket_id */

	*opts->socket_id = spdk_pci_device_get_socket_id(pci_device);

	device_type = spdk_pci_device_get_type(pci_device);
	if (device_type == NULL) {
		D_ERROR("spdk_pci_device_get_type returned nil\n");
		opts->status = -DER_INVAL;
		return;
	}
	len = strlen(device_type);
	if (len == 0) {
		D_ERROR("spdk_pci_device_get_type returned empty\n");
		opts->status = -DER_INVAL;
		return;
	}
	D_STRNDUP(*opts->pci_type, device_type, len);
	if (*opts->pci_type == NULL) {
		opts->status = -DER_NOMEM;
		return;
	}

	rc = spdk_pci_device_cfg_read(pci_device, *opts->pci_cfg, NVME_PCI_CFG_SPC_MAX_LEN, 0);
	if (rc != 0) {
		D_ERROR("Failed to read config space of device (%s)\n", spdk_strerror(-rc));
		opts->status = -DER_INVAL;
		return;
	}
}

static int
fetch_pci_dev_info(struct nvme_ctrlr_t *w_ctrlr, const char *tr_addr)
{
	struct pci_dev_opts  opts = {0};
	struct spdk_pci_addr pci_addr;
	int                  rc;

	rc = spdk_pci_addr_parse(&pci_addr, tr_addr);
	if (rc != 0) {
		D_ERROR("Unable to parse PCI address for device %s (%s)\n", tr_addr,
			spdk_strerror(-rc));
		return -DER_INVAL;
	}

	opts.finished  = false;
	opts.status    = 0;
	opts.pci_addr  = pci_addr;
	opts.socket_id = &w_ctrlr->socket_id;
	opts.pci_type  = &w_ctrlr->pci_type;
	opts.pci_cfg   = &w_ctrlr->pci_cfg;

	spdk_pci_for_each_device(&opts, pci_device_cb);

	return opts.status;
}

static int
alloc_ctrlr_info(uuid_t dev_id, char *dev_name, struct bio_dev_info *b_info)
{
	struct spdk_bdev *bdev;
	uint32_t          blk_sz;
	uint64_t          nr_blks;
	int               rc;

	D_ASSERT(b_info != NULL);
	D_ASSERT(b_info->bdi_ctrlr == NULL);

	if (dev_name == NULL) {
		D_DEBUG(DB_MGMT,
			"missing bdev device name for device " DF_UUID ", skipping ctrlr "
			"info fetch\n",
			DP_UUID(dev_id));
		return 0;
	}

	bdev = spdk_bdev_get_by_name(dev_name);
	if (bdev == NULL) {
		D_ERROR("Failed to get SPDK bdev for %s\n", dev_name);
		return -DER_NONEXIST;
	}

	if (get_bdev_type(bdev) != BDEV_CLASS_NVME)
		return 0;

	D_ALLOC_PTR(b_info->bdi_ctrlr);
	if (b_info->bdi_ctrlr == NULL)
		return -DER_NOMEM;

	D_ALLOC_PTR(b_info->bdi_ctrlr->nss);
	if (b_info->bdi_ctrlr->nss == NULL)
		return -DER_NOMEM;

	D_ALLOC(b_info->bdi_ctrlr->pci_cfg, NVME_PCI_CFG_SPC_MAX_LEN);
	if (b_info->bdi_ctrlr->pci_cfg == NULL)
		return -DER_NOMEM;

	/* Namespace capacity by direct query of SPDK bdev object */
	blk_sz                       = spdk_bdev_get_block_size(bdev);
	nr_blks                      = spdk_bdev_get_num_blocks(bdev);
	b_info->bdi_ctrlr->nss->size = nr_blks * (uint64_t)blk_sz;

	/* Controller details and namespace ID by parsing SPDK bdev JSON info */
	rc = json_find_bdev_params(bdev, b_info);
	if (rc != 0) {
		D_ERROR("Failed to get bdev json params for %s\n", dev_name);
		return rc;
	}

	/* Fetch PCI details by enumerating spdk_pci_device list */
	return fetch_pci_dev_info(b_info->bdi_ctrlr, b_info->bdi_traddr);
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

		rc = alloc_ctrlr_info(d_bdev->bb_uuid, dev_name, b_info);
		if (rc) {
			DL_ERROR(rc, "Failed to get ctrlr details");
			bio_free_dev_info(b_info);
			goto out;
		}

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
	 */
	d_list_for_each_entry(s_info, &s_dev_list, sdi_link) {
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

struct led_opts {
	struct spdk_pci_addr pci_addr;
	bool                 all_devices;
	bool                 finished;
	Ctl__LedAction       action;
	Ctl__LedState        led_state;
	int                  status;
};

static Ctl__LedState
led_state_spdk2daos(enum spdk_vmd_led_state in)
{
	switch (in) {
	case SPDK_VMD_LED_STATE_OFF:
		return CTL__LED_STATE__OFF;
	case SPDK_VMD_LED_STATE_IDENTIFY:
		return CTL__LED_STATE__QUICK_BLINK;
	case SPDK_VMD_LED_STATE_FAULT:
		return CTL__LED_STATE__ON;
	case SPDK_VMD_LED_STATE_REBUILD:
		return CTL__LED_STATE__SLOW_BLINK;
	default:
		return CTL__LED_STATE__NA;
	}
}

static enum spdk_vmd_led_state
led_state_daos2spdk(Ctl__LedState in)
{
	switch (in) {
	case CTL__LED_STATE__OFF:
		return SPDK_VMD_LED_STATE_OFF;
	case CTL__LED_STATE__QUICK_BLINK:
		return SPDK_VMD_LED_STATE_IDENTIFY;
	case CTL__LED_STATE__ON:
		return SPDK_VMD_LED_STATE_FAULT;
	case CTL__LED_STATE__SLOW_BLINK:
		return SPDK_VMD_LED_STATE_REBUILD;
	default:
		return SPDK_VMD_LED_STATE_UNKNOWN;
	}
}

static void
led_device_action(void *ctx, struct spdk_pci_device *pci_device)
{
	struct led_opts		*opts = ctx;
	enum spdk_vmd_led_state	 cur_led_state;
	Ctl__LedState            d_led_state;
	const char		*pci_dev_type = NULL;
	char			 addr_buf[ADDR_STR_MAX_LEN + 1];
	int			 rc;

	if (opts->status != 0)
		return;
	if (opts->finished)
		return;

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

	pci_dev_type = spdk_pci_device_get_type(pci_device);
	if (pci_dev_type == NULL) {
		D_ERROR("nil pci device type returned\n");
		opts->status = -DER_MISC;
		return;
	}

	if (strncmp(pci_dev_type, NVME_PCI_DEV_TYPE_VMD, strlen(NVME_PCI_DEV_TYPE_VMD)) != 0) {
		D_DEBUG(DB_MGMT, "Found non-VMD device type (%s:%s), can't manage LED\n",
			pci_dev_type, addr_buf);
		opts->status = -DER_NOTSUPPORTED;
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

	/* Convert state to Ctl__LedState from SPDK led_state */
	d_led_state = led_state_spdk2daos(cur_led_state);

	D_DEBUG(DB_MGMT, "led on dev %s has state: %s (action: %s, new state: %s)\n", addr_buf,
		LED_STATE_NAME(d_led_state), LED_ACTION_NAME(opts->action),
		LED_STATE_NAME(opts->led_state));

	switch (opts->action) {
	case CTL__LED_ACTION__GET:
		/* Return early with current device state set */
		opts->led_state = d_led_state;
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

	if (d_led_state == opts->led_state) {
		D_DEBUG(DB_MGMT, "VMD device %s LED state already in state %s\n", addr_buf,
			LED_STATE_NAME(opts->led_state));
		return;
	}

	/* Set the LED to the new state */
	rc = spdk_vmd_set_led_state(pci_device, led_state_daos2spdk(opts->led_state));
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
	d_led_state = led_state_spdk2daos(cur_led_state);

	/* Verify the correct state is set */
	if (d_led_state != opts->led_state) {
		D_ERROR("Unexpected LED state on %s, want %s got %s\n", addr_buf,
			LED_STATE_NAME(opts->led_state), LED_STATE_NAME(d_led_state));
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
		} else if (strcmp(dev_info->bdi_traddr, tr_addr) == 0) {
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
		if (opts.status != -DER_NOTSUPPORTED) {
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
	if (rc || b_info.bdi_traddr == NULL) {
		D_DEBUG(DB_MGMT, "Unable to get traddr for device %s\n", d_bdev->bb_name);
		return -DER_INVAL;
	}

	rc = spdk_pci_addr_parse(pci_addr, b_info.bdi_traddr);
	if (rc != 0) {
		D_DEBUG(DB_MGMT, "Unable to parse PCI address for device %s (%s)\n",
			b_info.bdi_traddr, spdk_strerror(-rc));
		rc = -DER_INVAL;
	}

	D_FREE(b_info.bdi_traddr);
	return rc;
}

int
bio_led_manage(struct bio_xs_context *xs_ctxt, char *tr_addr, uuid_t dev_uuid, unsigned int action,
	       unsigned int *state, uint64_t duration)
{
	struct spdk_pci_addr	pci_addr;
	int                     addr_len = 0;
	int			rc;

	/* LED management on NVMe devices currently only supported when VMD is enabled. */
	if (!bio_vmd_enabled)
		return -DER_NOTSUPPORTED;

	/**
	 * If tr_addr is already provided, convert to a PCI address. If tr_addr is NULL or empty,
	 * derive PCI address from the provided UUID and if tr_addr is an empty string buffer then
	 * populate with the derived address.
	 */

	if (tr_addr != NULL) {
		addr_len = strnlen(tr_addr, SPDK_NVMF_TRADDR_MAX_LEN + 1);
		if (addr_len == SPDK_NVMF_TRADDR_MAX_LEN + 1)
			return -DER_INVAL;
	}

	if (addr_len == 0) {
		rc = dev_uuid2pci_addr(&pci_addr, dev_uuid);
		if (rc != 0) {
			DL_ERROR(rc, "Failed to read PCI addr from dev UUID");
			return rc;
		}

		if (tr_addr != NULL) {
			/* Populate tr_addr buffer to return address */
			rc = spdk_pci_addr_fmt(tr_addr, addr_len, &pci_addr);
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
