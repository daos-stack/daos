/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * ds_pool: Target Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related target states.
 *
 * Data structures used here:
 *
 *                 Pool           Container
 *
 *         Global  ds_pool
 *                 ds_pool_hdl
 *
 *   Thread-local  ds_pool_child  ds_cont
 *                                ds_cont_hdl
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>

#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/vos.h>
#include <daos_srv/rebuild.h>
#include "rpc.h"
#include "srv_internal.h"

/* ds_pool_child **************************************************************/

struct ds_pool_child *
ds_pool_child_lookup(const uuid_t uuid)
{
	struct ds_pool_child   *child;
	struct pool_tls	       *tls = pool_tls_get();

	d_list_for_each_entry(child, &tls->dt_pool_list, spc_list) {
		if (uuid_compare(uuid, child->spc_uuid) == 0) {
			child->spc_ref++;
			return child;
		}
	}
	return NULL;
}

struct ds_pool_child *
ds_pool_child_get(struct ds_pool_child *child)
{
	child->spc_ref++;
	return child;
}

void
ds_pool_child_put(struct ds_pool_child *child)
{
	D_ASSERTF(child->spc_ref > 0, "%d\n", child->spc_ref);
	child->spc_ref--;
	if (child->spc_ref == 0) {
		D_DEBUG(DF_DSMS, DF_UUID": destroying\n",
			DP_UUID(child->spc_uuid));
		D_ASSERT(d_list_empty(&child->spc_list));
		vos_pool_close(child->spc_hdl);
		D_FREE(child);
	}
}

void
ds_pool_child_purge(struct pool_tls *tls)
{
	struct ds_pool_child   *child;
	struct ds_pool_child   *n;

	d_list_for_each_entry_safe(child, n, &tls->dt_pool_list, spc_list) {
		D_ASSERTF(child->spc_ref == 1, DF_UUID": %d\n",
			  DP_UUID(child->spc_uuid), child->spc_ref);
		d_list_del_init(&child->spc_list);
		ds_pool_child_put(child);
	}
}

struct pool_child_lookup_arg {
	void	       *pla_uuid;
	uint32_t	pla_map_version;
};

/*
 * Called via dss_thread_collective() to create and add the ds_pool_child object
 * for one thread. This opens the matching VOS pool.
 */
int
ds_pool_child_open(uuid_t uuid, unsigned int version)
{
	struct pool_tls		       *tls = pool_tls_get();
	struct ds_pool_child	       *child;
	struct dss_module_info	       *info = dss_get_module_info();
	char			       *path;
	int				rc;

	child = ds_pool_child_lookup(uuid);
	if (child != NULL) {
		ds_pool_child_put(child);
		return 0;
	}

	D_DEBUG(DF_DSMS, DF_UUID": creating\n", DP_UUID(uuid));

	D_ALLOC_PTR(child);
	if (child == NULL)
		return -DER_NOMEM;

	rc = ds_mgmt_tgt_file(uuid, VOS_FILE, &info->dmi_tgt_id, &path);
	if (rc != 0) {
		D_FREE(child);
		return rc;
	}

	rc = vos_pool_open(path, uuid, &child->spc_hdl);

	D_FREE(path);

	if (rc != 0) {
		D_FREE(child);
		return rc;
	}

	uuid_copy(child->spc_uuid, uuid);
	child->spc_map_version = version;
	child->spc_ref = 1; /* 1 for the list */

	d_list_add(&child->spc_list, &tls->dt_pool_list);

	return 0;
}

/*
 * Called via dss_collective() to create and add the ds_pool_child object for
 * one thread. This opens the matching VOS pool.
 */
static int
pool_child_add_one(void *varg)
{
	struct pool_child_lookup_arg   *arg = varg;

	return ds_pool_child_open(arg->pla_uuid, arg->pla_map_version);
}

int
ds_pool_child_close(uuid_t uuid)
{
	struct ds_pool_child *child;

	child = ds_pool_child_lookup(uuid);
	if (child == NULL)
		return 0;

	d_list_del_init(&child->spc_list);
	ds_pool_child_put(child); /* -1 for the list */
	ds_pool_child_put(child); /* -1 for lookup */
	return 0;
}

/*
 * Called via dss_thread_collective() to delete the ds_pool_child object for one
 * thread. If nobody else is referencing this object, then its VOS pool handle
 * is closed and the object itself is freed.
 */
