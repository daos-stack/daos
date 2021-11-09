/**
 * (C) Copyright 2016-2021 Intel Corporation.
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


static void
bs_state_query(void *arg)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct bio_xs_context	*bxc;
	int			*bs_state = arg;

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO blobstore state query on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return;
	}

	bio_get_bs_state(bs_state, bxc);
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
	rc = dss_ult_create(bs_state_query, (void *)bs_state, DSS_XS_VOS,
			    tgt_id, 0, &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	DABT_THREAD_FREE(&thread);

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

	rc = bio_get_dev_state(&mbh->mb_dev_state, bxc);
	if (rc != 0) {
		D_ERROR("Error getting BIO device state\n");
		return;
	}
}

int
ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t dev_uuid,
			char *tgt)
{
	struct smd_dev_info	*dev_info;
	ABT_thread		 thread;
	int			 tgt_id;
	int			 rc = 0;

	if (uuid_is_null(dev_uuid) && strlen(tgt) == 0) {
		/* Either dev uuid or tgt id needs to be specified for query */
		D_ERROR("Neither dev_uuid or tgt_id specified for BIO query\n");
		return -DER_INVAL;
	}

	/*
	 * Query per-server metadata (SMD) to get either target ID(s) for given
	 * device or alternatively the device mapped to a given target.
	 */
	if (!uuid_is_null(dev_uuid)) {
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
		/* Default tgt_id is the first mapped tgt */
		tgt_id = dev_info->sdi_tgts[0];
	} else {
		tgt_id = atoi(tgt);
		rc = smd_dev_get_by_tgt(tgt_id, &dev_info);
		if (rc != 0) {
			D_ERROR("Tgt_id:%d not found\n", tgt_id);
			return rc;
		}
		uuid_copy(dev_uuid, dev_info->sdi_id);
	}

	D_DEBUG(DB_MGMT, "Querying BIO Health Data for dev:"DF_UUID"\n",
		DP_UUID(dev_uuid));
	uuid_copy(mbh->mb_devid, dev_uuid);

	/* Create a ULT on the tgt_id */
	D_DEBUG(DB_MGMT, "Starting ULT on tgt_id:%d\n", tgt_id);
	rc = dss_ult_create(bio_health_query, mbh, DSS_XS_VOS, tgt_id, 0,
			    &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	DABT_THREAD_FREE(&thread);

out:
	smd_dev_free_info(dev_info);
	return rc;
}

struct bio_list_devs_info {
	d_list_t	dev_list;
	int		dev_list_cnt;
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
	struct bio_dev_info	   *dev_info = NULL, *tmp;
	struct bio_list_devs_info   list_devs_info = { 0 };
	enum bio_dev_state	    state;
	int			    buflen = 10;
	int			    i = 0, j;
	int			    rc = 0;

	D_DEBUG(DB_MGMT, "Querying BIO & SMD device list\n");

	D_INIT_LIST_HEAD(&list_devs_info.dev_list);

