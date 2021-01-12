/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * \file
 *
 * ds_pool: Pool Service
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related pool metadata.
 */

#define D_LOGFAC DD_FAC(pool)

#include <daos_srv/pool.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <daos_api.h> /* for daos_prop_alloc/_free() */
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos/rsvc.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/rdb.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/security.h>
#include <cart/api.h>
#include <cart/iv.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

/* Pool service */
struct pool_svc {
	struct ds_rsvc		ps_rsvc;
	uuid_t			ps_uuid;	/* pool UUID */
	struct cont_svc	       *ps_cont_svc;	/* one combined svc for now */
	ABT_rwlock		ps_lock;	/* for DB data */
	rdb_path_t		ps_root;	/* root KVS */
	rdb_path_t		ps_handles;	/* pool handle KVS */
	rdb_path_t		ps_user;	/* pool user attributes KVS */
	struct ds_pool	       *ps_pool;
};

static bool pool_disable_evict = false;
static int pool_prop_read(struct rdb_tx *tx, const struct pool_svc *svc,
			  uint64_t bits, daos_prop_t **prop_out);
static int pool_space_query_bcast(crt_context_t ctx, struct pool_svc *svc,
				  uuid_t pool_hdl, struct daos_pool_space *ps);

static struct pool_svc *
pool_svc_obj(struct ds_rsvc *rsvc)
{
	return container_of(rsvc, struct pool_svc, ps_rsvc);
}

static int
write_map_buf(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_buf *buf,
	      uint32_t version)
{
	d_iov_t	value;
	int		rc;

	D_DEBUG(DF_DSMS, "version=%u ntargets=%u ndomains=%u\n", version,
		buf->pb_target_nr, buf->pb_domain_nr);

	/* Write the version. */
	d_iov_set(&value, &version, sizeof(version));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_map_version, &value);
	if (rc != 0)
		return rc;

	/* Write the buffer. */
	d_iov_set(&value, buf, pool_buf_size(buf->pb_nr));
	return rdb_tx_update(tx, kvs, &ds_pool_prop_map_buffer, &value);
}

/*
 * Retrieve the pool map buffer address in persistent memory and the pool map
 * version into "map_buf" and "map_version", respectively.
 */
static int
locate_map_buf(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_buf **buf,
	       uint32_t *version)
{
	uint32_t	ver;
	d_iov_t	value;
	int		rc;

	/* Read the version. */
	d_iov_set(&value, &ver, sizeof(ver));
	rc = rdb_tx_lookup(tx, kvs, &ds_pool_prop_map_version, &value);
	if (rc != 0)
		return rc;

	/* Look up the buffer address. */
	d_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(tx, kvs, &ds_pool_prop_map_buffer, &value);
	if (rc != 0)
		return rc;

	*buf = value.iov_buf;
	*version = ver;
	D_DEBUG(DF_DSMS, "version=%u ntargets=%u ndomains=%u\n", *version,
		(*buf)->pb_target_nr, (*buf)->pb_domain_nr);
	return 0;
}

/* Callers are responsible for freeing buf with D_FREE. */
static int
read_map_buf(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_buf **buf,
	     uint32_t *version)
{
	struct pool_buf	       *b;
	size_t			size;
	int			rc;

	rc = locate_map_buf(tx, kvs, &b, version);
	if (rc != 0)
		return rc;
	size = pool_buf_size(b->pb_nr);
	D_ALLOC(*buf, size);
	if (*buf == NULL)
		return -DER_NOMEM;
	memcpy(*buf, b, size);
	return 0;
}

/* Callers are responsible for destroying the object via pool_map_decref(). */
static int
read_map(struct rdb_tx *tx, const rdb_path_t *kvs, struct pool_map **map)
{
	struct pool_buf	       *buf;
	uint32_t		version;
	int			rc;

	rc = locate_map_buf(tx, kvs, &buf, &version);
	if (rc != 0)
		return rc;

	return pool_map_create(buf, version, map);
}

/* Store uuid in file path. */
static int
uuid_store(const char *path, const uuid_t uuid)
{
	int	fd;
	int	rc;

	/* Create and open the UUID file. */
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		D_ERROR(DF_UUID": failed to create uuid file %s: %d\n",
			DP_UUID(uuid), path, errno);
		rc = daos_errno2der(errno);
		goto out;
	}

	/* Write the UUID. */
	rc = write(fd, uuid, sizeof(uuid_t));
	if (rc != sizeof(uuid_t)) {
		if (rc != -1)
			errno = EIO;
		D_ERROR(DF_UUID": failed to write uuid into %s: %d %d\n",
			DP_UUID(uuid), path, rc, errno);
		rc = daos_errno2der(errno);
		goto out_fd;
	}

	/* Persist the UUID. */
	rc = fsync(fd);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to fsync %s: %d\n", DP_UUID(uuid),
			path, errno);
		rc = daos_errno2der(errno);
	}

	/* Free the resource and remove the file on errors. */
out_fd:
	close(fd);
	if (rc != 0)
		remove(path);
out:
	return rc;
}

/* Load uuid from file path. */
static int
uuid_load(const char *path, uuid_t uuid)
{
	int	fd;
	int	rc;

	/* Open the UUID file. */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			D_DEBUG(DB_MD, "failed to open uuid file %s: %d\n",
				path, errno);
		else
			D_ERROR("failed to open uuid file %s: %d\n", path,
				errno);
		rc = daos_errno2der(errno);
		goto out;
	}

	/* Read the UUID. */
	rc = read(fd, uuid, sizeof(uuid_t));
	if (rc == sizeof(uuid_t)) {
		rc = 0;
	} else {
		if (rc != -1)
			errno = EIO;
		D_ERROR("failed to read %s: %d %d\n", path, rc, errno);
		rc = daos_errno2der(errno);
	}

	close(fd);
out:
	return rc;
}

static char *
pool_svc_rdb_path_common(const uuid_t pool_uuid, const char *suffix)
{
	char   *name;
	char   *path;
	int	rc;

	D_ASPRINTF(name, RDB_FILE"pool%s", suffix);
	if (name == NULL)
		return NULL;
	rc = ds_mgmt_tgt_file(pool_uuid, name, NULL /* idx */, &path);
	D_FREE(name);
	if (rc != 0)
		return NULL;
	return path;
}

/* Return a pool service RDB path. */
static char *
pool_svc_rdb_path(const uuid_t pool_uuid)
{
	return pool_svc_rdb_path_common(pool_uuid, "");
}

/* Return a pool service RDB UUID file path. This file stores the RDB UUID. */
static char *
pool_svc_rdb_uuid_path(const uuid_t pool_uuid)
{
	return pool_svc_rdb_path_common(pool_uuid, "-uuid");
}

/*
 * Called by mgmt module on every storage node belonging to this pool.
 * "path" is the directory under which the VOS and metadata files shall be.
 * "target_uuid" returns the UUID generated for the target on this storage node.
 */
int
ds_pool_create(const uuid_t pool_uuid, const char *path, uuid_t target_uuid)
{
	char   *fpath;
	int	rc;

	uuid_generate(target_uuid);

	/* Store target_uuid in DSM_META_FILE. */
	D_ASPRINTF(fpath, "%s/%s", path, DSM_META_FILE);
	if (fpath == NULL)
		return -DER_NOMEM;
	rc = uuid_store(fpath, target_uuid);
	D_FREE(fpath);

	return rc;
}

/* copy \a prop to \a prop_def (duplicated default prop) */
static int
pool_prop_default_copy(daos_prop_t *prop_def, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	struct daos_prop_entry	*entry_def;
	int			 i;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		entry_def = daos_prop_entry_get(prop_def, entry->dpe_type);
		D_ASSERTF(entry_def != NULL, "type %d not found in "
			  "default prop.\n", entry->dpe_type);
		switch (entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
			D_FREE(entry_def->dpe_str);
			D_STRNDUP(entry_def->dpe_str, entry->dpe_str,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (entry_def->dpe_str == NULL)
				return -DER_NOMEM;
			break;
		case DAOS_PROP_PO_OWNER:
		case DAOS_PROP_PO_OWNER_GROUP:
			D_FREE(entry_def->dpe_str);
			D_STRNDUP(entry_def->dpe_str, entry->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_def->dpe_str == NULL)
				return -DER_NOMEM;
			break;
		case DAOS_PROP_PO_SPACE_RB:
		case DAOS_PROP_PO_SELF_HEAL:
		case DAOS_PROP_PO_RECLAIM:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_PO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl = entry->dpe_val_ptr;

				daos_prop_entry_dup_ptr(entry_def, entry,
							daos_acl_get_size(acl));
				if (entry_def->dpe_val_ptr == NULL)
					return -DER_NOMEM;
			}
			break;
		default:
			D_ERROR("ignore bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}

	return 0;
}

static int
pool_prop_write(struct rdb_tx *tx, const rdb_path_t *kvs, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	d_iov_t			 value;
	int			 i;
	int			 rc = 0;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		switch (entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
			if (entry->dpe_str == NULL ||
			    strlen(entry->dpe_str) == 0) {
				entry = daos_prop_entry_get(&pool_prop_default,
							    entry->dpe_type);
				D_ASSERT(entry != NULL);
			}
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_label,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_OWNER:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_owner,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_owner_group,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl;

				acl = entry->dpe_val_ptr;
				d_iov_set(&value, acl, daos_acl_get_size(acl));
				rc = rdb_tx_update(tx, kvs, &ds_pool_prop_acl,
						   &value);
				if (rc)
					return rc;
			}
			break;
		case DAOS_PROP_PO_SPACE_RB:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_space_rb,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_SELF_HEAL:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_self_heal,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_RECLAIM:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_reclaim,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_SVC_LIST:
			break;
		default:
			D_ERROR("bad dpe_type %d.\n", entry->dpe_type);
			return -DER_INVAL;
		}
	}

	return rc;
}

static int
init_pool_metadata(struct rdb_tx *tx, const rdb_path_t *kvs,
		   uint32_t nnodes, uuid_t target_uuids[], const char *group,
		   const d_rank_list_t *target_addrs, daos_prop_t *prop,
		   uint32_t ndomains, const int32_t *domains)
{
	struct pool_buf	       *map_buf;
	uint32_t		map_version = 1;
	uint32_t		connectable;
	uint32_t		nhandles = 0;
	uuid_t		       *uuids;
	d_iov_t			value;
	struct rdb_kvs_attr	attr;
	int			ntargets = nnodes * dss_tgt_nr;
	int			rc;

	/* Generate the pool buffer. */
	rc = gen_pool_buf(NULL, &map_buf, map_version, ndomains, nnodes,
			ntargets, domains, target_uuids, target_addrs, &uuids,
			dss_tgt_nr);
	if (rc != 0)
		D_GOTO(out_map_buf, rc);

	/* Initialize the pool map properties. */
	rc = write_map_buf(tx, kvs, map_buf, map_version);
	if (rc != 0)
		D_GOTO(out_uuids, rc);
	d_iov_set(&value, uuids, sizeof(uuid_t) * nnodes);
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_map_uuids, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	/* Write the optional properties. */
	rc = pool_prop_write(tx, kvs, prop);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	/* Write connectable property */
	connectable = 1;
	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_connectable, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	/* Write the handle properties. */
	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_uuids, rc);
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_pool_prop_handles, &attr);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

	/* Create pool user attributes KVS */
	rc = rdb_tx_create_kvs(tx, kvs, &ds_pool_attr_user, &attr);
	if (rc != 0)
		D_GOTO(out_uuids, rc);

out_uuids:
	D_FREE(uuids);
out_map_buf:
	pool_buf_free(map_buf);
	return rc;
}

/*
 * nreplicas inputs how many replicas are wanted, while ranks->rl_nr
 * outputs how many replicas are actually selected, which may be less than
 * nreplicas. If successful, callers are responsible for calling
 * d_rank_list_free(*ranksp).
 */
static int
select_svc_ranks(int nreplicas, const d_rank_list_t *target_addrs,
		 int ndomains, const int *domains, d_rank_list_t **ranksp)
{
	int			i_rank_zero = -1;
	int			selectable;
	d_rank_list_t       *ranks;
	int			i;
	int			j;

	if (nreplicas <= 0)
		return -DER_INVAL;

	/* Determine the number of selectable targets. */
	selectable = target_addrs->rl_nr;
	if (daos_rank_list_find((d_rank_list_t *)target_addrs, 0 /* rank */,
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
	for (i = 0; i < target_addrs->rl_nr; i++) {
		if (j == ranks->rl_nr)
			break;
		if (i == i_rank_zero && selectable > 1)
			/* This is rank 0 and it's not the only rank. */
			continue;
		D_DEBUG(DB_MD, "ranks[%d]: %u\n", j, target_addrs->rl_ranks[i]);
		ranks->rl_ranks[j] = target_addrs->rl_ranks[i];
		j++;
	}
	D_ASSERTF(j == ranks->rl_nr, "%d == %u\n", j, ranks->rl_nr);

	*ranksp = ranks;
	return 0;
}

/**
 * Create a (combined) pool(/container) service. This method shall be called on
 * a single storage node in the pool. "target_uuids" shall be an array of the
 * target UUIDs returned by the ds_pool_create() calls.
 *
 * \param[in]		pool_uuid	pool UUID
 * \param[in]		ntargets	number of targets in the pool
 * \param[in]		target_uuids	array of \a ntargets target UUIDs
 * \param[in]		group		crt group ID (unused now)
 * \param[in]		target_addrs	list of \a ntargets target ranks
 * \param[in]		ndomains	number of domains the pool spans over
 * \param[in]		domains		serialized domain tree
 * \param[in]		prop		pool properties
 * \param[in,out]	svc_addrs	\a svc_addrs.rl_nr inputs how many
 *					replicas shall be created; returns the
 *					list of pool service replica ranks
 */
int
ds_pool_svc_create(const uuid_t pool_uuid, int ntargets, uuid_t target_uuids[],
		   const char *group, const d_rank_list_t *target_addrs,
		   int ndomains, const int *domains, daos_prop_t *prop,
		   d_rank_list_t *svc_addrs)
{
	d_rank_list_t	       *ranks;
	uuid_t			rdb_uuid;
	d_iov_t		psid;
	struct rsvc_client	client;
	struct dss_module_info *info = dss_get_module_info();
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct pool_create_in  *in;
	struct pool_create_out *out;
	int			rc;

	D_ASSERTF(ntargets == target_addrs->rl_nr, "ntargets=%u num=%u\n",
		  ntargets, target_addrs->rl_nr);

	rc = select_svc_ranks(svc_addrs->rl_nr, target_addrs, ndomains,
			      domains, &ranks);
	if (rc != 0)
		D_GOTO(out, rc);

	uuid_generate(rdb_uuid);
	d_iov_set(&psid, (void *)pool_uuid, sizeof(uuid_t));
	rc = ds_rsvc_dist_start(DS_RSVC_CLASS_POOL, &psid, rdb_uuid, ranks,
				true /* create */, true /* bootstrap */,
				ds_rsvc_get_md_cap());
	if (rc != 0)
		D_GOTO(out_ranks, rc);

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out_creation, rc);

rechoose:
	/* Create a POOL_CREATE request. */
	ep.ep_grp = NULL;
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}
	rc = pool_req_create(info->dmi_ctx, &ep, POOL_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create POOL_CREATE RPC: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_client, rc);
	}
	in = crt_req_get(rpc);
	uuid_copy(in->pri_op.pi_uuid, pool_uuid);
	uuid_clear(in->pri_op.pi_hdl);
	in->pri_ntgts = ntargets;
	in->pri_tgt_uuids.ca_count = ntargets;
	in->pri_tgt_uuids.ca_arrays = target_uuids;
	in->pri_tgt_ranks = (d_rank_list_t *)target_addrs;
	in->pri_prop = prop;
	in->pri_ndomains = ndomains;
	in->pri_domains.ca_count = ndomains;
	in->pri_domains.ca_arrays = (int *)domains;

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
		D_ERROR(DF_UUID": failed to create pool: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_rpc, rc);
	}

	rc = daos_rank_list_copy(svc_addrs, ranks);
	D_ASSERTF(rc == 0, "daos_rank_list_copy: "DF_RC"\n", DP_RC(rc));
out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out_creation:
	if (rc != 0)
		ds_rsvc_dist_stop(DS_RSVC_CLASS_POOL, &psid, ranks,
				  NULL, true /* destroy */);
