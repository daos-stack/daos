/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

/* fwd decl */
static void free_server_list(struct server_entry *list, int len);

/* Management service ID string */
static char *mgmt_svc_id_s;

/* Management service ID */
static d_iov_t mgmt_svc_id;

/* Management service DB UUID */
static uuid_t mgmt_svc_db_uuid;

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
mgmt_svc_locate_cb(d_iov_t *id, char **path)
{
	char *s = NULL;

	/* Just create a dummy path that won't fail stat(). */
	D_ASPRINTF(s, "/dev/null");
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

	svc->map_servers = NULL;
	svc->n_map_servers = 0;
	svc->ms_rsvc.s_id = mgmt_svc_id;

	rc = ABT_rwlock_create(&svc->ms_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ms_lock: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_svc;
	}

	*rsvc = &svc->ms_rsvc;
	return 0;

err_svc:
	D_FREE(svc);
err:
	return rc;
}

static void
mgmt_svc_free_cb(struct ds_rsvc *rsvc)
{
	struct mgmt_svc *svc = mgmt_svc_obj(rsvc);

	if (svc->map_servers != NULL)
		free_server_list(svc->map_servers, svc->n_map_servers);

	ABT_rwlock_free(&svc->ms_lock);
	D_FREE(svc);
}

static int
mgmt_svc_bootstrap_cb(struct ds_rsvc *rsvc, void *varg)
{
	return 0;
}

static int
mgmt_svc_step_up_cb(struct ds_rsvc *rsvc)
{
	return 0;
}

static void
mgmt_svc_step_down_cb(struct ds_rsvc *rsvc)
{
}

static void
mgmt_svc_drain_cb(struct ds_rsvc *rsvc)
{
}

static void
free_server_list(struct server_entry *list, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		D_FREE(list[i].se_uri);
	}
	D_FREE(list);
}

static struct server_entry *
dup_server_list(struct server_entry *in, int in_len)
{
	int		    i;
	struct server_entry *out;

	D_ALLOC_ARRAY(out, in_len);
	if (out == NULL)
		return NULL;

	for (i = 0; i < in_len; i++) {
		out[i].se_rank = in[i].se_rank;
		D_STRNDUP(out[i].se_uri, in[i].se_uri, ADDR_STR_MAX_LEN - 1);
		if (out[i].se_uri == NULL) {
			free_server_list(out, i);
			return NULL;
		}
	}

	return out;
}

int
ds_mgmt_group_update_handler(struct mgmt_grp_up_in *in)
{
	struct mgmt_svc		*svc;
	struct server_entry	*map_servers;
	int			rc;

	/* ensure that it's started */
	rc = ds_mgmt_svc_start();
	if (rc != 0 && rc != -DER_ALREADY)
		goto out;

	/* we don't care if it's not leader */
	rc = ds_mgmt_svc_get(&svc);
	if (rc != 0 && rc != -DER_NOTLEADER)
		goto out;

	D_DEBUG(DB_MGMT, "setting %d servers in map version %u\n",
		in->gui_n_servers, in->gui_map_version);
	rc = ds_mgmt_group_update(CRT_GROUP_MOD_OP_REPLACE, in->gui_servers,
				  in->gui_n_servers, in->gui_map_version);
	if (rc != 0)
		goto out_svc;

	D_DEBUG(DB_MGMT, "set %d servers in map version %u\n",
		in->gui_n_servers, in->gui_map_version);

	map_servers = dup_server_list(in->gui_servers, in->gui_n_servers);
	if (map_servers == NULL) {
		rc = -DER_NOMEM;
		goto out_svc;
	}

	ABT_rwlock_wrlock(svc->ms_lock);

	if (svc->map_servers != NULL)
		free_server_list(svc->map_servers, svc->n_map_servers);

	svc->map_servers = map_servers;
	svc->n_map_servers = in->gui_n_servers;
	svc->map_version = in->gui_map_version;

	ABT_rwlock_unlock(svc->ms_lock);

	D_DEBUG(DB_MGMT, "requesting dist of map version %u (%u servers)\n",
		in->gui_map_version, in->gui_n_servers);
	ds_rsvc_request_map_dist(&svc->ms_rsvc);

out_svc:
	ds_mgmt_svc_put(svc);
out:
	return rc;
}

