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

#include <daos_srv/daos_m_srv.h>
#include <uuid/uuid.h>
#include <daos/pool_map.h>
#include <daos/transport.h>
#include <daos_srv/vos.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

/*
 * dsms_vpool objects: thread-local pool cache
 */

static struct dsms_vpool *
vpool_lookup(daos_list_t *list, const uuid_t vp_uuid)
{
	struct dsms_vpool *dvp;

	daos_list_for_each_entry(dvp, list, dvp_list) {
		if (uuid_compare(vp_uuid, dvp->dvp_uuid) == 0) {
			dvp->dvp_ref++;
			return dvp;
		}
	}
	return NULL;
}

static void
vpool_put(struct dsms_vpool *vpool)
{
	D_ASSERTF(vpool->dvp_ref > 0, "%d\n", vpool->dvp_ref);
	vpool->dvp_ref--;
	if (vpool->dvp_ref == 0) {
		D_DEBUG(DF_DSMS, DF_UUID": destroying\n",
			DP_UUID(vpool->dvp_uuid));
		daos_list_del(&vpool->dvp_list);
		vos_pool_close(vpool->dvp_hdl, NULL /* ev */);
		D_FREE_PTR(vpool);
	}
}

struct es_pool_lookup_arg {
	void	       *pla_uuid;
	uint32_t	pla_map_version;
};

/*
 * Called via dss_collective() to look up or create the per-thread pool object.
 */
static int
es_pool_lookup(void *varg)
{
	struct es_pool_lookup_arg      *arg = varg;
	struct dsm_tls		       *tls = dsm_tls_get();
	struct dsms_vpool	       *vpool;
	struct dss_module_info	       *info = dss_get_module_info();
	char			       *path;
	int				rc;

	vpool = vpool_lookup(&tls->dt_pool_list, arg->pla_uuid);
	if (vpool != NULL) {
		vpool_put(vpool);
		return 0;
	}

	D_DEBUG(DF_DSMS, DF_UUID": creating\n", DP_UUID(arg->pla_uuid));

	D_ALLOC_PTR(vpool);
	if (vpool == NULL)
		return -DER_NOMEM;

	rc = dmgs_tgt_file(arg->pla_uuid, VOS_FILE, &info->dmi_tid, &path);
	if (rc != 0) {
		D_FREE_PTR(vpool);
		return rc;
	}

	rc = vos_pool_open(path, arg->pla_uuid, &vpool->dvp_hdl, NULL /* ev */);

	free(path);

	if (rc != 0) {
		D_FREE_PTR(vpool);
		return rc;
	}

	uuid_copy(vpool->dvp_uuid, arg->pla_uuid);
	vpool->dvp_map_version = arg->pla_map_version;
	vpool->dvp_ref = 1;
	daos_list_add(&vpool->dvp_list, &tls->dt_pool_list);
	return 0;
}

/* Called via dss_collective() to put or free the per-thread pool object. */
static int
es_pool_put(void *uuid)
{
	struct dsm_tls	       *tls = dsm_tls_get();
	struct dsms_vpool      *vpool;

	vpool = vpool_lookup(&tls->dt_pool_list, uuid);
	if (vpool == NULL)
		return 0;

	vpool_put(vpool);
	vpool_put(vpool);
	return 0;
}

/*
 * tgt_pool objects: global pool cache
 */

static struct daos_lru_cache *tgt_pool_cache;

static inline struct tgt_pool *
tgt_pool_obj(struct daos_llink *llink)
{
	return container_of(llink, struct tgt_pool, tp_entry);
}

enum map_ranks_class {
	MAP_RANKS_UP,
	MAP_RANKS_DOWN
};

static inline int
map_ranks_include(enum map_ranks_class class, int status)
{
	return (class == MAP_RANKS_UP && (status == PO_COMP_ST_UP ||
					  status == PO_COMP_ST_UPIN)) ||
	       (class == MAP_RANKS_DOWN && (status == PO_COMP_ST_DOWN ||
					    status == PO_COMP_ST_DOWNOUT));
}

