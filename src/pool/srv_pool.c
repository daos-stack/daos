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
 * ds_pool: Pool Service
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related pool metadata.
 */

#define DD_SUBSYS	DD_FAC(pool)

#include <daos_srv/pool.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/rsvc.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/rdb.h>
#include <daos_srv/rebuild.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

struct cont_svc;

/* Pool service state in pool_svc::ps_term */
enum pool_svc_state {
	POOL_SVC_UP_EMPTY,	/* up but DB newly-created and empty */
	POOL_SVC_UP,		/* up and ready to serve */
	POOL_SVC_DOWN		/* down */
};

/* Pool service */
struct pool_svc {
	daos_list_t		ps_entry;
	uuid_t			ps_uuid;	/* pool UUID */
	int			ps_ref;
	ABT_rwlock		ps_lock;	/* for DB data */
	struct rdb	       *ps_db;
	rdb_path_t		ps_root;	/* root KVS */
	rdb_path_t		ps_handles;	/* pool handle KVS */
	struct cont_svc	       *ps_cont_svc;	/* one combined svc for now */
	ABT_mutex		ps_mutex;	/* for POOL_CREATE */
	bool			ps_stop;
	uint64_t		ps_term;
	enum pool_svc_state	ps_state;
	struct ds_pool	       *ps_pool;
};

static int
write_map_buf(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_buf *buf,
	      uint32_t version)
{
	daos_iov_t	value;
	int		rc;

	D_DEBUG(DF_DSMS, "version=%u ntargets=%u ndomains=%u\n", version,
		buf->pb_target_nr, buf->pb_domain_nr);

	/* Write the version. */
	daos_iov_set(&value, &version, sizeof(version));
	rc = rdb_tx_update(tx, kvs, &ds_pool_attr_map_version, &value);
	if (rc != 0)
		return rc;

	/* Write the buffer. */
	daos_iov_set(&value, buf, pool_buf_size(buf->pb_nr));
	return rdb_tx_update(tx, kvs, &ds_pool_attr_map_buffer, &value);
}

/*
 * Retrieve the pool map buffer address in persistent memory and the pool map
 * version into "map_buf" and "map_version", respectively.
 */
static int
read_map_buf(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_buf **buf,
	     uint32_t *version)
{
	uint32_t	ver;
	daos_iov_t	value;
	int		rc;

	/* Read the version. */
	daos_iov_set(&value, &ver, sizeof(ver));
	rc = rdb_tx_lookup(tx, kvs, &ds_pool_attr_map_version, &value);
	if (rc != 0)
		return rc;

	/* Look up the buffer address. */
	daos_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(tx, kvs, &ds_pool_attr_map_buffer, &value);
	if (rc != 0)
		return rc;

	*buf = value.iov_buf;
	*version = ver;
	D_DEBUG(DF_DSMS, "version=%u ntargets=%u ndomains=%u\n", *version,
		(*buf)->pb_target_nr, (*buf)->pb_domain_nr);
	return 0;
}

/* Callers are responsible for destroying the object via pool_map_decref(). */
static int
read_map(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_map **map)
{
	struct pool_buf	       *buf;
	uint32_t		version;
	int			rc;

	rc = read_map_buf(tx, kvs, &buf, &version);
	if (rc != 0)
		return rc;

	return pool_map_create(buf, version, map);
}

/*
 * Called by mgmt module on every storage node belonging to this pool.
 * "path" is the directory under which the VOS and metadata files shall be.
 * "target_uuid" returns the UUID generated for the target on this storage node.
 */
int
ds_pool_create(const uuid_t pool_uuid, const char *path, uuid_t target_uuid)
{
	char	       *fpath;
	int		fd;
	int		rc;

	uuid_generate(target_uuid);

	/* Store target_uuid in DSM_META_FILE. */
	rc = asprintf(&fpath, "%s/%s", path, DSM_META_FILE);
	if (rc < 0)
		return -DER_NOMEM;
	fd = open(fpath, O_WRONLY | O_CREAT | O_EXCL);
	if (rc < 0) {
		D_ERROR(DF_UUID": failed to create pool target file %s: %d\n",
			DP_UUID(pool_uuid), fpath, errno);
		D_GOTO(err_fpath, rc = daos_errno2der(errno));
	}
	rc = fsync(fd);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to fsync pool target file %s: %d\n",
			DP_UUID(pool_uuid), fpath, errno);
		D_GOTO(err_fd, rc = daos_errno2der(errno));
	}
	rc = close(fd);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to close pool target file %s: %d\n",
			DP_UUID(pool_uuid), fpath, errno);
		D_GOTO(err_file, rc = daos_errno2der(errno));
	}

	return 0;

err_fd:
	close(fd);
err_file:
	remove(fpath);
err_fpath:
	free(fpath);
	return rc;
}

static int
uuid_compare_cb(const void *a, const void *b)
{
	uuid_t *ua = (uuid_t *)a;
	uuid_t *ub = (uuid_t *)b;

	return uuid_compare(*ua, *ub);
}

