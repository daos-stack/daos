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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * DAOS Server Persistent Metadata
 * NVMe Device Persistent Metadata Storage
 */
#define D_LOGFAC	DD_FAC(bio)

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <sys/sysinfo.h>
#include <daos/common.h>
#include <gurt/debug.h>
#include <abt.h>
#include <daos.h>
#include <daos_srv/smd.h>
#include <daos_types.h>

#include "smd_internal.h"

static int
device_tab_df_lookup(struct smd_store *nvme_obj, struct d_uuid *ukey,
		     struct smd_nvme_dev_df *ndev_df)
{
	daos_iov_t	key, value;

	daos_iov_set(&key, ukey, sizeof(struct d_uuid));
	daos_iov_set(&value, ndev_df, sizeof(struct smd_nvme_dev_df));
	return dbtree_lookup(nvme_obj->sms_dev_tab, &key, &value);
}

static int
pool_tab_df_lookup(struct smd_store *sms_obj, uuid_t ukey,
		   int stream_id, struct smd_nvme_pool_df *npool_df)
{
	daos_iov_t		key, value;
	struct pool_tab_key	ptkey;

	uuid_copy(ptkey.ptk_pid, ukey);
	ptkey.ptk_sid = stream_id;

	daos_iov_set(&key, &ptkey, sizeof(struct pool_tab_key));
	daos_iov_set(&value, npool_df, sizeof(struct smd_nvme_pool_df));
	return dbtree_lookup(sms_obj->sms_pool_tab, &key, &value);
}

static int
stream_tab_df_lookup(struct smd_store *sms_obj, int stream_id,
		     struct smd_nvme_stream_df *nstream_df)
{
	daos_iov_t		key, value;

	daos_iov_set(&key, &stream_id, sizeof(int));
	daos_iov_set(&value, nstream_df, sizeof(struct smd_nvme_stream_df));
	return dbtree_lookup(sms_obj->sms_stream_tab, &key, &value);
}

/**
 * DAOS NVMe metadata index add device bond and status
 *
 * \param info		[IN]	 DAOS mgmt NVMe device info
 *
 * \return			 0 on success and negative on
 *				 failure
 */
