/*
 * (C) Copyright 2019 Intel Corporation.
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
 * ds_mgmt: System Metadata (Management Service)
 *
 * This file implements the management service, which manages the system
 * metadata.
 */

#define D_LOGFAC DD_FAC(mgmt)

#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>
#include "srv_internal.h"
#include "srv_layout.h"

/* Management service ID string */
static char *mgmt_svc_id_s;

/* Management service ID */
static d_iov_t mgmt_svc_id;

/* Management service DB UUID */
static uuid_t mgmt_svc_db_uuid;

/* Management service */
struct mgmt_svc {
	struct ds_rsvc		ms_rsvc;
	ABT_rwlock		ms_lock;
	rdb_path_t		ms_root;
	rdb_path_t		ms_servers;
	rdb_path_t		ms_uuids;
	ABT_mutex		ms_mutex;
	bool			ms_step_down;
	bool			ms_distribute;
	ABT_cond		ms_distribute_cv;
	ABT_thread		ms_distributord;
	uint32_t		ms_map_version;
	uint32_t		ms_rank_next;
	struct ds_rsvc_eventd	ms_eventd;
};

static struct mgmt_svc *
mgmt_svc_obj(struct ds_rsvc *rsvc)
{
	return container_of(rsvc, struct mgmt_svc, ms_rsvc);
}

static int
mgmt_svc_name_cb(d_iov_t *id, char **name)
{
	char *s;

	D_STRNDUP(s, mgmt_svc_id_s, DAOS_SYS_NAME_MAX);
	if (s == NULL)
		return -DER_NOMEM;
	*name = s;
	return 0;
}

static int
mgmt_svc_load_uuid_cb(d_iov_t *id, uuid_t db_uuid)
{
	uuid_copy(db_uuid, mgmt_svc_db_uuid);
	return 0;
}

static int
mgmt_svc_store_uuid_cb(d_iov_t *id, uuid_t db_uuid)
{
	return 0;
}

static int
mgmt_svc_delete_uuid_cb(d_iov_t *id)
{
	return 0;
}

static int
mgmt_svc_locate_cb(d_iov_t *id, char **path)
{
	char *s;

	D_ASPRINTF(s, "%s/rdb-system", dss_storage_path);
	if (s == NULL)
		return -DER_NOMEM;
	*path = s;
	return 0;
}

static int
mgmt_svc_alloc_cb(d_iov_t *id, struct ds_rsvc **rsvc)
{
	struct mgmt_svc	       *svc;
	int			rc;

	D_ALLOC_PTR(svc);
	if (svc == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}

	svc->ms_rsvc.s_id = mgmt_svc_id;

	rc = ABT_rwlock_create(&svc->ms_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ms_lock: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_svc;
	}

	rc = rdb_path_init(&svc->ms_root);
	if (rc != 0)
		goto err_lock;
	rc = rdb_path_push(&svc->ms_root, &rdb_path_root_key);
	if (rc != 0)
		goto err_root;

	rc = rdb_path_clone(&svc->ms_root, &svc->ms_servers);
	if (rc != 0)
		goto err_root;
	rc = rdb_path_push(&svc->ms_servers, &ds_mgmt_prop_servers);
	if (rc != 0)
		goto err_servers;

	rc = rdb_path_clone(&svc->ms_root, &svc->ms_uuids);
	if (rc != 0)
		goto err_servers;
	rc = rdb_path_push(&svc->ms_uuids, &ds_mgmt_prop_uuids);
	if (rc != 0)
		goto err_uuids;

	rc = ABT_mutex_create(&svc->ms_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create mutex: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_uuids;
	}

	rc = ABT_cond_create(&svc->ms_distribute_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create distribute_cv: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_mutex;
	}

	*rsvc = &svc->ms_rsvc;
	return 0;

err_mutex:
	ABT_mutex_free(&svc->ms_mutex);
err_uuids:
	rdb_path_fini(&svc->ms_uuids);
err_servers:
	rdb_path_fini(&svc->ms_servers);
err_root:
	rdb_path_fini(&svc->ms_root);
err_lock:
	ABT_rwlock_free(&svc->ms_lock);
err_svc:
	D_FREE(svc);
err:
	return rc;
}

