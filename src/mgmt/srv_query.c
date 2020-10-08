/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * ds_mgmt: Storage Query Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/bio.h>
#include <daos_srv/smd.h>

#include "srv_internal.h"

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
	/* TODO Add a new DSS_ULT_BIO tag */
	rc = dss_ult_create(bio_health_query, mbh, DSS_ULT_GC, tgt_id, 0,
			    &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

out:
	smd_free_dev_info(dev_info);
	return rc;
}

int
ds_mgmt_smd_list_devs(Mgmt__SmdDevResp *resp)
{
	struct smd_dev_info	*dev_info = NULL, *tmp;
	d_list_t		 dev_list;
	int			 dev_list_cnt = 0;
	int			 buflen = 10;
	int			 i = 0, j;
	int			 rc = 0;

	D_DEBUG(DB_MGMT, "Querying SMD device list\n");

	D_INIT_LIST_HEAD(&dev_list);
	rc = smd_dev_list(&dev_list, &dev_list_cnt);
	if (rc != 0) {
		D_ERROR("Failed to get all NVMe devices from SMD\n");
		return rc;
	}

	D_ALLOC_ARRAY(resp->devices, dev_list_cnt);
	if (resp->devices == NULL) {
		D_ERROR("Failed to allocate devices for resp\n");
		return -DER_NOMEM;
	}

	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, sdi_link) {
		D_ALLOC_PTR(resp->devices[i]);
		if (resp->devices[i] == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		mgmt__smd_dev_resp__device__init(resp->devices[i]);
		D_ALLOC(resp->devices[i]->uuid, DAOS_UUID_STR_SIZE);
		if (resp->devices[i]->uuid == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		uuid_unparse_lower(dev_info->sdi_id, resp->devices[i]->uuid);

		D_ALLOC(resp->devices[i]->state, buflen);
		if (resp->devices[i]->state == NULL) {
			D_ERROR("Failed to allocate device state");
			rc = -DER_NOMEM;
			break;
		}
		strncpy(resp->devices[i]->state,
			smd_state_enum_to_str(dev_info->sdi_state), buflen);

		resp->devices[i]->n_tgt_ids = dev_info->sdi_tgt_cnt;
		D_ALLOC(resp->devices[i]->tgt_ids,
			sizeof(int) * dev_info->sdi_tgt_cnt);
		if (resp->devices[i]->tgt_ids == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		for (j = 0; j < dev_info->sdi_tgt_cnt; j++)
			resp->devices[i]->tgt_ids[j] = dev_info->sdi_tgts[j];

		d_list_del(&dev_info->sdi_link);
		/* Frees sdi_tgts and dev_info */
		smd_free_dev_info(dev_info);
		dev_info = NULL;

		i++;
	}

	/* Free all devices if there was an error allocating any */
	if (rc != 0) {
		d_list_for_each_entry_safe(dev_info, tmp, &dev_list, sdi_link) {
			d_list_del(&dev_info->sdi_link);
			smd_free_dev_info(dev_info);
		}
		for (; i >= 0; i--) {
			if (resp->devices[i] != NULL) {
				if (resp->devices[i]->uuid != NULL)
					D_FREE(resp->devices[i]->uuid);
				if (resp->devices[i]->tgt_ids != NULL)
					D_FREE(resp->devices[i]->tgt_ids);
				if (resp->devices[i]->state != NULL)
					D_FREE(resp->devices[i]->state);
				D_FREE(resp->devices[i]);
			}
		}
		D_FREE(resp->devices);
		resp->devices = NULL;
		resp->n_devices = 0;
		goto out;
	}
	resp->n_devices = dev_list_cnt;

out:
	return rc;
}

int
ds_mgmt_smd_list_pools(Mgmt__SmdPoolResp *resp)
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
		mgmt__smd_pool_resp__pool__init(resp->pools[i]);
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
		smd_free_pool_info(pool_info);
		pool_info = NULL;

		i++;
	}

	/* Free all pools if there was an error allocating any */
	if (rc != 0) {
		d_list_for_each_entry_safe(pool_info, tmp, &pool_list,
					   spi_link) {
			d_list_del(&pool_info->spi_link);
			smd_free_pool_info(pool_info);
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
ds_mgmt_dev_state_query(uuid_t dev_uuid, Mgmt__DevStateResp *resp)
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
		smd_state_enum_to_str(dev_info->sdi_state), buflen);

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL) {
		D_ERROR("Failed to allocate device uuid");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_unparse_lower(dev_uuid, resp->dev_uuid);

out:
	smd_free_dev_info(dev_info);

	if (rc != 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	return rc;
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
ds_mgmt_dev_set_faulty(uuid_t dev_uuid, Mgmt__DevStateResp *resp)
{
	struct smd_dev_info	*dev_info;
	ABT_thread		 thread;
	int			 tgt_id;
	int			 buflen = 10;
	int			 rc = 0;

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
	/* TODO Add a new DSS_ULT_BIO tag */
	rc = dss_ult_create(bio_faulty_state_set, NULL, DSS_ULT_GC,
			    tgt_id, 0, &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		goto out;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

out:
	dev_info->sdi_state = SMD_DEV_FAULTY;
	strncpy(resp->dev_state,
		smd_state_enum_to_str(dev_info->sdi_state), buflen);
	smd_free_dev_info(dev_info);

	if (rc != 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	return rc;
}
