/**
 * (C) Copyright 2018 Intel Corporation.
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
 * ds_pool: RDB Operations
 *
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


enum rdb_start_flag {
	RDB_AF_CREATE	= 1
};

enum rdb_stop_flag {
	RDB_OF_DESTROY	= 1
};

static int
bcast_create(crt_opcode_t opc, crt_group_t *group, crt_rpc_t **rpc);

/**
 * Perform a distributed create, if \a create is true, and start operation on
 * all replicas of a database with \a dbid spanning \a ranks. This method can
 * be called on any rank. If \a create is false, \a ranks may be NULL.
 *
 * \param[in]	dbid		database UUID
 * \param[in]	pool_uuid	pool UUID (for ds_mgmt_tgt_file())
 * \param[in]	ranks		list of replica ranks
 * \param[in]	create		create replicas first
 * \param[in]	size		size of each replica in bytes if \a create
 */
int
ds_pool_rdb_dist_start(const uuid_t dbid, const uuid_t pool_uuid,
		       const d_rank_list_t *ranks, bool create, size_t size)
{
	crt_rpc_t			*rpc;
	struct pool_rdb_start_in	*in;
	struct pool_rdb_start_out	*out;
	int				 rc;

	D_ASSERT(!create || ranks != NULL);
	D_DEBUG(DB_MD, DF_UUID": %s db "DF_UUIDF"\n", DP_UUID(pool_uuid),
		create ? "creating" : "starting", DP_UUID(dbid));

	/*
	 * If ranks doesn't include myself, creating a group with ranks will
	 * fail; bcast to the primary group instead.
	 */
	rc = bcast_create(POOL_RDB_START, NULL /* group */, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);
	in = crt_req_get(rpc);
	uuid_copy(in->dai_dbid, dbid);
	uuid_copy(in->dai_pool, pool_uuid);
	if (create)
		in->dai_flags |= RDB_AF_CREATE;
	in->dai_size = size;
	in->dai_ranks = (d_rank_list_t *)ranks;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->dao_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start%s %d replicas\n",
			DP_UUID(dbid), create ? "/create" : "", rc);
		ds_pool_rdb_dist_stop(pool_uuid, ranks, create /* destroy */);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

void
ds_pool_rdb_start_handler(crt_rpc_t *rpc)
{
	struct pool_rdb_start_in    *in = crt_req_get(rpc);
	struct pool_rdb_start_out   *out = crt_reply_get(rpc);
	bool			     created = false;
	char			    *path = NULL;
	int			     rc;

	if (in->dai_flags & RDB_AF_CREATE && in->dai_ranks == NULL)
		D_GOTO(out, rc = -DER_PROTO);

	if (in->dai_ranks != NULL) {
		d_rank_t	rank;
		int		i;

		/* Do nothing if I'm not one of the replicas. */
		rc = crt_group_rank(NULL /* grp */, &rank);
		D_ASSERTF(rc == 0, "%d\n", rc);
		if (!daos_rank_list_find(in->dai_ranks, rank, &i))
			D_GOTO(out, rc = 0);
	}

	if (in->dai_flags & RDB_AF_CREATE) {
		path = ds_pool_svc_rdb_path(in->dai_pool);
		if (path == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}

		rc = rdb_create(path, in->dai_dbid, in->dai_size,
				in->dai_ranks);
		if (rc == 0) {
			rc = ds_pool_svc_rdb_uuid_store(in->dai_pool,
							in->dai_dbid);
			if (rc != 0) {
				rdb_destroy(path, in->dai_dbid);
				goto out_path;
			}
			created = true;
		} else if (rc != -DER_EXIST) {
			D_ERROR(DF_UUID": failed to create replica: %d\n",
				DP_UUID(in->dai_dbid), rc);
			D_GOTO(out_path, rc);
		}

	}

	rc = ds_pool_svc_start(in->dai_pool);
	if (rc != 0) {
		if ((in->dai_flags & RDB_AF_CREATE) || rc != -DER_NONEXIST)
			D_ERROR(DF_UUID": failed to start replica: %d\n",
				DP_UUID(in->dai_dbid), rc);
		if (created) {
			ds_pool_svc_rdb_uuid_remove(in->dai_pool);
			rdb_destroy(path, in->dai_dbid);
		}
	}

out_path:
	if (path != NULL)
		free(path);
out:
	out->dao_rc = (rc == 0 ? 0 : 1);
	crt_reply_send(rpc);
}