/* Build a rank list of targets with certain status. */
static int
map_ranks_init(const struct pool_map *map, enum map_ranks_class class,
	       daos_rank_list_t *ranks)
{
	struct pool_target     *targets;
	int			ntargets;
	int			n = 0;
	int			i;

	ntargets = pool_map_find_target((struct pool_map *)map, PO_COMP_ID_ALL,
					&targets);
	if (ntargets == 0) {
		D_ERROR("no targets in pool map\n");
		return -DER_IO;
	}

	ranks->rl_nr.num = 0;
	ranks->rl_nr.num_out = 0;

	for (i = 0; i < ntargets; i++)
		if (map_ranks_include(class, targets[i].ta_comp.co_status))
			ranks->rl_nr.num++;

	D_ALLOC(ranks->rl_ranks, sizeof(*ranks->rl_ranks) * ranks->rl_nr.num);
	if (ranks->rl_ranks == NULL)
		return -DER_NOMEM;

	for (i = 0; i < ntargets; i++) {
		if (!map_ranks_include(class, targets[i].ta_comp.co_status))
			continue;
		ranks->rl_ranks[n] = targets[i].ta_comp.co_rank;
		n++;
	}
	D_ASSERTF(n == ranks->rl_nr.num, "%d != %d\n", n, ranks->rl_nr.num);

	return 0;
}

static void
map_ranks_fini(daos_rank_list_t *ranks)
{
	D_FREE(ranks->rl_ranks, sizeof(*ranks->rl_ranks) * ranks->rl_nr.num);
}

static int
group_create_cb(dtp_group_t *grp, void *priv, int status)
{
	ABT_eventual *eventual = priv;

	if (status != 0) {
		D_ERROR("failed to create pool group: %d\n", status);
		grp = NULL;
	}
	ABT_eventual_set(*eventual, &grp, sizeof(grp));
	return 0;
}

/* Create the dtp group of a pool based on its pool map. */
static int
group_create(const uuid_t pool_uuid, const struct pool_map *map,
	     dtp_group_t **group)
{
	char			id[DAOS_UUID_STR_SIZE];
	daos_rank_list_t	ranks;
	ABT_eventual		eventual;
	dtp_group_t	      **g;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID"\n", DP_UUID(pool_uuid));

	uuid_unparse_lower(pool_uuid, id);

	rc = map_ranks_init(map, MAP_RANKS_UP, &ranks);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ABT_eventual_create(sizeof(*g), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_ranks, rc = dss_abterr2der(rc));

	/* "!populate_now" is not implemented yet. */
	rc = dtp_group_create(id, &ranks, true /* populate_now */,
			      group_create_cb, &eventual);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&g);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*g == NULL)
		D_GOTO(out_eventual, rc = -DER_IO);

	*group = *g;
	rc = 0;
out_eventual:
	ABT_eventual_free(&eventual);
out_ranks:
	map_ranks_fini(&ranks);
out:
	return rc;
}

static int
group_destroy_cb(void *args, int status)
{
	ABT_eventual *eventual = args;

	ABT_eventual_set(*eventual, &status, sizeof(status));
	return 0;
}

static int
group_destroy(dtp_group_t *group)
{
	ABT_eventual	eventual;
	int	       *status;
	int		rc;

	D_DEBUG(DF_DSMS, "%s\n", group->dg_grpid);

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = dtp_group_destroy(group, group_destroy_cb, &eventual);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

	rc = 0;
out_eventual:
	ABT_eventual_free(&eventual);
out:
	return rc;
}

static int
tgt_pool_alloc_ref(void *key, unsigned int ksize, void *varg,
		   struct daos_llink **link)
{
	struct tgt_pool_create_arg     *arg = varg;
	struct tgt_pool		       *pool;
	struct es_pool_lookup_arg	es_arg;
	int				rc;
	int				rc_tmp;

	if (arg == NULL)
		D_GOTO(err, rc = -DER_NONEXIST);

	D_DEBUG(DF_DSMS, DF_UUID": creating\n", DP_UUID(key));

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	uuid_copy(pool->tp_uuid, key);
	pool->tp_map_version = arg->pca_map_version;