static void
mgmt_svc_free_cb(struct ds_rsvc *rsvc)
{
	struct mgmt_svc *svc = mgmt_svc_obj(rsvc);

	ABT_cond_free(&svc->ms_distribute_cv);
	ABT_mutex_free(&svc->ms_mutex);
	rdb_path_fini(&svc->ms_uuids);
	rdb_path_fini(&svc->ms_servers);
	rdb_path_fini(&svc->ms_root);
	ABT_rwlock_free(&svc->ms_lock);
	D_FREE(svc);
}

static int add_server(struct rdb_tx *tx, struct mgmt_svc *svc, uint32_t rank,
		      struct server_rec *server);

struct bootstrap_arg {
	d_rank_t		sa_rank;
	struct server_rec	sa_server;
};

static int
mgmt_svc_bootstrap_cb(struct ds_rsvc *rsvc, void *varg)
{
	struct mgmt_svc	       *svc = mgmt_svc_obj(rsvc);
	struct bootstrap_arg   *arg = varg;
	struct rdb_tx		tx;
	struct rdb_kvs_attr	attr;
	d_iov_t		value;
	uint32_t		map_version = 1;
	uint32_t		rank_next = 0;
	uint8_t			self_heal = 1;
	int			rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;

	ABT_rwlock_wrlock(svc->ms_lock);

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 4;
	rc = rdb_tx_create_root(&tx, &attr);
	if (rc != 0)
		goto out_lock;

	attr.dsa_class = RDB_KVS_INTEGER;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(&tx, &svc->ms_root, &ds_mgmt_prop_servers,
			       &attr);
	if (rc != 0)
		goto out_lock;

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(&tx, &svc->ms_root, &ds_mgmt_prop_uuids, &attr);
	if (rc != 0)
		goto out_lock;

	rc = add_server(&tx, svc, arg->sa_rank, &arg->sa_server);
	if (rc != 0)
		goto out_lock;

	d_iov_set(&value, &map_version, sizeof(map_version));
	rc = rdb_tx_update(&tx, &svc->ms_root, &ds_mgmt_prop_map_version,
			   &value);
	if (rc != 0)
		goto out_lock;

	if (rank_next == arg->sa_rank)
		rank_next++;
	d_iov_set(&value, &rank_next, sizeof(rank_next));
	rc = rdb_tx_update(&tx, &svc->ms_root, &ds_mgmt_prop_rank_next, &value);
	if (rc != 0)
		goto out_lock;

	daos_iov_set(&value, &self_heal, sizeof(self_heal));
	rc = rdb_tx_update(&tx, &svc->ms_root, &ds_mgmt_prop_self_heal, &value);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

static void handle_event(struct ds_rsvc_event *e, void *arg);
static void map_distributord(void *arg);
static void notify_map_distributord(struct mgmt_svc *svc);

static int
mgmt_svc_step_up_cb(struct ds_rsvc *rsvc)
{
	struct mgmt_svc	       *svc = mgmt_svc_obj(rsvc);
	struct rdb_tx		tx;
	d_iov_t			value;
	uint8_t			self_heal;
	int			rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;

	ABT_rwlock_rdlock(svc->ms_lock);

	d_iov_set(&value, &svc->ms_map_version, sizeof(svc->ms_map_version));
	rc = rdb_tx_lookup(&tx, &svc->ms_root, &ds_mgmt_prop_map_version,
			   &value);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_MGMT, "new db\n");
			rc = DER_UNINIT;
		}
		goto out_lock;
	}

	d_iov_set(&value, &svc->ms_rank_next, sizeof(svc->ms_rank_next));
	rc = rdb_tx_lookup(&tx, &svc->ms_root, &ds_mgmt_prop_rank_next, &value);
	if (rc != 0)
		goto out_lock;

	daos_iov_set(&value, &self_heal, sizeof(self_heal));
	rc = rdb_tx_lookup(&tx, &svc->ms_root, &ds_mgmt_prop_self_heal, &value);
	if (rc != 0)
		goto out_lock;

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out;

	if (self_heal) {
		rc = ds_rsvc_eventd_start(handle_event, svc, &svc->ms_eventd);
		if (rc != 0)
			goto out;
	}

	svc->ms_step_down = false;
	rc = dss_ult_create(map_distributord, svc, DSS_ULT_MISC, DSS_TGT_SELF,
			    0, &svc->ms_distributord);
	if (rc != 0) {
		ds_rsvc_eventd_stop(&svc->ms_eventd);
		goto out;
	}

	/*
	 * Just in case the previous leader didn't complete distributing the
	 * system map before stepping down.
	 */
	notify_map_distributord(svc);