static int
map_update_bcast(crt_context_t ctx, struct mgmt_svc *svc, uint32_t map_version,
		 int nservers, struct server_entry servers[])
{
	struct mgmt_tgt_map_update_in  *in;
	struct mgmt_tgt_map_update_out *out;
	crt_opcode_t			opc;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DB_MGMT, "enter: version=%u nservers=%d\n", map_version,
		nservers);

	opc = DAOS_RPC_OPCODE(MGMT_TGT_MAP_UPDATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(ctx, NULL /* grp */,
				  NULL /* excluded_ranks */, opc,
				  NULL /* co_bulk_hdl */, NULL /* priv */,
				  0 /* flags */,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 32), &rpc);
	if (rc != 0) {
		D_ERROR("failed to create system map update RPC: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}
	in = crt_req_get(rpc);
	in->tm_servers.ca_count = nservers;
	in->tm_servers.ca_arrays = servers;
	in->tm_map_version = map_version;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	if (out->tm_rc != 0)
		rc = -DER_IO;

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DB_MGMT, "leave: version=%u nservers=%d: "DF_RC"\n",
		map_version, nservers, DP_RC(rc));
	return rc;
}

static int
mgmt_svc_map_dist_cb(struct ds_rsvc *rsvc)
{
	struct mgmt_svc	       *svc = mgmt_svc_obj(rsvc);
	struct dss_module_info *info = dss_get_module_info();
	uint32_t		map_version;
	int			n_map_servers;
	struct server_entry	*map_servers;
	int			rc;

	ABT_rwlock_rdlock(svc->ms_lock);

	map_servers = dup_server_list(svc->map_servers, svc->n_map_servers);
	if (map_servers == NULL) {
		ABT_rwlock_unlock(svc->ms_lock);
		return -DER_NOMEM;
	}
	n_map_servers = svc->n_map_servers;
	map_version = svc->map_version;

	ABT_rwlock_unlock(svc->ms_lock);

	rc = map_update_bcast(info->dmi_ctx, svc, map_version,
			      n_map_servers, map_servers);

	free_server_list(map_servers, n_map_servers);

	return rc;
}

static struct ds_rsvc_class mgmt_svc_rsvc_class = {
	.sc_name	= mgmt_svc_name_cb,
	.sc_locate	= mgmt_svc_locate_cb,
	.sc_alloc	= mgmt_svc_alloc_cb,
	.sc_free	= mgmt_svc_free_cb,
	.sc_bootstrap	= mgmt_svc_bootstrap_cb,
	.sc_step_up	= mgmt_svc_step_up_cb,
	.sc_step_down	= mgmt_svc_step_down_cb,
	.sc_drain	= mgmt_svc_drain_cb,
	.sc_map_dist	= mgmt_svc_map_dist_cb
};

/**
 * Start Management Service worker.
 */
int
ds_mgmt_svc_start(void)
{
	int			rc;

	rc = ds_rsvc_start_nodb(DS_RSVC_CLASS_MGMT, &mgmt_svc_id,
				mgmt_svc_db_uuid);
	if (rc != 0 && rc != -DER_ALREADY)
		D_ERROR("failed to start management service: "DF_RC"\n",
			DP_RC(rc));

	return rc;
}

static void
stopper(void *arg)
{
	int rc;

	rc = ds_rsvc_stop_nodb(DS_RSVC_CLASS_MGMT, &mgmt_svc_id);
	if (rc != 0)
		D_DEBUG(DB_MGMT, "ignoring "DF_RC"\n", DP_RC(rc));
}

int
ds_mgmt_svc_stop(void)
{
	ABT_thread	thread;
	int		rc;

	rc = dss_ult_create(stopper, NULL, DSS_XS_SYS, 0, 0, &thread);
	if (rc != 0) {
		D_ERROR("failed to create stopper ULT: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	DABT_THREAD_FREE(&thread);
	return 0;
}

int
ds_mgmt_svc_get(struct mgmt_svc **svc)
{
	struct ds_rsvc *rsvc;
	int rc;

	rc = ds_rsvc_lookup(DS_RSVC_CLASS_MGMT, &mgmt_svc_id, &rsvc);
	if (rc != 0)
		return rc;
	rsvc->s_leader_ref++;

	*svc = mgmt_svc_obj(rsvc);
	return 0;
}

void
ds_mgmt_svc_put(struct mgmt_svc *svc)
{
	ds_rsvc_put_leader(&svc->ms_rsvc);
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