static int
init_pool_metadata(struct rdb_tx *tx, const rdb_path_t *kvs, uint32_t uid,
		   uint32_t gid, uint32_t mode,
		   uint32_t ntargets, uuid_t target_uuids[], const char *group,
		   const daos_rank_list_t *target_addrs, uint32_t ndomains,
		   const int *domains)
{
	struct pool_buf	       *map_buf;
	struct pool_component	map_comp;
	uint32_t		map_version = 1;
	uint32_t		nhandles = 0;
	uuid_t		       *uuids;
	daos_iov_t		value;
	struct rdb_kvs_attr	attr;
	int			rc;
	int			i;

	/* Prepare the pool map attribute buffers. */
	map_buf = pool_buf_alloc(ntargets + ndomains);
	if (map_buf == NULL)
		return -DER_NOMEM;
	/*
	 * Make a sorted target UUID array to determine target IDs. See the
	 * bsearch() call below.
	 */
	D_ALLOC(uuids, sizeof(uuid_t) * ntargets);
	if (uuids == NULL)
		D_GOTO(out_map_buf, rc = -DER_NOMEM);
	memcpy(uuids, target_uuids, sizeof(uuid_t) * ntargets);
	qsort(uuids, ntargets, sizeof(uuid_t), uuid_compare_cb);
	/* Fill the pool_buf out. */
	for (i = 0; i < ndomains; i++) {
		map_comp.co_type = PO_COMP_TP_RACK;	/* TODO */
		map_comp.co_status = PO_COMP_ST_UP;
		map_comp.co_padding = 0;
		map_comp.co_id = i;
		map_comp.co_rank = 0;
		map_comp.co_ver = map_version;
		map_comp.co_fseq = 1;
		map_comp.co_nr = domains[i];

		rc = pool_buf_attach(map_buf, &map_comp, 1 /* comp_nr */);
		if (rc != 0)
			D_GOTO(out_uuids, rc);
	}
	for (i = 0; i < ntargets; i++) {
		uuid_t *p = bsearch(target_uuids[i], uuids, ntargets,
				    sizeof(uuid_t), uuid_compare_cb);

		map_comp.co_type = PO_COMP_TP_TARGET;
		map_comp.co_status = PO_COMP_ST_UP;
		map_comp.co_padding = 0;
		map_comp.co_id = p - uuids;
		map_comp.co_rank = target_addrs->rl_ranks[i];
		map_comp.co_ver = map_version;
		map_comp.co_fseq = 1;
		map_comp.co_nr = dss_nxstreams;

		rc = pool_buf_attach(map_buf, &map_comp, 1 /* comp_nr */);
		if (rc != 0)
			D_GOTO(out_uuids, rc);
	}

	/* Initialize the UID, GID, and mode attributes. */
	daos_iov_set(&value, &uid, sizeof(uid));
	rc = rdb_tx_update(tx, kvs, &ds_pool_attr_uid, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);
	daos_iov_set(&value, &gid, sizeof(gid));
	rc = rdb_tx_update(tx, kvs, &ds_pool_attr_gid, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);
	daos_iov_set(&value, &mode, sizeof(mode));
	rc = rdb_tx_update(tx, kvs, &ds_pool_attr_mode, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	/* Initialize the pool map attributes. */
	rc = write_map_buf(tx, kvs, map_buf, map_version);
	if (rc != 0)
		D_GOTO(out_uuids, rc);
	daos_iov_set(&value, uuids, sizeof(uuid_t) * ntargets);
	rc = rdb_tx_update(tx, kvs, &ds_pool_attr_map_uuids, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	/* Write the handle attributes. */
	daos_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(tx, kvs, &ds_pool_attr_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_pool_attr_handles, &attr);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	D_EXIT;
out_uuids:
	D_FREE(uuids, sizeof(uuid_t) * ntargets);
out_map_buf:
	pool_buf_free(map_buf);
	return rc;
}

/*
 * nreplicas inputs how many replicas are wanted, while ranks->rl_nr.num
 * outputs how many replicas are actually selected, which may be less than
 * nreplicas. If successful, callers are responsible for calling
 * daos_rank_list_free(*ranksp).
 */
static int
select_svc_ranks(int nreplicas, const daos_rank_list_t *target_addrs,
		 int ndomains, const int *domains, daos_rank_list_t **ranksp)
{
	int			i_rank_zero = -1;
	int			selectable;
	daos_rank_list_t       *ranks;
	int			i;
	int			j;

	if (nreplicas <= 0)
		return -DER_INVAL;

	/* Determine the number of selectable targets. */
	selectable = target_addrs->rl_nr.num;
	if (daos_rank_list_find((daos_rank_list_t *)target_addrs, 0 /* rank */,
				&i_rank_zero)) {
		/*
		 * Unless it is the only target available, we don't select rank
		 * 0 for now to avoid losing orterun stdout.
		 */
		if (selectable > 1)
			selectable -= 1 /* rank 0 */;
	}

	if (nreplicas > selectable)
		nreplicas = selectable;
	ranks = daos_rank_list_alloc(nreplicas);
	if (ranks == NULL)
		return -DER_NOMEM;

	/* TODO: Choose ranks according to failure domains. */
	j = 0;
	for (i = 0; i < target_addrs->rl_nr.num; i++) {
		if (j == ranks->rl_nr.num)
			break;
		if (i == i_rank_zero && selectable > 1)
			/* This is rank 0 and it's not the only rank. */
			continue;
		D_DEBUG(DB_MD, "ranks[%d]: %u\n", j, target_addrs->rl_ranks[i]);
		ranks->rl_ranks[j] = target_addrs->rl_ranks[i];
		j++;
	}
	D_ASSERTF(j == ranks->rl_nr.num, "%d == %u\n", j, ranks->rl_nr.num);

	*ranksp = ranks;
	return 0;
}

static size_t
get_md_cap(void)
{
	const size_t	size_default = 1 << 27 /* 128 MB */;
	char	       *v;
	int		n;

	v = getenv("DAOS_MD_CAP"); /* in MB */
	if (v == NULL)
		return size_default;
	n = atoi(v);
	if (n < size_default >> 20) {
		D_ERROR("metadata capacity too low; using %zu MB\n",
			size_default >> 20);
		return size_default;
	}
	return (size_t)n << 20;
}

/**
 * Create a (combined) pool(/container) service. This method shall be called on
 * a single storage node in the pool. "target_uuids" shall be an array of the
 * target UUIDs returned by the ds_pool_create() calls.
 *
 * \param[in]		pool_uuid	pool UUID
 * \param[in]		uid		pool UID
 * \param[in]		gid		pool GID
 * \param[in]		mode		pool mode
 * \param[in]		ntargets	number of targets in the pool
 * \param[in]		target_uuids	array of \a ntargets target UUIDs
 * \param[in]		group		crt group ID (unused now)
 * \param[in]		target_addrs	list of \a ntargets target ranks
 * \param[in]		ndomains	number of domains the pool spans over
 * \param[in]		domains		serialized domain tree
 * \param[in,out]	svc_addrs	\a svc_addrs.rl_nr.num inputs how many
 *					replicas shall be created; returns the
 *					list of pool service replica ranks
 */
int
ds_pool_svc_create(const uuid_t pool_uuid, unsigned int uid, unsigned int gid,
		   unsigned int mode, int ntargets, uuid_t target_uuids[],
		   const char *group, const daos_rank_list_t *target_addrs,
		   int ndomains, const int *domains,
		   daos_rank_list_t *svc_addrs)
{
	daos_rank_list_t       *ranks;
	char			id[DAOS_UUID_STR_SIZE];
	crt_group_t	       *g;
	struct rsvc_client	client;
	struct dss_module_info *info = dss_get_module_info();
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct pool_create_in  *in;
	struct pool_create_out *out;
	int			rc;

	D_ASSERTF(ntargets == target_addrs->rl_nr.num, "ntargets=%u num=%u\n",
		  ntargets, target_addrs->rl_nr.num);

	rc = select_svc_ranks(svc_addrs->rl_nr.num, target_addrs, ndomains,
			      domains, &ranks);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_UUID": creating pool group\n", DP_UUID(pool_uuid));
	uuid_unparse_lower(pool_uuid, id);
	rc = dss_group_create(id, (daos_rank_list_t *)target_addrs, &g);
	if (rc != 0)
		D_GOTO(out_ranks, rc);

	/* Use the pool UUID as the RDB UUID. */
	rc = rdb_dist_start(pool_uuid, pool_uuid, ranks, true /* create */,
			    get_md_cap());
	if (rc != 0)
		D_GOTO(out_group, rc);

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out_creation, rc);

rechoose:
	/* Create a POOL_CREATE request. */
	ep.ep_grp = NULL;
	rsvc_client_choose(&client, &ep);
	rc = pool_req_create(info->dmi_ctx, &ep, POOL_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create POOL_CREATE RPC: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}
	in = crt_req_get(rpc);
	uuid_copy(in->pri_op.pi_uuid, pool_uuid);
	uuid_clear(in->pri_op.pi_hdl);
	in->pri_uid = uid;
	in->pri_gid = gid;
	in->pri_mode = mode;
	in->pri_ntgts = ntargets;
	in->pri_tgt_uuids.da_count = ntargets;
	in->pri_tgt_uuids.da_arrays = target_uuids;
	in->pri_tgt_ranks = (daos_rank_list_t *)target_addrs;
	in->pri_ndomains = ndomains;
	in->pri_domains.da_count = ndomains;
	in->pri_domains.da_arrays = (int *)domains;

	/* Send the POOL_CREATE request. */
	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);
	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      rc == 0 ? out->pro_op.po_rc : -DER_IO,
				      rc == 0 ? &out->pro_op.po_hint : NULL);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}
	rc = out->pro_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_rpc, rc);
	}

	daos_rank_list_copy(svc_addrs, ranks, false /* !input */);
out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out_creation:
	if (rc != 0)
		rdb_dist_stop(pool_uuid, pool_uuid, ranks, true /* destroy */);
out_group:
	if (rc != 0)
		dss_group_destroy(g);
out_ranks:
	daos_rank_list_free(ranks);
out:
	return rc;
}

int
ds_pool_svc_destroy(const uuid_t pool_uuid)
{
	char		id[DAOS_UUID_STR_SIZE];
	crt_group_t    *group;
	int		rc;

	rc = rdb_dist_stop(pool_uuid, pool_uuid, NULL /* ranks */,
			   true /* destroy */);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to destroy pool service: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

	uuid_unparse_lower(pool_uuid, id);
	group = crt_group_lookup(id);
	if (group != NULL) {
		D_DEBUG(DB_MD, DF_UUID": destroying pool group\n",
			DP_UUID(pool_uuid));
		rc = dss_group_destroy(group);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to destroy pool group: %d\n",
				DP_UUID(pool_uuid), rc);
			return rc;
		}
	}

	return 0;
}

