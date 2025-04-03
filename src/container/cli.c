/**
 * (C) Copyright 2016-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont: Container Client
 *
 * This module is part of libdaos. It implements the container methods of DAOS
 * API as well as daos/container.h.
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos/container.h>
#include <daos/cont_props.h>
#include <daos/dedup.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/pool.h>
#include <daos/object.h>
#include <daos/rsvc.h>
#include <daos_types.h>
#include "cli_internal.h"
#include "rpc.h"

int	dc_cont_proto_version;

/**
 * Initialize container interface
 */
int
dc_cont_init(void)
{
	int		rc;
	uint32_t        ver_array[2] = {DAOS_CONT_VERSION - 1, DAOS_CONT_VERSION};

	dc_cont_proto_version = 0;
	rc = daos_rpc_proto_query(cont_proto_fmt_v7.cpf_base, ver_array, 2, &dc_cont_proto_version);
	if (rc)
		return rc;

	if (dc_cont_proto_version == DAOS_CONT_VERSION - 1) {
		rc = daos_rpc_register(&cont_proto_fmt_v7, CONT_PROTO_CLI_COUNT, NULL,
				       DAOS_CONT_MODULE);
	} else if (dc_cont_proto_version == DAOS_CONT_VERSION) {
		rc = daos_rpc_register(&cont_proto_fmt_v8, CONT_PROTO_CLI_COUNT, NULL,
				       DAOS_CONT_MODULE);
	} else {
		D_ERROR("%d version cont RPC not supported.\n", dc_cont_proto_version);
		rc = -DER_PROTO;
	}
	if (rc != 0)
		D_ERROR("failed to register %d version cont RPCs: "DF_RC"\n",
			dc_cont_proto_version, DP_RC(rc));

	return rc;
}

/**
 * Finalize container interface
 */
void
dc_cont_fini(void)
{
	int rc;

	if (dc_cont_proto_version == DAOS_CONT_VERSION - 1)
		rc = daos_rpc_unregister(&cont_proto_fmt_v7);
	else
		rc = daos_rpc_unregister(&cont_proto_fmt_v8);
	if (rc != 0)
		D_ERROR("failed to unregister %d version cont RPCs: "DF_RC"\n",
			dc_cont_proto_version, DP_RC(rc));
}

/*
 * Returns:
 *
 *   < 0			error; end the operation
 *   RSVC_CLIENT_RECHOOSE	task reinited; return 0 from completion cb
 *   RSVC_CLIENT_PROCEED	OK; proceed to process the reply
 */
static int
cont_rsvc_client_complete_rpc(struct dc_pool *pool, const crt_endpoint_t *ep,
			      int rc_crt, struct cont_op_out *out,
			      tse_task_t *task)
{
	int rc;

	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_complete_rpc(&pool->dp_client, ep, rc_crt, out->co_rc,
				      &out->co_hint);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->co_rc)))
		return RSVC_CLIENT_RECHOOSE;
	return RSVC_CLIENT_PROCEED;
}

struct cont_args {
	struct dc_pool			*pool;
	crt_rpc_t			*rpc;
	daos_prop_t			*prop;
};

struct cont_task_priv {
	struct d_backoff_seq backoff_seq;
	uint64_t             rq_time; /* time of the request (hybrid logical clock) */
	struct dc_cont      *cont;    /* client container handle (used by cont_open) */
};

static int
cont_task_create_priv(tse_task_t *task, struct cont_task_priv **tpriv_out)
{
	struct cont_task_priv *tpriv;

	D_ASSERT(dc_task_get_priv(task) == NULL);

	D_ALLOC_PTR(tpriv);
	if (tpriv == NULL)
		return -DER_NOMEM;

	dc_pool_init_backoff_seq(&tpriv->backoff_seq);

	dc_task_set_priv(task, tpriv);

	*tpriv_out = tpriv;
	return 0;
}

static void
cont_task_destroy_priv(tse_task_t *task)
{
	struct cont_task_priv *tpriv = dc_task_get_priv(task);

	D_ASSERT(tpriv != NULL);
	dc_task_set_priv(task, NULL /* priv */);
	dc_pool_fini_backoff_seq(&tpriv->backoff_seq);
	D_FREE(tpriv);
}

static int
cont_task_reinit(tse_task_t *task)
{
	struct cont_task_priv *tpriv = dc_task_get_priv(task);
	uint32_t               delay = d_backoff_seq_next(&tpriv->backoff_seq);

	return tse_task_reinit_with_delay(task, delay);
}

static int
cont_create_complete(tse_task_t *task, void *data)
{
	struct cont_args       *arg = (struct cont_args *)data;
	daos_cont_create_t     *args;
	struct dc_pool	       *pool = arg->pool;
	struct cont_create_out *out = crt_reply_get(arg->rpc);
	bool                    reinit = false;
	int			rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cco_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while creating container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cco_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, "failed to create container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	args = dc_task_get_args(task);
	/** Returned container UUID upon successful creation */
	if (args->cuuid != NULL)
		uuid_copy(*args->cuuid, args->uuid);

	D_DEBUG(DB_MD, "completed creating container\n");

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	daos_prop_free(arg->prop);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

static bool
daos_prop_has_entry(daos_prop_t *prop, uint32_t entry_type)
{
	return (prop != NULL) &&
	       (daos_prop_entry_get(prop, entry_type) != NULL);
}

/*
 * If no owner/group prop was supplied, translates euid/egid to user and group
 * names, and adds them as owners to a new copy of the daos_prop_t passed in.
 * Allocate root objid if not specified by the caller.
 * The newly allocated prop is expected to be freed by the cont create callback.
 */
static int
dup_cont_create_props(daos_handle_t poh, daos_prop_t **prop_out,
		      daos_prop_t *prop_in)
{
	char				*owner = NULL;
	char				*owner_grp = NULL;
	struct daos_prop_co_roots	*roots = NULL;
	daos_prop_t			*final_prop = NULL;
	uint32_t			idx = 0;
	uint32_t			entries;
	int				rc = 0;
	uid_t				uid = geteuid();
	gid_t				gid = getegid();

	entries = (prop_in == NULL) ? 0 : prop_in->dpp_nr;

	rc = daos_acl_uid_to_principal(uid, &owner);
	if (rc != 0) {
		D_ERROR("Failed to parse uid "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_out, rc);
	}

	if (daos_prop_has_entry(prop_in, DAOS_PROP_CO_OWNER)) {
		struct daos_prop_entry *owner_entry;

		owner_entry = daos_prop_entry_get(prop_in, DAOS_PROP_CO_OWNER);
		D_ASSERT(owner_entry != NULL);
		if (strncmp(owner_entry->dpe_str, owner, DAOS_ACL_MAX_PRINCIPAL_LEN) != 0) {
			D_ERROR("creating a container with an owner other than the current user is "
				"not permitted\n");
			D_GOTO(err_out, rc = -DER_INVAL);
		}

		D_FREE(owner); /* don't add owner to the prop array below if it's already there */
	} else {
		entries++;
	}

	if (!daos_prop_has_entry(prop_in, DAOS_PROP_CO_OWNER_GROUP)) {
		rc = daos_acl_gid_to_principal(gid, &owner_grp);
		if (rc != 0) {
			D_ERROR("Failed to parse gid "DF_RC"\n", DP_RC(rc));
			D_GOTO(err_out, rc);
		}

		entries++;
	}

	if (!daos_prop_has_entry(prop_in, DAOS_PROP_CO_ROOTS)) {
		uint64_t rf;

		/**
		 * If user did not provide a root object id, let's generate
		 * it here. Worst case is that it is not used by the middleware.
		 */

		D_ALLOC_PTR(roots);
		if (roots == NULL) {
			D_GOTO(err_out, rc = -DER_NOMEM);
		}

		/** allocate objid according to the redundancy factor */
		roots->cr_oids[0].lo = 0;
		roots->cr_oids[0].hi = 0;
		rf = daos_cont_prop2redunfac(prop_in);
		rc = daos_obj_generate_oid_by_rf(poh, rf, &roots->cr_oids[0], 0,
						 0, 0, 0, daos_cont_prop2redunlvl(prop_in));
		if (rc) {
			D_ERROR("Failed to generate root OID "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(err_out, rc);
		}

		/** only one root oid is needed in the common case */
		roots->cr_oids[1] = DAOS_OBJ_NIL;
		roots->cr_oids[2] = DAOS_OBJ_NIL;
		roots->cr_oids[3] = DAOS_OBJ_NIL;

		entries++;
	}

	/* We always free this prop in the callback - so need to make a copy */
	final_prop = daos_prop_alloc(entries);
	if (final_prop == NULL)
		D_GOTO(err_out, rc = -DER_NOMEM);

	if (prop_in != NULL && prop_in->dpp_nr > 0) {
		rc = daos_prop_copy(final_prop, prop_in);
		if (rc)
			D_GOTO(err_out, rc);
		idx = prop_in->dpp_nr;
	}

	if (prop_in == NULL || entries > prop_in->dpp_nr) {
		if (owner != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_CO_OWNER;
			final_prop->dpp_entries[idx].dpe_str = owner;
			owner = NULL; /* prop is responsible for it now */
			idx++;
		}

		if (owner_grp != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_CO_OWNER_GROUP;
			final_prop->dpp_entries[idx].dpe_str = owner_grp;
			owner_grp = NULL; /* prop is responsible for it now */
			idx++;
		}

		if (roots != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_CO_ROOTS;
			final_prop->dpp_entries[idx].dpe_val_ptr = roots;
			roots = NULL; /* prop is responsible for it now */
			idx++;
		}
	}

	*prop_out = final_prop;

	return rc;

err_out:
	daos_prop_free(final_prop);
	D_FREE(roots);
	D_FREE(owner);
	D_FREE(owner_grp);
	return rc;
}

int
dc_cont_create(tse_task_t *task)
{
	daos_cont_create_t     *args;
	struct daos_prop_entry *entry;
	struct dc_pool	       *pool;
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct cont_task_priv  *tpriv = dc_task_get_priv(task);
	struct cont_args	arg = {0, };
	uuid_t                  null_hdl_uuid;
	int			rc;
	daos_prop_t	       *rpc_prop = NULL;

	args = dc_task_get_args(task);

	if (!daos_uuid_valid(args->uuid))
		/** generate a UUID for the new container */
		uuid_generate(args->uuid);

	entry = daos_prop_entry_get(args->prop, DAOS_PROP_CO_STATUS);
	if (entry != NULL) {
		rc = -DER_INVAL;
		D_ERROR("cannot set DAOS_PROP_CO_STATUS prop for cont_create "
			DF_RC"\n", DP_RC(rc));
		goto err_task;
	}

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	/** duplicate properties and add extra ones (e.g. ownership) */
	rc = dup_cont_create_props(args->poh, &rpc_prop, args->prop);
	if (rc != 0)
		D_GOTO(err_pool, rc);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_prop;
	}

	D_DEBUG(DB_MD, DF_UUID": creating "DF_UUIDF"\n",
		DP_UUID(pool->dp_pool), DP_UUID(args->uuid));

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, args->uuid), DP_RC(rc));
		goto err_tpriv;
	}
	uuid_clear(null_hdl_uuid);
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_CREATE, pool->dp_pool_hdl, args->uuid,
			     null_hdl_uuid, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_tpriv, rc);
	}

	cont_create_in_set_data(rpc, CONT_CREATE, dc_cont_proto_version, rpc_prop);

	arg.pool = pool;
	arg.rpc = rpc;
	arg.prop = rpc_prop;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_create_complete, &arg, sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_prop:
	daos_prop_free(rpc_prop);
