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
 * ds_cont: Container Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related container metadata.
 */

#define D_LOGFAC DD_FAC(container)

#include <daos_srv/container.h>

#include <daos_api.h>	/* for daos_prop_alloc/_free() */
#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include <daos_srv/rdb.h>
#include <daos_srv/security.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

static int
cont_prop_read(struct rdb_tx *tx, struct cont *cont, uint64_t bits,
	       daos_prop_t **prop_out);

/** Container Property knowledge */

/**
 * This function verifies that the container meets it's redundancy requirements
 * based on the current pool map. The redundancy requirement is measured
 * checking a domain level and counting how many downstream failures it has.
 * if there are more failures for that domain type then allowed restrict
 * container opening.
 *
 * \param pmap  [in]    The pool map referenced by the container.
 * \param props [in]    The container properties, used to get redundancy factor
 *                      and level.
 *
 * \return	0 if the container meets the requirements, negative error code
 *		if it does not.
 */
static int
cont_verify_redun_req(struct pool_map *pmap, daos_prop_t *props)
{
	int		num_failed;
	int		num_allowed_failures;
	int		redun_fac = daos_cont_prop2redunfac(props);
	int		redun_lvl = daos_cont_prop2redunlvl(props);
	int		rc = 0;

	switch (redun_lvl) {
	case DAOS_PROP_CO_REDUN_RACK:
		rc = pool_map_get_failed_cnt(pmap,
					     PO_COMP_TP_RACK);
		break;
	case DAOS_PROP_CO_REDUN_NODE:
		rc = pool_map_get_failed_cnt(pmap,
					     PO_COMP_TP_NODE);
		break;
	default:
		return -DER_INVAL;
	}

	if (rc < 0)
		return rc;

	/**
	 * A pool with a redundancy factor of n can have at most n failures
	 * before pool_open fails and an error is reported.
	 */
	num_failed = rc;
	num_allowed_failures = daos_cont_rf2allowedfailures(redun_fac);
	if (num_allowed_failures < 0)
		return num_allowed_failures;

	if (num_allowed_failures >= num_failed)
		return 0;
	else {
		D_ERROR("Domain contains %d failed "
			"components, allows at most %d", num_failed,
			num_allowed_failures);
		return -DER_INVAL;
	}
}

static int
cont_svc_init(struct cont_svc *svc, const uuid_t pool_uuid, uint64_t id,
	      struct ds_rsvc *rsvc)
{
	int rc;

	uuid_copy(svc->cs_pool_uuid, pool_uuid);
	svc->cs_id = id;
	svc->cs_rsvc = rsvc;

	rc = ABT_rwlock_create(&svc->cs_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create cs_lock: %d\n", rc);
		D_GOTO(err, rc = dss_abterr2der(rc));
	}

	/* cs_root */
	rc = rdb_path_init(&svc->cs_root);
	if (rc != 0)
		D_GOTO(err_lock, rc);
	rc = rdb_path_push(&svc->cs_root, &rdb_path_root_key);
	if (rc != 0)
		D_GOTO(err_root, rc);

	/* cs_conts */
	rc = rdb_path_clone(&svc->cs_root, &svc->cs_conts);
	if (rc != 0)
		D_GOTO(err_root, rc);
	rc = rdb_path_push(&svc->cs_conts, &ds_cont_prop_conts);
	if (rc != 0)
		D_GOTO(err_conts, rc);

	/* cs_hdls */
	rc = rdb_path_clone(&svc->cs_root, &svc->cs_hdls);
	if (rc != 0)
		D_GOTO(err_conts, rc);
	rc = rdb_path_push(&svc->cs_hdls, &ds_cont_prop_cont_handles);
	if (rc != 0)
		D_GOTO(err_hdls, rc);

	return 0;

err_hdls:
	rdb_path_fini(&svc->cs_hdls);
err_conts:
	rdb_path_fini(&svc->cs_conts);
err_root:
	rdb_path_fini(&svc->cs_root);
err_lock:
	ABT_rwlock_free(&svc->cs_lock);
err:
	return rc;
}

static void
cont_svc_fini(struct cont_svc *svc)
{
	rdb_path_fini(&svc->cs_hdls);
	rdb_path_fini(&svc->cs_conts);
	rdb_path_fini(&svc->cs_root);
	ABT_rwlock_free(&svc->cs_lock);
}

int
ds_cont_svc_init(struct cont_svc **svcp, const uuid_t pool_uuid, uint64_t id,
		 struct ds_rsvc *rsvc)
{
	struct cont_svc	       *svc;
	int			rc;

	D_ALLOC_PTR(svc);
	if (svc == NULL)
		return -DER_NOMEM;
	rc = cont_svc_init(svc, pool_uuid, id, rsvc);
	if (rc != 0) {
		D_FREE(svc);
		return rc;
	}
	*svcp = svc;
	return 0;
}

void
ds_cont_svc_fini(struct cont_svc **svcp)
{
	cont_svc_fini(*svcp);
	D_FREE(*svcp);
	*svcp = NULL;
}

static int cont_svc_ec_agg_leader_start(struct cont_svc *svc);
static void cont_svc_ec_agg_leader_stop(struct cont_svc *svc);

void
ds_cont_svc_step_up(struct cont_svc *svc)
{
	int rc;

	D_ASSERT(svc->cs_pool == NULL);
	svc->cs_pool = ds_pool_lookup(svc->cs_pool_uuid);
	D_ASSERT(svc->cs_pool != NULL);

	rc = cont_svc_ec_agg_leader_start(svc);
	if (rc != 0)
		D_ERROR(DF_UUID" start ec agg leader failed: %d\n",
			DP_UUID(svc->cs_pool_uuid), rc);
}

void
ds_cont_svc_step_down(struct cont_svc *svc)
{
	cont_svc_ec_agg_leader_stop(svc);
	D_ASSERT(svc->cs_pool != NULL);
	ds_pool_put(svc->cs_pool);
	svc->cs_pool = NULL;
}

int
cont_svc_lookup_leader(uuid_t pool_uuid, uint64_t id, struct cont_svc **svcp,
		       struct rsvc_hint *hint)
{
	struct cont_svc	       *p;
	int			rc;

	D_ASSERTF(id == 0, DF_U64"\n", id);
	rc = ds_pool_cont_svc_lookup_leader(pool_uuid, &p, hint);
	if (rc != 0)
		return rc;
	D_ASSERT(p != NULL);
	*svcp = p;
	return 0;
}

void
cont_svc_put_leader(struct cont_svc *svc)
{
	ds_rsvc_put_leader(svc->cs_rsvc);
}

int
ds_cont_bcast_create(crt_context_t ctx, struct cont_svc *svc,
		     crt_opcode_t opcode, crt_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->cs_pool, DAOS_CONT_MODULE, opcode,
				    rpc, NULL, NULL);
}

void
ds_cont_wrlock_metadata(struct cont_svc *svc)
{
	ABT_rwlock_wrlock(svc->cs_lock);
}

void
ds_cont_rdlock_metadata(struct cont_svc *svc)
{
	ABT_rwlock_rdlock(svc->cs_lock);
}

void
ds_cont_unlock_metadata(struct cont_svc *svc)
{
	ABT_rwlock_unlock(svc->cs_lock);
}

/**
 * Initialize the container metadata in the combined pool/container service.
 *
 * \param[in]	tx		transaction
 * \param[in]	kvs		root KVS for container metadata
 * \param[in]	pool_uuid	pool UUID
 */
int
ds_cont_init_metadata(struct rdb_tx *tx, const rdb_path_t *kvs,
		      const uuid_t pool_uuid)
{
	struct rdb_kvs_attr	attr;
	int			rc;

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_cont_prop_conts, &attr);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create container KVS: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_cont_prop_cont_handles, &attr);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create container handle KVS: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

	return rc;
}

/* copy \a prop to \a prop_def (duplicated default prop) */
static int
cont_prop_default_copy(daos_prop_t *prop_def, daos_prop_t *prop)
{
	int			 i;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		struct daos_prop_entry	*entry;
		struct daos_prop_entry	*entry_def;

		entry = &prop->dpp_entries[i];
		entry_def = daos_prop_entry_get(prop_def, entry->dpe_type);
		D_ASSERTF(entry_def != NULL, "type %d not found in "
			  "default prop.\n", entry->dpe_type);
		switch (entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			D_FREE(entry_def->dpe_str);
			D_STRNDUP(entry_def->dpe_str, entry->dpe_str,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (entry_def->dpe_str == NULL)
				return -DER_NOMEM;
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
		case DAOS_PROP_CO_LAYOUT_VER:
		case DAOS_PROP_CO_CSUM:
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
		case DAOS_PROP_CO_REDUN_FAC:
		case DAOS_PROP_CO_REDUN_LVL:
		case DAOS_PROP_CO_SNAPSHOT_MAX:
		case DAOS_PROP_CO_COMPRESS:
		case DAOS_PROP_CO_ENCRYPT:
		case DAOS_PROP_CO_DEDUP:
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_CO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl = entry->dpe_val_ptr;

				daos_prop_entry_dup_ptr(entry_def, entry,
							daos_acl_get_size(acl));
				if (entry_def->dpe_val_ptr == NULL)
					return -DER_NOMEM;
			}
			break;
		case DAOS_PROP_CO_OWNER:
		case DAOS_PROP_CO_OWNER_GROUP:
			D_FREE(entry_def->dpe_str);
			D_STRNDUP(entry_def->dpe_str, entry->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_def->dpe_str == NULL)
				return -DER_NOMEM;
			break;
		default:
			D_ASSERTF(0, "bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}
	return 0;
}

static int
cont_prop_write(struct rdb_tx *tx, const rdb_path_t *kvs, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	d_iov_t		 value;
	int			 i;
	int			 rc = 0;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		switch (entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_label,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_layout_type,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_layout_ver,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_CSUM:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_csum, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
				&ds_cont_prop_csum_chunk_size, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
				&ds_cont_prop_csum_server_verify, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_DEDUP:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
				&ds_cont_prop_dedup, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
				&ds_cont_prop_dedup_threshold, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_redun_fac,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_redun_lvl,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_snapshot_max,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_COMPRESS:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_compress,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_ENCRYPT:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_encrypt,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_OWNER:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_owner,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_OWNER_GROUP:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_owner_group,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_CO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl = entry->dpe_val_ptr;

				d_iov_set(&value, acl, daos_acl_get_size(acl));
				rc = rdb_tx_update(tx, kvs, &ds_cont_prop_acl,
						   &value);
				if (rc)
					return rc;
			}
			break;
		default:
			D_ERROR("bad dpe_type %d.\n", entry->dpe_type);
			return -DER_INVAL;
		}
	}

	return rc;
}

