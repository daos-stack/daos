/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * ds_mgmt: Storage Query Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/bio.h>
#include <daos_srv/smd.h>

#include "srv_internal.h"


struct bs_state_query_arg {
	int			bs_arg_state;
	uuid_t			bs_arg_uuid;
};

static void
bs_state_query(void *arg)
{
	struct dss_module_info		*info = dss_get_module_info();
	struct bio_xs_context		*bxc;
	struct bs_state_query_arg	*bs_arg = arg;
	int				 rc;

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO blobstore state query on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return;
	}

	rc = bio_get_bs_state(&bs_arg->bs_arg_state, bs_arg->bs_arg_uuid, bxc);
	if (rc)
		D_ERROR("Blobstore query on dev:"DF_UUID" failed. "DF_RC"\n",
			DP_UUID(bs_arg->bs_arg_uuid), DP_RC(rc));
}

static inline enum dss_xs_type
init_xs_type()
{
	return bio_nvme_configured(SMD_DEV_TYPE_META) ?  DSS_XS_SYS : DSS_XS_VOS;
}

static inline int
tgt2xs_type(int tgt_id)
{
	return tgt_id == BIO_SYS_TGT_ID ? DSS_XS_SYS : DSS_XS_VOS;
}

/*
 * CaRT RPC handler for management service "get blobstore state" (C API)
 *
 * Internal blobstore states returned for test validation only.
 */
int ds_mgmt_get_bs_state(uuid_t bs_uuid, int *bs_state)
{
	struct smd_dev_info		*dev_info;
	ABT_thread			 thread;
	int				 tgt_id;
	int				 rc;
	struct bs_state_query_arg	 bs_arg;

	/*
	 * Query per-server metadata (SMD) to get target ID(s) for given device.
	 */
	if (!uuid_is_null(bs_uuid)) {
		rc = smd_dev_get_by_id(bs_uuid, &dev_info);
		if (rc != 0) {
			D_ERROR("Blobstore UUID:"DF_UUID" not found\n",
				DP_UUID(bs_uuid));
			return rc;
		}
		if (dev_info->sdi_tgts == NULL) {
			D_ERROR("No targets mapped to device\n");
			rc = -DER_NONEXIST;
			goto out;
		}
		/* Default tgt_id is the first mapped tgt */
		tgt_id = dev_info->sdi_tgts[0];
	} else {
		D_ERROR("Blobstore UUID is not provided for state query\n");
		return -DER_INVAL;
	}

	/* Create a ULT on the tgt_id */
	D_DEBUG(DB_MGMT, "Starting ULT on tgt_id:%d\n", tgt_id);
	uuid_copy(bs_arg.bs_arg_uuid, bs_uuid);
	*bs_state = -1;
	rc = dss_ult_create(bs_state_query, (void *)&bs_arg, tgt2xs_type(tgt_id),
			    tgt_id, 0, &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);
	/* Set 'bs_state' after state query ULT executed */
	*bs_state = bs_arg.bs_arg_state;
out:
	smd_dev_free_info(dev_info);
	return rc;
}

void
ds_mgmt_hdlr_get_bs_state(crt_rpc_t *rpc_req)
{
	struct mgmt_get_bs_state_in	*bs_in;
	struct mgmt_get_bs_state_out	*bs_out;
	uuid_t				 bs_uuid;
	int				 bs_state;
	int				 rc;


	bs_in = crt_req_get(rpc_req);
	D_ASSERT(bs_in != NULL);
	bs_out = crt_reply_get(rpc_req);
	D_ASSERT(bs_out != NULL);

	uuid_copy(bs_uuid, bs_in->bs_uuid);

	rc = ds_mgmt_get_bs_state(bs_uuid, &bs_state);

	uuid_copy(bs_out->bs_uuid, bs_uuid);
	bs_out->bs_state = bs_state;
	bs_out->bs_rc = rc;

	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: "DF_RC"\n", DP_RC(rc));
}