out_ranks:
	d_rank_list_free(ranks);
out:
	return rc;
}

int
ds_pool_svc_destroy(const uuid_t pool_uuid)
{
	d_iov_t		psid;
	d_rank_list_t	excluded = { 0 };
	int		rc;

	ds_rebuild_leader_stop(pool_uuid, -1);
	rc = ds_pool_get_ranks(pool_uuid, MAP_RANKS_DOWN, &excluded);
	if (rc)
		return rc;

	d_iov_set(&psid, (void *)pool_uuid, sizeof(uuid_t));
	rc = ds_rsvc_dist_stop(DS_RSVC_CLASS_POOL, &psid, NULL /* ranks */,
			       &excluded, true /* destroy */);
	map_ranks_fini(&excluded);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to destroy pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));

	return rc;
}

static int
pool_svc_name_cb(d_iov_t *id, char **name)
{
	char *s;

	if (id->iov_len != sizeof(uuid_t))
		return -DER_INVAL;
	D_ALLOC(s, DAOS_UUID_STR_SIZE);
	if (s == NULL)
		return -DER_NOMEM;
	uuid_unparse_lower(id->iov_buf, s);
	s[8] = '\0'; /* strlen(DF_UUID) */
	*name = s;
	return 0;
}

static int
pool_svc_load_uuid_cb(d_iov_t *id, uuid_t db_uuid)
{
	char   *path;
	int	rc;

	if (id->iov_len != sizeof(uuid_t))
		return -DER_INVAL;
	path = pool_svc_rdb_uuid_path(id->iov_buf);
	if (path == NULL)
		return -DER_NOMEM;
	rc = uuid_load(path, db_uuid);
	D_FREE(path);
	return rc;
}

static int
pool_svc_store_uuid_cb(d_iov_t *id, uuid_t db_uuid)
{
	char   *path;
	int	rc;

	if (id->iov_len != sizeof(uuid_t))
		return -DER_INVAL;
	path = pool_svc_rdb_uuid_path(id->iov_buf);
	if (path == NULL)
		return -DER_NOMEM;
	rc = uuid_store(path, db_uuid);
	D_FREE(path);
	return rc;
}

static int
pool_svc_delete_uuid_cb(d_iov_t *id)
{
	char   *path;
	int	rc;

	if (id->iov_len != sizeof(uuid_t))
		return -DER_INVAL;
	path = pool_svc_rdb_uuid_path(id->iov_buf);
	if (path == NULL)
		return -DER_NOMEM;
	rc = remove(path);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to remove %s: %d\n",
			DP_UUID(id->iov_buf), path, errno);
		rc = daos_errno2der(errno);
	}
	D_FREE(path);
	return rc;
}

static int
pool_svc_locate_cb(d_iov_t *id, char **path)
{
	char *s;

	if (id->iov_len != sizeof(uuid_t))
		return -DER_INVAL;
	s = pool_svc_rdb_path(id->iov_buf);
	if (s == NULL)
		return -DER_NOMEM;
	*path = s;
	return 0;
}

static int
pool_svc_alloc_cb(d_iov_t *id, struct ds_rsvc **rsvc)
{
	struct pool_svc	       *svc;
	int			rc;

	if (id->iov_len != sizeof(uuid_t)) {
		rc = -DER_INVAL;
		goto err;
	}

	D_ALLOC_PTR(svc);
	if (svc == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}

	d_iov_set(&svc->ps_rsvc.s_id, svc->ps_uuid, sizeof(uuid_t));

	uuid_copy(svc->ps_uuid, id->iov_buf);

	rc = ABT_rwlock_create(&svc->ps_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ps_lock: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_svc;
	}

	rc = rdb_path_init(&svc->ps_root);
	if (rc != 0)
		goto err_lock;
	rc = rdb_path_push(&svc->ps_root, &rdb_path_root_key);
	if (rc != 0)
		goto err_root;

	rc = rdb_path_clone(&svc->ps_root, &svc->ps_handles);
	if (rc != 0)
		goto err_root;
	rc = rdb_path_push(&svc->ps_handles, &ds_pool_prop_handles);
	if (rc != 0)
		goto err_handles;

	rc = rdb_path_clone(&svc->ps_root, &svc->ps_user);
	if (rc != 0)
		goto err_handles;
	rc = rdb_path_push(&svc->ps_user, &ds_pool_attr_user);
	if (rc != 0)
		goto err_user;

	rc = ds_cont_svc_init(&svc->ps_cont_svc, svc->ps_uuid, 0 /* id */,
			      &svc->ps_rsvc);
	if (rc != 0)
		goto err_user;

	*rsvc = &svc->ps_rsvc;
	return 0;

err_user:
	rdb_path_fini(&svc->ps_user);
err_handles:
	rdb_path_fini(&svc->ps_handles);
err_root:
	rdb_path_fini(&svc->ps_root);
err_lock:
	ABT_rwlock_free(&svc->ps_lock);
err_svc:
	D_FREE(svc);
err:
	return rc;
}

static void
pool_svc_get(struct pool_svc *svc)
{
	ds_rsvc_get(&svc->ps_rsvc);
}

static void
pool_svc_put(struct pool_svc *svc)
{
	ds_rsvc_put(&svc->ps_rsvc);
}

struct ds_pool_evict_arg {
	struct pool_svc *svc;
	d_rank_t	rank;
};

static void
pool_evict_rank_ult(void *data)
{
	struct ds_pool_evict_arg *arg = data;
	int			 rc;

	rc = ds_pool_evict_rank(arg->svc->ps_uuid, arg->rank);

	D_DEBUG(DB_MGMT, DF_UUID" evict rank %u : rc %d\n",
		DP_UUID(arg->svc->ps_uuid), arg->rank, rc);

	pool_svc_put(arg->svc);
	D_FREE_PTR(arg);
}

/* Disable all pools eviction */
void
ds_pool_disable_evict(void)
{
	pool_disable_evict = true;
}

void
ds_pool_enable_evict(void)
{
	pool_disable_evict = false;
}

static int
pool_evict_rank(struct pool_svc *svc, d_rank_t rank)
{
	struct ds_pool_evict_arg	*ult_arg;
	int				rc;

	D_ALLOC_PTR(ult_arg);
	if (ult_arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	pool_svc_get(svc);
	ult_arg->svc = svc;
	ult_arg->rank = rank;
	rc = dss_ult_create(pool_evict_rank_ult, ult_arg, DSS_XS_SELF,
			    0, 0, NULL);
	if (rc) {
		pool_svc_put(svc);
		D_FREE_PTR(ult_arg);
	}
out:
	if (rc)
		D_ERROR("evict ult failed: rc %d\n", rc);
	return rc;
}

static void
ds_pool_crt_event_cb(d_rank_t rank, enum crt_event_source src,
		     enum crt_event_type type, void *arg)
{
	daos_prop_t		prop = { 0 };
	struct daos_prop_entry	*entry;
	struct pool_svc		*svc = arg;
	int			rc = 0;

	/* Only used for evict the rank for the moment */
	if (src != CRT_EVS_SWIM || type != CRT_EVT_DEAD || pool_disable_evict) {
		D_DEBUG(DB_MGMT, "ignore src/type/evict %u/%u/%d\n",
			src, type, pool_disable_evict);
		return;
	}

	rc = ds_pool_iv_prop_fetch(svc->ps_pool, &prop);
	if (rc)
		D_GOTO(out, rc);

	entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SELF_HEAL);
	D_ASSERT(entry != NULL);
	if (!(entry->dpe_val & DAOS_SELF_HEAL_AUTO_EXCLUDE)) {
		D_DEBUG(DB_MGMT, "self healing is disabled\n");
		D_GOTO(out, rc);
	}

	rc = pool_evict_rank(svc, rank);
out:
	if (rc)
		D_ERROR("pool "DF_UUID" event %d failed: rc %d\n",
			DP_UUID(svc->ps_uuid), src, rc);
	daos_prop_fini(&prop);
}

static void
pool_svc_free_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc *svc = pool_svc_obj(rsvc);

	ds_cont_svc_fini(&svc->ps_cont_svc);
	rdb_path_fini(&svc->ps_user);
	rdb_path_fini(&svc->ps_handles);
	rdb_path_fini(&svc->ps_root);
	ABT_rwlock_free(&svc->ps_lock);
	D_FREE(svc);
}

/*
 * Initialize and update svc->ps_pool with map_buf and map_version. This
 * ensures that svc->ps_pool matches the latest pool map.
 */
static int
init_svc_pool(struct pool_svc *svc, struct pool_buf *map_buf,
	      uint32_t map_version)
{
	struct ds_pool *pool;
	int		rc;

	pool = ds_pool_lookup(svc->ps_uuid);
	if (pool == NULL) {
		D_ERROR(DF_UUID": failed to get ds_pool\n",
			DP_UUID(svc->ps_uuid));
		return -DER_NONEXIST;
	}
	rc = ds_pool_tgt_map_update(pool, map_buf, map_version);
	if (rc != 0) {
		ds_pool_put(pool);
		return rc;
	}
	ds_pool_iv_ns_update(pool, dss_self_rank());

	D_ASSERT(svc->ps_pool == NULL);
	svc->ps_pool = pool;
	return 0;
}

/* Finalize svc->ps_pool. */
static void
fini_svc_pool(struct pool_svc *svc)
{
	D_ASSERT(svc->ps_pool != NULL);
	ds_pool_iv_ns_update(svc->ps_pool, -1 /* master_rank */);
	ds_pool_put(svc->ps_pool);
	svc->ps_pool = NULL;
}

/*
 * There might be some swim status inconsistency, let's check and
 * fix it.
 */
static int
pool_svc_check_node_status(struct pool_svc *svc)
{
	struct pool_domain	*doms;
	int			doms_cnt;
	int			i;
	int			rc = 0;

	if (pool_disable_evict) {
		D_DEBUG(DB_REBUILD, DF_UUID" disable swim evict.\n",
			DP_UUID(svc->ps_uuid));
		return 0;
	}

	doms_cnt = pool_map_find_nodes(svc->ps_pool->sp_map, PO_COMP_ID_ALL,
				       &doms);
	D_ASSERT(doms_cnt >= 0);
	for (i = 0; i < doms_cnt; i++) {
		struct swim_member_state state;

		/* Only check if UPIN server becomes DEAD for now */
		if (!(doms[i].do_comp.co_status & PO_COMP_ST_UPIN))
			continue;

		rc = crt_rank_state_get(crt_group_lookup(NULL),
				   doms[i].do_comp.co_rank, &state);
		if (rc != 0) {
			D_ERROR("failed to get swim for rank %u: %d\n",
				doms[i].do_comp.co_rank, rc);
			break;
		}

		/* Since there is a big chance the INACTIVE node will become
		 * ACTIVE soon, let's only evict the DEAD node rank for the
		 * moment.
		 */
		D_DEBUG(DB_REBUILD, "rank/state %d/%d\n",
			doms[i].do_comp.co_rank, state.sms_status);
		if (state.sms_status == SWIM_MEMBER_DEAD) {
			rc = pool_evict_rank(svc, doms[i].do_comp.co_rank);
			if (rc) {
				D_ERROR("failed to evict rank %u: %d\n",
					doms[i].do_comp.co_rank, rc);
				break;
			}
		}
	}
	return rc;
}

static int
pool_svc_step_up_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc	       *svc = pool_svc_obj(rsvc);
	struct rdb_tx		tx;
	struct pool_buf	       *map_buf = NULL;
	uint32_t		map_version;
	uuid_t			pool_hdl_uuid;
	uuid_t			cont_hdl_uuid;
	daos_prop_t	       *prop = NULL;
	uint64_t		prop_bits;
	bool			cont_svc_up = false;
	bool			event_cb_registered = false;
	d_rank_t		rank;
	int			rc;

	/* Read the pool map into map_buf and map_version. */
	rc = rdb_tx_begin(rsvc->s_db, rsvc->s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_rdlock(svc->ps_lock);
	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_MD, DF_UUID": new db\n",
				DP_UUID(svc->ps_uuid));
			rc = +DER_UNINIT;
		} else {
			D_ERROR(DF_UUID": failed to read pool map buffer: "
				""DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
		}
		D_GOTO(unlock, rc);
	}
	prop_bits = DAOS_PO_QUERY_PROP_ALL;
	rc = pool_prop_read(&tx, svc, prop_bits, &prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot get access data for pool, "
			"rc="DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(unlock, rc);
	}
unlock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out;

	rc = init_svc_pool(svc, map_buf, map_version);
	if (rc != 0)
		goto out;

	/*
	 * Just in case the previous leader didn't complete distributing the
	 * latest pool map. This doesn't need to be undone if we encounter an
	 * error below.
	 */
	ds_rsvc_request_map_dist(&svc->ps_rsvc);

	ds_cont_svc_step_up(svc->ps_cont_svc);
	cont_svc_up = true;

	rc = crt_register_event_cb(ds_pool_crt_event_cb, svc);
	if (rc)
		goto out;
	event_cb_registered = true;

	rc = ds_pool_iv_prop_update(svc->ps_pool, prop);
	if (rc) {
		D_ERROR("ds_pool_iv_prop_update failed %d.\n", rc);
		D_GOTO(out, rc);
	}

	rc = pool_svc_check_node_status(svc);
	if (rc)
		D_GOTO(out, rc);

	if (!uuid_is_null(svc->ps_pool->sp_srv_cont_hdl)) {
		uuid_copy(pool_hdl_uuid, svc->ps_pool->sp_srv_pool_hdl);
		uuid_copy(cont_hdl_uuid, svc->ps_pool->sp_srv_cont_hdl);
	} else {
		uuid_generate(pool_hdl_uuid);
		uuid_generate(cont_hdl_uuid);
	}

	rc = ds_pool_iv_srv_hdl_update(svc->ps_pool, pool_hdl_uuid,
				       cont_hdl_uuid);
	if (rc) {
		D_ERROR("ds_pool_iv_srv_hdl_update failed %d.\n", rc);
		D_GOTO(out, rc);
	}

	D_PRINT(DF_UUID": pool/cont hdl uuid "DF_UUID"/"DF_UUID"\n",
		DP_UUID(svc->ps_uuid), DP_UUID(pool_hdl_uuid),
		DP_UUID(cont_hdl_uuid));

	rc = ds_rebuild_regenerate_task(svc->ps_pool);
	if (rc != 0)
		goto out;

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	D_PRINT(DF_UUID": rank %u became pool service leader "DF_U64"\n",
		DP_UUID(svc->ps_uuid), rank, svc->ps_rsvc.s_term);
out:
	if (rc != 0) {
		if (event_cb_registered)
			crt_unregister_event_cb(ds_pool_crt_event_cb, svc);
		if (cont_svc_up)
			ds_cont_svc_step_down(svc->ps_cont_svc);
		if (svc->ps_pool != NULL)
			fini_svc_pool(svc);
	}
	if (map_buf != NULL)
		D_FREE(map_buf);
	if (prop != NULL)
		daos_prop_free(prop);
	return rc;
}

static void
pool_svc_step_down_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc	       *svc = pool_svc_obj(rsvc);
	d_rank_t		rank;
	int			rc;

	crt_unregister_event_cb(ds_pool_crt_event_cb, svc);

	ds_pool_iv_srv_hdl_invalidate(svc->ps_pool);
	ds_cont_svc_step_down(svc->ps_cont_svc);
	fini_svc_pool(svc);

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	D_PRINT(DF_UUID": rank %u no longer pool service leader "DF_U64"\n",
		DP_UUID(svc->ps_uuid), rank, svc->ps_rsvc.s_term);
}

static void
pool_svc_drain_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc *svc = pool_svc_obj(rsvc);

	ds_rebuild_leader_stop(svc->ps_uuid, -1);
}

static int
pool_svc_map_dist_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc	       *svc = pool_svc_obj(rsvc);
	struct rdb_tx		tx;
	struct pool_buf	       *map_buf = NULL;
	uint32_t		map_version;
	int			rc;

	/* Read the pool map into map_buf and map_version. */
	rc = rdb_tx_begin(rsvc->s_db, rsvc->s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_rdlock(svc->ps_lock);
	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map buffer: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		goto out;
	}

	rc = ds_pool_iv_map_update(svc->ps_pool, map_buf, map_version);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to distribute pool map %u: %d\n",
			DP_UUID(svc->ps_uuid), map_version, rc);