static int
pool_child_delete_one(void *uuid)
{
	return ds_pool_child_close(uuid);
}

/* ds_pool ********************************************************************/

static struct daos_lru_cache   *pool_cache;
static ABT_mutex		pool_cache_lock;

static inline struct ds_pool *
pool_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_pool, sp_entry);
}

static int
pool_alloc_ref(void *key, unsigned int ksize, void *varg,
	       struct daos_llink **link)
{
	struct ds_pool_create_arg      *arg = varg;
	struct ds_pool		       *pool;
	struct pool_child_lookup_arg	collective_arg;
	int				rc;
	int				rc_tmp;

	D_DEBUG(DF_DSMS, DF_UUID": creating\n", DP_UUID(key));

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	rc = ABT_rwlock_create(&pool->sp_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_pool, rc = dss_abterr2der(rc));

	uuid_copy(pool->sp_uuid, key);
	pool->sp_map_version = arg->pca_map_version;

	if (arg->pca_map != NULL) {
		rc = pl_map_update(pool->sp_uuid, arg->pca_map, true);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to update pl_map: %d\n",
				DP_UUID(key), rc);
			D_GOTO(err_lock, rc);
		}

		pool->sp_map = arg->pca_map;
	}

	collective_arg.pla_uuid = key;
	collective_arg.pla_map_version = arg->pca_map_version;

	rc = dss_thread_collective(pool_child_add_one, &collective_arg, 0);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to add ES pool caches: %d\n",
			DP_UUID(key), rc);
		goto err_map;
	}

	if (arg->pca_need_group) {
		char id[DAOS_UUID_STR_SIZE];

		uuid_unparse_lower(key, id);
		pool->sp_group = crt_group_lookup(id);
		if (pool->sp_group == NULL) {
			D_ERROR(DF_UUID": pool group not found\n",
				DP_UUID(key));
			D_GOTO(err_collective, rc = -DER_NONEXIST);
		}
	}

	*link = &pool->sp_entry;
	return 0;

err_collective:
	rc_tmp = dss_thread_collective(pool_child_delete_one, key, 0);
	D_ASSERTF(rc_tmp == 0, "%d\n", rc_tmp);
err_map:
	pl_map_disconnect(pool->sp_uuid);
err_lock:
	ABT_rwlock_free(&pool->sp_lock);
err_pool:
	D_FREE(pool);
err:
	return rc;
}

static void
pool_free_ref(struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);
	int		rc;

	D_DEBUG(DF_DSMS, DF_UUID": freeing\n", DP_UUID(pool->sp_uuid));

	if (pool->sp_group != NULL) {
#if 0
		rc = ds_pool_group_destroy(pool->sp_uuid, pool->sp_group);
		if (rc != 0)
			D_ERROR(DF_UUID": failed to destroy pool group %s: "
				"%d\n", DP_UUID(pool->sp_uuid),
				pool->sp_group->cg_grpid, rc);
#else
		pool->sp_group = NULL;
#endif
	}

	if (pool->sp_iv_ns != NULL)
		ds_iv_ns_destroy(pool->sp_iv_ns);

	rc = dss_thread_collective(pool_child_delete_one, pool->sp_uuid, 0);
	if (rc == -DER_CANCELED)
		D_DEBUG(DB_MD, DF_UUID": no ESs\n", DP_UUID(pool->sp_uuid));
	else if (rc != 0)
		D_ERROR(DF_UUID": failed to delete ES pool caches: %d\n",
			DP_UUID(pool->sp_uuid), rc);

	pl_map_disconnect(pool->sp_uuid);
	if (pool->sp_map != NULL)
		pool_map_decref(pool->sp_map);

	ABT_rwlock_free(&pool->sp_lock);
	D_FREE(pool);
}

static bool
pool_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);

	return uuid_compare(key, pool->sp_uuid) == 0;
}

static struct daos_llink_ops pool_cache_ops = {
	.lop_alloc_ref	= pool_alloc_ref,
	.lop_free_ref	= pool_free_ref,
	.lop_cmp_keys	= pool_cmp_keys
};

