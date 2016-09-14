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
 * dsms: Target Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related target states. Note that object I/O methods and handlers are in
 * dsms_object.c.
 *
 * Data structures used here:
 *
 *                 Pool          Container
 *
 *         Global  tgt_pool
 *                 tgt_pool_hdl
 *
 *   Thread-local  dsms_vpool    dsms_vcont
 *                               tgt_cont_hdl
 */

#include <uuid/uuid.h>
#include <daos/pool_map.h>
#include <daos/transport.h>
#include <daos_srv/pool.h>
#include <daos_srv/vos.h>
#include "rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

/*
 * dsms_vcont objects: thread-local container cache
 */

static inline struct dsms_vcont *
vcont_obj(struct daos_llink *llink)
{
	return container_of(llink, struct dsms_vcont, dvc_list);
}

static int
vcont_alloc_ref(void *key, unsigned int ksize, void *varg,
		struct daos_llink **link)
{
	struct dsms_vpool      *pool = varg;
	struct dsms_vcont      *cont;
	int			rc;

	if (pool == NULL)
		return -DER_NONEXIST;

	D_DEBUG(DF_DSMS, DF_CONT": creating\n", DP_CONT(pool->dvp_uuid, key));

	D_ALLOC_PTR(cont);
	if (cont == NULL)
		return -DER_NOMEM;

	uuid_copy(cont->dvc_uuid, key);

	rc = vos_co_open(pool->dvp_hdl, key, &cont->dvc_hdl);
	if (rc != 0) {
		D_FREE_PTR(cont);
		return rc;
	}

	*link = &cont->dvc_list;
	return 0;
}

static void
vcont_free_ref(struct daos_llink *llink)
{
	struct dsms_vcont *cont = vcont_obj(llink);

	D_DEBUG(DF_DSMS, DF_CONT": freeing\n", DP_CONT(NULL, cont->dvc_uuid));
	vos_co_close(cont->dvc_hdl);
	D_FREE_PTR(cont);
}

static bool
vcont_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct dsms_vcont *cont = vcont_obj(llink);

	return uuid_compare(key, cont->dvc_uuid) == 0;
}

static struct daos_llink_ops vcont_cache_ops = {
	.lop_alloc_ref	= vcont_alloc_ref,
	.lop_free_ref	= vcont_free_ref,
	.lop_cmp_keys	= vcont_cmp_keys
};

int
dsms_vcont_cache_create(struct daos_lru_cache **cache)
{
	/*
	 * Since there's currently no way to evict an idle object, we don't
	 * really cache any idle objects.
	 */
	return daos_lru_cache_create(0 /* bits */, DHASH_FT_NOLOCK /* feats */,
				     &vcont_cache_ops, cache);
}

void
dsms_vcont_cache_destroy(struct daos_lru_cache *cache)
{
	daos_lru_cache_destroy(cache);
}

/*
 * If "pool == NULL", then this is assumed to be a pure lookup. In this case,
 * -DER_NONEXIST is returned if the dsms_vcont object does not exist.
 */
static int
vcont_lookup(struct daos_lru_cache *cache, const uuid_t uuid,
	     struct dsms_vpool *pool, struct dsms_vcont **cont)
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

	*cont = vcont_obj(llink);
	return 0;
}

static void
vcont_put(struct daos_lru_cache *cache, struct dsms_vcont *cont)
{
	daos_lru_ref_release(cache, &cont->dvc_list);
}

/*
 * tgt_cont_hdl objects: thread-local container handle hash table
 */

static inline struct tgt_cont_hdl *
tgt_cont_hdl_obj(daos_list_t *rlink)
{
	return container_of(rlink, struct tgt_cont_hdl, tch_entry);
}

static bool
tgt_cont_hdl_key_cmp(struct dhash_table *htable, daos_list_t *rlink,
		     const void *key, unsigned int ksize)
{
	struct tgt_cont_hdl *hdl = tgt_cont_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->tch_uuid, key) == 0;
}

static void
tgt_cont_hdl_rec_addref(struct dhash_table *htable, daos_list_t *rlink)
{
	tgt_cont_hdl_obj(rlink)->tch_ref++;
}

static bool
tgt_cont_hdl_rec_decref(struct dhash_table *htable, daos_list_t *rlink)
{
	struct tgt_cont_hdl *hdl = tgt_cont_hdl_obj(rlink);

	hdl->tch_ref--;
	return hdl->tch_ref == 0;
}

static void
tgt_cont_hdl_rec_free(struct dhash_table *htable, daos_list_t *rlink)
{
	struct tgt_cont_hdl    *hdl = tgt_cont_hdl_obj(rlink);
	struct dsm_tls	       *tls = dsm_tls_get();

	D_DEBUG(DF_DSMS, DF_CONT": freeing "DF_UUID"\n",
		DP_CONT(hdl->tch_pool->dvp_uuid, hdl->tch_cont->dvc_uuid),
		DP_UUID(hdl->tch_uuid));
	D_ASSERT(dhash_rec_unlinked(&hdl->tch_entry));
	D_ASSERTF(hdl->tch_ref == 0, "%d\n", hdl->tch_ref);
	vcont_put(tls->dt_cont_cache, hdl->tch_cont);
	vpool_put(hdl->tch_pool);
	D_FREE_PTR(hdl);
}

