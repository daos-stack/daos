/**
 * (C) Copyright 2018-2019 Intel Corporation.
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

#include <daos_errno.h>
#include <daos/common.h>
#include <daos/mem.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos_types.h>

#include "smd_internal.h"
#define SMD_DTAB_ORDER 32

static int
dtab_df_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct d_uuid);
}

static void
dtab_df_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct d_uuid));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
dtab_df_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	TMMID(struct smd_nvme_dev_df)	ndev_mmid;
	struct umem_instance		*umm = &tins->ti_umm;

	ndev_mmid = umem_id_u2t(rec->rec_mmid, struct smd_nvme_dev_df);
	if (TMMID_IS_NULL(ndev_mmid))
		return -DER_NONEXIST;

	umem_free_typed(umm, ndev_mmid);
	return 0;
}

static int
dtab_df_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
		  daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct smd_nvme_dev_df)	ndev_mmid;
	struct smd_nvme_dev_df		*ndev_df;
	struct d_uuid			*ukey = NULL;

	D_ASSERT(key_iov->iov_len == sizeof(struct d_uuid));
	ukey = (struct d_uuid *)key_iov->iov_buf;
	D_DEBUG(DB_DF, "Allocating device uuid=%s\n", DP_UUID(ukey->uuid));

	ndev_mmid = umem_znew_typed(&tins->ti_umm, struct smd_nvme_dev_df);
	if (TMMID_IS_NULL(ndev_mmid))
		return -DER_NOMEM;
	ndev_df = umem_id2ptr_typed(&tins->ti_umm, ndev_mmid);
	uuid_copy(ndev_df->nd_dev_id, ukey->uuid);
	memcpy(ndev_df, val_iov->iov_buf, sizeof(*ndev_df));
	rec->rec_mmid = umem_id_t2u(ndev_mmid);
	return 0;
}

static int
dtab_df_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_dev_df		*ndev_df = NULL;

	ndev_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(val_iov->iov_buf, ndev_df, sizeof(*ndev_df));
	return 0;
}

static int
dtab_df_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_dev_df		*ndev_df;

	umem_tx_add(&tins->ti_umm, rec->rec_mmid,
		    sizeof(struct smd_nvme_pool_df));
	ndev_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(ndev_df, val_iov->iov_buf, val_iov->iov_len);
	return 0;
}

btr_ops_t dtab_ops = {
	.to_hkey_size	= dtab_df_hkey_size,
	.to_hkey_gen	= dtab_df_hkey_gen,
	.to_rec_alloc	= dtab_df_rec_alloc,
	.to_rec_free	= dtab_df_rec_free,
	.to_rec_fetch	= dtab_df_rec_fetch,
	.to_rec_update	= dtab_df_rec_update,
};

int
smd_nvme_dtab_register()
{
	int	rc;

	D_DEBUG(DB_DF, "Register persistent metadata device index: %d\n",
		DBTREE_CLASS_SMD_DTAB);

	rc = dbtree_class_register(DBTREE_CLASS_SMD_DTAB, 0, &dtab_ops);
	if (rc)
		D_ERROR("DBTREE DTAB registration failed\n");

	return rc;
}

int
smd_nvme_md_dtab_create(struct umem_attr *d_umem_attr,
			struct smd_nvme_dev_tab_df *table_df)
{

	int		rc = 0;
	daos_handle_t	btr_hdl;

	D_ASSERT(table_df->ndt_btr.tr_class == 0);
	D_DEBUG(DB_DF, "Create Persistent NVMe MD Device Index, type=%d\n",
		DBTREE_CLASS_SMD_DTAB);

	rc = dbtree_create_inplace(DBTREE_CLASS_SMD_DTAB, 0, SMD_DTAB_ORDER,
				   d_umem_attr, &table_df->ndt_btr, &btr_hdl);
	if (rc) {
		D_ERROR("Persistent NVMe device dbtree create failed\n");
		D_GOTO(exit, rc);
	}

	rc = dbtree_close(btr_hdl);
	if (rc)
		D_ERROR("Error in closing btree handle\n");
exit:
	return rc;
}

static int
smd_dtab_df_lookup(struct smd_store *nvme_obj, struct d_uuid *ukey,
		   struct smd_nvme_dev_df *ndev_df)
{
	daos_iov_t	key, value;

	daos_iov_set(&key, ukey, sizeof(struct d_uuid));
	daos_iov_set(&value, ndev_df, sizeof(struct smd_nvme_dev_df));
	return dbtree_lookup(nvme_obj->sms_dev_tab, &key, &value);
}


static int
smd_dtab_df_update(struct smd_store *nvme_obj, struct d_uuid *ukey,
		   struct smd_nvme_dev_df *ndev_df)
{
	int		rc = 0;
	daos_iov_t	key, value;

	daos_iov_set(&key, ukey, sizeof(struct d_uuid));
	daos_iov_set(&value, ndev_df, sizeof(struct smd_nvme_dev_df));

	TX_BEGIN(smd_store_ptr2pop(nvme_obj)) {
		rc = dbtree_update(nvme_obj->sms_dev_tab, &key, &value);
		if (rc) {
			D_ERROR("Adding a device/updating status: %d\n", rc);
			pmemobj_tx_abort(ENOMEM);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Adding/updating a device entry: %d\n", rc);
	} TX_END;

	return rc;
}

int
smd_dtab_df_find_update(struct smd_store *nvme_obj, struct d_uuid *ukey,
			uint32_t status)
{
	int			rc = 0;
	struct smd_nvme_dev_df	ndev_df;

	smd_lock(SMD_DTAB_LOCK);
	rc = smd_dtab_df_lookup(nvme_obj, ukey, &ndev_df);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_TRACE, "Device %s not found in dev table\n",
			DP_UUID(ukey->uuid));
		memset(&ndev_df, 0, sizeof(ndev_df));
		uuid_copy(ndev_df.nd_dev_id, ukey->uuid);
		ndev_df.nd_status = status;
		rc = smd_dtab_df_update(nvme_obj, ukey, &ndev_df);
		if (rc)
			D_ERROR("Adding device in dev table failed: %d\n",
				rc);
	}
	smd_unlock(SMD_DTAB_LOCK);
	return rc;
}



/**
 * Server NVMe set device status will update the status of the NVMe device
 * in the SMD device table, if the device is not found it adds a new entry
 *
 * \param	[IN]	device_id	UUID of device
 * \param	[IN]	status		Status of device
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int
smd_nvme_set_device_status(uuid_t device_id, enum smd_device_status status)
{
	struct d_uuid		ukey;
	struct smd_nvme_dev_df	nvme_dtab_args;
	struct smd_store	*store  = get_smd_store();
	int			rc	  = 0;

	smd_lock(SMD_DTAB_LOCK);
	uuid_copy(ukey.uuid, device_id);
	uuid_copy(nvme_dtab_args.nd_dev_id, device_id);
	nvme_dtab_args.nd_status = status;
	rc = smd_dtab_df_update(store, &ukey, &nvme_dtab_args);
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
	struct smd_store		*store = get_smd_store();
	struct d_uuid			ukey;
	struct smd_nvme_dev_df		nvme_dtab_args;
	struct smd_nvme_stream_bond	streams[DEV_MAX_STREAMS];
	int				i, rc	= 0;
	uint32_t			nr;

	if (info == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Retun value memory NULL: %d\n", rc);
		return rc;
	}

	memset(info, 0, sizeof(*info));
	uuid_copy(ukey.uuid, device_id);

	smd_lock(SMD_DTAB_LOCK);
	rc = smd_dtab_df_lookup(store, &ukey, &nvme_dtab_args);
	if (rc) {
		smd_unlock(SMD_DTAB_LOCK);
		return rc;
	}
	smd_unlock(SMD_DTAB_LOCK);

	uuid_copy(info->ndi_dev_id, nvme_dtab_args.nd_dev_id);
	info->ndi_status = nvme_dtab_args.nd_status;
	nr = DEV_MAX_STREAMS;

	rc = smd_nvme_list_streams(&nr, &streams[0], NULL);
	if (rc) {
		D_ERROR("Error in retrieving streams from stream table\n");
		return rc;
	}
	D_DEBUG(DB_TRACE, "total streams returned after listing: %d\n", nr);

	for (i = 0; i < nr; i++) {
		rc = uuid_compare(device_id, streams[i].nsm_dev_id);
		if (!rc) {
			info->ndi_xstreams[info->ndi_xs_cnt] =
				streams[i].nsm_stream_id;
			info->ndi_xs_cnt++;
		}
	}
	return 0;
}