int
smd_nvme_add_device(struct smd_nvme_device_info *info)
{

	struct d_uuid		ukey;
	struct smd_nvme_dev_df	nvme_dtab_args;
	struct smd_store	*sms_obj  = get_sm_obj();
	int			rc	  = 0;

	if (info == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Missing input parameters: %d\n", rc);
		return rc;
	}

	smd_lock(SMD_DTAB_LOCK);
	uuid_copy(ukey.uuid, info->ndi_dev_id);
	nvme_dtab_args.nd_info = *info;

	TX_BEGIN(smd_store_ptr2pop(sms_obj)) {
		daos_iov_t	key, value;

		daos_iov_set(&key, &ukey, sizeof(ukey));
		daos_iov_set(&value, &nvme_dtab_args, sizeof(nvme_dtab_args));

		rc = dbtree_update(sms_obj->sms_dev_tab, &key, &value);
		if (rc) {
			D_ERROR("Adding a device: %d\n", rc);
			pmemobj_tx_abort(ENOMEM);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Adding a device entry: %d\n", rc);
	} TX_END;
	smd_unlock(SMD_DTAB_LOCK);

	return rc;
}

/**
 * DAOS NVMe metadata index get device status
 *
 * \param device_id [IN]	 NVMe device UUID
 * \param info	    [OUT]	 NVMe device info
 *
 * \return			 0 on success and negative on
 *				 failure
 *
 */
int
smd_nvme_get_device(uuid_t device_id,
		    struct smd_nvme_device_info *info)
{
	struct smd_store	*sms_obj = get_sm_obj();
	struct d_uuid		ukey;
	struct smd_nvme_dev_df	nvme_dtab_args;
	int			rc	= 0;

	if (info == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Retun value memory NULL: %d\n", rc);
		return rc;
	}
	uuid_copy(ukey.uuid, device_id);

	smd_lock(SMD_DTAB_LOCK);
	rc = device_tab_df_lookup(sms_obj, &ukey, &nvme_dtab_args);
	if (rc) {
		smd_unlock(SMD_DTAB_LOCK);
		return rc;
	}
	smd_unlock(SMD_DTAB_LOCK);
	*info = nvme_dtab_args.nd_info;

	return rc;
}

/**
 * DAOS NVMe pool metatdata index add status
 *
 * \param pool_info     [IN]	NVMe pool info
 *
 * \return			0 on success and negative on
 *				failure
 */
int
smd_nvme_add_pool(struct smd_nvme_pool_info *info)
{
	struct pool_tab_key	ptab_key;
	struct smd_nvme_pool_df	nvme_ptab_args;
	struct smd_store	*sms_obj = get_sm_obj();
	int			rc	= 0;

	D_DEBUG(DB_TRACE, "Add a pool id in pool table\n");
	if (info == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Error adding entry in pool table: %d\n", rc);
		return rc;
	}

	smd_lock(SMD_PTAB_LOCK);
	uuid_copy(ptab_key.ptk_pid, info->npi_pool_uuid);
	ptab_key.ptk_sid = info->npi_stream_id;
	nvme_ptab_args.np_info = *info;

	TX_BEGIN(smd_store_ptr2pop(sms_obj)) {
		daos_iov_t	key, value;

		daos_iov_set(&key, &ptab_key, sizeof(ptab_key));
		daos_iov_set(&value, &nvme_ptab_args, sizeof(nvme_ptab_args));

		rc = dbtree_update(sms_obj->sms_pool_tab, &key, &value);
		if (rc) {
			D_ERROR("Adding a pool failed in pool_table: %d\n", rc);
			pmemobj_tx_abort(ENOMEM);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Adding a pool entry to nvme pool table: %d\n", rc);
	} TX_END;
	smd_unlock(SMD_PTAB_LOCK);

	return rc;
}

/**
 * DAOS NVMe pool metatdata index get status
 *
 * \param pool_id	[IN]	 Pool UUID
 * \param stream_id	[IN]	 Stream ID
 * \param info		[OUT]	 NVMe pool info
 */
int
smd_nvme_get_pool(uuid_t pool_id, int stream_id,
		  struct smd_nvme_pool_info *info)
{
	struct smd_nvme_pool_df	nvme_ptab_args;
	struct smd_store	*sms_obj = get_sm_obj();
	int			rc	= 0;

	D_DEBUG(DB_TRACE, "Fetching pool id in pool table\n");
	if (info == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Pool in pool table already exists, rc: %d\n", rc);
		return rc;
	}

	smd_lock(SMD_PTAB_LOCK);
	rc = pool_tab_df_lookup(sms_obj, pool_id, stream_id,
				&nvme_ptab_args);
	if (rc) {
		D_DEBUG(DB_MGMT,
			"Cannot find pool entry in pool table rc: %d\n", rc);
		smd_unlock(SMD_PTAB_LOCK);
		return rc;
	}
	smd_unlock(SMD_PTAB_LOCK);
	*info = nvme_ptab_args.np_info;

	return rc;
}

/**
 * Server NMVe add Stream to Device bond SMD stream table
 *
 * \param	[IN]	stream_bond	SMD NVMe device/stream
 *					bond
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int smd_nvme_add_stream_bond(struct smd_nvme_stream_bond *bond)
{
	struct smd_nvme_stream_df	nvme_stab_args;
	struct smd_store		*sms_obj = get_sm_obj();
	int				rc	 = 0;

	if (bond == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Missing input parameters: %d\n", rc);
		return rc;
	}

	smd_lock(SMD_STAB_LOCK);
	nvme_stab_args.ns_map = *bond;

	TX_BEGIN(smd_store_ptr2pop(sms_obj)) {
		daos_iov_t	key, value;

		daos_iov_set(&key, &bond->nsm_stream_id, sizeof(int));
		daos_iov_set(&value, &nvme_stab_args, sizeof(nvme_stab_args));

		rc = dbtree_update(sms_obj->sms_stream_tab, &key, &value);
		if (rc) {
			D_ERROR("Adding a device : %d\n", rc);
			pmemobj_tx_abort(ENOMEM);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Adding a stream bond entry: %d\n", rc);
	} TX_END;
	smd_unlock(SMD_STAB_LOCK);

	return rc;
}

/**
 * Server NVMe get device corresponding to a stream
 *
 * \param	[IN]	stream_id	SMD NVMe stream ID
 * \param	[OUT]	bond		SMD bond information
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int smd_nvme_get_stream_bond(int stream_id,
				struct smd_nvme_stream_bond *bond)
{
	struct smd_nvme_stream_df	nvme_stab_args;
	struct smd_store		*sms_obj = get_sm_obj();
	int				rc	 = 0;

	D_DEBUG(DB_TRACE, "looking up device id in stream table\n");
	if (bond == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Missing input parameters: %d\n", rc);
		return rc;
	}

	smd_lock(SMD_STAB_LOCK);
	rc = stream_tab_df_lookup(sms_obj, stream_id, &nvme_stab_args);
	if (rc) {
		smd_unlock(SMD_STAB_LOCK);
		D_GOTO(exit, rc);
	}
	smd_unlock(SMD_STAB_LOCK);
	*bond = nvme_stab_args.ns_map;
exit:
	return rc;
}
