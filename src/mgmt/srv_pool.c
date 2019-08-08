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
/*
 * ds_mgmt: Pool Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/pool.h>
#include <daos_srv/bio.h>

#include "srv_internal.h"

static int
ds_mgmt_tgt_pool_destroy(uuid_t pool_uuid, crt_group_t *grp)
{
	struct ds_pool			*pool;
	crt_rpc_t			*td_req;
	struct mgmt_tgt_destroy_in	*td_in;
	d_rank_list_t			excluded = { 0 };
	struct mgmt_tgt_destroy_out	*td_out;
	unsigned int			opc;
	int				topo;
	int				rc;

	pool = ds_pool_lookup(pool_uuid);
	if (pool != NULL) {
		/* This may not be the pool leader node, so down targets
		 * may not be updated, then the following collective RPC
		 * might be timeout. XXX
		 */
		ABT_rwlock_rdlock(pool->sp_lock);
		rc = map_ranks_init(pool->sp_map, MAP_RANKS_DOWN, &excluded);
		ABT_rwlock_unlock(pool->sp_lock);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to create rank list: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			return rc;
		}
	}

	/* Collective RPC to destroy the pool on all of targets */
	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_DESTROY, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, grp,
				  &excluded, opc, NULL, NULL, 0, topo,
				  &td_req);
	if (rc)
		return rc;

	td_in = crt_req_get(td_req);
	D_ASSERT(td_in != NULL);
	uuid_copy(td_in->td_pool_uuid, pool_uuid);

	rc = dss_rpc_send(td_req);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	td_out = crt_reply_get(td_req);
	rc = td_out->td_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to update pool map on %d targets\n",
			DP_UUID(pool_uuid), rc);
out_rpc:
	crt_req_decref(td_req);
	map_ranks_fini(&excluded);

	return rc;
}

static int
ds_mgmt_pool_svc_create(uuid_t pool_uuid,
			int ntargets, uuid_t target_uuids[],
			const char *group, d_rank_list_t *ranks,
			daos_prop_t *prop, d_rank_list_t *svc_list)
{
	int	doms[ntargets];
	int	rc;
	int	i;

	D_DEBUG(DB_MGMT, DF_UUID": all tgts created, setting up pool "
		"svc\n", DP_UUID(pool_uuid));

	for (i = 0; i < ntargets; i++)
		doms[i] = 1;

	/**
	 * TODO: fetch domain list from external source
	 * Report 1 domain per target for now
	 */
	rc = ds_pool_svc_create(pool_uuid, ranks->rl_nr,
				target_uuids, group, ranks, ARRAY_SIZE(doms),
				doms, prop, svc_list);

	return rc;
}

static size_t
pool_rec_size(int nreplicas)
{
	struct pool_rec *rec = NULL;

	return sizeof(*rec) + sizeof(*rec->pr_replicas) * nreplicas;
}

static bool
pool_rec_valid(d_iov_t *v)
{
	struct pool_rec *rec;

	if (v->iov_len < sizeof(*rec)) {
		D_ERROR("invalid pool record header size %zu (not %zu)\n",
			v->iov_len, sizeof(*rec));
		return false;
	}
	rec = v->iov_buf;

	if (v->iov_len != pool_rec_size(rec->pr_nreplicas)) {
		D_ERROR("invalid pool record size %zu (not %zu)\n", v->iov_len,
			pool_rec_size(rec->pr_nreplicas));
		return false;
	}

	return true;
}

/* Look up the pool record's SCM address. */
static int
pool_rec_lookup(struct rdb_tx *tx, struct mgmt_svc *svc, uuid_t uuid,
		struct pool_rec **rec)
{
	d_iov_t	key;
	d_iov_t	value;
	int	rc;

	d_iov_set(&key, uuid, sizeof(uuid_t));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(tx, &svc->ms_pools, &key, &value);
	if (rc != 0)
		return rc;

	if (!pool_rec_valid(&value))
		return -DER_IO;

	*rec = value.iov_buf;
	return 0;
}