err_pool:
	dc_pool_put(pool);
err_task:
	tse_task_complete(task, rc);
	return rc;
}

/* Common function for CONT_DESTROY and CONT_DESTROY_BYLABEL RPCs */
static int
cont_destroy_complete(tse_task_t *task, void *data)
{
	struct cont_args        *arg    = (struct cont_args *)data;
	struct dc_pool		*pool = arg->pool;
	struct cont_destroy_out	*out = crt_reply_get(arg->rpc);
	bool                     reinit = false;
	int			 rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cdo_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while destroying container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cdo_op.co_rc;
	if (rc != 0) {
		D_ERROR("failed to destroy container: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, "completed destroying container\n");

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

int
dc_cont_destroy(tse_task_t *task)
{
	daos_cont_destroy_t     *args;
	struct cont_task_priv   *tpriv = dc_task_get_priv(task);
	struct dc_pool		*pool;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_args	 arg;
	uuid_t                   null_hdl_uuid;
	uuid_t			 uuid;
	crt_opcode_t             opc;
	const char		*label;
	int			 rc;

	args = dc_task_get_args(task);

	if (daos_uuid_valid(args->uuid)) {
		/** Backward compatibility, we are provided a UUID */
		label = NULL;
		uuid_copy(uuid, args->uuid);
	} else if (daos_label_is_valid(args->cont)) {
		/** The provided string is a valid label */
		uuid_clear(uuid);
		label = args->cont;
	} else if (uuid_parse(args->cont, uuid) == 0) {
		/** The provided string was successfully parsed as a UUID */
		label = NULL;
	} else {
		/** neither a label nor a UUID ... try again */
		D_GOTO(err, rc = -DER_INVAL);
	}

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_pool;
	}

	D_DEBUG(DB_MD, DF_UUID": destroying %s: force=%d\n",
		DP_UUID(pool->dp_pool), args->cont ? : "<compat>",
		args->force);

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": %s: cannot find container service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), args->cont ? : "<compat>",
			DP_RC(rc));
		goto err_tpriv;
	}
	opc = label ? CONT_DESTROY_BYLABEL : CONT_DESTROY;
	uuid_clear(null_hdl_uuid);
	rc = cont_req_create(daos_task2ctx(task), &ep, opc, pool->dp_pool_hdl, uuid, null_hdl_uuid,
			     &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_tpriv, rc);
	}

	cont_destroy_in_set_data(rpc, opc, dc_cont_proto_version, args->force, label);

	arg.pool = pool;
	arg.rpc = rpc;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_destroy_complete, &arg, sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_pool:
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	return rc;
}

static void
dc_cont_free(struct dc_cont *dc)
{
	D_ASSERT(daos_hhash_link_empty(&dc->dc_hlink));
	D_RWLOCK_DESTROY(&dc->dc_obj_list_lock);
	D_ASSERT(d_list_empty(&dc->dc_po_list));
	D_ASSERT(d_list_empty(&dc->dc_obj_list));
	D_FREE(dc);
}

static void
dc_cont_hop_free(struct d_hlink *hlink)
{
	struct dc_cont *dc;

	dc = container_of(hlink, struct dc_cont, dc_hlink);
	D_ASSERT(daos_hhash_link_empty(&dc->dc_hlink));
	dc_cont_free(dc);
}

static struct d_hlink_ops cont_h_ops = {
	.hop_free	= dc_cont_hop_free,
};

void
dc_cont_hdl_link(struct dc_cont *dc)
{
	daos_hhash_link_insert(&dc->dc_hlink, DAOS_HTYPE_CO);
}

void
dc_cont_hdl_unlink(struct dc_cont *dc)
{
	daos_hhash_link_delete(&dc->dc_hlink);
}

struct dc_cont *
dc_cont_alloc(const uuid_t uuid)
{
	struct dc_cont *dc;

	D_ALLOC_PTR(dc);
	if (dc == NULL)
		return NULL;

	daos_hhash_hlink_init(&dc->dc_hlink, &cont_h_ops);
	uuid_copy(dc->dc_uuid, uuid);
	D_INIT_LIST_HEAD(&dc->dc_obj_list);
	D_INIT_LIST_HEAD(&dc->dc_po_list);
	if (D_RWLOCK_INIT(&dc->dc_obj_list_lock, NULL) != 0)
		D_FREE(dc);

	return dc;
}

static int
dc_cont_props_init(struct dc_cont *cont)
{
	uint32_t	csum_type = cont->dc_props.dcp_csum_type;
	uint32_t	compress_type = cont->dc_props.dcp_compress_type;
	uint32_t	encrypt_type = cont->dc_props.dcp_encrypt_type;
	bool		dedup_only = false;
	int		rc;

	cont->dc_props.dcp_compress_enabled =
			daos_cont_compress_prop_is_enabled(compress_type);
	cont->dc_props.dcp_encrypt_enabled =
			daos_cont_encrypt_prop_is_enabled(encrypt_type);

	if (csum_type == DAOS_PROP_CO_CSUM_OFF) {
		dedup_only = true;
		csum_type = dedup_get_csum_algo(&cont->dc_props);
	}

	if (!daos_cont_csum_prop_is_enabled(csum_type))
		return 0;

	rc = daos_csummer_init_with_type(&cont->dc_csummer, csum_type,
				    cont->dc_props.dcp_chunksize, 0);

	if (dedup_only)
		dedup_configure_csummer(cont->dc_csummer, &cont->dc_props);

	return rc;
}

struct cont_open_args {
	struct dc_pool		*coa_pool;
	daos_cont_info_t	*coa_info;
	const char		*coa_label;
	crt_rpc_t		*rpc;
	daos_handle_t		 hdl;
	daos_handle_t		*hdlp;
};

struct pmap_refresh_cb_arg {
	struct dc_pool		*pra_pool;
	uint32_t		 pra_pm_ver;
	uint32_t		 pra_retry_nr;
};

static int
pmap_refresh_cb(tse_task_t *task, void *data)
{
	struct pmap_refresh_cb_arg	*cb_arg;
	struct dc_pool			*pool;
	uint32_t			 pm_ver;
	uint64_t			 delay;
	int				 rc = task->dt_result;

	cb_arg = (struct pmap_refresh_cb_arg *)data;
	pool = cb_arg->pra_pool;
	if (rc)
		goto out;

	pm_ver = dc_pool_get_version(pool);
	if (pm_ver < cb_arg->pra_pm_ver) {
		if (cb_arg->pra_retry_nr > 10) {
			/* basically this is impossible, or there is system
			 * issue. Return EAGAIN just for code integrality.
			 */
			rc = -DER_AGAIN;
			D_ERROR(DF_UUID": pmap_refresh cannot get required "
				"version (%d:%d), try again later "DF_RC"\n",
				DP_UUID(pool->dp_pool), pm_ver,
				cb_arg->pra_pm_ver, DP_RC(rc));
			goto out;
		}
		if (cb_arg->pra_retry_nr > 3)
			delay = cb_arg->pra_retry_nr * 10;
		else
			delay = 0;

		rc = tse_task_register_comp_cb(task, pmap_refresh_cb, cb_arg,
					       sizeof(*cb_arg));
		if (rc) {
			D_ERROR(DF_UUID": pmap_refresh version (%d:%d), failed "
				"to reg_comp_cb, "DF_RC"\n",
				DP_UUID(pool->dp_pool), pm_ver,
				cb_arg->pra_pm_ver, DP_RC(rc));
			goto out;
		}

		cb_arg->pra_retry_nr++;
		D_DEBUG(DB_TRACE, DF_UUID": pmap_refresh version (%d:%d), "
			"in %d retry\n", DP_UUID(pool->dp_pool), pm_ver,
			cb_arg->pra_pm_ver, cb_arg->pra_retry_nr);

		rc = tse_task_reinit_with_delay(task, delay);
		if (rc) {
			D_ERROR(DF_UUID": pmap_refresh version (%d:%d), resched"
				" failed, "DF_RC"\n", DP_UUID(pool->dp_pool),
				pm_ver, cb_arg->pra_pm_ver, DP_RC(rc));
			goto out;
		}

		return rc;
	}
out:
	if (rc)
		D_ERROR(DF_UUID": pmap_refresh(task %p) failed, "DF_RC"\n",
			DP_UUID(pool->dp_pool), task, DP_RC(rc));
	dc_pool_put(pool);
	return rc;
}

static int
pmap_refresh(tse_task_t *task, daos_handle_t poh, uint32_t pm_ver)
{
	struct dc_pool			*pool;
	struct pmap_refresh_cb_arg	 cb_arg;
	tse_sched_t			*sched;
	tse_task_t			*ptask = NULL;
	int				 rc;

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	sched = (task != NULL) ? tse_task2sched(task) : NULL;
	rc = dc_pool_create_map_refresh_task(poh, pm_ver, sched, &ptask);
	if (rc != 0)
		goto out;

	cb_arg.pra_pool = pool;
	cb_arg.pra_pm_ver = pm_ver;
	cb_arg.pra_retry_nr = 0;
	rc = tse_task_register_comp_cb(ptask, pmap_refresh_cb, &cb_arg,
				       sizeof(cb_arg));
	if (rc != 0)
		goto out;

	if (task) {
		rc = dc_task_depend(task, 1, &ptask);
		if (rc != 0)
			goto out;
	}

	rc = tse_task_schedule(ptask, true);
	return rc;

out:
	if (rc) {
		dc_pool_put(pool);
		if (ptask)
			dc_pool_abandon_map_refresh_task(ptask);
	}
	return rc;
}