static int
cont_create(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	    struct cont_svc *svc, crt_rpc_t *rpc)
{
	struct cont_create_in  *in = crt_req_get(rpc);
	daos_prop_t	       *prop_dup = NULL;
	d_iov_t		key;
	d_iov_t		value;
	struct rdb_kvs_attr	attr;
	rdb_path_t		kvs;
	uint64_t		ghce = 0;
	uint64_t		max_oid = 0;
	int			rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc);

	/* Verify the pool handle capabilities. */
	if (!ds_sec_pool_can_create_cont(pool_hdl->sph_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to create cont\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid));
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	/* Check if a container with this UUID already exists. */
	d_iov_set(&key, in->cci_op.ci_uuid, sizeof(uuid_t));
	d_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(tx, &svc->cs_conts, &key, &value);
	if (rc != -DER_NONEXIST) {
		if (rc == 0)
			D_DEBUG(DF_DSMS, DF_CONT": container already exists\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid,
					in->cci_op.ci_uuid));
		else
			D_ERROR(DF_CONT": container lookup failed: "DF_RC"\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid,
					in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/*
	 * Target-side creations (i.e., vos_cont_create() calls) are
	 * deferred to the time when the container is first successfully
	 * opened.
	 */

	/* Create the container attribute KVS under the container KVS. */
	d_iov_set(&key, in->cci_op.ci_uuid, sizeof(uuid_t));
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &svc->cs_conts, &key, &attr);
	if (rc != 0) {
		D_ERROR(DF_CONT" failed to create container attribute KVS: "
			""DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Create a path to the container attribute KVS. */
	rc = rdb_path_clone(&svc->cs_conts, &kvs);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = rdb_path_push(&kvs, &key);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Create the GHCE and MaxOID properties. */
	d_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_ghce, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": create ghce property failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}
	d_iov_set(&value, &max_oid, sizeof(max_oid));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_max_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": create max_oid property failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}

	/* duplicate the default properties, overwrite it with cont create
	 * parameter and then write to rdb.
	 */
	prop_dup = daos_prop_dup(&cont_prop_default, false);
	if (prop_dup == NULL) {
		D_ERROR(DF_CONT" daos_prop_dup failed.\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid));
		D_GOTO(out_kvs, rc = -DER_NOMEM);
	}
	rc = cont_prop_default_copy(prop_dup, in->cci_prop);
	if (rc != 0) {
		D_ERROR(DF_CONT" cont_prop_default_copy failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}
	rc = cont_prop_write(tx, &kvs, prop_dup);
	if (rc != 0) {
		D_ERROR(DF_CONT" cont_prop_write failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}

	/* Create the snapshot KVS. */
	attr.dsa_class = RDB_KVS_INTEGER;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_prop_snapshots, &attr);
	if (rc != 0) {
		D_ERROR(DF_CONT" failed to create container snapshots KVS: "
			""DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
	}

	/* Create the user attribute KVS. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_attr_user, &attr);
	if (rc != 0) {
		D_ERROR(DF_CONT" failed to create container user attr KVS: "
			""DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
	}

	/* Create the handle index KVS. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_prop_handles, &attr);
	if (rc != 0) {
		D_ERROR(DF_CONT" failed to create container handle index KVS: "
			""DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
	}

out_kvs:
	daos_prop_free(prop_dup);
	rdb_path_fini(&kvs);
out:
	return rc;
}

static int
cont_destroy_bcast(crt_context_t ctx, struct cont_svc *svc,
		   const uuid_t cont_uuid)
{
	struct cont_tgt_destroy_in     *in;
	struct cont_tgt_destroy_out    *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid));

	rc = ds_cont_bcast_create(ctx, svc, CONT_TGT_DESTROY, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tdi_pool_uuid, svc->cs_pool_uuid);
	uuid_copy(in->tdi_uuid, cont_uuid);

	rc = dss_rpc_send(rpc);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_CONT_DESTROY_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to destroy %d targets\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: %d\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
	return rc;
}

/*
 * Doesn't allocate anything new, just passes back pointers to data inside the
 * prop.
 */
static void
get_cont_prop_access_info(daos_prop_t *prop, struct ownership *owner,
			  struct daos_acl **acl)
{
	struct daos_prop_entry	*acl_entry;
	struct daos_prop_entry	*owner_entry;
	struct daos_prop_entry	*owner_grp_entry;

	acl_entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ACL);
	D_ASSERT(acl_entry != NULL);
	D_ASSERT(acl_entry->dpe_val_ptr != NULL);

	owner_entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OWNER);
	D_ASSERT(owner_entry != NULL);
	D_ASSERT(owner_entry->dpe_str != NULL);

	owner_grp_entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OWNER_GROUP);
	D_ASSERT(owner_grp_entry != NULL);
	D_ASSERT(owner_grp_entry->dpe_str != NULL);

	owner->user = owner_entry->dpe_str;
	owner->group = owner_grp_entry->dpe_str;

	*acl = acl_entry->dpe_val_ptr;
}

struct recs_buf {
	struct cont_tgt_close_rec      *rb_recs;
	size_t				rb_recs_size;
	int				rb_nrecs;
};

static int
recs_buf_init(struct recs_buf *buf)
{
	struct cont_tgt_close_rec      *tmp;
	size_t				tmp_size;

	tmp_size = 4096;
	D_ALLOC(tmp, tmp_size);
	if (tmp == NULL)
		return -DER_NOMEM;

	buf->rb_recs = tmp;
	buf->rb_recs_size = tmp_size;
	buf->rb_nrecs = 0;
	return 0;
}

static void
recs_buf_fini(struct recs_buf *buf)
{
	D_FREE(buf->rb_recs);
	buf->rb_recs = NULL;
	buf->rb_recs_size = 0;
	buf->rb_nrecs = 0;
}

/* Make sure buf have enough space for one more element. */
static int
recs_buf_grow(struct recs_buf *buf)
{
	D_ASSERT(buf->rb_recs != NULL);
	D_ASSERT(buf->rb_recs_size > sizeof(*buf->rb_recs));

	if (sizeof(*buf->rb_recs) * (buf->rb_nrecs + 1) > buf->rb_recs_size) {
		struct cont_tgt_close_rec      *recs_tmp;
		size_t				recs_size_tmp;

		recs_size_tmp = buf->rb_recs_size * 2;
		D_ALLOC(recs_tmp, recs_size_tmp);
		if (recs_tmp == NULL)
			return -DER_NOMEM;
		memcpy(recs_tmp, buf->rb_recs, buf->rb_recs_size);
		D_FREE(buf->rb_recs);
		buf->rb_recs = recs_tmp;
		buf->rb_recs_size = recs_size_tmp;
	}

	return 0;
}

static int
find_hdls_by_cont_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct recs_buf	       *buf = arg;
	int			rc;

	if (key->iov_len != sizeof(uuid_t) || val->iov_len != sizeof(char)) {
		D_ERROR("invalid key/value size: key="DF_U64" value="DF_U64"\n",
			key->iov_len, val->iov_len);
		return -DER_IO;
	}

	rc = recs_buf_grow(buf);
	if (rc != 0)
		return rc;

	uuid_copy(buf->rb_recs[buf->rb_nrecs].tcr_hdl, key->iov_buf);
	buf->rb_recs[buf->rb_nrecs].tcr_hce = 0 /* unused */;
	buf->rb_nrecs++;
	return 0;
}

static int cont_close_hdls(struct cont_svc *svc,
			   struct cont_tgt_close_rec *recs, int nrecs,
			   crt_context_t ctx);

static int
evict_hdls(struct rdb_tx *tx, struct cont *cont, bool force, crt_context_t ctx)
{
	struct recs_buf	buf;
	int		rc;

	rc = recs_buf_init(&buf);
	if (rc != 0)
		return rc;

	rc = rdb_tx_iterate(tx, &cont->c_hdls, false /* !backward */,
			    find_hdls_by_cont_cb, &buf);
	if (rc != 0)
		goto out;

	if (buf.rb_nrecs == 0)
		goto out;

	if (!force) {
		rc = -DER_BUSY;
		goto out;
	}

	rc = cont_close_hdls(cont->c_svc, buf.rb_recs, buf.rb_nrecs, ctx);

out:
	recs_buf_fini(&buf);
	return rc;
}

static int
cont_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	     struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_destroy_in *in = crt_req_get(rpc);
	d_iov_t			key;
	int			rc;
	daos_prop_t	       *prop = NULL;
	struct ownership	owner;
	struct daos_acl	       *acl;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: force=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cdi_op.ci_uuid), rpc,
		in->cdi_force);

	/* Fetch the container props to check access for delete */
	rc = cont_prop_read(tx, cont,
			    DAOS_CO_QUERY_PROP_ACL |
			    DAOS_CO_QUERY_PROP_OWNER |
			    DAOS_CO_QUERY_PROP_OWNER_GROUP, &prop);
	if (rc != 0)
		D_GOTO(out, rc);
	D_ASSERT(prop != NULL);

	get_cont_prop_access_info(prop, &owner, &acl);

	/*
	 * Two groups of users can delete a container:
	 * - Users who can delete any container in the pool
	 * - Users who have been given access to delete the specific container
	 */
	if (!ds_sec_pool_can_delete_cont(pool_hdl->sph_sec_capas) &&
	    !ds_sec_cont_can_delete(pool_hdl->sph_flags, &pool_hdl->sph_cred,
				    &owner, acl)) {
		D_ERROR(DF_CONT": permission denied to delete cont\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cdi_op.ci_uuid));
		D_GOTO(out_prop, rc = -DER_NO_PERM);
	}

	rc = evict_hdls(tx, cont, in->cdi_force, rpc->cr_ctx);
	if (rc != 0)
		goto out_prop;

	rc = cont_destroy_bcast(rpc->cr_ctx, cont->c_svc, in->cdi_op.ci_uuid);
	if (rc != 0)
		goto out_prop;

	/* Destroy the handle index KVS. */
	rc = rdb_tx_destroy_kvs(tx, &cont->c_prop, &ds_cont_prop_handles);
	if (rc != 0)
		goto out_prop;

	/* Destroy the user attribute KVS. */
	rc = rdb_tx_destroy_kvs(tx, &cont->c_prop, &ds_cont_attr_user);
	if (rc != 0)
		goto out_prop;

	/* Destroy the snapshot KVS. */
	rc = rdb_tx_destroy_kvs(tx, &cont->c_prop, &ds_cont_prop_snapshots);
	if (rc != 0)
		goto out_prop;

	/* Destroy the container attribute KVS. */
	d_iov_set(&key, in->cdi_op.ci_uuid, sizeof(uuid_t));
	rc = rdb_tx_destroy_kvs(tx, &cont->c_svc->cs_conts, &key);

out_prop:
	daos_prop_free(prop);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cdi_op.ci_uuid), rpc,
		rc);
	return rc;
}