int
ds_pool_cache_init(void)
{
	int rc;

	rc = ABT_mutex_create(&pool_cache_lock);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = daos_lru_cache_create(-1 /* bits */, D_HASH_FT_NOLOCK /* feats */,
				     &pool_cache_ops, &pool_cache);
	if (rc != 0)
		ABT_mutex_free(&pool_cache_lock);
	return rc;
}

void
ds_pool_cache_fini(void)
{
	ABT_mutex_lock(pool_cache_lock);
	daos_lru_cache_destroy(pool_cache);
	ABT_mutex_unlock(pool_cache_lock);
	ABT_mutex_free(&pool_cache_lock);
}

/*
 * If "arg == NULL", then this is assumed to be a pure lookup. In this case,
 * -DER_NONEXIST is returned if the ds_pool object does not exist in the cache.
 * A group is only created if "arg->pca_create_group != 0".
 */
int
ds_pool_lookup_create(const uuid_t uuid, struct ds_pool_create_arg *arg,
		      struct ds_pool **pool)
{
	struct daos_llink      *llink;
	int			rc;

	D_ASSERT(arg == NULL || !arg->pca_need_group || arg->pca_map != NULL);

	ABT_mutex_lock(pool_cache_lock);
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t),
			       arg, &llink);
	ABT_mutex_unlock(pool_cache_lock);
	if (rc != 0) {
		if (arg == NULL && rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, DF_UUID": pure lookup failed: %d\n",
				DP_UUID(uuid), rc);
		else
			D_ERROR(DF_UUID": failed to lookup%s pool: %d\n",
				DP_UUID(uuid), arg == NULL ? "" : "/create",
				rc);
		return rc;
	}

	*pool = pool_obj(llink);
	return 0;
}

struct ds_pool *
ds_pool_lookup(const uuid_t uuid)
{
	struct ds_pool *pool;
	int		rc;

	rc = ds_pool_lookup_create(uuid, NULL /* arg */, &pool);
	if (rc != 0)
		pool = NULL;
	return pool;
}

void
ds_pool_put(struct ds_pool *pool)
{
	ABT_mutex_lock(pool_cache_lock);
	daos_lru_ref_release(pool_cache, &pool->sp_entry);
	ABT_mutex_unlock(pool_cache_lock);
}

/* ds_pool_hdl ****************************************************************/

static struct d_hash_table *pool_hdl_hash;

static inline struct ds_pool_hdl *
pool_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_pool_hdl, sph_entry);
}

static bool
pool_hdl_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->sph_uuid, key) == 0;
}

static void
pool_hdl_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	pool_hdl_obj(rlink)->sph_ref++;
}

static bool
pool_hdl_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_ASSERTF(hdl->sph_ref > 0, "%d\n", hdl->sph_ref);
	hdl->sph_ref--;
	return hdl->sph_ref == 0;
}

static void
pool_hdl_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_DEBUG(DF_DSMS, DF_UUID": freeing "DF_UUID"\n",
		DP_UUID(hdl->sph_pool->sp_uuid), DP_UUID(hdl->sph_uuid));
	D_ASSERT(d_hash_rec_unlinked(&hdl->sph_entry));
	D_ASSERTF(hdl->sph_ref == 0, "%d\n", hdl->sph_ref);
	ds_pool_put(hdl->sph_pool);
	D_FREE(hdl);
}

static d_hash_table_ops_t pool_hdl_hash_ops = {
	.hop_key_cmp	= pool_hdl_key_cmp,
	.hop_rec_addref	= pool_hdl_rec_addref,
	.hop_rec_decref	= pool_hdl_rec_decref,
	.hop_rec_free	= pool_hdl_rec_free
};

int
ds_pool_hdl_hash_init(void)
{
	return d_hash_table_create(0 /* feats */, 4 /* bits */, NULL /* priv */,
				   &pool_hdl_hash_ops, &pool_hdl_hash);
}

void
ds_pool_hdl_hash_fini(void)
{
	/* Currently, we use "force" to purge all ds_pool_hdl objects. */
	d_hash_table_destroy(pool_hdl_hash, true /* force */);
}

static int
pool_hdl_add(struct ds_pool_hdl *hdl)
{
	return d_hash_rec_insert(pool_hdl_hash, hdl->sph_uuid,
				 sizeof(uuid_t), &hdl->sph_entry,
				 true /* exclusive */);
}