static int
pool_svc_step_up(struct pool_svc *svc)
{
	struct rdb_tx			tx;
	struct pool_map		       *map;
	uint32_t			map_version;
	struct ds_pool_create_arg	arg;
	struct ds_pool		       *pool;
	daos_rank_t			rank;
	int				rc;

	D_ASSERT(svc->ps_state != POOL_SVC_UP);
	D_DEBUG(DB_MD, DF_UUID": stepping up to "DF_U64"\n",
		DP_UUID(svc->ps_uuid), svc->ps_term);

	/* Read the pool map into map and map_version. */
	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		return rc;
	ABT_rwlock_rdlock(svc->ps_lock);
	rc = read_map(&tx, &svc->ps_root, &map);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DF_DSMS, DF_UUID": new db\n",
				DP_UUID(svc->ps_uuid));
		return rc;
	}
	map_version = pool_map_get_version(map);

	/* Create or revalidate svc->ps_pool with map and map_version. */
	D_ASSERT(svc->ps_pool == NULL);
	arg.pca_map = map;
	arg.pca_map_version = map_version;
	arg.pca_need_group = 1;
	rc = ds_pool_lookup_create(svc->ps_uuid, &arg, &svc->ps_pool);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to get ds_pool: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		pool_map_decref(map);
		return rc;
	}
	pool = svc->ps_pool;
	ABT_rwlock_wrlock(pool->sp_lock);
	if (pool->sp_map != map) {
		/* An existing ds_pool; map not used yet. */
		D_ASSERTF(pool->sp_map_version <= map_version, "%u <= %u\n",
			  pool->sp_map_version, map_version);
		D_ASSERTF(pool->sp_map == NULL ||
			  pool_map_get_version(pool->sp_map) <= map_version,
			  "%u <= %u\n", pool_map_get_version(pool->sp_map),
			  map_version);
		if (pool->sp_map == NULL ||
		    pool_map_get_version(pool->sp_map) < map_version) {
			struct pool_map *tmp;

			/* Need to update pool->sp_map. Swap with map. */
			pool->sp_map_version = map_version;
			tmp = pool->sp_map;
			pool->sp_map = map;
			map = tmp;
		}
		if (map != NULL)
			pool_map_decref(map);
	}
	ABT_rwlock_unlock(pool->sp_lock);

	ds_cont_svc_step_up(svc->ps_cont_svc);

	/*
	 * TODO: Call rebuild. Step down svc->ps_cont_svc and put svc->ps_pool
	 * on errors.
	 */

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_PRINT(DF_UUID": rank %u became pool service leader "DF_U64"\n",
		DP_UUID(svc->ps_uuid), rank, svc->ps_term);
	return 0;
}

static void
pool_svc_step_down(struct pool_svc *svc)
{
	daos_rank_t	rank;
	int		rc;

	D_ASSERT(svc->ps_state != POOL_SVC_DOWN);
	D_DEBUG(DB_MD, DF_UUID": stepping down from "DF_U64"\n",
		DP_UUID(svc->ps_uuid), svc->ps_term);

	ds_cont_svc_step_down(svc->ps_cont_svc);
	D_ASSERT(svc->ps_pool != NULL);
	ds_pool_put(svc->ps_pool);
	svc->ps_pool = NULL;

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_PRINT(DF_UUID": rank %u no longer pool service leader "DF_U64"\n",
		DP_UUID(svc->ps_uuid), rank, svc->ps_term);
}

static int
pool_svc_step_up_cb(struct rdb *db, uint64_t term, void *arg)
{
	struct pool_svc	       *svc = arg;
	int			rc;

	ABT_mutex_lock(svc->ps_mutex);
	if (svc->ps_stop) {
		D_DEBUG(DB_MD, DF_UUID": skip term "DF_U64" due to stopping\n",
			DP_UUID(svc->ps_uuid), term);
		D_GOTO(out_mutex, rc = 0);
	}
	D_ASSERTF(svc->ps_state == POOL_SVC_DOWN, "%d\n", svc->ps_state);
	svc->ps_term = term;

	rc = pool_svc_step_up(svc);
	if (rc == -DER_NONEXIST) {
		svc->ps_state = POOL_SVC_UP_EMPTY;
		D_GOTO(out_mutex, rc = 0);
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to step up as leader "DF_U64"\n",
			DP_UUID(svc->ps_uuid), term);
		/* TODO: Ask rdb to step down. */
		D_GOTO(out_mutex, rc = 0);
	}

	svc->ps_state = POOL_SVC_UP;
out_mutex:
	ABT_mutex_unlock(svc->ps_mutex);
	return rc;
}

static void
pool_svc_step_down_cb(struct rdb *db, uint64_t term, void *arg)
{
	struct pool_svc *svc = arg;

	ABT_mutex_lock(svc->ps_mutex);
	if (svc->ps_stop) {
		D_DEBUG(DB_MD, DF_UUID": skip term "DF_U64" due to stopping\n",
			DP_UUID(svc->ps_uuid), term);
		D_GOTO(out_mutex, 0);
	}
	D_ASSERTF(svc->ps_term == term, DF_U64" == "DF_U64"\n", svc->ps_term,
		  term);
	D_ASSERT(svc->ps_state != POOL_SVC_DOWN);

	if (svc->ps_state == POOL_SVC_UP)
		pool_svc_step_down(svc);

	svc->ps_state = POOL_SVC_DOWN;
out_mutex:
	ABT_mutex_unlock(svc->ps_mutex);
}

static struct rdb_cbs pool_svc_rdb_cbs = {
	.dc_step_up	= pool_svc_step_up_cb,
	.dc_step_down	= pool_svc_step_down_cb
};

char *
ds_pool_rdb_path(const uuid_t uuid, const uuid_t pool_uuid)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	char   *name;
	char   *path;
	int	rc;

	uuid_unparse_lower(uuid, uuid_str);
	rc = asprintf(&name, RDB_FILE"%s", uuid_str);
	if (rc < 0)
		return NULL;
	rc = ds_mgmt_tgt_file(pool_uuid, name, NULL /* idx */, &path);
	free(name);
	if (rc != 0)
		return NULL;
	return path;
}

static int
pool_svc_init(struct pool_svc *svc, const uuid_t uuid)
{
	char   *path;
	int	rc;

	uuid_copy(svc->ps_uuid, uuid);
	svc->ps_ref = 1;
	svc->ps_stop = false;
	svc->ps_state = POOL_SVC_DOWN;

	rc = ABT_rwlock_create(&svc->ps_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ps_lock: %d\n", rc);
		D_GOTO(err, rc = dss_abterr2der(rc));
	}

	rc = ABT_mutex_create(&svc->ps_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ps_mutex: %d\n", rc);
		D_GOTO(err_lock, rc = dss_abterr2der(rc));
	}

	rc = rdb_path_init(&svc->ps_root);
	if (rc != 0)
		D_GOTO(err_mutex, rc);
	rc = rdb_path_push(&svc->ps_root, &rdb_path_root_key);
	if (rc != 0)
		D_GOTO(err_root, rc);

	rc = rdb_path_clone(&svc->ps_root, &svc->ps_handles);
	if (rc != 0)
		D_GOTO(err_root, rc);
	rc = rdb_path_push(&svc->ps_handles, &ds_pool_attr_handles);
	if (rc != 0)
		D_GOTO(err_handles, rc);

	path = ds_pool_rdb_path(uuid, uuid);
	if (path == NULL)
		D_GOTO(err_handles, rc);
	rc = rdb_start(path, &pool_svc_rdb_cbs, svc, &svc->ps_db);
	free(path);
	if (rc != 0)
		D_GOTO(err_handles, rc);

	rc = ds_cont_svc_init(&svc->ps_cont_svc, uuid, 0 /* id */, svc->ps_db);
	if (rc != 0)
		D_GOTO(err_db, rc);

	return 0;

err_db:
	rdb_stop(svc->ps_db);
err_handles:
	rdb_path_fini(&svc->ps_handles);
err_root:
	rdb_path_fini(&svc->ps_root);
err_mutex:
	ABT_mutex_free(&svc->ps_mutex);
err_lock:
	ABT_rwlock_free(&svc->ps_lock);
err:
	return rc;
}

static void
pool_svc_fini(struct pool_svc *svc)
{
	ds_cont_svc_fini(&svc->ps_cont_svc);
	rdb_stop(svc->ps_db);
	rdb_path_fini(&svc->ps_handles);
	rdb_path_fini(&svc->ps_root);
	ABT_mutex_free(&svc->ps_mutex);
	ABT_rwlock_free(&svc->ps_lock);
}

static inline struct pool_svc *
pool_svc_obj(daos_list_t *rlink)
{
	return container_of(rlink, struct pool_svc, ps_entry);
}

static bool
pool_svc_key_cmp(struct dhash_table *htable, daos_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct pool_svc *svc = pool_svc_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(svc->ps_uuid, key) == 0;
}

