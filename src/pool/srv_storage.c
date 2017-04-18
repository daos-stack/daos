/**
 * (C) Copyright 2016 Intel Corporation.
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
 * ds_pool: Pool Server Storage
 */
#define DD_SUBSYS	DD_FAC(pool)

#include <daos_srv/pool.h>

#include <daos_srv/daos_mgmt_srv.h>

static DAOS_LIST_HEAD(mpool_cache);
static pthread_mutex_t mpool_cache_lock;

static int
mpool_init(const uuid_t pool_uuid, struct ds_pool_mpool *mp)
{
	PMEMoid	sb_oid;
	char   *path;
	int	rc;

	DAOS_INIT_LIST_HEAD(&mp->mp_entry);
	uuid_copy(mp->mp_uuid, pool_uuid);
	mp->mp_ref = 1;

	rc = pthread_mutex_init(&mp->mp_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize mp_lock: %d\n", rc);
		D_GOTO(err, rc = -DER_NOMEM);
	}

	rc = ds_mgmt_tgt_file(pool_uuid, DSM_META_FILE, NULL, &path);
	if (rc != 0) {
		D_ERROR("failed to lookup path: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	mp->mp_pmem = pmemobj_open(path, DS_POOL_MPOOL_LAYOUT);
	if (mp->mp_pmem == NULL) {
		if (errno == ENOENT)
			D_DEBUG(DF_DSMS, "cannot find %s: %d\n", path, errno);
		else
			D_ERROR("failed to open %s: %d\n", path, errno);
		D_GOTO(err_path, rc = -DER_NONEXIST);
	}

	sb_oid = pmemobj_root(mp->mp_pmem, sizeof(*mp->mp_sb));
	mp->mp_sb = pmemobj_direct(sb_oid);

	if (mp->mp_sb->s_magic != DS_POOL_MPOOL_SB_MAGIC) {
		D_ERROR("found invalid superblock magic: "DF_X64"\n",
			mp->mp_sb->s_magic);
		D_GOTO(err_pmem, rc = -DER_NONEXIST);
	}

	return 0;

err_pmem:
	pmemobj_close(mp->mp_pmem);
err_path:
	free(path);
err_lock:
	pthread_mutex_destroy(&mp->mp_lock);
err:
	return rc;
}

void
ds_pool_mpool_get(struct ds_pool_mpool *mpool)
{
	pthread_mutex_lock(&mpool->mp_lock);
	mpool->mp_ref++;
	pthread_mutex_unlock(&mpool->mp_lock);
}

int
ds_pool_mpool_lookup(const uuid_t pool_uuid, struct ds_pool_mpool **mpool)
{
	struct ds_pool_mpool   *mp;
	int			rc = 0;

	D_DEBUG(DF_DSMS, DF_UUID": looking up\n", DP_UUID(pool_uuid));

	pthread_mutex_lock(&mpool_cache_lock);

	daos_list_for_each_entry(mp, &mpool_cache, mp_entry) {
		if (uuid_compare(mp->mp_uuid, pool_uuid) == 0) {
			D_DEBUG(DF_DSMS, DF_UUID": found %p\n",
				DP_UUID(pool_uuid), mp);
			ds_pool_mpool_get(mp);
			*mpool = mp;
			D_GOTO(out, rc = 0);
		}
	}

	D_ALLOC_PTR(mp);
	if (mp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = mpool_init(pool_uuid, mp);
	if (rc != 0) {
		D_FREE_PTR(mp);
		D_GOTO(out, rc);
	}

	daos_list_add(&mp->mp_entry, &mpool_cache);
	D_DEBUG(DF_DSMS, DF_UUID": allocated %p\n", DP_UUID(pool_uuid), mp);

	*mpool = mp;
out:
	pthread_mutex_unlock(&mpool_cache_lock);
	return rc;
}

void
ds_pool_mpool_put(struct ds_pool_mpool *mpool)
{
	int is_last_ref = 0;

	pthread_mutex_lock(&mpool->mp_lock);
	D_ASSERTF(mpool->mp_ref > 0, "%d\n", mpool->mp_ref);
	if (mpool->mp_ref == 1)
		is_last_ref = 1;
	else
		mpool->mp_ref--;
	pthread_mutex_unlock(&mpool->mp_lock);

	if (is_last_ref) {
		pthread_mutex_lock(&mpool_cache_lock);
		pthread_mutex_lock(&mpool->mp_lock);
		mpool->mp_ref--;
		if (mpool->mp_ref == 0) {
			D_DEBUG(DF_DSMS, "freeing mpool %p\n", mpool);
			pmemobj_close(mpool->mp_pmem);
			pthread_mutex_destroy(&mpool->mp_lock);
			daos_list_del(&mpool->mp_entry);
			D_FREE_PTR(mpool);
		} else {
			pthread_mutex_unlock(&mpool->mp_lock);
		}
		pthread_mutex_unlock(&mpool_cache_lock);
	}
}

int
ds_pool_mpool_cache_init(void)
{
	int rc;

	rc = pthread_mutex_init(&mpool_cache_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize mpool cache lock: %d\n", rc);
		rc = -DER_NOMEM;
	}

	return rc;
}

void
ds_pool_mpool_cache_fini(void)
{
	pthread_mutex_destroy(&mpool_cache_lock);
}