static dhash_table_ops_t tgt_cont_hdl_hash_ops = {
	.hop_key_cmp	= tgt_cont_hdl_key_cmp,
	.hop_rec_addref	= tgt_cont_hdl_rec_addref,
	.hop_rec_decref	= tgt_cont_hdl_rec_decref,
	.hop_rec_free	= tgt_cont_hdl_rec_free
};

int
dsms_tgt_cont_hdl_hash_create(struct dhash_table *hash)
{
	return dhash_table_create_inplace(0 /* feats */, 8 /* bits */,
					  NULL /* priv */,
					  &tgt_cont_hdl_hash_ops, hash);
}

void
dsms_tgt_cont_hdl_hash_destroy(struct dhash_table *hash)
{
	dhash_table_destroy_inplace(hash, true /* force */);
}

static int
tgt_cont_hdl_add(struct dhash_table *hash, struct tgt_cont_hdl *hdl)
{
	return dhash_rec_insert(hash, hdl->tch_uuid, sizeof(uuid_t),
				&hdl->tch_entry, true /* exclusive */);
}

static void
tgt_cont_hdl_delete(struct dhash_table *hash, struct tgt_cont_hdl *hdl)
{
	bool deleted;

	deleted = dhash_rec_delete(hash, hdl->tch_uuid, sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

static struct tgt_cont_hdl *
dsms_tgt_cont_hdl_lookup_internal(struct dhash_table *hash, const uuid_t uuid)
{
	daos_list_t *rlink;

	rlink = dhash_rec_find(hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return tgt_cont_hdl_obj(rlink);
}

struct tgt_cont_hdl *
dsms_tgt_cont_hdl_lookup(const uuid_t uuid)
{
	struct dhash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	return dsms_tgt_cont_hdl_lookup_internal(hash, uuid);
}

static void
dsms_tgt_cont_hdl_put_internal(struct dhash_table *hash,
			       struct tgt_cont_hdl *hdl)
{
	dhash_rec_decref(hash, &hdl->tch_entry);
}

void
dsms_tgt_cont_hdl_put(struct tgt_cont_hdl *hdl)
{
	struct dhash_table *hash = &dsm_tls_get()->dt_cont_hdl_hash;

	dsms_tgt_cont_hdl_put_internal(hash, hdl);
}

/*
 * Called via dss_collective() to destroy the per-thread container (i.e.,
 * dsms_vcont) as well as the vos container.
 */
static int
es_cont_destroy(void *vin)
{
	struct tgt_cont_destroy_in     *in = vin;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct dsms_vpool	       *pool;
	struct dsms_vcont	       *cont;
	int				rc;

	pool = vpool_lookup(in->tcdi_pool);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	rc = vcont_lookup(tls->dt_cont_cache, in->tcdi_cont, NULL /* arg */,
			  &cont);
	if (rc == 0) {
		/* Should evict if idle, but no such interface at the moment. */
		vcont_put(tls->dt_cont_cache, cont);
		D_GOTO(out_pool, rc = -DER_BUSY);
	} else if (rc != -DER_NONEXIST) {
		D_GOTO(out_pool, rc);
	}

	D_DEBUG(DF_DSMS, DF_CONT": destroying vos container\n",
		DP_CONT(pool->dvp_uuid, in->tcdi_cont));

	rc = vos_co_destroy(pool->dvp_hdl, in->tcdi_cont);

out_pool:
	vpool_put(pool);
out:
	return rc;
}

int
dsms_hdlr_tgt_cont_destroy(dtp_rpc_t *rpc)
{
	struct tgt_cont_destroy_in     *in = dtp_req_get(rpc);
	struct tgt_cont_destroy_out    *out = dtp_reply_get(rpc);
	int				rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p\n",
		DP_CONT(in->tcdi_pool, in->tcdi_cont), rpc);

	rc = dss_collective(es_cont_destroy, in);
	D_ASSERTF(rc == 0, "%d\n", rc);

	out->tcdo_ret = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(in->tcdi_pool, in->tcdi_cont), rpc, out->tcdo_ret, rc);
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_tgt_cont_destroy_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
				     void *priv)
{
	struct tgt_cont_destroy_out    *out_source = dtp_reply_get(source);
	struct tgt_cont_destroy_out    *out_result = dtp_reply_get(result);

	out_result->tcdo_ret += out_source->tcdo_ret;
	return 0;
}

/*
 * Called via dss_collective() to establish the per-thread container handle
 * (i.e., tgt_cont_hdl) as well as the per-thread container object (i.e.,
 * dsms_vcont).
 */
static int
es_cont_open(void *vin)
{
	struct tgt_cont_open_in	       *in = vin;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct tgt_cont_hdl	       *hdl;
	int				vos_co_created = 0;
	int				rc;

	hdl = dsms_tgt_cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash,
						in->tcoi_cont_hdl);
	if (hdl != NULL) {
		if (hdl->tch_capas == in->tcoi_capas) {
			D_DEBUG(DF_DSMS, DF_CONT": found compatible container "
				"handle: hdl="DF_UUID" capas="DF_U64"\n",
				DP_CONT(in->tcoi_pool, in->tcoi_cont),
				DP_UUID(in->tcoi_cont_hdl), hdl->tch_capas);
			rc = 0;
		} else {
			D_ERROR(DF_CONT": found conflicting container handle: "
				"hdl="DF_UUID" capas="DF_U64"\n",
				DP_CONT(in->tcoi_pool, in->tcoi_cont),
				DP_UUID(in->tcoi_cont_hdl), hdl->tch_capas);
			rc = -DER_EXIST;
		}
		dsms_tgt_cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
		return rc;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	hdl->tch_pool = vpool_lookup(in->tcoi_pool);
	if (hdl->tch_pool == NULL)
		D_GOTO(err_hdl, rc = -DER_NO_PERM);

	rc = vcont_lookup(tls->dt_cont_cache, in->tcoi_cont, hdl->tch_pool,
			  &hdl->tch_cont);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DF_DSMS, DF_CONT": creating new vos container\n",
			DP_CONT(hdl->tch_pool->dvp_uuid, in->tcoi_cont));

		rc = vos_co_create(hdl->tch_pool->dvp_hdl, in->tcoi_cont);
		if (rc != 0)
			D_GOTO(err_pool, rc);

		vos_co_created = 1;

		rc = vcont_lookup(tls->dt_cont_cache, in->tcoi_cont,
				  hdl->tch_pool, &hdl->tch_cont);
		if (rc != 0)
			D_GOTO(err_vos_co, rc);
	} else if (rc != 0) {
		D_GOTO(err_pool, rc);
	}

	uuid_copy(hdl->tch_uuid, in->tcoi_cont_hdl);
	hdl->tch_capas = in->tcoi_capas;

	rc = tgt_cont_hdl_add(&tls->dt_cont_hdl_hash, hdl);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	return 0;