static void
pool_hdl_delete(struct ds_pool_hdl *hdl)
{
	bool deleted;

	deleted = d_hash_rec_delete(pool_hdl_hash, hdl->sph_uuid,
				    sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

struct ds_pool_hdl *
ds_pool_hdl_lookup(const uuid_t uuid)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(pool_hdl_hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return pool_hdl_obj(rlink);
}

void
ds_pool_hdl_put(struct ds_pool_hdl *hdl)
{
	d_hash_rec_decref(pool_hdl_hash, &hdl->sph_entry);
}

static void
aggregate_pool_space(struct daos_pool_space *agg_ps,
		     struct daos_pool_space *ps)
{
	int	i;
	bool	first;

	D_ASSERT(agg_ps && ps);

	if (ps->ps_ntargets == 0) {
		D_ERROR("Skip emtpy space info\n");
		return;
	}

	first = (agg_ps->ps_ntargets == 0);
	agg_ps->ps_ntargets += ps->ps_ntargets;

	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		agg_ps->ps_space.s_total[i] += ps->ps_space.s_total[i];
		agg_ps->ps_space.s_free[i] += ps->ps_space.s_free[i];

		if (agg_ps->ps_free_max[i] < ps->ps_free_max[i])
			agg_ps->ps_free_max[i] = ps->ps_free_max[i];
		if (agg_ps->ps_free_min[i] > ps->ps_free_min[i] || first)
			agg_ps->ps_free_min[i] = ps->ps_free_min[i];

		agg_ps->ps_free_mean[i] = agg_ps->ps_space.s_free[i] /
					  agg_ps->ps_ntargets;
	}
}

struct pool_query_xs_arg {
	struct ds_pool		*qxa_pool;
	struct daos_pool_space	 qxa_space;
};

static void
pool_query_xs_reduce(void *agg_arg, void *xs_arg)
{
	struct pool_query_xs_arg	*a_arg = agg_arg;
	struct pool_query_xs_arg	*x_arg = xs_arg;

	D_ASSERT(x_arg->qxa_space.ps_ntargets == 1);
	aggregate_pool_space(&a_arg->qxa_space, &x_arg->qxa_space);
}

static int
pool_query_xs_arg_alloc(struct dss_stream_arg_type *xs, void *agg_arg)
{
	struct pool_query_xs_arg	*x_arg, *a_arg = agg_arg;

	D_ALLOC_PTR(x_arg);
	if (x_arg == NULL)
		return -DER_NOMEM;

	xs->st_arg = x_arg;
	x_arg->qxa_pool = a_arg->qxa_pool;
	return 0;
}

static void
pool_query_xs_arg_free(struct dss_stream_arg_type *xs)
{
	D_ASSERT(xs->st_arg != NULL);
	D_FREE(xs->st_arg);
}

static int
pool_query_one(void *vin)
{
	struct dss_coll_stream_args	*reduce = vin;
	struct dss_stream_arg_type	*streams = reduce->csa_streams;
	struct dss_module_info		*info = dss_get_module_info();
	int				 tid = info->dmi_tgt_id;
	struct pool_query_xs_arg	*x_arg = streams[tid].st_arg;
	struct ds_pool			*pool = x_arg->qxa_pool;
	struct ds_pool_child		*pool_child;
	struct daos_pool_space		*x_ps = &x_arg->qxa_space;
	vos_pool_info_t			 vos_pool_info = { 0 };
	int				 rc, i;

	pool_child = ds_pool_child_lookup(pool->sp_uuid);
	if (pool_child == NULL)
		return -DER_NO_HDL;

	rc = vos_pool_query(pool_child->spc_hdl, &vos_pool_info);
	if (rc != 0) {
		D_ERROR("Failed to query pool "DF_UUID", tgt_id: %d, rc: %d\n",
			DP_UUID(pool->sp_uuid), tid, rc);
		goto out;
	}

	x_ps->ps_ntargets = 1;
	x_ps->ps_space.s_total[DAOS_MEDIA_SCM] = vos_pool_info.pif_scm_sz;
	x_ps->ps_space.s_total[DAOS_MEDIA_NVME] = vos_pool_info.pif_nvme_sz;
	x_ps->ps_space.s_free[DAOS_MEDIA_SCM] = vos_pool_info.pif_scm_free;
	x_ps->ps_space.s_free[DAOS_MEDIA_NVME] = vos_pool_info.pif_nvme_free;

	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		x_ps->ps_free_max[i] = x_ps->ps_space.s_free[i];
		x_ps->ps_free_min[i] = x_ps->ps_space.s_free[i];
	}
out:
	ds_pool_child_put(pool_child);
	return rc;
}