	rc = dss_ult_execute(bio_query_dev_list, &list_devs_info, NULL, NULL,
			     DSS_XS_VOS, 0, 0);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT\n");
		goto out;
	}

	D_ALLOC_ARRAY(resp->devices, list_devs_info.dev_list_cnt);
	if (resp->devices == NULL) {
		D_ERROR("Failed to allocate devices for resp\n");
		return -DER_NOMEM;
	}

	d_list_for_each_entry_safe(dev_info, tmp, &list_devs_info.dev_list,
				   bdi_link) {
		D_ALLOC_PTR(resp->devices[i]);
		if (resp->devices[i] == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		ctl__smd_dev_resp__device__init(resp->devices[i]);
		/*
		 * XXX: These fields are initialized as "empty string" by above
		 * protobuf auto-generated function, to avoid error cleanup
		 * code mistakenly free the "empty string", let's reset them as
		 * NULL.
		 */
		resp->devices[i]->uuid = NULL;
		resp->devices[i]->state = NULL;
		resp->devices[i]->tr_addr = NULL;

		D_ALLOC(resp->devices[i]->uuid, DAOS_UUID_STR_SIZE);
		if (resp->devices[i]->uuid == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		uuid_unparse_lower(dev_info->bdi_dev_id,
				   resp->devices[i]->uuid);

		D_ALLOC(resp->devices[i]->state, buflen);
		if (resp->devices[i]->state == NULL) {
			D_ERROR("Failed to allocate device state");
			rc = -DER_NOMEM;
			break;
		}
		/* BIO device state is determined by device flags */
		if (dev_info->bdi_flags & NVME_DEV_FL_PLUGGED) {
			if (dev_info->bdi_flags & NVME_DEV_FL_FAULTY)
				state = BIO_DEV_FAULTY;
			else if (dev_info->bdi_flags & NVME_DEV_FL_INUSE)
				state = BIO_DEV_NORMAL;
			else
				state = BIO_DEV_NEW;
		} else
			state = BIO_DEV_OUT;

		strncpy(resp->devices[i]->state,
			bio_dev_state_enum_to_str(state), buflen);

		if (dev_info->bdi_traddr != NULL) {
			buflen = strlen(dev_info->bdi_traddr) + 1;
			D_ALLOC(resp->devices[i]->tr_addr, buflen);
			if (resp->devices[i]->tr_addr == NULL) {
				D_ERROR("Failed to allocate device tr_addr");
				rc = -DER_NOMEM;
				break;
			}
			/* Transport Addr -> Blobstore UUID mapping */
			strncpy(resp->devices[i]->tr_addr, dev_info->bdi_traddr,
				buflen);
		}

		resp->devices[i]->n_tgt_ids = dev_info->bdi_tgt_cnt;
		D_ALLOC(resp->devices[i]->tgt_ids,
			sizeof(int) * dev_info->bdi_tgt_cnt);
		if (resp->devices[i]->tgt_ids == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < dev_info->bdi_tgt_cnt; j++)
			resp->devices[i]->tgt_ids[j] = dev_info->bdi_tgts[j];

		d_list_del(&dev_info->bdi_link);
		/* Frees sdi_tgts and dev_info */
		bio_free_dev_info(dev_info);
		dev_info = NULL;

		i++;
	}

	/* Free all devices if there was an error allocating any */
	if (rc != 0) {
		d_list_for_each_entry_safe(dev_info, tmp,
					   &list_devs_info.dev_list,
					   bdi_link) {
			d_list_del(&dev_info->bdi_link);
			bio_free_dev_info(dev_info);
		}
		for (; i >= 0; i--) {
			if (resp->devices[i] != NULL) {
				if (resp->devices[i]->uuid != NULL)
					D_FREE(resp->devices[i]->uuid);
				if (resp->devices[i]->tgt_ids != NULL)
					D_FREE(resp->devices[i]->tgt_ids);
				if (resp->devices[i]->state != NULL)
					D_FREE(resp->devices[i]->state);
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
	if (resp->pools == NULL) {
		D_ERROR("Failed to allocate pools for resp\n");
		return -DER_NOMEM;
	}

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

		resp->pools[i]->n_tgt_ids = pool_info->spi_tgt_cnt;
		D_ALLOC(resp->pools[i]->tgt_ids,
			sizeof(int) * pool_info->spi_tgt_cnt);
		if (resp->pools[i]->tgt_ids == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < pool_info->spi_tgt_cnt; j++)
			resp->pools[i]->tgt_ids[j] = pool_info->spi_tgts[j];

		resp->pools[i]->n_blobs = pool_info->spi_tgt_cnt;
		D_ALLOC(resp->pools[i]->blobs,
			sizeof(uint64_t) * pool_info->spi_tgt_cnt);
		if (resp->pools[i]->blobs == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < pool_info->spi_tgt_cnt; j++)
			resp->pools[i]->blobs[j] = pool_info->spi_blobs[j];


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

int
ds_mgmt_dev_state_query(uuid_t dev_uuid, Ctl__DevStateResp *resp)
{
	struct smd_dev_info	*dev_info;
	int			 buflen = 10;
	int			 rc = 0;

	if (uuid_is_null(dev_uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Querying SMD device state for dev:"DF_UUID"\n",
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

	D_ALLOC(resp->dev_state, buflen);
	if (resp->dev_state == NULL) {
		D_ERROR("Failed to allocate device state");
		rc = -DER_NOMEM;
		goto out;
	}
	strncpy(resp->dev_state,
		smd_dev_stat2str(dev_info->sdi_state), buflen);

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL) {
		D_ERROR("Failed to allocate device uuid");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_unparse_lower(dev_uuid, resp->dev_uuid);

out:
	smd_dev_free_info(dev_info);

	if (rc != 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	return rc;
}

struct bio_faulty_dev_info {
	uuid_t          devid;
};

static int
bio_faulty_led_set(void *arg)
{
	struct bio_faulty_dev_info *faulty_info = arg;
	struct dss_module_info	   *info = dss_get_module_info();
	struct bio_xs_context	   *bxc;
	int			    rc;

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO health state set on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return -DER_INVAL;
	}

	/* Set the LED of the VMD device to a FAULT state */
	rc = bio_set_led_state(bxc, faulty_info->devid, "fault",
			       false/*reset*/);
	if (rc != 0)
		D_ERROR("Error managing LED on device:"DF_UUID"\n",
			DP_UUID(faulty_info->devid));

	return 0;

}

static void
bio_faulty_state_set(void *arg)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct bio_xs_context	*bxc;
	int			 rc;

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO health state set on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return;
	}

	rc = bio_dev_set_faulty(bxc);
	if (rc != 0) {
		D_ERROR("Error setting FAULTY BIO device state\n");
		return;
	}
}

int
ds_mgmt_dev_set_faulty(uuid_t dev_uuid, Ctl__DevStateResp *resp)
{
	struct bio_faulty_dev_info  faulty_info = { 0 };
	struct smd_dev_info	   *dev_info;
	ABT_thread		    thread;
	int			    tgt_id;
	int			    buflen = 10;
	int			    rc = 0;

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
		rc = -DER_NONEXIST;
		goto out;
	}
	/* Default tgt_id is the first mapped tgt */
	tgt_id = dev_info->sdi_tgts[0];

	D_ALLOC(resp->dev_state, buflen);
	if (resp->dev_state == NULL) {
		D_ERROR("Failed to allocate device state");
		rc = -DER_NOMEM;
		goto out;
	}

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL) {
		D_ERROR("Failed to allocate device uuid");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_unparse_lower(dev_uuid, resp->dev_uuid);

	/* Create a ULT on the tgt_id */
	D_DEBUG(DB_MGMT, "Starting ULT on tgt_id:%d\n", tgt_id);
	rc = dss_ult_create(bio_faulty_state_set, NULL, DSS_XS_VOS,
			    tgt_id, 0, &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	DABT_THREAD_FREE(&thread);

	uuid_copy(faulty_info.devid, dev_uuid);
	/* set the VMD LED to FAULTY state on init xstream */
	rc = dss_ult_execute(bio_faulty_led_set, &faulty_info, NULL,
			     NULL, DSS_XS_VOS, 0, 0);
	if (rc) {
		D_ERROR("FAULT LED state not set on device:"DF_UUID"\n",
			DP_UUID(dev_uuid));
	}

	dev_info->sdi_state = SMD_DEV_FAULTY;
	strncpy(resp->dev_state, smd_dev_stat2str(dev_info->sdi_state), buflen);

out:
	smd_dev_free_info(dev_info);

	if (rc != 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
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
ds_mgmt_dev_replace(uuid_t old_dev_uuid, uuid_t new_dev_uuid,
		    Ctl__DevReplaceResp *resp)
{
	struct bio_replace_dev_info	 replace_dev_info = { 0 };
	int				 buflen = 10;
	int				 rc = 0;

	if (uuid_is_null(old_dev_uuid))
		return -DER_INVAL;
	if (uuid_is_null(new_dev_uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Replacing device:"DF_UUID" with device:"DF_UUID"\n",
		DP_UUID(old_dev_uuid), DP_UUID(new_dev_uuid));

	D_ALLOC(resp->new_dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->new_dev_uuid == NULL) {
		D_ERROR("Failed to allocate new device uuid");
		rc = -DER_NOMEM;
		goto out;
	}
	uuid_unparse_lower(new_dev_uuid, resp->new_dev_uuid);

	D_ALLOC(resp->dev_state, buflen);
	if (resp->dev_state == NULL) {
		D_ERROR("Failed to allocate device state");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_copy(replace_dev_info.old_dev, old_dev_uuid);
	uuid_copy(replace_dev_info.new_dev, new_dev_uuid);
	rc = dss_ult_execute(bio_storage_dev_replace, &replace_dev_info, NULL,
			     NULL, DSS_XS_VOS, 0, 0);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT\n");
		goto out;
	}

	/* BIO device state after reintegration should be NORMAL */
	strncpy(resp->dev_state, smd_dev_stat2str(SMD_DEV_NORMAL),
		buflen);
out:

	if (rc != 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->new_dev_uuid != NULL)
			D_FREE(resp->new_dev_uuid);
	}

	return rc;
}

struct bio_identify_dev_info {
	uuid_t		devid;
};

static int
bio_storage_dev_identify(void *arg)
{
	struct bio_identify_dev_info *identify_info = arg;
	struct dss_module_info	     *info = dss_get_module_info();
	struct bio_xs_context	     *bxc;
	int			      rc;

	D_ASSERT(info != NULL);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		return -DER_INVAL;
	}

	rc = bio_set_led_state(bxc, identify_info->devid, "identify",
			       false/*reset*/);
	if (rc != 0) {
		D_ERROR("Error managing LED on device:"DF_UUID"\n",
			DP_UUID(identify_info->devid));
		return rc;
	}

	return 0;
}


int
ds_mgmt_dev_identify(uuid_t dev_uuid, Ctl__DevIdentifyResp *resp)
{
	struct bio_identify_dev_info identify_info = { 0 };
	int			     buflen = 10;
	int			     rc = 0;

	if (uuid_is_null(dev_uuid))
		return -DER_INVAL;

	D_DEBUG(DB_MGMT, "Identifying device:"DF_UUID"\n", DP_UUID(dev_uuid));

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL) {
		D_ERROR("Failed to allocate device uuid");
		rc = -DER_NOMEM;
		goto out;
	}
	uuid_unparse_lower(dev_uuid, resp->dev_uuid);

	D_ALLOC(resp->led_state, buflen);
	if (resp->led_state == NULL) {
		D_ERROR("Failed to allocate device led state");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_copy(identify_info.devid, dev_uuid);
	rc = dss_ult_execute(bio_storage_dev_identify, &identify_info, NULL,
			     NULL, DSS_XS_VOS, 0, 0);
	if (rc != 0)
		goto out;

	strcpy(resp->led_state, "IDENTIFY");

out:

	if (rc != 0) {
		if (resp->led_state != NULL)
			D_FREE(resp->led_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	return rc;
}
