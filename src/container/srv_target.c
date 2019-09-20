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
 * ds_cont: Target Operations
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
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/container.h>

#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include <daos_srv/vos.h>
#include <daos_srv/iv.h>
#include "rpc.h"
#include "srv_internal.h"

/* ds_cont_child *******************************************************/

static inline struct ds_cont_child *
cont_child_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_cont_child, sc_list);
}

static int
cont_child_alloc_ref(void *key, unsigned int ksize, void *varg,
	       struct daos_llink **link)
{
	struct ds_pool_child	*pool = varg;
	struct ds_cont_child	*cont;
	int			rc;

	D_DEBUG(DF_DSMS, DF_CONT": opening\n", DP_CONT(pool->spc_uuid, key));

	D_ALLOC_PTR(cont);
	if (cont == NULL)
		return -DER_NOMEM;

	rc = ABT_mutex_create(&cont->sc_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}

	rc = ABT_cond_create(&cont->sc_dtx_resync_cond);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out_mutex;
	}

	rc = vos_cont_open(pool->spc_hdl, key, &cont->sc_hdl);
	if (rc != 0)
		goto out_cond;

	uuid_copy(cont->sc_uuid, key);

	*link = &cont->sc_list;
	return 0;
 out_cond:
	ABT_cond_free(&cont->sc_dtx_resync_cond);
 out_mutex:
	ABT_mutex_free(&cont->sc_mutex);
 out:
	D_FREE(cont);
	return rc;
}

static void
cont_child_free_ref(struct daos_llink *llink)
{
	struct ds_cont_child *cont = cont_child_obj(llink);

	D_DEBUG(DF_DSMS, DF_CONT": freeing\n", DP_CONT(NULL, cont->sc_uuid));
	vos_cont_close(cont->sc_hdl);

	ABT_cond_free(&cont->sc_dtx_resync_cond);
	ABT_mutex_free(&cont->sc_mutex);
	D_FREE(cont);
}

static bool
cont_child_cmp_keys(const void *key, unsigned int ksize,
		    struct daos_llink *llink)
{
	struct ds_cont_child *cont = cont_child_obj(llink);

	return uuid_compare(key, cont->sc_uuid) == 0;
}

static struct daos_llink_ops ds_cont_child_cache_ops = {
	.lop_alloc_ref	= cont_child_alloc_ref,
	.lop_free_ref	= cont_child_free_ref,
	.lop_cmp_keys	= cont_child_cmp_keys
};

int
ds_cont_child_cache_create(struct daos_lru_cache **cache)
{
	/*
	 * Since there's currently no way to evict an idle object, we don't
	 * really cache any idle objects.
	 */
	return daos_lru_cache_create(-1 /* bits */, D_HASH_FT_NOLOCK /*feats*/,
				     &ds_cont_child_cache_ops, cache);
}

void
ds_cont_child_cache_destroy(struct daos_lru_cache *cache)
{
	daos_lru_cache_destroy(cache);
}

/*
 * If "pool == NULL", then this is assumed to be a pure lookup. In this case,
 * -DER_NONEXIST is returned if the ds_cont_child object does not exist.
 */
static int
cont_child_lookup(struct daos_lru_cache *cache, const uuid_t uuid,
		  struct ds_pool_child *pool, struct ds_cont_child **cont)
{
	struct daos_llink      *llink;
	int			rc;

	rc = daos_lru_ref_hold(cache, (void *)uuid, sizeof(uuid_t), pool,
			       &llink);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, DF_CONT": failed to lookup%s "
				"container: %d\n", DP_CONT(NULL, uuid),
				pool == NULL ? "" : "/create", rc);
		else
			D_ERROR(DF_CONT": failed to lookup%s container: %d\n",
				DP_CONT(NULL, uuid),
				pool == NULL ? "" : "/create", rc);
		return rc;
	}

	*cont = cont_child_obj(llink);
	return 0;
}

static void
cont_child_put(struct daos_lru_cache *cache, struct ds_cont_child *cont)
{
	daos_lru_ref_release(cache, &cont->sc_list);
}

/* ds_cont_hdl ****************************************************************/

static inline struct ds_cont_hdl *
cont_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_cont_hdl, sch_entry);
}

static bool
cont_hdl_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->sch_uuid, key) == 0;
}

static void
cont_hdl_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	cont_hdl_obj(rlink)->sch_ref++;
}

static bool
cont_hdl_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);

	hdl->sch_ref--;
	return hdl->sch_ref == 0;
}

static void
cont_hdl_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_cont_hdl     *hdl = cont_hdl_obj(rlink);
	struct dsm_tls	       *tls = dsm_tls_get();

	D_ASSERT(d_hash_rec_unlinked(&hdl->sch_entry));
	D_ASSERTF(hdl->sch_ref == 0, "%d\n", hdl->sch_ref);
	D_DEBUG(DF_DSMS, "freeing "DF_UUID"\n", DP_UUID(hdl->sch_uuid));
	if (hdl->sch_cont != NULL) {
		D_DEBUG(DF_DSMS, DF_CONT": freeing\n",
			DP_CONT(hdl->sch_pool->spc_uuid,
			hdl->sch_cont->sc_uuid));
		cont_child_put(tls->dt_cont_cache, hdl->sch_cont);
	}
	ds_pool_child_put(hdl->sch_pool);
	D_FREE(hdl);
}