out:
	if (map_buf != NULL)
		D_FREE(map_buf);
	return rc;
}

static struct ds_rsvc_class pool_svc_rsvc_class = {
	.sc_name	= pool_svc_name_cb,
	.sc_load_uuid	= pool_svc_load_uuid_cb,
	.sc_store_uuid	= pool_svc_store_uuid_cb,
	.sc_delete_uuid	= pool_svc_delete_uuid_cb,
	.sc_locate	= pool_svc_locate_cb,
	.sc_alloc	= pool_svc_alloc_cb,
	.sc_free	= pool_svc_free_cb,
	.sc_step_up	= pool_svc_step_up_cb,
	.sc_step_down	= pool_svc_step_down_cb,
	.sc_drain	= pool_svc_drain_cb,
	.sc_map_dist	= pool_svc_map_dist_cb
};

void
ds_pool_rsvc_class_register(void)
{
	ds_rsvc_class_register(DS_RSVC_CLASS_POOL, &pool_svc_rsvc_class);
}

void
ds_pool_rsvc_class_unregister(void)
{
	ds_rsvc_class_unregister(DS_RSVC_CLASS_POOL);
}

static int
pool_svc_lookup(uuid_t uuid, struct pool_svc **svcp)
{
	struct ds_rsvc *rsvc;
	d_iov_t	id;
	int		rc;

	d_iov_set(&id, uuid, sizeof(uuid_t));
	rc = ds_rsvc_lookup(DS_RSVC_CLASS_POOL, &id, &rsvc);
	if (rc != 0)
		return rc;
	*svcp = pool_svc_obj(rsvc);
	return 0;
}

static int
pool_svc_lookup_leader(uuid_t uuid, struct pool_svc **svcp,
		       struct rsvc_hint *hint)
{
	struct ds_rsvc *rsvc;
	d_iov_t	id;
	int		rc;

	d_iov_set(&id, uuid, sizeof(uuid_t));
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_POOL, &id, &rsvc, hint);
	if (rc != 0)
		return rc;
	*svcp = pool_svc_obj(rsvc);
	return 0;
}

static void
pool_svc_put_leader(struct pool_svc *svc)
{
	ds_rsvc_put_leader(&svc->ps_rsvc);
}

/** Look up container service \a pool_uuid. */
int
ds_pool_cont_svc_lookup_leader(uuid_t pool_uuid, struct cont_svc **svcp,
			       struct rsvc_hint *hint)
{
	struct pool_svc	       *pool_svc;
	int			rc;

	rc = pool_svc_lookup_leader(pool_uuid, &pool_svc, hint);
	if (rc != 0)
		return rc;
	*svcp = pool_svc->ps_cont_svc;
	return 0;
}

/*
 * Try to start the pool. If a pool service RDB exists, start it. Continue the
 * iteration upon errors as other pools may still be able to work.
 */
static int
start_one(uuid_t uuid, void *varg)
{
	char	       *path;
	d_iov_t		id;
	struct stat	st;
	int		rc;

	D_DEBUG(DB_MD, DF_UUID": starting pool\n", DP_UUID(uuid));

	rc = ds_pool_start(uuid);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start pool: %d\n", DP_UUID(uuid),
			rc);
		return 0;
	}

	/*
	 * Check if an RDB file exists, to avoid unnecessary error messages
	 * from the ds_rsvc_start() call.
	 */
	path = pool_svc_rdb_path(uuid);
	if (path == NULL) {
		D_ERROR(DF_UUID": failed to allocate rdb path\n",
			DP_UUID(uuid));
		return 0;
	}
	rc = stat(path, &st);
	D_FREE(path);
	if (rc != 0) {
		if (errno != ENOENT)
			D_ERROR(DF_UUID": failed to check rdb existence: %d\n",
				DP_UUID(uuid), errno);
		return 0;
	}

	d_iov_set(&id, uuid, sizeof(uuid_t));
	ds_rsvc_start(DS_RSVC_CLASS_POOL, &id, NULL /* db_uuid */,
		      false /* create */, 0 /* size */, NULL /* replicas */,
		      NULL /* arg */);
	return 0;
}

static void
pool_start_all(void *arg)
{
	int rc;

	/* Scan the storage and start all pool services. */
	rc = ds_mgmt_tgt_pool_iterate(start_one, NULL /* arg */);
	if (rc != 0)
		D_ERROR("failed to scan all pool services: "DF_RC"\n",
			DP_RC(rc));
}

/* Note that this function is currently called from the main xstream. */
int
ds_pool_start_all(void)
{
	ABT_thread	thread;
	int		rc;

	/* Create a ULT to call ds_rsvc_start() in xstream 0. */
	rc = dss_ult_create(pool_start_all, NULL /* arg */, DSS_XS_SYS,
			    0 /* tgt_idx */, 0 /* stack_size */, &thread);
	if (rc != 0) {
		D_ERROR("failed to create pool start ULT: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}
	ABT_thread_join(thread);
	ABT_thread_free(&thread);
	return 0;
}

/*
 * Note that this function is currently called from the main xstream to save
 * one ULT creation.
 */
int
ds_pool_stop_all(void)
{
	/*
	 * TODO: Before returning, release the ds_pool references held by
	 * ds_pool_start_all.
	 */
	return ds_rsvc_stop_all(DS_RSVC_CLASS_POOL);
}

static int
bcast_create(crt_context_t ctx, struct pool_svc *svc, crt_opcode_t opcode,
	     crt_bulk_t bulk_hdl, crt_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->ps_pool, DAOS_POOL_MODULE, opcode,
				    rpc, bulk_hdl, NULL);
}

/**
 * Retrieve the latest leader hint from \a db and fill it into \a hint.
 *
 * \param[in]	db	database
 * \param[out]	hint	rsvc hint
 */
void
ds_pool_set_hint(struct rdb *db, struct rsvc_hint *hint)
{
	int rc;

	rc = rdb_get_leader(db, &hint->sh_term, &hint->sh_rank);
	if (rc != 0)
		return;
	hint->sh_flags |= RSVC_HINT_VALID;
}

static int
pool_prop_read(struct rdb_tx *tx, const struct pool_svc *svc, uint64_t bits,
	       daos_prop_t **prop_out)
{
	daos_prop_t	*prop;
	d_iov_t	 value;
	uint64_t	 val;
	uint32_t	 idx = 0, nr = 0;
	int		 rc;

	if (bits & DAOS_PO_QUERY_PROP_LABEL)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_SPACE_RB)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_SELF_HEAL)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_RECLAIM)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_ACL)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_OWNER)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_OWNER_GROUP)
		nr++;
	if (bits & DAOS_PO_QUERY_PROP_SVC_LIST)
		nr++;
	if (nr == 0)
		return 0;

	prop = daos_prop_alloc(nr);
	if (prop == NULL)
		return -DER_NOMEM;
	*prop_out = prop;
	if (bits & DAOS_PO_QUERY_PROP_LABEL) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_label,
				   &value);
		if (rc != 0)
			return rc;
		if (value.iov_len > DAOS_PROP_LABEL_MAX_LEN) {
			D_ERROR("bad label length %zu (> %d).\n", value.iov_len,
				DAOS_PROP_LABEL_MAX_LEN);
			return -DER_IO;
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_LABEL;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			return -DER_NOMEM;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SPACE_RB) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_space_rb,
				   &value);
		if (rc != 0)
			return rc;
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SPACE_RB;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SELF_HEAL) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_self_heal,
				   &value);
		if (rc != 0)
			return rc;
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SELF_HEAL;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_RECLAIM) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_reclaim,
				   &value);
		if (rc != 0)
			return rc;
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_RECLAIM;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_ACL) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_acl,
				   &value);
		if (rc != 0)
			return rc;
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_ACL;
		D_ALLOC(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf_len);
		if (prop->dpp_entries[idx].dpe_val_ptr == NULL)
			return -DER_NOMEM;
		memcpy(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf,
		       value.iov_buf_len);
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_OWNER) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_owner,
				   &value);
		if (rc != 0)
			return rc;
		if (value.iov_len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
			D_ERROR("bad owner length %zu (> %d).\n", value.iov_len,
				DAOS_ACL_MAX_PRINCIPAL_LEN);
			return -DER_IO;
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OWNER;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			return -DER_NOMEM;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_OWNER_GROUP) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_owner_group,
				   &value);
		if (rc != 0)
			return rc;
		if (value.iov_len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
			D_ERROR("bad owner group length %zu (> %d).\n",
				value.iov_len,
				DAOS_ACL_MAX_PRINCIPAL_LEN);
			return -DER_IO;
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OWNER_GROUP;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			return -DER_NOMEM;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SVC_LIST) {
		d_rank_list_t	*svc_list = NULL;

		d_iov_set(&value, NULL, 0);
		rc = rdb_get_ranks(svc->ps_rsvc.s_db, &svc_list);
		if (rc) {
			D_ERROR("get svc list failed: rc %d\n", rc);
			return rc;
		}
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SVC_LIST;
		prop->dpp_entries[idx].dpe_val_ptr = svc_list;
		idx++;
	}

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
	d_iov_t		value;
	struct rdb_kvs_attr	attr;
	daos_prop_t	       *prop_dup = NULL;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pri_op.pi_uuid), rpc);

	if (in->pri_ntgts != in->pri_tgt_uuids.ca_count ||
	    in->pri_ntgts != in->pri_tgt_ranks->rl_nr)
		D_GOTO(out, rc = -DER_PROTO);
	if (in->pri_ndomains != in->pri_domains.ca_count)
		D_GOTO(out, rc = -DER_PROTO);

	/* This RPC doesn't care about whether the service is up. */
	rc = pool_svc_lookup(in->pri_op.pi_uuid, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * Simply serialize this whole RPC with rsvc_step_{up,down}_cb() and
	 * ds_rsvc_stop().
	 */
	ABT_mutex_lock(svc->ps_rsvc.s_mutex);

	if (svc->ps_rsvc.s_stop) {
		D_DEBUG(DB_MD, DF_UUID": pool service already stopping\n",
			DP_UUID(svc->ps_uuid));
		D_GOTO(out_mutex, rc = -DER_CANCELED);
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, RDB_NIL_TERM, &tx);
	if (rc != 0)
		D_GOTO(out_mutex, rc);
	ABT_rwlock_wrlock(svc->ps_lock);
	ds_cont_wrlock_metadata(svc->ps_cont_svc);

	/* See if the DB has already been initialized. */
	d_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_map_buffer,
			   &value);
	if (rc != -DER_NONEXIST) {
		if (rc == 0)
			D_DEBUG(DF_DSMS, DF_UUID": db already initialized\n",
				DP_UUID(svc->ps_uuid));
		else
			D_ERROR(DF_UUID": failed to look up pool map: "
				""DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_tx, rc);
	}

	/* duplicate the default properties, overwrite it with pool create
	 * parameter and then write to pool meta data.
	 */
	prop_dup = daos_prop_dup(&pool_prop_default, true);
	if (prop_dup == NULL) {
		D_ERROR("daos_prop_dup failed.\n");
		D_GOTO(out_tx, rc = -DER_NOMEM);
	}
	rc = pool_prop_default_copy(prop_dup, in->pri_prop);
	if (rc) {
		D_ERROR("daos_prop_default_copy failed.\n");
		D_GOTO(out_tx, rc);
	}

	/* Initialize the DB and the metadata for this pool. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 8;
	rc = rdb_tx_create_root(&tx, &attr);
	if (rc != 0)
		D_GOTO(out_tx, rc);
	rc = init_pool_metadata(&tx, &svc->ps_root, in->pri_tgt_uuids.ca_count,
				in->pri_tgt_uuids.ca_arrays, NULL /* group */,
				in->pri_tgt_ranks, prop_dup,
				in->pri_ndomains, in->pri_domains.ca_arrays);
	if (rc != 0)
		D_GOTO(out_tx, rc);
	rc = ds_cont_init_metadata(&tx, &svc->ps_root, in->pri_op.pi_uuid);
	if (rc != 0)
		D_GOTO(out_tx, rc);

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_GOTO(out_tx, rc);

out_tx:
	daos_prop_free(prop_dup);
	ds_cont_unlock_metadata(svc->ps_cont_svc);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	if (svc->ps_rsvc.s_state == DS_RSVC_UP_EMPTY) {
		/*
		 * The DB is no longer empty. Since the previous
		 * pool_svc_step_up_cb() call didn't finish stepping up due to
		 * an empty DB, and there hasn't been a pool_svc_step_down_cb()
		 * call yet, we should call pool_svc_step_up() to finish
		 * stepping up.
		 */
		D_DEBUG(DF_DSMS, DF_UUID": trying to finish stepping up\n",
			DP_UUID(in->pri_op.pi_uuid));
		rc = pool_svc_step_up_cb(&svc->ps_rsvc);
		if (rc != 0) {
			D_ASSERT(rc != DER_UNINIT);
			/* TODO: Ask rdb to step down. */
			D_GOTO(out_svc, rc);
		}
		svc->ps_rsvc.s_state = DS_RSVC_UP;
		ABT_cond_broadcast(svc->ps_rsvc.s_state_cv);
	}

out_mutex:
	ABT_mutex_unlock(svc->ps_rsvc.s_mutex);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pro_op.po_hint);
	pool_svc_put(svc);
out:
	out->pro_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pri_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