out:
	return rc;
}

static void
mgmt_svc_step_down_cb(struct ds_rsvc *rsvc)
{
	struct mgmt_svc	       *svc = mgmt_svc_obj(rsvc);
	int			rc;

	svc->ms_step_down = true;
	ABT_cond_broadcast(svc->ms_distribute_cv);
	rc = ABT_thread_join(svc->ms_distributord);
	D_ASSERTF(rc == 0, "%d\n", rc);
	ABT_thread_free(&svc->ms_distributord);
	if (ds_rsvc_eventd_started(&svc->ms_eventd))
		ds_rsvc_eventd_stop(&svc->ms_eventd);
}

static void
mgmt_svc_drain_cb(struct ds_rsvc *rsvc)
{
}

static struct ds_rsvc_class mgmt_svc_rsvc_class = {
	.sc_name	= mgmt_svc_name_cb,
	.sc_load_uuid	= mgmt_svc_load_uuid_cb,
	.sc_store_uuid	= mgmt_svc_store_uuid_cb,
	.sc_delete_uuid	= mgmt_svc_delete_uuid_cb,
	.sc_locate	= mgmt_svc_locate_cb,
	.sc_alloc	= mgmt_svc_alloc_cb,
	.sc_free	= mgmt_svc_free_cb,
	.sc_bootstrap	= mgmt_svc_bootstrap_cb,
	.sc_step_up	= mgmt_svc_step_up_cb,
	.sc_step_down	= mgmt_svc_step_down_cb,
	.sc_drain	= mgmt_svc_drain_cb
};

/**
 * Start Management Service replica. If \a create is false, all remaining input
 * parameters are ignored; otherwise, create the replica first. If \a bootstrap
 * is false, all remaining input parameters are ignored; otherwise, bootstrap
 * Management Service.
 *
 * \param[in]	create		whether to create the replica before starting
 * \param[in]	size		replica size in bytes
 * \param[in]	bootstrap	whether to bootstrap Management Service
 * \param[in]	srv_uuid	server UUID
 * \param[in]	addr		server management address
 */
int
ds_mgmt_svc_start(bool create, size_t size, bool bootstrap, uuid_t srv_uuid,
		  char *addr)
{
	d_rank_list_t		replicas;
	struct bootstrap_arg	arg = {};
	int			rc;

	if (bootstrap) {
		crt_group_t    *grp;
		char	       *uri;
		size_t		len;

		/* Prepare a self-only replica list. */
		replicas.rl_nr = 1;
		replicas.rl_ranks = &arg.sa_rank;

		rc = crt_group_rank(NULL, &arg.sa_rank);
		D_ASSERTF(rc == 0, "%d\n", rc);
		arg.sa_server.sr_flags = SERVER_IN;
		arg.sa_server.sr_nctxs = dss_ctx_nr_get();
		uuid_copy(arg.sa_server.sr_uuid, srv_uuid);

		len = strnlen(addr, ADDR_STR_MAX_LEN);
		if (len >= ADDR_STR_MAX_LEN) {
			D_ERROR("server address '%.*s...' too long\n",
				ADDR_STR_MAX_LEN - 1, addr);
			return -DER_INVAL;
		}
		memcpy(arg.sa_server.sr_addr, addr, len + 1);

		grp = crt_group_lookup(NULL);
		D_ASSERT(grp != NULL);
		rc = crt_rank_uri_get(grp, arg.sa_rank, 0 /* tag */, &uri);
		if (rc != 0) {
			D_ERROR("unable to get self URI: %d\n", rc);
			return rc;
		}
		len = strnlen(uri, ADDR_STR_MAX_LEN);
		if (len >= ADDR_STR_MAX_LEN) {
			D_ERROR("self URI '%.*s...' too long\n",
				ADDR_STR_MAX_LEN, uri);
			D_FREE(uri);
			return rc;
		}
		memcpy(arg.sa_server.sr_uri, uri, len + 1);
		D_FREE(uri);
	}

	rc = ds_rsvc_start(DS_RSVC_CLASS_MGMT, &mgmt_svc_id, mgmt_svc_db_uuid,
			   create, size, bootstrap ? &replicas : NULL,
			   bootstrap ? &arg : NULL);
	if (rc != 0 && rc != -DER_ALREADY)
		D_ERROR("failed to start management service: %d\n", rc);

	return rc;
}