static d_hash_table_ops_t cont_hdl_hash_ops = {
	.hop_key_cmp	= cont_hdl_key_cmp,
	.hop_rec_addref	= cont_hdl_rec_addref,
	.hop_rec_decref	= cont_hdl_rec_decref,
	.hop_rec_free	= cont_hdl_rec_free
};

int
ds_cont_hdl_hash_create(struct d_hash_table *hash)
{
	return d_hash_table_create_inplace(0 /* feats */, 8 /* bits */,
					   NULL /* priv */,
					   &cont_hdl_hash_ops, hash);
}

void
ds_cont_hdl_hash_destroy(struct d_hash_table *hash)
{
	d_hash_table_destroy_inplace(hash, true /* force */);
}

static int
cont_hdl_add(struct d_hash_table *hash, struct ds_cont_hdl *hdl)
{
	return d_hash_rec_insert(hash, hdl->sch_uuid, sizeof(uuid_t),
				 &hdl->sch_entry, true /* exclusive */);
}

static void
cont_hdl_delete(struct d_hash_table *hash, struct ds_cont_hdl *hdl)
{
	bool deleted;

	deleted = d_hash_rec_delete(hash, hdl->sch_uuid, sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

static struct ds_cont_hdl *
cont_hdl_lookup_internal(struct d_hash_table *hash, const uuid_t uuid)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return cont_hdl_obj(rlink);
}

/**
 * lookup target container handle by container handle uuid (usually from req)
 *
 * \param uuid [IN]		container handle uuid
 *
 * \return			target container handle if succeeds.
 * \return			NULL if it does not find.
 */
struct ds_cont_hdl *
ds_cont_hdl_lookup(const uuid_t uuid)
{
	struct d_hash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	return cont_hdl_lookup_internal(hash, uuid);
}

static void
cont_hdl_put_internal(struct d_hash_table *hash,
		      struct ds_cont_hdl *hdl)
{
	d_hash_rec_decref(hash, &hdl->sch_entry);
}

static void
cont_hdl_get_internal(struct d_hash_table *hash,
		      struct ds_cont_hdl *hdl)
{
	d_hash_rec_addref(hash, &hdl->sch_entry);
}

/**
 * Put target container handle.
 *
 * \param hdl [IN]		container handle to be put.
 **/
void
ds_cont_hdl_put(struct ds_cont_hdl *hdl)
{
	struct d_hash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	cont_hdl_put_internal(hash, hdl);
}

/**
 * Get target container handle.
 *
 * \param hdl [IN]		container handle to be get.
 **/
void
ds_cont_hdl_get(struct ds_cont_hdl *hdl)
{
	struct d_hash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	cont_hdl_get_internal(hash, hdl);
}

/* ds cont cache */
static struct daos_lru_cache   *ds_cont_cache;
static ABT_mutex		cont_cache_lock;

static inline struct ds_cont *
cont_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_cont, sc_list);
}

static int
cont_alloc_ref(void *key, unsigned int ksize, void *varg,
	       struct daos_llink **link)
{
	struct ds_cont	*cont;
	uuid_t		*uuid = varg;

	D_ALLOC_PTR(cont);
	uuid_copy(cont->sc_uuid, *uuid);
	*link = &cont->sc_list;
	return 0;
}

static void
cont_free_ref(struct daos_llink *llink)
{
	struct ds_cont *cont = cont_obj(llink);

	D_FREE(cont);
}

static bool
cont_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct ds_cont *cont = cont_obj(llink);

	return uuid_compare(key, cont->sc_uuid) == 0;
}

static struct daos_llink_ops ds_cont_cache_ops = {
	.lop_alloc_ref	= cont_alloc_ref,
	.lop_free_ref	= cont_free_ref,
	.lop_cmp_keys	= cont_cmp_keys
};

int
ds_cont_lookup_create(const uuid_t uuid, void *arg, struct ds_cont **cont_p)
{
	struct daos_llink	*llink;
	int			rc;

	ABT_mutex_lock(cont_cache_lock);
	rc = daos_lru_ref_hold(ds_cont_cache, (void *)uuid, sizeof(uuid_t),
			       arg, &llink);
	ABT_mutex_unlock(cont_cache_lock);
	if (rc != 0) {
		if (arg == NULL && rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, DF_UUID": pure lookup failed: %d\n",
				DP_UUID(uuid), rc);
		else
			D_ERROR(DF_UUID": failed to lookup%s: %d\n",
				DP_UUID(uuid), arg == NULL ? "" : "/create",
				rc);
		return rc;
	}

	*cont_p = cont_obj(llink);
	return 0;
}

struct ds_cont *
ds_cont_lookup(const uuid_t uuid)
{
	struct ds_cont *cont;
	int		rc;

	rc = ds_cont_lookup_create(uuid, NULL, &cont);
	if (rc != 0)
		cont = NULL;

	return cont;
}

void
ds_cont_put(struct ds_cont *cont)
{
	ABT_mutex_lock(cont_cache_lock);
	daos_lru_ref_release(ds_cont_cache, &cont->sc_list);
	ABT_mutex_unlock(cont_cache_lock);
}

int
ds_cont_cache_init(void)
{
	int rc;

	rc = ABT_mutex_create(&cont_cache_lock);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = daos_lru_cache_create(-1 /* bits */, D_HASH_FT_NOLOCK /* feats */,
				   &ds_cont_cache_ops, &ds_cont_cache);
	if (rc != 0)
		ABT_mutex_free(&cont_cache_lock);
	return rc;
}

