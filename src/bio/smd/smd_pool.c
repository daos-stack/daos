/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <daos/common.h>
#include <daos/dtx.h>
#include "smd_internal.h"

char TABLE_TGTS[SMD_DEV_TYPE_MAX][SMD_DEV_NAME_MAX] = {
	"target",	/* compatible with old version */
	"meta_target",
	"wal_target",
};

char TABLE_POOLS[SMD_DEV_TYPE_MAX][SMD_DEV_NAME_MAX] = {
	"pool",
	"meta_pool",
	"wal_pool",
};

char TABLE_RDBS[SMD_DEV_TYPE_MAX][SMD_DEV_NAME_MAX] = {
	"rdb_data",
	"rdb_meta",
	"rdb_wal",
};

struct smd_pool {
	uint64_t	sp_blob_sz;
	uint16_t	sp_flags;
	uint16_t	sp_tgt_cnt;
	uint32_t	sp_tgts[SMD_MAX_TGT_CNT];
	uint64_t	sp_blobs[SMD_MAX_TGT_CNT];
};

char TABLE_POOLS_EX[SMD_DEV_TYPE_MAX][SMD_DEV_NAME_MAX] = {
	"data_pool_ex",
	"meta_pool_ex",
	"wal_pool_ex",
};

struct smd_pool_meta {
	uint64_t	spm_scm_sz;
	uint64_t	spm_reserved[3];
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

static int
pool_add_tgt(uuid_t pool_id, uint32_t tgt_id, uint64_t blob_id, char *table_name, uint64_t blob_sz)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	/* Fetch pool if it's already existing */
	rc = smd_db_fetch(table_name, &id, sizeof(id), &pool, sizeof(pool));
	if (rc == 0) {
		if (pool.sp_blob_sz != blob_sz) {
			D_ERROR("Pool "DF_UUID" blob size mismatch. "
				""DF_U64" != "DF_U64"\n",
				DP_UUID(&id.uuid), pool.sp_blob_sz,
				blob_sz);
			return -DER_INVAL;
		}

		if (pool.sp_tgt_cnt >= SMD_MAX_TGT_CNT) {
			D_ERROR("Pool "DF_UUID" is assigned to too many "
				"targets (%d)\n", DP_UUID(&id.uuid),
				pool.sp_tgt_cnt);
			return -DER_OVERFLOW;
		}

		rc = smd_pool_find_tgt(&pool, tgt_id);
		if (rc >= 0) {
			D_ERROR("Dup target %d, idx: %d\n", tgt_id, rc);
			return -DER_EXIST;
		}

		pool.sp_tgts[pool.sp_tgt_cnt] = tgt_id;
		pool.sp_blobs[pool.sp_tgt_cnt] = blob_id;
		pool.sp_tgt_cnt += 1;
		if (!strncmp(table_name, TABLE_POOLS[SMD_DEV_TYPE_META], SMD_DEV_NAME_MAX))
			pool.sp_flags |= SMD_POOL_IN_CREATION;

	} else if (rc == -DER_NONEXIST) {
		pool.sp_tgts[0]	 = tgt_id;
		pool.sp_blobs[0] = blob_id;
		pool.sp_tgt_cnt	 = 1;
		pool.sp_blob_sz  = blob_sz;
		pool.sp_flags = 0;
		if (!strncmp(table_name, TABLE_POOLS[SMD_DEV_TYPE_META], SMD_DEV_NAME_MAX))
			pool.sp_flags |= SMD_POOL_IN_CREATION;

	} else {
		D_ERROR("Fetch pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		return rc;
	}

	rc = smd_db_upsert(table_name, &id, sizeof(id), &pool, sizeof(pool));
	if (rc)
		D_ERROR("Update pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));

	return rc;
}

int
smd_pool_add_tgt(uuid_t pool_id, uint32_t tgt_id, uint64_t blob_id,
		 enum smd_dev_type st, uint64_t blob_sz, uint64_t scm_sz)
{
	struct smd_pool_meta	meta = { 0 };
	struct d_uuid		id;
	int			rc;

	smd_db_lock();

	rc = pool_add_tgt(pool_id, tgt_id, blob_id, TABLE_POOLS[st], blob_sz);
	if (rc || scm_sz == 0) {
		smd_db_unlock();
		return rc;
	}

	D_ASSERTF(scm_sz < blob_sz, "scm_sz("DF_U64") >= blob_sz("DF_U64")\n", scm_sz, blob_sz);
	D_ASSERT(st == SMD_DEV_TYPE_META);

	uuid_copy(id.uuid, pool_id);
	/* Fetch pool_meta_ex to see if it's already existing */
	rc = smd_db_fetch(TABLE_POOLS_EX[st], &id, sizeof(id), &meta, sizeof(meta));
	if (rc == 0) {
		if (meta.spm_scm_sz != scm_sz) {
			D_ERROR("Pool "DF_UUID" meta size mismatch. "DF_U64" != "DF_U64"\n",
				DP_UUID(&id.uuid), meta.spm_scm_sz, scm_sz);
			rc = -DER_INVAL;
		}
	} else if (rc == -DER_NONEXIST) {
		meta.spm_scm_sz = scm_sz;
		rc = smd_db_upsert(TABLE_POOLS_EX[st], &id, sizeof(id), &meta, sizeof(meta));
		if (rc)
			DL_ERROR(rc, "Update pool_meta "DF_UUID" failed.", DP_UUID(&id.uuid));
	} else {
		DL_ERROR(rc, "Fetch pool_meta "DF_UUID" failed.", DP_UUID(&id.uuid));
	}

	smd_db_unlock();
	return rc;
}

int
smd_rdb_add_tgt(uuid_t pool_id, uint32_t tgt_id, uint64_t blob_id,
		enum smd_dev_type st, uint64_t blob_sz)
{
	int	rc;

	smd_db_lock();
	rc = pool_add_tgt(pool_id, tgt_id, blob_id, TABLE_RDBS[st], blob_sz);
	smd_db_unlock();

	return rc;
}

static int
pool_del_tgt(uuid_t pool_id, uint32_t tgt_id, char *table_name)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		i;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	rc = smd_db_fetch(table_name, &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		D_ERROR("Fetch pool "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(id.uuid), DP_RC(rc));
		return rc;
	}

	rc = smd_pool_find_tgt(&pool, tgt_id);
	if (rc < 0) {
		D_ERROR("Pool "DF_UUID" target %d not found.\n",
			DP_UUID(id.uuid), tgt_id);
		return -DER_NONEXIST;
	}

	for (i = rc; i < pool.sp_tgt_cnt - 1; i++) {
		pool.sp_tgts[i] = pool.sp_tgts[i + 1];
		pool.sp_blobs[i] = pool.sp_blobs[i + 1];
	}

	pool.sp_tgt_cnt -= 1;
	if (pool.sp_tgt_cnt > 0) {
		rc = smd_db_upsert(table_name, &id, sizeof(id),
				   &pool, sizeof(pool));
		if (rc) {
			D_ERROR("Update pool "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(&id.uuid), DP_RC(rc));
			return rc;
		}
	} else {
		rc = smd_db_delete(table_name, &id, sizeof(id));
		if (rc) {
			D_ERROR("Delete pool "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(&id.uuid), DP_RC(rc));
			return rc;
		}
		rc = 1;	/* Inform caller that last target is deleted */
	}

	return rc;
}

int
smd_pool_del_tgt(uuid_t pool_id, uint32_t tgt_id, enum smd_dev_type st)
{
	struct d_uuid	id;
	int		rc;

	smd_db_lock();
	rc = pool_del_tgt(pool_id, tgt_id, TABLE_POOLS[st]);
	if (rc <= 0)
		goto out;

	rc = 0;
	if (st == SMD_DEV_TYPE_META) {
		uuid_copy(id.uuid, pool_id);
		rc = smd_db_delete(TABLE_POOLS_EX[st], &id, sizeof(id));
		if (rc == -DER_NONEXIST)
			rc = 0;
		else if (rc)
			DL_ERROR(rc, "Delete pool_meta "DF_UUID" failed.", DP_UUID(&id.uuid));
	}
out:
	smd_db_unlock();
	return rc;
}

int
smd_rdb_del_tgt(uuid_t pool_id, uint32_t tgt_id, enum smd_dev_type st)
{
	int	rc;

	smd_db_lock();
	rc = pool_del_tgt(pool_id, tgt_id, TABLE_RDBS[st]);
	smd_db_unlock();

	return rc < 0 ? rc : 0;
}

static struct smd_pool_info *
smd_pool_alloc_info(struct d_uuid *id, struct smd_pool *pools, uint64_t scm_sz)
{
	struct smd_pool_info	*info;
	enum smd_dev_type	 st;
	int			 i;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	info->spi_scm_sz = scm_sz;
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
		info->spi_flags[st] = pools[st].sp_flags;

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
	struct smd_pool_meta	 meta = { 0 };
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

	rc = smd_db_fetch(TABLE_POOLS_EX[SMD_DEV_TYPE_META], &id, sizeof(id), &meta, sizeof(meta));
	if (rc == -DER_NONEXIST) {
		meta.spm_scm_sz = pools[SMD_DEV_TYPE_META].sp_blob_sz;
		rc = 0;
	} else if (rc) {
		DL_ERROR(rc, "Fetch pool_meta "DF_UUID" failed.", DP_UUID(&id.uuid));
		goto out;
	}

	info = smd_pool_alloc_info(&id, pools, meta.spm_scm_sz);
	if (info == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	*pool_info = info;
out:
	smd_db_unlock();
	return rc;
}

static int
pool_get_blob(uuid_t pool_id, uint32_t tgt_id, char *table_name, uint64_t *blob_id)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	smd_db_lock();
	rc = smd_db_fetch(table_name, &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		DL_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DB_MGMT, rc,
			  "Fetch pool " DF_UUID " failed", DP_UUID(&id.uuid));
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

int
smd_pool_get_blob(uuid_t pool_id, uint32_t tgt_id, enum smd_dev_type st, uint64_t *blob_id)
{
	return pool_get_blob(pool_id, tgt_id, TABLE_POOLS[st], blob_id);
}

int
smd_rdb_get_blob(uuid_t pool_id, uint32_t tgt_id, enum smd_dev_type st, uint64_t *blob_id)
{
	return pool_get_blob(pool_id, tgt_id, TABLE_RDBS[st], blob_id);
}

int
smd_rdb_get_blob_sz(uuid_t pool_id, uint64_t *blob_sz)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		rc = 0;

	uuid_copy(id.uuid, pool_id);

	smd_db_lock();
	rc = smd_db_fetch(TABLE_RDBS[SMD_DEV_TYPE_META], &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		D_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DB_MGMT,
			 "Fetch pool "DF_UUID" failed. "DF_RC"\n",
			 DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}
	*blob_sz = pool.sp_blob_sz;
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
	struct smd_pool_meta	 meta = { 0 };
	enum smd_dev_type	 st;
	struct d_uuid            id;
	int                      rc;
	bool			 meta_pool = false;

	if (!strncmp(table, TABLE_POOLS[SMD_DEV_TYPE_META], SMD_DEV_NAME_MAX))
		meta_pool = true;

	D_ASSERT(key->iov_len == sizeof(id));
	id = *(struct d_uuid *)key->iov_buf;
	memset(pools, 0, sizeof(struct smd_pool) * SMD_DEV_TYPE_MAX);
	for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
		rc = smd_db_fetch(TABLE_POOLS[st], &id, sizeof(id), &pools[st], sizeof(pools[st]));
		/* PMDK case, only check data blob */
		if (!meta_pool) {
			if (rc)
				return rc;
			break;
		}

		/* MD-ON-SSD case, data blob might not exist */
		if (rc && (rc != -DER_NONEXIST || st != SMD_DEV_TYPE_DATA))
			return rc;
	}

