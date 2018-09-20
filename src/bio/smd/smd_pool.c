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

#include <daos_errno.h>
#include <daos/common.h>
#include <daos/mem.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos_types.h>

#include "smd_internal.h"

#define SMD_PTAB_ORDER 56

/** Pool table key type */
struct pool_tab_key {
	uuid_t	ptk_pid;
	int	ptk_sid;
};

static int
ptab_df_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct pool_tab_key);
}

static void
ptab_df_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct pool_tab_key));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
ptab_df_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct pool_tab_key *key1 = (struct pool_tab_key *)&rec->rec_hkey[0];
	struct pool_tab_key *key2 = (struct pool_tab_key *)hkey;
	int		     cmp  = 0;

	cmp = uuid_compare(key1->ptk_pid, key2->ptk_pid);
	if (cmp < 0)
		return BTR_CMP_LT;

	if (cmp > 0)
		return BTR_CMP_GT;

	if (key1->ptk_sid > key2->ptk_sid)
		return BTR_CMP_GT;

	if (key1->ptk_sid < key2->ptk_sid)
		return BTR_CMP_LT;

	return BTR_CMP_EQ;
}


static int
ptab_df_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	TMMID(struct smd_nvme_pool_df)	npool_mmid;
	struct umem_instance		*umm = &tins->ti_umm;

	npool_mmid = umem_id_u2t(rec->rec_mmid, struct smd_nvme_pool_df);
	if (TMMID_IS_NULL(npool_mmid))
		return -DER_NONEXIST;

	umem_free_typed(umm, npool_mmid);
	return 0;
}

static int
ptab_df_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
		  daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct smd_nvme_pool_df)	npool_mmid;
	struct smd_nvme_pool_df		*npool_df;
	struct pool_tab_key		*pkey = NULL;

	D_ASSERT(key_iov->iov_len == sizeof(struct pool_tab_key));
	pkey = (struct pool_tab_key *)key_iov->iov_buf;

	npool_mmid = umem_znew_typed(&tins->ti_umm,
				     struct smd_nvme_pool_df);
	if (TMMID_IS_NULL(npool_mmid))
		return -DER_NOMEM;
	npool_df = umem_id2ptr_typed(&tins->ti_umm, npool_mmid);
	uuid_copy(npool_df->np_info.npi_pool_uuid, pkey->ptk_pid);
	npool_df->np_info.npi_stream_id = pkey->ptk_sid;
	memcpy(npool_df, val_iov->iov_buf, sizeof(struct smd_nvme_pool_df));
	rec->rec_mmid = umem_id_t2u(npool_mmid);

	return 0;
}

static int
ptab_df_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_pool_df		*npool_df = NULL;

	npool_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(val_iov->iov_buf, npool_df, sizeof(struct smd_nvme_pool_df));

	return 0;
}

static int
ptab_df_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_pool_df		*npool_df;

	umem_tx_add(&tins->ti_umm, rec->rec_mmid,
		    sizeof(struct smd_nvme_pool_df));
	npool_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(npool_df, val_iov->iov_buf, val_iov->iov_len);
	return 0;
}

btr_ops_t ptab_ops = {
	.to_hkey_size	= ptab_df_hkey_size,
	.to_hkey_gen	= ptab_df_hkey_gen,
	.to_hkey_cmp	= ptab_df_hkey_cmp,
	.to_rec_alloc	= ptab_df_rec_alloc,
	.to_rec_free	= ptab_df_rec_free,
	.to_rec_fetch	= ptab_df_rec_fetch,
	.to_rec_update	= ptab_df_rec_update,
};

int
smd_nvme_ptab_register()
{
	int	rc;

	D_DEBUG(DB_DF, "Register peristent metadata pool index: %d\n",
		DBTREE_CLASS_SMD_PTAB);

	rc = dbtree_class_register(DBTREE_CLASS_SMD_PTAB, 0, &ptab_ops);
	if (rc)
		D_ERROR("DBTREE PTAB registration failed\n");

	return rc;
}

int
smd_nvme_md_ptab_create(struct umem_attr *p_umem_attr,
			struct smd_nvme_pool_tab_df *table_df)
{
	int		rc = 0;
	daos_handle_t	btr_hdl;

	D_ASSERT(table_df->npt_btr.tr_class == 0);
	D_DEBUG(DB_DF, "Create Persistent NVMe MD Device Index, type=%d\n",
		DBTREE_CLASS_SMD_DTAB);

	rc = dbtree_create_inplace(DBTREE_CLASS_SMD_PTAB, 0, SMD_PTAB_ORDER,
				   p_umem_attr, &table_df->npt_btr, &btr_hdl);
	if (rc) {
		D_ERROR("Persistent NVMe pool dbtree create failed\n");
		D_GOTO(exit, rc);
	}

	rc = dbtree_close(btr_hdl);
	if (rc)
		D_ERROR("Error in closing btree handle\n");
exit:
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
	struct smd_store	*store = get_smd_store();
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

	TX_BEGIN(smd_store_ptr2pop(store)) {
		daos_iov_t	key, value;

		daos_iov_set(&key, &ptab_key, sizeof(ptab_key));
		daos_iov_set(&value, &nvme_ptab_args, sizeof(nvme_ptab_args));

		rc = dbtree_update(store->sms_pool_tab, &key, &value);
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
	struct smd_store	*store = get_smd_store();
	daos_iov_t		key, value;
	struct pool_tab_key	ptkey;
	int			rc	= 0;

	D_DEBUG(DB_TRACE, "Fetching pool id in pool table\n");
	if (info == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Pool in pool table already exists, rc: %d\n", rc);
		return rc;
	}

	uuid_copy(ptkey.ptk_pid, pool_id);
	ptkey.ptk_sid = stream_id;
	daos_iov_set(&key, &ptkey, sizeof(struct pool_tab_key));
	daos_iov_set(&value, &nvme_ptab_args, sizeof(struct smd_nvme_pool_df));

	smd_lock(SMD_PTAB_LOCK);
	rc = dbtree_lookup(store->sms_pool_tab, &key, &value);
	smd_unlock(SMD_PTAB_LOCK);

	if (rc)
		D_DEBUG(DB_MGMT, "Cannot find pool in pool table rc: %d\n",
			rc);
	else
		*info = nvme_ptab_args.np_info;

	return rc;
}