void
ds_cont_cache_fini(void)
{
	ABT_mutex_lock(cont_cache_lock);
	daos_lru_cache_destroy(ds_cont_cache);
	ABT_mutex_unlock(cont_cache_lock);
	ABT_mutex_free(&cont_cache_lock);
}

/*
 * Called via dss_collective() to destroy the ds_cont object as well as the vos
 * container.
 */
static int
cont_child_destroy_one(void *vin)
{
	struct dsm_tls		       *tls = dsm_tls_get();
	struct cont_tgt_destroy_in     *in = vin;
	struct ds_pool_child	       *pool;
	int				rc;

	pool = ds_pool_child_lookup(in->tdi_pool_uuid);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	while (1) {
		struct ds_cont_child *cont;
		bool		      resyncing = false;

		rc = cont_child_lookup(tls->dt_cont_cache, in->tdi_uuid, NULL,
				       &cont);
		if (rc == -DER_NONEXIST)
			break;
		if (rc != 0)
			D_GOTO(out_pool, rc);
		/* found it */

		ABT_mutex_lock(cont->sc_mutex);
		cont->sc_destroying = 1;
		if (cont->sc_dtx_resyncing) {
			resyncing = true;
			ABT_cond_wait(cont->sc_dtx_resync_cond, cont->sc_mutex);
		}
		ABT_mutex_unlock(cont->sc_mutex);
		/* Should evict if idle, but no such interface at the moment. */
		cont_child_put(tls->dt_cont_cache, cont);

		if (!resyncing) {
			D_ERROR("container is still in-use\n");
			D_GOTO(out_pool, rc = -DER_BUSY);
		} /* else: resync should have completed, try again */
	}

	D_DEBUG(DF_DSMS, DF_CONT": destroying vos container\n",
		DP_CONT(pool->spc_uuid, in->tdi_uuid));

	rc = vos_cont_destroy(pool->spc_hdl, in->tdi_uuid);
	if (rc == -DER_NONEXIST) {
		/** VOS container creation is effectively delayed until
		 * container open time, so it might legitimately not exist if
		 * the container has never been opened */
		rc = 0;
	}
	/* XXX there might be a race between GC and pool destroy, let's do
	 * synchronous GC for now.
	 */
	dss_gc_run(-1);

	/*
	 * Force VEA to expire all the just freed extents and make them
	 * available for allocation immediately.
	 */
	vos_pool_ctl(pool->spc_hdl, VOS_PO_CTL_VEA_FLUSH);
	if (rc) {
		D_ERROR(DF_CONT": VEA flush failed. %d\n",
			DP_CONT(pool->spc_uuid, in->tdi_uuid), rc);
		goto out_pool;
	}

out_pool:
	ds_pool_child_put(pool);
out:
	return rc;
}

void
ds_cont_tgt_destroy_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_destroy_in     *in = crt_req_get(rpc);
	struct cont_tgt_destroy_out    *out = crt_reply_get(rpc);
	int				rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p\n",
		DP_CONT(in->tdi_pool_uuid, in->tdi_uuid), rpc);

	rc = dss_thread_collective(cont_child_destroy_one, in, 0);
	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(in->tdi_pool_uuid, in->tdi_uuid), rpc, out->tdo_rc,
		rc);
	crt_reply_send(rpc);
}

int
ds_cont_tgt_destroy_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_destroy_out    *out_source = crt_reply_get(source);
	struct cont_tgt_destroy_out    *out_result = crt_reply_get(result);

	out_result->tdo_rc += out_source->tdo_rc;
	return 0;
}

/**
 * sert container lookup by pool/container uuid.
 **/
int
ds_cont_child_lookup(uuid_t pool_uuid, uuid_t cont_uuid,
		     struct ds_cont_child **ds_cont)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_pool_child	*ds_pool;
	int			rc;

	ds_pool = ds_pool_child_lookup(pool_uuid);
	if (ds_pool == NULL)
		return -DER_NO_HDL;

	rc = cont_child_lookup(tls->dt_cont_cache, cont_uuid, ds_pool,
			       ds_cont);
	ds_pool_child_put(ds_pool);

	return rc;
}

/**
 * server container lookup and create. If the container is created,
 * it will return 1, otherwise return 0 or error code.
 **/
int
ds_cont_child_lookup_or_create(struct ds_cont_hdl *hdl, uuid_t cont_uuid)
{
	struct dsm_tls	*tls = dsm_tls_get();
	int rc;

	D_ASSERT(hdl->sch_cont == NULL);
	rc = cont_child_lookup(tls->dt_cont_cache, cont_uuid, hdl->sch_pool,
			       &hdl->sch_cont);
	if (rc != -DER_NONEXIST)
		return rc;

	D_DEBUG(DF_DSMS, DF_CONT": creating new vos container\n",
		DP_CONT(hdl->sch_pool->spc_uuid, cont_uuid));

	rc = vos_cont_create(hdl->sch_pool->spc_hdl, cont_uuid);
	if (rc != 0)
		return rc;

	rc = cont_child_lookup(tls->dt_cont_cache, cont_uuid,
			       hdl->sch_pool, &hdl->sch_cont);
	if (rc != 0) {
		vos_cont_destroy(hdl->sch_pool->spc_hdl, cont_uuid);
		return rc;
	}

	return 1;
}

int
ds_cont_local_close(uuid_t cont_hdl_uuid)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_cont_hdl	*hdl;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, cont_hdl_uuid);
	if (hdl == NULL)
		return 0;

	cont_hdl_delete(&tls->dt_cont_hdl_hash, hdl);

	ds_cont_hdl_put(hdl);
	return 0;
}

