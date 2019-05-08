/**
 * (C) Copyright 2017 Intel Corporation.
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
 * ds_pool: Pool IV cache
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>
#include <daos/pool_map.h>
#include "srv_internal.h"
#include <daos_srv/iv.h>

uint32_t
pool_iv_ent_size(int nr)
{
	return pool_buf_size(nr) +
	       sizeof(struct pool_iv_entry) -
	       sizeof(struct pool_buf);
}

static int
pool_iv_value_alloc_internal(d_sg_list_t *sgl)
{
	uint32_t	buf_size;
	uint32_t	pool_nr;
	int		rc;

	rc = daos_sgl_init(sgl, 1);
	if (rc)
		return rc;

	/* XXX Let's use primary group  + 1 domain per target now. */
	crt_group_size(NULL, &pool_nr);
	buf_size = pool_iv_ent_size((int)pool_nr * 2 * 10);
	D_ALLOC(sgl->sg_iovs[0].iov_buf, buf_size);
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	sgl->sg_iovs[0].iov_buf_len = buf_size;
free:
	if (rc)
		daos_sgl_fini(sgl, true);

	return rc;
}

static int
pool_iv_ent_init(struct ds_iv_key *iv_key, void *data,
		 struct ds_iv_entry *entry)
{
	int	rc;

	rc = pool_iv_value_alloc_internal(&entry->iv_value);
	if (rc)
		return rc;

	entry->iv_key.class_id = iv_key->class_id;
	entry->iv_key.rank = iv_key->rank;

	return rc;
}

static int
pool_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
pool_iv_ent_put(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static int
pool_iv_ent_destroy(d_sg_list_t *sgl)
{
	daos_sgl_fini(sgl, true);
	return 0;
}

static int
pool_iv_ent_copy(d_sg_list_t *dst, d_sg_list_t *src)
{
	struct pool_iv_entry *src_iv = src->sg_iovs[0].iov_buf;
	struct pool_iv_entry *dst_iv = dst->sg_iovs[0].iov_buf;

	if (dst_iv == src_iv)
		return 0;

	D_ASSERT(src_iv != NULL);
	D_ASSERT(dst_iv != NULL);

	dst_iv->piv_master_rank = src_iv->piv_master_rank;
	uuid_copy(dst_iv->piv_pool_uuid, src_iv->piv_pool_uuid);
	dst_iv->piv_pool_map_ver = src_iv->piv_pool_map_ver;

	if (src_iv->piv_pool_buf.pb_nr > 0) {
		int src_len = pool_buf_size(src_iv->piv_pool_buf.pb_nr);
		int dst_len = dst->sg_iovs[0].iov_buf_len - sizeof(*dst_iv) +
			      sizeof(struct pool_buf);

		/* copy pool buf */
		if (dst_len < src_len) {
			D_ERROR("dst %d\n src %d\n", dst_len, src_len);
			return -DER_REC2BIG;
		}

		memcpy(&dst_iv->piv_pool_buf, &src_iv->piv_pool_buf, src_len);
	}

	dst->sg_iovs[0].iov_len = src->sg_iovs[0].iov_len;
	D_DEBUG(DB_TRACE, "pool "DF_UUID" map ver %d\n",
		 DP_UUID(dst_iv->piv_pool_uuid), dst_iv->piv_pool_map_ver);
	return 0;
}

static int
pool_iv_ent_fetch(struct ds_iv_entry *entry, d_sg_list_t *dst, d_sg_list_t *src,
		  void **priv)
{
	return pool_iv_ent_copy(dst, src);
}

static int
pool_iv_ent_update(struct ds_iv_entry *entry, d_sg_list_t *dst,
		   d_sg_list_t *src, void **priv)
{
	struct pool_iv_entry	*src_iv = src->sg_iovs[0].iov_buf;
	struct ds_pool		*pool;
	d_rank_t		rank;
	int			rc;

	pool = ds_pool_lookup(src_iv->piv_pool_uuid);
	if (pool == NULL)
		return -DER_NONEXIST;

	rc = crt_group_rank(pool->sp_group, &rank);
	if (rc) {
		ds_pool_put(pool);
		return rc;
	}

	if (rank != src_iv->piv_master_rank) {
		ds_pool_put(pool);
		return -DER_IVCB_FORWARD;
	}

	D_DEBUG(DB_TRACE, "rank %d master rank %d\n", rank,
		src_iv->piv_master_rank);

	/* Update pool map version or pool map */
	rc = ds_pool_tgt_map_update(pool, src_iv->piv_pool_buf.pb_nr > 0 ?
				    &src_iv->piv_pool_buf : NULL,
				    src_iv->piv_pool_map_ver);
	ds_pool_put(pool);

	return pool_iv_ent_copy(dst, src);
}

static int
pool_iv_ent_refresh(d_sg_list_t *dst, d_sg_list_t *src, int ref_rc, void **priv)
{
	struct pool_iv_entry	*dst_iv = dst->sg_iovs[0].iov_buf;
	struct pool_iv_entry	*src_iv = src->sg_iovs[0].iov_buf;
	struct ds_pool		*pool;
	int			rc;

	D_ASSERT(src_iv != NULL);
	D_ASSERT(dst_iv != NULL);
	rc = pool_iv_ent_copy(dst, src);
	if (rc)
		return rc;

	/* Update pool map version or pool map */
	pool = ds_pool_lookup(src_iv->piv_pool_uuid);
	if (pool == NULL) {
		D_WARN("No pool "DF_UUID"\n", DP_UUID(src_iv->piv_pool_uuid));
		return 0;
	}

	rc = ds_pool_tgt_map_update(pool, src_iv->piv_pool_buf.pb_nr > 0 ?
				    &src_iv->piv_pool_buf : NULL,
				    src_iv->piv_pool_map_ver);
	ds_pool_put(pool);

	return rc;
}

static int
pool_iv_value_alloc(struct ds_iv_entry *entry, d_sg_list_t *sgl)
{
	return pool_iv_value_alloc_internal(sgl);
}

struct ds_iv_class_ops pool_iv_ops = {
	.ivc_ent_init	= pool_iv_ent_init,
	.ivc_ent_get	= pool_iv_ent_get,
	.ivc_ent_put	= pool_iv_ent_put,
	.ivc_ent_destroy = pool_iv_ent_destroy,
	.ivc_ent_fetch	= pool_iv_ent_fetch,
	.ivc_ent_update	= pool_iv_ent_update,
	.ivc_ent_refresh = pool_iv_ent_refresh,
	.ivc_value_alloc = pool_iv_value_alloc,
};

int
pool_iv_fetch(void *ns, struct pool_iv_entry *pool_iv)
{
	d_sg_list_t		sgl = { 0 };
	daos_iov_t		iov = { 0 };
	uint32_t		pool_iv_len;
	struct ds_iv_key	key;
	int			rc;

	/* pool_iv == NULL, it means only refreshing local IV cache entry,
	 * i.e. no need fetch the IV value for the caller.
	 */
	if (pool_iv != NULL) {
		pool_iv_len = pool_iv_ent_size(pool_iv->piv_pool_buf.pb_nr);
		daos_iov_set(&iov, pool_iv, pool_iv_len);
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs = &iov;
	}

	memset(&key, 0, sizeof(key));
	key.class_id = IV_POOL_MAP;
	rc = ds_iv_fetch(ns, &key, pool_iv == NULL ? NULL : &sgl);
	if (rc)
		D_ERROR("iv fetch failed %d\n", rc);

	return rc;
}

int
pool_iv_update(void *ns, struct pool_iv_entry *pool_iv,
	       unsigned int shortcut, unsigned int sync_mode)
{
	d_sg_list_t		sgl;
	daos_iov_t		iov;
	uint32_t		pool_iv_len;
	struct ds_iv_key	key;
	int			rc;

	pool_iv_len = pool_iv_ent_size(pool_iv->piv_pool_buf.pb_nr);
	iov.iov_buf = pool_iv;
	iov.iov_len = pool_iv_len;
	iov.iov_buf_len = pool_iv_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_POOL_MAP;
	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0);
	if (rc)
		D_ERROR("iv update failed %d\n", rc);

	return rc;
}

