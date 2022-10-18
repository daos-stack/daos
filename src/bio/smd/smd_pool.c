/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <daos/common.h>
#include <daos/dtx.h>
#include "smd_internal.h"

char *TABLE_TGTS[SMD_DEV_TYPE_MAX] = {
	"target",	/* compatible with old version */
	"meta_target",
	"wal_target",
};

char *TABLE_POOLS[SMD_DEV_TYPE_MAX] = {
	"pool",
	"meta_pool",
	"wal_pool",
};


struct smd_pool {
	uint64_t	sp_blob_sz;
	uint32_t	sp_tgt_cnt;
	uint32_t	sp_tgts[SMD_MAX_TGT_CNT];
	uint64_t	sp_blobs[SMD_MAX_TGT_CNT];
};

static int
smd_pool_find_tgt(struct smd_pool *pool, int tgt_id)
{
	int	i;

	for (i = 0; i < pool->sp_tgt_cnt; i++) {
		if (pool->sp_tgts[i] == tgt_id)
			return i;
	}
	return -1;
}

int
smd_pool_add_tgt(uuid_t pool_id, uint32_t tgt_id, uint64_t blob_id,
		 enum smd_dev_type smd_type, uint64_t blob_sz)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	smd_db_lock();
	/* Fetch pool if it's already existing */
	rc = smd_db_fetch(TABLE_POOLS[smd_type], &id, sizeof(id), &pool, sizeof(pool));
	if (rc == 0) {
		if (pool.sp_blob_sz != blob_sz) {
			D_ERROR("Pool "DF_UUID" blob size mismatch. "
				""DF_U64" != "DF_U64"\n",
				DP_UUID(&id.uuid), pool.sp_blob_sz,
				blob_sz);
			rc = -DER_INVAL;
			goto out;
		}

		if (pool.sp_tgt_cnt >= SMD_MAX_TGT_CNT) {
			D_ERROR("Pool "DF_UUID" is assigned to too many "
				"targets (%d)\n", DP_UUID(&id.uuid),
				pool.sp_tgt_cnt);
			rc = -DER_OVERFLOW;
			goto out;
		}

		rc = smd_pool_find_tgt(&pool, tgt_id);
		if (rc >= 0) {
			D_ERROR("Dup target %d, idx: %d\n", tgt_id, rc);
			rc = -DER_EXIST;
			goto out;
		}

		pool.sp_tgts[pool.sp_tgt_cnt] = tgt_id;
		pool.sp_blobs[pool.sp_tgt_cnt] = blob_id;
		pool.sp_tgt_cnt += 1;

	} else if (rc == -DER_NONEXIST) {
		pool.sp_tgts[0]	 = tgt_id;
		pool.sp_blobs[0] = blob_id;
		pool.sp_tgt_cnt	 = 1;
		pool.sp_blob_sz  = blob_sz;

	} else {
		D_ERROR("Fetch pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}

	rc = smd_db_upsert(TABLE_POOLS[smd_type], &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		D_ERROR("Update pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}
out:
	smd_db_unlock();
	return rc;
}

int
smd_pool_del_tgt(uuid_t pool_id, uint32_t tgt_id, enum smd_dev_type smd_type)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		i;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	smd_db_lock();
	rc = smd_db_fetch(TABLE_POOLS[smd_type], &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		D_ERROR("Fetch pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(id.uuid), DP_RC(rc));
		goto out;
	}

	rc = smd_pool_find_tgt(&pool, tgt_id);
	if (rc < 0) {
		D_ERROR("Pool "DF_UUID" target %d not found.\n",
			DP_UUID(id.uuid), tgt_id);
		rc = -DER_NONEXIST;
		goto out;
	}

	for (i = rc; i < pool.sp_tgt_cnt - 1; i++) {
		pool.sp_tgts[i] = pool.sp_tgts[i + 1];
		pool.sp_blobs[i] = pool.sp_blobs[i + 1];
	}

	pool.sp_tgt_cnt -= 1;
	if (pool.sp_tgt_cnt > 0) {
		rc = smd_db_upsert(TABLE_POOLS[smd_type], &id, sizeof(id),
				   &pool, sizeof(pool));
		if (rc) {
			D_ERROR("Update pool "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(&id.uuid), DP_RC(rc));
			goto out;
		}
	} else {
		rc = smd_db_delete(TABLE_POOLS[smd_type], &id, sizeof(id));
		if (rc) {
			D_ERROR("Delete pool "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(&id.uuid), DP_RC(rc));
			goto out;
		}
	}
out:
	smd_db_unlock();
	return rc;
}

static struct smd_pool_info *
smd_pool_alloc_info(struct d_uuid *id, struct smd_pool *pools)
{
	struct smd_pool_info	*info;
	enum smd_dev_type	 st;
	int			 i;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		D_ALLOC_ARRAY(info->spi_tgts[st], SMD_MAX_TGT_CNT);
		if (info->spi_tgts[st] == NULL) {
			smd_pool_free_info(info);
			return NULL;
		}
		D_ALLOC_ARRAY(info->spi_blobs[st], SMD_MAX_TGT_CNT);
		if (info->spi_blobs[st] == NULL) {
			smd_pool_free_info(info);
			return NULL;
		}
		info->spi_blob_sz[st] = pools[st].sp_blob_sz;
		info->spi_tgt_cnt[st] = pools[st].sp_tgt_cnt;

		for (i = 0; i < pools[st].sp_tgt_cnt; i++) {
			info->spi_tgts[st][i] = pools[st].sp_tgts[i];
			info->spi_blobs[st][i] = pools[st].sp_blobs[i];
		}
	}

	D_INIT_LIST_HEAD(&info->spi_link);
	uuid_copy(info->spi_id, id->uuid);
	return info;
}

int
smd_pool_get_info(uuid_t pool_id, struct smd_pool_info **pool_info)
{
	struct smd_pool_info	*info;
	struct smd_pool          pools[SMD_DEV_TYPE_MAX];
	enum smd_dev_type	 st;
	struct d_uuid		 id;
	int			 rc;

	memset(pools, 0, sizeof(struct smd_pool) * SMD_DEV_TYPE_MAX);
	uuid_copy(id.uuid, pool_id);
	smd_db_lock();
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		rc = smd_db_fetch(TABLE_POOLS[st], &id, sizeof(id), &pools[st], sizeof(pools[st]));
		/* META and WAL are optional */
		rc = (rc == -DER_NONEXIST && st > SMD_DEV_TYPE_DATA) ? 0 : rc;
		if (rc) {
			D_ERROR("Fetch pool "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(&id.uuid), DP_RC(rc));
			goto out;
		}
	}

	info = smd_pool_alloc_info(&id, pools);
	if (info == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	*pool_info = info;
out:
	smd_db_unlock();
	return rc;
}

int
smd_pool_get_blob(uuid_t pool_id, uint32_t tgt_id, enum smd_dev_type smd_type, uint64_t *blob_id)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	smd_db_lock();
	rc = smd_db_fetch(TABLE_POOLS[smd_type], &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		D_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DB_MGMT,
			 "Fetch pool "DF_UUID" failed. "DF_RC"\n",
			 DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}

	rc = smd_pool_find_tgt(&pool, tgt_id);
	if (rc < 0) {
		D_DEBUG(DB_MGMT, "Pool "DF_UUID" target %d not found.\n",
			DP_UUID(id.uuid), tgt_id);
		rc = -DER_NONEXIST;
		goto out;
	}
	*blob_id = pool.sp_blobs[rc];
	rc = 0;
out:
	smd_db_unlock();
	return rc;
}

static int
smd_pool_list_cb(struct sys_db *db, char *table, d_iov_t *key, void *args)
{
	struct smd_trav_data    *td = args;
	struct smd_pool_info    *info;
	struct smd_pool          pools[SMD_DEV_TYPE_MAX];
	enum smd_dev_type	 st;
	struct d_uuid            id;
	int                      rc;

	D_ASSERT(key->iov_len == sizeof(id));
	id = *(struct d_uuid *)key->iov_buf;
	memset(pools, 0, sizeof(struct smd_pool) * SMD_DEV_TYPE_MAX);
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		rc = smd_db_fetch(TABLE_POOLS[st], &id, sizeof(id), &pools[st], sizeof(pools[st]));
		if (rc && !(rc == -DER_NONEXIST && st > SMD_DEV_TYPE_DATA))
			return rc;
	}

	info = smd_pool_alloc_info(&id, pools);
	if (!info)
		return -DER_NOMEM;

	d_list_add_tail(&info->spi_link, &td->td_list);
	td->td_count++;
	return 0;
}