struct cont_ec_agg *
cont_ec_agg_lookup(struct cont_svc *cont_svc, uuid_t cont_uuid)
{
	struct cont_ec_agg *ec_agg;

	d_list_for_each_entry(ec_agg, &cont_svc->cs_ec_agg_list, ea_list) {
		if (uuid_compare(ec_agg->ea_cont_uuid, cont_uuid) == 0)
			return ec_agg;
	}
	return NULL;
}

static int
cont_ec_agg_alloc(struct cont_svc *cont_svc, uuid_t cont_uuid,
		  struct cont_ec_agg **ec_aggp)
{
	struct cont_ec_agg	*ec_agg = NULL;
	struct pool_domain	*doms;
	int			node_nr;
	int			rc = 0;
	int			i;

	D_ALLOC_PTR(ec_agg);
	if (ec_agg == NULL)
		return -DER_NOMEM;

	D_ASSERT(cont_svc->cs_pool->sp_map != NULL);
	node_nr = pool_map_find_nodes(cont_svc->cs_pool->sp_map,
				      PO_COMP_ID_ALL, &doms);
	if (node_nr < 0)
		D_GOTO(out, rc = node_nr);

	D_ALLOC_ARRAY(ec_agg->ea_server_ephs, node_nr);
	if (ec_agg->ea_server_ephs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(ec_agg->ea_cont_uuid, cont_uuid);
	ec_agg->ea_servers_num = node_nr;
	ec_agg->ea_current_eph = 0;
	for (i = 0; i < node_nr; i++) {
		ec_agg->ea_server_ephs[i].rank = doms[i].do_comp.co_rank;
		ec_agg->ea_server_ephs[i].eph = 0;
	}
	d_list_add(&ec_agg->ea_list, &cont_svc->cs_ec_agg_list);
	*ec_aggp = ec_agg;
out:
	if (rc) {
		if (ec_agg && ec_agg->ea_server_ephs)
			D_FREE(ec_agg->ea_server_ephs);
		if (ec_agg)
			D_FREE(ec_agg);
	}

	return rc;
}

static void
cont_ec_agg_destroy(struct cont_ec_agg *ec_agg)
{
	d_list_del(&ec_agg->ea_list);
	if (ec_agg->ea_server_ephs)
		D_FREE(ec_agg->ea_server_ephs);
	D_FREE(ec_agg);
}

/**
 * Update the epoch (by rank) on the leader of container service, which
 * will be called by IV update on the leader.
 */
int
ds_cont_leader_update_agg_eph(uuid_t pool_uuid, uuid_t cont_uuid,
			      d_rank_t rank, daos_epoch_t eph)
{
	struct cont_svc		*svc;
	struct cont_ec_agg	*ec_agg;
	int			rc;
	bool			retried = false;
	int			i;

	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc,
				    NULL /* hint */);
	if (rc != 0)
		return rc;

retry:
	ec_agg = cont_ec_agg_lookup(svc, cont_uuid);
	if (ec_agg == NULL) {
		rc = cont_ec_agg_alloc(svc, cont_uuid, &ec_agg);
		if (rc)
			D_GOTO(out_put, rc);
	}

	for (i = 0; i < ec_agg->ea_servers_num; i++) {
		if (ec_agg->ea_server_ephs[i].rank == rank) {
			if (ec_agg->ea_server_ephs[i].eph < eph)
				ec_agg->ea_server_ephs[i].eph = eph;
			break;
		}
	}

	if (i == ec_agg->ea_servers_num) {
		if (!retried) {
			D_DEBUG(DB_MD, "rank %u eph "DF_U64" retry for"
				DF_CONT"\n", rank, eph,
				DP_CONT(pool_uuid, cont_uuid));
			retried = true;
			cont_ec_agg_destroy(ec_agg);
			goto retry;
		} else {
			D_WARN("rank %u eph "DF_U64" does not exist for "
			       DF_CONT"\n", rank, eph,
			       DP_CONT(pool_uuid, cont_uuid));
		}
	} else {
		D_DEBUG(DB_MD, DF_CONT" update eph rank %u eph "DF_U64"\n",
			DP_CONT(pool_uuid, cont_uuid), rank, eph);
	}

out_put:
	cont_svc_put_leader(svc);
	return 0;
}

struct refresh_vos_agg_eph_arg {
	uuid_t	pool_uuid;
	uuid_t  cont_uuid;
	daos_epoch_t min_eph;
};

int
cont_refresh_vos_agg_eph_one(void *data)
{
	struct refresh_vos_agg_eph_arg *arg = data;
	struct ds_cont_child	*cont_child;
	int			rc;

	rc = ds_cont_child_lookup(arg->pool_uuid, arg->cont_uuid, &cont_child);
	if (rc)
		return rc;

	D_DEBUG(DB_MD, DF_CONT": update aggregation max eph "DF_U64"\n",
		DP_CONT(arg->pool_uuid, arg->cont_uuid), arg->min_eph);
	cont_child->sc_ec_agg_eph_boundry = arg->min_eph;
	ds_cont_child_put(cont_child);
	return rc;
}

int
ds_cont_tgt_refresh_agg_eph(uuid_t pool_uuid, uuid_t cont_uuid,
			    daos_epoch_t eph)
{
	struct refresh_vos_agg_eph_arg	arg;
	int				rc;

	uuid_copy(arg.pool_uuid, pool_uuid);
	uuid_copy(arg.cont_uuid, cont_uuid);
	arg.min_eph = eph;

	rc = dss_task_collective(cont_refresh_vos_agg_eph_one, &arg, 0);
	return rc;
}