void
ds_cont_child_get(struct ds_cont_child *cont)
{
	daos_lru_ref_add(&cont->sc_list);
}

void
ds_cont_child_put(struct ds_cont_child *cont)
{
	struct dsm_tls	*tls = dsm_tls_get();

	cont_child_put(tls->dt_cont_cache, cont);
}

struct ds_dtx_resync_args {
	struct ds_pool_child	*pool;
	uuid_t			 co_uuid;
};

static void
ds_dtx_resync(void *arg)
{
	struct ds_dtx_resync_args	*ddra = arg;
	int				 rc;

	rc = dtx_resync(ddra->pool->spc_hdl, ddra->pool->spc_uuid,
			ddra->co_uuid, ddra->pool->spc_map_version, false);
	if (rc != 0)
		D_WARN("Fail to resync some DTX(s) for the pool/cont "
		       DF_UUID"/"DF_UUID" that may affect subsequent "
		       "operations: rc = %d.\n", DP_UUID(ddra->pool->spc_uuid),
		       DP_UUID(ddra->co_uuid), rc);

	ds_pool_child_put(ddra->pool);
	D_FREE(ddra);
}

int
ds_cont_local_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
		   uint64_t capas, struct ds_cont_hdl **cont_hdl)
{
	struct dsm_tls		*tls = dsm_tls_get();
	struct ds_cont_hdl	*hdl;
	int			vos_co_created = 0;
	int			rc = 0;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, cont_hdl_uuid);
	if (hdl != NULL) {
		if (capas != 0) {
			if (hdl->sch_capas != capas) {
				D_ERROR(DF_CONT": conflicting container : hdl="
					DF_UUID" capas="DF_U64"\n",
					DP_CONT(pool_uuid, cont_uuid),
					DP_UUID(cont_hdl_uuid), capas);
				rc = -DER_EXIST;
			} else {
				D_DEBUG(DF_DSMS, DF_CONT": found compatible"
					" container handle: hdl="DF_UUID
					" capas="DF_U64"\n",
				      DP_CONT(pool_uuid, cont_uuid),
				      DP_UUID(cont_hdl_uuid), hdl->sch_capas);
			}
		}

		if (rc == 0)
			hdl->sch_deleted = 0;

		if (cont_hdl != NULL && rc == 0)
			*cont_hdl = hdl;
		else
			cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
		return rc;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	hdl->sch_pool = ds_pool_child_lookup(pool_uuid);
	if (hdl->sch_pool == NULL)
		D_GOTO(err_hdl, rc = -DER_NO_HDL);

	if (cont_uuid != NULL) {
		rc = ds_cont_child_lookup_or_create(hdl, cont_uuid);
		if (rc == 1) {
			vos_co_created = 1;
			rc = 0;
		} else if (rc != 0) {
			D_GOTO(err_pool, rc);
		}
	}
	uuid_copy(hdl->sch_uuid, cont_hdl_uuid);
	hdl->sch_capas = capas;

	rc = cont_hdl_add(&tls->dt_cont_hdl_hash, hdl);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	if (cont_hdl != NULL) {
		cont_hdl_get_internal(&tls->dt_cont_hdl_hash, hdl);
		*cont_hdl = hdl;
	}

	/* It is possible to sync DTX status before destroy the CoS for close
	 * the container. But that may be not enough. Because the server may
	 * crashed before closing the container. Then the DTXs' status in the
	 * CoS cache will be lost. So we need to re-sync the DTXs status when
	 * open the container for the first time (not for cached open handle).
	 *
	 * On the other hand, even if we skip the DTX sync before destroy the
	 * CoS cache when close the container, resync DTX when open container
	 * is enough to guarantee related data records' visibility. That also
	 * simplify the DTX logic.
	 *
	 * XXX: The logic is related with DAOS server re-intergration, but we
	 *	do not support that currently. Then resync DTX when container
	 *	open will be used as temporary solution for DTX related logic.
	 *
	 * We do not trigger dtx_resync() when start the server. Because:
	 * 1. Currently, we do not support server re-integrate after restart.
	 * 2. A server may has multiple pools and each pool may has multiple
	 *    containers. These containers may not related with one another.
	 *    Make all the DTXs resync together during the server start will
	 *    cause the DTX resync time to be much longer than resync against
	 *    single container just when use (open) it. On the other hand, if
	 *    some servers are ready for dtx_resync, but others may not start
	 *    yet, then the ready ones may have to wait or failed dtx_resync.
	 *    Both cases are not expected.
	 */
	if (cont_uuid != NULL) {
		struct ds_dtx_resync_args	*ddra = NULL;

		rc = dtx_batched_commit_register(hdl);
		if (rc != 0) {
			D_ERROR("Failed to register the container "DF_UUID
				" to the DTX batched commit list: rc = %d\n",
				DP_UUID(cont_uuid), rc);
			D_GOTO(err_cont, rc);
		}

		D_ALLOC_PTR(ddra);
		if (ddra == NULL)
			D_GOTO(err_cont, rc = -DER_NOMEM);

		ddra->pool = ds_pool_child_get(hdl->sch_pool);
		uuid_copy(ddra->co_uuid, cont_uuid);
		rc = dss_ult_create(ds_dtx_resync, ddra, DSS_ULT_DTX_RESYNC,
				    DSS_TGT_SELF, 0, NULL);
		if (rc != 0) {
			ds_pool_child_put(hdl->sch_pool);
			D_FREE(ddra);
			D_GOTO(err_cont, rc);
		}
	}

	return 0;

err_cont:
	dtx_batched_commit_deregister(hdl);
	if (hdl->sch_cont)
		cont_child_put(tls->dt_cont_cache, hdl->sch_cont);

	if (vos_co_created) {
		D_DEBUG(DF_DSMS, DF_CONT": destroying new vos container\n",
			DP_CONT(hdl->sch_pool->spc_uuid, cont_uuid));
		vos_cont_destroy(hdl->sch_pool->spc_hdl, cont_uuid);
	}
err_pool:
	ds_pool_child_put(hdl->sch_pool);
err_hdl:
	D_FREE(hdl);
err:
	return rc;
}

