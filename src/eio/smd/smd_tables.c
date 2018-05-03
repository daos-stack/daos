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
#define D_LOGFAC	DD_FAC(eio)

#include <daos_errno.h>
#include <daos/common.h>
#include <daos/mem.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos_types.h>

#include "smd_internal.h"

#define SMD_DTAB_ORDER 32
#define SMD_PTAB_ORDER 56
#define SMD_STAB_ORDER 72

#define DBTREE_CLASS_SMD_DTAB (DBTREE_SMD_BEGIN + 0)
#define DBTREE_CLASS_SMD_PTAB (DBTREE_SMD_BEGIN + 1)
#define DBTREE_CLASS_SMD_STAB (DBTREE_SMD_BEGIN + 2)

static int
stab_df_hkey_size(struct btr_instance *tins)
{
	return sizeof(int);
}

static void
stab_df_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(int));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
stab_df_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	TMMID(struct smd_nvme_stream_df)	nstream_mmid;
	struct umem_instance			*umm = &tins->ti_umm;

	nstream_mmid = umem_id_u2t(rec->rec_mmid, struct smd_nvme_stream_df);
	if (TMMID_IS_NULL(nstream_mmid))
		return -DER_NONEXIST;

	umem_free_typed(umm, nstream_mmid);
	return 0;
}

static int
stab_df_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
		  daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct smd_nvme_stream_df)	nstream_mmid;
	struct smd_nvme_stream_df		*nstream_df;
	int					*ukey = NULL;

	D_ASSERT(key_iov->iov_len == sizeof(int));
	ukey = (int *)key_iov->iov_buf;
	D_DEBUG(DB_DF, "Allocating device uuid=%d\n", *ukey);

	nstream_mmid = umem_znew_typed(&tins->ti_umm,
				       struct smd_nvme_stream_df);
	if (TMMID_IS_NULL(nstream_mmid))
		return -DER_NOMEM;

	nstream_df = umem_id2ptr_typed(&tins->ti_umm, nstream_mmid);
	nstream_df->ns_map.nsm_stream_id = *ukey;
	memcpy(nstream_df, val_iov->iov_buf, sizeof(*nstream_df));
	rec->rec_mmid = umem_id_t2u(nstream_mmid);
	return 0;
}

static int
stab_df_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_stream_df		*nstream_df = NULL;

	nstream_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(val_iov->iov_buf, nstream_df, sizeof(*nstream_df));
	return 0;
}

static int
stab_df_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_stream_df		*nstream_df;

	nstream_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(nstream_df, val_iov->iov_buf, val_iov->iov_len);
	return 0;
}

static btr_ops_t stab_ops = {
	.to_hkey_size	= stab_df_hkey_size,
	.to_hkey_gen	= stab_df_hkey_gen,
	.to_rec_alloc	= stab_df_rec_alloc,
	.to_rec_free	= stab_df_rec_free,
	.to_rec_fetch	= stab_df_rec_fetch,
	.to_rec_update	= stab_df_rec_update,
};

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
	uuid_copy(ndev_df->nd_info.ndi_dev_id, ukey->uuid);
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

	ndev_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(ndev_df, val_iov->iov_buf, val_iov->iov_len);
	return 0;
}

static btr_ops_t dtab_ops = {
	.to_hkey_size	= dtab_df_hkey_size,
	.to_hkey_gen	= dtab_df_hkey_gen,
	.to_rec_alloc	= dtab_df_rec_alloc,
	.to_rec_free	= dtab_df_rec_free,
	.to_rec_fetch	= dtab_df_rec_fetch,
	.to_rec_update	= dtab_df_rec_update,
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

	npool_df = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(npool_df, val_iov->iov_buf, val_iov->iov_len);
	return 0;
}

static btr_ops_t ptab_ops = {
	.to_hkey_size	= ptab_df_hkey_size,
	.to_hkey_gen	= ptab_df_hkey_gen,
	.to_hkey_cmp	= ptab_df_hkey_cmp,
	.to_rec_alloc	= ptab_df_rec_alloc,
	.to_rec_free	= ptab_df_rec_free,
	.to_rec_fetch	= ptab_df_rec_fetch,
	.to_rec_update	= ptab_df_rec_update,
};

int
smd_nvme_md_tables_register()
{
	int	rc;

	D_DEBUG(DB_DF, "Register persistent metadata device index: %d\n",
		DBTREE_CLASS_SMD_DTAB);

	rc = dbtree_class_register(DBTREE_CLASS_SMD_DTAB, 0, &dtab_ops);
	if (rc)
		D_ERROR("DBTREE DTAB creation failed\n");

	D_DEBUG(DB_DF, "Register peristent metadata pool index: %d\n",
		DBTREE_CLASS_SMD_PTAB);

	rc = dbtree_class_register(DBTREE_CLASS_SMD_PTAB, 0, &ptab_ops);
	if (rc)
		D_ERROR("DBTREE PTAB creation failed\n");

	rc = dbtree_class_register(DBTREE_CLASS_SMD_STAB, 0, &stab_ops);
	if (rc)
		D_ERROR("DBTREE STAB creation failed\n");

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

int
smd_nvme_md_stab_create(struct umem_attr *p_umem_attr,
			struct smd_nvme_stream_tab_df *table_df)
{
	int		rc = 0;
	daos_handle_t	btr_hdl;

	D_ASSERT(table_df->nst_btr.tr_class == 0);
	D_DEBUG(DB_DF, "Create Persistent NVMe MD Device Index, type=%d\n",
		DBTREE_CLASS_SMD_DTAB);

	rc = dbtree_create_inplace(DBTREE_CLASS_SMD_STAB, 0, SMD_STAB_ORDER,
				   p_umem_attr, &table_df->nst_btr, &btr_hdl);
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