int
ds_mgmt_svc_stop(void)
{
	int rc;

	rc = ds_rsvc_stop_all(DS_RSVC_CLASS_MGMT);
	if (rc != 0)
		D_ERROR("failed to stop management service: %d\n", rc);
	return rc;
}

static int
mgmt_svc_lookup_leader(struct mgmt_svc **svc, struct rsvc_hint *hint)
{
	struct ds_rsvc *rsvc;
	int		rc;

	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_MGMT, &mgmt_svc_id, &rsvc,
				   hint);
	if (rc != 0)
		return rc;
	*svc = mgmt_svc_obj(rsvc);
	return 0;
}

static void
mgmt_svc_put_leader(struct mgmt_svc *svc)
{
	ds_rsvc_put_leader(&svc->ms_rsvc);
}

struct enum_server_arg {
	struct server_entry    *esa_servers;
	int			esa_servers_cap;
	int			esa_servers_len;
};

static int
enum_server_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct enum_server_arg *arg = varg;
	uint64_t	       *rank_key;
	struct server_rec      *rec;
	struct server_entry    *entry;

	if (key->iov_len != sizeof(*rank_key) ||
	    val->iov_len != sizeof(*rec)) {
		D_ERROR("invalid key/value size: key="DF_U64" value="DF_U64"\n",
			key->iov_len, val->iov_len);
		return -DER_IO;
	}
	rank_key = key->iov_buf;
	if (*rank_key > UINT32_MAX) {
		D_ERROR("invalid key: "DF_U64"\n", *rank_key);
		return -DER_IO;
	}
	rec = val->iov_buf;

	/* Make sure arg->esa_servers[] have enough space for this server. */
	if (arg->esa_servers_len + 1 > arg->esa_servers_cap) {
		struct server_entry    *servers;
		int			servers_cap;

		if (arg->esa_servers_cap == 0)
			servers_cap = 1;
		else
			servers_cap = arg->esa_servers_cap * 2;
		D_REALLOC_ARRAY(servers, arg->esa_servers, servers_cap);
		if (servers == NULL)
			return -DER_NOMEM;
		arg->esa_servers = servers;
		arg->esa_servers_cap = servers_cap;
	}

	/*
	 * Note that D_REALLOC_ARRAY doesn't zero the new memory region. Also,
	 * we use pointers to persistent memory addresses.
	 */
	entry = &arg->esa_servers[arg->esa_servers_len];
	entry->se_rank = (uint32_t)*rank_key;
	entry->se_flags = rec->sr_flags;
	entry->se_nctxs = rec->sr_nctxs;
	entry->se_uri = rec->sr_uri;
	arg->esa_servers_len++;

	return 0;
}