static void
pool_svc_rec_addref(struct dhash_table *htable, daos_list_t *rlink)
{
	pool_svc_obj(rlink)->ps_ref++;
}

static bool
pool_svc_rec_decref(struct dhash_table *htable, daos_list_t *rlink)
{
	struct pool_svc *svc = pool_svc_obj(rlink);

	D_ASSERTF(svc->ps_ref > 0, "%d\n", svc->ps_ref);
	svc->ps_ref--;
	return svc->ps_ref == 0;
}

static void
pool_svc_rec_free(struct dhash_table *htable, daos_list_t *rlink)
{
	struct pool_svc *svc = pool_svc_obj(rlink);

	D_DEBUG(DF_DSMS, DF_UUID": freeing\n", DP_UUID(svc->ps_uuid));
	D_ASSERT(dhash_rec_unlinked(&svc->ps_entry));
	D_ASSERTF(svc->ps_ref == 0, "%d\n", svc->ps_ref);
	pool_svc_fini(svc);
	D_FREE_PTR(svc);
}

static dhash_table_ops_t pool_svc_hash_ops = {
	.hop_key_cmp	= pool_svc_key_cmp,
	.hop_rec_addref	= pool_svc_rec_addref,
	.hop_rec_decref	= pool_svc_rec_decref,
	.hop_rec_free	= pool_svc_rec_free
};

static struct dhash_table	pool_svc_hash;
static ABT_mutex		pool_svc_hash_lock;

int
ds_pool_svc_hash_init(void)
{
	int rc;

	rc = ABT_mutex_create(&pool_svc_hash_lock);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = dhash_table_create_inplace(DHASH_FT_NOLOCK, 4 /* bits */,
					NULL /* priv */, &pool_svc_hash_ops,
					&pool_svc_hash);
	if (rc != 0)
		ABT_mutex_free(&pool_svc_hash_lock);
	return rc;
}

void
ds_pool_svc_hash_fini(void)
{
	dhash_table_destroy_inplace(&pool_svc_hash, true /* force */);
	ABT_mutex_free(&pool_svc_hash_lock);
}

static int
pool_svc_lookup(const uuid_t uuid, struct pool_svc **svcp)
{
	daos_list_t    *entry;
	bool		nonexist = false;

	ABT_mutex_lock(pool_svc_hash_lock);
	entry = dhash_rec_find(&pool_svc_hash, uuid, sizeof(uuid_t));
	if (entry == NULL) {
		char	       *path;
		struct stat	buf;
		int		rc;

		/*
		 * See if the DB exists. If an error prevents us from find that
		 * out, return -DER_NOTLEADER so that the client tries other
		 * replicas.
		 */
		path = ds_pool_rdb_path(uuid, uuid);
		if (path == NULL) {
			D_ERROR(DF_UUID": failed to get rdb path\n",
				DP_UUID(uuid));
			D_GOTO(out_lock, -DER_NOMEM);
		}
		rc = stat(path, &buf);
		free(path);
		if (rc != 0) {
			if (errno == ENOENT)
				nonexist = true;
			else
				D_ERROR(DF_UUID": failed to stat rdb: %d\n",
					DP_UUID(uuid), errno);
			D_GOTO(out_lock, daos_errno2der(errno));
		}
	}
out_lock:
	ABT_mutex_unlock(pool_svc_hash_lock);
	if (nonexist)
		return -DER_NONEXIST;
	if (entry == NULL)
		return -DER_NOTLEADER;
	*svcp = pool_svc_obj(entry);
	return 0;
}

/*
 * Is svc up (i.e., ready to accept RPCs)? If not, the caller may always report
 * -DER_NOTLEADER, even if svc->ps_db is in leader state, in which case the
 * client will retry the RPC.
 */
static inline bool
pool_svc_up(struct pool_svc *svc)
{
	return svc->ps_state == POOL_SVC_UP;
}

static void
pool_svc_put(struct pool_svc *svc)
{
	ABT_mutex_lock(pool_svc_hash_lock);
	dhash_rec_decref(&pool_svc_hash, &svc->ps_entry);
	ABT_mutex_unlock(pool_svc_hash_lock);
}

/**
 * Look up container service \a pool_uuid. We have to return the address of
 * ps_cont_svc via a pointer... :(
 */
int
ds_pool_lookup_cont_svc(const uuid_t pool_uuid, struct cont_svc ***svcpp)
{
	struct pool_svc	       *pool_svc;
	int			rc;

	rc = pool_svc_lookup(pool_uuid, &pool_svc);
	if (rc != 0)
		return rc;
	*svcpp = &pool_svc->ps_cont_svc;
	return 0;
}

/**
 * Is the container service up?
 */
bool
ds_pool_is_cont_svc_up(struct cont_svc **svcp)
{
	struct pool_svc *pool_svc;

	pool_svc = container_of(svcp, struct pool_svc, ps_cont_svc);
	return pool_svc_up(pool_svc);
}

/**
 * Put container service *\a svcp.
 */
void
ds_pool_put_cont_svc(struct cont_svc **svcp)
{
	struct pool_svc *pool_svc;

	pool_svc = container_of(svcp, struct pool_svc, ps_cont_svc);
	pool_svc_put(pool_svc);
}

int
ds_pool_svc_start(const uuid_t uuid)
{
	daos_list_t	       *entry;
	struct pool_svc	       *svc;
	int			rc;

	ABT_mutex_lock(pool_svc_hash_lock);

	entry = dhash_rec_find(&pool_svc_hash, uuid, sizeof(uuid_t));
	if (entry != NULL) {
		svc = pool_svc_obj(entry);
		D_GOTO(out_ref, rc = 0);
	}

	D_ALLOC_PTR(svc);
	if (svc == NULL)
		D_GOTO(err_lock, rc = -DER_NOMEM);

	rc = pool_svc_init(svc, uuid);
	if (rc != 0)
		D_GOTO(err_svc, rc);

	rc = dhash_rec_insert(&pool_svc_hash, uuid, sizeof(uuid_t),
			      &svc->ps_entry, true /* exclusive */);
	if (rc != 0)
		D_GOTO(err_svc_init, rc);

out_ref:
	dhash_rec_decref(&pool_svc_hash, &svc->ps_entry);
	ABT_mutex_unlock(pool_svc_hash_lock);
	D_DEBUG(DF_DSMS, DF_UUID": started pool service\n", DP_UUID(uuid));
	return 0;

err_svc_init:
	pool_svc_fini(svc);
err_svc:
	D_FREE_PTR(svc);
err_lock:
	ABT_mutex_unlock(pool_svc_hash_lock);
	D_ERROR(DF_UUID": failed to start pool service\n", DP_UUID(uuid));
	return rc;
}

void
ds_pool_svc_stop(const uuid_t uuid)
{
	daos_list_t	       *entry;
	struct pool_svc	       *svc;

	ABT_mutex_lock(pool_svc_hash_lock);
	entry = dhash_rec_find(&pool_svc_hash, uuid, sizeof(uuid_t));
	ABT_mutex_unlock(pool_svc_hash_lock);
	if (entry == NULL)
		return;
	svc = pool_svc_obj(entry);

	/*
	 * TODO: If the svc object still has references, and the service is
	 * immediately restarted, then there will be two svc objects for the
	 * same service. Mark it as stopping and delete when stopped?
	 */
	D_DEBUG(DF_DSMS, DF_UUID": stopping pool service\n", DP_UUID(uuid));
	ABT_mutex_lock(svc->ps_mutex);
	svc->ps_stop = true;
	if (svc->ps_state == POOL_SVC_UP)
		pool_svc_step_down(svc);
	svc->ps_state = POOL_SVC_DOWN;
	ABT_mutex_unlock(svc->ps_mutex);

	ABT_mutex_lock(pool_svc_hash_lock);
	dhash_rec_delete(&pool_svc_hash, uuid, sizeof(uuid_t));
	ABT_mutex_unlock(pool_svc_hash_lock);
	pool_svc_put(svc);
}

static int
bcast_create(crt_context_t ctx, struct pool_svc *svc, crt_opcode_t opcode,
	     crt_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->ps_pool, DAOS_POOL_MODULE, opcode,
				    rpc, NULL, NULL);
}