static int
pool_create_prepare(struct mgmt_svc *svc, uuid_t uuid, d_rank_list_t *tgts_in,
		    d_rank_list_t **tgts_out)
{
	struct rdb_tx	 tx;
	d_iov_t		 key;
	d_iov_t		 value;
	struct pool_rec	*rec;
	struct pool_rec	 recbuf = {};
	int		 rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_wrlock(svc->ms_lock);

	/* Look up the pool UUID. */
	rc = pool_rec_lookup(&tx, svc, uuid, &rec);
	if (rc == 0) {
		D_DEBUG(DB_MGMT, "found "DF_UUID" state=%u\n", DP_UUID(uuid),
			rec->pr_state);
		if (rec->pr_state & POOL_CREATING)
			rc = -DER_AGAIN;
		else
			rc = -DER_ALREADY;
		goto out_lock;
	} else if (rc != -DER_NONEXIST) {
		goto out_lock;
	}

	/*
	 * Determine which servers belong to the pool. This should consult the
	 * system map in the future.
	 */
	if (tgts_in != NULL) {
		rc = d_rank_list_dup_sort_uniq(tgts_out, tgts_in);
		if (rc != 0)
			goto out_lock;
	} else {
		uint32_t	n;
		int		i;

		rc = crt_group_size(NULL, &n);
		D_ASSERTF(rc == 0, "%d\n", rc);
		*tgts_out = d_rank_list_alloc(n);
		if (*tgts_out == NULL) {
			rc = -DER_NOMEM;
			goto out_lock;
		}
		for (i = 0; i < n; i++)
			(*tgts_out)->rl_ranks[i] = i;
	}

	/* Add a pool directory entry. */
	d_iov_set(&key, uuid, sizeof(uuid_t));
	recbuf.pr_state = POOL_CREATING;
	d_iov_set(&value, &recbuf, sizeof(recbuf));
	rc = rdb_tx_update(&tx, &svc->ms_pools, &key, &value);
	if (rc != 0) {
		D_ERROR("failed to add pool "DF_UUID" to directory: %d\n",
			DP_UUID(uuid), rc);
		goto out_tgts_out;
	}

	rc = rdb_tx_commit(&tx);

out_tgts_out:
	if (rc != 0)
		d_rank_list_free(*tgts_out);
out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

static int
pool_create_complete(struct mgmt_svc *svc, uuid_t uuid, d_rank_list_t *replicas)
{
	struct rdb_tx	 tx;
	d_iov_t		 key;
	d_iov_t		 value;
	struct pool_rec	*rec;
	size_t		 size = pool_rec_size(replicas->rl_nr);
	int		 rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_wrlock(svc->ms_lock);

	/* Complete the pool directory entry. */
	d_iov_set(&key, uuid, sizeof(uuid_t));
	D_ALLOC(rec, size);
	if (rec == NULL) {
		rc = -DER_NOMEM;
		goto out_lock;
	}
	D_ASSERTF(replicas->rl_nr <= UINT8_MAX, "%u\n", replicas->rl_nr);
	rec->pr_nreplicas = replicas->rl_nr;
	rec->pr_state = POOL_READY;
	D_CASSERT(sizeof(*rec->pr_replicas) == sizeof(*replicas->rl_ranks));
	memcpy(rec->pr_replicas, replicas->rl_ranks,
	       sizeof(*rec->pr_replicas) * replicas->rl_nr);
	d_iov_set(&value, rec, size);
	rc = rdb_tx_update(&tx, &svc->ms_pools, &key, &value);
	D_FREE(rec);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

static int
pool_rec_delete(struct mgmt_svc *svc, uuid_t uuid)
{
	struct rdb_tx	tx;
	d_iov_t		key;
	int		rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_wrlock(svc->ms_lock);

	d_iov_set(&key, uuid, sizeof(uuid_t));
	rc = rdb_tx_delete(&tx, &svc->ms_pools, &key);
	if (rc != 0) {
		D_ERROR("failed to delete pool "DF_UUID" from directory: %d\n",
			DP_UUID(uuid), rc);
		goto out_lock;
	}

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

static int ds_mgmt_pool_list(void);

int
ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev,
		    d_rank_list_t *targets, size_t scm_size, size_t nvme_size,
		    daos_prop_t *prop, uint32_t svc_nr, d_rank_list_t **svcp)
{
	struct mgmt_svc			*svc;
	crt_rpc_t			*tc_req;
	crt_opcode_t			opc;
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out;
	d_rank_t			*tc_out_ranks;
	uuid_t				*tc_out_uuids;
	crt_group_t			*grp = NULL;
	char				id[64];
	d_rank_list_t			*rank_list;
	uuid_t				*tgt_uuids = NULL;
	unsigned int			i;
	int				topo;
	int				rc;

	rc = ds_mgmt_svc_lookup_leader(&svc, NULL /* hint */);
	if (rc != 0)
		goto out;

	rc = pool_create_prepare(svc, pool_uuid, targets, &rank_list);
	if (rc != 0) {
		if (rc == -DER_ALREADY)
			rc = 0;
		goto out_svc;
	}

	/* Collective RPC to all of targets of the pool */
	D_CASSERT(sizeof(id) >= DAOS_UUID_STR_SIZE);
	uuid_unparse_lower(pool_uuid, id);
	rc = snprintf(id + DAOS_UUID_STR_SIZE - 1,
		      sizeof(id) - (DAOS_UUID_STR_SIZE - 1), "-tmp");
	D_ASSERT(rc >= 0 && rc + DAOS_UUID_STR_SIZE <= sizeof(id));
	rc = dss_group_create(id, rank_list, &grp);
	if (rc != 0)
		goto out_preparation;

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 4);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_CREATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, grp, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		goto out_grp;

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);
	uuid_copy(tc_in->tc_pool_uuid, pool_uuid);
	tc_in->tc_tgt_dev = tgt_dev;
	tc_in->tc_scm_size = scm_size;
	tc_in->tc_nvme_size = nvme_size;
	rc = dss_rpc_send(tc_req);
	if (rc != 0) {
		crt_req_decref(tc_req);
		goto out_grp;
	}

	tc_out = crt_reply_get(tc_req);
	rc = tc_out->tc_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to update pool map on %d targets\n",
			DP_UUID(tc_in->tc_pool_uuid), rc);
		crt_req_decref(tc_req);
		goto tgt_fail;
	}

	D_DEBUG(DB_MGMT, DF_UUID" create %zu tgts pool\n",
		DP_UUID(pool_uuid), tc_out->tc_tgt_uuids.ca_count);

	/** Gather target uuids ranks from collective RPC to start pool svc. */
	D_ALLOC_ARRAY(tgt_uuids, rank_list->rl_nr);
	if (tgt_uuids == NULL) {
		rc = -DER_NOMEM;
		goto tgt_fail;
	}
	tc_out_ranks = tc_out->tc_ranks.ca_arrays;
	tc_out_uuids = tc_out->tc_tgt_uuids.ca_arrays;
	for (i = 0; i < tc_out->tc_tgt_uuids.ca_count; i++) {
		int	idx;
		bool	found;

		found = daos_rank_list_find(rank_list, tc_out_ranks[i], &idx);
		D_ASSERT(found);

		/** copy returned target UUID */
		uuid_copy(tgt_uuids[idx], tc_out_uuids[i]);

		D_DEBUG(DB_TRACE, "fill ranks %d idx %d "DF_UUID"\n",
			tc_out_ranks[i], idx, DP_UUID(tc_out_uuids[i]));
	}

	crt_req_decref(tc_req);

	/** allocate service rank list */
	*svcp = d_rank_list_alloc(svc_nr);
	if (*svcp == NULL) {
		rc = -DER_NOMEM;
		goto out_uuids;
	}

	rc = ds_mgmt_pool_svc_create(pool_uuid, rank_list->rl_nr, tgt_uuids,
				     group, rank_list, prop, *svcp);
	if (rc) {
		D_ERROR("create pool "DF_UUID" svc failed: rc %d\n",
			DP_UUID(pool_uuid), rc);
		goto out_svcp;
	}

	rc = pool_create_complete(svc, pool_uuid, *svcp);
	if (rc != 0) {
		D_ERROR("failed to mark pool "DF_UUID" ready: %d\n",
			DP_UUID(pool_uuid), rc);
		ds_pool_svc_destroy(pool_uuid);
	}

	/* TODO: Remove this in DAOS-2529. */
	ds_mgmt_pool_list();

