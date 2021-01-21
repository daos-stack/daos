/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(bio)

#include <daos/common.h>
#include <daos/dtx.h>
#include "smd_internal.h"

struct smd_pool_entry {
	uint64_t	spe_blob_sz;
	uint32_t	spe_tgt_cnt;
	int		spe_tgts[SMD_MAX_TGT_CNT];
	uint64_t	spe_blobs[SMD_MAX_TGT_CNT];
};

static int
get_tgt_idx(struct smd_pool_entry *entry, int tgt_id)
{
	int	i;

	for (i = 0; i < entry->spe_tgt_cnt; i++) {
		if (entry->spe_tgts[i] == tgt_id)
			break;
	}
	return (i == entry->spe_tgt_cnt) ? -1 : i;
}

int
smd_pool_assign(uuid_t pool_id, int tgt_id, uint64_t blob_id, uint64_t blob_sz)
{
	struct smd_pool_entry	entry = { 0 };
	struct d_uuid		key_pool;
	d_iov_t			key, val;
	int			tgt_idx, rc;

	D_ASSERT(daos_handle_is_valid(smd_store.ss_pool_hdl));

	uuid_copy(key_pool.uuid, pool_id);
	smd_lock(&smd_store);

	/* Fetch pool if it's already existing */
	d_iov_set(&key, &key_pool, sizeof(key_pool));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_pool_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc == 0) {
		if (entry.spe_blob_sz != blob_sz) {
			D_ERROR("Pool "DF_UUID" blob size mismatch. "
				""DF_U64" != "DF_U64"\n",
				DP_UUID(&key_pool.uuid), entry.spe_blob_sz,
				blob_sz);
			rc = -DER_INVAL;
			goto out;
		}

		if (entry.spe_tgt_cnt >= SMD_MAX_TGT_CNT) {
			D_ERROR("Pool "DF_UUID" is assigned to too many "
				"targets (%d)\n", DP_UUID(&key_pool.uuid),
				entry.spe_tgt_cnt);
			rc = -DER_OVERFLOW;
			goto out;
		}

		tgt_idx = get_tgt_idx(&entry, tgt_id);
		if (tgt_idx != -1) {
			D_ERROR("Dup target %d, idx: %d\n", tgt_id, tgt_idx);
			rc = -DER_EXIST;
			goto out;
		}

		entry.spe_tgts[entry.spe_tgt_cnt] = tgt_id;
		entry.spe_blobs[entry.spe_tgt_cnt] = blob_id;
		entry.spe_tgt_cnt += 1;
	} else if (rc == -DER_NONEXIST) {
		entry.spe_tgts[0] = tgt_id;
		entry.spe_blobs[0] = blob_id;
		entry.spe_tgt_cnt = 1;
		entry.spe_blob_sz = blob_sz;
	} else {
		D_ERROR("Fetch pool "DF_UUID" failed. %d\n",
			DP_UUID(&key_pool.uuid), rc);
		goto out;
	}

	rc = dbtree_update(smd_store.ss_pool_hdl, &key, &val);
	if (rc) {
		D_ERROR("Update pool "DF_UUID" failed. %d\n",
			DP_UUID(&key_pool.uuid), rc);
		goto out;
	}
out:
	smd_unlock(&smd_store);
	return rc;
}