void
ds_mgmt_hdlr_query(crt_rpc_t *rpc)
{
	struct mgmt_svc	       *svc;
	struct mgmt_query_out  *out = crt_reply_get(rpc);
	struct rdb_tx		tx;
	struct enum_server_arg	arg = {};
	int			rc;

	rc = mgmt_svc_lookup_leader(&svc, &out->qo_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_rdlock(svc->ms_lock);

	rc = rdb_tx_iterate(&tx, &svc->ms_servers, false /* !backward */,
			    enum_server_cb, &arg);
	if (rc != 0)
		goto out_lock;

	out->qo_servers.ca_count = arg.esa_servers_len;
	out->qo_servers.ca_arrays = arg.esa_servers;
	out->qo_map_version = svc->ms_map_version;
	out->qo_map_in_sync = !svc->ms_distribute;

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out_svc:
	mgmt_svc_put_leader(svc);
out:
	out->qo_rc = rc;
	crt_reply_send(rpc);
	if (arg.esa_servers != NULL)
		D_FREE(arg.esa_servers);
}

/*
 * If successful, output parameters rank and rank_next return the allocated
 * rank and the new rank_next value, respectively.
 */
static int
alloc_rank(struct rdb_tx *tx, struct mgmt_svc *svc, uint32_t *rank,
	   uint32_t *rank_next)
{
	d_iov_t	key;
	d_iov_t	value;
	int		rc;

	/*
	 * Skip ranks that have already been taken (by servers who requested
	 * specific ranks.
	 */
	*rank = svc->ms_rank_next;
	for (;;) {
		uint64_t rank_key = *rank;

		d_iov_set(&key, &rank_key, sizeof(rank_key));
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ms_servers, &key, &value);
		if (rc != 0)
			break;
		*rank += 1;
	}
	if (rc != -DER_NONEXIST)
		return rc;

	/*
	 * Update rank_next. svc->ms_rank_next shall be updated only after this
	 * TX commits successfully.
	 */
	*rank_next = *rank + 1;
	d_iov_set(&value, rank_next, sizeof(*rank_next));
	return rdb_tx_update(tx, &svc->ms_root, &ds_mgmt_prop_rank_next,
			     &value);
}

static int
add_server(struct rdb_tx *tx, struct mgmt_svc *svc, uint32_t rank,
	   struct server_rec *server)
{
	uint64_t	rank_key = rank;
	d_iov_t	key;
	d_iov_t	value;
	int		rc;

	d_iov_set(&key, &rank_key, sizeof(rank_key));
	d_iov_set(&value, server, sizeof(*server));
	rc = rdb_tx_update(tx, &svc->ms_servers, &key, &value);
	if (rc != 0)
		return rc;

	d_iov_set(&key, server->sr_uuid, sizeof(uuid_t));
	d_iov_set(&value, &rank, sizeof(rank));
	rc = rdb_tx_update(tx, &svc->ms_uuids, &key, &value);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_MGMT, "rank=%u uuid="DF_UUID" uri=%s nctxs=%u addr=%s\n",
		rank, DP_UUID(server->sr_uuid), server->sr_uri,
		server->sr_nctxs, server->sr_addr);
	return 0;
}

