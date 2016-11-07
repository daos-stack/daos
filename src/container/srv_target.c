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

#include <daos_srv/container.h>

#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include <daos_srv/vos.h>
#include "rpc.h"
#include "srv_internal.h"

/* ds_cont ********************************************************************/

static inline struct ds_cont *
cont_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_cont, sc_list);
}

static int
cont_alloc_ref(void *key, unsigned int ksize, void *varg,
	       struct daos_llink **link)
{
	struct ds_pool_child   *pool = varg;
	struct ds_cont	       *cont;
	int			rc;

	if (pool == NULL)
		return -DER_NONEXIST;

	D_DEBUG(DF_DSMS, DF_CONT": creating\n", DP_CONT(pool->spc_uuid, key));

	D_ALLOC_PTR(cont);
	if (cont == NULL)
		return -DER_NOMEM;

	uuid_copy(cont->sc_uuid, key);

	rc = vos_co_open(pool->spc_hdl, key, &cont->sc_hdl);
	if (rc != 0) {
		D_FREE_PTR(cont);
		return rc;
	}

	*link = &cont->sc_list;
	return 0;
}

static void
cont_free_ref(struct daos_llink *llink)
{
	struct ds_cont *cont = cont_obj(llink);

	D_DEBUG(DF_DSMS, DF_CONT": freeing\n", DP_CONT(NULL, cont->sc_uuid));
	vos_co_close(cont->sc_hdl);
	D_FREE_PTR(cont);
}

static bool
cont_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct ds_cont *cont = cont_obj(llink);

	return uuid_compare(key, cont->sc_uuid) == 0;
}

static struct daos_llink_ops cont_cache_ops = {
	.lop_alloc_ref	= cont_alloc_ref,
	.lop_free_ref	= cont_free_ref,
	.lop_cmp_keys	= cont_cmp_keys
};

int
ds_cont_cache_create(struct daos_lru_cache **cache)
{
	/*
	 * Since there's currently no way to evict an idle object, we don't
	 * really cache any idle objects.
	 */
	return daos_lru_cache_create(0 /* bits */, DHASH_FT_NOLOCK /* feats */,
				     &cont_cache_ops, cache);
}

void
ds_cont_cache_destroy(struct daos_lru_cache *cache)
{
	daos_lru_cache_destroy(cache);
}

/*
 * If "pool == NULL", then this is assumed to be a pure lookup. In this case,
 * -DER_NONEXIST is returned if the ds_cont object does not exist.
 */
static int
cont_lookup(struct daos_lru_cache *cache, const uuid_t uuid,
	    struct ds_pool_child *pool, struct ds_cont **cont)
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

	*cont = cont_obj(llink);
	return 0;
}

static void
cont_put(struct daos_lru_cache *cache, struct ds_cont *cont)
{
	daos_lru_ref_release(cache, &cont->sc_list);
}

/* ds_cont_hdl ****************************************************************/

static inline struct ds_cont_hdl *
cont_hdl_obj(daos_list_t *rlink)
{
	return container_of(rlink, struct ds_cont_hdl, sch_entry);
}

static bool
cont_hdl_key_cmp(struct dhash_table *htable, daos_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->sch_uuid, key) == 0;
}

static void
cont_hdl_rec_addref(struct dhash_table *htable, daos_list_t *rlink)
{
	cont_hdl_obj(rlink)->sch_ref++;
}

static bool
cont_hdl_rec_decref(struct dhash_table *htable, daos_list_t *rlink)
{
	struct ds_cont_hdl *hdl = cont_hdl_obj(rlink);

	hdl->sch_ref--;
	return hdl->sch_ref == 0;
}

static void
cont_hdl_rec_free(struct dhash_table *htable, daos_list_t *rlink)
{
	struct ds_cont_hdl     *hdl = cont_hdl_obj(rlink);
	struct dsm_tls	       *tls = dsm_tls_get();

	D_DEBUG(DF_DSMS, DF_CONT": freeing "DF_UUID"\n",
		DP_CONT(hdl->sch_pool->spc_uuid, hdl->sch_cont->sc_uuid),
		DP_UUID(hdl->sch_uuid));
	D_ASSERT(dhash_rec_unlinked(&hdl->sch_entry));
	D_ASSERTF(hdl->sch_ref == 0, "%d\n", hdl->sch_ref);
	cont_put(tls->dt_cont_cache, hdl->sch_cont);
	ds_pool_child_put(hdl->sch_pool);
	D_FREE_PTR(hdl);
}