	rc = smd_db_fetch(TABLE_POOLS_EX[SMD_DEV_TYPE_META], &id, sizeof(id), &meta, sizeof(meta));
	if (rc == -DER_NONEXIST) {
		meta.spm_scm_sz = pools[SMD_DEV_TYPE_META].sp_blob_sz;
		rc = 0;
	} else if (rc) {
		DL_ERROR(rc, "Fetch pool_meta "DF_UUID" failed.", DP_UUID(&id.uuid));
		return rc;
	}

	info = smd_pool_alloc_info(&id, pools, meta.spm_scm_sz);
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
	rc = smd_db_traverse(TABLE_POOLS[SMD_DEV_TYPE_META], smd_pool_list_cb, &td);
	/* No meta found, it might be PMDK case */
	if (rc == 0 && td.td_count == 0)
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

int
smd_pool_mark_ready(uuid_t pool_id)
{
	struct smd_pool	pool;
	struct d_uuid	id;
	int		rc;

	uuid_copy(id.uuid, pool_id);

	smd_db_lock();
	rc = smd_db_fetch(TABLE_POOLS[SMD_DEV_TYPE_META], &id, sizeof(id), &pool, sizeof(pool));
	if (rc == -DER_NONEXIST) {
		D_GOTO(out, rc = 0);
	} else if (rc) {
		D_ERROR("Failed to fetch smd entry of the meta blob for "DF_UUID". "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}

	pool.sp_flags &= ~SMD_POOL_IN_CREATION;

	rc = smd_db_upsert(TABLE_POOLS[SMD_DEV_TYPE_META], &id, sizeof(id), &pool, sizeof(pool));
	if (rc) {
		D_ERROR("Failed to make pool "DF_UUID" as ready in smd. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}
out:
	smd_db_unlock();
	return rc;
}