static void
set_rsvc_hint(struct rdb *db, struct pool_op_out *out)
{
	int rc;

	rc = rdb_get_leader(db, &out->po_hint.sh_term, &out->po_hint.sh_rank);
	if (rc != 0)
		return;
	out->po_hint.sh_flags |= RSVC_HINT_VALID;
}

struct pool_attr {
	uint32_t	pa_uid;
	uint32_t	pa_gid;
	uint32_t	pa_mode;
};

static int
pool_attr_read(struct rdb_tx *tx, const struct pool_svc *svc,
	       struct pool_attr *attr)
{
	daos_iov_t	value;
	int		rc;

	daos_iov_set(&value, &attr->pa_uid, sizeof(attr->pa_uid));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_attr_uid, &value);
	if (rc != 0)
		return rc;

	daos_iov_set(&value, &attr->pa_gid, sizeof(attr->pa_gid));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_attr_gid, &value);
	if (rc != 0)
		return rc;

	daos_iov_set(&value, &attr->pa_mode, sizeof(attr->pa_mode));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_attr_mode, &value);
	if (rc != 0)
		return rc;

	D_DEBUG(DF_DSMS, "uid=%u gid=%u mode=%u\n", attr->pa_uid, attr->pa_gid,
		attr->pa_mode);
	return 0;
}

/*
 * We use this RPC to not only create the pool metadata but also initialize the
 * pool/container service DB.
 */
void
ds_pool_create_handler(crt_rpc_t *rpc)
{
	struct pool_create_in  *in = crt_req_get(rpc);
	struct pool_create_out *out = crt_reply_get(rpc);
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	daos_iov_t		value;
	struct rdb_kvs_attr	attr;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pri_op.pi_uuid), rpc);

	if (in->pri_ntgts != in->pri_tgt_uuids.da_count ||
	    in->pri_ntgts != in->pri_tgt_ranks->rl_nr.num)
		D_GOTO(out, rc = -DER_PROTO);
	if (in->pri_ndomains != in->pri_domains.da_count)
		D_GOTO(out, rc = -DER_PROTO);

	/* This RPC doesn't care about pool_svc_up(). */
	rc = pool_svc_lookup(in->pri_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * Simply serialize this whole RPC with pool_svc_step_{up,down}_cb()
	 * and ds_pool_svc_stop().
	 */
	ABT_mutex_lock(svc->ps_mutex);

	if (svc->ps_stop) {
		D_DEBUG(DB_MD, DF_UUID": pool service already stopping\n",
			DP_UUID(svc->ps_uuid));
		D_GOTO(out, rc = -DER_CANCELED);
	}

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out_mutex, rc);
	ABT_rwlock_wrlock(svc->ps_lock);
	ds_cont_wrlock_metadata(svc->ps_cont_svc);

	/* See if the DB has already been initialized. */
	daos_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_attr_map_buffer,
			   &value);
	if (rc != -DER_NONEXIST) {
		if (rc == 0)
			D_DEBUG(DF_DSMS, DF_UUID": db already initialized\n",
				DP_UUID(svc->ps_uuid));
		else
			D_ERROR(DF_UUID": failed to look up pool map: %d\n",
				DP_UUID(svc->ps_uuid), rc);
		D_GOTO(out_tx, rc);
	}

	/* Initialize the DB and the metadata for this pool. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 8;
	rc = rdb_tx_create_root(&tx, &attr);
	if (rc != 0)
		D_GOTO(out_tx, rc);
	rc = init_pool_metadata(&tx, &svc->ps_root, in->pri_uid, in->pri_gid,
				in->pri_mode, in->pri_ntgts,
				in->pri_tgt_uuids.da_arrays, NULL /* group */,
				in->pri_tgt_ranks, in->pri_ndomains,
				in->pri_domains.da_arrays);
	if (rc != 0)
		D_GOTO(out_tx, rc);
	rc = ds_cont_init_metadata(&tx, &svc->ps_root, in->pri_op.pi_uuid);
	if (rc != 0)
		D_GOTO(out_tx, rc);

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_GOTO(out_tx, rc);

out_tx:
	ds_cont_unlock_metadata(svc->ps_cont_svc);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	if (svc->ps_state == POOL_SVC_UP_EMPTY) {
		/*
		 * The DB is no longer empty. Since the previous
		 * pool_svc_step_up_cb() call didn't finish stepping up due to
		 * an empty DB, and there hasn't been a pool_svc_step_down_cb()
		 * call yet, we should call pool_svc_step_up() to finish
		 * stepping up.
		 */
		D_DEBUG(DF_DSMS, DF_UUID": trying to finish stepping up\n",
			DP_UUID(in->pri_op.pi_uuid));
		rc = pool_svc_step_up(svc);
		if (rc != 0) {
			D_ASSERT(rc != -DER_NONEXIST);
			/* TODO: Ask rdb to step down. */
			D_GOTO(out_svc, rc);
		}
		svc->ps_state = POOL_SVC_UP;
	}

out_mutex:
	ABT_mutex_unlock(svc->ps_mutex);
out_svc:
	set_rsvc_hint(svc->ps_db, &out->pro_op);
	pool_svc_put(svc);
out:
	out->pro_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pri_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

static int
permitted(const struct pool_attr *attr, uint32_t uid, uint32_t gid,
	  uint64_t capas)
{
	int		shift;
	uint32_t	capas_permitted;

	/*
	 * Determine which set of capability bits applies. See also the
	 * comment/diagram for ds_pool_attr_mode in src/pool/srv_layout.h.
	 */
	if (uid == attr->pa_uid)
		shift = DAOS_PC_NBITS * 2;	/* user */
	else if (gid == attr->pa_gid)
		shift = DAOS_PC_NBITS;		/* group */
	else
		shift = 0;			/* other */

	/* Extract the applicable set of capability bits. */
	capas_permitted = (attr->pa_mode >> shift) & DAOS_PC_MASK;

	/* Only if all requested capability bits are permitted... */
	return (capas & capas_permitted) == capas;
}

static int
pool_connect_bcast(crt_context_t ctx, struct pool_svc *svc,
		   const uuid_t pool_hdl, uint64_t capas)
{
	struct pool_tgt_connect_in     *in;
	struct pool_tgt_connect_out    *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	rc = bcast_create(ctx, svc, POOL_TGT_CONNECT, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tci_uuid, svc->ps_uuid);
	uuid_copy(in->tci_hdl, pool_hdl);
	in->tci_capas = capas;
	in->tci_map_version = pool_map_get_version(svc->ps_pool->sp_map);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tco_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to connect to %d targets\n",
			DP_UUID(svc->ps_uuid), rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_UUID": bcasted: %d\n", DP_UUID(svc->ps_uuid), rc);
	return rc;
}

static int
bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->bci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->bci_rc,
			 sizeof(cb_info->bci_rc));
	return 0;
}

/*
 * Transfer the pool map to "remote_bulk". If the remote bulk buffer is too
 * small, then return -DER_TRUNC and set "required_buf_size" to the local pool
 * map buffer size.
 */