int
pool_iv_invalidate(void *ns, unsigned int shortcut, unsigned int sync_mode)
{
	struct ds_iv_key	key = { 0 };
	int			rc;

	key.class_id = IV_POOL_MAP;
	rc = ds_iv_invalidate(ns, &key, shortcut, sync_mode, 0);
	if (rc)
		D_ERROR("iv invalidate failed %d\n", rc);

	return rc;
}

/* ULT to refresh pool map version */
void
ds_pool_iv_refresh_ult(void *arg)
{
	struct pool_iv_refresh_ult_arg *iv_arg = arg;
	d_rank_t	rank;
	struct ds_pool *pool;
	int rc = 0;

	/* Pool IV fetch should only be done in xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	pool = ds_pool_lookup(iv_arg->iua_pool_uuid);
	if (pool == NULL) {
		rc = -DER_NONEXIST;
		goto out;
	}

	rc = crt_group_rank(pool->sp_group, &rank);
	if (rc)
		goto out;

	if (rank == pool->sp_iv_ns->iv_master_rank) {
		D_WARN("try to refresh pool map on pool leader\n");
		goto out;
	}

	/* If there are already refresh going on, let's wait
	 * until the refresh is done.
	 */
	ABT_mutex_lock(pool->sp_iv_refresh_lock);
	if (pool->sp_map_version >= iv_arg->iua_pool_version &&
	    pool->sp_map != NULL &&
	    !DAOS_FAIL_CHECK(DAOS_FORCE_REFRESH_POOL_MAP)) {
		D_DEBUG(DB_TRACE, "current pool version %u >= %u\n",
			pool_map_get_version(pool->sp_map),
			iv_arg->iua_pool_version);
		goto unlock;
	}

	/* Invalidate the local pool IV cache, then call pool_iv_fetch to
	 * refresh local pool IV cache, which will update the local pool map
	 * see pool_iv_ent_refresh()
	 */
	rc = pool_iv_invalidate(pool->sp_iv_ns, CRT_IV_SHORTCUT_NONE,
				CRT_IV_SYNC_NONE);
	if (rc)
		goto unlock;

	rc = pool_iv_fetch(pool->sp_iv_ns, NULL);

unlock:
	ABT_mutex_unlock(pool->sp_iv_refresh_lock);
out:
	if (iv_arg->iua_eventual)
		ABT_eventual_set(iv_arg->iua_eventual, (void *)&rc, sizeof(rc));
	D_FREE_PTR(iv_arg);
}

int
ds_pool_iv_init(void)
{
	return ds_iv_class_register(IV_POOL_MAP, &iv_cache_ops, &pool_iv_ops);
}

int
ds_pool_iv_fini(void)
{
	return ds_iv_class_unregister(IV_POOL_MAP);
}