struct cont_tgt_open_arg {
	uuid_t	pool_uuid;
	uuid_t	cont_uuid;
	uuid_t	cont_hdl_uuid;
	uint64_t capas;
};

/*
 * Called via dss_collective() to establish the ds_cont_hdl object as well as
 * the ds_cont object.
 */
static int
cont_open_one(void *vin)
{
	struct cont_tgt_open_arg	*arg = vin;

	return ds_cont_local_open(arg->pool_uuid, arg->cont_hdl_uuid,
				  arg->cont_uuid, arg->capas, NULL);
}

int
ds_cont_tgt_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		 uuid_t cont_uuid, uint64_t capas)
{
	struct ds_pool		*pool = NULL;
	struct ds_cont		*cont = NULL;
	struct cont_tgt_open_arg arg;
	int			 rc;

	uuid_copy(arg.pool_uuid, pool_uuid);
	uuid_copy(arg.cont_hdl_uuid, cont_hdl_uuid);
	uuid_copy(arg.cont_uuid, cont_uuid);
	arg.capas = capas;

	D_DEBUG(DB_TRACE, "open pool/cont/hdl "DF_UUID"/"DF_UUID"/"DF_UUID"\n",
		DP_UUID(pool_uuid), DP_UUID(cont_uuid), DP_UUID(cont_hdl_uuid));

	rc = dss_thread_collective(cont_open_one, &arg, 0);
	D_ASSERTF(rc == 0, "%d\n", rc);

	pool = ds_pool_lookup(pool_uuid);
	D_ASSERT(pool != NULL);
	rc = ds_cont_lookup_create(cont_uuid, &cont_uuid, &cont);
	if (rc)
		D_GOTO(out, rc);

	cont->sc_iv_ns = pool->sp_iv_ns;
out:
	if (pool)
		ds_pool_put(pool);
	if (cont)
		ds_cont_put(cont);

	return rc;
}

/* Close a single record (i.e., handle). */
static int
cont_close_one_rec(struct cont_tgt_close_rec *rec)
{
	struct dsm_tls	       *tls = dsm_tls_get();
	struct ds_cont_hdl     *hdl;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, rec->tcr_hdl);
	if (hdl == NULL) {
		D_DEBUG(DF_DSMS, DF_CONT": already closed: hdl="DF_UUID" hce="
			DF_U64"\n", DP_CONT(NULL, NULL), DP_UUID(rec->tcr_hdl),
			rec->tcr_hce);
		return 0;
	}

	D_ASSERT(hdl->sch_cont != NULL);

	D_DEBUG(DF_DSMS, DF_CONT": closing (%s): hdl="DF_UUID" hce="DF_U64"\n",
		DP_CONT(hdl->sch_pool->spc_uuid, hdl->sch_cont->sc_uuid),
		hdl->sch_cont->sc_closing ? "resent" : "new",
		DP_UUID(rec->tcr_hdl), rec->tcr_hce);

	dtx_batched_commit_deregister(hdl);
	if (!hdl->sch_deleted) {
		cont_hdl_delete(&tls->dt_cont_hdl_hash, hdl);
		hdl->sch_deleted = 1;
	}

	cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
	return 0;
}

/* Called via dss_collective() to close the containers belong to this thread. */
static int
cont_close_one(void *vin)
{
	struct cont_tgt_close_in       *in = vin;
	struct cont_tgt_close_rec      *recs = in->tci_recs.ca_arrays;
	int				i;
	int				rc = 0;

	for (i = 0; i < in->tci_recs.ca_count; i++) {
		int rc_tmp;

		rc_tmp = cont_close_one_rec(&recs[i]);
		if (rc_tmp != 0 && rc == 0)
			rc = rc_tmp;
	}

	return rc;
}

void
ds_cont_tgt_close_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_close_in       *in = crt_req_get(rpc);
	struct cont_tgt_close_out      *out = crt_reply_get(rpc);
	struct cont_tgt_close_rec      *recs = in->tci_recs.ca_arrays;
	int				rc;

	if (in->tci_recs.ca_count == 0)
		D_GOTO(out, rc = 0);

	if (in->tci_recs.ca_arrays == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: recs[0].hdl="DF_UUID
		"recs[0].hce="DF_U64" nres="DF_U64"\n", DP_CONT(NULL, NULL),
		rpc, DP_UUID(recs[0].tcr_hdl), recs[0].tcr_hce,
		in->tci_recs.ca_count);

	rc = dss_thread_collective(cont_close_one, in, 0);
	D_ASSERTF(rc == 0, "%d\n", rc);

out:
	out->tco_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(NULL, NULL), rpc, out->tco_rc, rc);
	crt_reply_send(rpc);
}