static int
pool_tgt_query(struct ds_pool *pool, struct daos_pool_space *ps)
{
	struct dss_coll_ops		coll_ops;
	struct dss_coll_args		coll_args;
	struct pool_query_xs_arg	agg_arg = { 0 };
	int				rc;

	D_ASSERT(ps != NULL);
	memset(ps, 0, sizeof(*ps));

	/* collective operations */
	coll_ops.co_func		= pool_query_one;
	coll_ops.co_reduce		= pool_query_xs_reduce;
	coll_ops.co_reduce_arg_alloc	= pool_query_xs_arg_alloc;
	coll_ops.co_reduce_arg_free	= pool_query_xs_arg_free;

	/* packing arguments for aggregator args */
	agg_arg.qxa_pool		= pool;

	/* setting aggregator args */
	coll_args.ca_aggregator		= &agg_arg;
	coll_args.ca_func_args		= &coll_args.ca_stream_args;

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, 0);
	if (rc) {
		D_ERROR("Pool query on pool "DF_UUID" failed, rc:%d\n",
			DP_UUID(pool->sp_uuid), rc);
		return rc;
	}

	*ps = agg_arg.qxa_space;
	return rc;
}

void
ds_pool_tgt_connect_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_connect_in	*in = crt_req_get(rpc);
	struct pool_tgt_connect_out	*out = crt_reply_get(rpc);
	struct pool_map			*map = NULL;
	struct ds_pool			*pool;
	struct ds_pool_hdl		*hdl;
	struct ds_pool_create_arg	 arg;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_UUID": handling rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->tci_uuid), rpc, DP_UUID(in->tci_hdl));

	hdl = ds_pool_hdl_lookup(in->tci_hdl);
	if (hdl != NULL) {
		if (hdl->sph_capas == in->tci_capas) {
			D_DEBUG(DF_DSMS, DF_UUID": found compatible pool "
				"handle: hdl="DF_UUID" capas="DF_U64"\n",
				DP_UUID(in->tci_uuid), DP_UUID(in->tci_hdl),
				hdl->sph_capas);
			rc = 0;
		} else {
			D_ERROR(DF_UUID": found conflicting pool handle: hdl="
				DF_UUID" capas="DF_U64"\n",
				DP_UUID(in->tci_uuid), DP_UUID(in->tci_hdl),
				hdl->sph_capas);
			rc = -DER_EXIST;
		}
		ds_pool_hdl_put(hdl);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Init the pool map */
	if (rpc->cr_co_bulk_hdl != NULL) {
		struct pool_buf		*buf;
		daos_iov_t		 iov = { 0 };
		daos_sg_list_t		 sgl;

		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;
		rc = crt_bulk_access(rpc->cr_co_bulk_hdl, daos2crt_sg(&sgl));
		if (rc != 0) {
			D_ERROR(DF_UUID": crt_bulk_access failed, rc %d.\n",
				DP_UUID(in->tci_uuid), rc);
			D_GOTO(out, rc);
		}

		buf = iov.iov_buf;
		D_ASSERT(buf != NULL);
		rc = pool_map_create(buf, in->tci_map_version, &map);
		if (rc != 0) {
			D_ERROR(DF_UUID" failed to create pool map: %d\n",
				DP_UUID(in->tci_uuid), rc);
			D_GOTO(out, rc);
		}
	}

	arg.pca_map = map;
	arg.pca_map_version = in->tci_map_version;
	arg.pca_need_group = 0;

	rc = ds_pool_lookup_create(in->tci_uuid, &arg, &pool);
	if (rc != 0) {
		D_FREE(hdl);
		D_GOTO(out, rc);
	}

	uuid_copy(hdl->sph_uuid, in->tci_hdl);
	hdl->sph_capas = in->tci_capas;
	hdl->sph_pool = pool;

	rc = pool_hdl_add(hdl);
	if (rc != 0) {
		ds_pool_put(pool);
		D_GOTO(out, rc);
	}

	if (pool->sp_iv_ns == NULL) {
		rc = ds_pool_iv_ns_update(pool, in->tci_master_rank,
					  &in->tci_iv_ctxt,
					  in->tci_iv_ns_id);
		if (rc) {
			D_ERROR("attach iv ns failed rc %d\n", rc);
			D_GOTO(out, rc);
		}
	}

	rc = pool_tgt_query(pool, &out->tco_space);
out:
	if (rc != 0 && map != NULL)
		pool_map_decref(map);

	out->tco_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->tci_uuid), rpc, out->tco_rc, rc);
	crt_reply_send(rpc);
}