static void
bio_health_query(void *arg)
{
	struct mgmt_bio_health	*mbh = arg;
	struct dss_module_info	*info = dss_get_module_info();
	struct bio_xs_context	*bxc;
	int			 rc;

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO health stats query on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return;
	}

	rc = bio_get_dev_state(&mbh->mb_dev_state, mbh->mb_devid,
			       bxc, mbh->mb_meta_size, mbh->mb_rdb_size);
	if (rc != 0) {
		D_ERROR("Error getting BIO device state\n");
		return;
	}
}

int
ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t dev_uuid)
{
	struct smd_dev_info	*dev_info;
	ABT_thread		 thread;
	int			 tgt_id;
	int			 rc = 0;

	if (uuid_is_null(dev_uuid)) {
		D_ERROR("dev_uuid is required for BIO query\n");
		return -DER_INVAL;
	}

	/*
	 * Query per-server metadata (SMD) to get either target ID(s) for given
	 * device.
	 */
	rc = smd_dev_get_by_id(dev_uuid, &dev_info);
	if (rc != 0) {
		D_ERROR("Device UUID:"DF_UUID" not found\n",
			DP_UUID(dev_uuid));
		return rc;
	}
	if (dev_info->sdi_tgts == NULL) {
		D_ERROR("No targets mapped to device\n");
		rc = -DER_NONEXIST;
		goto out;
	}
	/* Use the first mapped tgt */
	tgt_id = dev_info->sdi_tgts[0];

	D_DEBUG(DB_MGMT, "Querying BIO Health Data for dev:"DF_UUID"\n",
		DP_UUID(dev_uuid));
	uuid_copy(mbh->mb_devid, dev_uuid);

	/* Create a ULT on the tgt_id */
	D_DEBUG(DB_MGMT, "Starting ULT on tgt_id:%d\n", tgt_id);
	rc = dss_ult_create(bio_health_query, mbh, tgt2xs_type(tgt_id), tgt_id, 0,
			    &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

out:
	smd_dev_free_info(dev_info);
	return rc;
}

struct bio_led_manage_info {
	uuid_t		 dev_uuid;
	char		*tr_addr;
	Ctl__LedAction	 action;
	Ctl__LedState	*state;
	uint64_t	 duration;
};

static int
bio_storage_dev_manage_led(void *arg)
{
	struct bio_led_manage_info *led_info = arg;
	struct dss_module_info	   *mod_info = dss_get_module_info();
	struct bio_xs_context	   *bxc = NULL;
	int			    rc = 0;

	D_ASSERT(mod_info != NULL);
	D_ASSERT(led_info != NULL);
	D_ASSERT(led_info->state != NULL);

	bxc = mod_info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			mod_info->dmi_xs_id, mod_info->dmi_tgt_id);
		return -DER_INVAL;
	}

	/* Set the LED of the VMD device to a FAULT state, tr_addr and state may be updated */
	rc = bio_led_manage(bxc, led_info->tr_addr, led_info->dev_uuid,
			    (unsigned int)led_info->action, (unsigned int *)led_info->state,
			    led_info->duration);
	if ((rc != 0) && (rc != -DER_NOSYS))
		D_ERROR("bio_led_manage failed on device:"DF_UUID" (action: %s, state %s): "
			DF_RC"\n", DP_UUID(led_info->dev_uuid),
			ctl__led_action__descriptor.values[led_info->action].name,
			ctl__led_state__descriptor.values[*led_info->state].name,
			DP_RC(rc));

	return rc;
}

struct bio_list_devs_info {
	d_list_t	dev_list;
	int		dev_list_cnt;
	uuid_t		devid;
	int		*state;
};

static int
bio_query_dev_list(void *arg)
{
	struct bio_list_devs_info	*list_devs_info = arg;
	struct dss_module_info		*info = dss_get_module_info();
	struct bio_xs_context		*bxc;
	int				 rc;

	D_ASSERT(info != NULL);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return -DER_INVAL;
	}

	rc = bio_dev_list(bxc, &list_devs_info->dev_list,
			  &list_devs_info->dev_list_cnt);
	if (rc != 0) {
		D_ERROR("Error getting BIO device list\n");
		return rc;
	}

	return 0;
}