static int
pool_connect_iv_dist(struct pool_svc *svc, uuid_t pool_hdl,
		     uint64_t flags, uint64_t sec_capas, d_iov_t *cred)
{
	d_rank_t rank;
	int	 rc;

	D_DEBUG(DF_DSMS, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	rc = crt_group_rank(svc->ps_pool->sp_group, &rank);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_pool_iv_conn_hdl_update(svc->ps_pool, pool_hdl, flags,
					sec_capas, cred);
	if (rc)
		D_GOTO(out, rc);
out:
	D_DEBUG(DF_DSMS, DF_UUID": bcasted: "DF_RC"\n", DP_UUID(svc->ps_uuid),
		DP_RC(rc));
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
 * If the map_buf_bulk is non-NULL, then the created local bulk handle for
 * pool_buf will be returned and caller needs to do crt_bulk_free later.
 * If the map_buf_bulk is NULL then the internally created local bulk handle
 * will be freed within this function.
 */
static int
transfer_map_buf(struct pool_buf *map_buf, uint32_t map_version,
		 struct pool_svc *svc, crt_rpc_t *rpc,
		 crt_bulk_t remote_bulk, uint32_t *required_buf_size)
{
	size_t			map_buf_size;
	daos_size_t		remote_bulk_size;
	d_iov_t		map_iov;
	d_sg_list_t		map_sgl;
	crt_bulk_t		bulk = CRT_BULK_NULL;
	struct crt_bulk_desc	map_desc;
	crt_bulk_opid_t		map_opid;
	ABT_eventual		eventual;
	int		       *status;
	int			rc;

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

	d_iov_set(&map_iov, map_buf, map_buf_size);
	map_sgl.sg_nr = 1;
	map_sgl.sg_nr_out = 0;
	map_sgl.sg_iovs = &map_iov;

	rc = crt_bulk_create(rpc->cr_ctx, &map_sgl, CRT_BULK_RO, &bulk);
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
	if (bulk != CRT_BULK_NULL)
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
	struct pool_buf		       *map_buf = NULL;
	uint32_t			map_version;
	uint32_t			connectable;
	struct rdb_tx			tx;
	d_iov_t				key;
	d_iov_t				value;
	struct pool_hdl			hdl;
	uint32_t			nhandles;
	int				skip_update = 0;
	int				rc;
	daos_prop_t		       *prop = NULL;
	uint64_t			prop_bits;
	struct daos_prop_entry	       *acl_entry;
	struct ownership		owner;
	struct daos_prop_entry	       *owner_entry;
	struct daos_prop_entry	       *owner_grp_entry;
	uint64_t			sec_capas = 0;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pci_op.pi_uuid), rpc, DP_UUID(in->pci_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pci_op.pi_uuid, &svc,
				    &out->pco_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	if (in->pci_query_bits & DAOS_PO_QUERY_REBUILD_STATUS) {
		rc = ds_rebuild_query(in->pci_op.pi_uuid, &out->pco_rebuild_st);
		if (rc != 0)
			D_GOTO(out_svc, rc);
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	/* Check if pool is being destroyed and not accepting connections */
	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_lookup(&tx, &svc->ps_root,
			   &ds_pool_prop_connectable, &value);
	if (rc != 0)
		D_GOTO(out_lock, rc);
	D_DEBUG(DF_DSMS, DF_UUID": connectable=%u\n",
		DP_UUID(in->pci_op.pi_uuid), connectable);
	if (!connectable) {
		D_ERROR(DF_UUID": being destroyed, not accepting connections\n",
			DP_UUID(in->pci_op.pi_uuid));
		D_GOTO(out_lock, rc = -DER_BUSY);
	}

	/* Check existing pool handles. */
	d_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, &hdl, sizeof(hdl));
	rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
	if (rc == 0) {
		if (hdl.ph_flags == in->pci_flags) {
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

	/* Fetch properties, the  ACL and ownership info for access check,
	 * all properties will update to IV.
	 */
	prop_bits = DAOS_PO_QUERY_PROP_ALL;
	rc = pool_prop_read(&tx, svc, prop_bits, &prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot get access data for pool, "
			"rc="DF_RC"\n", DP_UUID(in->pci_op.pi_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}
	D_ASSERT(prop != NULL);

	acl_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
	D_ASSERT(acl_entry != NULL);
	D_ASSERT(acl_entry->dpe_val_ptr != NULL);

	owner_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER);
	D_ASSERT(owner_entry != NULL);
	D_ASSERT(owner_entry->dpe_str != NULL);

	owner_grp_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	D_ASSERT(owner_grp_entry != NULL);
	D_ASSERT(owner_grp_entry->dpe_str != NULL);

	owner.user = owner_entry->dpe_str;
	owner.group = owner_grp_entry->dpe_str;

	/*
	 * Security capabilities determine the access control policy on this
	 * pool handle.
	 */
	rc = ds_sec_pool_get_capabilities(in->pci_flags, &in->pci_cred, &owner,
					  acl_entry->dpe_val_ptr,
					  &sec_capas);
	if (rc != 0) {
		D_ERROR(DF_UUID": refusing connect attempt for "
			DF_X64" error: "DF_RC"\n", DP_UUID(in->pci_op.pi_uuid),
			in->pci_flags, DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}

	if (!ds_sec_pool_can_connect(sec_capas)) {
		D_ERROR(DF_UUID": permission denied for connect attempt for "
			DF_X64"\n", DP_UUID(in->pci_op.pi_uuid),
			in->pci_flags);
		D_GOTO(out_map_version, rc = -DER_NO_PERM);
	}

	/*
	 * Transfer the pool map to the client before adding the pool handle,
	 * so that we don't need to worry about rolling back the transaction
	 * when the transfer fails. The client has already been authenticated
	 * and authorized at this point. If an error occurs after the transfer
	 * completes, then we simply return the error and the client will throw
	 * its pool_buf away.
	 */
	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}
	rc = transfer_map_buf(map_buf, map_version, svc, rpc, in->pci_map_bulk,
			      &out->pco_map_buf_size);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	if (skip_update)
		D_GOTO(out_map_version, rc = 0);

	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	/* Take care of exclusive handles. */
	if (nhandles != 0) {
		if (in->pci_flags & DAOS_PC_EX) {
			D_DEBUG(DF_DSMS, DF_UUID": others already connected\n",
				DP_UUID(in->pci_op.pi_uuid));
			D_GOTO(out_map_version, rc = -DER_BUSY);
		} else {
			/*
			 * If there is a non-exclusive handle, then all handles
			 * are non-exclusive.
			 */
			d_iov_set(&value, &hdl, sizeof(hdl));
			rc = rdb_tx_fetch(&tx, &svc->ps_handles,
					  RDB_PROBE_FIRST, NULL /* key_in */,
					  NULL /* key_out */, &value);
			if (rc != 0)
				D_GOTO(out_map_version, rc);
			if (hdl.ph_flags & DAOS_PC_EX)
				D_GOTO(out_map_version, rc = -DER_BUSY);
		}
	}

	rc = pool_connect_iv_dist(svc, in->pci_op.pi_hdl, in->pci_flags,
				  sec_capas, &in->pci_cred);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_CONNECT_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to connect to targets: "DF_RC"\n",
			DP_UUID(in->pci_op.pi_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}

	hdl.ph_flags = in->pci_flags;
	hdl.ph_sec_capas = sec_capas;
	nhandles++;
	d_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, &hdl, sizeof(hdl));
	rc = rdb_tx_update(&tx, &svc->ps_handles, &key, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(&tx, &svc->ps_root, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	rc = rdb_tx_commit(&tx);
	if (rc)
		D_GOTO(out_map_version, rc);

	if (in->pci_query_bits & DAOS_PO_QUERY_SPACE)
		rc = pool_space_query_bcast(rpc->cr_ctx, svc, in->pci_op.pi_hdl,
					    &out->pco_space);
out_map_version:
	out->pco_op.po_map_version = pool_map_get_version(svc->ps_pool->sp_map);
	if (map_buf)
		D_FREE(map_buf);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (prop)
		daos_prop_free(prop);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pco_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pco_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pci_op.pi_uuid), rpc, DP_RC(rc));
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

	rc = bcast_create(ctx, svc, POOL_TGT_DISCONNECT, NULL, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tdi_uuid, svc->ps_uuid);
	in->tdi_hdls.ca_arrays = pool_hdls;
	in->tdi_hdls.ca_count = n_pool_hdls;
	rc = dss_rpc_send(rpc);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_DISCONNECT_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to disconnect from "DF_RC" targets\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_UUID": bcasted: "DF_RC"\n", DP_UUID(svc->ps_uuid),
		DP_RC(rc));
	return rc;
}

static int
pool_disconnect_hdls(struct rdb_tx *tx, struct pool_svc *svc, uuid_t *hdl_uuids,
		     int n_hdl_uuids, crt_context_t ctx)
{
	d_iov_t	value;
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

	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out, rc);

	nhandles -= n_hdl_uuids;

	for (i = 0; i < n_hdl_uuids; i++) {
		d_iov_t key;

		d_iov_set(&key, hdl_uuids[i], sizeof(uuid_t));
		rc = rdb_tx_delete(tx, &svc->ps_handles, &key);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out, rc);

out:
	D_DEBUG(DF_DSMS, DF_UUID": leaving: "DF_RC"\n", DP_UUID(svc->ps_uuid),
		DP_RC(rc));
	return rc;
}

void
ds_pool_disconnect_handler(crt_rpc_t *rpc)
{
	struct pool_disconnect_in      *pdi = crt_req_get(rpc);
	struct pool_disconnect_out     *pdo = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	struct rdb_tx			tx;
	d_iov_t			key;
	d_iov_t			value;
	struct pool_hdl			hdl;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(pdi->pdi_op.pi_uuid), rpc, DP_UUID(pdi->pdi_op.pi_hdl));

	rc = pool_svc_lookup_leader(pdi->pdi_op.pi_uuid, &svc,
				    &pdo->pdo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	d_iov_set(&key, pdi->pdi_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, &hdl, sizeof(hdl));
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
	ds_rsvc_set_hint(&svc->ps_rsvc, &pdo->pdo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	pdo->pdo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(pdi->pdi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

static int
pool_space_query_bcast(crt_context_t ctx, struct pool_svc *svc, uuid_t pool_hdl,
		       struct daos_pool_space *ps)
{
	struct pool_tgt_query_in	*in;
	struct pool_tgt_query_out	*out;
	crt_rpc_t			*rpc;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	rc = bcast_create(ctx, svc, POOL_TGT_QUERY, NULL, &rpc);
	if (rc != 0)
		goto out;

	in = crt_req_get(rpc);
	uuid_copy(in->tqi_op.pi_uuid, svc->ps_uuid);
	uuid_copy(in->tqi_op.pi_hdl, pool_hdl);
	rc = dss_rpc_send(rpc);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_QUERY_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	rc = out->tqo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to query from "DF_RC" targets\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		rc = -DER_IO;
	} else {
		D_ASSERT(ps != NULL);
		*ps = out->tqo_space;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DB_MD, DF_UUID": bcasted: "DF_RC"\n", DP_UUID(svc->ps_uuid),
		DP_RC(rc));
	return rc;
}

/*
 * Transfer list of containers to "remote_bulk". If the remote bulk buffer
 * is too small, then return -DER_TRUNC. RPC response will contain the number
 * of containers in the pool that the client can use to resize its buffer
 * for another RPC request.
 */
static int
transfer_cont_buf(struct daos_pool_cont_info *cont_buf, size_t ncont,
		  struct pool_svc *svc, crt_rpc_t *rpc, crt_bulk_t remote_bulk)
{
	size_t				 cont_buf_size;
	daos_size_t			 remote_bulk_size;
	d_iov_t				 cont_iov;
	d_sg_list_t			 cont_sgl;
	crt_bulk_t			 bulk = CRT_BULK_NULL;
	struct crt_bulk_desc		 bulk_desc;
	crt_bulk_opid_t			 bulk_opid;
	ABT_eventual			 eventual;
	int				*status;
	int				 rc;

	D_ASSERT(ncont > 0);
	cont_buf_size = ncont * sizeof(struct daos_pool_cont_info);

	/* Check if the client bulk buffer is large enough. */
	rc = crt_bulk_get_len(remote_bulk, &remote_bulk_size);
	if (rc != 0)
		D_GOTO(out, rc);
	if (remote_bulk_size < cont_buf_size) {
		D_ERROR(DF_UUID": remote container buffer("DF_U64")"
			" < required (%lu)\n", DP_UUID(svc->ps_uuid),
			remote_bulk_size, cont_buf_size);
		D_GOTO(out, rc = -DER_TRUNC);
	}

	d_iov_set(&cont_iov, cont_buf, cont_buf_size);
	cont_sgl.sg_nr = 1;
	cont_sgl.sg_nr_out = 0;
	cont_sgl.sg_iovs = &cont_iov;

	rc = crt_bulk_create(rpc->cr_ctx, &cont_sgl, CRT_BULK_RO, &bulk);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Prepare for crt_bulk_transfer(). */
	bulk_desc.bd_rpc = rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_PUT;
	bulk_desc.bd_remote_hdl = remote_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = bulk;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = cont_iov.iov_len;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_bulk, rc = dss_abterr2der(rc));

	rc = crt_bulk_transfer(&bulk_desc, bulk_cb, &eventual, &bulk_opid);
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
	if (bulk != CRT_BULK_NULL)
		crt_bulk_free(bulk);
out:
	return rc;
}

/**
 * Send CaRT RPC to pool svc to get container list.
 *
 * \param[in]	uuid		UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[out]	containers	Array of container information (allocated)
 * \param[out]	ncontainers	Number of items in containers
 *
 * return	0		Success
 *
 */
int
ds_pool_svc_list_cont(uuid_t uuid, d_rank_list_t *ranks,
		      struct daos_pool_cont_info **containers,
		      uint64_t *ncontainers)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_list_cont_in	*in;
	struct pool_list_cont_out	*out;
	uint64_t			resp_ncont = 1024;
	struct daos_pool_cont_info	*resp_cont = NULL;

	D_DEBUG(DB_MGMT, DF_UUID": Getting container list\n", DP_UUID(uuid));

	*containers = NULL;

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto out_client;
	}

realloc_resp:
	rc = pool_req_create(info->dmi_ctx, &ep, POOL_LIST_CONT, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool list cont rpc: %d\n",
			DP_UUID(uuid), rc);
		D_GOTO(out_client, rc);
	}

	/* Allocate response buffer */
	D_ALLOC_ARRAY(resp_cont, resp_ncont);
	if (resp_cont == NULL)
		D_GOTO(out_rpc, rc = -DER_NOMEM);

	in = crt_req_get(rpc);
	uuid_copy(in->plci_op.pi_uuid, uuid);
	uuid_clear(in->plci_op.pi_hdl);
	in->plci_ncont = resp_ncont;
	rc = list_cont_bulk_create(info->dmi_ctx, &in->plci_cont_bulk,
				   resp_cont, in->plci_ncont);
	if (rc != 0)
		D_GOTO(out_resp_buf, rc);

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->plco_op.po_rc,
				      &out->plco_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		/* To simplify logic, destroy bulk hdl and buffer each time */
		list_cont_bulk_destroy(in->plci_cont_bulk);
		D_FREE(resp_cont);
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->plco_op.po_rc;
	if (rc == -DER_TRUNC) {
		/* resp_ncont too small - realloc with server-provided ncont */
		resp_ncont = out->plco_ncont;
		list_cont_bulk_destroy(in->plci_cont_bulk);
		D_FREE(resp_cont);
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(realloc_resp, rc);
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to get container list for pool: %d\n",
			DP_UUID(uuid), rc);
	} else {
		*containers = resp_cont;
	}

	list_cont_bulk_destroy(in->plci_cont_bulk);
out_resp_buf:
	if (rc != 0)
		D_FREE(resp_cont);
out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

/* CaRT RPC handler for pool container listing
 * Requires a pool handle (except for rebuild).
 */
