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
#include <daos/dtx.h>
#include <daos_types.h>

#include "smd_internal.h"

#define SMD_STAB_ORDER 72

static int
stab_df_hkey_size(void)
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
	struct umem_instance			*umm = &tins->ti_umm;

	if (UMOFF_IS_NULL(rec->rec_off))
		return -DER_NONEXIST;

	return umem_free_off(umm, rec->rec_off);
}

static int
stab_df_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
		  daos_iov_t *val_iov, struct btr_record *rec)
{
	umem_off_t				 nstream_off;
	struct smd_nvme_stream_df		*nstream_df;
	int					*ukey = NULL;

	D_ASSERT(key_iov->iov_len == sizeof(int));
	ukey = (int *)key_iov->iov_buf;
	D_DEBUG(DB_DF, "Allocating device uuid=%d\n", *ukey);

	nstream_off = umem_zalloc_off(&tins->ti_umm,
				      sizeof(struct smd_nvme_stream_df));
	if (UMOFF_IS_NULL(nstream_off))
		return -DER_NOMEM;

	nstream_df = umem_off2ptr(&tins->ti_umm, nstream_off);
	nstream_df->ns_map.nsm_stream_id = *ukey;
	memcpy(nstream_df, val_iov->iov_buf, sizeof(*nstream_df));
	rec->rec_off = nstream_off;
	return 0;
}

static int
stab_df_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_stream_df		*nstream_df;

	nstream_df = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	memcpy(val_iov->iov_buf, nstream_df, sizeof(*nstream_df));
	return 0;
}

static int
stab_df_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct smd_nvme_stream_df		*nstream_df;
	int					 rc;

	rc = umem_tx_add_off(&tins->ti_umm, rec->rec_off,
			     sizeof(struct smd_nvme_stream_df));
	if (rc != 0)
		return rc;

	nstream_df = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	memcpy(nstream_df, val_iov->iov_buf, val_iov->iov_len);
	return 0;
}

btr_ops_t stab_ops = {
	.to_hkey_size	= stab_df_hkey_size,
	.to_hkey_gen	= stab_df_hkey_gen,
	.to_rec_alloc	= stab_df_rec_alloc,
	.to_rec_free	= stab_df_rec_free,
	.to_rec_fetch	= stab_df_rec_fetch,
	.to_rec_update	= stab_df_rec_update,
};

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

/**
 * Server NMVe add Stream to Device bond SMD stream table
 *
 * \param	[IN]	stream_bond	SMD NVMe device/stream
 *					bond
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int
smd_nvme_add_stream_bond(struct smd_nvme_stream_bond *bond)
{
	struct smd_nvme_stream_df	nvme_stab_args;
	struct smd_store		*store = get_smd_store();
	struct d_uuid			ukey;
	daos_iov_t			key, value;
	int				rc	 = 0;

	if (bond == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Missing input parameters: %d\n", rc);
		return rc;
	}

	/**
	 * Check for device, if not found add a new entry in device
	 * table
	 */
	uuid_copy(ukey.uuid, bond->nsm_dev_id);
	rc = smd_dtab_df_find_update(store, &ukey, SMD_NVME_NORMAL);
	if (rc) {
		D_ERROR("Error in finding/adding to device table\n");
		return rc;
	}

	nvme_stab_args.ns_map = *bond;
	smd_lock(SMD_STAB_LOCK);

	rc = smd_tx_begin(store);
	if (rc != 0)
		goto failed;

	daos_iov_set(&key, &bond->nsm_stream_id, sizeof(bond->nsm_stream_id));
	daos_iov_set(&value, &nvme_stab_args, sizeof(nvme_stab_args));

	rc = dbtree_update(store->sms_stream_tab, &key, &value);

	rc = smd_tx_end(store, rc);
failed:
	if (rc)
		D_ERROR("Adding a device : %d\n", rc);
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
int
smd_nvme_get_stream_bond(int stream_id,
			 struct smd_nvme_stream_bond *bond)
{
	struct smd_nvme_stream_df	nvme_stab_args;
	struct smd_store		*store = get_smd_store();
	daos_iov_t			key, value;
	int				rc	 = 0;

	D_DEBUG(DB_TRACE, "looking up device id in stream table\n");
	if (bond == NULL) {
		rc = -DER_INVAL;
		D_ERROR("Missing input parameters: %d\n", rc);
		return rc;
	}
	daos_iov_set(&key, &stream_id, sizeof(stream_id));
	daos_iov_set(&value, &nvme_stab_args,
		     sizeof(struct smd_nvme_stream_df));

	smd_lock(SMD_STAB_LOCK);
	rc = dbtree_lookup(store->sms_stream_tab, &key, &value);
	smd_unlock(SMD_STAB_LOCK);
	if (!rc)
		*bond = nvme_stab_args.ns_map;
	return rc;
}

int
smd_nvme_list_streams(uint32_t *nr, struct smd_nvme_stream_bond *streams,
		      daos_anchor_t *anchor)
{
	struct smd_store	*store = get_smd_store();
	daos_anchor_t		*probe_hash = NULL;
	dbtree_probe_opc_t	opc;
	daos_handle_t		sti_hdl;
	int			i = 0;
	int			rc = 0;

	D_DEBUG(DB_TRACE, "listing the stream table\n");
	if (streams == NULL || nr == NULL) {
		D_ERROR("Streams array or NR cannot be NULL\n");
		return -DER_INVAL;
	}

	smd_lock(SMD_STAB_LOCK);
	rc = dbtree_iter_prepare(store->sms_stream_tab, 0, &sti_hdl);
	if (rc)
		D_GOTO(out, rc);

	if (anchor != NULL &&  !daos_anchor_is_zero(anchor))
		probe_hash = anchor;

	opc = probe_hash == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GT;
	rc = dbtree_iter_probe(sti_hdl, opc, DAOS_INTENT_DEFAULT, NULL, anchor);
	if (rc != 0)
		D_GOTO(out_eof, rc);

	i = 0;
	while (i < *nr) {
		daos_iov_t	key, value;
		int		stream_id;

		daos_iov_set(&key, &stream_id, sizeof(stream_id));
		daos_iov_set(&value, &streams[i],
			     sizeof(struct smd_nvme_stream_bond));
		rc = dbtree_iter_fetch(sti_hdl, &key, &value, anchor);
		if (rc != 0) {
			D_ERROR("Error while fetching stream info: %d\n", rc);
			break;
		}
		i++;

		rc = dbtree_iter_next(sti_hdl);
		if (rc) {
			if (rc != -DER_NONEXIST)
				D_ERROR("Failed to iterate next: %d\n", rc);
			break;
		}
	}

out_eof:
	*nr = i;
	if (rc == -DER_NONEXIST) {
		anchor ? daos_anchor_set_eof(anchor) : NULL;
		rc = 0;
	}
	dbtree_iter_finish(sti_hdl);
out:
	smd_unlock(SMD_STAB_LOCK);
	return rc;
}