out_svcp:
	if (rc) {
		d_rank_list_free(*svcp);
		*svcp = NULL;
	}
out_uuids:
	D_FREE(tgt_uuids);
tgt_fail:
	if (rc)
		ds_mgmt_tgt_pool_destroy(pool_uuid, grp);
out_grp:
	dss_group_destroy(grp);
out_preparation:
	if (rc != 0)
		pool_rec_delete(svc, pool_uuid);
out_svc:
	ds_mgmt_svc_put_leader(svc);
out:
	D_DEBUG(DB_MGMT, "create pool "DF_UUID": %d\n", DP_UUID(pool_uuid), rc);
	return rc;
}

void
ds_mgmt_hdlr_pool_create(crt_rpc_t *rpc_req)
{
	struct mgmt_pool_create_in	*pc_in;
	struct mgmt_pool_create_out	*pc_out;
	int				 rc;

	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);
	pc_out = crt_reply_get(rpc_req);
	D_ASSERT(pc_out != NULL);

	rc = ds_mgmt_create_pool(pc_in->pc_pool_uuid, pc_in->pc_grp,
				 pc_in->pc_tgt_dev, pc_in->pc_tgts,
				 pc_in->pc_scm_size, pc_in->pc_nvme_size,
				 pc_in->pc_prop, pc_in->pc_svc_nr,
				 &pc_out->pc_svc);
	pc_out->pc_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d (pc_tgt_dev: %s).\n",
			rc, pc_in->pc_tgt_dev);
	if (pc_out->pc_rc == 0)
		d_rank_list_free(pc_out->pc_svc);
}