void
ds_pool_list_cont_handler(crt_rpc_t *rpc)
{
	struct pool_list_cont_in	*in = crt_req_get(rpc);
	struct pool_list_cont_out	*out = crt_reply_get(rpc);
	struct daos_pool_cont_info	*cont_buf = NULL;
	uint64_t			 ncont = 0;
	struct pool_svc			*svc;
	struct rdb_tx			 tx;
	d_iov_t				 key;
	d_iov_t				 value;
	struct pool_hdl			 hdl;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->plci_op.pi_uuid), rpc, DP_UUID(in->plci_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->plci_op.pi_uuid, &svc,
				    &out->plco_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Verify pool handle only if RPC initiated by a client
	 * (not for mgmt svc to pool svc RPCs that do not have a handle).
	 */
	if (daos_rpc_from_client(rpc)) {
		rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
		if (rc != 0)
			D_GOTO(out_svc, rc);

		ABT_rwlock_rdlock(svc->ps_lock);

		/* Verify the pool handle. Note: since rebuild will not
		 * connect the pool, so we only verify the non-rebuild
		 * pool.
		 */
		if (!is_pool_from_srv(in->plci_op.pi_uuid,
				      in->plci_op.pi_hdl)) {
			d_iov_set(&key, in->plci_op.pi_hdl, sizeof(uuid_t));
			d_iov_set(&value, &hdl, sizeof(hdl));
			rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
			if (rc == -DER_NONEXIST)
				rc = -DER_NO_HDL;
				/* defer goto out_svc until unlock/tx_end */
		}

		ABT_rwlock_unlock(svc->ps_lock);
		rdb_tx_end(&tx);
		if (rc != 0)
			D_GOTO(out_svc, rc);
	}

	/* Call container service to get the list */
	rc = ds_cont_list(in->plci_op.pi_uuid, &cont_buf, &ncont);
	if (rc != 0) {
		D_GOTO(out_svc, rc);
	} else if ((in->plci_ncont > 0) && (ncont > in->plci_ncont)) {
		/* Got a list, but client buffer not supplied or too small */
		D_DEBUG(DF_DSMS, DF_UUID": hdl="DF_UUID": has %"PRIu64
				 " containers (more than client: %"PRIu64")\n",
				 DP_UUID(in->plci_op.pi_uuid),
				 DP_UUID(in->plci_op.pi_hdl),
				 ncont, in->plci_ncont);
		D_GOTO(out_free_cont_buf, rc = -DER_TRUNC);
	} else {
		D_DEBUG(DF_DSMS, DF_UUID": hdl="DF_UUID": has %"PRIu64
				 " containers\n", DP_UUID(in->plci_op.pi_uuid),
				 DP_UUID(in->plci_op.pi_hdl), ncont);

		/* Send any results only if client provided a handle */
		if (cont_buf && (in->plci_ncont > 0) &&
		    (in->plci_cont_bulk != CRT_BULK_NULL)) {
			rc = transfer_cont_buf(cont_buf, ncont, svc, rpc,
					       in->plci_cont_bulk);
		}
	}

out_free_cont_buf:
	if (cont_buf) {
		D_FREE(cont_buf);
		cont_buf = NULL;
	}
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->plco_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->plco_op.po_rc = rc;
	out->plco_ncont = ncont;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->plci_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

void
ds_pool_query_handler(crt_rpc_t *rpc)
{
	struct pool_query_in   *in = crt_req_get(rpc);
	struct pool_query_out  *out = crt_reply_get(rpc);
	daos_prop_t	       *prop = NULL;
	struct pool_buf		*map_buf = NULL;
	uint32_t		map_version;
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	d_iov_t			key;
	d_iov_t			value;
	struct pool_hdl		hdl;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pqi_op.pi_uuid), rpc, DP_UUID(in->pqi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pqi_op.pi_uuid, &svc,
				    &out->pqo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	if (in->pqi_query_bits & DAOS_PO_QUERY_REBUILD_STATUS) {
		rc = ds_rebuild_query(in->pqi_op.pi_uuid, &out->pqo_rebuild_st);
		if (rc != 0)
			D_GOTO(out_svc, rc);
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->ps_lock);

	/* Verify the pool handle for client calls.
	 * Note: since rebuild will not connect the pool, so we only verify
	 * the non-rebuild pool. Server-to-server calls also don't have a
	 * handle.
	 */
	if (daos_rpc_from_client(rpc) &&
	    !is_pool_from_srv(in->pqi_op.pi_uuid, in->pqi_op.pi_hdl)) {
		d_iov_set(&key, in->pqi_op.pi_hdl, sizeof(uuid_t));
		d_iov_set(&value, &hdl, sizeof(hdl));
		rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
		if (rc != 0) {
			if (rc == -DER_NONEXIST)
				rc = -DER_NO_HDL;
			D_GOTO(out_lock, rc);
		}
	}

	/* read optional properties */
	rc = pool_prop_read(&tx, svc, in->pqi_query_bits, &prop);
	if (rc != 0)
		D_GOTO(out_map_version, rc);
	out->pqo_prop = prop;

	if (DAOS_FAIL_CHECK(DAOS_FORCE_PROP_VERIFY) && prop != NULL) {
		daos_prop_t		*iv_prop = NULL;
		struct daos_prop_entry	*entry, *iv_entry;
		int			i;

		D_ALLOC_PTR(iv_prop);
		if (iv_prop == NULL)
			D_GOTO(out_map_version, rc = -DER_NOMEM);

		rc = ds_pool_iv_prop_fetch(svc->ps_pool, iv_prop);
		if (rc) {
			D_ERROR("ds_pool_iv_prop_fetch failed "DF_RC"\n",
				DP_RC(rc));
			daos_prop_free(iv_prop);
			D_GOTO(out_map_version, rc);
		}

		for (i = 0; i < prop->dpp_nr; i++) {
			entry = &prop->dpp_entries[i];
			iv_entry = daos_prop_entry_get(iv_prop,
						       entry->dpe_type);
			D_ASSERT(iv_entry != NULL);
			switch (entry->dpe_type) {
			case DAOS_PROP_PO_LABEL:
				D_ASSERT(strlen(entry->dpe_str) <=
					 DAOS_PROP_LABEL_MAX_LEN);
				if (strncmp(entry->dpe_str, iv_entry->dpe_str,
					    DAOS_PROP_LABEL_MAX_LEN) != 0) {
					D_ERROR("mismatch %s - %s.\n",
						entry->dpe_str,
						iv_entry->dpe_str);
					rc = -DER_IO;
				}
				break;
			case DAOS_PROP_PO_OWNER:
			case DAOS_PROP_PO_OWNER_GROUP:
				D_ASSERT(strlen(entry->dpe_str) <=
					 DAOS_ACL_MAX_PRINCIPAL_LEN);
				if (strncmp(entry->dpe_str, iv_entry->dpe_str,
					    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN)
				    != 0) {
					D_ERROR("mismatch %s - %s.\n",
						entry->dpe_str,
						iv_entry->dpe_str);
					rc = -DER_IO;
				}
				break;
			case DAOS_PROP_PO_SPACE_RB:
			case DAOS_PROP_PO_SELF_HEAL:
			case DAOS_PROP_PO_RECLAIM:
				if (entry->dpe_val != iv_entry->dpe_val) {
					D_ERROR("type %d mismatch "DF_U64" - "
						DF_U64".\n", entry->dpe_type,
						entry->dpe_val,
						iv_entry->dpe_val);
					rc = -DER_IO;
				}
				break;
			case DAOS_PROP_PO_ACL:
				if (daos_prop_entry_cmp_acl(entry,
							    iv_entry) != 0)
					rc = -DER_IO;
				break;
			case DAOS_PROP_PO_SVC_LIST:
				break;
			default:
				D_ASSERTF(0, "bad dpe_type %d\n",
					  entry->dpe_type);
				break;
			};
		}
		daos_prop_free(iv_prop);
		if (rc) {
			D_ERROR("iv_prop verify failed "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_map_version, rc);
		}
	}

	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}

	rc = transfer_map_buf(map_buf, map_version, svc, rpc, in->pqi_map_bulk,
			      &out->pqo_map_buf_size);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

out_map_version:
	out->pqo_op.po_map_version = pool_map_get_version(svc->ps_pool->sp_map);
	if (map_buf)
		D_FREE(map_buf);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pqo_op.po_hint);
	/* See comment above, rebuild doesn't connect the pool */
	if (rc == 0 && (in->pqi_query_bits & DAOS_PO_QUERY_SPACE) &&
	    !is_pool_from_srv(in->pqi_op.pi_uuid, in->pqi_op.pi_hdl))
		rc = pool_space_query_bcast(rpc->cr_ctx, svc, in->pqi_op.pi_hdl,
					    &out->pqo_space);
	pool_svc_put_leader(svc);
out:
	out->pqo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pqi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

/* Convert pool_comp_state_t to daos_target_state_t */
static daos_target_state_t
enum_pool_comp_state_to_tgt_state(int tgt_state)
{

	switch (tgt_state) {
	case PO_COMP_ST_UNKNOWN: return DAOS_TS_UNKNOWN;
	case PO_COMP_ST_NEW: return DAOS_TS_NEW;
	case PO_COMP_ST_UP: return DAOS_TS_UP;
	case PO_COMP_ST_UPIN: return DAOS_TS_UP_IN;
	case PO_COMP_ST_DOWN: return  DAOS_TS_DOWN;
	case PO_COMP_ST_DOWNOUT: return DAOS_TS_DOWN_OUT;
	case PO_COMP_ST_DRAIN: return DAOS_TS_DRAIN;
	}

	return DAOS_TS_UNKNOWN;
}

void
ds_pool_query_info_handler(crt_rpc_t *rpc)
{
	struct pool_query_info_in	*in = crt_req_get(rpc);
	struct pool_query_info_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct pool_target		*target = NULL;
	int				 tgt_state;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pqii_op.pi_uuid), rpc, DP_UUID(in->pqii_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pqii_op.pi_uuid, &svc,
				    &out->pqio_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	/* get the target state from pool map */
	ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
	rc = pool_map_find_target_by_rank_idx(svc->ps_pool->sp_map,
					      in->pqii_rank,
					      in->pqii_tgt,
					      &target);
	if (rc != 1) {
		D_ERROR(DF_UUID": Failed to get rank:%u, idx:%d\n, rc:%d",
			DP_UUID(in->pqii_op.pi_uuid), in->pqii_rank,
			in->pqii_tgt, rc);
		D_GOTO(out, rc = -DER_NONEXIST);
	 } else {
		rc = 0;
	}

	D_ASSERT(target != NULL);

	tgt_state = target->ta_comp.co_status;
	out->pqio_state = enum_pool_comp_state_to_tgt_state(tgt_state);

	/**
	 * TODO (DAOS-3625): Send pool tgt query RPC (server->server) to
	 * return pool target space info (including fragmentation).
	 */

	ABT_rwlock_unlock(svc->ps_pool->sp_lock);
	pool_svc_put_leader(svc);
out:
	out->pqio_op.po_rc = rc;
	out->pqio_rank = in->pqii_rank;
	out->pqio_tgt = in->pqii_tgt;

	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pqii_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

static int
process_query_result(daos_pool_info_t *info, uuid_t pool_uuid,
		     uint32_t map_version, uint32_t leader_rank,
		     struct daos_pool_space *ps,
		     struct daos_rebuild_status *rs,
		     struct pool_buf *map_buf)
{
	struct pool_map	       *map;
	int			rc;
	unsigned int		num_disabled = 0;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: %d\n", rc);
		return rc;
	}

	rc = pool_map_find_failed_tgts(map, NULL, &num_disabled);
	if (rc != 0) {
		D_ERROR("failed to get num disabled tgts, rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	info->pi_ndisabled = num_disabled;

	pool_query_reply_to_info(pool_uuid, map_buf, map_version, leader_rank,
				 ps, rs, info);

out:
	pool_map_decref(map);
	return rc;
}

/**
 * Query the pool without holding a pool handle.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Ranks of pool svc replicas
 * \param[out]	pool_info	Results of the pool query
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		Negative value	Error
 */
int
ds_pool_svc_query(uuid_t pool_uuid, d_rank_list_t *ranks,
		  daos_pool_info_t *pool_info)
{
	int			rc;
	struct rsvc_client	client;
	crt_endpoint_t		ep;
	struct dss_module_info	*info = dss_get_module_info();
	crt_rpc_t		*rpc;
	struct pool_query_in	*in;
	struct pool_query_out	*out;
	struct pool_buf		*map_buf;
	uint32_t		map_size = 0;

	if (ranks == NULL || pool_info == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DB_MGMT, DF_UUID": Querying pool\n", DP_UUID(pool_uuid));

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

realloc:
	rc = pool_req_create(info->dmi_ctx, &ep, POOL_QUERY, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool query rpc: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pqi_op.pi_uuid, pool_uuid);
	uuid_clear(in->pqi_op.pi_hdl);
	in->pqi_query_bits = pool_query_bits(pool_info, NULL);

	rc = map_bulk_create(info->dmi_ctx, &in->pqi_map_bulk, &map_buf,
			     map_size);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->pqo_op.po_rc,
				      &out->pqo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		map_bulk_destroy(in->pqi_map_bulk, map_buf);
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->pqo_op.po_rc;
	if (rc == -DER_TRUNC) {
		map_size = out->pqo_map_buf_size;
		map_bulk_destroy(in->pqi_map_bulk, map_buf);
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(realloc, rc);
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to query pool: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_bulk, rc);
	}

	D_DEBUG(DB_MGMT, "Successfully queried pool\n");

	rc = process_query_result(pool_info, pool_uuid,
					 out->pqo_op.po_map_version,
					 out->pqo_op.po_hint.sh_rank,
					 &out->pqo_space,
					 &out->pqo_rebuild_st,
					 map_buf);
	if (rc != 0)
		D_ERROR("Failed to process pool query results, rc=%d\n", rc);

out_bulk:
	map_bulk_destroy(in->pqi_map_bulk, map_buf);
out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

/**
 * Query a pool's properties without having a handle for the pool
 */
void
ds_pool_prop_get_handler(crt_rpc_t *rpc)
{
	struct pool_prop_get_in		*in = crt_req_get(rpc);
	struct pool_prop_get_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct rdb_tx			tx;
	int				rc;
	daos_prop_t			*prop = NULL;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pgi_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->pgi_op.pi_uuid, &svc,
				    &out->pgo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->ps_lock);

	rc = pool_prop_read(&tx, svc, in->pgi_query_bits, &prop);
	if (rc != 0)
		D_GOTO(out_lock, rc);
	out->pgo_prop = prop;

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pgo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pgo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pgi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
	daos_prop_free(prop);
}

/**
 * Send a CaRT message to the pool svc to get the ACL pool property.
 *
 * \param[in]		pool_uuid	UUID of the pool
 * \param[in]		ranks		Pool service replicas
 * \param[in][out]	prop		Prop with requested properties, to be
 *					filled out and returned.
 *
 * \return	0		Success
 *
 */
int
ds_pool_svc_get_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
		     daos_prop_t *prop)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_prop_get_in		*in;
	struct pool_prop_get_out	*out;

	D_DEBUG(DB_MGMT, DF_UUID": Getting prop\n", DP_UUID(pool_uuid));

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_PROP_GET, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool get prop rpc: "
			""DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pgi_op.pi_uuid, pool_uuid);
	uuid_clear(in->pgi_op.pi_hdl);
	in->pgi_query_bits = pool_query_bits(NULL, prop);

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->pgo_op.po_rc,
				      &out->pgo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->pgo_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to get prop for pool: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_rpc, rc);
	}

	rc = daos_prop_copy(prop, out->pgo_prop);

out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

int
ds_pool_extend(uuid_t pool_uuid, int ntargets, uuid_t target_uuids[],
	       const d_rank_list_t *rank_list, int ndomains,
	       const int *domains, d_rank_list_t *svc_ranks)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_extend_in		*in;
	struct pool_extend_out		*out;

	rc = rsvc_client_init(&client, svc_ranks);
	if (rc != 0)
		return rc;

rechoose:

	ep.ep_grp = NULL; /* primary group */
	rsvc_client_choose(&client, &ep);

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_EXTEND, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool extend rpc: "
			""DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pei_op.pi_uuid, pool_uuid);
	in->pei_ntgts = ntargets;
	in->pei_ndomains = ndomains;
	in->pei_tgt_uuids.ca_count = rank_list->rl_nr;
	in->pei_tgt_uuids.ca_arrays = target_uuids;
	in->pei_tgt_ranks = (d_rank_list_t *)rank_list;
	in->pei_domains.ca_count = ndomains;
	in->pei_domains.ca_arrays = (int *)domains;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
		out->peo_op.po_rc, &out->peo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->peo_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to set targets to UP state for "
				"reintegration: "DF_RC"\n", DP_UUID(pool_uuid),
				DP_RC(rc));
		D_GOTO(out_rpc, rc);
	}

out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
	return rc;
}

int
ds_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *ranks,
		uint32_t rank, struct pool_target_id_list *target_list,
		pool_comp_state_t state)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_target_addr_list	list;
	struct pool_add_in		*in;
	struct pool_add_out		*out;
	crt_opcode_t			opcode;
	int i = 0;

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		return rc;

rechoose:

	ep.ep_grp = NULL; /* primary group */
	rsvc_client_choose(&client, &ep);

	switch (state) {
	case PO_COMP_ST_DOWN:
		opcode = POOL_EXCLUDE;
		break;
	case PO_COMP_ST_UP:
		opcode = POOL_REINT;
		break;
	case PO_COMP_ST_DRAIN:
		opcode = POOL_DRAIN;
		break;
	default:
		D_GOTO(out_client, rc = -DER_INVAL);
	}

	rc = pool_req_create(info->dmi_ctx, &ep, opcode, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool req: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pti_op.pi_uuid, pool_uuid);

	rc = pool_target_addr_list_alloc(target_list->pti_number, &list);
	if (rc) {
		D_ERROR(DF_UUID": pool_target_addr_list_alloc failed, rc %d.\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_rpc, rc);
	}

	/* pool_update rpc requires an addr list. */
	for (i = 0; i < target_list->pti_number; i++) {
		list.pta_addrs[i].pta_target = target_list->pti_ids[i].pti_id;
		list.pta_addrs[i].pta_rank = rank;
	}

	in->pti_addr_list.ca_arrays = list.pta_addrs;
	in->pti_addr_list.ca_count = (size_t)list.pta_number;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
		out->pto_op.po_rc, &out->pto_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->pto_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to set targets to %s state: "DF_RC"\n",
			DP_UUID(pool_uuid),
			state == PO_COMP_ST_DOWN ? "DOWN" :
			state == PO_COMP_ST_UP ? "UP" : "UNKNOWN",
			DP_RC(rc));
		D_GOTO(out_rpc, rc);
	}

out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
	return rc;
}

/**
 * Set a pool's properties without having a handle for the pool
 */
void
ds_pool_prop_set_handler(crt_rpc_t *rpc)
{
	struct pool_prop_set_in		*in = crt_req_get(rpc);
	struct pool_prop_set_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct rdb_tx			tx;
	daos_prop_t			*prop = NULL;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->psi_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->psi_op.pi_uuid, &svc,
				    &out->pso_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_prop_write(&tx, &svc->ps_root, in->psi_prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to write prop for pool: %d\n",
			DP_UUID(in->psi_op.pi_uuid), rc);
		D_GOTO(out_lock, rc);
	}

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	/* Read all props & update prop IV */
	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ALL, &prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read prop for pool, rc=%d\n",
			DP_UUID(in->psi_op.pi_uuid), rc);
		D_GOTO(out_lock, rc);
	}
	D_ASSERT(prop != NULL);

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	/*
	 * TODO: Introduce prop version to avoid inconsistent prop over targets
	 *	 caused by the out of order IV sync.
	 */
	if (!rc && prop != NULL) {
		rc = ds_pool_iv_prop_update(svc->ps_pool, prop);
		if (rc)
			D_ERROR(DF_UUID": failed to update prop IV for pool, "
				"%d.\n", DP_UUID(in->psi_op.pi_uuid), rc);
	}
	daos_prop_free(prop);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pso_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pso_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->psi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