static dhash_table_ops_t cont_hdl_hash_ops = {
	.hop_key_cmp	= cont_hdl_key_cmp,
	.hop_rec_addref	= cont_hdl_rec_addref,
	.hop_rec_decref	= cont_hdl_rec_decref,
	.hop_rec_free	= cont_hdl_rec_free
};

int
ds_cont_hdl_hash_create(struct dhash_table *hash)
{
	return dhash_table_create_inplace(0 /* feats */, 8 /* bits */,
					  NULL /* priv */,
					  &cont_hdl_hash_ops, hash);
}

void
ds_cont_hdl_hash_destroy(struct dhash_table *hash)
{
	dhash_table_destroy_inplace(hash, true /* force */);
}

static int
cont_hdl_add(struct dhash_table *hash, struct ds_cont_hdl *hdl)
{
	return dhash_rec_insert(hash, hdl->sch_uuid, sizeof(uuid_t),
				&hdl->sch_entry, true /* exclusive */);
}

static void
cont_hdl_delete(struct dhash_table *hash, struct ds_cont_hdl *hdl)
{
	bool deleted;

	deleted = dhash_rec_delete(hash, hdl->sch_uuid, sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

static struct ds_cont_hdl *
cont_hdl_lookup_internal(struct dhash_table *hash, const uuid_t uuid)
{
	daos_list_t *rlink;

	rlink = dhash_rec_find(hash, uuid, sizeof(uuid_t));
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
	struct dhash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	return cont_hdl_lookup_internal(hash, uuid);
}

static void
cont_hdl_put_internal(struct dhash_table *hash,
			       struct ds_cont_hdl *hdl)
{
	dhash_rec_decref(hash, &hdl->sch_entry);
}

/**
 * Put target container handle.
 *
 * \param hdl [IN]		container handle to be put.
 **/
void
ds_cont_hdl_put(struct ds_cont_hdl *hdl)
{
	struct dhash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	cont_hdl_put_internal(hash, hdl);
}

/*
 * Called via dss_collective() to destroy the ds_cont object as well as the vos
 * container.
 */
static int
cont_destroy_one(void *vin)
{
	struct cont_tgt_destroy_in     *in = vin;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct ds_pool_child	       *pool;
	struct ds_cont		       *cont;
	int				rc;

	pool = ds_pool_child_lookup(in->tdi_pool_uuid);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	rc = cont_lookup(tls->dt_cont_cache, in->tdi_uuid, NULL /* arg */,
			 &cont);
	if (rc == 0) {
		/* Should evict if idle, but no such interface at the moment. */
		cont_put(tls->dt_cont_cache, cont);
		D_GOTO(out_pool, rc = -DER_BUSY);
	} else if (rc != -DER_NONEXIST) {
		D_GOTO(out_pool, rc);
	}

	D_DEBUG(DF_DSMS, DF_CONT": destroying vos container\n",
		DP_CONT(pool->spc_uuid, in->tdi_uuid));

	rc = vos_co_destroy(pool->spc_hdl, in->tdi_uuid);

out_pool:
	ds_pool_child_put(pool);
out:
	return rc;
}

int
ds_cont_tgt_destroy_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_destroy_in     *in = crt_req_get(rpc);
	struct cont_tgt_destroy_out    *out = crt_reply_get(rpc);
	int				rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p\n",
		DP_CONT(in->tdi_pool_uuid, in->tdi_uuid), rpc);

	rc = dss_collective(cont_destroy_one, in);
	D_ASSERTF(rc == 0, "%d\n", rc);

	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(in->tdi_pool_uuid, in->tdi_uuid), rpc, out->tdo_rc,
		rc);
	return crt_reply_send(rpc);
}

int
ds_cont_tgt_destroy_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_destroy_out    *out_source = crt_reply_get(source);
	struct cont_tgt_destroy_out    *out_result = crt_reply_get(result);

	out_result->tdo_rc += out_source->tdo_rc;
	return 0;
}

/*
 * Called via dss_collective() to establish the ds_cont_hdl object as well as
 * the ds_cont object.
 */