int
ds_pool_tgt_connect_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_tgt_connect_out	*out_source = crt_reply_get(source);
	struct pool_tgt_connect_out	*out_result = crt_reply_get(result);

	out_result->tco_rc += out_source->tco_rc;
	if (out_source->tco_rc != 0)
		return 0;

	aggregate_pool_space(&out_result->tco_space, &out_source->tco_space);
	return 0;
}

void
ds_pool_tgt_disconnect_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_disconnect_in  *in = crt_req_get(rpc);
	struct pool_tgt_disconnect_out *out = crt_reply_get(rpc);
	uuid_t			       *hdl_uuids = in->tdi_hdls.ca_arrays;
	int				i;
	int				rc;

	if (in->tdi_hdls.ca_count == 0)
		D_GOTO(out, rc = 0);

	if (in->tdi_hdls.ca_arrays == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DF_DSMS, DF_UUID": handling rpc %p: hdls[0]="DF_UUID" nhdls="
		DF_U64"\n", DP_UUID(in->tdi_uuid), rpc, DP_UUID(hdl_uuids),
		in->tdi_hdls.ca_count);

	for (i = 0; i < in->tdi_hdls.ca_count; i++) {
		struct ds_pool_hdl *hdl;

		hdl = ds_pool_hdl_lookup(hdl_uuids[i]);
		if (hdl == NULL) {
			D_DEBUG(DF_DSMS, DF_UUID": handle "DF_UUID
				" does not exist\n", DP_UUID(in->tdi_uuid),
				DP_UUID(hdl_uuids[i]));
			continue;
		}

		pool_hdl_delete(hdl);
		ds_pool_hdl_put(hdl);
	}

	rc = 0;
out:
	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->tdi_uuid), rpc, out->tdo_rc, rc);
	crt_reply_send(rpc);
}

int
ds_pool_tgt_disconnect_aggregator(crt_rpc_t *source, crt_rpc_t *result,
					void *priv)
{
	struct pool_tgt_disconnect_out *out_source = crt_reply_get(source);
	struct pool_tgt_disconnect_out *out_result = crt_reply_get(result);

	out_result->tdo_rc += out_source->tdo_rc;
	return 0;
}

/*
 * Called via dss_collective() to update the pool map version in the
 * ds_pool_child object.
 */
static int
update_child_map(void *data)
{
	struct ds_pool		*pool = (struct ds_pool *)data;
	struct ds_pool_child	*child;

	child = ds_pool_child_lookup(pool->sp_uuid);
	if (child == NULL)
		return -DER_NONEXIST;

	ds_rebuild_pool_map_update(pool);
	child->spc_map_version = pool->sp_map_version;
	ds_pool_child_put(child);
	return 0;
}