/**
 * Send a CaRT message to the pool svc to set the requested pool properties.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	prop		Pool prop
 *
 * \return	0		Success
 *
 */
int
ds_pool_svc_set_prop(uuid_t pool_uuid, d_rank_list_t *ranks, daos_prop_t *prop)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_prop_set_in		*in;
	struct pool_prop_set_out	*out;

	D_DEBUG(DB_MGMT, DF_UUID": Setting pool prop\n", DP_UUID(pool_uuid));

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to init rsvc client: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_PROP_SET, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool set prop rpc: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->psi_op.pi_uuid, pool_uuid);
	uuid_clear(in->psi_op.pi_hdl);
	in->psi_prop = prop;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->pso_op.po_rc,
				      &out->pso_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->pso_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to set prop for pool: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_rpc, rc);
	}

out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

/*
 * Adds the contents of new_acl to the original ACL. If an entry is added for
 * a principal already in the ACL, the old entry will be replaced.
 * *acl may be reallocated in the process.
 */
static int
merge_acl(struct daos_acl **acl, struct daos_acl *new_acl)
{
	struct daos_ace	*new_ace;
	int		rc = 0;

	new_ace = daos_acl_get_next_ace(new_acl, NULL);
	while (new_ace != NULL) {
		rc = daos_acl_add_ace(acl, new_ace);
		if (rc != 0)
			break;
		new_ace = daos_acl_get_next_ace(new_acl, new_ace);
	}

	return rc;
}

/**
 * Update entries in a pool's ACL without having a handle for the pool
 */
void
ds_pool_acl_update_handler(crt_rpc_t *rpc)
{
	struct pool_acl_update_in	*in = crt_req_get(rpc);
	struct pool_acl_update_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct rdb_tx			tx;
	int				rc;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry = NULL;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pui_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->pui_op.pi_uuid, &svc,
				    &out->puo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	/*
	 * We need to read the old ACL, modify, and rewrite it
	 */
	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ACL, &prop);
	if (rc != 0)
		/* Prop might be allocated and returned even if rc != 0 */
		D_GOTO(out_prop, rc);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
	if (entry == NULL) {
		D_ERROR(DF_UUID": No ACL prop entry for pool\n",
			DP_UUID(in->pui_op.pi_uuid));
		D_GOTO(out_prop, rc);
	}

	rc = merge_acl((struct daos_acl **)&entry->dpe_val_ptr, in->pui_acl);
	if (rc != 0) {
		D_ERROR(DF_UUID": Unable to update pool with new ACL, rc=%d\n",
			DP_UUID(in->pui_op.pi_uuid), rc);
		D_GOTO(out_prop, rc);
	}

	rc = pool_prop_write(&tx, &svc->ps_root, prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to write updated ACL for pool: %d\n",
			DP_UUID(in->pui_op.pi_uuid), rc);
		D_GOTO(out_prop, rc);
	}

	rc = rdb_tx_commit(&tx);

out_prop:
	if (prop != NULL)
		daos_prop_free(prop);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->puo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->puo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pui_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

/**
 * Send a CaRT message to the pool svc to update the pool ACL by adding and
 * updating entries.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	acl		ACL to merge with the current pool ACL
 *
 * \return	0		Success
 *
 */
int
ds_pool_svc_update_acl(uuid_t pool_uuid, d_rank_list_t *ranks,
		       struct daos_acl *acl)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_acl_update_in	*in;
	struct pool_acl_update_out	*out;

	D_DEBUG(DB_MGMT, DF_UUID": Updating pool ACL\n", DP_UUID(pool_uuid));

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_ACL_UPDATE, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool update ACL rpc: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pui_op.pi_uuid, pool_uuid);
	uuid_clear(in->pui_op.pi_hdl);
	in->pui_acl = acl;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->puo_op.po_rc,
				      &out->puo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->puo_op.po_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to update ACL for pool: %d\n",
			DP_UUID(pool_uuid), rc);

	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

/**
 * Delete entries in a pool's ACL without having a handle for the pool
 */
void
ds_pool_acl_delete_handler(crt_rpc_t *rpc)
{
	struct pool_acl_delete_in	*in = crt_req_get(rpc);
	struct pool_acl_delete_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct rdb_tx			tx;
	int				rc;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pdi_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->pdi_op.pi_uuid, &svc,
				    &out->pdo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	/*
	 * We need to read the old ACL, modify, and rewrite it
	 */
	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ACL, &prop);
	if (rc != 0)
		/* Prop might be allocated and returned even if rc != 0 */
		D_GOTO(out_prop, rc);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
	if (entry == NULL) {
		D_ERROR(DF_UUID": No ACL prop entry for pool\n",
			DP_UUID(in->pdi_op.pi_uuid));
		D_GOTO(out_prop, rc);
	}

	rc = daos_acl_remove_ace((struct daos_acl **)&entry->dpe_val_ptr,
				 in->pdi_type, in->pdi_principal);
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to remove requested principal, "
			"rc=%d\n", DP_UUID(in->pdi_op.pi_uuid), rc);
		D_GOTO(out_prop, rc);
	}

	rc = pool_prop_write(&tx, &svc->ps_root, prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to write updated ACL for pool: %d\n",
			DP_UUID(in->pdi_op.pi_uuid), rc);
		D_GOTO(out_prop, rc);
	}

	rc = rdb_tx_commit(&tx);

out_prop:
	if (prop != NULL)
		daos_prop_free(prop);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pdo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pdo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d\n",
		DP_UUID(in->pdi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

/**
 * Send a CaRT message to the pool svc to remove an entry by principal from the
 * pool's ACL.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	principal_type	Type of the principal to be removed
 * \param[in]	principal_name	Name of the principal to be removed
 *
 * \return	0		Success
 *
 */
int
ds_pool_svc_delete_acl(uuid_t pool_uuid, d_rank_list_t *ranks,
		       enum daos_acl_principal_type principal_type,
		       const char *principal_name)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_acl_delete_in	*in;
	struct pool_acl_delete_out	*out;
	char				*name_buf = NULL;
	size_t				name_buf_len;

	D_DEBUG(DB_MGMT, DF_UUID": Deleting entry from pool ACL\n",
		DP_UUID(pool_uuid));

	if (principal_name != NULL) {
		/* Need to sanitize the incoming string */
		name_buf_len = DAOS_ACL_MAX_PRINCIPAL_BUF_LEN;
		D_ALLOC_ARRAY(name_buf, name_buf_len);
		if (name_buf == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		/* force null terminator in copy */
		strncpy(name_buf, principal_name, name_buf_len - 1);
	}

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_ACL_DELETE, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool delete ACL rpc: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pdi_op.pi_uuid, pool_uuid);
	uuid_clear(in->pdi_op.pi_hdl);
	in->pdi_type = (uint8_t)principal_type;
	in->pdi_principal = name_buf;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->pdo_op.po_rc,
				      &out->pdo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->pdo_op.po_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to delete ACL entry for pool: %d\n",
			DP_UUID(pool_uuid), rc);

	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	D_FREE(name_buf);
	return rc;
}

static int
replace_failed_replicas(struct pool_svc *svc, struct pool_map *map)
{
	d_rank_list_t	*replicas;
	d_rank_list_t	*tmp_replicas;
	d_rank_list_t	 failed_ranks;
	d_rank_list_t	 replace_ranks;
	int		 rc;

	rc = rdb_get_ranks(svc->ps_rsvc.s_db, &replicas);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = ds_pool_check_failed_replicas(map, replicas, &failed_ranks,
					   &replace_ranks);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": cannot replace failed replicas: "
			""DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}
	if (replace_ranks.rl_nr > 0)
		ds_rsvc_add_replicas_s(&svc->ps_rsvc, &replace_ranks,
				       ds_rsvc_get_md_cap());
	if (failed_ranks.rl_nr > 0)
		ds_rsvc_remove_replicas_s(&svc->ps_rsvc, &failed_ranks);
	/** `replace_ranks.rl_ranks` is not allocated and shouldn't be freed **/
	D_FREE(failed_ranks.rl_ranks);

	if (rdb_get_ranks(svc->ps_rsvc.s_db, &tmp_replicas) == 0) {
		daos_rank_list_sort(replicas);
		daos_rank_list_sort(tmp_replicas);
		if (!daos_rank_list_identical(replicas, tmp_replicas))
			D_DEBUG(DB_MD, DF_UUID": failed to update replicas\n",
				DP_UUID(svc->ps_uuid));
		d_rank_list_free(tmp_replicas);
	}
	d_rank_list_free(replicas);
out:
	return rc;
}

/* Callers are responsible for d_rank_list_free(*replicasp). */
static int
ds_pool_update_internal(uuid_t pool_uuid, struct pool_target_id_list *tgts,
			unsigned int opc, struct rsvc_hint *hint,
			bool *p_updated, bool evict_rank,
			uint32_t *map_version_p, uint32_t *tgt_map_ver)
{
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	struct pool_map	       *map = NULL;
	uint32_t		map_version_before;
	uint32_t		map_version = 0;
	struct pool_buf	       *map_buf = NULL;
	bool			updated = false;
	int			rc;

	rc = pool_svc_lookup_leader(pool_uuid, &svc, hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	ABT_rwlock_wrlock(svc->ps_lock);

	/* Create a temporary pool map based on the last committed version. */
	rc = read_map(&tx, &svc->ps_root, &map);
	if (rc != 0)
		D_GOTO(out_map, rc);

	/*
	 * Attempt to modify the temporary pool map and save its versions
	 * before and after. If the version hasn't changed, we are done.
	 */
	map_version_before = pool_map_get_version(map);
	rc = ds_pool_map_tgts_update(map, tgts, opc, evict_rank, tgt_map_ver);
	if (rc != 0)
		D_GOTO(out_map, rc);

	map_version = pool_map_get_version(map);

	D_DEBUG(DF_DSMS, DF_UUID": version=%u->%u\n",
		DP_UUID(svc->ps_uuid), map_version_before, map_version);
	if (map_version == map_version_before)
		D_GOTO(out_map, rc = 0);

	/* Write the new pool map. */
	rc = pool_buf_extract(map, &map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);
	rc = write_map_buf(&tx, &svc->ps_root, map_buf, map_version);
	if (rc != 0)
		D_GOTO(out_map, rc);

	rc = rdb_tx_commit(&tx);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": failed to commit: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_map, rc);
	}

	updated = true;

	/* Update svc->ps_pool to match the new pool map. */
	rc = ds_pool_tgt_map_update(svc->ps_pool, map_buf, map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to update pool map cache: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		/*
		 * We must resign to avoid handling future requests with a
		 * stale pool map cache.
		 */
		rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
		rc = 0;
		goto out_map;
	}

	ds_rsvc_request_map_dist(&svc->ps_rsvc);

	replace_failed_replicas(svc, map);

	if (opc == POOL_ADD_IN) {
		/*
		 * If we are setting ranks from UP -> UPIN, schedule a reclaim
		 * operation to garbage collect any unreachable data moved
		 * during reintegration/addition
		 */
		rc = ds_rebuild_schedule(pool_uuid, map_version, tgts,
					 RB_OP_RECLAIM);
		if (rc != 0) {
			D_ERROR("failed to schedule reclaim rc: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

out_map:
	if (map_version_p != NULL)
		*map_version_p = pool_map_get_version((map == NULL || rc != 0) ?
						      svc->ps_pool->sp_map :
						      map);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (map)
		pool_map_decref(map);

	if (map_buf != NULL)
		pool_buf_free(map_buf);
out_svc:
	if (hint != NULL)
		ds_rsvc_set_hint(&svc->ps_rsvc, hint);
	pool_svc_put_leader(svc);
out:
	if (p_updated)
		*p_updated = updated;
	return rc;
}

static int
pool_find_all_targets_by_addr(uuid_t pool_uuid,
			      struct pool_target_addr_list *list,
			      struct pool_target_id_list *tgt_list,
			      struct pool_target_addr_list *out_list,
			      struct rsvc_hint *hint)
{
	struct pool_svc	*svc;
	struct rdb_tx	tx;
	struct pool_map *map = NULL;
	int		i;
	int		rc;

	rc = pool_svc_lookup_leader(pool_uuid, &svc, hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	ABT_rwlock_rdlock(svc->ps_lock);

	/* Create a temporary pool map based on the last committed version. */
	rc = read_map(&tx, &svc->ps_root, &map);

	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	for (i = 0; i < list->pta_number; i++) {
		struct pool_target *tgt;
		int tgt_nr;
		int j;
		int ret;

		tgt_nr = pool_map_find_target_by_rank_idx(map,
				list->pta_addrs[i].pta_rank,
				list->pta_addrs[i].pta_target, &tgt);
		if (tgt_nr <= 0) {
			/* Can not locate the target in pool map, let's
			 * add it to the output list
			 */
			D_WARN("Can not find %u/%d , add to out_list\n",
				list->pta_addrs[i].pta_rank,
				(int)list->pta_addrs[i].pta_target);
			ret = pool_target_addr_list_append(out_list,
						&list->pta_addrs[i]);
			if (ret) {
				rc = ret;
				break;
			}
		}

		for (j = 0; j < tgt_nr; j++) {
			struct pool_target_id tid;

			tid.pti_id = tgt[j].ta_comp.co_id;
			ret = pool_target_id_list_append(tgt_list, &tid);
			if (ret) {
				rc = ret;
				break;
			}
		}
	}
out_svc:
	pool_svc_put_leader(svc);
out:
	if (map != NULL)
		pool_map_decref(map);
	return rc;
}

struct redist_open_hdls_arg {
	/**
	 * Pointer to pointer containing flattened array of output handles
	 * Note that these are variable size, so can't be indexed as an array
	 */
	struct pool_iv_conn **hdls;
	/** Pointer to the next write location within hdls */
	struct pool_iv_conn *next;
	/** Total current size of the hdls buffer, in bytes */
	size_t hdls_size;
	/** Total used space in hdls buffer, in bytes */
	size_t hdls_used;
};

static int
get_open_handles_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct redist_open_hdls_arg *arg = varg;
	uuid_t *uuid = key->iov_buf;
	struct pool_hdl *hdl = val->iov_buf;
	struct ds_pool_hdl *lookup_hdl;
	size_t size_needed;
	int rc = DER_SUCCESS;

	if (key->iov_len != sizeof(uuid_t) ||
	    val->iov_len != sizeof(struct pool_hdl)) {
		D_ERROR("invalid key/value size: key="DF_U64" value="DF_U64"\n",
			key->iov_len, val->iov_len);
		return -DER_IO;
	}

	/* Look up the handle in the local pool to obtain the creds, which are
	 * not stored in RDB
	 */
	lookup_hdl = ds_pool_hdl_lookup(*uuid);
	if (lookup_hdl == NULL) {
		D_ERROR("Pool open handle "DF_UUID" is in RDB but not the pool",
			DP_UUID(*uuid));
		return -DER_NONEXIST;
	}

	/* Check if there's enough room is the preallocated array, and expand
	 * if not
	 */
	size_needed = arg->hdls_used + lookup_hdl->sph_cred.iov_buf_len +
		sizeof(struct pool_iv_conn);
	if (size_needed > arg->hdls_size) {
		void *newbuf = NULL;

		D_REALLOC(newbuf, *arg->hdls, size_needed);
		if (newbuf == NULL)
			D_GOTO(out_hdl, rc = -DER_NOMEM);

		/* Since this probably changed the hdls pointer, adjust the
		 * next pointer correspondingly
		 */
		*(arg->hdls) = newbuf;
		arg->next = (struct pool_iv_conn *)
			(((char *)*arg->hdls) + arg->hdls_used);
		arg->hdls_size = size_needed;
	}

	/* Copy the data */
	uuid_copy(arg->next->pic_hdl, *uuid);
	arg->next->pic_flags = hdl->ph_flags;
	arg->next->pic_capas = hdl->ph_sec_capas;
	arg->next->pic_cred_size = lookup_hdl->sph_cred.iov_buf_len;
	memcpy(arg->next->pic_creds, lookup_hdl->sph_cred.iov_buf,
	       lookup_hdl->sph_cred.iov_buf_len);

	/* Adjust the pointers for the next iteration */
	arg->hdls_used = size_needed;
	arg->next = (struct pool_iv_conn *)
		(((char *)*arg->hdls) + arg->hdls_used);

out_hdl:
	ds_pool_hdl_put(lookup_hdl);

	return DER_SUCCESS;
}

/**
 * Retrieves a flat buffer containing all currently open handles
 *
 * \param pool_uuid [IN]  The pool to get handles for
 * \param hdls      [OUT] A flat-packed buffer of all open handles
 *                        (struct pool_iv_conn). Caller must free hdls->iov_buf.
 *                        Note that these are variable size, and can not be
 *                        indexed like an array
 * \param out_size  [OUT] The size of the buffer pointed to by hdls
 *
 * \return If no handles are currently open this will return DER_SUCCESS with
 *         hdls = NULL and out_size = 0.
 *         Otherwise returns DER_SUCCESS or an -error code
 */
int
ds_pool_get_open_handles(uuid_t pool_uuid, d_iov_t *hdls)
{
	struct pool_svc			*svc;
	struct redist_open_hdls_arg	 arg;
	uint32_t			 connectable;
	struct rdb_tx			 tx;
	d_iov_t				 value;
	uint32_t			 nhandles;
	int				 rc;

	d_iov_set(hdls, NULL, 0);

	rc = pool_svc_lookup_leader(pool_uuid, &svc, NULL /* hint */);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->ps_lock);

	/* Check if pool is being destroyed and not accepting connections */
	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_lookup(&tx, &svc->ps_root,
			   &ds_pool_prop_connectable, &value);
	if (rc != 0)
		D_GOTO(out_lock, rc);
	if (!connectable) {
		D_ERROR(DF_UUID": being destroyed, not accepting connections\n",
			DP_UUID(pool_uuid));
		D_GOTO(out_lock, rc = -DER_BUSY);
	}

	/* Check how many handles are currently open */
	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	/* Abort early if there are no open handles */
	if (nhandles == 0)
		D_GOTO(out_lock, rc);

	/* Preallocate an approximate amount of space for the handles and the
	 * variable-size creds field which is not accounted for in the size of
	 * the base structure. The goal here isn't to be exactly right - can't
	 * know at this point how much space is actually needed. Ballparking
	 * close enough just reduces the number of reallocations needed during
	 * iteration
	 */
	D_ALLOC(hdls->iov_buf, nhandles * (sizeof(struct pool_iv_conn) + 160));
	if (hdls->iov_buf == NULL)
		D_GOTO(out_lock, rc = -DER_NOMEM);

	/* Pass in the preallocated array and handles as pointers
	 * This allows the iterator to reallocate the array if an element
	 * was added between when we retrieved the size and iteration completes
	 */
	arg.hdls = (struct pool_iv_conn **)&hdls->iov_buf;
	arg.next = *arg.hdls;
	arg.hdls_size = nhandles * (sizeof(struct pool_iv_conn) + 128);
	arg.hdls_used = 0;

	/* Iterate the open handles and accumulate their UUIDs */
	rc = rdb_tx_iterate(&tx, &svc->ps_handles, false /* backward */,
			    get_open_handles_cb, &arg);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	hdls->iov_buf_len = hdls->iov_len = arg.hdls_used;

	D_DEBUG(DF_DSMS, DF_UUID": packed %u handles into %zu bytes\n",
		DP_UUID(pool_uuid), nhandles, arg.hdls_used);

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);

out_svc:
	pool_svc_put_leader(svc);
	return rc;
}

int
ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return ds_pool_update_internal(pool_uuid, list, POOL_EXCLUDE_OUT,
				       NULL, NULL, false, NULL, NULL);
}