int
ds_cont_tgt_close_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_close_out    *out_source = crt_reply_get(source);
	struct cont_tgt_close_out    *out_result = crt_reply_get(result);

	out_result->tco_rc += out_source->tco_rc;
	return 0;
}

struct xstream_cont_query {
	struct cont_tgt_query_in	*xcq_rpc_in;
	daos_epoch_t			xcq_purged_epoch;
};

static int
cont_query_one(void *vin)
{
	struct dss_coll_stream_args	*reduce	   = vin;
	struct dss_stream_arg_type	*streams   = reduce->csa_streams;
	struct dss_module_info		*info	   = dss_get_module_info();
	int				tid	   = info->dmi_tgt_id;
	struct xstream_cont_query	*pack_args = streams[tid].st_arg;
	struct cont_tgt_query_in	*in	   = pack_args->xcq_rpc_in;
	struct ds_pool_hdl		*pool_hdl;
	struct ds_pool_child		*pool_child;
	daos_handle_t			vos_chdl;
	vos_cont_info_t			vos_cinfo;
	char				*opstr;
	int				rc;

	info = dss_get_module_info();
	pool_hdl = ds_pool_hdl_lookup(in->tqi_pool_uuid);
	if (pool_hdl == NULL)
		return -DER_NO_HDL;

	pool_child = ds_pool_child_lookup(pool_hdl->sph_pool->sp_uuid);
	if (pool_child == NULL)
		D_GOTO(ds_pool_hdl, rc = -DER_NO_HDL);

	opstr = "Opening VOS container open handle\n";
	rc = vos_cont_open(pool_child->spc_hdl, in->tqi_cont_uuid,
			   &vos_chdl);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed %s: %d",
			DP_CONT(in->tqi_pool_uuid, in->tqi_cont_uuid), opstr,
			rc);
		D_GOTO(ds_child, rc);
	}

	opstr = "Querying VOS container open handle\n";
	rc = vos_cont_query(vos_chdl, &vos_cinfo);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed :%s: %d",
			DP_CONT(in->tqi_pool_uuid, in->tqi_cont_uuid), opstr,
			rc);
		D_GOTO(out, rc);
	}
	pack_args->xcq_purged_epoch = vos_cinfo.ci_hae;

out:
	vos_cont_close(vos_chdl);
ds_child:
	ds_pool_child_put(pool_child);
ds_pool_hdl:
	ds_pool_hdl_put(pool_hdl);
	return rc;
}

static void
ds_cont_query_coll_reduce(void *a_args, void *s_args)
{
	struct	xstream_cont_query	 *aggregator = a_args;
	struct  xstream_cont_query	 *stream     = s_args;
	daos_epoch_t			 *min_epoch;

	min_epoch = &aggregator->xcq_purged_epoch;
	*min_epoch = MIN(*min_epoch, stream->xcq_purged_epoch);
}

static int
ds_cont_query_stream_alloc(struct dss_stream_arg_type *args,
			   void *a_arg)
{
	struct xstream_cont_query	*rarg = a_arg;

	D_ALLOC(args->st_arg, sizeof(struct xstream_cont_query));
	if (args->st_arg == NULL)
		return -DER_NOMEM;
	memcpy(args->st_arg, rarg, sizeof(struct xstream_cont_query));

	return 0;
}

static void
ds_cont_query_stream_free(struct dss_stream_arg_type *c_args)
{
	D_ASSERT(c_args->st_arg != NULL);
	D_FREE(c_args->st_arg);
}

void
ds_cont_tgt_query_handler(crt_rpc_t *rpc)
{
	int				rc;
	struct cont_tgt_query_in	*in  = crt_req_get(rpc);
	struct cont_tgt_query_out	*out = crt_reply_get(rpc);
	struct dss_coll_ops		coll_ops;
	struct dss_coll_args		coll_args = { 0 };
	struct xstream_cont_query	pack_args;

	out->tqo_min_purged_epoch  = DAOS_EPOCH_MAX;

	/** on all available streams */

	coll_ops.co_func		= cont_query_one;
	coll_ops.co_reduce		= ds_cont_query_coll_reduce;
	coll_ops.co_reduce_arg_alloc	= ds_cont_query_stream_alloc;
	coll_ops.co_reduce_arg_free	= ds_cont_query_stream_free;

	/** packing arguments for aggregator args */
	pack_args.xcq_rpc_in		= in;
	pack_args.xcq_purged_epoch	= DAOS_EPOCH_MAX;

	/** setting aggregator args */
	coll_args.ca_aggregator		= &pack_args;
	coll_args.ca_func_args		= &coll_args.ca_stream_args;


	rc = dss_task_collective_reduce(&coll_ops, &coll_args, 0);

	D_ASSERTF(rc == 0, "%d\n", rc);
	out->tqo_min_purged_epoch = MIN(out->tqo_min_purged_epoch,
					pack_args.xcq_purged_epoch);
	out->tqo_rc = (rc == 0 ? 0 : 1);

	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(NULL, NULL), rpc, out->tqo_rc, rc);
	crt_reply_send(rpc);
}

int
ds_cont_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_query_out	*out_source = crt_reply_get(source);
	struct cont_tgt_query_out	*out_result = crt_reply_get(result);

	out_result->tqo_min_purged_epoch =
		MIN(out_result->tqo_min_purged_epoch,
		    out_source->tqo_min_purged_epoch);
	out_result->tqo_rc += out_source->tqo_rc;
	return 0;
}