static int
transfer_map_buf(struct rdb_tx *tx, struct pool_svc *svc, crt_rpc_t *rpc,
		 crt_bulk_t remote_bulk, uint32_t *required_buf_size)
{
	struct pool_buf	       *map_buf;
	size_t			map_buf_size;
	uint32_t		map_version;
	daos_size_t		remote_bulk_size;
	daos_iov_t		map_iov;
	daos_sg_list_t		map_sgl;
	crt_bulk_t		bulk;
	struct crt_bulk_desc	map_desc;
	crt_bulk_opid_t		map_opid;
	ABT_eventual		eventual;
	int		       *status;
	int			rc;

	rc = read_map_buf(tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		D_GOTO(out, rc);
	}

	if (map_version != pool_map_get_version(svc->ps_pool->sp_map)) {
		D_ERROR(DF_UUID": found different cached and persistent pool "
			"map versions: cached=%u persistent=%u\n",
			DP_UUID(svc->ps_uuid),
			pool_map_get_version(svc->ps_pool->sp_map),
			map_version);
		D_GOTO(out, rc = -DER_IO);
	}

	map_buf_size = pool_buf_size(map_buf->pb_nr);

	/* Check if the client bulk buffer is large enough. */
	rc = crt_bulk_get_len(remote_bulk, &remote_bulk_size);
	if (rc != 0)
		D_GOTO(out, rc);
	if (remote_bulk_size < map_buf_size) {
		D_ERROR(DF_UUID": remote pool map buffer ("DF_U64") < required "
			"(%lu)\n", DP_UUID(svc->ps_uuid), remote_bulk_size,
			map_buf_size);
		*required_buf_size = map_buf_size;
		D_GOTO(out, rc = -DER_TRUNC);
	}

	daos_iov_set(&map_iov, map_buf, map_buf_size);
	map_sgl.sg_nr.num = 1;
	map_sgl.sg_nr.num_out = 0;
	map_sgl.sg_iovs = &map_iov;

	rc = crt_bulk_create(rpc->cr_ctx, daos2crt_sg(&map_sgl),
			     CRT_BULK_RO, &bulk);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Prepare "map_desc" for crt_bulk_transfer(). */
	map_desc.bd_rpc = rpc;
	map_desc.bd_bulk_op = CRT_BULK_PUT;
	map_desc.bd_remote_hdl = remote_bulk;
	map_desc.bd_remote_off = 0;
	map_desc.bd_local_hdl = bulk;
	map_desc.bd_local_off = 0;
	map_desc.bd_len = map_iov.iov_len;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_bulk, rc = dss_abterr2der(rc));

	rc = crt_bulk_transfer(&map_desc, bulk_cb, &eventual, &map_opid);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

out_eventual:
	ABT_eventual_free(&eventual);
out_bulk:
	crt_bulk_free(bulk);
out:
	return rc;
}

void
ds_pool_connect_handler(crt_rpc_t *rpc)
{
	struct pool_connect_in	       *in = crt_req_get(rpc);
	struct pool_connect_out	       *out = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	struct rdb_tx			tx;
	daos_iov_t			key;
	daos_iov_t			value;
	struct pool_attr		attr;
	struct pool_hdl			hdl;
	uint32_t			nhandles;
	int				skip_update = 0;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pci_op.pi_uuid), rpc, DP_UUID(in->pci_op.pi_hdl));

	rc = pool_svc_lookup(in->pci_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);
	if (!pool_svc_up(svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	/* Check existing pool handles. */
	daos_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	daos_iov_set(&value, &hdl, sizeof(hdl));
	rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
	if (rc == 0) {
		if (hdl.ph_capas == in->pci_capas) {
			/*
			 * The handle already exists; only do the pool map
			 * transfer.
			 */
			skip_update = 1;
		} else {
			/* The existing one does not match the new one. */
			D_ERROR(DF_UUID": found conflicting pool handle\n",
				DP_UUID(in->pci_op.pi_uuid));
			D_GOTO(out_lock, rc = -DER_EXIST);
		}
	} else if (rc != -DER_NONEXIST) {
		D_GOTO(out_lock, rc);
	}

	rc = pool_attr_read(&tx, svc, &attr);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	if (!permitted(&attr, in->pci_uid, in->pci_gid, in->pci_capas)) {
		D_ERROR(DF_UUID": refusing connect attempt for uid %u gid %u "
			DF_X64"\n", DP_UUID(in->pci_op.pi_uuid), in->pci_uid,
			in->pci_gid, in->pci_capas);
		D_GOTO(out_map_version, rc = -DER_NO_PERM);
	}

	out->pco_mode = attr.pa_mode;

	/*
	 * Transfer the pool map to the client before adding the pool handle,
	 * so that we don't need to worry about rolling back the transaction
	 * when the tranfer fails. The client has already been authenticated
	 * and authorized at this point. If an error occurs after the transfer
	 * completes, then we simply return the error and the client will throw
	 * its pool_buf away.
	 */
	rc = transfer_map_buf(&tx, svc, rpc, in->pci_map_bulk,
			      &out->pco_map_buf_size);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	if (skip_update)
		D_GOTO(out_map_version, rc = 0);

	daos_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_attr_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	/* Take care of exclusive handles. */
	if (nhandles != 0) {
		if (in->pci_capas & DAOS_PC_EX) {
			D_DEBUG(DF_DSMS, DF_UUID": others already connected\n",
				DP_UUID(in->pci_op.pi_uuid));
			D_GOTO(out_map_version, rc = -DER_BUSY);
		} else {
			/*
			 * If there is a non-exclusive handle, then all handles
			 * are non-exclusive.
			 */
			daos_iov_set(&value, &hdl, sizeof(hdl));
			rc = rdb_tx_fetch(&tx, &svc->ps_handles,
					  RDB_PROBE_FIRST, NULL /* key_in */,
					  NULL /* key_out */, &value);
			if (rc != 0)
				D_GOTO(out_map_version, rc);
			if (hdl.ph_capas & DAOS_PC_EX)
				D_GOTO(out_map_version, rc = -DER_BUSY);
		}
	}

	rc = pool_connect_bcast(rpc->cr_ctx, svc, in->pci_op.pi_hdl,
				in->pci_capas);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to connect to targets: %d\n",
			DP_UUID(in->pci_op.pi_uuid), rc);
		D_GOTO(out_map_version, rc);
	}

	hdl.ph_capas = in->pci_capas;
	nhandles++;

	daos_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(&tx, &svc->ps_root, &ds_pool_attr_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	daos_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	daos_iov_set(&value, &hdl, sizeof(hdl));
	rc = rdb_tx_update(&tx, &svc->ps_handles, &key, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	rc = rdb_tx_commit(&tx);
out_map_version:
	out->pco_op.po_map_version = pool_map_get_version(svc->ps_pool->sp_map);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	set_rsvc_hint(svc->ps_db, &out->pco_op);
	pool_svc_put(svc);
out:
	out->pco_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pci_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

static int
pool_disconnect_bcast(crt_context_t ctx, struct pool_svc *svc,
		      uuid_t *pool_hdls, int n_pool_hdls)
{
	struct pool_tgt_disconnect_in  *in;
	struct pool_tgt_disconnect_out *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	rc = bcast_create(ctx, svc, POOL_TGT_DISCONNECT, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tdi_uuid, svc->ps_uuid);
	in->tdi_hdls.da_arrays = pool_hdls;
	in->tdi_hdls.da_count = n_pool_hdls;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to disconnect from %d targets\n",
			DP_UUID(svc->ps_uuid), rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_UUID": bcasted: %d\n", DP_UUID(svc->ps_uuid), rc);
	return rc;
}

static int
pool_disconnect_hdls(struct rdb_tx *tx, struct pool_svc *svc, uuid_t *hdl_uuids,
		     int n_hdl_uuids, crt_context_t ctx)
{
	daos_iov_t	value;
	uint32_t	nhandles;
	int		i;
	int		rc;

	D_ASSERTF(n_hdl_uuids > 0, "%d\n", n_hdl_uuids);

	D_DEBUG(DF_DSMS, DF_UUID": disconnecting %d hdls: hdl_uuids[0]="DF_UUID
		"\n", DP_UUID(svc->ps_uuid), n_hdl_uuids,
		DP_UUID(hdl_uuids[0]));

	/*
	 * TODO: Send POOL_TGT_CLOSE_CONTS and somehow retry until every
	 * container service has responded (through ds_pool).
	 */
	rc = ds_cont_close_by_pool_hdls(svc->ps_uuid, hdl_uuids, n_hdl_uuids,
					ctx);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pool_disconnect_bcast(ctx, svc, hdl_uuids, n_hdl_uuids);
	if (rc != 0)
		D_GOTO(out, rc);

	daos_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_attr_nhandles, &value);
	if (rc != 0)
		D_GOTO(out, rc);

	nhandles -= n_hdl_uuids;

	for (i = 0; i < n_hdl_uuids; i++) {
		daos_iov_t key;

		daos_iov_set(&key, hdl_uuids[i], sizeof(uuid_t));
		rc = rdb_tx_delete(tx, &svc->ps_handles, &key);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	daos_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_attr_nhandles, &value);
	if (rc != 0)
		D_GOTO(out, rc);

out:
	D_DEBUG(DF_DSMS, DF_UUID": leaving: %d\n", DP_UUID(svc->ps_uuid), rc);
	return rc;
}