/* NB: common function for CONT_OPEN and CONT_OPEN_BYLABEL RPCs */
static int
cont_open_complete(tse_task_t *task, void *data)
{
	struct cont_open_args	*arg = (struct cont_open_args *)data;
	struct cont_open_out    *out   = crt_reply_get(arg->rpc);
	struct dc_pool		*pool = arg->coa_pool;
	struct cont_task_priv   *tpriv = dc_task_get_priv(task);
	struct dc_cont          *cont  = tpriv->cont;
	time_t			 otime_sec, mtime_sec;
	char			 otime_str[32];
	char			 mtime_str[32];
	uint32_t		 cli_pm_ver;
	bool                     reinit = false;
	int			 rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->coo_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while opening container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->coo_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": failed to open container: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* If open by label, copy the returned UUID into dc_cont structure */
	if (arg->coa_label) {
		struct cont_open_bylabel_out *lbl_out = crt_reply_get(arg->rpc);

		uuid_copy(cont->dc_uuid, lbl_out->colo_uuid);
	}

	cont->dc_min_ver = out->coo_op.co_map_version;
	cli_pm_ver = dc_pool_get_version(pool);
	if (cli_pm_ver < cont->dc_min_ver) {
		rc = pmap_refresh(task, arg->hdl, cont->dc_min_ver);
		if (rc) {
			D_ERROR(DF_CONT": pmap_refresh fail "DF_RC"\n",
				DP_CONT(pool->dp_pool, cont->dc_uuid),
				DP_RC(rc));
			goto out;
		}
	}

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting) {
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		/*
		 * Instead of sending a CONT_CLOSE RPC, we leave this new
		 * container handle on the server side to the POOL_DISCONNECT
		 * effort we are racing with.
		 */
		D_GOTO(out, rc = -DER_NO_HDL);
	}

	d_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = arg->hdl;

	daos_props_2cont_props(out->coo_prop, &cont->dc_props);
	rc = dc_cont_props_init(cont);
	if (rc != 0) {
		D_ERROR("container props failed to initialize\n");
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);
		D_GOTO(out, rc);
	}

	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	dc_cont_hdl_link(cont); /* +1 ref */
	dc_cont2hdl(cont, arg->hdlp); /* +1 ref */

	D_DEBUG(DB_MD, DF_CONT": opened: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_CONT(pool->dp_pool, cont->dc_uuid),
		arg->hdlp->cookie, DP_UUID(cont->dc_cont_hdl));

	if (arg->coa_info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(arg->coa_info->ci_uuid, cont->dc_uuid);
	arg->coa_info->ci_nhandles = out->coo_nhandles;

	arg->coa_info->ci_nsnapshots = out->coo_snap_count;
	arg->coa_info->ci_lsnapshot = out->coo_lsnapshot;
	if (arg->coa_label) {
		struct cont_open_bylabel_out *lbl_out = crt_reply_get(arg->rpc);

		arg->coa_info->ci_md_otime = lbl_out->coo_md_otime;
		arg->coa_info->ci_md_mtime = lbl_out->coo_md_mtime;
	} else {
		arg->coa_info->ci_md_otime = out->coo_md_otime;
		arg->coa_info->ci_md_mtime = out->coo_md_mtime;
	}

	/* log metadata times. NB: ctime_r inserts \n so don't add it in the D_DEBUG format */
	rc = daos_hlc2timestamp(arg->coa_info->ci_md_otime, &otime_sec);
	if (rc)
		goto out;
	rc = daos_hlc2timestamp(arg->coa_info->ci_md_mtime, &mtime_sec);
	if (rc)
		goto out;
	D_ASSERT(otime_str == ctime_r((const time_t *)&otime_sec, otime_str));
	D_ASSERT(mtime_str == ctime_r((const time_t *)&mtime_sec, mtime_str));
	D_DEBUG(DB_MD, DF_CONT":%s: metadata open   time: HLC 0x"DF_X64", %s",
		DP_CONT(pool->dp_pool, cont->dc_uuid), arg->coa_label  ? : "",
			arg->coa_info->ci_md_otime, otime_str);
	D_DEBUG(DB_MD, DF_CONT":%s: metadata modify time: HLC 0x"DF_X64", %s",
		DP_CONT(pool->dp_pool, cont->dc_uuid), arg->coa_label ? : "",
			arg->coa_info->ci_md_mtime, mtime_str);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit) {
		dc_cont_put(tpriv->cont);
		cont_task_destroy_priv(task);
	}
	return rc;
}

static int
dc_cont_open_internal(tse_task_t *task, const char *label, struct dc_pool *pool)
{
	daos_cont_open_t        *args;
	struct cont_task_priv   *tpriv;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_open_args	 arg;
	enum cont_operation	 cont_op;
	uint64_t                 prop_bits; /* properties to retrieve when opening container */
	int			 rc;

	args = dc_task_get_args(task);
	tpriv   = dc_task_get_priv(task);
	cont_op = label ? CONT_OPEN_BYLABEL : CONT_OPEN;

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		if (label)
			D_ERROR(DF_UUID":%s: cannot find container service: "
				DF_RC"\n", DP_UUID(pool->dp_pool),
				label, DP_RC(rc));
		else
			D_ERROR(DF_CONT ": cannot find container service: " DF_RC "\n",
				DP_CONT(pool->dp_pool, tpriv->cont->dc_uuid), DP_RC(rc));
		goto err;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, cont_op, pool->dp_pool_hdl,
			     tpriv->cont->dc_uuid, tpriv->cont->dc_cont_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		goto err;
	}

	/* Fill in remaining components of RPCs (field offsets may vary by protocol version). */
	prop_bits = DAOS_CO_QUERY_PROP_CSUM | DAOS_CO_QUERY_PROP_CSUM_CHUNK |
		    DAOS_CO_QUERY_PROP_DEDUP | DAOS_CO_QUERY_PROP_DEDUP_THRESHOLD |
		    DAOS_CO_QUERY_PROP_REDUN_LVL | DAOS_CO_QUERY_PROP_REDUN_FAC |
		    DAOS_CO_QUERY_PROP_EC_CELL_SZ | DAOS_CO_QUERY_PROP_EC_PDA |
		    DAOS_CO_QUERY_PROP_RP_PDA | DAOS_CO_QUERY_PROP_GLOBAL_VERSION |
		    DAOS_CO_QUERY_PROP_OBJ_VERSION | DAOS_CO_QUERY_PROP_PERF_DOMAIN;
	cont_open_in_set_data(rpc, cont_op, dc_cont_proto_version, tpriv->cont->dc_capas, prop_bits,
			      label);

	arg.coa_pool	= pool;
	arg.coa_info	= args->info;
	arg.coa_label	= label;
	arg.rpc		= rpc;
	arg.hdl		= args->poh;
	arg.hdlp	= args->coh;

	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_open_complete,
				       &arg, sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err:
	D_DEBUG(DB_MD, "failed to open container: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_cont_open(tse_task_t *task)
{
	daos_cont_open_t	*args;
	struct dc_pool		*pool;
	struct cont_task_priv   *tpriv;
	const char		*label;
	uuid_t			 uuid;
	int			 rc;

	args = dc_task_get_args(task);
	tpriv = dc_task_get_priv(task);

	if (args->coh == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	if (daos_uuid_valid(args->uuid)) {
		/** Backward compatibility, we are provided a UUID */
		label = NULL;
		uuid_copy(uuid, args->uuid);
	} else if (daos_label_is_valid(args->cont)) {
		/** The provided string is a valid label */
		uuid_clear(uuid);
		label = args->cont;
	} else if (uuid_parse(args->cont, uuid) == 0) {
		/** The provided string was successfully parsed as a UUID */
		label = NULL;
	} else {
		/** neither a label nor a UUID ... try again */
		D_GOTO(err, rc = -DER_INVAL);
	}

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_pool;
		tpriv->cont = dc_cont_alloc(uuid);
		if (tpriv->cont == NULL)
			D_GOTO(err_tpriv, rc = -DER_NOMEM);
		uuid_generate(tpriv->cont->dc_cont_hdl);
		tpriv->cont->dc_capas = args->flags;
	}

	D_DEBUG(DB_MD, DF_UUID ":%s: opening: hdl=" DF_UUIDF " flags=%x\n", DP_UUID(pool->dp_pool),
		args->cont ?: "<compat>", DP_UUID(tpriv->cont->dc_cont_hdl), args->flags);

	rc = dc_cont_open_internal(task, label, pool);
	if (rc)
		goto err_cont;

	return rc;

err_cont:
	dc_cont_put(tpriv->cont);
err_tpriv:
	cont_task_destroy_priv(task);
err_pool:
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "failed to open container: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct cont_close_args {
	struct dc_pool	*cca_pool;
	struct dc_cont	*cca_cont;
	crt_rpc_t	*rpc;
	daos_handle_t	 hdl;
};

static int
cont_close_complete(tse_task_t *task, void *data)
{
	struct cont_close_args	*arg = (struct cont_close_args *)data;
	struct cont_close_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool          *pool   = arg->cca_pool;
	struct dc_cont		*cont = arg->cca_cont;
	bool                     reinit = false;
	int			 rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cco_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while closing container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cco_op.co_rc;
	if (rc == -DER_NO_HDL) {
		/* The pool connection cannot be found on the server. */
		D_DEBUG(DB_MD, DF_CONT": already disconnected: hdl="DF_UUID
			" pool_hdl="DF_UUID"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid),
			DP_UUID(cont->dc_cont_hdl), DP_UUID(pool->dp_pool_hdl));
		rc = 0;
	} else if (rc == -DER_NONEXIST) {
		/* The container cannot be found on the server. */
		D_DEBUG(DB_MD, DF_CONT": already destroyed: hdl="DF_UUID"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid),
			DP_UUID(cont->dc_cont_hdl));
		rc = 0;
	} else if (rc != 0) {
		D_ERROR("failed to close container: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT": closed: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_CONT(pool->dp_pool, cont->dc_uuid),
		arg->hdl.cookie, DP_UUID(cont->dc_cont_hdl));

	dc_cont_hdl_unlink(cont);
	dc_cont_put(cont);

	/* Remove the container from pool container list */
	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	d_list_del_init(&cont->dc_po_list);
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	daos_csummer_destroy(&cont->dc_csummer);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	dc_cont_put(cont);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

int
dc_cont_close(tse_task_t *task)
{
	daos_cont_close_t      *args;
	daos_handle_t           coh;
	struct dc_pool	       *pool;
	struct dc_cont	       *cont;
	struct cont_task_priv  *tpriv = dc_task_get_priv(task);
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct cont_close_args  arg;
	int			rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");
	coh = args->coh;

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	/* Check if there are not objects opened for this container */
	D_RWLOCK_RDLOCK(&cont->dc_obj_list_lock);
	if (!d_list_empty(&cont->dc_obj_list)) {
		D_RWLOCK_UNLOCK(&cont->dc_obj_list_lock);
		D_ERROR("cannot close container, object not closed.\n");
		D_GOTO(err_cont, rc = -DER_BUSY);
	}
	cont->dc_closing = 1;
	D_RWLOCK_UNLOCK(&cont->dc_obj_list_lock);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DB_MD, DF_CONT": closing: cookie="DF_X64" hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid), coh.cookie,
		DP_UUID(cont->dc_cont_hdl));

	if (cont->dc_slave) {
		daos_csummer_destroy(&cont->dc_csummer);
		dc_cont_hdl_unlink(cont);
		dc_cont_put(cont);

		/* Remove the container from pool container list */
		D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
		d_list_del_init(&cont->dc_po_list);
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

		D_DEBUG(DB_MD, DF_CONT": closed: cookie="DF_X64" hdl="DF_UUID
			"\n", DP_CONT(pool->dp_pool, cont->dc_uuid), coh.cookie,
			DP_UUID(cont->dc_cont_hdl));
		dc_pool_put(pool);
		dc_cont_put(cont);
		tse_task_complete(task, 0);
		return 0;
	}

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_pool;
	}

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_tpriv;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_CLOSE, pool->dp_pool_hdl, cont->dc_uuid,
			     cont->dc_cont_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		goto err_tpriv;
	}

	arg.cca_pool = pool;
	arg.cca_cont = cont;
	arg.rpc = rpc;
	arg.hdl = coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_close_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_pool:
	dc_pool_put(pool);
err_cont:
	dc_cont_put(cont);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "failed to close container handle "DF_X64": %d\n",
		coh.cookie, rc);
	return rc;
}