int
smd_pool_unassign(uuid_t pool_id, int tgt_id)
{
	struct smd_pool_entry	 entry = { 0 };
	struct d_uuid		 key_pool;
	d_iov_t			 key, val;
	int			*tgt_src, *tgt_tgt;
	uint64_t		*blob_src, *blob_tgt;
	int			 tgt_idx, rc, move_cnt;

	D_ASSERT(daos_handle_is_valid(smd_store.ss_pool_hdl));

	uuid_copy(key_pool.uuid, pool_id);
	smd_lock(&smd_store);

	d_iov_set(&key, &key_pool, sizeof(key_pool));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_pool_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_ERROR("Fetch pool "DF_UUID" failed. %d\n",
			DP_UUID(key_pool.uuid), rc);
		goto out;
	}

	tgt_idx = get_tgt_idx(&entry, tgt_id);
	if (tgt_idx == -1) {
		D_ERROR("Pool "DF_UUID" target %d not found.\n",
			DP_UUID(key_pool.uuid), tgt_id);
		rc = -DER_NONEXIST;
		goto out;
	}

	tgt_src = &entry.spe_tgts[tgt_idx + 1];
	tgt_tgt = &entry.spe_tgts[tgt_idx];
	blob_src = &entry.spe_blobs[tgt_idx + 1];
	blob_tgt = &entry.spe_blobs[tgt_idx];

	D_ASSERT(entry.spe_tgt_cnt >= (tgt_idx + 1));
	move_cnt = entry.spe_tgt_cnt - 1 - tgt_idx;
	entry.spe_tgt_cnt -= 1;

	if (move_cnt && entry.spe_tgt_cnt) {
		memmove(tgt_tgt, tgt_src, move_cnt * sizeof(int));
		memmove(blob_tgt, blob_src, move_cnt * sizeof(uint64_t));
	}

	if (entry.spe_tgt_cnt) {
		rc = dbtree_update(smd_store.ss_pool_hdl, &key, &val);
		if (rc)
			D_ERROR("Update pool "DF_UUID" failed. %d\n",
				DP_UUID(&key_pool.uuid), rc);
	} else {
		rc = dbtree_delete(smd_store.ss_pool_hdl, BTR_PROBE_EQ, &key,
				   NULL);
		if (rc)
			D_ERROR("Delete pool "DF_UUID" failed. %d\n",
				DP_UUID(&key_pool.uuid), rc);
	}
out:
	smd_unlock(&smd_store);
	return rc;
}

static struct smd_pool_info *
create_pool_info(uuid_t pool_id, struct smd_pool_entry *entry)
{
	struct smd_pool_info	*info;
	int			 i;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	D_ALLOC_ARRAY(info->spi_tgts, SMD_MAX_TGT_CNT);
	if (info->spi_tgts == NULL) {
		D_FREE(info);
		return NULL;
	}
	D_ALLOC_ARRAY(info->spi_blobs, SMD_MAX_TGT_CNT);
	if (info->spi_blobs == NULL) {
		D_FREE(info->spi_tgts);
		D_FREE(info);
		return NULL;
	}

	D_INIT_LIST_HEAD(&info->spi_link);
	uuid_copy(info->spi_id, pool_id);
	info->spi_blob_sz = entry->spe_blob_sz;
	info->spi_tgt_cnt = entry->spe_tgt_cnt;
	for (i = 0; i < info->spi_tgt_cnt; i++) {
		info->spi_tgts[i] = entry->spe_tgts[i];
		info->spi_blobs[i] = entry->spe_blobs[i];
	}

	return info;
}

int
smd_pool_get(uuid_t pool_id, struct smd_pool_info **pool_info)
{
	struct smd_pool_info	*info;
	struct smd_pool_entry	 entry = { 0 };
	struct d_uuid		 key_pool;
	d_iov_t			 key, val;
	int			 rc;

	D_ASSERT(daos_handle_is_valid(smd_store.ss_pool_hdl));

	uuid_copy(key_pool.uuid, pool_id);
	smd_lock(&smd_store);

	d_iov_set(&key, &key_pool, sizeof(key_pool));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_pool_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_ERROR("Fetch pool "DF_UUID" failed. %d\n",
			DP_UUID(&key_pool.uuid), rc);
		goto out;
	}

	info = create_pool_info(pool_id, &entry);
	if (info == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	*pool_info = info;
out:
	smd_unlock(&smd_store);
	return rc;
}

int
smd_pool_get_blob(uuid_t pool_id, int tgt_id, uint64_t *blob_id)
{
	struct smd_pool_entry	entry = { 0 };
	struct d_uuid		key_pool;
	d_iov_t			key, val;
	int			tgt_idx, rc;

	D_ASSERT(daos_handle_is_valid(smd_store.ss_pool_hdl));

	uuid_copy(key_pool.uuid, pool_id);
	smd_lock(&smd_store);

	d_iov_set(&key, &key_pool, sizeof(key_pool));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_pool_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DB_MGMT,
			 "Fetch pool "DF_UUID" failed. %d\n",
			 DP_UUID(&key_pool.uuid), rc);
		goto out;
	}

	tgt_idx = get_tgt_idx(&entry, tgt_id);
	if (tgt_idx == -1) {
		D_DEBUG(DB_MGMT, "Pool "DF_UUID" target %d not found.\n",
			DP_UUID(key_pool.uuid), tgt_id);
		rc = -DER_NONEXIST;
		goto out;
	}
	*blob_id = entry.spe_blobs[tgt_idx];