/* Called via dss_collective() to discard an epoch in the VOS pool. */
static int
cont_epoch_discard_one(void *vin)
{
	struct cont_tgt_epoch_discard_in       *in = vin;
	struct dsm_tls			       *tls = dsm_tls_get();
	struct ds_cont_hdl		       *hdl;
	daos_epoch_range_t			epr;
	int					rc;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, in->tii_hdl);
	if (hdl == NULL)
		return -DER_NO_PERM;

	epr.epr_lo = in->tii_epoch;
	epr.epr_hi = in->tii_epoch;

	rc = vos_discard(hdl->sch_cont->sc_hdl, &epr);
	if (rc > 0)	/* Aborted */
		rc = -DER_CANCELED;

	D_DEBUG(DB_EPC, DF_CONT": Discard epoch "DF_U64", hdl="DF_UUID": %d\n",
		DP_CONT(hdl->sch_pool->spc_uuid, hdl->sch_cont->sc_uuid),
		in->tii_epoch, DP_UUID(in->tii_hdl), rc);

	cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
	return rc;
}

void
ds_cont_tgt_epoch_discard_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_epoch_discard_in       *in = crt_req_get(rpc);
	struct cont_tgt_epoch_discard_out      *out = crt_reply_get(rpc);
	int					rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: hdl="DF_UUID" epoch="DF_U64
		"\n", DP_CONT(NULL, NULL), rpc, DP_UUID(in->tii_hdl),
		in->tii_epoch);

	if (in->tii_epoch == 0)
		D_GOTO(out, rc = -DER_EP_RO);
	else if (in->tii_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = dss_thread_collective(cont_epoch_discard_one, in, 0);

out:
	out->tio_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(NULL, NULL), rpc, out->tio_rc, rc);
	crt_reply_send(rpc);
}

int
ds_cont_tgt_epoch_discard_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				     void *priv)
{
	struct cont_tgt_epoch_discard_out      *out_source;
	struct cont_tgt_epoch_discard_out      *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->tio_rc += out_source->tio_rc;
	return 0;
}

static int
cont_epoch_aggregate_one(void *vin)
{
	struct cont_tgt_epoch_aggregate_in	*in  = vin;
	struct ds_pool_child			*pool_child;
	daos_handle_t				 cont_hdl;
	vos_cont_info_t				 cont_info;
	daos_epoch_range_t			 epr, epr_prev = { 0 };
	uint64_t				 i;
	int					 rc;

	pool_child = ds_pool_child_lookup(in->tai_pool_uuid);
	if (pool_child == NULL) {
		D_ERROR(DF_CONT": Lookup pool child failed\n",
			DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid));
		return -DER_NO_HDL;
	}

	rc = vos_cont_open(pool_child->spc_hdl, in->tai_cont_uuid,
			   &cont_hdl);
	if (rc != 0) {
		D_ERROR(DF_CONT": Open container failed: %d\n",
			DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid), rc);
		goto pool_child;
	}

	rc = vos_cont_query(cont_hdl, &cont_info);
	if (rc != 0) {
		D_ERROR(DF_CONT": Query container failed: %d\n",
			DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid), rc);
		goto cont_close;
	}

	for (i = 0; i < in->tai_epr_list.ca_count; i++) {
		bool snap_delete = false;

		epr = ((daos_epoch_range_t *)in->tai_epr_list.ca_arrays)[i];

		D_ASSERTF(epr_prev.epr_hi == 0 || epr_prev.epr_hi < epr.epr_lo,
			  "Previous epr_hi:"DF_U64" >= epr_lo:"DF_U64"\n",
			  epr_prev.epr_hi, epr.epr_lo);

		epr_prev = epr;
		D_DEBUG(DB_EPC, DF_CONT": epr["DF_U64"]("DF_U64"->"DF_U64")\n",
			DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
			i, epr.epr_lo, epr.epr_hi);
		/*
		 * TODO: Aggregation RPC will have a bit to indicate if the
		 * aggregation is triggered for snapshot deletion.
		 */
		if (!snap_delete && cont_info.ci_hae >= epr.epr_hi) {
			D_DEBUG(DB_EPC, DF_CONT": LAE:"DF_U64">="DF_U64"\n",
				DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
				cont_info.ci_hae, epr.epr_hi);
			continue;
		}

		rc = vos_aggregate(cont_hdl, &epr);
		if (rc < 0) {
			D_ERROR(DF_CONT": Agg "DF_U64"->"DF_U64" failed: %d\n",
				DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
				epr.epr_lo, epr.epr_hi, rc);
			break;
		} else if (rc) {
			D_DEBUG(DB_EPC, DF_CONT": Aggregation aborted\n",
				DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid));
			rc = -DER_CANCELED;
			break;
		}
	}

cont_close:
	vos_cont_close(cont_hdl);
pool_child:
	ds_pool_child_put(pool_child);
	return rc;
}