err_cont:
	vcont_put(tls->dt_cont_cache, hdl->tch_cont);
err_vos_co:
	if (vos_co_created) {
		D_DEBUG(DF_DSMS, DF_CONT": destroying new vos container\n",
			DP_CONT(hdl->tch_pool->dvp_uuid, in->tcoi_cont));
		vos_co_destroy(hdl->tch_pool->dvp_hdl, in->tcoi_cont);
	}
err_pool:
	vpool_put(hdl->tch_pool);
err_hdl:
	D_FREE_PTR(hdl);
err:
	return rc;
}

int
dsms_hdlr_tgt_cont_open(dtp_rpc_t *rpc)
{
	struct tgt_cont_open_in	       *in = dtp_req_get(rpc);
	struct tgt_cont_open_out       *out = dtp_reply_get(rpc);
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: hdl="DF_UUID"\n",
		DP_CONT(in->tcoi_pool, in->tcoi_cont), rpc,
		DP_UUID(in->tcoi_cont_hdl));

	rc = dss_collective(es_cont_open, in);
	D_ASSERTF(rc == 0, "%d\n", rc);

	out->tcoo_ret = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->tcoi_cont), rpc, out->tcoo_ret, rc);
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_tgt_cont_open_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
				  void *priv)
{
	struct tgt_cont_open_out    *out_source = dtp_reply_get(source);
	struct tgt_cont_open_out    *out_result = dtp_reply_get(result);

	out_result->tcoo_ret += out_source->tcoo_ret;
	return 0;
}

/*
 * Called via dss_collective() to close the per-thread container handle
 * (i.e., tgt_cont_hdl).
 */
static int
es_cont_close(void *vin)
{
	struct tgt_cont_close_in       *in = vin;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct tgt_cont_hdl	       *hdl;

	hdl = dsms_tgt_cont_hdl_lookup_internal(&tls->dt_cont_hdl_hash,
						in->tcci_cont_hdl);
	if (hdl == NULL)
		return 0;

	tgt_cont_hdl_delete(&tls->dt_cont_hdl_hash, hdl);

	dsms_tgt_cont_hdl_put_internal(&tls->dt_cont_hdl_hash, hdl);
	return 0;
}

int
dsms_hdlr_tgt_cont_close(dtp_rpc_t *rpc)
{
	struct tgt_cont_close_in       *in = dtp_req_get(rpc);
	struct tgt_cont_close_out      *out = dtp_reply_get(rpc);
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: hdl="DF_UUID"\n",
		DP_CONT(NULL, NULL), rpc, DP_UUID(in->tcci_cont_hdl));

	rc = dss_collective(es_cont_close, in);
	D_ASSERTF(rc == 0, "%d\n", rc);

	out->tcco_ret = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d (%d)\n",
		DP_CONT(NULL, NULL), rpc, out->tcco_ret, rc);
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_tgt_cont_close_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
				   void *priv)
{
	struct tgt_cont_close_out    *out_source = dtp_reply_get(source);
	struct tgt_cont_close_out    *out_result = dtp_reply_get(result);

	out_result->tcco_ret += out_source->tcco_ret;
	return 0;
}