static int
pool_destroy_prepare(struct mgmt_svc *svc, uuid_t uuid)
{
	struct rdb_tx	 tx;
	d_iov_t		 key;
	d_iov_t		 value;
	struct pool_rec	*rec;
	struct pool_rec	*recbuf;
	size_t		 size;
	int		 rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_wrlock(svc->ms_lock);

	rc = pool_rec_lookup(&tx, svc, uuid, &rec);
	if (rc == 0) {
		if (!(rec->pr_state & POOL_READY)) {
			rc = -DER_AGAIN;
			goto out_lock;
		}
	} else if (rc == -DER_NONEXIST) {
		rc = -DER_ALREADY;
		goto out_lock;
	} else {
		goto out_lock;
	}
	size = pool_rec_size(rec->pr_nreplicas);

	d_iov_set(&key, uuid, sizeof(uuid_t));
	D_ALLOC(recbuf, size);
	if (recbuf == NULL) {
		rc = -DER_NOMEM;
		goto out_lock;
	}
	memcpy(recbuf, rec, size);
	recbuf->pr_state = POOL_DESTROYING;
	d_iov_set(&value, recbuf, size);
	rc = rdb_tx_update(&tx, &svc->ms_pools, &key, &value);
	D_FREE(recbuf);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

int
ds_mgmt_destroy_pool(uuid_t pool_uuid, const char *group, uint32_t force)
{
	struct mgmt_svc	*svc;
	int		rc;

	/* TODO check metadata about the pool's existence?
	 *      and check active pool connection for "force"
	 */
	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID"\n", DP_UUID(pool_uuid));

	rc = ds_mgmt_svc_lookup_leader(&svc, NULL /* hint */);
	if (rc != 0)
		goto out;

	rc = pool_destroy_prepare(svc, pool_uuid);
	if (rc != 0) {
		if (rc == -DER_ALREADY)
			rc = 0;
		goto out_svc;
	}

	rc = ds_pool_svc_destroy(pool_uuid);
	if (rc != 0) {
		D_ERROR("Failed to destroy pool service "DF_UUID": %d\n",
			DP_UUID(pool_uuid), rc);
		goto out_svc;
	}

	rc = ds_mgmt_tgt_pool_destroy(pool_uuid, NULL);
	if (rc != 0) {
		D_ERROR("Destroying pool "DF_UUID" failed, rc: %d.\n",
			DP_UUID(pool_uuid), rc);
		goto out_svc;
	}

	rc = pool_rec_delete(svc, pool_uuid);
	if (rc != 0) {
		D_ERROR("Failed to delete pool "DF_UUID" from directory: %d\n",
			DP_UUID(pool_uuid), rc);
		goto out_svc;
	}

	/* TODO: Remove this in DAOS-2529. */
	ds_mgmt_pool_list();

	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID" succeed.\n",
		DP_UUID(pool_uuid));