#define EC_AGG_EPH_INTV	 (10ULL * 1000)	/* seconds interval to check*/
static void
cont_agg_eph_leader_ult(void *arg)
{
	struct cont_svc		*svc = arg;
	struct ds_pool		*pool = svc->cs_pool;
	struct cont_ec_agg	*ec_agg;
	struct cont_ec_agg	*tmp;
	int			rc = 0;

	while (!dss_ult_exiting(svc->cs_ec_leader_ephs_req)) {
		d_rank_list_t		fail_ranks = { 0 };

		rc = map_ranks_init(pool->sp_map, MAP_RANKS_DOWN,
				    &fail_ranks);
		if (rc) {
			D_ERROR(DF_UUID": ranks init failed: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			goto yield;
		}

		d_list_for_each_entry(ec_agg, &svc->cs_ec_agg_list,
				      ea_list) {
			daos_epoch_t min_eph = DAOS_EPOCH_MAX;
			int	     i;

			for (i = 0; i < ec_agg->ea_servers_num; i++) {
				d_rank_t rank = ec_agg->ea_server_ephs[i].rank;

				if (d_rank_in_rank_list(&fail_ranks, rank)) {
					D_DEBUG(DB_MD, DF_CONT" skip %u\n",
						DP_CONT(svc->cs_pool_uuid,
							ec_agg->ea_cont_uuid),
						rank);
					continue;
				}

				if (ec_agg->ea_server_ephs[i].eph < min_eph)
					min_eph = ec_agg->ea_server_ephs[i].eph;
			}

			D_ASSERTF(min_eph >= ec_agg->ea_current_eph, DF_U64" < "
				  DF_U64"\n", min_eph, ec_agg->ea_current_eph);
			if (min_eph == ec_agg->ea_current_eph)
				continue;

			D_DEBUG(DB_MD, DF_CONT" sync "DF_U64"\n",
				DP_CONT(svc->cs_pool_uuid,
					ec_agg->ea_cont_uuid), min_eph);
			rc = cont_iv_ec_agg_eph_refresh(pool->sp_iv_ns,
						ec_agg->ea_cont_uuid, min_eph);
			if (rc) {
				D_ERROR(DF_CONT": refresh failed: %d\n",
					DP_CONT(svc->cs_pool_uuid,
						ec_agg->ea_cont_uuid), rc);
				continue;
			}
			ec_agg->ea_current_eph = min_eph;
		}

		map_ranks_fini(&fail_ranks);

		if (dss_ult_exiting(svc->cs_ec_leader_ephs_req))
			break;
yield:
		sched_req_sleep(svc->cs_ec_leader_ephs_req, EC_AGG_EPH_INTV);
	}

	D_DEBUG(DF_DSMS, DF_UUID": stop eph ult: rc %d\n",
		DP_UUID(svc->cs_pool_uuid), rc);

	d_list_for_each_entry_safe(ec_agg, tmp, &svc->cs_ec_agg_list, ea_list)
		cont_ec_agg_destroy(ec_agg);

}

static int
cont_svc_ec_agg_leader_start(struct cont_svc *svc)
{
	struct sched_req_attr	attr;
	ABT_thread		ec_eph_leader_ult = ABT_THREAD_NULL;
	int			rc;

	D_INIT_LIST_HEAD(&svc->cs_ec_agg_list);

	rc = dss_ult_create(cont_agg_eph_leader_ult, svc, DSS_XS_SYS,
			    0, 0, &ec_eph_leader_ult);
	if (rc) {
		D_ERROR(DF_UUID" Failed to create aggregation ULT. %d\n",
			DP_UUID(svc->cs_pool_uuid), rc);
		return rc;
	}

	D_ASSERT(ec_eph_leader_ult != ABT_THREAD_NULL);
	sched_req_attr_init(&attr, SCHED_REQ_GC, &svc->cs_pool_uuid);
	svc->cs_ec_leader_ephs_req = sched_req_get(&attr, ec_eph_leader_ult);
	if (svc->cs_ec_leader_ephs_req == NULL) {
		D_ERROR(DF_UUID"Failed to get req for ec eph query ULT\n",
			DP_UUID(svc->cs_pool_uuid));
		ABT_thread_join(ec_eph_leader_ult);
		return -DER_NOMEM;
	}

	return rc;
}

static void
cont_svc_ec_agg_leader_stop(struct cont_svc *svc)
{
	D_DEBUG(DB_MD, DF_UUID" wait for ec agg leader stop\n",
		DP_UUID(svc->cs_pool_uuid));

	if (svc->cs_ec_leader_ephs_req == NULL)
		return;

	D_DEBUG(DB_MD, DF_UUID" Stopping EC query ULT\n",
		DP_UUID(svc->cs_pool_uuid));

	sched_req_wait(svc->cs_ec_leader_ephs_req, true);
	sched_req_put(svc->cs_ec_leader_ephs_req);
	svc->cs_ec_leader_ephs_req = NULL;
}

int
cont_lookup(struct rdb_tx *tx, const struct cont_svc *svc, const uuid_t uuid,
	    struct cont **cont)
{
	struct cont    *p;
	d_iov_t		key;
	d_iov_t		tmp;
	int		rc;

	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));
	d_iov_set(&tmp, NULL, 0);
	/* check if the container exists or not */
	rc = rdb_tx_lookup(tx, &svc->cs_conts, &key, &tmp);
	if (rc != 0)
		D_GOTO(err, rc);

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("Failed to allocate container descriptor\n");
		D_GOTO(err, rc = -DER_NOMEM);
	}

	uuid_copy(p->c_uuid, uuid);
	p->c_svc = (struct cont_svc *)svc;

	/* c_prop */
	rc = rdb_path_clone(&svc->cs_conts, &p->c_prop);
	if (rc != 0)
		D_GOTO(err_p, rc);

	rc = rdb_path_push(&p->c_prop, &key);
	if (rc != 0)
		D_GOTO(err_attrs, rc);

	/* c_snaps */
	rc = rdb_path_clone(&p->c_prop, &p->c_snaps);
	if (rc != 0)
		D_GOTO(err_attrs, rc);
	rc = rdb_path_push(&p->c_snaps, &ds_cont_prop_snapshots);
	if (rc != 0)
		D_GOTO(err_snaps, rc);

	/* c_user */
	rc = rdb_path_clone(&p->c_prop, &p->c_user);
	if (rc != 0)
		D_GOTO(err_snaps, rc);
	rc = rdb_path_push(&p->c_user, &ds_cont_attr_user);
	if (rc != 0)
		D_GOTO(err_user, rc);

	/* c_hdls */
	rc = rdb_path_clone(&p->c_prop, &p->c_hdls);
	if (rc != 0)
		D_GOTO(err_user, rc);
	rc = rdb_path_push(&p->c_hdls, &ds_cont_prop_handles);
	if (rc != 0)
		D_GOTO(err_hdls, rc);

	*cont = p;
	return 0;

err_hdls:
	rdb_path_fini(&p->c_hdls);
err_user:
	rdb_path_fini(&p->c_user);
err_snaps:
	rdb_path_fini(&p->c_snaps);
err_attrs:
	rdb_path_fini(&p->c_prop);
err_p:
	D_FREE(p);
err:
	return rc;
}

void
cont_put(struct cont *cont)
{
	rdb_path_fini(&cont->c_hdls);
	rdb_path_fini(&cont->c_user);
	rdb_path_fini(&cont->c_snaps);
	rdb_path_fini(&cont->c_prop);
	D_FREE(cont);
}

static int
cont_open(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	  crt_rpc_t *rpc)
{
	struct cont_open_in    *in = crt_req_get(rpc);
	d_iov_t			key;
	d_iov_t			value;
	daos_prop_t	       *prop = NULL;
	struct container_hdl	chdl;
	struct pool_map		*pmap;
	struct cont_open_out   *out = crt_reply_get(rpc);
	char			zero = 0;
	int			rc;
	struct ownership	owner;
	struct daos_acl		*acl;
	bool			cont_hdl_opened = false;
	uint64_t		sec_capas = 0;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" flags="
		DF_X64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coi_op.ci_uuid), rpc,
		DP_UUID(in->coi_op.ci_hdl), in->coi_flags);

	/* See if this container handle already exists. */
	d_iov_set(&key, in->coi_op.ci_hdl, sizeof(uuid_t));
	d_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != -DER_NONEXIST) {
		if (rc == 0 && chdl.ch_flags != in->coi_flags) {
			D_ERROR(DF_CONT": found conflicting container handle\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid));
			rc = -DER_EXIST;
		}
		D_GOTO(out, rc);
	}

	/*
	 * Need props to check for pool redundancy requirements and access
	 * control.
	 */
	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop);
	if (rc != 0)
		D_GOTO(out, rc);
	D_ASSERT(prop != NULL);
	D_ASSERT(prop->dpp_nr == CONT_PROP_NUM);

	get_cont_prop_access_info(prop, &owner, &acl);

	rc = ds_sec_cont_get_capabilities(in->coi_flags, &pool_hdl->sph_cred,
					  &owner, acl, &sec_capas);
	if (rc != 0) {
		D_ERROR(DF_CONT": refusing attempt to open with flags "
			DF_X64" error: "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			in->coi_flags, DP_RC(rc));
		daos_prop_free(prop);
		D_GOTO(out, rc);
	}

	if (!ds_sec_cont_can_open(sec_capas)) {
		D_ERROR(DF_CONT": permission denied opening with flags "
			DF_X64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			in->coi_flags);
		daos_prop_free(prop);
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	/* Determine pool meets container redundancy factor requirements */
	if (!(in->coi_flags & DAOS_COO_FORCE)) {
		pmap = pool_hdl->sph_pool->sp_map;
		rc = cont_verify_redun_req(pmap, prop);
		if (rc != 0) {
			D_ERROR(DF_CONT": Container does not meet redundancy "
					"requirements, set DAOS_COO_FORCE to "
					"force container open rc: %d.\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			daos_prop_free(prop);
			D_GOTO(out, rc);
		}
	}

	/* query the container properties from RDB and update to IV */
	rc = cont_iv_prop_update(pool_hdl->sph_pool->sp_iv_ns,
				 in->coi_op.ci_uuid, prop);
	daos_prop_free(prop);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_iv_prop_update failed %d.\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out, rc);
	}

	/* update container capa to IV */
	rc = cont_iv_capability_update(pool_hdl->sph_pool->sp_iv_ns,
				       in->coi_op.ci_hdl, in->coi_op.ci_uuid,
				       in->coi_flags, sec_capas);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_iv_capability_update failed %d.\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out, rc);
	}
	cont_hdl_opened = true;

	if (DAOS_FAIL_CHECK(DAOS_CONT_OPEN_FAIL))
		D_GOTO(out, rc = -DER_IO);

	uuid_copy(chdl.ch_pool_hdl, pool_hdl->sph_uuid);
	uuid_copy(chdl.ch_cont, cont->c_uuid);
	chdl.ch_flags = in->coi_flags;
	chdl.ch_sec_capas = sec_capas;

	rc = ds_cont_epoch_init_hdl(tx, cont, in->coi_op.ci_hdl, &chdl);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * Add the handle to the handle index KVS. The value is unused. (See
	 * the handle index KVS comment in srv_layout.h.)
	 */
	d_iov_set(&value, &zero, sizeof(zero));
	rc = rdb_tx_update(tx, &cont->c_hdls, &key, &value);
	if (rc != 0)
		goto out;

	/**
	 * Put requested properties in output.
	 * the allocated prop will be freed after rpc replied in
	 * ds_cont_op_handler.
	 */
	rc = cont_prop_read(tx, cont, in->coi_prop_bits, &prop);
	out->coo_prop = prop;

out:
	if (rc != 0 && cont_hdl_opened)
		cont_iv_capability_invalidate(pool_hdl->sph_pool->sp_iv_ns,
					      in->coi_op.ci_hdl,
					      CRT_IV_SYNC_EAGER);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coi_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_close_recs(crt_context_t ctx, struct cont_svc *svc,
		struct cont_tgt_close_rec recs[], int nrecs)
{
	int	i;
	int	rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": closing: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64" nrecs=%d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), DP_UUID(recs[0].tcr_hdl),
		recs[0].tcr_hce, nrecs);

	/* update container capa to IV */
	for (i = 0; i < nrecs; i++) {
		rc = cont_iv_capability_invalidate(
				svc->cs_pool->sp_iv_ns,
				recs[i].tcr_hdl, CRT_IV_SYNC_EAGER);
		if (rc)
			D_GOTO(out, rc);
	}

	if (DAOS_FAIL_CHECK(DAOS_CONT_CLOSE_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: hdls[0]="DF_UUID" nhdls=%d: %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), DP_UUID(recs[0].tcr_hdl),
		nrecs, rc);
	return rc;
}