int
ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
		       unsigned int map_version)
{
	struct pool_map *map = NULL;
	int		rc = 0;

	if (buf != NULL) {
		rc = pool_map_create(buf, map_version, &map);
		if (rc != 0) {
			D_ERROR(DF_UUID" failed to create pool map: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			D_GOTO(out, rc);
		}
	}

	ABT_rwlock_wrlock(pool->sp_lock);
	if (pool->sp_map_version < map_version ||
	    (pool->sp_map_version == map_version &&
	     (map != NULL && pool->sp_map == NULL))) {
		if (map != NULL) {
			struct pool_map *tmp = pool->sp_map;

			pool->sp_map = map;
			map = tmp;
		}

		D_DEBUG(DF_DSMS, DF_UUID
			": changed cached map version: %u -> %u\n",
			DP_UUID(pool->sp_uuid), pool->sp_map_version,
			map_version);

		pool->sp_map_version = map_version;
		rc = dss_task_collective(update_child_map, pool, 0);
		D_ASSERT(rc == 0);
	} else if (pool->sp_map != NULL &&
		   pool_map_get_version(pool->sp_map) < map_version &&
		   map != NULL) {
		struct pool_map *tmp = pool->sp_map;

		rc = pl_map_update(pool->sp_uuid, map,
				   pool->sp_map != NULL ? false : true);
		if (rc != 0) {
			ABT_rwlock_unlock(pool->sp_lock);
			D_ERROR(DF_UUID": failed to update pl_map: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			D_GOTO(out, rc);
		}

		/* drop the stale map */
		pool->sp_map = map;
		map = tmp;
	} else {
		D_WARN("Ignore old map version: cur=%u, input=%u pool %p\n",
		       pool->sp_map_version, map_version, pool);
	}
	ABT_rwlock_unlock(pool->sp_lock);

	if (map)
		pool_map_decref(map);

out:
	return rc;
}

void
ds_pool_tgt_update_map_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_update_map_in  *in = crt_req_get(rpc);
	struct pool_tgt_update_map_out *out = crt_reply_get(rpc);
	struct ds_pool		       *pool;
	struct pool_buf			*buf = NULL;
	int				rc = 0;

	D_DEBUG(DF_DSMS, DF_UUID": handling rpc %p: version=%u\n",
		DP_UUID(in->tui_uuid), rpc, in->tui_map_version);

	pool = ds_pool_lookup(in->tui_uuid);
	if (pool == NULL) {
		/* update the pool map w/o connection, just ignore it */
		D_GOTO(out, rc = 0);
	}

	if (rpc->cr_co_bulk_hdl != NULL) {
		daos_iov_t	iov;
		daos_sg_list_t	sgl;

		memset(&iov, 0, sizeof(iov));
		sgl.sg_nr = 1;
		sgl.sg_nr_out = 1;
		sgl.sg_iovs = &iov;
		rc = crt_bulk_access(rpc->cr_co_bulk_hdl, daos2crt_sg(&sgl));
		if (rc != 0)
			D_GOTO(out_pool, rc);
		buf = iov.iov_buf;
	}

	rc = ds_pool_tgt_map_update(pool, buf, in->tui_map_version);

out_pool:
	ds_pool_put(pool);
out:
	out->tuo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->tui_uuid), rpc, out->tuo_rc, rc);
	crt_reply_send(rpc);
}

int
ds_pool_tgt_update_map_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv)
{
	struct pool_tgt_update_map_out *out_source = crt_reply_get(source);
	struct pool_tgt_update_map_out *out_result = crt_reply_get(result);

	out_result->tuo_rc += out_source->tuo_rc;
	return 0;
}

void
ds_pool_tgt_query_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_query_in	*in = crt_req_get(rpc);
	struct pool_tgt_query_out	*out = crt_reply_get(rpc);
	struct ds_pool_hdl		*hdl;
	int				 rc;

	hdl = ds_pool_hdl_lookup(in->tqi_op.pi_hdl);
	if (hdl == NULL) {
		D_ERROR("Failed to find pool hdl "DF_UUID"\n",
			DP_UUID(in->tqi_op.pi_hdl));
		D_GOTO(out, rc = -DER_NO_HDL);
	}

	D_ASSERT(hdl->sph_pool != NULL);
	rc = pool_tgt_query(hdl->sph_pool, &out->tqo_space);
	ds_pool_hdl_put(hdl);
out:
	out->tqo_rc = (rc == 0 ? 0 : 1);
	crt_reply_send(rpc);
}

int
ds_pool_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_tgt_query_out	*out_source = crt_reply_get(source);
	struct pool_tgt_query_out	*out_result = crt_reply_get(result);

	out_result->tqo_rc += out_source->tqo_rc;
	if (out_source->tqo_rc != 0)
		return 0;

	aggregate_pool_space(&out_result->tqo_space, &out_source->tqo_space);
	return 0;
}

typedef int (*pool_iter_cb_t)(daos_handle_t ph, uuid_t co_uuid, void *arg);