void
ds_pool_disconnect_handler(crt_rpc_t *rpc)
{
	struct pool_disconnect_in      *pdi = crt_req_get(rpc);
	struct pool_disconnect_out     *pdo = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	struct rdb_tx			tx;
	daos_iov_t			key;
	daos_iov_t			value;
	struct pool_hdl			hdl;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(pdi->pdi_op.pi_uuid), rpc, DP_UUID(pdi->pdi_op.pi_hdl));

	rc = pool_svc_lookup(pdi->pdi_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);
	if (!pool_svc_up(svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	daos_iov_set(&key, pdi->pdi_op.pi_hdl, sizeof(uuid_t));
	daos_iov_set(&value, &hdl, sizeof(hdl));
	rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_lock, rc);
	}

	rc = pool_disconnect_hdls(&tx, svc, &pdi->pdi_op.pi_hdl,
				  1 /* n_hdl_uuids */, rpc->cr_ctx);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	rc = rdb_tx_commit(&tx);
	/* No need to set pdo->pdo_op.po_map_version. */
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	set_rsvc_hint(svc->ps_db, &pdo->pdo_op);
	pool_svc_put(svc);
out:
	pdo->pdo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(pdi->pdi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

void
ds_pool_query_handler(crt_rpc_t *rpc)
{
	struct pool_query_in   *in = crt_req_get(rpc);
	struct pool_query_out  *out = crt_reply_get(rpc);
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	daos_iov_t		key;
	daos_iov_t		value;
	struct pool_hdl		hdl;
	struct pool_attr	attr;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pqi_op.pi_uuid), rpc, DP_UUID(in->pqi_op.pi_hdl));

	rc = pool_svc_lookup(in->pqi_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);
	if (!pool_svc_up(svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = ds_rebuild_query(in->pqi_op.pi_uuid, false, NULL,
			      &out->pqo_rebuild_st);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->ps_lock);

	/* Verify the pool handle. Note: since rebuild will not
	 * connect the pool, so we only verify the non-rebuild
	 * pool.
	 */
	if (!is_rebuild_pool(in->pqi_op.pi_hdl)) {
		daos_iov_set(&key, in->pqi_op.pi_hdl, sizeof(uuid_t));
		daos_iov_set(&value, &hdl, sizeof(hdl));
		rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
		if (rc != 0) {
			if (rc == -DER_NONEXIST)
				rc = -DER_NO_HDL;
			D_GOTO(out_lock, rc);
		}
	}

	rc = pool_attr_read(&tx, svc, &attr);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	out->pqo_mode = attr.pa_mode;

	rc = transfer_map_buf(&tx, svc, rpc, in->pqi_map_bulk,
			      &out->pqo_map_buf_size);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

out_map_version:
	out->pqo_op.po_map_version = pool_map_get_version(svc->ps_pool->sp_map);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	set_rsvc_hint(svc->ps_db, &out->pqo_op);
	pool_svc_put(svc);
out:
	out->pqo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pqi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

static int
pool_update_map_bcast(crt_context_t ctx, struct pool_svc *svc,
		      uint32_t map_version, struct pool_buf *map_buf,
		      daos_rank_list_t *tgts_exclude)
{
	struct pool_tgt_update_map_in  *in;
	struct pool_tgt_update_map_out *out;
	crt_rpc_t		       *rpc;
	crt_bulk_t			bulk = NULL;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	if (map_buf != NULL) {
		size_t map_buf_size = pool_buf_size(map_buf->pb_nr);
		daos_sg_list_t	map_sgl;
		daos_iov_t	map_iov;

		daos_iov_set(&map_iov, map_buf, map_buf_size);
		map_sgl.sg_nr.num = 1;
		map_sgl.sg_nr.num_out = 1;
		map_sgl.sg_iovs = &map_iov;

		rc = crt_bulk_create(ctx, daos2crt_sg(&map_sgl),
				     CRT_BULK_RO, &bulk);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	rc = ds_pool_bcast_create(ctx, svc->ps_pool, DAOS_POOL_MODULE,
				  POOL_TGT_UPDATE_MAP, &rpc, bulk,
				  tgts_exclude);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tui_uuid, svc->ps_uuid);
	in->tui_map_version = map_version;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tuo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to update pool map on %d targets\n",
			DP_UUID(svc->ps_uuid), rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_UUID": bcasted: %d\n", DP_UUID(svc->ps_uuid), rc);
	return rc;
}

/* Callers are responsible for daos_rank_list_free(*replicasp). */
static int
ds_pool_update_internal(uuid_t pool_uuid, daos_rank_list_t *tgts,
			unsigned int opc, daos_rank_list_t *tgts_out,
			struct pool_op_out *pto_op, bool *updated,
			daos_rank_list_t **replicasp)
{
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	struct pool_map	       *map;
	uint32_t		map_version_before;
	uint32_t		map_version;
	struct pool_buf	       *map_buf;
	struct pool_map	       *map_tmp;
	struct dss_module_info *info = dss_get_module_info();
	int			rc;

	rc = pool_svc_lookup(pool_uuid, &svc);
	if (rc != 0)
		return rc;
	if (!pool_svc_up(svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	ABT_rwlock_wrlock(svc->ps_lock);

	if (replicasp != NULL) {
		rc = rdb_get_ranks(svc->ps_db, replicasp);
		if (rc != 0)
			D_GOTO(out_map_version, rc);
	}

	/* Create a temporary pool map based on the last committed version. */
	rc = read_map(&tx, &svc->ps_root, &map);
	if (rc != 0)
		D_GOTO(out_replicas, rc);

	/*
	 * Attempt to modify the temporary pool map and save its versions
	 * before and after. If the version hasn't changed, we are done.
	 */
	map_version_before = pool_map_get_version(map);
	rc = ds_pool_map_tgts_update(map, tgts, tgts_out, opc);
	if (rc != 0)
		D_GOTO(out_map, rc);
	map_version = pool_map_get_version(map);

	D_DEBUG(DF_DSMS, DF_UUID": version=%u->%u failed=%d\n",
		DP_UUID(svc->ps_uuid), map_version_before, map_version,
		tgts_out == NULL ? -1 : tgts_out->rl_nr.num_out);
	if (map_version == map_version_before) {
		if (updated)
			*updated = false;
		D_GOTO(out_map, rc = 0);
	}

	/* Write the new pool map. */
	rc = pool_buf_extract(map, &map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);
	rc = write_map_buf(&tx, &svc->ps_root, map_buf, map_version);
	pool_buf_free(map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);

	rc = rdb_tx_commit(&tx);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": failed to commit: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		D_GOTO(out_map, rc);
	}

	/*
	 * The new pool map is now committed and can be publicized. Swap the
	 * new pool map with the old one in the cache.
	 */
	ABT_rwlock_wrlock(svc->ps_pool->sp_lock);
	map_tmp = svc->ps_pool->sp_map;
	svc->ps_pool->sp_map = map;
	map = map_tmp;
	svc->ps_pool->sp_map_version = map_version;
	ABT_rwlock_unlock(svc->ps_pool->sp_lock);

	if (updated)
		*updated = true;
	/*
	 * Ignore the return code as we are more about committing a pool map
	 * change than its dissemination.
	 */
	pool_update_map_bcast(info->dmi_ctx, svc, map_version, NULL, NULL);
	D_EXIT;
out_map:
	pool_map_decref(map);
out_replicas:
	if (rc)
		daos_rank_list_free(*replicasp);
out_map_version:
	if (pto_op != NULL)
		pto_op->po_map_version =
			pool_map_get_version(svc->ps_pool->sp_map);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	if (pto_op != NULL)
		set_rsvc_hint(svc->ps_db, pto_op);
	pool_svc_put(svc);
	return rc;
}

int
ds_pool_tgt_exclude_out(uuid_t pool_uuid, daos_rank_list_t *tgts,
			daos_rank_list_t *tgts_out)
{
	return ds_pool_update_internal(pool_uuid, tgts, POOL_EXCLUDE_OUT,
				       tgts_out, NULL, NULL, NULL);
}

void
ds_pool_update_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_update_in	*in = crt_req_get(rpc);
	struct pool_tgt_update_out	*out = crt_reply_get(rpc);
	daos_rank_list_t		*replicas = NULL;
	bool				updated;
	int				rc;

	if (in->pti_targets == NULL || in->pti_targets->rl_nr.num == 0 ||
	    in->pti_targets->rl_ranks == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: ntargets=%u\n",
		DP_UUID(in->pti_op.pi_uuid), rpc, in->pti_targets->rl_nr.num);

	/* These have to be freed after the reply is sent. */
	D_ALLOC_PTR(out->pto_targets);
	if (out->pto_targets == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC(out->pto_targets->rl_ranks,
		sizeof(*out->pto_targets->rl_ranks) *
		in->pti_targets->rl_nr.num);
	if (out->pto_targets->rl_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	out->pto_targets->rl_nr.num = in->pti_targets->rl_nr.num;

	rc = ds_pool_update_internal(in->pti_op.pi_uuid, in->pti_targets,
				     opc_get(rpc->cr_opc), out->pto_targets,
				     &out->pto_op, &updated, &replicas);
	if (rc)
		D_GOTO(out, rc);

	/* The RPC encoding code only looks at rl_nr.num. */
	out->pto_targets->rl_nr.num = out->pto_targets->rl_nr.num_out;
	D_EXIT;
out:
	out->pto_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pti_op.pi_uuid), rpc, rc);
	rc = crt_reply_send(rpc);

	if (out->pto_op.po_rc == 0 && updated &&
	    opc_get(rpc->cr_opc) == POOL_EXCLUDE) {
		char	*env;
		int	 ret;

		env = getenv(REBUILD_ENV);
		if (env && !strcasecmp(env, REBUILD_ENV_DISABLED)) {
			D_DEBUG(DB_TRACE, "Rebuild is disabled\n");

		} else { /* enabled by default */
			D_ASSERT(replicas != NULL);
			ret = ds_rebuild_schedule(in->pti_op.pi_uuid,
						  out->pto_op.po_map_version,
						  in->pti_targets, replicas);
			if (ret != 0) {
				D_ERROR("rebuild fails rc %d\n", ret);
				if (rc == 0)
					rc = ret;
			}
		}
	}

	if (replicas != NULL)
		daos_rank_list_free(replicas);
	if (out->pto_targets != NULL) {
		if (out->pto_targets->rl_ranks != NULL)
			D_FREE(out->pto_targets->rl_ranks,
			       sizeof(*out->pto_targets->rl_ranks) *
			       in->pti_targets->rl_nr.num);
		D_FREE_PTR(out->pto_targets);
	}
}