static int
cont_close_one_hdl(struct rdb_tx *tx, struct cont_svc *svc,
		   crt_context_t ctx, const uuid_t uuid)
{
	d_iov_t			key;
	d_iov_t			value;
	struct container_hdl	chdl;
	struct cont	       *cont;
	int			rc;

	/* Look up the handle. */
	d_iov_set(&key, (void *)uuid, sizeof(uuid_t));
	d_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &svc->cs_hdls, &key, &value);
	if (rc != 0)
		return rc;

	rc = cont_lookup(tx, svc, chdl.ch_cont, &cont);
	if (rc != 0)
		return rc;

	rc = ds_cont_epoch_fini_hdl(tx, cont, ctx, &chdl);
	if (rc != 0)
		goto out;

	rc = rdb_tx_delete(tx, &cont->c_hdls, &key);
	if (rc != 0)
		goto out;

	rc = rdb_tx_delete(tx, &svc->cs_hdls, &key);

out:
	cont_put(cont);
	return rc;
}

/* Close an array of handles, possibly belonging to different containers. */
static int
cont_close_hdls(struct cont_svc *svc, struct cont_tgt_close_rec *recs,
		int nrecs, crt_context_t ctx)
{
	int	i;
	int	rc;

	D_ASSERTF(nrecs > 0, "%d\n", nrecs);
	D_DEBUG(DF_DSMS, DF_CONT": closing %d recs: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64"\n", DP_CONT(svc->cs_pool_uuid, NULL),
		nrecs, DP_UUID(recs[0].tcr_hdl), recs[0].tcr_hce);

	rc = cont_close_recs(ctx, svc, recs, nrecs);
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * Use one TX per handle to avoid calling ds_cont_epoch_fini_hdl() more
	 * than once in a TX, in which case we would be attempting to query
	 * uncommitted updates. This could be optimized by adding container
	 * UUIDs into recs[i] and sorting recs[] by container UUIDs. Then we
	 * could maintain a list of deleted LREs and a list of deleted LHEs for
	 * each container while looping, and use the lists to update the GHCE
	 * once for each container. This approach enables us to commit only
	 * once (or when a TX becomes too big).
	 */
	for (i = 0; i < nrecs; i++) {
		struct rdb_tx tx;

		rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term,
				  &tx);
		if (rc != 0)
			break;
		rc = cont_close_one_hdl(&tx, svc, ctx, recs[i].tcr_hdl);
		if (rc != 0) {
			rdb_tx_end(&tx);
			break;
		}
		rc = rdb_tx_commit(&tx);
		rdb_tx_end(&tx);
		if (rc != 0)
			break;
	}

out:
	D_DEBUG(DF_DSMS, DF_CONT": leaving: %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), rc);
	return rc;
}

static int
cont_close(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   crt_rpc_t *rpc)
{
	struct cont_close_in	       *in = crt_req_get(rpc);
	d_iov_t			key;
	d_iov_t			value;
	struct container_hdl		chdl;
	struct cont_tgt_close_rec	rec;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		DP_UUID(in->cci_op.ci_hdl));

	/* See if this container handle is already closed. */
	d_iov_set(&key, in->cci_op.ci_hdl, sizeof(uuid_t));
	d_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DF_DSMS, DF_CONT": already closed: "DF_UUID"\n",
				DP_CONT(cont->c_svc->cs_pool->sp_uuid,
					cont->c_uuid),
				DP_UUID(in->cci_op.ci_hdl));
			rc = 0;
		}
		D_GOTO(out, rc);
	}

	uuid_copy(rec.tcr_hdl, in->cci_op.ci_hdl);
	rec.tcr_hce = chdl.ch_hce;

	D_DEBUG(DF_DSMS, DF_CONT": closing: hdl="DF_UUID" hce="DF_U64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, in->cci_op.ci_uuid),
		DP_UUID(rec.tcr_hdl), rec.tcr_hce);

	rc = cont_close_recs(rpc->cr_ctx, cont->c_svc, &rec, 1 /* nrecs */);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_close_one_hdl(tx, cont->c_svc, rpc->cr_ctx, rec.tcr_hdl);

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_query_bcast(crt_context_t ctx, struct cont *cont, const uuid_t pool_hdl,
		 const uuid_t cont_hdl, struct cont_query_out *query_out)
{
	struct	cont_tgt_query_in	*in;
	struct  cont_tgt_query_out	*out;
	crt_rpc_t			*rpc;
	int				 rc;

	D_DEBUG(DF_DSMS,
		DF_CONT"bcasting pool_hld="DF_UUID" cont_hdl ="DF_UUID"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(pool_hdl), DP_UUID(cont_hdl));

	rc = ds_cont_bcast_create(ctx, cont->c_svc, CONT_TGT_QUERY, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tqi_pool_uuid, pool_hdl);
	uuid_copy(in->tqi_cont_uuid, cont->c_uuid);
	out = crt_reply_get(rpc);
	out->tqo_hae = DAOS_EPOCH_MAX;

	rc = dss_rpc_send(rpc);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_CONT_QUERY_FAIL_CORPC))
		rc = -DER_TIMEDOUT;
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc  = out->tqo_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMS, DF_CONT": failed to query %d targets\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out_rpc, rc = -DER_IO);
	} else {
		query_out->cqo_hae = out->tqo_hae;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

static int
cont_prop_read(struct rdb_tx *tx, struct cont *cont, uint64_t bits,
	       daos_prop_t **prop_out)
{
	daos_prop_t	*prop = NULL;
	d_iov_t		 value;
	uint64_t	 val, bitmap;
	uint32_t	 idx = 0, nr = 0;
	int		 rc = 0;

	bitmap = bits & DAOS_CO_QUERY_PROP_ALL;
	while (idx < DAOS_CO_QUERY_PROP_BITS_NR) {
		if (bitmap & 0x1)
			nr++;
		idx++;
		bitmap = bitmap >> 1;
	};

	if (nr == 0)
		return 0;
	D_ASSERT(nr <= DAOS_CO_QUERY_PROP_BITS_NR);
	prop = daos_prop_alloc(nr);
	if (prop == NULL)
		return -DER_NOMEM;

	idx = 0;
	if (bits & DAOS_CO_QUERY_PROP_LABEL) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_label,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		if (value.iov_len > DAOS_PROP_LABEL_MAX_LEN) {
			D_ERROR("bad label length %zu (> %d).\n", value.iov_len,
				DAOS_PROP_LABEL_MAX_LEN);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_LABEL;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_LAYOUT_TYPE) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_layout_type,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_LAYOUT_VER) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_layout_ver,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_LAYOUT_VER;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_CSUM) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_csum,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_CSUM;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_CSUM_CHUNK) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop,
			&ds_cont_prop_csum_chunk_size, &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_CSUM_SERVER) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop,
			&ds_cont_prop_csum_server_verify, &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type =
			DAOS_PROP_CO_CSUM_SERVER_VERIFY;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_REDUN_FAC) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_redun_fac,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_REDUN_FAC;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_REDUN_LVL) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_redun_lvl,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_REDUN_LVL;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_SNAPSHOT_MAX) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop,
				   &ds_cont_prop_snapshot_max, &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_COMPRESS) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_compress,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_COMPRESS;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_ENCRYPT) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_encrypt,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_ENCRYPT;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_ACL) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_acl,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_ACL;
		D_ALLOC(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf_len);
		if (prop->dpp_entries[idx].dpe_val_ptr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		memcpy(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf,
		       value.iov_buf_len);
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_OWNER) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_owner,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		if (value.iov_len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
			D_ERROR("bad owner length %zu (> %d).\n", value.iov_len,
				DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_GOTO(out, rc = -DER_IO);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_OWNER;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_OWNER_GROUP) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_owner_group,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		if (value.iov_len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
			D_ERROR("bad owner group length %zu (> %d).\n",
				value.iov_len,
				DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_GOTO(out, rc = -DER_IO);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_DEDUP) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_dedup,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_DEDUP;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_DEDUP_THRESHOLD) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop,
				   &ds_cont_prop_dedup_threshold,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
out:
	if (rc)
		daos_prop_free(prop);
	else
		*prop_out = prop;
	return rc;
}