int
ds_mgmt_smd_list_devs(Ctl__SmdDevResp *resp)
{
	struct bio_dev_info		*dev_info = NULL, *tmp;
	struct bio_list_devs_info	 list_devs_info = { 0 };
	struct bio_led_manage_info	 led_info = { 0 };
	Ctl__LedState		 led_state;
	int				 buflen;
	int				 rc = 0;
	int				 i = 0, j;

	D_DEBUG(DB_MGMT, "Querying BIO & SMD device list\n");

	D_INIT_LIST_HEAD(&list_devs_info.dev_list);

	rc = dss_ult_execute(bio_query_dev_list, &list_devs_info, NULL, NULL,
			     init_xs_type(), 0, 0);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT\n");
		goto out;
	}

	D_ALLOC_ARRAY(resp->devices, list_devs_info.dev_list_cnt);
	if (resp->devices == NULL)
		return -DER_NOMEM;

	d_list_for_each_entry_safe(dev_info, tmp, &list_devs_info.dev_list, bdi_link) {
		D_ALLOC_PTR(resp->devices[i]);
		if (resp->devices[i] == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		ctl__smd_device__init(resp->devices[i]);
		/*
		 * XXX: These fields are initialized as "empty string" by above
		 * protobuf auto-generated function, to avoid error cleanup
		 * code mistakenly free the "empty string", let's reset them as
		 * NULL.
		 */
		resp->devices[i]->uuid = NULL;
		resp->devices[i]->tr_addr = NULL;
		resp->devices[i]->tgt_ids = NULL;
		resp->devices[i]->led_state = CTL__LED_STATE__NA;

		D_ALLOC(resp->devices[i]->uuid, DAOS_UUID_STR_SIZE);
		if (resp->devices[i]->uuid == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		uuid_unparse_lower(dev_info->bdi_dev_id, resp->devices[i]->uuid);

		if (dev_info->bdi_traddr != NULL) {
			buflen = strlen(dev_info->bdi_traddr) + 1;
			D_ALLOC(resp->devices[i]->tr_addr, buflen);
			if (resp->devices[i]->tr_addr == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			/* Transport Addr -> Blobstore UUID mapping */
			strncpy(resp->devices[i]->tr_addr, dev_info->bdi_traddr, buflen);
		}

		if ((dev_info->bdi_flags & NVME_DEV_FL_PLUGGED) == 0) {
			resp->devices[i]->dev_state = CTL__NVME_DEV_STATE__UNPLUGGED;
			goto skip_dev;
		}

		if ((dev_info->bdi_flags & NVME_DEV_FL_FAULTY) != 0)
			resp->devices[i]->dev_state = CTL__NVME_DEV_STATE__EVICTED;
		else if ((dev_info->bdi_flags & NVME_DEV_FL_INUSE) == 0)
			resp->devices[i]->dev_state = CTL__NVME_DEV_STATE__NEW;
		else
			resp->devices[i]->dev_state = CTL__NVME_DEV_STATE__NORMAL;

		resp->devices[i]->role_bits = dev_info->bdi_dev_roles;

		/* Fetch LED State if device is plugged */
		uuid_copy(led_info.dev_uuid, dev_info->bdi_dev_id);
		led_info.action = CTL__LED_ACTION__GET;
		led_state = CTL__LED_STATE__NA;
		led_info.state = &led_state;
		led_info.duration = 0;
		rc = dss_ult_execute(bio_storage_dev_manage_led, &led_info, NULL, NULL,
				     init_xs_type(),
				     0, 0);
		if (rc != 0) {
			if (rc == -DER_NOSYS) {
				led_state = CTL__LED_STATE__NA;
				/* Reset rc for non-VMD case */
				rc = 0;
			} else {
				break;
			}
		}
		resp->devices[i]->led_state = led_state;

		resp->devices[i]->n_tgt_ids = dev_info->bdi_tgt_cnt;
		D_ALLOC(resp->devices[i]->tgt_ids, sizeof(int) * dev_info->bdi_tgt_cnt);
		if (resp->devices[i]->tgt_ids == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < dev_info->bdi_tgt_cnt; j++)
			resp->devices[i]->tgt_ids[j] = dev_info->bdi_tgts[j];
skip_dev:
		d_list_del(&dev_info->bdi_link);
		/* Frees sdi_tgts and dev_info */
		bio_free_dev_info(dev_info);
		dev_info = NULL;

		i++;
	}

	/* Free all devices if there was an error allocating any */
	if (rc != 0) {
		d_list_for_each_entry_safe(dev_info, tmp, &list_devs_info.dev_list, bdi_link) {
			d_list_del(&dev_info->bdi_link);
			bio_free_dev_info(dev_info);
		}
		for (; i >= 0; i--) {
			if (resp->devices[i] != NULL) {
				if (resp->devices[i]->uuid != NULL)
					D_FREE(resp->devices[i]->uuid);
				if (resp->devices[i]->tgt_ids != NULL)
					D_FREE(resp->devices[i]->tgt_ids);
				if (resp->devices[i]->tr_addr != NULL)
					D_FREE(resp->devices[i]->tr_addr);
				D_FREE(resp->devices[i]);
			}
		}
		D_FREE(resp->devices);
		resp->devices = NULL;
		resp->n_devices = 0;
		goto out;
	}
	resp->n_devices = list_devs_info.dev_list_cnt;

out:
	return rc;
}

int
ds_mgmt_smd_list_pools(Ctl__SmdPoolResp *resp)
{
	struct smd_pool_info	*pool_info = NULL, *tmp;
	d_list_t		 pool_list;
	int			 pool_list_cnt = 0;
	int			 i = 0, j;
	int			 rc = 0;

	D_DEBUG(DB_MGMT, "Querying SMD pool list\n");

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_list_cnt);
	if (rc != 0) {
		D_ERROR("Failed to get all VOS pools from SMD\n");
		return rc;
	}

	D_ALLOC_ARRAY(resp->pools, pool_list_cnt);
	if (resp->pools == NULL)
		return -DER_NOMEM;

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		D_ALLOC_PTR(resp->pools[i]);
		if (resp->pools[i] == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		ctl__smd_pool_resp__pool__init(resp->pools[i]);
		/* See "empty string" comments in ds_mgmt_smd_list_devs() */
		resp->pools[i]->uuid = NULL;

		D_ALLOC(resp->pools[i]->uuid, DAOS_UUID_STR_SIZE);
		if (resp->pools[i]->uuid == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		uuid_unparse_lower(pool_info->spi_id, resp->pools[i]->uuid);

		resp->pools[i]->n_tgt_ids = pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA];
		D_ALLOC(resp->pools[i]->tgt_ids,
			sizeof(int) * pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA]);
		if (resp->pools[i]->tgt_ids == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA]; j++)
			resp->pools[i]->tgt_ids[j] = pool_info->spi_tgts[SMD_DEV_TYPE_DATA][j];

		resp->pools[i]->n_blobs = pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA];
		D_ALLOC(resp->pools[i]->blobs,
			sizeof(uint64_t) * pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA]);
		if (resp->pools[i]->blobs == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < pool_info->spi_tgt_cnt[SMD_DEV_TYPE_DATA]; j++)
			resp->pools[i]->blobs[j] = pool_info->spi_blobs[SMD_DEV_TYPE_DATA][j];


		d_list_del(&pool_info->spi_link);
		/* Frees spi_tgts, spi_blobs, and pool_info */
		smd_pool_free_info(pool_info);
		pool_info = NULL;

		i++;
	}

	/* Free all pools if there was an error allocating any */
	if (rc != 0) {
		d_list_for_each_entry_safe(pool_info, tmp, &pool_list,
					   spi_link) {
			d_list_del(&pool_info->spi_link);
			smd_pool_free_info(pool_info);
		}
		for (; i >= 0; i--) {
			if (resp->pools[i] != NULL) {
				if (resp->pools[i]->uuid != NULL)
					D_FREE(resp->pools[i]->uuid);
				if (resp->pools[i]->tgt_ids != NULL)
					D_FREE(resp->pools[i]->tgt_ids);
				if (resp->pools[i]->blobs != NULL)
					D_FREE(resp->pools[i]->blobs);
				D_FREE(resp->pools[i]);
			}
		}
		D_FREE(resp->pools);
		resp->n_pools = 0;
		goto out;
	}
	resp->n_pools = pool_list_cnt;