out_svc:
	ds_mgmt_svc_put_leader(svc);
out:
	return rc;
}

void
ds_mgmt_hdlr_pool_destroy(crt_rpc_t *rpc_req)
{
	struct mgmt_pool_destroy_in	*pd_in;
	struct mgmt_pool_destroy_out	*pd_out;
	int				rc;

	pd_in = crt_req_get(rpc_req);
	pd_out = crt_reply_get(rpc_req);
	D_ASSERT(pd_in != NULL && pd_out != NULL);

	pd_out->pd_rc = ds_mgmt_destroy_pool(pd_in->pd_pool_uuid,
					     pd_in->pd_grp, pd_in->pd_force);
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d.\n", rc);
}

static int
enum_pool_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct pool_rec	*rec;
	char		*buf;
	char		*p;
	size_t		 size;
	int		 i;

	if (key->iov_len != sizeof(uuid_t)) {
		D_ERROR("invalid key size: key="DF_U64"\n", key->iov_len);
		return -DER_IO;
	}
	if (!pool_rec_valid(val))
		return -DER_IO;
	rec = val->iov_buf;

	size = 4096;
	D_ALLOC(buf, size);
	if (buf == NULL)
		return -DER_NOMEM;
	p = buf;
	for (i = 0; i < rec->pr_nreplicas; i++) {
		int n;

		n = snprintf(p, buf + size - p, "%s%u", i == 0 ? "" : ",",
			     rec->pr_replicas[i]);
		if (n < 0 || n >= buf + size - p) {
			D_FREE(buf);
			return -DER_OVERFLOW;
		}
		p += n;
	}

	D_DEBUG(DB_MGMT, "  "DF_UUID": state=%u svc=%s\n",
		DP_UUID(key->iov_buf), rec->pr_state, buf);

	D_FREE(buf);
	return 0;
}

/* TODO: To be completed in DAOS-2529; currently only for testing purposes. */
int
ds_mgmt_pool_list(void)
{
	struct mgmt_svc	*svc;
	struct rdb_tx	 tx;
	int		 rc;

	rc = ds_mgmt_svc_lookup_leader(&svc, NULL /* hint */);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;
	ABT_rwlock_rdlock(svc->ms_lock);

	D_DEBUG(DB_MGMT, "pools:\n");
	rc = rdb_tx_iterate(&tx, &svc->ms_pools, false /* !backward */,
			    enum_pool_cb, NULL /* arg */);

	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_mgmt_svc_put_leader(svc);
out:
	return rc;
}

int
ds_mgmt_bio_health_query()
{
//	struct mgmt_svc	*svc;
	int		rc;
	struct dss_module_info *info = dss_get_module_info();
	struct bio_xs_context *bxc;
	struct bio_dev_state *health_stats;

	D_DEBUG(DB_MGMT, "Querying BIO Health Data\n");

	bxc = info->dmi_nvme_ctxt;
	//health_stats = bxc->bxc_blobstore->bb_dev_health.bdh_health_state;
	health_stats = get_bio_dev_state(bxc);
	D_ERROR("BIO Health Stats: temp = %u\n",
		health_stats->bds_temperature);

	rc = 0;
	return rc;
}