struct evict_iter_arg {
	uuid_t *eia_hdl_uuids;
	size_t	eia_hdl_uuids_size;
	int	eia_n_hdl_uuids;
};

static int
evict_iter_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val, void *varg)
{
	struct evict_iter_arg  *arg = varg;

	D_ASSERT(arg->eia_hdl_uuids != NULL);
	D_ASSERT(arg->eia_hdl_uuids_size > sizeof(uuid_t));

	if (key->iov_len != sizeof(uuid_t) ||
	    val->iov_len != sizeof(struct pool_hdl)) {
		D_ERROR("invalid key/value size: key="DF_U64" value="DF_U64"\n",
			key->iov_len, val->iov_len);
		return -DER_IO;
	}

	/*
	 * Make sure arg->eia_hdl_uuids[arg->eia_hdl_uuids_size] have enough
	 * space for this handle.
	 */
	if (sizeof(uuid_t) * (arg->eia_n_hdl_uuids + 1) >
	    arg->eia_hdl_uuids_size) {
		uuid_t *hdl_uuids_tmp;
		size_t	hdl_uuids_size_tmp;

		hdl_uuids_size_tmp = arg->eia_hdl_uuids_size * 2;
		D_ALLOC(hdl_uuids_tmp, hdl_uuids_size_tmp);
		if (hdl_uuids_tmp == NULL)
			return -DER_NOMEM;
		memcpy(hdl_uuids_tmp, arg->eia_hdl_uuids,
		       arg->eia_hdl_uuids_size);
		D_FREE(arg->eia_hdl_uuids, arg->eia_hdl_uuids_size);
		arg->eia_hdl_uuids = hdl_uuids_tmp;
		arg->eia_hdl_uuids_size = hdl_uuids_size_tmp;
	}

	uuid_copy(arg->eia_hdl_uuids[arg->eia_n_hdl_uuids], key->iov_buf);
	arg->eia_n_hdl_uuids++;
	return 0;
}

/*
 * Callers are responsible for freeing *hdl_uuids if this function returns zero.
 */
static int
find_hdls_to_evict(struct rdb_tx *tx, struct pool_svc *svc, uuid_t **hdl_uuids,
		   size_t *hdl_uuids_size, int *n_hdl_uuids)
{
	struct evict_iter_arg	arg;
	int			rc;

	arg.eia_hdl_uuids_size = sizeof(uuid_t) * 4;
	D_ALLOC(arg.eia_hdl_uuids, arg.eia_hdl_uuids_size);
	if (arg.eia_hdl_uuids == NULL)
		return -DER_NOMEM;
	arg.eia_n_hdl_uuids = 0;

	rc = rdb_tx_iterate(tx, &svc->ps_handles, false /* backward */,
			    evict_iter_cb, &arg);
	if (rc != 0) {
		D_FREE(arg.eia_hdl_uuids, arg.eia_hdl_uuids_size);
		return rc;
	}

	*hdl_uuids = arg.eia_hdl_uuids;
	*hdl_uuids_size = arg.eia_hdl_uuids_size;
	*n_hdl_uuids = arg.eia_n_hdl_uuids;
	return 0;
}

void
ds_pool_evict_handler(crt_rpc_t *rpc)
{
	struct pool_evict_in   *in = crt_req_get(rpc);
	struct pool_evict_out  *out = crt_reply_get(rpc);
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	uuid_t		       *hdl_uuids;
	size_t			hdl_uuids_size;
	int			n_hdl_uuids;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pvi_op.pi_uuid), rpc);

	rc = pool_svc_lookup(in->pvi_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);
	if (!pool_svc_up(svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = find_hdls_to_evict(&tx, svc, &hdl_uuids, &hdl_uuids_size,
				&n_hdl_uuids);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	if (n_hdl_uuids > 0)
		rc = pool_disconnect_hdls(&tx, svc, hdl_uuids, n_hdl_uuids,
					  rpc->cr_ctx);

	rc = rdb_tx_commit(&tx);
	/* No need to set out->pvo_op.po_map_version. */
	D_FREE(hdl_uuids, hdl_uuids_size);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	set_rsvc_hint(svc->ps_db, &out->pvo_op);
	pool_svc_put(svc);
out:
	out->pvo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pvi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

void
ds_pool_svc_stop_handler(crt_rpc_t *rpc)
{
	struct pool_svc_stop_in	       *in = crt_req_get(rpc);
	struct pool_svc_stop_out       *out = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->psi_op.pi_uuid), rpc);

	rc = pool_svc_lookup(in->psi_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);
	if (!pool_svc_up(svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	ds_pool_svc_stop(svc->ps_uuid);

out_svc:
	set_rsvc_hint(svc->ps_db, &out->pso_op);
	pool_svc_put(svc);
out:
	out->pso_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->psi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

/**
 * broadcast the pool map of the pool to all servers.
 **/
int
ds_pool_pmap_broadcast(const uuid_t uuid, daos_rank_list_t *tgts_exclude)
{
	struct pool_svc		*svc;
	struct rdb_tx		tx;
	struct pool_buf		*map_buf;
	uint32_t		map_version;
	int			rc;

	rc = pool_svc_lookup(uuid, &svc);
	if (rc != 0)
		return rc;
	if (!pool_svc_up(svc))
		D_GOTO(out, rc = -DER_NOTLEADER);

	rc = rdb_tx_begin(svc->ps_db, &tx);
	if (rc != 0)
		D_GOTO(out, rc);
	ABT_rwlock_rdlock(svc->ps_lock);

	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		D_GOTO(out_lock, rc);
	}

	rc = pool_update_map_bcast(dss_get_module_info()->dmi_ctx, svc,
				   map_version, map_buf, tgts_exclude);

	D_DEBUG(DB_TRACE, "boradcast pool "DF_UUID" %u/%u/%u/%u  %d\n",
		DP_UUID(uuid), map_buf->pb_nr, map_buf->pb_domain_nr,
		map_buf->pb_target_nr, map_buf->pb_csum, rc);

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	pool_svc_put(svc);
	return rc;
}