static bool
hdl_has_query_access(struct container_hdl *hdl, struct cont *cont,
		     uint64_t query_bits)
{
	uint64_t	prop_bits;
	uint64_t	ownership_bits;

	if ((query_bits & DAOS_CO_QUERY_PROP_ALL) &&
	    !ds_sec_cont_can_get_props(hdl->ch_sec_capas) &&
	    !ds_sec_cont_can_get_acl(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied, no access to props\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return false;
	}

	/* ACL access is managed separately from the other props */
	if ((query_bits & DAOS_CO_QUERY_PROP_ACL) &&
	    !ds_sec_cont_can_get_acl(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to get ACL\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return false;
	}

	/*
	 * Ownership can be accessed with either ACL or general prop access.
	 * Don't need both or any particular one, so it's excluded from the
	 * more specific checks.
	 */
	ownership_bits = DAOS_CO_QUERY_PROP_OWNER |
			 DAOS_CO_QUERY_PROP_OWNER_GROUP;

	/* All remaining props */
	prop_bits = (DAOS_CO_QUERY_PROP_ALL &
		     ~(DAOS_CO_QUERY_PROP_ACL | ownership_bits));
	if ((query_bits & prop_bits) &&
	    !ds_sec_cont_can_get_props(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to get props\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return false;
	}

	return true;
}

static int
cont_query(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_query_in   *in  = crt_req_get(rpc);
	struct cont_query_out  *out = crt_reply_get(rpc);
	daos_prop_t	       *prop = NULL;
	int			rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cqi_op.ci_uuid), rpc,
		DP_UUID(in->cqi_op.ci_hdl));

	if (!hdl_has_query_access(hdl, cont, in->cqi_bits))
		return -DER_NO_PERM;

	if (in->cqi_bits & DAOS_CO_QUERY_TGT) {
		rc = cont_query_bcast(rpc->cr_ctx, cont, in->cqi_op.ci_pool_hdl,
				      in->cqi_op.ci_hdl, out);
		if (rc)
			return rc;
	}

	/* Caller didn't actually ask for any props */
	if ((in->cqi_bits & DAOS_CO_QUERY_PROP_ALL) == 0)
		return 0;

	/* the allocated prop will be freed after rpc replied in
	 * ds_cont_op_handler.
	 */
	rc = cont_prop_read(tx, cont, in->cqi_bits, &prop);
	out->cqo_prop = prop;

	if (DAOS_FAIL_CHECK(DAOS_FORCE_PROP_VERIFY)) {
		daos_prop_t		*iv_prop = NULL;
		struct daos_prop_entry	*entry, *iv_entry;
		int			i;

		D_ALLOC_PTR(iv_prop);
		if (iv_prop == NULL)
			return -DER_NOMEM;

		rc = cont_iv_prop_fetch(pool_hdl->sph_pool->sp_iv_ns,
					in->cqi_op.ci_uuid, iv_prop);
		if (rc) {
			D_ERROR("cont_iv_prop_fetch failed "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		for (i = 0; i < prop->dpp_nr; i++) {
			entry = &prop->dpp_entries[i];
			iv_entry = daos_prop_entry_get(iv_prop,
						       entry->dpe_type);
			D_ASSERT(iv_entry != NULL);
			switch (entry->dpe_type) {
			case DAOS_PROP_CO_LABEL:
				D_ASSERT(strlen(entry->dpe_str) <=
					 DAOS_PROP_LABEL_MAX_LEN);
				if (strncmp(entry->dpe_str, iv_entry->dpe_str,
					    DAOS_PROP_LABEL_MAX_LEN) != 0) {
					D_ERROR("label mismatch %s - %s.\n",
						entry->dpe_str,
						iv_entry->dpe_str);
					rc = -DER_IO;
				}
				break;
			case DAOS_PROP_CO_LAYOUT_TYPE:
			case DAOS_PROP_CO_LAYOUT_VER:
			case DAOS_PROP_CO_CSUM:
			case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			case DAOS_PROP_CO_REDUN_FAC:
			case DAOS_PROP_CO_REDUN_LVL:
			case DAOS_PROP_CO_SNAPSHOT_MAX:
			case DAOS_PROP_CO_COMPRESS:
			case DAOS_PROP_CO_ENCRYPT:
			case DAOS_PROP_CO_DEDUP:
			case DAOS_PROP_CO_DEDUP_THRESHOLD:
				if (entry->dpe_val != iv_entry->dpe_val) {
					D_ERROR("type %d mismatch "DF_U64" - "
						DF_U64".\n", entry->dpe_type,
						entry->dpe_val,
						iv_entry->dpe_val);
					rc = -DER_IO;
				}
				break;
			case DAOS_PROP_CO_ACL:
				if (daos_prop_entry_cmp_acl(entry, iv_entry)
				    != 0)
					rc = -DER_IO;
				break;
			case DAOS_PROP_CO_OWNER:
			case DAOS_PROP_CO_OWNER_GROUP:
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
			default:
				D_ASSERTF(0, "bad dpe_type %d\n",
					  entry->dpe_type);
				break;
			};
		}
		daos_prop_free(iv_prop);
	}

	return rc;
}

static bool
has_non_access_props(daos_prop_t *prop)
{
	uint32_t i;

	for (i = 0; i < prop->dpp_nr; i++) {
		uint32_t type = prop->dpp_entries[i].dpe_type;

		if ((type != DAOS_PROP_CO_ACL) &&
		    (type != DAOS_PROP_CO_OWNER) &&
		    (type != DAOS_PROP_CO_OWNER_GROUP))
			return true;
	}

	return false;
}

static bool
capas_can_set_prop(struct cont *cont, uint64_t sec_capas,
		 daos_prop_t *prop)
{
	struct daos_prop_entry	*acl_entry;
	struct daos_prop_entry	*owner_entry;
	struct daos_prop_entry	*grp_entry;

	/*
	 * Changing ACL prop requires special permissions
	 */
	acl_entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ACL);
	if ((acl_entry != NULL) &&
	    !ds_sec_cont_can_set_acl(sec_capas)) {
		D_ERROR(DF_CONT": permission denied for set-ACL\n",
			DP_CONT(cont->c_svc->cs_pool_uuid,
				cont->c_uuid));
		return false;
	}

	/*
	 * Changing ownership-related props requires special permissions
	 */
	owner_entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OWNER);
	grp_entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OWNER_GROUP);
	if (((owner_entry != NULL) || (grp_entry != NULL)) &&
	    !ds_sec_cont_can_set_owner(sec_capas)) {
		D_ERROR(DF_CONT": permission denied for set-owner\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return false;
	}

	/*
	 * General (non-access-related) props requires the general set-prop
	 * permission
	 */
	if (has_non_access_props(prop) &&
	    !ds_sec_cont_can_set_props(sec_capas)) {
		D_ERROR(DF_CONT": permission denied for set-props\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return false;
	}

	return true;
}

static int
set_prop(struct rdb_tx *tx, struct ds_pool *pool,
	 struct cont *cont, uint64_t sec_capas, uuid_t hdl_uuid,
	 daos_prop_t *prop_in)
{
	int		rc;
	daos_prop_t	*prop_old = NULL;
	daos_prop_t	*prop_iv = NULL;

	if (!daos_prop_valid(prop_in, false, true))
		D_GOTO(out, rc = -DER_INVAL);

	if (!capas_can_set_prop(cont, sec_capas, prop_in))
		D_GOTO(out, rc = -DER_NO_PERM);

	/* Read all props for prop IV update */
	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop_old);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read prop for cont, rc=%d\n",
			DP_UUID(cont->c_uuid), rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(prop_old != NULL);
	prop_iv = daos_prop_merge(prop_old, prop_in);
	if (prop_iv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = cont_prop_write(tx, &cont->c_prop, prop_in);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Update prop IV with merged prop */
	rc = cont_iv_prop_update(pool->sp_iv_ns, cont->c_uuid, prop_iv);
	if (rc)
		D_ERROR(DF_UUID": failed to update prop IV for cont, "
			"%d.\n", DP_UUID(cont->c_uuid), rc);

out:
	daos_prop_free(prop_old);
	daos_prop_free(prop_iv);
	return rc;
}

int
ds_cont_prop_set(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		 struct cont *cont, struct container_hdl *hdl,
		 crt_rpc_t *rpc)
{
	struct cont_prop_set_in		*in  = crt_req_get(rpc);
	daos_prop_t			*prop_in = in->cpsi_prop;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cpsi_op.ci_uuid), rpc,
		DP_UUID(in->cpsi_op.ci_hdl));

	return set_prop(tx, pool_hdl->sph_pool, cont, hdl->ch_sec_capas,
			in->cpsi_op.ci_hdl, prop_in);
}

static int
get_acl(struct rdb_tx *tx, struct cont *cont, struct daos_acl **acl)
{
	int			rc;
	struct daos_prop_entry	*entry;
	daos_prop_t		*acl_prop = NULL;

	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ACL, &acl_prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read ACL prop for cont, rc=%d\n",
			DP_UUID(cont->c_uuid), rc);
		D_GOTO(out, rc);
	}

	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_ACL);
	if (entry == NULL) {
		D_ERROR(DF_UUID": cont prop read didn't return ACL property\n",
			DP_UUID(cont->c_uuid));
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	*acl = daos_acl_dup(entry->dpe_val_ptr);
	if (*acl == NULL) {
		D_ERROR(DF_UUID": couldn't copy container's ACL for "
			"modification\n",
			DP_UUID(cont->c_uuid));
		D_GOTO(out, rc = -DER_NOMEM);
	}

out:
	daos_prop_free(acl_prop);
	return rc;
}

static int
set_acl(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	struct cont *cont, struct container_hdl *hdl, uuid_t hdl_uuid,
	struct daos_acl *acl)
{
	daos_prop_t	*prop = NULL;
	int		rc;

	prop = daos_prop_alloc(1);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ACL;
	prop->dpp_entries[0].dpe_val_ptr = daos_acl_dup(acl);

	rc = set_prop(tx, pool_hdl->sph_pool, cont, hdl->ch_sec_capas,
		      hdl_uuid, prop);
	daos_prop_free(prop);

	return rc;
}

int
ds_cont_acl_update(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		   struct cont *cont, struct container_hdl *hdl,
		   crt_rpc_t *rpc)
{
	struct cont_acl_update_in	*in  = crt_req_get(rpc);
	int				rc = 0;
	struct daos_acl			*acl_in;
	struct daos_acl			*acl = NULL;
	struct daos_ace			*ace;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->caui_op.ci_uuid), rpc,
		DP_UUID(in->caui_op.ci_hdl));

	acl_in = in->caui_acl;
	if (daos_acl_cont_validate(acl_in) != 0)
		D_GOTO(out, rc = -DER_INVAL);

	rc = get_acl(tx, cont, &acl);
	if (rc != 0)
		D_GOTO(out, rc);

	ace = daos_acl_get_next_ace(acl_in, NULL);
	while (ace != NULL) {
		rc = daos_acl_add_ace(&acl, ace);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to add/update ACEs\n",
				DP_UUID(cont->c_uuid));
			D_GOTO(out_acl, rc);
		}

		ace = daos_acl_get_next_ace(acl_in, ace);
	}

	/* Just need to re-set the ACL prop with the merged ACL */
	rc = set_acl(tx, pool_hdl, cont, hdl, in->caui_op.ci_hdl, acl);

out_acl:
	daos_acl_free(acl);
out:
	return rc;
}

int
ds_cont_acl_delete(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		   struct cont *cont, struct container_hdl *hdl,
		   crt_rpc_t *rpc)
{
	struct cont_acl_delete_in	*in  = crt_req_get(rpc);
	int				rc = 0;
	struct daos_acl			*acl = NULL;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cadi_op.ci_uuid), rpc,
		DP_UUID(in->cadi_op.ci_hdl));

	rc = get_acl(tx, cont, &acl);
	if (rc != 0)
		D_GOTO(out, rc);

	/* remove principal's entry from current ACL */
	rc = daos_acl_remove_ace(&acl, in->cadi_principal_type,
				 in->cadi_principal_name);
	if (rc != 0) {
		D_ERROR("Unable to remove ACE from ACL\n");
		D_GOTO(out_acl, rc);
	}

	/* Re-set the ACL prop with the updated ACL */
	rc = set_acl(tx, pool_hdl, cont, hdl, in->cadi_op.ci_hdl, acl);