out:
	smd_unlock(&smd_store);
	return rc;
}

int
smd_pool_list(d_list_t *pool_list, int *pools)
{
	struct smd_pool_info	*info;
	struct smd_pool_entry	 entry = { 0 };
	daos_handle_t		 iter_hdl;
	struct d_uuid		 key_pool;
	d_iov_t			 key, val;
	int			 pool_cnt = 0;
	int			 rc;

	D_ASSERT(pool_list && d_list_empty(pool_list));
	D_ASSERT(daos_handle_is_valid(smd_store.ss_pool_hdl));

	smd_lock(&smd_store);

	rc = dbtree_iter_prepare(smd_store.ss_pool_hdl, 0, &iter_hdl);
	if (rc) {
		D_ERROR("Prepare pool iterator failed. "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = dbtree_iter_probe(iter_hdl, BTR_PROBE_FIRST, DAOS_INTENT_DEFAULT,
			       NULL, NULL);
	if (rc) {
		if (rc != -DER_NONEXIST)
			D_ERROR("Probe first pool failed. "DF_RC"\n",
				DP_RC(rc));
		else
			rc = 0;
		goto done;
	}

	d_iov_set(&key, &key_pool, sizeof(key_pool));
	d_iov_set(&val, &entry, sizeof(entry));

	while (1) {
		rc = dbtree_iter_fetch(iter_hdl, &key, &val, NULL);
		if (rc != 0) {
			D_ERROR("Iterate fetch failed. "DF_RC"\n", DP_RC(rc));
			break;
		}

		info = create_pool_info(key_pool.uuid, &entry);
		if (info == NULL) {
			rc = -DER_NOMEM;
			D_ERROR("Create pool info failed. "DF_RC"\n",
				DP_RC(rc));
			break;
		}
		d_list_add_tail(&info->spi_link, pool_list);

		pool_cnt++;
		rc = dbtree_iter_next(iter_hdl);
		if (rc) {
			if (rc != -DER_NONEXIST)
				D_ERROR("Iterate next failed. "DF_RC"\n",
					DP_RC(rc));
			else
				rc = 0;
			break;
		}
	}
done:
	dbtree_iter_finish(iter_hdl);
out:
	smd_unlock(&smd_store);

	/* return pool count along with the pool list */
	*pools = pool_cnt;

	return rc;
}

/* smd_lock() and smd_tx_begin() are called by caller */
int
smd_replace_blobs(struct smd_pool_info *info, uint32_t tgt_cnt, int *tgts)
{
	struct smd_pool_entry	entry = { 0 };
	struct d_uuid		key_pool;
	d_iov_t			key, val;
	int			tgt_id, tgt_idx;
	int			i, rc;

	D_ASSERT(daos_handle_is_valid(smd_store.ss_pool_hdl));

	uuid_copy(key_pool.uuid, info->spi_id);

	d_iov_set(&key, &key_pool, sizeof(key_pool));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_pool_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_ERROR("Fetch pool "DF_UUID" failed. %d\n",
			DP_UUID(&key_pool.uuid), rc);
		return rc;
	}

	D_ASSERT(info->spi_blob_sz == entry.spe_blob_sz);
	D_ASSERT(info->spi_tgt_cnt == entry.spe_tgt_cnt);
	D_ASSERT(entry.spe_tgt_cnt >= tgt_cnt);

	for (i = 0; i < tgt_cnt; i++) {
		tgt_id = tgts[i];
		tgt_idx = get_tgt_idx(&entry, tgt_id);
		if (tgt_idx == -1) {
			D_ERROR("Invalid tgt %d for pool "DF_UUID"\n",
				tgt_id, DP_UUID(&key_pool.uuid));
			return -DER_INVAL;
		}

		entry.spe_blobs[tgt_idx] = info->spi_blobs[tgt_idx];
	}

	rc = dbtree_update(smd_store.ss_pool_hdl, &key, &val);
	if (rc)
		D_ERROR("Replace blobs for pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&key_pool.uuid), DP_RC(rc));

	return rc;
}
