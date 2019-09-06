/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

	D_ASSERT(info != NULL);
	D_DEBUG(DB_MGMT, "BIO health stats query on xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);

	bxc = info->dmi_nvme_ctxt;
	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
			info->dmi_xs_id, info->dmi_tgt_id);
		goto out;
	}

	mbh->mb_dev_state = bio_get_dev_state(bxc);
	if (mbh->mb_dev_state == NULL) {
		D_ERROR("Error getting BIO device state\n");
		goto out;
	}

out:
	smd_free_dev_info(mbh->mb_dev_info);
}

int
ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t dev_uuid,
			char *tgt)
{
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
		rc = smd_dev_get_by_id(dev_uuid, &mbh->mb_dev_info);
		if (rc != 0) {
			D_ERROR("Device UUID:"DF_UUID" not found\n",
				DP_UUID(dev_uuid));
			return rc;
		}
		if (mbh->mb_dev_info->sdi_tgts == NULL) {
			D_ERROR("No targets mapped to device\n");
			return -DER_NONEXIST;
		}
		/* Default tgt_id is the first mapped tgt */
		tgt_id = mbh->mb_dev_info->sdi_tgts[0];
	} else {
		tgt_id = atoi(tgt);
		rc = smd_dev_get_by_tgt(tgt_id, &mbh->mb_dev_info);
		if (rc != 0) {
			D_ERROR("Tgt_id:%d not found\n", tgt_id);
			return rc;
		}
		uuid_copy(dev_uuid, mbh->mb_dev_info->sdi_id);
	}

	D_DEBUG(DB_MGMT, "Querying BIO Health Data for dev:"DF_UUID"\n",
		DP_UUID(dev_uuid));
	uuid_copy(mbh->mb_devid, dev_uuid);

	/* Create a ULT on the tgt_id */
	D_DEBUG(DB_MGMT, "Starting ULT on tgt_id:%d\n", tgt_id);
	rc = dss_ult_create(bio_health_query, mbh, DSS_ULT_BIO, tgt_id, 0,
			    &thread);
	if (rc != 0) {
		D_ERROR("Unable to create a ULT on tgt_id:%d\n", tgt_id);
		return rc;
	}

	ABT_thread_join(thread);
	ABT_thread_free(&thread);

	return rc;
}

int
ds_mgmt_smd_list_devs(Mgmt__SmdDevResp *resp)
{
	struct smd_dev_info	*dev_info, *tmp;
	d_list_t		 dev_list;
	int			 dev_list_cnt = 0;
	int			 i = 0, j;
	int			 rc = 0;

	D_DEBUG(DB_MGMT, "Querying SMD device list\n");

	D_INIT_LIST_HEAD(&dev_list);
	rc = smd_dev_list(&dev_list, &dev_list_cnt);

	D_ALLOC(resp->devices, sizeof(*resp->devices) * dev_list_cnt);
	if (resp->devices == NULL) {
		D_ERROR("Failed to allocate devices for resp\n");
		return -DER_NOMEM;
	}

	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, sdi_link) {
		D_ALLOC(resp->devices[i], sizeof(*(resp->devices[i])));
		if (resp->devices[i] == NULL) {
			D_ERROR("Failed to allocate device\n");
			rc = -DER_NOMEM;
			break;
		}
		mgmt__smd_dev_resp__device__init(resp->devices[i]);
		D_ALLOC(resp->devices[i]->uuid, DAOS_UUID_STR_SIZE);
		if (resp->devices[i]->uuid == NULL) {
			D_ERROR("Failed to allocate device uuid\n");
			rc = -DER_NOMEM;
			break;
		}
		uuid_unparse_lower(dev_info->sdi_id, resp->devices[i]->uuid);

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

		i++;
	}
	/* Free all devices is there was an error allocating any */
	if (rc != 0) {
		for( ; i >= 0; i-- ) {
			if (resp->devices[i] != NULL) {
				if (resp->devices[i]->uuid != NULL)
					D_FREE(resp->devices[i]->uuid);
				if (resp->devices[i]->tgt_ids != NULL)
					D_FREE(resp->devices[i]->tgt_ids);
				D_FREE(resp->devices[i]);
			}
		}
		D_FREE(resp->devices);
		resp->devices = NULL;
		goto out;
	}
	resp->n_devices = dev_list_cnt;

out:
	return rc;
}