out_acl:
	daos_acl_free(acl);
out:
	return rc;
}

static int
cont_attr_set(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	      struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_attr_set_in		*in = crt_req_get(rpc);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->casi_op.ci_uuid),
		rpc, DP_UUID(in->casi_op.ci_hdl));

	if (!ds_sec_cont_can_write_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to set container attr\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->casi_op.ci_uuid));
		return -DER_NO_PERM;
	}

	return ds_rsvc_set_attr(cont->c_svc->cs_rsvc, tx, &cont->c_user,
				in->casi_bulk, rpc, in->casi_count);
}

static int
cont_attr_del(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	      struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_attr_del_in		*in = crt_req_get(rpc);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cadi_op.ci_uuid),
		rpc, DP_UUID(in->cadi_op.ci_hdl));

	if (!ds_sec_cont_can_write_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to del container attr\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cadi_op.ci_uuid));
		return -DER_NO_PERM;
	}

	return ds_rsvc_del_attr(cont->c_svc->cs_rsvc, tx, &cont->c_user,
				in->cadi_bulk, rpc, in->cadi_count);
}

static int
cont_attr_get(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	      struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_attr_get_in		*in = crt_req_get(rpc);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cagi_op.ci_uuid),
		rpc, DP_UUID(in->cagi_op.ci_hdl));

	if (!ds_sec_cont_can_read_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to get container attr\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cagi_op.ci_uuid));
		return -DER_NO_PERM;
	}

	return ds_rsvc_get_attr(cont->c_svc->cs_rsvc, tx, &cont->c_user,
				in->cagi_bulk, rpc, in->cagi_count,
				in->cagi_key_length);
}

static int
cont_attr_list(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	      struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_attr_list_in	*in	    = crt_req_get(rpc);
	struct cont_attr_list_out	*out	    = crt_reply_get(rpc);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cali_op.ci_uuid),
		rpc, DP_UUID(in->cali_op.ci_hdl));

	if (!ds_sec_cont_can_read_data(hdl->ch_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to list container attr\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cali_op.ci_uuid));
		return -DER_NO_PERM;
	}

	return ds_rsvc_list_attr(cont->c_svc->cs_rsvc, tx, &cont->c_user,
				 in->cali_bulk, rpc, &out->calo_size);
}

struct close_iter_arg {
	struct recs_buf	cia_buf;
	uuid_t	       *cia_pool_hdls;
	int		cia_n_pool_hdls;
};

static int
shall_close(const uuid_t pool_hdl, uuid_t *pool_hdls, int n_pool_hdls)
{
	int i;

	for (i = 0; i < n_pool_hdls; i++) {
		if (uuid_compare(pool_hdls[i], pool_hdl) == 0)
			return 1;
	}
	return 0;
}

static int
close_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct close_iter_arg  *arg = varg;
	struct recs_buf	       *buf = &arg->cia_buf;
	struct container_hdl   *hdl;
	int			rc;

	if (key->iov_len != sizeof(uuid_t) ||
	    val->iov_len != sizeof(*hdl)) {
		D_ERROR("invalid key/value size: key="DF_U64" value="DF_U64"\n",
			key->iov_len, val->iov_len);
		return -DER_IO;
	}

	hdl = val->iov_buf;

	if (!shall_close(hdl->ch_pool_hdl, arg->cia_pool_hdls,
			 arg->cia_n_pool_hdls))
		return 0;

	rc = recs_buf_grow(buf);
	if (rc != 0)
		return rc;

	uuid_copy(buf->rb_recs[buf->rb_nrecs].tcr_hdl, key->iov_buf);
	buf->rb_recs[buf->rb_nrecs].tcr_hce = hdl->ch_hce;
	buf->rb_nrecs++;
	return 0;
}

/*
 * Close container handles that are associated with "pool_hdls[n_pool_hdls]"
 * and managed by local container services.
 */
int
ds_cont_close_by_pool_hdls(uuid_t pool_uuid, uuid_t *pool_hdls, int n_pool_hdls,
			   crt_context_t ctx)
{
	struct cont_svc		       *svc;
	struct rdb_tx			tx;
	struct close_iter_arg		arg;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": closing by %d pool hdls: pool_hdls[0]="
		DF_UUID"\n", DP_CONT(pool_uuid, NULL), n_pool_hdls,
		DP_UUID(pool_hdls[0]));

	/* TODO: Do the following for all local container services. */
	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc,
				    NULL /* hint */);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->cs_lock);

	rc = recs_buf_init(&arg.cia_buf);
	if (rc != 0)
		goto out_lock;
	arg.cia_pool_hdls = pool_hdls;
	arg.cia_n_pool_hdls = n_pool_hdls;

	/* Iterate through the handles of all containers in this service. */
	rc = rdb_tx_iterate(&tx, &svc->cs_hdls, false /* !backward */,
			    close_iter_cb, &arg);
	if (rc != 0)
		goto out_buf;

	if (arg.cia_buf.rb_nrecs > 0)
		rc = cont_close_hdls(svc, arg.cia_buf.rb_recs,
				     arg.cia_buf.rb_nrecs, ctx);

out_buf:
	recs_buf_fini(&arg.cia_buf);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_svc:
	cont_svc_put_leader(svc);
	return rc;
}

/* argument type for callback function to list containers */
struct list_cont_iter_args {
	uuid_t				pool_uuid;

	/* Number of containers in pool and conts[] index while counting */
	uint64_t			ncont;

	/* conts[]: capacity*/
	uint64_t			conts_len;
	struct daos_pool_cont_info	*conts;
};

/* callback function for list containers iteration. */
static int
enum_cont_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct list_cont_iter_args	*ap = varg;
	struct daos_pool_cont_info	*cinfo;
	uuid_t				 cont_uuid;
	(void) val;

	if (key->iov_len != sizeof(uuid_t)) {
		D_ERROR("invalid key size: key="DF_U64"\n", key->iov_len);
		return -DER_IO;
	}

	uuid_copy(cont_uuid, key->iov_buf);

	D_DEBUG(DF_DSMS, "pool/cont: "DF_CONTF"\n",
		DP_CONT(ap->pool_uuid, cont_uuid));

	/* Realloc conts[] if needed (double each time starting with 1) */
	if (ap->ncont == ap->conts_len) {
		void	*ptr;
		size_t	realloc_elems = (ap->conts_len == 0) ? 1 :
					ap->conts_len * 2;

		D_REALLOC_ARRAY(ptr, ap->conts, realloc_elems);
		if (ptr == NULL)
			return -DER_NOMEM;
		ap->conts = ptr;
		ap->conts_len = realloc_elems;
	}

	cinfo = &ap->conts[ap->ncont];
	ap->ncont++;
	uuid_copy(cinfo->pci_uuid, cont_uuid);
	return 0;
}

/**
 * List all containers in a pool.
 *
 * \param[in]	pool_uuid	Pool UUID.
 * \param[out]	conts		Array of container info structures
 *				to be allocated. Caller must free.
 * \param[out]	ncont		Number of containers in the pool
 *				(number of items populated in conts[]).
 */
int
ds_cont_list(uuid_t pool_uuid, struct daos_pool_cont_info **conts,
	     uint64_t *ncont)
{
	int				 rc;
	struct cont_svc			*svc;
	struct rdb_tx			 tx;
	struct list_cont_iter_args	 args;

	*conts = NULL;
	*ncont = 0;

	args.ncont = 0;			/* number of containers in the pool */
	args.conts_len = 0;		/* allocated length of conts[] */
	args.conts = NULL;

	uuid_copy(args.pool_uuid, pool_uuid);

	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc,
				    NULL /* hint **/);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->cs_lock);

	rc = rdb_tx_iterate(&tx, &svc->cs_conts, false /* !backward */,
			    enum_cont_cb, &args);

	/* read-only, so no rdb_tx_commit */
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);

out_svc:
	cont_svc_put_leader(svc);

out:
	D_DEBUG(DF_DSMS, "iterate rc=%d, args.conts=%p, args.ncont="DF_U64"\n",
			rc, args.conts, args.ncont);

	if (rc != 0) {
		/* Error in iteration */
		if (args.conts)
			D_FREE(args.conts);
	} else {
		*ncont = args.ncont;
		*conts = args.conts;
	}
	return rc;
}