	if (arg->pca_map_buf != NULL) {
		rc = pool_map_create(arg->pca_map_buf, arg->pca_map_version,
				     &pool->tp_map);
		if (rc != 0)
			D_GOTO(err_pool, rc);
	}

	es_arg.pla_uuid = key;
	es_arg.pla_map_version = arg->pca_map_version;

	rc = dss_collective(es_pool_lookup, &es_arg);
	D_ASSERTF(rc == 0, "%d\n", rc);

	if (arg->pca_create_group) {
		D_ASSERT(pool->tp_map != NULL);
		rc = group_create(key, pool->tp_map, &pool->tp_group);
		if (rc != 0)
			D_GOTO(err_collective, rc);
	}

	*link = &pool->tp_entry;
	return 0;

err_collective:
	rc_tmp = dss_collective(es_pool_put, key);
	D_ASSERTF(rc_tmp == 0, "%d\n", rc_tmp);
	if (arg->pca_map_buf != NULL)
		pool_map_destroy(pool->tp_map);
err_pool:
	D_FREE_PTR(pool);
err:
	return rc;
}

static void
tgt_pool_free_ref(struct daos_llink *llink)
{
	struct tgt_pool	       *pool = tgt_pool_obj(llink);
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": freeing\n", DP_UUID(pool->tp_uuid));

	if (pool->tp_group != NULL) {
		rc = group_destroy(pool->tp_group);
		D_ASSERTF(rc == 0, "%d\n", rc);
	}

	rc = dss_collective(es_pool_put, pool->tp_uuid);
	D_ASSERTF(rc == 0, "%d\n", rc);

	if (pool->tp_map != NULL)
		pool_map_destroy(pool->tp_map);

	D_FREE_PTR(pool);
}

static bool
tgt_pool_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct tgt_pool *pool = tgt_pool_obj(llink);

	return uuid_compare(key, pool->tp_uuid) == 0;
}

static struct daos_llink_ops tgt_pool_cache_ops = {
	.lop_alloc_ref	= tgt_pool_alloc_ref,
	.lop_free_ref	= tgt_pool_free_ref,
	.lop_cmp_keys	= tgt_pool_cmp_keys
};

/*
 * If "arg == NULL", then this is assumed to be a pure lookup. In this case,
 * -DER_NONEXIST is returned if the tgt_pool object does not exist in the
 * cache. A group is only created if "arg->pca_create_group != 0".
 */
int
dsms_tgt_pool_lookup(const uuid_t uuid, struct tgt_pool_create_arg *arg,
		     struct tgt_pool **pool)
{
	struct daos_llink      *llink;
	int			rc;

	D_ASSERT(arg == NULL || !arg->pca_create_group ||
		 arg->pca_map_buf != NULL);

	rc = daos_lru_ref_hold(tgt_pool_cache, (void *)uuid, sizeof(uuid_t),
			       arg, &llink);
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

	*pool = tgt_pool_obj(llink);
	return 0;
}

void
dsms_tgt_pool_put(struct tgt_pool *pool)
{
	daos_lru_ref_release(tgt_pool_cache, &pool->tp_entry);
}

/*
 * tgt_pool_hdl objects: global pool handle hash table
 */

static struct dhash_table *tgt_pool_hdl_hash;

static inline struct tgt_pool_hdl *
tgt_pool_hdl_obj(daos_list_t *rlink)
{
	return container_of(rlink, struct tgt_pool_hdl, tph_entry);
}

static bool
tgt_pool_hdl_key_cmp(struct dhash_table *htable, daos_list_t *rlink,
		     const void *key, unsigned int ksize)
{
	struct tgt_pool_hdl *hdl = tgt_pool_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->tph_uuid, key) == 0;
}

static void
tgt_pool_hdl_rec_addref(struct dhash_table *htable, daos_list_t *rlink)
{
	tgt_pool_hdl_obj(rlink)->tph_ref++;
}

static bool
tgt_pool_hdl_rec_decref(struct dhash_table *htable, daos_list_t *rlink)
{
	struct tgt_pool_hdl *hdl = tgt_pool_hdl_obj(rlink);

	D_ASSERTF(hdl->tph_ref > 0, "%d\n", hdl->tph_ref);
	hdl->tph_ref--;
	return hdl->tph_ref == 0;
}