int
ds_mgmt_join_handler(struct mgmt_join_in *in, struct mgmt_join_out *out)
{
	struct mgmt_svc	       *svc;
	struct rdb_tx		tx;
	d_iov_t		key;
	d_iov_t		value;
	uint32_t		rank;
	uint32_t		rank_next;
	uint32_t		map_version;
	int			rc;

	rc = mgmt_svc_lookup_leader(&svc, &out->jo_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_wrlock(svc->ms_lock);

	/* Look up the server by UUID. */
	d_iov_set(&key, in->ji_server.sr_uuid, sizeof(uuid_t));
	d_iov_set(&value, &rank, sizeof(rank));
	rc = rdb_tx_lookup(&tx, &svc->ms_uuids, &key, &value);
	if (rc == 0) {
		uint64_t		rank_key = rank;
		struct server_rec      *r;

		if (in->ji_rank != -1 && in->ji_rank != rank) {
			D_ERROR("rank cannot change: %u -> %u\n", rank,
				in->ji_rank);
			rc = -DER_PROTO;
			goto out_lock;
		}
		out->jo_rank = rank;
		d_iov_set(&key, &rank_key, sizeof(rank_key));
		d_iov_set(&value, NULL, sizeof(*r));
		rc = rdb_tx_lookup(&tx, &svc->ms_servers, &key, &value);
		if (rc != 0) {
			D_ERROR("failed to find server rank %u record: %d\n",
				rank, rc);
			goto out_lock;
		}
		r = value.iov_buf;
		out->jo_flags = r->sr_flags;
		if (!(r->sr_flags & SERVER_IN)) {
			D_INFO("rejected excluded server rank %u\n", rank);
			goto out_lock;
		}
		D_DEBUG(DB_TRACE, "rank %u rejoined\n", rank);
		notify_map_distributord(svc);
		rc = 0;
		goto out_lock;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("failed to look up server "DF_UUID": %d\n",
			DP_UUID(in->ji_server.sr_uuid), rc);
		goto out_lock;
	}

	/* Allocate/verify the server rank. */
	if (in->ji_rank == -1) {
		rc = alloc_rank(&tx, svc, &rank, &rank_next);
		if (rc != 0) {
			D_ERROR("failed to allocate rank for server "DF_UUID
				": %d\n", DP_UUID(in->ji_server.sr_uuid), rc);
			goto out_lock;
		}
	} else {
		uint64_t rank_key = in->ji_rank;

		d_iov_set(&key, &rank_key, sizeof(rank_key));
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(&tx, &svc->ms_servers, &key, &value);
		if (rc == 0) {
			D_ERROR("rank %u requested by server "DF_UUID
				" already taken\n", in->ji_rank,
				DP_UUID(in->ji_server.sr_uuid));
			rc = -DER_EXIST;
			goto out_lock;
		} else if (rc != -DER_NONEXIST) {
			D_ERROR("failed to verify rank for server "DF_UUID
				": %d\n", DP_UUID(in->ji_server.sr_uuid), rc);
			goto out_lock;
		}
		rank = in->ji_rank;
	}

	rc = add_server(&tx, svc, rank, &in->ji_server);
	if (rc != 0) {
		D_ERROR("failed to add server "DF_UUID" as rank %u: %d\n",
			DP_UUID(in->ji_server.sr_uuid), rank, rc);
		goto out_lock;
	}

	map_version = svc->ms_map_version + 1;
	d_iov_set(&value, &map_version, sizeof(map_version));
	rc = rdb_tx_update(&tx, &svc->ms_root, &ds_mgmt_prop_map_version,
			   &value);
	if (rc != 0) {
		D_ERROR("failed to increment map version to %u: %d\n",
			map_version, rc);
		goto out_lock;
	}

	rc = rdb_tx_commit(&tx);
	if (rc != 0) {
		D_ERROR("failed to commit map version %u: %d\n", map_version,
			rc);
		goto out_lock;
	}

	D_DEBUG(DB_TRACE, "rank %u joined in map version %u\n", rank,
		map_version);
	svc->ms_map_version = map_version;
	if (in->ji_rank != -1)
		svc->ms_rank_next = rank_next;
	notify_map_distributord(svc);
	out->jo_rank = rank;
	out->jo_flags = SERVER_IN;

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out_svc:
	mgmt_svc_put_leader(svc);
out:
	return rc;
}

/* Callers are responsible for freeing resp->psrs. */
int
ds_mgmt_get_attach_info_handler(Mgmt__GetAttachInfoResp *resp)
{
	struct mgmt_svc	       *svc;
	d_rank_list_t	       *ranks;
	crt_group_t	       *grp;
	int			i;
	int			rc;

	rc = mgmt_svc_lookup_leader(&svc, NULL /* hint */);
	if (rc != 0)
		goto out;

	rc = rdb_get_ranks(svc->ms_rsvc.s_db, &ranks);
	if (rc != 0)
		goto out_svc;

	D_ALLOC(resp->psrs, sizeof(*resp->psrs) * ranks->rl_nr);
	if (resp->psrs == NULL) {
		rc = -DER_NOMEM;
		goto out_ranks;
	}
	grp = crt_group_lookup(NULL);
	D_ASSERT(grp != NULL);
	for (i = 0; i < ranks->rl_nr; i++) {
		d_rank_t rank = ranks->rl_ranks[i];

		D_ALLOC(resp->psrs[i], sizeof(*(resp->psrs[i])));
		if (resp->psrs[i] == NULL) {
			rc = -DER_NOMEM;
			break;
		}
		mgmt__get_attach_info_resp__psr__init(resp->psrs[i]);
		resp->psrs[i]->rank = rank;
		rc = crt_rank_uri_get(grp, rank, 0 /* tag */,
				      &(resp->psrs[i]->uri));
		if (rc != 0) {
			D_ERROR("unable to get rank %u URI: %d\n", rank, rc);
			break;
		}
	}
	if (rc != 0) {
		for (; i >= 0; i--) {
			if (resp->psrs[i] != NULL) {
				if (resp->psrs[i]->uri != NULL)
					D_FREE(resp->psrs[i]->uri);
				D_FREE(resp->psrs[i]);
			}
		}
		D_FREE(resp->psrs);
		resp->psrs = NULL; /* paranoid */
		goto out_ranks;
	}
	resp->n_psrs = ranks->rl_nr;

out_ranks:
	d_rank_list_free(ranks);
out_svc:
	mgmt_svc_put_leader(svc);
out:
	return rc;
}

static int
map_update_bcast(crt_context_t ctx, struct mgmt_svc *svc, uint32_t map_version,
		 bool self_heal, int nservers, struct server_entry servers[])
{
	struct mgmt_tgt_map_update_in  *in;
	struct mgmt_tgt_map_update_out *out;
	crt_opcode_t			opc;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DB_MGMT, "enter: version=%u nservers=%d\n", map_version,
		nservers);

	opc = DAOS_RPC_OPCODE(MGMT_TGT_MAP_UPDATE, DAOS_MGMT_MODULE, 1);
	rc = crt_corpc_req_create(ctx, NULL /* grp */,
				  NULL /* excluded_ranks */, opc,
				  NULL /* co_bulk_hdl */, NULL /* priv */,
				  0 /* flags */,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 32), &rpc);
	if (rc != 0) {
		D_ERROR("failed to create system map update RPC: %d\n", rc);
		goto out;
	}
	in = crt_req_get(rpc);
	in->tm_servers.ca_count = nservers;
	in->tm_servers.ca_arrays = servers;
	in->tm_map_version = map_version;
	in->tm_self_heal = self_heal;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	if (out->tm_rc != 0)
		rc = -DER_IO;

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DB_MGMT, "leave: version=%u nservers=%d: %d\n", map_version,
		nservers, rc);
	return rc;
}