static int
cont_op_with_hdl(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		 struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	switch (opc_get(rpc->cr_opc)) {
	case CONT_QUERY:
		return cont_query(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_LIST:
		return cont_attr_list(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_GET:
		return cont_attr_get(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_SET:
		return cont_attr_set(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_DEL:
		return cont_attr_del(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_AGGREGATE:
		return ds_cont_epoch_aggregate(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_LIST:
		return ds_cont_snap_list(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_CREATE:
		return ds_cont_snap_create(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_DESTROY:
		return ds_cont_snap_destroy(tx, pool_hdl, cont, hdl, rpc);
	case CONT_PROP_SET:
		return ds_cont_prop_set(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ACL_UPDATE:
		return ds_cont_acl_update(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ACL_DELETE:
		return ds_cont_acl_delete(tx, pool_hdl, cont, hdl, rpc);
	default:
		D_ASSERT(0);
	}

	return 0;
}

/*
 * Look up the container handle, or if the RPC does not need this, call the
 * final handler.
 */
static int
cont_op_with_cont(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		  struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	d_iov_t			key;
	d_iov_t			value;
	struct container_hdl	hdl;
	int			rc;

	switch (opc_get(rpc->cr_opc)) {
	case CONT_OPEN:
		rc = cont_open(tx, pool_hdl, cont, rpc);
		break;
	case CONT_CLOSE:
		rc = cont_close(tx, pool_hdl, cont, rpc);
		break;
	case CONT_DESTROY:
		rc = cont_destroy(tx, pool_hdl, cont, rpc);
		break;
	default:
		/* Look up the container handle. */
		d_iov_set(&key, in->ci_hdl, sizeof(uuid_t));
		d_iov_set(&value, &hdl, sizeof(hdl));
		rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				D_ERROR(DF_CONT": rejecting unauthorized "
					"operation: "DF_UUID"\n",
					DP_CONT(cont->c_svc->cs_pool_uuid,
						cont->c_uuid),
					DP_UUID(in->ci_hdl));
				rc = -DER_NO_HDL;
			} else {
				D_ERROR(DF_CONT": failed to look up container "
					"handle "DF_UUID": %d\n",
					DP_CONT(cont->c_svc->cs_pool_uuid,
						cont->c_uuid),
					DP_UUID(in->ci_hdl), rc);
			}
			D_GOTO(out, rc);
		}
		rc = cont_op_with_hdl(tx, pool_hdl, cont, &hdl, rpc);
	}
out:
	return rc;
}

/*
 * Look up the container, or if the RPC does not need this, call the final
 * handler.
 */
static int
cont_op_with_svc(struct ds_pool_hdl *pool_hdl, struct cont_svc *svc,
		 crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	struct rdb_tx		tx;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	struct cont	       *cont = NULL;
	int			rc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out, rc);

	/* TODO: Implement per-container locking. */
	if (opc == CONT_QUERY || opc == CONT_ATTR_GET ||
	    opc == CONT_ATTR_LIST || opc == CONT_SNAP_LIST)
		ABT_rwlock_rdlock(svc->cs_lock);
	else
		ABT_rwlock_wrlock(svc->cs_lock);

	switch (opc) {
	case CONT_CREATE:
		rc = cont_create(&tx, pool_hdl, svc, rpc);
		break;
	default:
		rc = cont_lookup(&tx, svc, in->ci_uuid, &cont);
		if (rc != 0)
			D_GOTO(out_lock, rc);
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc);
		cont_put(cont);
	}
	if (rc != 0)
		D_GOTO(out_lock, rc);

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_ERROR(DF_CONT": rpc=%p opc=%u hdl="DF_UUID" rdb_tx_commit "
			"failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid),
			rpc, opc, DP_UUID(in->ci_hdl), DP_RC(rc));

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out:
	/* Propagate new snapshot list by IV */
	if (rc == 0 && (opc == CONT_SNAP_CREATE || opc == CONT_SNAP_DESTROY))
		ds_cont_update_snap_iv(svc, in->ci_uuid);

	return rc;
}

/* Look up the pool handle and the matching container service. */
void
ds_cont_op_handler(crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	struct cont_op_out     *out = crt_reply_get(rpc);
	struct ds_pool_hdl     *pool_hdl;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	daos_prop_t	       *prop = NULL;
	struct cont_svc	       *svc;
	int			rc;

	pool_hdl = ds_pool_hdl_lookup(in->ci_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" opc=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc);

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup_leader(pool_hdl->sph_pool->sp_uuid, 0 /* id */,
				    &svc, &out->co_hint);
	if (rc != 0) {
		D_ERROR(DF_CONT": rpc %p: hdl="DF_UUID" opc=%u find leader\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid),
			rpc, DP_UUID(in->ci_hdl), opc);
		D_GOTO(out_pool_hdl, rc);
	}

	rc = cont_op_with_svc(pool_hdl, svc, rpc);

	ds_rsvc_set_hint(svc->cs_rsvc, &out->co_hint);
	cont_svc_put_leader(svc);
out_pool_hdl:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: hdl="DF_UUID
		" opc=%u rc=%d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc, rc);
	ds_pool_hdl_put(pool_hdl);
out:
	/* cleanup the properties for cont_query */
	if (opc == CONT_QUERY) {
		struct cont_query_out  *cqo = crt_reply_get(rpc);

		prop = cqo->cqo_prop;
	}
	out->co_rc = rc;
	crt_reply_send(rpc);
	daos_prop_free(prop);

	return;
}

int
ds_cont_oid_fetch_add(uuid_t poh_uuid, uuid_t co_uuid, uuid_t coh_uuid,
		      uint64_t num_oids, uint64_t *oid)
{
	struct ds_pool_hdl	*pool_hdl;
	struct cont_svc		*svc;
	struct rdb_tx		tx;
	struct cont		*cont = NULL;
	d_iov_t		key;
	d_iov_t		value;
	struct container_hdl	hdl;
	uint64_t		max_oid;
	int			rc;

	pool_hdl = ds_pool_hdl_lookup(poh_uuid);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup_leader(pool_hdl->sph_pool->sp_uuid, 0, &svc, NULL);
	if (rc != 0)
		D_GOTO(out_pool_hdl, rc);

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->cs_lock);

	rc = cont_lookup(&tx, svc, co_uuid, &cont);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	/* Look up the container handle. */
	d_iov_set(&key, coh_uuid, sizeof(uuid_t));
	d_iov_set(&value, &hdl, sizeof(hdl));
	rc = rdb_tx_lookup(&tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = -DER_NO_HDL;
		D_GOTO(out_cont, rc);
	}

	/* Read the max OID from the container metadata */
	d_iov_set(&value, &max_oid, sizeof(max_oid));
	rc = rdb_tx_lookup(&tx, &cont->c_prop, &ds_cont_prop_max_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup max_oid: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out_cont, rc);
	}

	/** Set the oid for the caller */
	*oid = max_oid;
	/** Increment the max_oid by how many oids user requested */
	max_oid += num_oids;

	/* Update the max OID */
	rc = rdb_tx_update(&tx, &cont->c_prop, &ds_cont_prop_max_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to update max_oid: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out_cont, rc);
	}

	rc = rdb_tx_commit(&tx);

out_cont:
	cont_put(cont);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_svc:
	cont_svc_put_leader(svc);
out_pool_hdl:
	ds_pool_hdl_put(pool_hdl);
out:
	return rc;
}

/* Send the RPC from a DAOS server instance to the container service */
int
ds_cont_svc_set_prop(uuid_t pool_uuid, uuid_t cont_uuid,
		     d_rank_list_t *ranks, daos_prop_t *prop)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct cont_prop_set_in		*in;
	struct cont_prop_set_out	*out;

	D_DEBUG(DB_MGMT, DF_CONT": Setting container prop\n",
		DP_CONT(pool_uuid, cont_uuid));

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out, rc);

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find pool service: "DF_RC"\n",
			DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		D_GOTO(out_client, rc);
	}

	rc = cont_req_create(info->dmi_ctx, &ep, CONT_PROP_SET, &rpc);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to create cont set prop rpc: %d\n",
			DP_CONT(pool_uuid, cont_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_clear(in->cpsi_op.ci_hdl);
	uuid_clear(in->cpsi_op.ci_pool_hdl);
	uuid_copy(in->cpsi_op.ci_uuid, cont_uuid);
	uuid_copy(in->cpsi_pool_uuid, pool_uuid);
	in->cpsi_prop = prop;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      out->cpso_op.co_rc,
				      &out->cpso_op.co_hint);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(1000 /* ms */);
		D_GOTO(rechoose, rc);
	}

	rc = out->cpso_op.co_rc;
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to set prop for container: %d\n",
			DP_CONT(pool_uuid, cont_uuid), rc);
	}

	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

void
ds_cont_set_prop_handler(crt_rpc_t *rpc)
{
	int				 rc;
	struct cont_svc			*svc;
	struct cont_prop_set_in		*in = crt_req_get(rpc);
	struct cont_prop_set_out	*out = crt_reply_get(rpc);
	struct rdb_tx			 tx;
	uuid_t				 pool_uuid;
	uuid_t				 cont_uuid;
	struct cont			*cont;

	/* Client RPCs go through the regular flow with pool/cont handles */
	if (daos_rpc_from_client(rpc)) {
		ds_cont_op_handler(rpc);
		return;
	}

	/*
	 * Server RPCs don't have pool or container handles. Just need the pool
	 * and container UUIDs.
	 */
	uuid_copy(pool_uuid, in->cpsi_pool_uuid);
	uuid_copy(cont_uuid, in->cpsi_op.ci_uuid);

	D_DEBUG(DF_DSMS, DF_CONT": processing cont set prop rpc %p\n",
		DP_CONT(pool_uuid, cont_uuid), rpc);

	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */,
				    &svc, &out->cpso_op.co_hint);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed to look up cont svc: %d\n",
			DP_CONT(pool_uuid, cont_uuid), rc);
		D_GOTO(out, rc);
	}

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->cs_lock);

	rc = cont_lookup(&tx, svc, cont_uuid, &cont);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	rc = set_prop(&tx, svc->cs_pool, cont,
		      ds_sec_get_admin_cont_capabilities(), cont_uuid,
		      in->cpsi_prop);
	if (rc != 0)
		D_GOTO(out_cont, rc);

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_ERROR(DF_CONT": Unable to commit RDB transaction\n",
			DP_CONT(pool_uuid, cont_uuid));

out_cont:
	cont_put(cont);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(svc->cs_rsvc, &out->cpso_op.co_hint);
	cont_svc_put_leader(svc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: rc=%d\n",
		DP_CONT(pool_uuid, in->cpsi_op.ci_uuid), rpc, rc);

	out->cpso_op.co_rc = rc;
	crt_reply_send(rpc);
}

int
ds_cont_get_prop(uuid_t pool_uuid, uuid_t cont_uuid, daos_prop_t **prop_out)
{
	daos_prop_t	*prop = NULL;
	struct cont_svc	*svc;
	struct rdb_tx	 tx;
	struct cont	*cont = NULL;
	int		 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = cont_svc_lookup_leader(pool_uuid, 0, &svc, NULL);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_put, rc);

	ABT_rwlock_rdlock(svc->cs_lock);
	rc = cont_lookup(&tx, svc, cont_uuid, &cont);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	rc = cont_prop_read(&tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop);
	cont_put(cont);

	D_ASSERT(prop != NULL);
	D_ASSERT(prop->dpp_nr == CONT_PROP_NUM);

	if (rc != 0)
		D_GOTO(out_lock, rc);

	*prop_out = prop;

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_put:
	cont_svc_put_leader(svc);
	return rc;
}