void
ds_cont_tgt_epoch_aggregate_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_epoch_aggregate_in	 in_copy;
	struct cont_tgt_epoch_aggregate_in	*in  = crt_req_get(rpc);
	struct cont_tgt_epoch_aggregate_out	*out = crt_reply_get(rpc);
	daos_epoch_range_t			*epr;
	int					 i, rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: epr (%p) [#"DF_U64"]\n",
		DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid), rpc,
		in->tai_epr_list.ca_arrays, in->tai_epr_list.ca_count);
	epr = in->tai_epr_list.ca_arrays;
	rc = 0;
	for (i = 0; i < in->tai_epr_list.ca_count; i++) {
		if (epr[i].epr_hi <= epr[i].epr_lo) {
			D_ERROR(DF_CONT": Invalid Range "DF_U64"->"DF_U64"\n",
				DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
				epr[i].epr_lo, epr[i].epr_hi);
			rc = -DER_INVAL;
			goto out;
		} else if (epr[i].epr_hi == 0) {
			D_ERROR(DF_CONT": Read-only Epoch "DF_U64"\n",
				DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
				epr[i].epr_hi);
			rc = -DER_EP_RO;
			goto out;
		} else if (epr[i].epr_hi >= DAOS_EPOCH_MAX) {
			D_ERROR(DF_CONT": Epoch out of bounds "DF_U64"\n",
				DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
				epr[i].epr_hi);
			rc = -DER_OVERFLOW;
			goto out;
		}
	}

	/* TODO: Use bulk transfer to avoid memory copy? */
	D_ALLOC_ARRAY(epr, (int) in->tai_epr_list.ca_count);
	if (epr == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	memcpy(epr, in->tai_epr_list.ca_arrays,
	       sizeof(*epr) * in->tai_epr_list.ca_count);
	in_copy = *in;
	in_copy.tai_epr_list.ca_arrays = epr;

out:
	/* Reply without waiting for the aggregation ULTs to finish. */
	out->tao_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid),
		rpc, out->tao_rc, rc);
	crt_reply_send(rpc);
	if (out->tao_rc != 0)
		return;

	rc = dss_thread_collective(cont_epoch_aggregate_one, &in_copy, 0);
	if (rc != 0)
		D_ERROR(DF_CONT": Aggregation failed: %d\n",
			DP_CONT(in->tai_pool_uuid, in->tai_cont_uuid), rc);
	D_FREE(epr);
}

int
ds_cont_tgt_epoch_aggregate_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				       void *priv)
{
	struct cont_tgt_epoch_aggregate_out      *out_source;
	struct cont_tgt_epoch_aggregate_out      *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->tao_rc += out_source->tao_rc;
	return 0;
}

/* iterate all of objects or uncommitted DTXs of the container. */
int
ds_cont_iter(daos_handle_t ph, uuid_t co_uuid, ds_iter_cb_t callback,
	     void *arg, uint32_t type)
{
	vos_iter_param_t param;
	daos_handle_t	 iter_h;
	daos_handle_t	 coh;
	int		 rc;

	rc = vos_cont_open(ph, co_uuid, &coh);
	if (rc != 0) {
		D_ERROR("Open container "DF_UUID" failed: rc = %d\n",
			DP_UUID(co_uuid), rc);
		return rc;
	}

	memset(&param, 0, sizeof(param));
	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags = VOS_IT_FOR_REBUILD;

	rc = vos_iter_prepare(type, &param, &iter_h);
	if (rc != 0) {
		D_ERROR("prepare obj iterator failed %d\n", rc);
		D_GOTO(close, rc);
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
			/* reach to the end of the container */
			if (rc == -DER_NONEXIST)
				rc = 0;
			else
				D_ERROR("Fetch obj failed: %d\n", rc);
			break;
		}

		D_DEBUG(DB_ANY, "iter "DF_UOID"/"DF_UUID"\n",
			DP_UOID(ent.ie_oid), DP_UUID(co_uuid));

		rc = callback(co_uuid, &ent, arg);
		if (rc) {
			D_DEBUG(DB_ANY, "iter "DF_UOID" rc %d\n",
				DP_UOID(ent.ie_oid), rc);
			if (rc > 0)
				rc = 0;
			break;
		}

		vos_iter_next(iter_h);
	}

iter_fini:
	vos_iter_finish(iter_h);
close:
	vos_cont_close(coh);
	return rc;
}

static int
cont_oid_alloc(struct ds_pool_hdl *pool_hdl, crt_rpc_t *rpc)
{
	struct cont_oid_alloc_in	*in = crt_req_get(rpc);
	struct cont_oid_alloc_out	*out;
	d_sg_list_t			sgl;
	d_iov_t				iov;
	struct oid_iv_range		rg;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": oid alloc: num_oids="DF_U64"\n",
		 DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coai_op.ci_uuid),
		 in->num_oids);

	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	d_iov_set(&iov, &rg, sizeof(rg));

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	rc = oid_iv_reserve(pool_hdl->sph_pool->sp_iv_ns,
			    in->coai_op.ci_pool_hdl, in->coai_op.ci_uuid,
			    in->coai_op.ci_hdl, in->num_oids, &sgl);
	if (rc)
		D_GOTO(out, rc);

	out->oid = rg.oid;

out:
	out->coao_op.co_rc = rc;
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		 DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coai_op.ci_uuid),
		 rpc, rc);

	return rc;
}

void
ds_cont_oid_alloc_handler(crt_rpc_t *rpc)
{
	struct cont_op_in	*in = crt_req_get(rpc);
	struct cont_op_out	*out = crt_reply_get(rpc);
	struct ds_pool_hdl	*pool_hdl;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	int			rc;

	pool_hdl = ds_pool_hdl_lookup(in->ci_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" opc=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc);

	D_ASSERT(opc == CONT_OID_ALLOC);

	rc = cont_oid_alloc(pool_hdl, rpc);

	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: hdl="DF_UUID
		" opc=%u rc=%d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc, rc);

	ds_pool_hdl_put(pool_hdl);
out:
	out->co_rc = rc;
	out->co_map_version = 0;
	crt_reply_send(rpc);
}