static int
distribute_map(crt_context_t ctx, struct mgmt_svc *svc)
{
	struct rdb_tx		tx;
	uint8_t			self_heal;
	daos_iov_t		value;
	struct enum_server_arg	arg = {};
	int			rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;

	ABT_rwlock_rdlock(svc->ms_lock);

	daos_iov_set(&value, &self_heal, sizeof(self_heal));
	rc = rdb_tx_lookup(&tx, &svc->ms_root, &ds_mgmt_prop_self_heal, &value);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_iterate(&tx, &svc->ms_servers, false /* !backward */,
			    enum_server_cb, &arg);
	if (rc != 0)
		goto out_lock;

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out_arg;

	rc = map_update_bcast(ctx, svc, svc->ms_map_version, self_heal,
			      arg.esa_servers_len, arg.esa_servers);

out_arg:
	if (arg.esa_servers != NULL)
		D_FREE(arg.esa_servers);
out:
	return rc;
}

static void
map_distributord(void *arg)
{
	struct mgmt_svc	       *svc = arg;
	struct dss_module_info *info = dss_get_module_info();

	for (;;) {
		bool	step_down;
		int	rc;

		ABT_mutex_lock(svc->ms_mutex);
		for (;;) {
			step_down = svc->ms_step_down;
			if (step_down)
				break;
			if (svc->ms_distribute) {
				svc->ms_distribute = false;
				break;
			}
			ABT_cond_wait(svc->ms_distribute_cv, svc->ms_mutex);
		}
		ABT_mutex_unlock(svc->ms_mutex);
		if (step_down)
			break;
		rc = distribute_map(info->dmi_ctx, svc);
		if (rc != 0)
			/* Try again. */
			svc->ms_distribute = true;
		dss_sleep(1000 /* ms */);
	}
}

static void
notify_map_distributord(struct mgmt_svc *svc)
{
	svc->ms_distribute = true;
	ABT_cond_broadcast(svc->ms_distribute_cv);
}