int
ds_pool_rdb_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_rdb_start_out   *out_source;
	struct pool_rdb_start_out   *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->dao_rc += out_source->dao_rc;
	return 0;
}

/**
 * Perform a distributed stop, and if \a destroy is true, destroy operation on
 * all replicas of a database spanning \a ranks. This method can be called on
 * any rank. \a ranks may be NULL.
 *
 * \param[in]	pool_uuid	pool UUID (for ds_mgmt_tgt_file())
 * \param[in]	ranks		list of \a ranks->rl_nr replica ranks
 * \param[in]	destroy		destroy after close
 */
int
ds_pool_rdb_dist_stop(const uuid_t pool_uuid, const d_rank_list_t *ranks,
		      bool destroy)
{
	crt_rpc_t			*rpc;
	struct pool_rdb_stop_in		*in;
	struct pool_rdb_stop_out	*out;
	int				 rc;

	/*
	 * If ranks doesn't include myself, creating a group with ranks will
	 * fail; bcast to the primary group instead.
	 */
	rc = bcast_create(POOL_RDB_STOP, NULL /* group */, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);
	in = crt_req_get(rpc);
	uuid_copy(in->doi_pool, pool_uuid);
	if (destroy)
		in->doi_flags |= RDB_OF_DESTROY;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->doo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to stop%s %d replicas\n",
			DP_UUID(pool_uuid), destroy ? "/destroy" : "", rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

void
ds_pool_rdb_stop_handler(crt_rpc_t *rpc)
{
	struct pool_rdb_stop_in		*in = crt_req_get(rpc);
	struct pool_rdb_stop_out	*out = crt_reply_get(rpc);
	int				 rc = 0;

	ds_pool_svc_stop(in->doi_pool);

	if (in->doi_flags & RDB_OF_DESTROY) {
		uuid_t	uuid;
		char   *path;

		rc = ds_pool_svc_rdb_uuid_load(in->doi_pool, uuid);
		if (rc != 0) {
			if (rc == -DER_NONEXIST)
				rc = 0;
			goto out;
		}
		path = ds_pool_svc_rdb_path(in->doi_pool);
		if (path == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		rc = rdb_destroy(path, uuid);
		free(path);
		if (rc == 0)
			rc = ds_pool_svc_rdb_uuid_remove(in->doi_pool);
		if (rc == -DER_NONEXIST)
			rc = 0;
		else if (rc != 0)
			D_ERROR(DF_UUID": failed to destroy replica: %d\n",
				DP_UUID(uuid), rc);
	}

out:
	out->doo_rc = (rc == 0 ? 0 : 1);
	crt_reply_send(rpc);
}

int
ds_pool_rdb_stop_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_rdb_stop_out   *out_source;
	struct pool_rdb_stop_out   *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->doo_rc += out_source->doo_rc;
	return 0;
}

static int
bcast_create(crt_opcode_t opc, crt_group_t *group, crt_rpc_t **rpc)
{
	struct dss_module_info *info = dss_get_module_info();
	crt_opcode_t		opc_full;

	opc_full = DAOS_RPC_OPCODE(opc, DAOS_POOL_MODULE, DAOS_POOL_VERSION);
	return crt_corpc_req_create(info->dmi_ctx, group,
				    NULL /* excluded_ranks */, opc_full,
				    NULL /* co_bulk_hdl */, NULL /* priv */,
				    0 /* flags */,
				    crt_tree_topo(CRT_TREE_FLAT, 0), rpc);
}
