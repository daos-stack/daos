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
 * ds_pool: Target Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related target states.
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

#include <daos_srv/pool.h>

#include <daos/pool_map.h>
#include <daos/transport.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/vos.h>
#include "rpc.h"
#include "srv_internal.h"

/*
 * dsms_vpool objects: thread-local pool cache
 */
struct dsms_vpool *
vpool_lookup(const uuid_t vp_uuid)
{
	struct dsms_vpool *dvp;
	struct dsm_tls  *tls = dsm_tls_get();

	daos_list_for_each_entry(dvp, &tls->dt_pool_list, dvp_list) {
		if (uuid_compare(vp_uuid, dvp->dvp_uuid) == 0) {
			dvp->dvp_ref++;
			return dvp;
		}
	}
	return NULL;
}

void
vpool_put(struct dsms_vpool *vpool)
{
	D_ASSERTF(vpool->dvp_ref > 0, "%d\n", vpool->dvp_ref);
	vpool->dvp_ref--;
	if (vpool->dvp_ref == 0) {
		D_DEBUG(DF_DSMS, DF_UUID": destroying\n",
			DP_UUID(vpool->dvp_uuid));
		daos_list_del(&vpool->dvp_list);
		vos_pool_close(vpool->dvp_hdl);
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

	vpool = vpool_lookup(arg->pla_uuid);
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

	rc = vos_pool_open(path, arg->pla_uuid, &vpool->dvp_hdl);

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
	struct dsms_vpool      *vpool;

	vpool = vpool_lookup(uuid);
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