static int
is_ms_replica(struct mgmt_svc *svc, d_rank_t rank, bool *result)
{
	d_rank_list_t  *ms_ranks;
	int		rc;

	rc = rdb_get_ranks(svc->ms_rsvc.s_db, &ms_ranks);
	if (rc != 0)
		return rc;
	*result = d_rank_list_find(ms_ranks, rank, NULL /* idx */);
	d_rank_list_free(ms_ranks);
	return 0;
}

static void
handle_event(struct ds_rsvc_event *e, void *arg)
{
	struct mgmt_svc	       *svc = arg;
	struct rdb_tx		tx;
	bool			ms_replica;
	uint64_t		rank_key = e->v_rank;
	daos_iov_t		key;
	daos_iov_t		value;
	struct server_rec	server;
	uint32_t		map_version;
	int			rc;

	rc = rdb_tx_begin(svc->ms_rsvc.s_db, svc->ms_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;

	ABT_rwlock_wrlock(svc->ms_lock);

	/* Do not exclude MS replicas. */
	rc = is_ms_replica(svc, e->v_rank, &ms_replica);
	if (rc != 0)
		goto out_lock;
	if (ms_replica) {
		D_DEBUG(DB_MGMT, "ignore MS replica rank %u\n", e->v_rank);
		goto out_lock;
	}

	/* Look up the rank. */
	daos_iov_set(&key, &rank_key, sizeof(rank_key));
	daos_iov_set(&value, &server, sizeof(server));
	rc = rdb_tx_lookup(&tx, &svc->ms_servers, &key, &value);
	if (rc != 0)
		goto out_lock;
	if (!(server.sr_flags & SERVER_IN))
		goto out_lock;

	/* Mark the server as out. */
	server.sr_flags &= ~SERVER_IN;
	daos_iov_set(&key, &rank_key, sizeof(rank_key));
	daos_iov_set(&value, &server, sizeof(server));
	rc = rdb_tx_update(&tx, &svc->ms_servers, &key, &value);
	if (rc != 0)
		goto out_lock;

	/* Update the map version. */
	map_version = svc->ms_map_version + 1;
	daos_iov_set(&value, &map_version, sizeof(map_version));
	rc = rdb_tx_update(&tx, &svc->ms_root, &ds_mgmt_prop_map_version,
			   &value);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;

	D_DEBUG(DB_TRACE, "rank %u excluded in map version %u\n", e->v_rank,
		map_version);
	svc->ms_map_version = map_version;
	notify_map_distributord(svc);

out_lock:
	ABT_rwlock_unlock(svc->ms_lock);
	rdb_tx_end(&tx);
out:
	D_DEBUG(DB_MGMT, "rank=%u type=%d: %d\n", e->v_rank, e->v_type, rc);
	return;
}

int
ds_mgmt_system_module_init(void)
{
	crt_group_t    *group;
	size_t		len;

	/* Set the MS ID to the system name. */
	group = crt_group_lookup(NULL);
	D_ASSERT(group != NULL);
	len = strnlen(group->cg_grpid, DAOS_SYS_NAME_MAX + 1);
	D_ASSERT(len <= DAOS_SYS_NAME_MAX);
	D_STRNDUP(mgmt_svc_id_s, group->cg_grpid, len);
	if (mgmt_svc_id_s == NULL)
		return -DER_NOMEM;
	d_iov_set(&mgmt_svc_id, mgmt_svc_id_s, len + 1);

	/* Set the MS DB UUID bytes to the system name bytes. */
	D_CASSERT(DAOS_SYS_NAME_MAX + 1 <= sizeof(mgmt_svc_db_uuid));
	memcpy(mgmt_svc_db_uuid, mgmt_svc_id.iov_buf, mgmt_svc_id.iov_len);

	ds_rsvc_class_register(DS_RSVC_CLASS_MGMT, &mgmt_svc_rsvc_class);

	return 0;
}

void
ds_mgmt_system_module_fini(void)
{
	ds_rsvc_class_unregister(DS_RSVC_CLASS_MGMT);
	D_FREE(mgmt_svc_id_s);
}