static int
cont_open_one(void *vin)
{
	struct cont_tgt_open_in	       *in = vin;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct ds_cont_hdl	       *hdl;
	int				vos_co_created = 0;
	int				rc;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, in->toi_hdl);
	if (hdl != NULL) {
		if (hdl->sch_capas == in->toi_capas) {
			D_DEBUG(DF_DSMS, DF_CONT": found compatible container "
				"handle: hdl="DF_UUID" capas="DF_U64"\n",
				DP_CONT(in->toi_pool_uuid, in->toi_uuid),
				DP_UUID(in->toi_hdl), hdl->sch_capas);
			rc = 0;
		} else {
			D_ERROR(DF_CONT": found conflicting container handle: "
				"hdl="DF_UUID" capas="DF_U64"\n",
				DP_CONT(in->toi_pool_uuid, in->toi_uuid),
				DP_UUID(in->toi_hdl), hdl->sch_capas);
			rc = -DER_EXIST;
		}
		cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
		return rc;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	hdl->sch_pool = ds_pool_child_lookup(in->toi_pool_uuid);
	if (hdl->sch_pool == NULL)
		D_GOTO(err_hdl, rc = -DER_NO_PERM);

	rc = cont_lookup(tls->dt_cont_cache, in->toi_uuid, hdl->sch_pool,
			  &hdl->sch_cont);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DF_DSMS, DF_CONT": creating new vos container\n",
			DP_CONT(hdl->sch_pool->spc_uuid, in->toi_uuid));

		rc = vos_co_create(hdl->sch_pool->spc_hdl, in->toi_uuid);
		if (rc != 0)
			D_GOTO(err_pool, rc);

		vos_co_created = 1;

		rc = cont_lookup(tls->dt_cont_cache, in->toi_uuid,
				  hdl->sch_pool, &hdl->sch_cont);
		if (rc != 0)
			D_GOTO(err_vos_co, rc);
	} else if (rc != 0) {
		D_GOTO(err_pool, rc);
	}

	uuid_copy(hdl->sch_uuid, in->toi_hdl);
	hdl->sch_capas = in->toi_capas;

	rc = cont_hdl_add(&tls->dt_cont_hdl_hash, hdl);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	return 0;

err_cont:
	cont_put(tls->dt_cont_cache, hdl->sch_cont);
err_vos_co:
	if (vos_co_created) {
		D_DEBUG(DF_DSMS, DF_CONT": destroying new vos container\n",
			DP_CONT(hdl->sch_pool->spc_uuid, in->toi_uuid));
		vos_co_destroy(hdl->sch_pool->spc_hdl, in->toi_uuid);
	}
err_pool:
	ds_pool_child_put(hdl->sch_pool);
err_hdl:
	D_FREE_PTR(hdl);
err:
	return rc;
}

int
ds_cont_tgt_open_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_open_in	       *in = crt_req_get(rpc);
	struct cont_tgt_open_out       *out = crt_reply_get(rpc);
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: hdl="DF_UUID"\n",
		DP_CONT(in->toi_pool_uuid, in->toi_uuid), rpc,
		DP_UUID(in->toi_hdl));

	rc = dss_collective(cont_open_one, in);
	D_ASSERTF(rc == 0, "%d\n", rc);

	out->too_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->toi_uuid), rpc, out->too_rc, rc);
	return crt_reply_send(rpc);
}

int
ds_cont_tgt_open_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_open_out    *out_source = crt_reply_get(source);
	struct cont_tgt_open_out    *out_result = crt_reply_get(result);

	out_result->too_rc += out_source->too_rc;
	return 0;
}

/* Called via dss_collective() to close the ds_cont_hdl object. */
static int
cont_close_one(void *vin)
{
	struct cont_tgt_close_in       *in = vin;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct ds_cont_hdl	       *hdl;

	hdl = cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash, in->tci_hdl);
	if (hdl == NULL)
		return 0;

	cont_hdl_delete(&tls->dt_cont_hdl_hash, hdl);

	cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
	return 0;
}

int
ds_cont_tgt_close_handler(crt_rpc_t *rpc)
{
	struct cont_tgt_close_in       *in = crt_req_get(rpc);
	struct cont_tgt_close_out      *out = crt_reply_get(rpc);
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: hdl="DF_UUID"\n",
		DP_CONT(NULL, NULL), rpc, DP_UUID(in->tci_hdl));

	rc = dss_collective(cont_close_one, in);
	D_ASSERTF(rc == 0, "%d\n", rc);

	out->tco_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(NULL, NULL), rpc, out->tco_rc, rc);
	return crt_reply_send(rpc);
}

int
ds_cont_tgt_close_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct cont_tgt_close_out    *out_source = crt_reply_get(source);
	struct cont_tgt_close_out    *out_result = crt_reply_get(result);

	out_result->tco_rc += out_source->tco_rc;
	return 0;
}