int
ds_pool_tgt_exclude(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return ds_pool_update_internal(pool_uuid, list, POOL_EXCLUDE,
				       NULL, NULL, false, NULL, NULL);
}

int
ds_pool_tgt_add_in(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return ds_pool_update_internal(pool_uuid, list, POOL_ADD_IN,
				       NULL, NULL, false, NULL, NULL);
}

/*
 * Perform a pool map update indicated by opc. If successful, the new pool map
 * version is reported via map_version. Upon -DER_NOTLEADER, a pool service
 * leader hint, if available, is reported via hint (if not NULL).
 */
static int
ds_pool_update(uuid_t pool_uuid, crt_opcode_t opc,
	       struct pool_target_addr_list *list,
	       struct pool_target_addr_list *out_list,
	       uint32_t *map_version, struct rsvc_hint *hint, bool evict_rank)
{
	daos_rebuild_opc_t		op;
	struct pool_target_id_list	target_list = { 0 };
	struct ds_pool			*pool = NULL;
	daos_prop_t			prop = { 0 };
	uint32_t			tgt_map_ver = 0;
	struct daos_prop_entry		*entry;
	bool				updated;
	int				rc;
	char				*env;

	rc = pool_find_all_targets_by_addr(pool_uuid, list, &target_list,
					   out_list, hint);
	if (rc)
		D_GOTO(out, rc);

	/* Update target by target id */
	rc = ds_pool_update_internal(pool_uuid, &target_list, opc, hint,
				     &updated, evict_rank, map_version,
				     &tgt_map_ver);
	if (rc)
		D_GOTO(out, rc);

	if (!updated)
		D_GOTO(out, rc);

	switch (opc) {
	case POOL_EXCLUDE:
		op = RB_OP_FAIL;
		break;
	case POOL_DRAIN:
		op = RB_OP_DRAIN;
		break;
	case POOL_REINT:
		op = RB_OP_REINT;
		break;
	case POOL_EXTEND:
		op = RB_OP_EXTEND;
		break;
	default:
		D_GOTO(out, rc);
	}

	env = getenv(REBUILD_ENV);
	if ((env && !strcasecmp(env, REBUILD_ENV_DISABLED)) ||
	     daos_fail_check(DAOS_REBUILD_DISABLE)) {
		D_DEBUG(DB_TRACE, "Rebuild is disabled\n");
		D_GOTO(out, rc = 0);
	}

	pool = ds_pool_lookup(pool_uuid);
	D_ASSERT(pool != NULL);
	rc = ds_pool_iv_prop_fetch(pool, &prop);
	if (rc)
		D_GOTO(out, rc);

	entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SELF_HEAL);
	D_ASSERT(entry != NULL);
	if (!(entry->dpe_val & DAOS_SELF_HEAL_AUTO_REBUILD)) {
		D_DEBUG(DB_MGMT, "self healing is disabled\n");
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMS, "map ver %u/%u\n", map_version ? *map_version : -1,
		tgt_map_ver);
	if (tgt_map_ver != 0) {
		rc = ds_rebuild_schedule(pool_uuid, tgt_map_ver, &target_list,
					 op);
		if (rc != 0) {
			D_ERROR("rebuild fails rc: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

out:
	if (pool)
		ds_pool_put(pool);
	daos_prop_fini(&prop);
	pool_target_id_list_free(&target_list);
	return rc;
}

/*
 * Currently can only add racks/top level domains. There's not currently
 * any way to specify fault domain at a better level
 */
static int
pool_extend_map(struct rdb_tx *tx, struct pool_svc *svc,
		uint32_t nnodes, uuid_t target_uuids[],
		d_rank_list_t *rank_list, uint32_t ndomains,
		int32_t *domains, bool *updated_p, uint32_t *map_version_p,
		struct rsvc_hint *hint)
{
	struct pool_buf		*map_buf = NULL;
	struct pool_map		*map = NULL;
	uint32_t		map_version;
	bool			updated = false;
	int			ntargets;
	int			rc;

	ntargets = nnodes * dss_tgt_nr;

	/* Create a temporary pool map based on the last committed version. */
	rc = read_map(tx, &svc->ps_root, &map);
	if (rc != 0)
		return rc;

	map_version = pool_map_get_version(map) + 1;

	rc = gen_pool_buf(map, &map_buf, map_version, ndomains, nnodes,
			ntargets, domains, target_uuids, rank_list, NULL,
			dss_tgt_nr);
	if (rc != 0)
		D_GOTO(out_map_buf, rc);

	/* Extend the current pool map */
	rc = pool_map_extend(map, map_version, map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);

	/* Write the new pool map. */
	rc = pool_buf_extract(map, &map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);

	rc = write_map_buf(tx, &svc->ps_root, map_buf, map_version);
	if (rc != 0)
		D_GOTO(out_map, rc);

	rc = rdb_tx_commit(tx);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": failed to commit: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
			D_GOTO(out_map, rc);
	}

	updated = true;
	/* Update svc->ps_pool to match the new pool map. */
	rc = ds_pool_tgt_map_update(svc->ps_pool, map_buf, map_version);
	if (rc != 0) {
		/*
		* We must resign to avoid handling future requests with a
		* stale pool map cache.
		*/
		rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
		rc = 0;
		goto out_map;
	}

	ds_rsvc_request_map_dist(&svc->ps_rsvc);

out_map:
	if (map_version_p != NULL)
		*map_version_p = pool_map_get_version((map == NULL || rc != 0) ?
						      svc->ps_pool->sp_map :
						      map);
	rdb_tx_end(tx);

out_map_buf:
	if (map_buf != NULL)
		pool_buf_free(map_buf);
	if (updated_p)
		*updated_p = updated;
	if (map)
		pool_map_decref(map);

	return rc;
}

static int
pool_extend_internal(uuid_t pool_uuid, struct rsvc_hint *hint,
		     uint32_t nnodes,
		     uuid_t target_uuids[], d_rank_list_t *rank_list,
		     uint32_t ndomains, int32_t *domains,
		     uint32_t *map_version_p)
{
	struct pool_svc		*svc;
	struct rdb_tx		tx;
	bool			updated = false;
	struct pool_target_id_list tgts = { 0 };
	int rc;

	rc = pool_svc_lookup_leader(pool_uuid, &svc, hint);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	ABT_rwlock_wrlock(svc->ps_lock);

	/*
	 * Extend the pool map directly - this is more complicated than other
	 * operations which are handled within ds_pool_update()
	 */
	rc = pool_extend_map(&tx, svc, ndomains, target_uuids,
			     rank_list, ndomains, domains,
			     &updated, map_version_p, hint);

	if (!updated)
		D_GOTO(out_lock, rc);

	/* Get a list of all the targets being added */
	rc = pool_map_find_targets_on_ranks(svc->ps_pool->sp_map, rank_list,
					    &tgts);
	if (rc <= 0) {
		D_ERROR("failed to schedule extend rc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_lock, rc);
	}

	/* Schedule an extension rebuild for those targets */
	rc = ds_rebuild_schedule(pool_uuid, *map_version_p, &tgts,
				 RB_OP_EXTEND);
	if (rc != 0) {
		D_ERROR("failed to schedule extend rc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_lock, rc);
	}

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);

out_svc:
	pool_target_id_list_free(&tgts);
	if (hint != NULL)
		ds_rsvc_set_hint(&svc->ps_rsvc, hint);
	pool_svc_put_leader(svc);
	return rc;
}

void
ds_pool_extend_handler(crt_rpc_t *rpc)
{
	struct pool_extend_in	*in = crt_req_get(rpc);
	struct pool_extend_out	*out = crt_reply_get(rpc);
	uuid_t			pool_uuid;
	uuid_t			*target_uuids;
	d_rank_list_t		rank_list;
	uint32_t		ndomains;
	int32_t			*domains;
	int rc;

	uuid_copy(pool_uuid, in->pei_op.pi_uuid);
	target_uuids = in->pei_tgt_uuids.ca_arrays;
	rank_list.rl_nr = in->pei_tgt_ranks->rl_nr;
	rank_list.rl_ranks = in->pei_tgt_ranks->rl_ranks;
	ndomains = in->pei_ndomains;
	domains = in->pei_domains.ca_arrays;

	rc = pool_extend_internal(pool_uuid, &out->peo_op.po_hint, ndomains,
				  target_uuids, &rank_list, ndomains, domains,
				  &out->peo_op.po_map_version);

	out->peo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pei_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_update_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_update_in	*in = crt_req_get(rpc);
	struct pool_tgt_update_out	*out = crt_reply_get(rpc);
	struct pool_target_addr_list	list = { 0 };
	struct pool_target_addr_list	out_list = { 0 };
	int				rc;

	if (in->pti_addr_list.ca_arrays == NULL ||
	    in->pti_addr_list.ca_count == 0)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: ntargets=%zu\n",
		DP_UUID(in->pti_op.pi_uuid), rpc, in->pti_addr_list.ca_count);

	list.pta_number = in->pti_addr_list.ca_count;
	list.pta_addrs = in->pti_addr_list.ca_arrays;
	rc = ds_pool_update(in->pti_op.pi_uuid, opc_get(rpc->cr_opc), &list,
			    &out_list, &out->pto_op.po_map_version,
			    &out->pto_op.po_hint, false);
	if (rc)
		D_GOTO(out, rc);

	out->pto_addr_list.ca_arrays = out_list.pta_addrs;
	out->pto_addr_list.ca_count = out_list.pta_number;

out:
	out->pto_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pti_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
	pool_target_addr_list_free(&out_list);
}

int
ds_pool_evict_rank(uuid_t pool_uuid, d_rank_t rank)
{
	struct pool_target_addr_list	list;
	struct pool_target_addr_list	out_list = { 0 };
	struct pool_target_addr		tgt_rank;
	uint32_t			map_version = 0;
	int				rc;

	tgt_rank.pta_rank = rank;
	tgt_rank.pta_target = -1;
	list.pta_number = 1;
	list.pta_addrs = &tgt_rank;

	rc = ds_pool_update(pool_uuid, POOL_EXCLUDE, &list, &out_list,
			    &map_version, NULL, true);

	D_DEBUG(DB_MGMT, "Exclude pool "DF_UUID"/%u rank %u: rc %d\n",
		DP_UUID(pool_uuid), map_version, rank, rc);

	pool_target_addr_list_free(&out_list);

	return rc;
}

struct evict_iter_arg {
	uuid_t *eia_hdl_uuids;
	size_t	eia_hdl_uuids_size;
	int	eia_n_hdl_uuids;
};

static int
evict_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
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
		D_FREE(arg->eia_hdl_uuids);
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
		D_FREE(arg.eia_hdl_uuids);
		return rc;
	}

	*hdl_uuids = arg.eia_hdl_uuids;
	*hdl_uuids_size = arg.eia_hdl_uuids_size;
	*n_hdl_uuids = arg.eia_n_hdl_uuids;
	return 0;
}

/*
 * Callers are responsible for freeing *hdl_uuids if this function returns zero.
 */
static int
validate_hdls_to_evict(struct rdb_tx *tx, struct pool_svc *svc,
		       uuid_t **hdl_uuids, int *n_hdl_uuids, uuid_t *hdl_list,
		       int n_hdl_list) {
	uuid_t		*valid_list;
	int		n_valid_list = 0;
	int		i;
	int		rc = 0;
	d_iov_t		key;
	d_iov_t		value;
	struct pool_hdl	hdl;

	if (hdl_list == NULL || n_hdl_list == 0) {
		return -DER_INVAL;
	}

	/* Assume the entire list is valid */
	D_ALLOC(valid_list, sizeof(uuid_t) * n_hdl_list);
	if (valid_list == NULL)
		return -DER_NOMEM;

	for (i = 0; i < n_hdl_list; i++) {
		d_iov_set(&key, hdl_list[i], sizeof(uuid_t));
		d_iov_set(&value, &hdl, sizeof(hdl));
		rc = rdb_tx_lookup(tx, &svc->ps_handles, &key, &value);

		if (rc == 0) {
			uuid_copy(valid_list[n_valid_list], hdl_list[i]);
			n_valid_list++;
		} else if (rc == -DER_NONEXIST) {
			D_DEBUG(DF_DSMS, "Skipping invalid handle" DF_UUID "\n",
				DP_UUID(hdl_list[i]));
			/* Reset RC in case we're the last entry */
			rc = 0;
			continue;
		} else {
			D_FREE(valid_list);
			D_GOTO(out, rc);
		}
	}

	*hdl_uuids = valid_list;
	*n_hdl_uuids = n_valid_list;

out:
	return rc;
}