static void
tgt_pool_hdl_rec_free(struct dhash_table *htable, daos_list_t *rlink)
{
	struct tgt_pool_hdl *hdl = tgt_pool_hdl_obj(rlink);

	D_DEBUG(DF_DSMS, DF_UUID": freeing "DF_UUID"\n",
		DP_UUID(hdl->tph_pool->tp_uuid), DP_UUID(hdl->tph_uuid));
	D_ASSERT(dhash_rec_unlinked(&hdl->tph_entry));
	D_ASSERTF(hdl->tph_ref == 0, "%d\n", hdl->tph_ref);
	dsms_tgt_pool_put(hdl->tph_pool);
	D_FREE_PTR(hdl);
}

static dhash_table_ops_t tgt_pool_hdl_hash_ops = {
	.hop_key_cmp	= tgt_pool_hdl_key_cmp,
	.hop_rec_addref	= tgt_pool_hdl_rec_addref,
	.hop_rec_decref	= tgt_pool_hdl_rec_decref,
	.hop_rec_free	= tgt_pool_hdl_rec_free
};

static int
tgt_pool_hdl_add(struct tgt_pool_hdl *hdl)
{
	return dhash_rec_insert(tgt_pool_hdl_hash, hdl->tph_uuid,
				sizeof(uuid_t), &hdl->tph_entry,
				true /* exclusive */);
}