out:
	return rc;
}

struct bio_faulty_dev_info {
	uuid_t	bf_dev_uuid;
};

static void
bio_faulty_state_set(void *arg)
{
	struct dss_module_info		*info = dss_get_module_info();
	struct bio_xs_context		*bxc;
	struct bio_faulty_dev_info	*bfdi = arg;
	int				 rc;

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO health state set on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return;
	}

	rc = bio_dev_set_faulty(bxc, bfdi->bf_dev_uuid);
	if (rc != 0) {
		D_ERROR("Error setting FAULTY BIO device state\n");
		return;
	}
}

int
ds_mgmt_dev_set_faulty(uuid_t dev_uuid, Ctl__DevManageResp *resp)
{
	struct bio_led_manage_info	 led_info = { 0 };
	struct bio_faulty_dev_info	 faulty_info = { 0 };
	struct smd_dev_info		*dev_info;
	ABT_thread			 thread;
	Ctl__LedState			 led_state;
	int				 tgt_id;
	int				 rc = 0;

	if (uuid_is_null(dev_uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Setting FAULTY SMD device state for dev:"DF_UUID"\n",
		DP_UUID(dev_uuid));

	/*
	 * Query per-server metadata (SMD) to get NVMe device info for given
	 * device UUID.
	 */
	rc = smd_dev_get_by_id(dev_uuid, &dev_info);
	if (rc != 0) {
		D_ERROR("Device UUID:"DF_UUID" not found\n", DP_UUID(dev_uuid));
		return rc;
	}

	if (dev_info->sdi_tgts == NULL) {
		D_ERROR("No targets mapped to device\n");
		D_GOTO(out, rc = -DER_NONEXIST);
	}
	/* Default tgt_id is the first mapped tgt */
	tgt_id = dev_info->sdi_tgts[0];

	uuid_copy(faulty_info.bf_dev_uuid, dev_uuid);
	/* Create a ULT on the tgt_id */
	D_DEBUG(DB_MGMT, "Starting ULT on tgt_id:%d\n", tgt_id);
	rc = dss_ult_create(bio_faulty_state_set, (void *)&faulty_info,
			    tgt2xs_type(tgt_id), tgt_id, 0, &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

	dev_info->sdi_state = SMD_DEV_FAULTY;

	D_ALLOC_PTR(resp->device);
	if (resp->device == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	ctl__smd_device__init(resp->device);
	resp->device->uuid = NULL;
	resp->device->dev_state = CTL__NVME_DEV_STATE__EVICTED;

	D_ALLOC(resp->device->uuid, DAOS_UUID_STR_SIZE);
	if (resp->device->uuid == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}
	uuid_unparse_lower(dev_uuid, resp->device->uuid);

	uuid_copy(led_info.dev_uuid, dev_uuid);
	led_info.action = CTL__LED_ACTION__SET;
	led_state = CTL__LED_STATE__ON;
	led_info.state = &led_state;
	/* Indicate infinite duration */
	led_info.duration = 0;

	/* Set the VMD LED to FAULTY state on init xstream */
	rc = dss_ult_execute(bio_storage_dev_manage_led, &led_info, NULL, NULL,
			     init_xs_type(), 0, 0);
	if (rc != 0) {
		D_ERROR("FAULT LED state not set on device:"DF_UUID"\n", DP_UUID(dev_uuid));
		if (rc == -DER_NOSYS) {
			led_state = CTL__LED_STATE__NA;
			/* Reset rc for non-VMD case */
			rc = 0;
		} else {
			goto out;
		}
	}
	resp->device->led_state = led_state;

out:
	smd_dev_free_info(dev_info);

	return rc;
}

int
ds_mgmt_dev_manage_led(Ctl__LedManageReq *req, Ctl__DevManageResp *resp)
{
	struct bio_led_manage_info	led_info = { 0 };
	Ctl__LedState			led_state;
	int				rc = 0;

	D_ASSERT(req->ids != NULL);

	D_ALLOC_PTR(resp->device);
	if (resp->device == NULL) {
		return -DER_NOMEM;
	}
	ctl__smd_device__init(resp->device);
	resp->device->uuid = NULL;
	resp->device->tr_addr = NULL;

	D_ALLOC(resp->device->tr_addr, ADDR_STR_MAX_LEN + 1);
	if (resp->device->tr_addr == NULL)
		return -DER_NOMEM;

	if (strlen(req->ids) == 0) {
		D_ERROR("Transport address not provided in request\n");
		return -DER_INVAL;
	}

	strncpy(resp->device->tr_addr, req->ids, ADDR_STR_MAX_LEN + 1);

	/* tr_addr will be used if set and get populated if not */
	led_info.tr_addr = resp->device->tr_addr;
	led_info.action = req->led_action;
	led_state = req->led_state;
	led_info.state = &led_state;
	led_info.duration = req->led_duration_mins * 60 * (NSEC_PER_SEC / NSEC_PER_USEC);

	/* Manage the VMD LED state on init xstream */
	rc = dss_ult_execute(bio_storage_dev_manage_led, &led_info, NULL, NULL,
			     init_xs_type(), 0, 0);
	if (rc != 0) {
		if (rc == -DER_NOSYS) {
			resp->device->led_state = CTL__LED_STATE__NA;
			/* Reset rc for non-VMD case */
			rc = 0;
		}
	} else {
		resp->device->led_state = (Ctl__LedState)led_state;
	}

	return rc;
}

struct bio_replace_dev_info {
	uuid_t		old_dev;
	uuid_t		new_dev;
};

static int
bio_storage_dev_replace(void *arg)
{
	struct bio_replace_dev_info	*replace_dev_info = arg;
	struct dss_module_info		*info = dss_get_module_info();
	struct bio_xs_context		*bxc;
	int				 rc;

	D_ASSERT(info != NULL);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return -DER_INVAL;
	}

	rc = bio_replace_dev(bxc, replace_dev_info->old_dev,
			     replace_dev_info->new_dev);
	if (rc != 0) {
		D_ERROR("Error replacing BIO device\n");
		return rc;
	}

	return 0;
}

int
ds_mgmt_dev_replace(uuid_t old_dev_uuid, uuid_t new_dev_uuid, Ctl__DevManageResp *resp)
{
	struct bio_replace_dev_info	 replace_dev_info = { 0 };
	int				 rc = 0;

	if (uuid_is_null(old_dev_uuid))
		return -DER_INVAL;
	if (uuid_is_null(new_dev_uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Replacing device:"DF_UUID" with device:"DF_UUID"\n",
		DP_UUID(old_dev_uuid), DP_UUID(new_dev_uuid));

	D_ALLOC(resp->device->uuid, DAOS_UUID_STR_SIZE);
	if (resp->device->uuid == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	uuid_unparse_lower(new_dev_uuid, resp->device->uuid);

	uuid_copy(replace_dev_info.old_dev, old_dev_uuid);
	uuid_copy(replace_dev_info.new_dev, new_dev_uuid);
	rc = dss_ult_execute(bio_storage_dev_replace, &replace_dev_info, NULL, NULL,
			     init_xs_type(), 0, 0);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT\n");
		goto out;
	}

	/* BIO device state after reintegration */
	resp->device->dev_state = CTL__NVME_DEV_STATE__NORMAL;
out:
	if (rc != 0) {
		if (resp->device->uuid != NULL)
			D_FREE(resp->device->uuid);
	}

	return rc;
}