void
ds_pool_evict_handler(crt_rpc_t *rpc)
{
	struct pool_evict_in   *in = crt_req_get(rpc);
	struct pool_evict_out  *out = crt_reply_get(rpc);
	struct pool_svc	       *svc;
	struct rdb_tx		tx;
	uuid_t		       *hdl_uuids = NULL;
	size_t			hdl_uuids_size;
	int			n_hdl_uuids = 0;
	int			rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pvi_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->pvi_op.pi_uuid, &svc,
				    &out->pvo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	/*
	 * If a subset of handles is specified use them instead of iterating
	 * through all handles for the pool uuid
	 */
	if (in->pvi_hdls.ca_arrays) {
		rc = validate_hdls_to_evict(&tx, svc, &hdl_uuids, &n_hdl_uuids,
					    in->pvi_hdls.ca_arrays,
					    in->pvi_hdls.ca_count);
	} else {
		rc = find_hdls_to_evict(&tx, svc, &hdl_uuids, &hdl_uuids_size,
					&n_hdl_uuids);
	}

	if (rc != 0)
		D_GOTO(out_lock, rc);

	if (n_hdl_uuids > 0) {
		/* If pool destroy but not forcibly, error: the pool is busy */

		if (in->pvi_pool_destroy && !in->pvi_pool_destroy_force) {
			D_DEBUG(DF_DSMS, DF_UUID": busy, %u open handles\n",
				DP_UUID(in->pvi_op.pi_uuid), n_hdl_uuids);
			D_GOTO(out_free, rc = -DER_BUSY);
		} else {
			/* Pool evict, or pool destroy with force=true */
			rc = pool_disconnect_hdls(&tx, svc, hdl_uuids,
						  n_hdl_uuids, rpc->cr_ctx);
			if (rc != 0)
				D_GOTO(out_free, rc);
		}
	}

	/* If pool destroy and not error case, disable new connections */
	if (in->pvi_pool_destroy) {
		uint32_t	connectable = 0;
		d_iov_t		value;

		d_iov_set(&value, &connectable, sizeof(connectable));
		rc = rdb_tx_update(&tx, &svc->ps_root,
				   &ds_pool_prop_connectable, &value);
		if (rc != 0)
			D_GOTO(out_free, rc);

		ds_pool_iv_srv_hdl_invalidate(svc->ps_pool);
		D_DEBUG(DF_DSMS, DF_UUID": pool destroy/evict: mark pool for "
			"no new connections\n", DP_UUID(in->pvi_op.pi_uuid));
	}

	rc = rdb_tx_commit(&tx);
	/* No need to set out->pvo_op.po_map_version. */
out_free:
	D_FREE(hdl_uuids);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pvo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pvo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pvi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

/**
 * Send a CaRT message to the pool svc to test and
 * (if applicable based on destoy and force option) evict all open handles
 * on a pool.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	handles		List of handles to selectively evict
 * \param[in]	n_handles	Number of items in handles
 * \param[in]	destroy		If true the evict request is a destroy request
 * \param[in]	force		If true and destroy is true request all handles
 *				be forcibly evicted
 *
 * \return	0		Success
 *		-DER_BUSY	Open pool handles exist and no force requested
 *
 */
int
ds_pool_svc_check_evict(uuid_t pool_uuid, d_rank_list_t *ranks,
			uuid_t *handles, size_t n_handles,
			uint32_t destroy, uint32_t force)
{
	int			 rc;
	struct rsvc_client	 client;
	crt_endpoint_t		 ep;
	struct dss_module_info	*info = dss_get_module_info();
	crt_rpc_t		*rpc;
	struct pool_evict_in	*in;
	struct pool_evict_out	*out;

	D_DEBUG(DB_MGMT,
		DF_UUID": Destroy pool (force: %d), inspect/evict handles\n",
		DP_UUID(pool_uuid), force);

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_EVICT, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool evict rpc: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pvi_op.pi_uuid, pool_uuid);
	uuid_clear(in->pvi_op.pi_hdl);
	in->pvi_hdls.ca_arrays = handles;
	in->pvi_hdls.ca_count = n_handles;

	/* Pool destroy (force=false): assert no open handles / do not evict.
	 * Pool destroy (force=true): evict any/all open handles on the pool.
	 */
	in->pvi_pool_destroy = destroy;
	in->pvi_pool_destroy_force = force;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->pvo_op.po_rc,
				      &out->pvo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->pvo_op.po_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": pool destroy failed to evict handles, "
			"rc: %d\n", DP_UUID(pool_uuid), rc);

	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

/* This RPC could be implemented by ds_rsvc. */
void
ds_pool_svc_stop_handler(crt_rpc_t *rpc)
{
	struct pool_svc_stop_in	       *in = crt_req_get(rpc);
	struct pool_svc_stop_out       *out = crt_reply_get(rpc);
	d_iov_t			id;
	int				rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p\n",
		DP_UUID(in->psi_op.pi_uuid), rpc);

	d_iov_set(&id, in->psi_op.pi_uuid, sizeof(uuid_t));
	rc = ds_rsvc_stop_leader(DS_RSVC_CLASS_POOL, &id, &out->pso_op.po_hint);

	out->pso_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->psi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

/**
 * Get a copy of the latest pool map buffer. Callers are responsible for
 * freeing iov->iov_buf with D_FREE.
 */
int
ds_pool_map_buf_get(uuid_t uuid, d_iov_t *iov, uint32_t *map_version)
{
	struct pool_svc	*svc;
	struct rdb_tx	tx;
	struct pool_buf	*map_buf;
	int		rc;

	rc = pool_svc_lookup_leader(uuid, &svc, NULL /* hint */);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->ps_lock);
	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_lock, rc);
	}
	D_ASSERT(map_buf != NULL);
	iov->iov_buf = map_buf;
	iov->iov_len = pool_buf_size(map_buf->pb_nr);
	iov->iov_buf_len = pool_buf_size(map_buf->pb_nr);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	pool_svc_put_leader(svc);
out:
	return rc;
}

void
ds_pool_iv_ns_update(struct ds_pool *pool, unsigned int master_rank)
{
	ds_iv_ns_update(pool->sp_iv_ns, master_rank);
}

int
ds_pool_svc_term_get(uuid_t uuid, uint64_t *term)
{
	struct pool_svc	*svc;
	int		rc;

	rc = pool_svc_lookup_leader(uuid, &svc, NULL /* hint */);
	if (rc != 0)
		return rc;

	*term = svc->ps_rsvc.s_term;

	pool_svc_put_leader(svc);
	return 0;
}

void
ds_pool_attr_set_handler(crt_rpc_t *rpc)
{
	struct pool_attr_set_in  *in = crt_req_get(rpc);
	struct pool_op_out	 *out = crt_reply_get(rpc);
	struct pool_svc		 *svc;
	struct rdb_tx		  tx;
	int			  rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pasi_op.pi_uuid), rpc, DP_UUID(in->pasi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pasi_op.pi_uuid, &svc, &out->po_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_wrlock(svc->ps_lock);
	rc = ds_rsvc_set_attr(&svc->ps_rsvc, &tx, &svc->ps_user,
			      in->pasi_bulk, rpc, in->pasi_count);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->po_hint);
	pool_svc_put_leader(svc);
out:
	out->po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pasi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_attr_del_handler(crt_rpc_t *rpc)
{
	struct pool_attr_del_in  *in = crt_req_get(rpc);
	struct pool_op_out	 *out = crt_reply_get(rpc);
	struct pool_svc		 *svc;
	struct rdb_tx		  tx;
	int			  rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->padi_op.pi_uuid), rpc, DP_UUID(in->padi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->padi_op.pi_uuid, &svc, &out->po_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_wrlock(svc->ps_lock);
	rc = ds_rsvc_del_attr(&svc->ps_rsvc, &tx, &svc->ps_user,
			      in->padi_bulk, rpc, in->padi_count);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->po_hint);
	pool_svc_put_leader(svc);
out:
	out->po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->padi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_attr_get_handler(crt_rpc_t *rpc)
{
	struct pool_attr_get_in  *in = crt_req_get(rpc);
	struct pool_op_out	 *out = crt_reply_get(rpc);
	struct pool_svc		 *svc;
	struct rdb_tx		  tx;
	int			  rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pagi_op.pi_uuid), rpc, DP_UUID(in->pagi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pagi_op.pi_uuid, &svc, &out->po_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_rdlock(svc->ps_lock);
	rc = ds_rsvc_get_attr(&svc->ps_rsvc, &tx, &svc->ps_user, in->pagi_bulk,
			      rpc, in->pagi_count, in->pagi_key_length);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->po_hint);
	pool_svc_put_leader(svc);
out:
	out->po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pagi_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);

}

void
ds_pool_attr_list_handler(crt_rpc_t *rpc)
{
	struct pool_attr_list_in	*in	    = crt_req_get(rpc);
	struct pool_attr_list_out	*out	    = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct rdb_tx			 tx;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_UUID": processing rpc %p: hdl="DF_UUID"\n",
		DP_UUID(in->pali_op.pi_uuid), rpc, DP_UUID(in->pali_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pali_op.pi_uuid, &svc,
				    &out->palo_op.po_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_rdlock(svc->ps_lock);
	rc = ds_rsvc_list_attr(&svc->ps_rsvc, &tx, &svc->ps_user,
			       in->pali_bulk, rpc, &out->palo_size);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->palo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->palo_op.po_rc = rc;
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->pali_op.pi_uuid), rpc, DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_replicas_update_handler(crt_rpc_t *rpc)
{
	struct pool_membership_in	*in = crt_req_get(rpc);
	struct pool_membership_out	*out = crt_reply_get(rpc);
	d_rank_list_t			*ranks;
	d_iov_t				 id;
	int				 rc;

	rc = daos_rank_list_dup(&ranks, in->pmi_targets);
	if (rc != 0)
		goto out;
	d_iov_set(&id, in->pmi_uuid, sizeof(uuid_t));

	switch (opc_get(rpc->cr_opc)) {
	case POOL_REPLICAS_ADD:
		rc = ds_rsvc_add_replicas(DS_RSVC_CLASS_POOL, &id, ranks,
					  ds_rsvc_get_md_cap(), &out->pmo_hint);
		break;

	case POOL_REPLICAS_REMOVE:
		rc = ds_rsvc_remove_replicas(DS_RSVC_CLASS_POOL, &id, ranks,
					     &out->pmo_hint);
		break;

	default:
		D_ASSERT(0);
	}

	out->pmo_failed = ranks;
out:
	out->pmo_rc = rc;
	crt_reply_send(rpc);
}

int
ds_pool_elect_dtx_leader(struct ds_pool *pool, daos_unit_oid_t *oid,
			 uint32_t version)
{
	struct pl_map		*map;
	struct pl_obj_layout	*layout;
	struct daos_obj_md	 md = { 0 };
	int			 rc = 0;

	map = pl_map_find(pool->sp_uuid, oid->id_pub);
	if (map == NULL) {
		D_WARN("Failed to find pool map tp select leader for "
		       DF_UOID" version = %d\n", DP_UOID(*oid), version);
		return -DER_INVAL;
	}

	md.omd_id = oid->id_pub;
	md.omd_ver = version;
	rc = pl_obj_place(map, &md, NULL, &layout);
	if (rc != 0)
		goto out;

	rc = pl_select_leader(oid->id_pub, oid->id_shard / layout->ol_grp_size,
			      layout->ol_grp_size, true,
			      pl_obj_get_shard, layout);
	pl_obj_layout_free(layout);
	if (rc < 0)
		D_WARN("Failed to select leader for "DF_UOID
		       "version = %d: rc = %d\n",
		       DP_UOID(*oid), version, rc);

out:
	pl_map_decref(map);
	return rc;
}

/**
 * Check whether the leader replica of the given object resides
 * on current server or not.
 *
 * \param [IN]	pool		Pointer to the pool
 * \param [IN]	oid		The OID of the object to be checked
 * \param [IN]	version		The pool map version
 *
 * \return			+1 if leader is on current server.
 * \return			Zero if the leader resides on another server.
 * \return			Negative value if error.
 */
int
ds_pool_check_dtx_leader(struct ds_pool *pool, daos_unit_oid_t *oid,
			 uint32_t version)
{
	struct pool_target	*target;
	d_rank_t		 myrank;
	int			 leader;
	int			 rc;

	leader = ds_pool_elect_dtx_leader(pool, oid, version);
	if (leader < 0)
		return leader;

	D_DEBUG(DB_TRACE, "get new leader tgt id %d\n", leader);
	rc = pool_map_find_target(pool->sp_map, leader, &target);
	if (rc < 0)
		return rc;

	if (rc != 1)
		return -DER_INVAL;

	rc = crt_group_rank(NULL, &myrank);
	if (rc < 0)
		return rc;

	if (myrank != target->ta_comp.co_rank)
		rc = 0;
	else
		rc = 1;

	return rc;
}

/* Update pool map version for current xstream. */
int
ds_pool_child_map_refresh_sync(struct ds_pool_child *dpc)
{
	struct pool_map_refresh_ult_arg	arg;
	ABT_eventual			eventual;
	int				*status;
	int				rc;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	arg.iua_pool_version = dpc->spc_map_version;
	uuid_copy(arg.iua_pool_uuid, dpc->spc_uuid);
	arg.iua_eventual = eventual;

	rc = dss_ult_create(ds_pool_map_refresh_ult, &arg, DSS_XS_SYS,
			    0, 0, NULL);
	if (rc)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));
	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

out_eventual:
	ABT_eventual_free(&eventual);
	return rc;
}

int
ds_pool_child_map_refresh_async(struct ds_pool_child *dpc)
{
	struct pool_map_refresh_ult_arg	*arg;
	int				rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -DER_NOMEM;
	arg->iua_pool_version = dpc->spc_map_version;
	uuid_copy(arg->iua_pool_uuid, dpc->spc_uuid);

	rc = dss_ult_create(ds_pool_map_refresh_ult, arg, DSS_XS_SYS,
			    0, 0, NULL);
	return rc;
}


int ds_pool_prop_fetch(struct ds_pool *pool, unsigned int bits,
		       daos_prop_t **prop_out)
{
	struct pool_svc	*svc;
	struct rdb_tx	tx;
	int		rc;

	rc = pool_svc_lookup_leader(pool->sp_uuid, &svc, NULL);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	/* read optional properties */
	ABT_rwlock_rdlock(svc->ps_lock);
	rc = pool_prop_read(&tx, svc, bits, prop_out);
	ABT_rwlock_unlock(svc->ps_lock);
	if (rc != 0)
		D_GOTO(out_tx, rc);
out_tx:
	rdb_tx_end(&tx);
out_svc:
	pool_svc_put_leader(svc);
	return rc;
}

bool
is_container_from_srv(uuid_t pool_uuid, uuid_t coh_uuid)
{
	struct ds_pool	*pool;
	uuid_t		hdl_uuid;
	int		rc;

	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL) {
		D_ERROR(DF_UUID": failed to get ds_pool\n",
			DP_UUID(pool_uuid));
		return false;
	}

	rc = ds_pool_iv_srv_hdl_fetch(pool, NULL, &hdl_uuid);
	ds_pool_put(pool);
	if (rc) {
		D_ERROR(DF_UUID" fetch srv hdl: %d\n", DP_UUID(pool_uuid), rc);
		return false;
	}

	return !uuid_compare(coh_uuid, hdl_uuid);
}

bool
is_pool_from_srv(uuid_t pool_uuid, uuid_t poh_uuid)
{
	struct ds_pool	*pool;
	uuid_t		hdl_uuid;
	int		rc;

	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL) {
		D_ERROR(DF_UUID": failed to get ds_pool\n",
			DP_UUID(pool_uuid));
		return false;
	}

	rc = ds_pool_iv_srv_hdl_fetch(pool, &hdl_uuid, NULL);
	ds_pool_put(pool);
	if (rc) {
		D_ERROR(DF_UUID" fetch srv hdl: %d\n", DP_UUID(pool_uuid), rc);
		return false;
	}

	return !uuid_compare(poh_uuid, hdl_uuid);
}