static void
tgt_pool_hdl_delete(struct tgt_pool_hdl *hdl)
{
	bool deleted;

	deleted = dhash_rec_delete(tgt_pool_hdl_hash, hdl->tph_uuid,
				   sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

struct tgt_pool_hdl *
dsms_tgt_pool_hdl_lookup(const uuid_t uuid)
{
	daos_list_t *rlink;

	rlink = dhash_rec_find(tgt_pool_hdl_hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return tgt_pool_hdl_obj(rlink);
}

void
dsms_tgt_pool_hdl_put(struct tgt_pool_hdl *hdl)
{
	dhash_rec_decref(tgt_pool_hdl_hash, &hdl->tph_entry);
}

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

	rc = vos_co_open(pool->dvp_hdl, key, &cont->dvc_hdl, NULL /* ev */);
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
	vos_co_close(cont->dvc_hdl, NULL /* ev */);
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

int
dsms_hdlr_tgt_pool_connect(dtp_rpc_t *rpc)
{
	struct tgt_pool_connect_in     *in = dtp_req_get(rpc);
	struct tgt_pool_connect_out    *out = dtp_reply_get(rpc);
	struct tgt_pool		       *pool;
	struct tgt_pool_hdl	       *hdl;
	struct tgt_pool_create_arg	arg;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": handling rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->tpci_pool), rpc, DP_UUID(in->tpci_pool_hdl));

	hdl = dsms_tgt_pool_hdl_lookup(in->tpci_pool_hdl);
	if (hdl != NULL) {
		if (hdl->tph_capas == in->tpci_capas) {
			D_DEBUG(DF_DSMS, DF_UUID": found compatible pool "
				"handle: hdl="DF_UUID" capas="DF_U64"\n",
				DP_UUID(in->tpci_pool),
				DP_UUID(in->tpci_pool_hdl), hdl->tph_capas);
			rc = 0;
		} else {
			D_ERROR(DF_UUID": found conflicting pool handle: hdl="
				DF_UUID" capas="DF_U64"\n",
				DP_UUID(in->tpci_pool),
				DP_UUID(in->tpci_pool_hdl), hdl->tph_capas);
			rc = -DER_EXIST;
		}
		dsms_tgt_pool_hdl_put(hdl);
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	arg.pca_map_buf = NULL;
	arg.pca_map_version = in->tpci_pool_map_version;
	arg.pca_create_group = 0;

	rc = dsms_tgt_pool_lookup(in->tpci_pool, &arg,
				  &pool);
	if (rc != 0) {
		D_FREE_PTR(hdl);
		D_GOTO(out, rc);
	}

	uuid_copy(hdl->tph_uuid, in->tpci_pool_hdl);
	hdl->tph_capas = in->tpci_capas;
	hdl->tph_pool = pool;

	rc = tgt_pool_hdl_add(hdl);
	if (rc != 0) {
		dsms_tgt_pool_put(pool);
		D_GOTO(out, rc);
	}

out:
	out->tpco_ret = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->tpci_pool), rpc, out->tpco_ret, rc);
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_tgt_pool_connect_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
				     void *priv)
{
	struct tgt_pool_connect_out    *out_source = dtp_reply_get(source);
	struct tgt_pool_connect_out    *out_result = dtp_reply_get(result);

	out_result->tpco_ret += out_source->tpco_ret;
	return 0;
}

int
dsms_hdlr_tgt_pool_disconnect(dtp_rpc_t *rpc)
{
	struct tgt_pool_disconnect_in  *in = dtp_req_get(rpc);
	struct tgt_pool_disconnect_out *out = dtp_reply_get(rpc);
	struct tgt_pool_hdl	       *hdl;
	int				rc = 0;

	D_DEBUG(DF_DSMS, DF_UUID": handling rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->tpdi_pool), rpc, DP_UUID(in->tpdi_pool_hdl));

	hdl = dsms_tgt_pool_hdl_lookup(in->tpdi_pool_hdl);
	if (hdl == NULL) {
		D_DEBUG(DF_DSMS, DF_UUID": handle "DF_UUID" does not exist\n",
			DP_UUID(in->tpdi_pool), DP_UUID(in->tpdi_pool_hdl));
		D_GOTO(out, rc = 0);
	}

	tgt_pool_hdl_delete(hdl);

	/*
	 * TODO: Release all container handles associated with this pool
	 * handle.
	 */

	dsms_tgt_pool_hdl_put(hdl);
out:
	out->tpdo_ret = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d (%d)\n",
		DP_UUID(in->tpdi_pool), rpc, out->tpdo_ret, rc);
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_tgt_pool_disconnect_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
					void *priv)
{
	struct tgt_pool_disconnect_out *out_source = dtp_reply_get(source);
	struct tgt_pool_disconnect_out *out_result = dtp_reply_get(result);

	out_result->tpdo_ret += out_source->tpdo_ret;
	return 0;
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

	pool = vpool_lookup(&tls->dt_pool_list, in->tcdi_pool);
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

	rc = vos_co_destroy(pool->dvp_hdl, in->tcdi_cont, NULL /* ev */);

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

	hdl->tch_pool = vpool_lookup(&tls->dt_pool_list, in->tcoi_pool);
	if (hdl->tch_pool == NULL)
		D_GOTO(err_hdl, rc = -DER_NO_PERM);

	rc = vcont_lookup(tls->dt_cont_cache, in->tcoi_cont, hdl->tch_pool,
			  &hdl->tch_cont);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DF_DSMS, DF_CONT": creating new vos container\n",
			DP_CONT(hdl->tch_pool->dvp_uuid, in->tcoi_cont));

		rc = vos_co_create(hdl->tch_pool->dvp_hdl, in->tcoi_cont,
				   NULL /* ev */);
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
		vos_co_destroy(hdl->tch_pool->dvp_hdl, in->tcoi_cont,
			       NULL /* ev */);
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

int
dsms_module_target_init(void)
{
	int rc;

	rc = daos_lru_cache_create(0 /* bits */, DHASH_FT_NOLOCK /* feats */,
				   &tgt_pool_cache_ops, &tgt_pool_cache);
	if (rc != 0)
		return rc;

	rc = dhash_table_create(0 /* feats */, 4 /* bits */, NULL /* priv */,
				&tgt_pool_hdl_hash_ops, &tgt_pool_hdl_hash);
	if (rc != 0)
		daos_lru_cache_destroy(tgt_pool_cache);

	return rc;
}

void
dsms_module_target_fini(void)
{
	/* Currently, we use "force" to purge all tgt_pool_hdl objects. */
	dhash_table_destroy(tgt_pool_hdl_hash, true /* force */);
	daos_lru_cache_destroy(tgt_pool_cache);
}