/* iterate all of the container of the pool. */
static int
ds_pool_cont_iter(daos_handle_t ph, pool_iter_cb_t callback, void *arg)
{
	vos_iter_param_t param;
	daos_handle_t	 iter_h;
	int		 rc;

	memset(&param, 0, sizeof(param));
	param.ip_hdl = ph;
	param.ip_flags = VOS_IT_FOR_REBUILD;

	rc = vos_iter_prepare(VOS_ITER_COUUID, &param, &iter_h);
	if (rc != 0) {
		D_ERROR("prepare co iterator failed %d\n", rc);
		return rc;
	}

	rc = vos_iter_probe(iter_h, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			D_ERROR("set iterator cursor failed: %d\n", rc);

		D_GOTO(iter_fini, rc);
	}

	while (1) {
		vos_iter_entry_t ent;

		rc = vos_iter_fetch(iter_h, &ent, NULL);
		if (rc != 0) {
			/* reach to the end of the pool */
			if (rc == -DER_NONEXIST)
				rc = 0;
			else
				D_ERROR("Fetch co failed: %d\n", rc);
			break;
		}

		if (!uuid_is_null(ent.ie_couuid)) {
			rc = callback(ph, ent.ie_couuid, arg);
			if (rc) {
				if (rc > 0)
					rc = 0;
				break;
			}
		}
		vos_iter_next(iter_h);
	}

iter_fini:
	vos_iter_finish(iter_h);
	return rc;
}

struct cont_iter_arg {
	ds_iter_cb_t	 callback;
	uuid_t		 po_uuid;
	void		*arg;
	uint32_t	 version;
	uint32_t	 intent;
};

static int
cont_iter_cb(uuid_t cont_uuid, vos_iter_entry_t *ent, void *data)
{
	struct cont_iter_arg *arg = data;

	return arg->callback(cont_uuid, ent, arg->arg);
}

struct dtx_resync_args {
	daos_handle_t	ph;
	uuid_t		po_uuid;
	uuid_t		co_uuid;
	uint32_t	ver;
};

static int
dtx_resync_ult(void *data)
{
	struct dtx_resync_args	*args = data;
	int			 rc;

	rc = dtx_resync(args->ph, args->po_uuid, args->co_uuid, args->ver,
			true);
	if (rc != 0)
		D_ERROR("Fail to resync some DTX(s) for the pool/cont "
			DF_UUID"/"DF_UUID" that will affect subsequent "
			"object rebuild: rc = %d.\n",
			DP_UUID(args->po_uuid), DP_UUID(args->co_uuid), rc);
	return rc;
}

static int
pool_iter_cb(daos_handle_t ph, uuid_t co_uuid, void *data)
{
	struct cont_iter_arg *arg = data;

	switch (arg->intent) {
	case DAOS_INTENT_REBUILD: {
		struct dtx_resync_args	args;
		int			rc;

		/* For rebuild case, we need to resync DTXs' status firstly. */
		args.ph = ph;
		args.ver = arg->version;
		uuid_copy(args.po_uuid, arg->po_uuid);
		uuid_copy(args.co_uuid, co_uuid);
		rc = dss_ult_create_execute(dtx_resync_ult, &args, NULL, NULL,
					    DSS_ULT_DTX_RESYNC, DSS_TGT_SELF,
					    0);
		if (rc != 0) {
			D_ERROR("dtx_resync_ult failed, rc %d.\n", rc);
			return rc;
		}

		/* Fall through to the regular object iteration. */
	}
	default:
		return ds_cont_iter(ph, co_uuid, cont_iter_cb, data,
				    VOS_ITER_OBJ);
	}
}

/**
 * Iterate all of the objects or DTXs in the pool.
 **/
int
ds_pool_iter(uuid_t pool_uuid, ds_iter_cb_t callback, void *data,
	     uint32_t version, uint32_t intent)
{
	struct cont_iter_arg	 arg;
	struct ds_pool_child	*child;
	int			 rc;

	child = ds_pool_child_lookup(pool_uuid);
	if (child == NULL)
		return -DER_NONEXIST;

	arg.callback = callback;
	uuid_copy(arg.po_uuid, pool_uuid);
	arg.arg = data;
	arg.version = version;
	arg.intent = intent;
	rc = ds_pool_cont_iter(child->spc_hdl, pool_iter_cb, &arg);

	ds_pool_child_put(child);

	D_DEBUG(DB_TRACE, DF_UUID" iterate pool is done\n",
		DP_UUID(pool_uuid));
	return rc;
}