struct cont_query_args {
	struct dc_pool		*cqa_pool;
	struct dc_cont		*cqa_cont;
	daos_cont_info_t	*cqa_info;
	daos_prop_t		*cqa_prop;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_query_complete(tse_task_t *task, void *data)
{
	struct cont_query_args		*arg = (struct cont_query_args *)data;
	struct cont_query_out           *out   = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cqa_pool;
	struct dc_cont                  *cont  = arg->cqa_cont;
	time_t				 otime_sec, mtime_sec;
	char				 otime_str[32];
	char				 mtime_str[32];
	bool                             reinit = false;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cqo_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while querying container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cqo_op.co_rc;
	if (rc == 0 && arg->cqa_prop != NULL)
		rc = daos_prop_copy(arg->cqa_prop, out->cqo_prop);

	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": failed to query container: %d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT": Queried: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	if (arg->cqa_info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(arg->cqa_info->ci_uuid, cont->dc_uuid);
	arg->cqa_info->ci_nhandles = out->cqo_nhandles;
	arg->cqa_info->ci_nsnapshots = out->cqo_snap_count;
	arg->cqa_info->ci_lsnapshot = out->cqo_lsnapshot;
	arg->cqa_info->ci_md_otime = out->cqo_md_otime;
	arg->cqa_info->ci_md_mtime = out->cqo_md_mtime;

	/* log metadata times. NB: ctime_r inserts \n so don't add it in the D_DEBUG format */
	rc = daos_hlc2timestamp(arg->cqa_info->ci_md_otime, &otime_sec);
	if (rc)
		goto out;
	rc = daos_hlc2timestamp(arg->cqa_info->ci_md_mtime, &mtime_sec);
	if (rc)
		goto out;
	D_ASSERT(otime_str == ctime_r((const time_t *)&otime_sec, otime_str));
	D_ASSERT(mtime_str == ctime_r((const time_t *)&mtime_sec, mtime_str));
	D_DEBUG(DB_MD, DF_CONT": metadata open   time: HLC 0x"DF_X64", %s",
		DP_CONT(pool->dp_pool, cont->dc_uuid), arg->cqa_info->ci_md_otime, otime_str);
	D_DEBUG(DB_MD, DF_CONT": metadata modify time: HLC 0x"DF_X64", %s",
		DP_CONT(pool->dp_pool, cont->dc_uuid), arg->cqa_info->ci_md_mtime, mtime_str);

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

static uint64_t
cont_query_bits(daos_prop_t *prop)
{
	uint64_t		 bits = 0;
	int			 i;

	if (prop == NULL)
		return 0;
	if (prop->dpp_entries == NULL)
		return DAOS_CO_QUERY_PROP_ALL;

	for (i = 0; i < prop->dpp_nr; i++) {
		struct daos_prop_entry	*entry;

		entry = &prop->dpp_entries[i];
		switch (entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			bits |= DAOS_CO_QUERY_PROP_LABEL;
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			bits |= DAOS_CO_QUERY_PROP_LAYOUT_TYPE;
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			bits |= DAOS_CO_QUERY_PROP_LAYOUT_VER;
			break;
		case DAOS_PROP_CO_CSUM:
			bits |= DAOS_CO_QUERY_PROP_CSUM;
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			bits |= DAOS_CO_QUERY_PROP_CSUM_CHUNK;
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			bits |= DAOS_CO_QUERY_PROP_CSUM_SERVER;
			break;
		case DAOS_PROP_CO_DEDUP:
			bits |= DAOS_CO_QUERY_PROP_DEDUP;
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			bits |= DAOS_CO_QUERY_PROP_DEDUP_THRESHOLD;
			break;
		case DAOS_PROP_CO_ALLOCED_OID:
			bits |= DAOS_CO_QUERY_PROP_ALLOCED_OID;
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			bits |= DAOS_CO_QUERY_PROP_REDUN_FAC;
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			bits |= DAOS_CO_QUERY_PROP_REDUN_LVL;
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
			bits |= DAOS_CO_QUERY_PROP_SNAPSHOT_MAX;
			break;
		case DAOS_PROP_CO_COMPRESS:
			bits |= DAOS_CO_QUERY_PROP_COMPRESS;
			break;
		case DAOS_PROP_CO_ENCRYPT:
			bits |= DAOS_CO_QUERY_PROP_ENCRYPT;
			break;
		case DAOS_PROP_CO_ACL:
			bits |= DAOS_CO_QUERY_PROP_ACL;
			break;
		case DAOS_PROP_CO_OWNER:
			bits |= DAOS_CO_QUERY_PROP_OWNER;
			break;
		case DAOS_PROP_CO_OWNER_GROUP:
			bits |= DAOS_CO_QUERY_PROP_OWNER_GROUP;
			break;
		case DAOS_PROP_CO_ROOTS:
			bits |= DAOS_CO_QUERY_PROP_ROOTS;
			break;
		case DAOS_PROP_CO_STATUS:
			bits |= DAOS_CO_QUERY_PROP_CO_STATUS;
			break;
		case DAOS_PROP_CO_EC_CELL_SZ:
			bits |= DAOS_CO_QUERY_PROP_EC_CELL_SZ;
			break;
		case DAOS_PROP_CO_PERF_DOMAIN:
			bits |= DAOS_CO_QUERY_PROP_PERF_DOMAIN;
			break;
		case DAOS_PROP_CO_EC_PDA:
			bits |= DAOS_CO_QUERY_PROP_EC_PDA;
			break;
		case DAOS_PROP_CO_RP_PDA:
			bits |= DAOS_CO_QUERY_PROP_RP_PDA;
			break;
		case DAOS_PROP_CO_GLOBAL_VERSION:
			bits |= DAOS_CO_QUERY_PROP_GLOBAL_VERSION;
			break;
		case DAOS_PROP_CO_SCRUBBER_DISABLED:
			bits |= DAOS_CO_QUERY_PROP_SCRUB_DIS;
			break;
		case DAOS_PROP_CO_OBJ_VERSION:
			bits |= DAOS_CO_QUERY_PROP_OBJ_VERSION;
			break;
		default:
			D_ERROR("ignore bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}
	return bits;
}

int
dc_cont_query(tse_task_t *task)
{
	daos_cont_query_t       *args;
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	struct cont_task_priv   *tpriv = dc_task_get_priv(task);
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_query_args	 arg;
	int			 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_cont;
	}

	D_DEBUG(DB_MD, DF_CONT ": querying: hdl=" DF_UUID " proto_ver=%d\n",
		DP_CONT(pool->dp_pool_hdl, cont->dc_uuid), DP_UUID(cont->dc_cont_hdl),
		dc_cont_proto_version);

	ep.ep_grp  = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_tpriv;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_QUERY, pool->dp_pool_hdl, cont->dc_uuid,
			     cont->dc_cont_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_tpriv, rc);
	}

	cont_query_in_set_data(rpc, CONT_QUERY, dc_cont_proto_version, cont_query_bits(args->prop));

	arg.cqa_pool = pool;
	arg.cqa_cont = cont;
	arg.cqa_info = args->info;
	arg.cqa_prop = args->prop;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_query_complete, &arg, sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to query container: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct cont_set_prop_args {
	struct dc_pool		*cqa_pool;
	struct dc_cont		*cqa_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_set_prop_complete(tse_task_t *task, void *data)
{
	struct cont_set_prop_args       *arg    = (struct cont_set_prop_args *)data;
	struct cont_prop_set_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cqa_pool;
	struct dc_cont			*cont = arg->cqa_cont;
	bool                             reinit = false;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cpso_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while setting prop on container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cpso_op.co_rc;

	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": failed to set prop on container: "
			"%d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT": Set prop: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

int
dc_cont_set_prop(tse_task_t *task)
{
	daos_cont_set_prop_t		*args;
	struct cont_task_priv           *tpriv = dc_task_get_priv(task);
	struct daos_prop_entry		*entry;
	struct daos_co_status            co_stat;
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct cont_set_prop_args	 arg;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (daos_prop_entry_get(args->prop, DAOS_PROP_CO_ALLOCED_OID)) {
		D_ERROR("Can't set OID property on existed container.\n");
		D_GOTO(err, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(args->prop, DAOS_PROP_CO_EC_CELL_SZ)) {
		D_ERROR("Can't set EC cell size on existed container\n");
		D_GOTO(err, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(args->prop, DAOS_PROP_CO_EC_PDA)) {
		D_ERROR("Can't set EC performance domain affinity "
			"on existed container.\n");
		D_GOTO(err, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(args->prop, DAOS_PROP_CO_RP_PDA)) {
		D_ERROR("Can't set RP performance domain affinity "
			"on existed container.\n");
		D_GOTO(err, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(args->prop, DAOS_PROP_CO_PERF_DOMAIN)) {
		D_ERROR("Can't set RP performance domain level "
			"on existed container.\n");
		D_GOTO(err, rc = -DER_NO_PERM);
	}

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DB_MD, DF_CONT": setting props: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	entry = daos_prop_entry_get(args->prop, DAOS_PROP_CO_STATUS);
	if (entry != NULL) {
		daos_prop_val_2_co_status(entry->dpe_val, &co_stat);
		if (co_stat.dcs_status != DAOS_PROP_CO_HEALTHY) {
			rc = -DER_INVAL;
			D_ERROR("To set DAOS_PROP_CO_STATUS property can-only "
				"set dcs_status as DAOS_PROP_CO_HEALTHY to "
				"clear UNCLEAN status, "DF_RC"\n", DP_RC(rc));
			goto err_cont;
		}
		co_stat.dcs_pm_ver = dc_pool_get_version(pool);
		entry->dpe_val = daos_prop_co_status_2_val( &co_stat);
	}

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_cont;
	}

	ep.ep_grp  = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_tpriv;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_PROP_SET, pool->dp_pool_hdl,
			     cont->dc_uuid, cont->dc_cont_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_tpriv, rc);
	}

	cont_prop_set_in_set_data(rpc, CONT_PROP_SET, dc_cont_proto_version, args->prop,
				  pool->dp_pool);

	arg.cqa_pool = pool;
	arg.cqa_cont = cont;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_set_prop_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to set prop on container: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct cont_update_acl_args {
	struct dc_pool		*cua_pool;
	struct dc_cont		*cua_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_update_acl_complete(tse_task_t *task, void *data)
{
	struct cont_update_acl_args     *arg    = (struct cont_update_acl_args *)data;
	struct cont_acl_update_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cua_pool;
	struct dc_cont			*cont = arg->cua_cont;
	bool                             reinit = false;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cauo_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while updating ACL on container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cauo_op.co_rc;

	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": failed to update ACL on container: "
			"%d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT": Update ACL: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

int
dc_cont_update_acl(tse_task_t *task)
{
	daos_cont_update_acl_t          *args;
	struct cont_task_priv           *tpriv = dc_task_get_priv(task);
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct cont_update_acl_args	 arg;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_cont;
	}

	D_DEBUG(DB_MD, DF_CONT": updating ACL: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_tpriv;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_ACL_UPDATE, pool->dp_pool_hdl,
			     cont->dc_uuid, cont->dc_cont_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_tpriv, rc);
	}

	cont_acl_update_in_set_data(rpc, CONT_ACL_UPDATE, dc_cont_proto_version, args->acl);

	arg.cua_pool = pool;
	arg.cua_cont = cont;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_update_acl_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to update ACL on container: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct cont_delete_acl_args {
	struct dc_pool		*cda_pool;
	struct dc_cont		*cda_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_delete_acl_complete(tse_task_t *task, void *data)
{
	struct cont_delete_acl_args     *arg    = (struct cont_delete_acl_args *)data;
	struct cont_acl_delete_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cda_pool;
	struct dc_cont			*cont = arg->cda_cont;
	bool                             reinit = false;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cado_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while deleting ACL on container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cado_op.co_rc;

	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": failed to delete ACL on container: "
			"%d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT": Delete ACL: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		cont_task_destroy_priv(task);
	return rc;
}

int
dc_cont_delete_acl(tse_task_t *task)
{
	daos_cont_delete_acl_t          *args;
	struct cont_task_priv           *tpriv = dc_task_get_priv(task);
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct cont_delete_acl_args	 arg;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto err_cont;
	}

	D_DEBUG(DB_MD, DF_CONT": deleting ACL: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_tpriv;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_ACL_DELETE, pool->dp_pool_hdl,
			     cont->dc_uuid, cont->dc_cont_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_tpriv, rc);
	}

	cont_acl_delete_in_set_data(rpc, CONT_ACL_DELETE, dc_cont_proto_version, args->name,
				    args->type);

	arg.cda_pool = pool;
	arg.cda_cont = cont;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_delete_acl_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_tpriv:
	cont_task_destroy_priv(task);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to delete ACL on container: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct cont_oid_alloc_args {
	struct dc_pool		*coaa_pool;
	struct dc_cont		*coaa_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
	daos_size_t		num_oids;
	uint64_t		*oid;
};

static int
cont_oid_alloc_complete(tse_task_t *task, void *data)
{
	struct cont_oid_alloc_args *arg = (struct cont_oid_alloc_args *)data;
	struct cont_oid_alloc_out *out = crt_reply_get(arg->rpc);
	struct dc_pool *pool = arg->coaa_pool;
	struct dc_cont *cont = arg->coaa_cont;
	int rc = task->dt_result;

	if (daos_rpc_retryable_rc(rc) || rc == -DER_STALE) {
		tse_sched_t *sched = tse_task2sched(task);
		tse_task_t *ptask;
		unsigned int map_version = out->coao_op.co_map_version;

		/** pool map refresh task */
		rc = dc_pool_create_map_refresh_task(arg->coaa_cont->dc_pool_hdl, map_version,
						     sched, &ptask);
		if (rc != 0)
			D_GOTO(out, rc);

		rc = dc_task_depend(task, 1, &ptask);
		if (rc != 0) {
			dc_pool_abandon_map_refresh_task(ptask);
			D_GOTO(out, rc);
		}

		rc = dc_task_resched(task);
		if (rc != 0) {
			dc_pool_abandon_map_refresh_task(ptask);
			D_GOTO(out, rc);
		}

		/* ignore returned value, error is reported by comp_cb */
		tse_task_schedule(ptask, true);
		D_GOTO(out, rc = 0);
	} else if (rc != 0) {
		/** error but non retryable RPC */
		D_ERROR("failed to allocate oids: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->coao_op.co_rc;
	if (rc != 0) {
		D_ERROR("failed to allocate oids: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT": OID ALLOC: using hdl="DF_UUID" oid "DF_U64"/"DF_U64"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid), DP_UUID(cont->dc_cont_hdl),
		out->oid, arg->num_oids);

	if (arg->oid)
		*arg->oid = out->oid;

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

static int
get_tgt_rank(struct dc_pool *pool, unsigned int *rank)
{
	struct pool_target	*tgts = NULL;
	unsigned int		tgt_cnt;
	int			rc;

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	rc = pool_map_find_upin_tgts(pool->dp_map, &tgts, &tgt_cnt);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	if (rc)
		return rc;

	if (tgt_cnt == 0 || tgts == NULL)
		return -DER_INVAL;

	*rank = tgts[d_rand() % tgt_cnt].ta_comp.co_rank;

	D_FREE(tgts);

	return 0;
}

int
dc_cont_alloc_oids(tse_task_t *task)
{
	daos_cont_alloc_oids_t		*args;
	struct cont_oid_alloc_in	*in;
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t                   ep;
	crt_rpc_t			*rpc;
	struct cont_oid_alloc_args       arg;
	int                              rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->num_oids == 0 || args->oid == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DB_MD, DF_CONT": oid allocate: hdl="DF_UUID" num "DF_U64"\n",
		DP_CONT(pool->dp_pool_hdl, cont->dc_uuid), DP_UUID(cont->dc_cont_hdl),
		args->num_oids);

	/** randomly select a rank from the pool map */
	ep.ep_grp = pool->dp_sys->sy_group;
	ep.ep_tag = 0;
	rc = get_tgt_rank(pool, &ep.ep_rank);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_OID_ALLOC, pool->dp_pool_hdl,
			     cont->dc_uuid, cont->dc_cont_hdl, NULL /* req_timep */, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in           = crt_req_get(rpc);
	in->num_oids = args->num_oids;

	arg.coaa_pool	= pool;
	arg.coaa_cont	= cont;
	arg.rpc		= rpc;
	arg.hdl		= args->coh;
	arg.num_oids	= args->num_oids;
	arg.oid		= args->oid;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_oid_alloc_complete, &arg, sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to allocate OIDs: "DF_RC"\n", DP_RC(rc));
	return rc;
}

#define DC_CONT_GLOB_MAGIC	(0x16ca0387)

/* Structure of global buffer for dc_cont */
struct dc_cont_glob {
	/** magic number, DC_CONT_GLOB_MAGIC */
	uint32_t	dcg_magic;
	uint32_t	dcg_padding;
	/** pool connection handle */
	uuid_t		dcg_pool_hdl;
	/** container uuid and capas */
	uuid_t		dcg_uuid;
	uuid_t		dcg_cont_hdl;
	uint64_t	dcg_capas;
	/** specific features */
	uint16_t	dcg_csum_type;
	uint16_t        dcg_encrypt_type;
	uint32_t	dcg_compress_type;
	uint32_t	dcg_csum_chunksize;
	uint32_t        dcg_dedup_th;
	uint32_t	dcg_redun_lvl;
	uint32_t	dcg_redun_fac;
	uint32_t	dcg_ec_cell_sz;
	uint32_t	dcg_ec_pda;
	uint32_t	dcg_rp_pda;
	uint32_t	dcg_perf_domain;
	uint32_t	dcg_global_version;
	uint32_t	dcg_obj_version;
	/** minimal required pool map version, as a fence to make sure after
	 * cont_open/g2l client-side pm_ver >= pm_ver@cont_create.
	 */
	uint32_t	dcg_min_ver;
	uint32_t	dcg_csum_srv_verify:1,
			dcg_dedup_enabled:1,
			dcg_dedup_verify:1;
};

static inline daos_size_t
dc_cont_glob_buf_size()
{
	return sizeof(struct dc_cont_glob);
}

static inline void
swap_co_glob(struct dc_cont_glob *cont_glob)
{
	D_ASSERT(cont_glob != NULL);

	D_SWAP32S(&cont_glob->dcg_magic);
	/* skip cont_glob->dcg_padding */
	/* skip cont_glob->dcg_pool_hdl (uuid_t) */
	/* skip cont_glob->dcg_uuid (uuid_t) */
	/* skip cont_glob->dcg_cont_hdl (uuid_t) */
	D_SWAP64S(&cont_glob->dcg_capas);
}

static int
dc_cont_l2g(daos_handle_t coh, d_iov_t *glob)
{
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	struct dc_cont_glob	*cont_glob;
	daos_size_t		 glob_buf_size;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	glob_buf_size = dc_cont_glob_buf_size();
	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_cont, rc = 0);
	}
	if (glob->iov_buf_len < glob_buf_size) {
		D_DEBUG(DB_MD, "Larger glob buffer needed ("DF_U64" bytes "
			"provided, "DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_cont, rc = -DER_TRUNC);
	}
	glob->iov_len = glob_buf_size;

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	if (pool == NULL)
		D_GOTO(out_cont, rc = -DER_NO_HDL);

	/** init global handle */
	cont_glob = (struct dc_cont_glob *)glob->iov_buf;
	cont_glob->dcg_magic = DC_CONT_GLOB_MAGIC;
	uuid_copy(cont_glob->dcg_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(cont_glob->dcg_uuid, cont->dc_uuid);
	uuid_copy(cont_glob->dcg_cont_hdl, cont->dc_cont_hdl);
	cont_glob->dcg_capas = cont->dc_capas;

	/** transfer container properties */
	cont_glob->dcg_csum_type	= cont->dc_props.dcp_csum_type;
	cont_glob->dcg_csum_chunksize	= cont->dc_props.dcp_chunksize;
	cont_glob->dcg_csum_srv_verify	= cont->dc_props.dcp_srv_verify;
	cont_glob->dcg_dedup_enabled	= cont->dc_props.dcp_dedup_enabled;
	cont_glob->dcg_dedup_verify	= cont->dc_props.dcp_dedup_verify;
	cont_glob->dcg_dedup_th		= cont->dc_props.dcp_dedup_size;
	cont_glob->dcg_compress_type	= cont->dc_props.dcp_compress_type;
	cont_glob->dcg_encrypt_type	= cont->dc_props.dcp_encrypt_type;
	cont_glob->dcg_redun_lvl	= cont->dc_props.dcp_redun_lvl;
	cont_glob->dcg_redun_fac	= cont->dc_props.dcp_redun_fac;
	cont_glob->dcg_ec_cell_sz	= cont->dc_props.dcp_ec_cell_sz;
	cont_glob->dcg_ec_pda		= cont->dc_props.dcp_ec_pda;
	cont_glob->dcg_rp_pda		= cont->dc_props.dcp_rp_pda;
	cont_glob->dcg_perf_domain	= cont->dc_props.dcp_perf_domain;
	cont_glob->dcg_min_ver		= cont->dc_min_ver;
	cont_glob->dcg_global_version	= cont->dc_props.dcp_global_version;
	cont_glob->dcg_obj_version	= cont->dc_props.dcp_obj_version;

	dc_pool_put(pool);
out_cont:
	dc_cont_put(cont);
out:
	if (rc)
		D_ERROR("daos_cont_l2g failed, rc: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_cont_local2global(daos_handle_t coh, d_iov_t *glob)
{
	int	rc = 0;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
	    glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_cont_l2g(coh, glob);

out:
	return rc;
}

static int
dc_cont_g2l(daos_handle_t poh, struct dc_cont_glob *cont_glob,
	    daos_handle_t *coh)
{
	struct dc_pool *pool;
	struct dc_cont *cont;
	uint32_t	pm_ver;
	int		rc = 0;

	D_ASSERT(cont_glob != NULL);
	D_ASSERT(coh != NULL);

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (uuid_compare(pool->dp_pool_hdl, cont_glob->dcg_pool_hdl) != 0) {
		D_ERROR("pool_hdl mismatch, in pool: "DF_UUID", in cont_glob: "
			DF_UUID"\n", DP_UUID(pool->dp_pool_hdl),
			DP_UUID(cont_glob->dcg_pool_hdl));
		D_GOTO(out_pool, rc = -DER_INVAL);
	}

	cont = dc_cont_alloc(cont_glob->dcg_uuid);
	if (cont == NULL)
		D_GOTO(out_pool, rc = -DER_NOMEM);

	uuid_copy(cont->dc_cont_hdl, cont_glob->dcg_cont_hdl);
	cont->dc_capas = cont_glob->dcg_capas;
	cont->dc_slave = 1;

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting || DAOS_FAIL_CHECK(DAOS_CONT_G2L_FAIL)) {
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		D_GOTO(out_cont, rc = -DER_NO_HDL);
	}
	cont->dc_pool_hdl = poh;
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	/** extract container properties */
	cont->dc_props.dcp_dedup_enabled = cont_glob->dcg_dedup_enabled;
	cont->dc_props.dcp_csum_type	 = cont_glob->dcg_csum_type;
	cont->dc_props.dcp_srv_verify	 = cont_glob->dcg_csum_srv_verify;
	cont->dc_props.dcp_chunksize	 = cont_glob->dcg_csum_chunksize;
	cont->dc_props.dcp_dedup_size	 = cont_glob->dcg_dedup_th;
	cont->dc_props.dcp_dedup_verify  = cont_glob->dcg_dedup_verify;
	cont->dc_props.dcp_compress_type = cont_glob->dcg_compress_type;
	cont->dc_props.dcp_encrypt_type	 = cont_glob->dcg_encrypt_type;
	cont->dc_props.dcp_redun_lvl	 = cont_glob->dcg_redun_lvl;
	cont->dc_props.dcp_redun_fac	 = cont_glob->dcg_redun_fac;
	cont->dc_props.dcp_ec_cell_sz	 = cont_glob->dcg_ec_cell_sz;
	cont->dc_props.dcp_ec_pda	 = cont_glob->dcg_ec_pda;
	cont->dc_props.dcp_rp_pda	 = cont_glob->dcg_rp_pda;
	cont->dc_props.dcp_perf_domain	 = cont_glob->dcg_perf_domain;
	cont->dc_min_ver		 = cont_glob->dcg_min_ver;
	cont->dc_props.dcp_global_version = cont_glob->dcg_global_version;
	cont->dc_props.dcp_obj_version = cont_glob->dcg_obj_version;
	rc = dc_cont_props_init(cont);
	if (rc != 0)
		D_GOTO(out_cont, rc);

	pm_ver = dc_pool_get_version(pool);
	if (pm_ver < cont->dc_min_ver) {
		rc = pmap_refresh(NULL, poh, cont->dc_min_ver);
		if (rc) {
			D_ERROR("pool: "DF_UUID" pamp_refresh failed, "
				DF_RC"\n", DP_UUID(pool->dp_pool_hdl),
				DP_RC(rc));
			goto out_cont;
		}
	}

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	d_list_add(&cont->dc_po_list, &pool->dp_co_list);
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	dc_cont_hdl_link(cont); /* +1 ref */
	dc_cont2hdl(cont, coh); /* +1 ref */

	D_DEBUG(DB_MD, DF_UUID": opened "DF_UUID": cookie="DF_X64" hdl="
		DF_UUID" slave\n", DP_UUID(pool->dp_pool),
		DP_UUID(cont->dc_uuid), coh->cookie,
		DP_UUID(cont->dc_cont_hdl));
out_cont:
	dc_cont_put(cont);
out_pool:
	dc_pool_put(pool);
out:
	return rc;
}

int
dc_cont_global2local(daos_handle_t poh, d_iov_t glob, daos_handle_t *coh)
{
	struct dc_cont_glob	*cont_glob;
	int			 rc = 0;

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dc_cont_glob_buf_size()) {
		D_DEBUG(DB_MD, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (coh == NULL) {
		D_DEBUG(DB_MD, "Invalid parameter, NULL coh.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	cont_glob = (struct dc_cont_glob *)glob.iov_buf;
	if (cont_glob->dcg_magic == D_SWAP32(DC_CONT_GLOB_MAGIC)) {
		swap_co_glob(cont_glob);
		D_ASSERT(cont_glob->dcg_magic == DC_CONT_GLOB_MAGIC);

	} else if (cont_glob->dcg_magic != DC_CONT_GLOB_MAGIC) {
		D_ERROR("Bad hgh_magic: %#x.\n", cont_glob->dcg_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (uuid_is_null(cont_glob->dcg_pool_hdl) ||
	    uuid_is_null(cont_glob->dcg_uuid) ||
	    uuid_is_null(cont_glob->dcg_cont_hdl)) {
		D_ERROR("Invalid parameter, pool_hdl/uuid/cont_hdl is null.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_cont_g2l(poh, cont_glob, coh);
	if (rc != 0)
		D_ERROR("dc_cont_g2l failed, rc: "DF_RC"\n", DP_RC(rc));

out:
	return rc;
}

struct cont_req_arg {
	struct dc_pool        *cra_pool;
	struct dc_cont        *cra_cont;
	struct cont_task_priv *cra_tpriv;
	crt_rpc_t             *cra_rpc;
	crt_bulk_t             cra_bulk;
	tse_task_cb_t          cra_callback;
};

enum creq_cleanup_stage {
	CLEANUP_ALL,
	CLEANUP_BULK,
	CLEANUP_RPC,
	CLEANUP_TASK_PRIV,
	CLEANUP_POOL,
	CLEANUP_CONT,
};

static void
cont_req_cleanup(enum creq_cleanup_stage stage, tse_task_t *task, bool free_tpriv,
		 struct cont_req_arg *args)
{
	switch (stage) {
	case CLEANUP_ALL:
		crt_req_decref(args->cra_rpc);
	case CLEANUP_BULK:
		if (args->cra_bulk)
			crt_bulk_free(args->cra_bulk);
	case CLEANUP_RPC:
		crt_req_decref(args->cra_rpc);
	case CLEANUP_TASK_PRIV:
		if (free_tpriv && args->cra_tpriv != NULL) {
			args->cra_tpriv = NULL;
			cont_task_destroy_priv(task);
		}
	case CLEANUP_POOL:
		dc_pool_put(args->cra_pool);
	case CLEANUP_CONT:
		dc_cont_put(args->cra_cont);
	}
}

static int
cont_req_complete(tse_task_t *task, void *data)
{
	struct cont_req_arg *args       = data;
	struct dc_pool      *pool       = args->cra_pool;
	struct dc_cont      *cont       = args->cra_cont;
	struct cont_op_out  *op_out     = crt_reply_get(args->cra_rpc);
	bool                 reinit     = false;
	int                  rc         = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &args->cra_rpc->cr_ep,
					   rc, op_out, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while querying container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = op_out->co_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": failed to access container: %d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT ": Accessed: using hdl=" DF_UUID "\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid), DP_UUID(cont->dc_cont_hdl));

	if (args->cra_callback != NULL)
		args->cra_callback(task, data);
out:
	cont_req_cleanup(CLEANUP_BULK, task, !reinit, args);
	if (reinit) {
		rc = cont_task_reinit(task);
		if (rc != 0 && args->cra_tpriv != NULL) {
			args->cra_tpriv = NULL;
			cont_task_destroy_priv(task);
		}
	}
	return rc;
}

static int
cont_req_prepare(daos_handle_t coh, enum cont_operation opcode, crt_context_t *ctx,
		 tse_task_t *task, struct cont_req_arg *args)
{
	struct cont_task_priv *tpriv = dc_task_get_priv(task);
	crt_endpoint_t         ep;
	int                    rc;

	memset(args, 0, sizeof(*args));
	args->cra_cont = dc_hdl2cont(coh);
	if (args->cra_cont == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);
	args->cra_pool = dc_hdl2pool(args->cra_cont->dc_pool_hdl);
	D_ASSERT(args->cra_pool != NULL);

	if (tpriv == NULL) {
		rc = cont_task_create_priv(task, &tpriv);
		if (rc != 0) {
			cont_req_cleanup(CLEANUP_POOL, task, false /* free_tpriv */, args);
			goto out;
		}
	}
	args->cra_tpriv = tpriv;

	ep.ep_grp  = args->cra_pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */,
				     args->cra_pool->dp_pool,
				     &args->cra_pool->dp_client,
				     &args->cra_pool->dp_client_lock,
				     args->cra_pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(args->cra_pool->dp_pool,
				args->cra_cont->dc_uuid), DP_RC(rc));
		cont_req_cleanup(CLEANUP_TASK_PRIV, task, true /* free_tpriv */, args);
		goto out;
	}

	rc = cont_req_create(ctx, &ep, opcode, args->cra_pool->dp_pool_hdl, args->cra_cont->dc_uuid,
			     args->cra_cont->dc_cont_hdl, &tpriv->rq_time, &args->cra_rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		cont_req_cleanup(CLEANUP_TASK_PRIV, task, true /* free_tpriv */, args);
		goto out;
	}

out:
	return rc;
}

static int
attr_list_req_complete(tse_task_t *task, void *data)
{
	struct cont_req_arg	  *args = data;
	daos_cont_list_attr_t	  *task_args = dc_task_get_args(task);
	struct cont_attr_list_out *out = crt_reply_get(args->cra_rpc);

	*task_args->size = out->calo_size;
	return 0;
}

int
dc_cont_list_attr(tse_task_t *task)
{
	daos_cont_list_attr_t           *args;
	struct cont_req_arg		 cb_args;
	crt_bulk_t                       bulk = NULL;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->size == NULL ||
	    (*args->size > 0 && args->buf == NULL)) {
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = cont_req_prepare(args->coh, CONT_ATTR_LIST, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_CONT": listing attributes: hdl="
			 DF_UUID "; size=%lu\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl), *args->size);

	if (*args->size > 0) {
		d_iov_t iov = {
			.iov_buf     = args->buf,
			.iov_buf_len = *args->size,
			.iov_len     = 0
		};
		d_sg_list_t sgl = {
			.sg_nr_out = 0,
			.sg_nr	   = 1,
			.sg_iovs   = &iov
		};
		rc = crt_bulk_create(daos_task2ctx(task), &sgl, CRT_BULK_RW, &bulk);
		if (rc != 0) {
			cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
			D_GOTO(out, rc);
		}
	}

	cont_attr_list_in_set_data(cb_args.cra_rpc, CONT_ATTR_LIST, dc_cont_proto_version, bulk);

	cb_args.cra_bulk     = bulk;
	cb_args.cra_callback = attr_list_req_complete;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	return daos_rpc_send(cb_args.cra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to list container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

static int
attr_bulk_create(int n, char *names[], void *values[], size_t sizes[],
		 crt_context_t crt_ctx, crt_bulk_perm_t perm, crt_bulk_t *bulk)
{
	int		rc;
	int		i;
	int		j;
	d_sg_list_t	sgl;

	/* Buffers = 'n' names */
	sgl.sg_nr_out	= 0;
	sgl.sg_nr	= n;

	/* + 1 sizes */
	if (sizes != NULL)
		++sgl.sg_nr;

	/* + non-null values */
	if (sizes != NULL && values != NULL) {
		for (j = 0; j < n; j++)
			if (sizes[j] > 0)
				++sgl.sg_nr;
	}

	D_ALLOC_ARRAY(sgl.sg_iovs, sgl.sg_nr);
	if (sgl.sg_iovs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* names */
	for (j = 0, i = 0; j < n; ++j)
		d_iov_set(&sgl.sg_iovs[i++], (void *)(names[j]),
			     strlen(names[j]) + 1 /* trailing '\0' */);

	/* TODO: Add packing/unpacking of non-byte-arrays to rpc.[hc] ? */

	/* sizes */
	if (sizes != NULL)
		d_iov_set(&sgl.sg_iovs[i++], (void *)sizes, n * sizeof(*sizes));

	/* values */
	if (sizes != NULL && values != NULL) {
		for (j = 0; j < n; ++j)
			if (sizes[j] > 0)
				d_iov_set(&sgl.sg_iovs[i++],
					  values[j], sizes[j]);
	}

	rc = crt_bulk_create(crt_ctx, &sgl, perm, bulk);
	D_FREE(sgl.sg_iovs);
out:
	return rc;
}

/*
 * Check for valid inputs. If readonly is true, normalizes
 * by setting corresponding size to zero for NULL values.
 * Otherwise, values may not be NULL.
 */
static int
attr_check_input(int n, char const *const names[], void const *const values[],
		 size_t sizes[], bool readonly)
{
	int i;

	if (n <= 0 || names == NULL || ((sizes == NULL
	    || values == NULL) && !readonly)) {
		D_ERROR("Invalid Arguments: n = %d, names = %p, values = %p, sizes = %p\n", n,
			names, values, sizes);
		return -DER_INVAL;
	}

	for (i = 0; i < n; i++) {
		if (names[i] == NULL || *names[i] == '\0') {
			D_ERROR("Invalid Arguments: names[%d] = %s\n", i,
				names[i] == NULL ? "NULL" : "\'\\0\'");

			return -DER_INVAL;
		}
		if (strnlen(names[i], DAOS_ATTR_NAME_MAX + 1) > DAOS_ATTR_NAME_MAX) {
			D_ERROR("Invalid Arguments: names[%d] size > DAOS_ATTR_NAME_MAX\n", i);
			return -DER_INVAL;
		}
		if (sizes != NULL) {
			if (values == NULL)
				sizes[i] = 0;
			else if (values[i] == NULL || sizes[i] == 0) {
				if (!readonly) {
					D_ERROR(
					    "Invalid Arguments: values[%d] = %p, sizes[%d] = %lu\n",
					    i, values[i], i, sizes[i]);
					return -DER_INVAL;
				}
				sizes[i] = 0;
			}
		}
	}
	return 0;
}

static int
free_heap_copy(tse_task_t *task, void *args)
{
	char *name = *(char **)args;

	D_FREE(name);
	return 0;
}

int
dc_cont_get_attr(tse_task_t *task)
{
	daos_cont_get_attr_t     *args;
	struct cont_req_arg	 cb_args;
	crt_bulk_t                bulk;
	uint64_t                  key_length;
	int			 rc;
	int			 i;
	char			**new_names = NULL;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names,
			      (const void *const*)args->values,
			      (size_t *)args->sizes, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_req_prepare(args->coh, CONT_ATTR_GET, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_CONT": getting attributes: hdl="DF_UUID"\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl));

	key_length = 0;

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out_rpc, rc = -DER_NOMEM);

	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out_rpc, rc);
	}

	for (i = 0 ; i < args->n ; i++) {
		uint64_t len;

		len = strnlen(args->names[i], DAOS_ATTR_NAME_MAX);
		key_length += len + 1;
		D_STRNDUP(new_names[i], args->names[i], len);
		if (new_names[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);

		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out_rpc, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, (void **)args->values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RW, &bulk);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	cont_attr_get_in_set_data(cb_args.cra_rpc, CONT_ATTR_GET, dc_cont_proto_version, args->n,
				  key_length, bulk);

	cb_args.cra_bulk = bulk;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	return daos_rpc_send(cb_args.cra_rpc, task);

out_rpc:
	cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to get container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

int
dc_cont_set_attr(tse_task_t *task)
{
	daos_cont_set_attr_t     *args;
	struct cont_req_arg	 cb_args;
	uint64_t                  count;
	crt_bulk_t                bulk;
	int			 i, rc;
	char			**new_names = NULL;
	void			**new_values = NULL;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, args->values,
			      (size_t *)args->sizes, false);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_req_prepare(args->coh, CONT_ATTR_SET, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_CONT": setting attributes: hdl="DF_UUID"\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl));

	count = args->n;

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out_rpc, rc = -DER_NOMEM);

	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out_rpc, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_STRNDUP(new_names[i], args->names[i], DAOS_ATTR_NAME_MAX);
		if (new_names[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out_rpc, rc);
		}
	}

	/* no easy way to determine if a value storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * value in heap
	 */
	D_ALLOC_ARRAY(new_values, args->n);
	if (!new_values)
		D_GOTO(out_rpc, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_values,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_values);
		D_GOTO(out_rpc, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_ALLOC(new_values[i], args->sizes[i]);
		if (new_values[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		memcpy(new_values[i], args->values[i], args->sizes[i]);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_values[i], sizeof(char *));
		if (rc) {
			D_FREE(new_values[i]);
			D_GOTO(out_rpc, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, new_values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RO, &bulk);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	cont_attr_set_in_set_data(cb_args.cra_rpc, CONT_ATTR_SET, dc_cont_proto_version, count,
				  bulk);

	cb_args.cra_bulk = bulk;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	return daos_rpc_send(cb_args.cra_rpc, task);

out_rpc:
	cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to set container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

int
dc_cont_del_attr(tse_task_t *task)
{
	daos_cont_set_attr_t     *args;
	struct cont_req_arg       cb_args;
	uint64_t                  count;
	crt_bulk_t                bulk;
	int			 i, rc;
	char			**new_names;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, NULL, NULL, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_req_prepare(args->coh, CONT_ATTR_DEL, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_CONT": deleting attributes: hdl="DF_UUID"\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl));

	count = args->n;

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out_rpc, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out_rpc, rc);
	}

	for (i = 0 ; i < args->n ; i++) {
		D_STRNDUP(new_names[i], args->names[i], DAOS_ATTR_NAME_MAX);
		if (new_names[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out_rpc, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, NULL, NULL, daos_task2ctx(task), CRT_BULK_RO,
			      &bulk);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	cont_attr_del_in_set_data(cb_args.cra_rpc, CONT_ATTR_DEL, dc_cont_proto_version, count,
				  bulk);

	cb_args.cra_bulk = bulk;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	return daos_rpc_send(cb_args.cra_rpc, task);

out_rpc:
	cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to del container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct epoch_op_arg {
	/* eoa_req must always be the first member of epoch_op_arg */
	struct cont_req_arg	 eoa_req;
	daos_epoch_t		*eoa_epoch;
};

static int
cont_epoch_op_req_complete(tse_task_t *task, void *data)
{
	struct epoch_op_arg *arg = data;
	int rc;

	rc = cont_req_complete(task, &arg->eoa_req);
	if (rc)
		return rc;

	/* Only assign epoch if the task is really done (i.e., cont_req_complete did not reinit) */
	if (arg->eoa_req.cra_tpriv == NULL) {
		struct cont_epoch_op_out *op_out = crt_reply_get(arg->eoa_req.cra_rpc);

		*arg->eoa_epoch = op_out->ceo_epoch;
	}

	return 0;
}

static int
dc_epoch_op(daos_handle_t coh, crt_opcode_t opc, daos_epoch_t *epoch, unsigned int opts,
	    tse_task_t *task)
{
	struct epoch_op_arg arg;
	daos_epoch_t        epc = 0;
	int                 rc;

	/* Check incoming arguments. For CONT_SNAP_CREATE, epoch is out only. */
	D_ASSERT(epoch != NULL);
	if (opc != CONT_SNAP_CREATE) {
		if (*epoch >= DAOS_EPOCH_MAX)
			D_GOTO(out, rc = -DER_OVERFLOW);
	}

	rc = cont_req_prepare(coh, opc, daos_task2ctx(task), task, &arg.eoa_req);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_MD, DF_CONT ": op=%u; hdl=" DF_UUID ";\n",
		DP_CONT(arg.eoa_req.cra_pool->dp_pool_hdl, arg.eoa_req.cra_cont->dc_uuid), opc,
		DP_UUID(arg.eoa_req.cra_cont->dc_cont_hdl));

	if (opc != CONT_SNAP_CREATE)
		epc = *epoch;
	cont_epoch_op_in_set_data(arg.eoa_req.cra_rpc, opc, dc_cont_proto_version, epc, opts);

	arg.eoa_epoch = epoch;

	rc = tse_task_register_comp_cb(task, cont_epoch_op_req_complete, &arg, sizeof(arg));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &arg.eoa_req);
		goto out;
	}

	crt_req_addref(arg.eoa_req.cra_rpc);
	return daos_rpc_send(arg.eoa_req.cra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "epoch op %u("DF_U64") failed: %d\n", opc, *epoch, rc);
	return rc;
}

int
dc_cont_aggregate(tse_task_t *task)
{
	daos_cont_aggregate_t *args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->epoch == DAOS_EPOCH_MAX) {
		D_ERROR("Invalid epoch: "DF_U64"\n", args->epoch);
		tse_task_complete(task, -DER_INVAL);
		return -DER_INVAL;
	}

	return dc_epoch_op(args->coh, CONT_EPOCH_AGGREGATE, &args->epoch, 0, task);
}

int
dc_cont_rollback(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_cont_subscribe(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_cont_create_snap(tse_task_t *task)
{
	daos_cont_create_snap_t *args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (!(args->opts & DAOS_SNAP_OPT_CR)) {
		D_ERROR("Specified snapshot is not supported\n");
		return -DER_NOSYS;
	}

	if (args->name != NULL) {
		D_ERROR("Named Snapshots not yet supported\n");
		tse_task_complete(task, -DER_NOSYS);
		return -DER_NOSYS;
	}

	if (args->epoch == NULL) {
		tse_task_complete(task, -DER_INVAL);
		return -DER_INVAL;
	}

	return dc_epoch_op(args->coh, CONT_SNAP_CREATE, args->epoch, args->opts, task);
}

int
dc_cont_snap_oit_create(tse_task_t *task)
{
	daos_cont_snap_oit_create_t *args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->name != NULL) {
		D_ERROR("Named Snapshots not yet supported\n");
		tse_task_complete(task, -DER_NOSYS);
		return -DER_NOSYS;
	}

	return dc_epoch_op(args->coh, CONT_SNAP_OIT_CREATE, &args->epoch, 0, task);
}

int
dc_cont_snap_oit_destroy(tse_task_t *task)
{
	daos_cont_snap_oit_create_t *args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->name != NULL) {
		D_ERROR("Named Snapshots not yet supported\n");
		tse_task_complete(task, -DER_NOSYS);
		return -DER_NOSYS;
	}

	return dc_epoch_op(args->coh, CONT_SNAP_OIT_DESTROY, &args->epoch, 0, task);
}

struct get_oit_oid_arg {
	/* eoa_req must always be the first member of epoch_op_arg */
	struct cont_req_arg	 goo_req;
	daos_obj_id_t		*goo_oid;
};

static int
cont_get_oit_oid_req_complete(tse_task_t *task, void *data)
{
	struct get_oit_oid_arg *arg = data;
	int                     rc;

	rc = cont_req_complete(task, &arg->goo_req);
	if (rc)
		return rc;

	if (arg->goo_req.cra_tpriv == NULL) {
		struct cont_snap_oit_oid_get_out *oit_out = crt_reply_get(arg->goo_req.cra_rpc);

		*arg->goo_oid = oit_out->ogo_oid;
	}

	return 0;
}

int dc_cont_snap_oit_oid_get(tse_task_t *task)
{
	daos_cont_snap_oit_oid_get_t	*dc_args;
	struct get_oit_oid_arg           arg;
	daos_epoch_t                     epoch;
	int				 rc;

	dc_args = dc_task_get_args(task);
	D_ASSERTF(dc_args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = cont_req_prepare(dc_args->coh, CONT_SNAP_OIT_OID_GET, daos_task2ctx(task), task,
			      &arg.goo_req);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_MD, DF_CONT": op=%u; hdl="DF_UUID";\n",
		DP_CONT(arg.goo_req.cra_pool->dp_pool_hdl,
			arg.goo_req.cra_cont->dc_uuid), CONT_SNAP_OIT_OID_GET,
		DP_UUID(arg.goo_req.cra_cont->dc_cont_hdl));

	epoch       = dc_args->epoch;
	arg.goo_oid = dc_args->oid;
	cont_snap_oit_oid_get_in_set_data(arg.goo_req.cra_rpc, CONT_SNAP_OIT_OID_GET,
					  dc_cont_proto_version, epoch);

	rc = tse_task_register_comp_cb(task, cont_get_oit_oid_req_complete,
				       &arg, sizeof(arg));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &arg.goo_req);
		goto out;
	}

	crt_req_addref(arg.goo_req.cra_rpc);
	return daos_rpc_send(arg.goo_req.cra_rpc, task);

out:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_cont_destroy_snap(tse_task_t *task)
{
	daos_cont_destroy_snap_t	*args;
	int				rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->epr.epr_lo > args->epr.epr_hi) {
		D_ERROR("Invalid epoch range.\n");
		D_GOTO(err, rc = -DER_INVAL);
	}

	/** TODO - add support for valid epoch ranges. */
	if (args->epr.epr_lo != args->epr.epr_hi || args->epr.epr_lo == 0) {
		D_ERROR("Unsupported epoch range.\n");
		D_GOTO(err, rc = -DER_INVAL);
	}

	return dc_epoch_op(args->coh, CONT_SNAP_DESTROY, &args->epr.epr_lo, 0, task);

err:
	tse_task_complete(task, -DER_INVAL);
	return rc;
}

static int
snap_list_req_complete(tse_task_t *task, void *data)
{
	struct cont_req_arg	  *args = data;
	daos_cont_list_snap_t	  *task_args = dc_task_get_args(task);
	struct cont_snap_list_out *out = crt_reply_get(args->cra_rpc);

	*task_args->nr = out->slo_count;
	task_args->anchor->da_type = DAOS_ANCHOR_TYPE_EOF;

	return 0;
}

int
dc_cont_list_snap(tse_task_t *task)
{
	daos_cont_list_snap_t           *args;
	struct cont_req_arg		 cb_args;
	crt_bulk_t                       bulk = NULL;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->nr == NULL)
		D_GOTO(out, rc = -DER_INVAL);
	if (args->epochs == NULL || *args->nr < 0)
		*args->nr = 0;

	rc = cont_req_prepare(args->coh, CONT_SNAP_LIST, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_CONT": listing snapshots: hdl="
			 DF_UUID": size=%d\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl), *args->nr);

	if (*args->nr > 0) {
		d_iov_t iov = {
			.iov_buf     = args->epochs,
			.iov_buf_len = *args->nr * sizeof(*args->epochs),
			.iov_len     = 0
		};
		d_sg_list_t sgl = {
			.sg_nr_out = 0,
			.sg_nr	   = 1,
			.sg_iovs   = &iov
		};

		/* TODO: iovs for names[] and list_snap bulk create function */

		rc = crt_bulk_create(daos_task2ctx(task), &sgl, CRT_BULK_RW, &bulk);
		if (rc != 0) {
			cont_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
			D_GOTO(out, rc);
		}
	}

	cont_snap_list_in_set_data(cb_args.cra_rpc, CONT_SNAP_LIST, dc_cont_proto_version, bulk);

	cb_args.cra_bulk     = bulk;
	cb_args.cra_callback = snap_list_req_complete;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	return daos_rpc_send(cb_args.cra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to list container snapshots: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

/**
 * Get pool_target by container handle and target index.
 *
 * \param coh [IN]	container handle.
 * \param tgt_idx [IN]	target index.
 * \param tgt [OUT]	pool target pointer.
 *
 * \return		0 if get the pool_target.
 * \return		errno if it does not get the pool_target.
 */
int
dc_cont_tgt_idx2ptr(daos_handle_t coh, uint32_t tgt_idx,
		    struct pool_target **tgt)
{
	struct dc_cont	*dc;
	struct dc_pool	*pool;
	int		 n;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	/* Get map_tgt so that we can have the rank of the target. */
	pool = dc_hdl2pool(dc->dc_pool_hdl);
	D_ASSERT(pool != NULL);
	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	n = pool_map_find_target(pool->dp_map, tgt_idx, tgt);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	dc_pool_put(pool);
	dc_cont_put(dc);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", tgt_idx);
		return -DER_INVAL;
	}
	return 0;
}

/**
 * Get pool_domain by container handle and node id.
 *
 * \param coh [IN]	container handle.
 * \param node_id [IN]	node id.
 * \param dom [OUT]	pool domain pointer.
 *
 * \return		0 if get the pool_target.
 * \return		errno if it does not get the pool_target.
 */
int
dc_cont_node_id2ptr(daos_handle_t coh, uint32_t node_id,
		    struct pool_domain **dom)
{
	struct dc_cont	*dc;
	struct dc_pool	*pool;
	int		 n;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	/* Get node so that we can have the rank of the target. */
	pool = dc_hdl2pool(dc->dc_pool_hdl);
	D_ASSERT(pool != NULL);
	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	n = pool_map_find_ranks(pool->dp_map, node_id, dom);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	dc_pool_put(pool);
	dc_cont_put(dc);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", node_id);
		return -DER_INVAL;
	}
	return 0;
}

int
dc_cont_hdl2uuid(daos_handle_t coh, uuid_t *hdl_uuid, uuid_t *uuid)
{
	struct dc_cont *dc;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	if (hdl_uuid != NULL)
		uuid_copy(*hdl_uuid, dc->dc_cont_hdl);
	if (uuid != NULL)
		uuid_copy(*uuid, dc->dc_uuid);
	dc_cont_put(dc);
	return 0;
}

daos_handle_t
dc_cont_hdl2pool_hdl(daos_handle_t coh)
{
	struct dc_cont	*dc;
	daos_handle_t	 ph;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return DAOS_HDL_INVAL;

	ph = dc->dc_pool_hdl;
	dc_cont_put(dc);
	return ph;
}

int
dc_cont_hdl2redunlvl(daos_handle_t coh, uint32_t *rl)
{
	struct dc_cont	*dc;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	*rl = dc->dc_props.dcp_redun_lvl;
	dc_cont_put(dc);

	return 0;
}

int
dc_cont_hdl2redunfac(daos_handle_t coh, uint32_t *rf)
{
	struct dc_cont	*dc;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	*rf = dc->dc_props.dcp_redun_fac;
	dc_cont_put(dc);

	return 0;
}

int
dc_cont_hdl2globalver(daos_handle_t coh, uint32_t *ver)
{
	struct dc_cont	*dc;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	*ver = dc->dc_props.dcp_global_version;
	dc_cont_put(dc);

	return 0;
}

int
dc_cont_oid2bid(daos_handle_t coh, daos_obj_id_t oid, uint32_t *bid)
{
	struct dc_cont	*dc;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	if (dc->dc_props.dcp_global_version < 2)
		*bid = 0;
	else
		*bid = d_hash_murmur64((unsigned char *)&oid,
				       sizeof(oid), 0) % DAOS_OIT_BUCKET_MAX;
	dc_cont_put(dc);

	return 0;
}

struct daos_csummer *
dc_cont_hdl2csummer(daos_handle_t coh)
{
	struct dc_cont	*dc;
	struct daos_csummer *csum;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return NULL;

	csum = dc->dc_csummer;
	dc_cont_put(dc);

	return csum;
}

struct cont_props
dc_cont_hdl2props(daos_handle_t coh)
{
	struct dc_cont	*dc = NULL;
	struct cont_props result = {0};

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return result;

	result = dc->dc_props;
	dc_cont_put(dc);

	return result;
}

static int
cont_mark_slave(struct d_hlink *link, void *arg)
{
	struct dc_cont *cont;

	cont           = container_of(link, struct dc_cont, dc_hlink);
	cont->dc_slave = 1;

	return 0;
}

int
dc_cont_mark_all_slave(void)
{
	return daos_hhash_traverse(DAOS_HTYPE_CO, cont_mark_slave, NULL);
}