int
smd_pool_list(d_list_t *pool_list, int *pools)
{
	struct smd_trav_data td;
	int		     rc;

	td.td_count = 0;
	D_INIT_LIST_HEAD(&td.td_list);

	if (!smd_db_ready())
		return 0; /* There is no NVMe, smd will not be initialized */

	smd_db_lock();
	rc = smd_db_traverse(TABLE_POOLS[SMD_DEV_TYPE_DATA], smd_pool_list_cb, &td);
	smd_db_unlock();

	if (rc == 0) { /* success */
		*pools = td.td_count;
		d_list_splice_init(&td.td_list, pool_list);
	}

	while (!d_list_empty(&td.td_list)) {
		struct smd_pool_info	*info;

		info = d_list_entry(td.td_list.next, struct smd_pool_info,
				    spi_link);
		d_list_del(&info->spi_link);
		smd_pool_free_info(info);

	}
	return rc;
}

/* smd_lock() and smd_tx_begin() are called by caller */
int
smd_pool_replace_blobs_locked(struct smd_pool_info *info, int tgt_cnt,
			      uint32_t *tgts)
{
	struct smd_pool         pools[SMD_DEV_TYPE_MAX];
	enum smd_dev_type	st;
	struct d_uuid		id;
	int			info_tgt_cnt = 0;
	int			i, rc;

	memset(pools, 0, sizeof(struct smd_pool) * SMD_DEV_TYPE_MAX);
	uuid_copy(id.uuid, info->spi_id);
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		rc = smd_db_fetch(TABLE_POOLS[st], &id, sizeof(id),
				&pools[st], sizeof(pools[st]));
		if (rc && !(rc == -DER_NONEXIST && st > SMD_DEV_TYPE_DATA)) {
			D_ERROR("Fetch pool "DF_UUID" failed. %d\n",
				DP_UUID(&id.uuid), rc);
			return rc;
		}

		D_ASSERT(info->spi_blob_sz[st] == pools[st].sp_blob_sz);
		D_ASSERT(info->spi_tgt_cnt[st] == pools[st].sp_tgt_cnt);
		info_tgt_cnt += info->spi_tgt_cnt[st];
	}

	D_ASSERT(info_tgt_cnt >= tgt_cnt);

	for (i = 0; i < tgt_cnt; i++) {
		int	tgt_id;
		int	tgt_idx;

		tgt_id = tgts[i];
		for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
			if (pools[st].sp_tgt_cnt) {
				tgt_idx = smd_pool_find_tgt(&pools[st], tgt_id);
				if (tgt_idx < 0) {
					D_ERROR("Invalid tgt %d for pool "DF_UUID"\n",
						tgt_id, DP_UUID(&id.uuid));
					return -DER_INVAL;
				}
				pools[st].sp_blobs[tgt_idx] = info->spi_blobs[st][tgt_idx];
			}
		}
	}

	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		if (pools[st].sp_tgt_cnt) {
			rc = smd_db_upsert(TABLE_POOLS[st], &id, sizeof(id),
					&pools[st], sizeof(pools[st]));
			if (rc) {
				D_ERROR("Replace blobs for pool "DF_UUID" failed. "DF_RC"\n",
					DP_UUID(&id.uuid), DP_RC(rc));
			}
		}
	}
	return rc;
}
