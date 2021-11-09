/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"

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
 * \param[in] pmap      The pool map referenced by the container.
 * \param[in] props     The container properties, used to get redundancy factor
 *                      and level.
 *
 * \return	0 if the container meets the requirements, negative error code
 *		if it does not.
 * XXX: obsoleted, to be removed later.
 */
int
cont_verify_redun_req(struct pool_map *pmap, daos_prop_t *props)
{
	int		num_failed;
	int		num_allowed_failures;
	int		redun_fac = daos_cont_prop2redunfac(props);
	uint32_t	redun_lvl = daos_cont_prop2redunlvl(props);
	int		rc = 0;

	rc = pool_map_get_failed_cnt(pmap, redun_lvl);
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

	D_ERROR("Domain contains %d failed components, allows at most %d",
		num_failed, num_allowed_failures);
	return -DER_INVAL;
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

	/* cs_uuids */
	rc = rdb_path_clone(&svc->cs_root, &svc->cs_uuids);
	if (rc != 0)
		D_GOTO(err_root, rc);
	rc = rdb_path_push(&svc->cs_uuids, &ds_cont_prop_cuuids);
	if (rc != 0)
		D_GOTO(err_uuids, rc);

	/* cs_conts */
	rc = rdb_path_clone(&svc->cs_root, &svc->cs_conts);
	if (rc != 0)
		D_GOTO(err_uuids, rc);
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
err_uuids:
	rdb_path_fini(&svc->cs_uuids);
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
	rdb_path_fini(&svc->cs_uuids);
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

int
ds_cont_svc_step_up(struct cont_svc *svc)
{
	struct rdb_tx	tx;
	d_iov_t		value;
	uint32_t	version;
	int		rc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_rdlock(svc->cs_lock);

	/* Check the layout version. */
	d_iov_set(&value, &version, sizeof(version));
	rc = rdb_tx_lookup(&tx, &svc->cs_root, &ds_cont_prop_version, &value);
	if (rc == -DER_NONEXIST) {
		ds_notify_ras_eventf(RAS_CONT_DF_INCOMPAT, RAS_TYPE_INFO,
				     RAS_SEV_ERROR, NULL /* hwid */,
				     NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */,
				     &svc->cs_pool_uuid, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */,
				     NULL /* data */,
				     "incompatible layout version");
		rc = -DER_DF_INCOMPT;
		goto out_lock;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to look up layout version: "DF_RC"\n",
			DP_UUID(svc->cs_pool_uuid), DP_RC(rc));
		goto out_lock;
	}
	if (version < DS_CONT_MD_VERSION_LOW || version > DS_CONT_MD_VERSION) {
		ds_notify_ras_eventf(RAS_CONT_DF_INCOMPAT, RAS_TYPE_INFO,
				     RAS_SEV_ERROR, NULL /* hwid */,
				     NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */,
				     &svc->cs_pool_uuid, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */,
				     NULL /* data */,
				     "incompatible layout version: %u not in "
				     "[%u, %u]", version,
				     DS_CONT_MD_VERSION_LOW,
				     DS_CONT_MD_VERSION);
		rc = -DER_DF_INCOMPT;
	}

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out;

	D_ASSERT(svc->cs_pool == NULL);
	svc->cs_pool = ds_pool_lookup(svc->cs_pool_uuid);
	D_ASSERT(svc->cs_pool != NULL);

	rc = cont_svc_ec_agg_leader_start(svc);
	if (rc != 0)
		D_ERROR(DF_UUID": start ec agg leader failed: "DF_RC"\n",
			DP_UUID(svc->cs_pool_uuid), DP_RC(rc));

out:
	return rc;
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
				    DAOS_CONT_VERSION, rpc, NULL, NULL);
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
	d_iov_t			value;
	uint32_t		version = DS_CONT_MD_VERSION;
	struct rdb_kvs_attr	attr;
	int			rc;

	d_iov_set(&value, &version, sizeof(version));
	rc = rdb_tx_update(tx, kvs, &ds_cont_prop_version, &value);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to initialize layout version: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_cont_prop_cuuids, &attr);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create container UUIDs KVS: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

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

/* check if container exists by UUID and (if applicable) non-default label */
static int
cont_existence_check(struct rdb_tx *tx, struct cont_svc *svc,
		     uuid_t puuid, uuid_t cuuid, char *clabel)
{
	d_iov_t		key;
	d_iov_t		val;
	bool		may_exist = false;
	uuid_t		match_cuuid;
	int		rc;

	/* Find by UUID in cs_conts KVS. */
	d_iov_set(&key, cuuid, sizeof(uuid_t));
	d_iov_set(&val, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(tx, &svc->cs_conts, &key, &val);
	if (rc != -DER_NONEXIST) {
		if (rc != 0)
			return rc;	/* other lookup failure */
		may_exist = true;
	}

	/* If no label in request, return cs_conts lookup result */
	if (clabel == NULL) {
		D_DEBUG(DF_DSMS, DF_CONT": no label, lookup by UUID "DF_UUIDF
			" "DF_RC"\n", DP_CONT(puuid, cuuid), DP_UUID(cuuid),
			DP_RC(rc));
		return rc;
	}

	/* Label provided in request - search for it in cs_uuids KVS
	 * and perform additional sanity checks.
	 */
	d_iov_set(&key, clabel, strnlen(clabel, DAOS_PROP_MAX_LABEL_BUF_LEN));
	d_iov_set(&val, match_cuuid, sizeof(uuid_t));
	rc = rdb_tx_lookup(tx, &svc->cs_uuids, &key, &val);
	if (rc != -DER_NONEXIST) {
		if (rc != 0)
			return rc;	/* other lookup failure */

		/* not found by UUID, but label matched - invalid */
		if (!may_exist) {
			D_ERROR(DF_CONT": non-unique label: %s\n",
				DP_CONT(puuid, cuuid), clabel);
			return -DER_EXIST;
		}

		/* found by UUID, and found by label: make sure the
		 * label lookup returned same UUID as the request UUID.
		 */
		if (uuid_compare(cuuid, match_cuuid)) {
			D_ERROR(DF_CONT": label=%s -> "DF_UUID" (mismatch)\n",
				DP_CONT(puuid, cuuid), clabel,
				DP_UUID(match_cuuid));
			return -DER_INVAL;
		}
		return 0;
	}

	/* not found by UUID, and not found by label - legitimate "non-exist" */
	if (!may_exist)
		return rc;	/* -DER_NONEXIST */

	/* found by UUID, not found by label - invalid label input */
	D_ERROR(DF_CONT": found by UUID but not by label: %s\n",
		DP_CONT(puuid, cuuid), clabel);
	return -DER_INVAL;
}

/* copy \a prop to \a prop_def (duplicated default prop) for cont_create */
static int
cont_create_prop_prepare(struct ds_pool_hdl *pool_hdl,
			 daos_prop_t *prop_def, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	struct daos_prop_entry	*entry_def;
	int			 i;
	int			 rc;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
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
		case DAOS_PROP_CO_EC_CELL_SZ:
		case DAOS_PROP_CO_ALLOCED_OID:
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_CO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl = entry->dpe_val_ptr;

				D_FREE(entry_def->dpe_val_ptr);
				rc = daos_prop_entry_dup_ptr(entry_def, entry,
							     daos_acl_get_size(acl));
				if (rc)
					return rc;
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
		case DAOS_PROP_CO_ROOTS:
			if (entry->dpe_val_ptr) {
				rc = daos_prop_entry_dup_co_roots(entry_def,
								  entry);
				if (rc)
					return rc;
			}
			break;
		default:
			D_ASSERTF(0, "bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}

	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_EC_CELL_SZ);
	D_ASSERT(entry_def != NULL);
	if (entry_def->dpe_val == 0) {
		/* No specified cell size from container, inherit from pool */
		D_ASSERT(pool_hdl->sph_pool->sp_ec_cell_sz != 0);
		entry_def->dpe_val = pool_hdl->sph_pool->sp_ec_cell_sz;
	}

	/* for new container set HEALTHY status with current pm ver */
	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_STATUS);
	D_ASSERT(entry_def != NULL);
	entry_def->dpe_val = DAOS_PROP_CO_STATUS_VAL(DAOS_PROP_CO_HEALTHY, 0,
				     ds_pool_get_version(pool_hdl->sph_pool));

	/* Validate the result */
	if (!daos_prop_valid(prop_def, false /* pool */, true /* input */)) {
		D_ERROR("properties validation check failed\n");
		return -DER_INVAL;
	}

	return 0;
}

static int
cont_prop_write(struct rdb_tx *tx, const rdb_path_t *kvs, daos_prop_t *prop,
		bool create)
{
	struct daos_prop_entry	*entry;
	d_iov_t			value;
	struct daos_co_status	stat;
	int			i;
	int			rc = 0;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		if (!create && (entry->dpe_type == DAOS_PROP_CO_EC_CELL_SZ)) {
			/* TODO: add more properties that can only be set
			 * on creation.
			 */
			rc = -DER_NO_PERM;
			break;
		}

		switch (entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			d_iov_set(&value, entry->dpe_str,
				  strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_label,
					   &value);
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_layout_type,
					   &value);
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_layout_ver,
					   &value);
			break;
		case DAOS_PROP_CO_CSUM:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_csum, &value);
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
					   &ds_cont_prop_csum_chunk_size,
					   &value);
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
					   &ds_cont_prop_csum_server_verify,
					   &value);
			break;
		case DAOS_PROP_CO_DEDUP:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
					   &ds_cont_prop_dedup, &value);
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
					   &ds_cont_prop_dedup_threshold,
					   &value);
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_redun_fac,
					   &value);
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_redun_lvl,
					   &value);
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_snapshot_max,
					   &value);
			break;
		case DAOS_PROP_CO_COMPRESS:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_compress,
					   &value);
			break;
		case DAOS_PROP_CO_ENCRYPT:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_encrypt,
					   &value);
			break;
		case DAOS_PROP_CO_EC_CELL_SZ:
			if (!daos_ec_cs_valid(entry->dpe_val)) {
				D_ERROR("Invalid EC cell size=%u\n",
					(uint32_t)entry->dpe_val);
				rc = -DER_INVAL;
				break;
			}
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_ec_cell_sz,
					   &value);
			break;
		case DAOS_PROP_CO_OWNER:
			d_iov_set(&value, entry->dpe_str,
				  strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_owner,
					   &value);
			break;
		case DAOS_PROP_CO_OWNER_GROUP:
			d_iov_set(&value, entry->dpe_str,
				  strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_owner_group,
					   &value);
			break;
		case DAOS_PROP_CO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl = entry->dpe_val_ptr;

				d_iov_set(&value, acl, daos_acl_get_size(acl));
				rc = rdb_tx_update(tx, kvs, &ds_cont_prop_acl,
						   &value);
			}
			break;
		case DAOS_PROP_CO_ROOTS:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_prop_co_roots *roots;

				roots = entry->dpe_val_ptr;
				d_iov_set(&value, roots, sizeof(*roots));
				rc = rdb_tx_update(tx, kvs, &ds_cont_prop_roots,
						   &value);
			}
			break;
		case DAOS_PROP_CO_STATUS:
			/* DAOS_PROP_CO_CLEAR only used for iv_prop_update */
			daos_prop_val_2_co_status(entry->dpe_val, &stat);
			stat.dcs_flags = 0;
			entry->dpe_val = daos_prop_co_status_2_val(&stat);
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_co_status,
					   &value);
			break;
		case DAOS_PROP_CO_ALLOCED_OID:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_alloced_oid,
					   &value);
			break;
		default:
			D_ERROR("bad dpe_type %d.\n", entry->dpe_type);
			return -DER_INVAL;
		}
		if (rc) {
			D_ERROR("Failed to update property=%d, "DF_RC"\n",
				entry->dpe_type, DP_RC(rc));
			break;
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
	d_iov_t			key;
	d_iov_t			value;
	struct rdb_kvs_attr	attr;
	rdb_path_t		kvs;
	uint64_t		ghce = 0;
	uint64_t		alloced_oid = 0;
	struct daos_prop_entry *lbl_ent;
	struct daos_prop_entry *def_lbl_ent;
	d_string_t		lbl = NULL;
	uint32_t		nsnapshots = 0;
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

	/* duplicate the default properties, overwrite it with cont create
	 * parameter (write to rdb below).
	 */
	prop_dup = daos_prop_dup(&cont_prop_default, false /* pool */,
				 false /* input */);
	if (prop_dup == NULL) {
		D_ERROR(DF_CONT" daos_prop_dup failed.\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid));
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = cont_create_prop_prepare(pool_hdl, prop_dup, in->cci_prop);
	if (rc != 0) {
		D_ERROR(DF_CONT" cont_create_prop_prepare failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Determine if non-default label property supplied */
	def_lbl_ent = daos_prop_entry_get(&cont_prop_default,
					  DAOS_PROP_CO_LABEL);
	D_ASSERT(def_lbl_ent != NULL);
	lbl_ent = daos_prop_entry_get(prop_dup, DAOS_PROP_CO_LABEL);
	D_ASSERT(lbl_ent != NULL);
	if (strncmp(def_lbl_ent->dpe_str, lbl_ent->dpe_str,
		    DAOS_PROP_LABEL_MAX_LEN)) {
		lbl = lbl_ent->dpe_str;
	}

	/* Check if a container with this UUID and label already exists */
	rc = cont_existence_check(tx, svc, pool_hdl->sph_pool->sp_uuid,
				  in->cci_op.ci_uuid, lbl);
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

	/* Create the GHCE property. */
	d_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_ghce, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": create ghce property failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}

	/** Create the ALLOCED_OID property. */
	d_iov_set(&value, &alloced_oid, sizeof(alloced_oid));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_alloced_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": create alloced_oid prop failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}

	/* write container properties to rdb. */
	rc = cont_prop_write(tx, &kvs, prop_dup, true);
	if (rc != 0) {
		D_ERROR(DF_CONT" cont_prop_write failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}

	/* If non-default label provided, add it in container UUIDs KVS */
	if (lbl) {
		/* If we have come this far (see existence check), there must
		 * not be an entry with this label in cs_uuids. Just update.
		 */
		d_iov_set(&key, lbl, strnlen(lbl, DAOS_PROP_MAX_LABEL_BUF_LEN));
		d_iov_set(&value, in->cci_op.ci_uuid, sizeof(uuid_t));
		rc = rdb_tx_update(tx, &svc->cs_uuids, &key, &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": update cs_uuids failed: "DF_RC"\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid,
					in->cci_op.ci_uuid), DP_RC(rc));
			D_GOTO(out_kvs, rc);
		}
		D_DEBUG(DF_DSMS, DF_CONT": creating container, label: %s\n",
			DP_CONT(svc->cs_pool_uuid, in->cci_op.ci_uuid), lbl);
	}

	/* Create the snapshot KVS. */
	d_iov_set(&value, &nsnapshots, sizeof(nsnapshots));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to update nsnapshots, "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}
	attr.dsa_class = RDB_KVS_INTEGER;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_prop_snapshots, &attr);
	if (rc != 0) {
		D_ERROR(DF_CONT" failed to create container snapshots KVS: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), DP_RC(rc));
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
	rdb_path_fini(&kvs);
out:
	daos_prop_free(prop_dup);
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

/* Number of recs per yield when growing a recs_buf. See close_iter_cb. */
#define RECS_BUF_RECS_PER_YIELD 128

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

struct find_hdls_by_cont_arg {
	struct rdb_tx	       *fha_tx;
	struct recs_buf		fha_buf;
};

static int
find_hdls_by_cont_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct find_hdls_by_cont_arg   *arg = varg;
	struct recs_buf		       *buf = &arg->fha_buf;
	int				rc;

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

	if (buf->rb_nrecs % RECS_BUF_RECS_PER_YIELD == 0) {
		ABT_thread_yield();
		rc = rdb_tx_revalidate(arg->fha_tx);
		if (rc != 0) {
			D_WARN("revalidate RDB TX: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}
	return 0;
}

static int cont_close_hdls(struct cont_svc *svc,
			   struct cont_tgt_close_rec *recs, int nrecs,
			   crt_context_t ctx);

static int
evict_hdls(struct rdb_tx *tx, struct cont *cont, bool force, crt_context_t ctx)
{
	struct find_hdls_by_cont_arg	arg;
	int				rc;

	arg.fha_tx = tx;
	rc = recs_buf_init(&arg.fha_buf);
	if (rc != 0)
		return rc;

	rc = rdb_tx_iterate(tx, &cont->c_hdls, false /* !backward */,
			    find_hdls_by_cont_cb, &arg);
	if (rc != 0)
		goto out;

	if (arg.fha_buf.rb_nrecs == 0)
		goto out;

	if (!force) {
		rc = -DER_BUSY;
		D_WARN("Not evicting handles, "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = cont_close_hdls(cont->c_svc, arg.fha_buf.rb_recs, arg.fha_buf.rb_nrecs, ctx);

out:
	recs_buf_fini(&arg.fha_buf);
	return rc;
}

static void
cont_ec_agg_delete(struct cont_svc *svc, uuid_t cont_uuid);

static int
cont_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	     struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_destroy_in *in = crt_req_get(rpc);
	d_iov_t				key;
	d_iov_t				val;
	int				rc;
	daos_prop_t		       *prop = NULL;
	struct daos_prop_entry	       *lbl_ent;
	struct ownership		owner;
	struct daos_acl		       *acl;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: force=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc,
		in->cdi_force);

	/* Fetch the container props to check access for delete */
	rc = cont_prop_read(tx, cont,
			    DAOS_CO_QUERY_PROP_ACL |
			    DAOS_CO_QUERY_PROP_OWNER |
			    DAOS_CO_QUERY_PROP_OWNER_GROUP |
			    DAOS_CO_QUERY_PROP_LABEL, &prop);
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
			DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid));
		D_GOTO(out_prop, rc = -DER_NO_PERM);
	}

	rc = evict_hdls(tx, cont, in->cdi_force, rpc->cr_ctx);
	if (rc != 0)
		goto out_prop;

	rc = cont_destroy_bcast(rpc->cr_ctx, cont->c_svc, cont->c_uuid);
	if (rc != 0)
		goto out_prop;

	cont_ec_agg_delete(cont->c_svc, cont->c_uuid);

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

	/* Delete entry in container UUIDs KVS (if added during create) */
	lbl_ent = daos_prop_entry_get(prop, DAOS_PROP_CO_LABEL);
	if (lbl_ent) {
		d_iov_set(&key, lbl_ent->dpe_str,
			  strnlen(lbl_ent->dpe_str, DAOS_PROP_MAX_LABEL_BUF_LEN));
		d_iov_set(&val, NULL, 0);
		rc = rdb_tx_lookup(tx, &cont->c_svc->cs_uuids, &key, &val);
		if (rc != -DER_NONEXIST) {
			if (rc == 0) {
				rc = rdb_tx_delete(tx, &cont->c_svc->cs_uuids,
						   &key);
				if (rc != 0)
					goto out_prop;
				D_DEBUG(DB_MD, DF_CONT": deleted label: %s\n",
					DP_CONT(pool_hdl->sph_pool->sp_uuid,
						cont->c_uuid),
						lbl_ent->dpe_str);
			} else {
				goto out_prop;
			}
		}
	}

	/* Destroy the container attribute KVS. */
	d_iov_set(&key, cont->c_uuid, sizeof(uuid_t));
	rc = rdb_tx_destroy_kvs(tx, &cont->c_svc->cs_conts, &key);

out_prop:
	daos_prop_free(prop);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc, rc);
	return rc;
}

struct cont_ec_agg *
cont_ec_agg_lookup(struct cont_svc *cont_svc, uuid_t cont_uuid)
{
	struct cont_ec_agg *ec_agg;

	d_list_for_each_entry(ec_agg, &cont_svc->cs_ec_agg_list, ea_list) {
		if (ec_agg->ea_deleted)
			continue;
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
		if (ec_agg)
			D_FREE(ec_agg->ea_server_ephs);
		D_FREE(ec_agg);
	}

	return rc;
}

static void
cont_ec_agg_delete(struct cont_svc *svc, uuid_t cont_uuid)
{
	struct cont_ec_agg	*ec_agg;

	ec_agg = cont_ec_agg_lookup(svc, cont_uuid);
	if (ec_agg == NULL)
		return;

	/* Set ea_deleted flag to destroy it inside cont_agg_eph_leader_ult()
	 * to avoid list iteration broken.
	 */
	ec_agg->ea_deleted = 1;
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
			ec_agg->ea_deleted = 1;
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

	rc = dss_task_collective(cont_refresh_vos_agg_eph_one, &arg,
				 DSS_ULT_FL_PERIODIC);
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

	if (svc->cs_ec_leader_ephs_req == NULL)
		goto out;

	while (!dss_ult_exiting(svc->cs_ec_leader_ephs_req)) {
		d_rank_list_t		fail_ranks = { 0 };

		rc = map_ranks_init(pool->sp_map, MAP_RANKS_DOWN,
				    &fail_ranks);
		if (rc) {
			D_ERROR(DF_UUID": ranks init failed: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			goto yield;
		}

		d_list_for_each_entry_safe(ec_agg, tmp, &svc->cs_ec_agg_list, ea_list) {
			daos_epoch_t min_eph = DAOS_EPOCH_MAX;
			int	     i;

			if (ec_agg->ea_deleted) {
				d_list_del(&ec_agg->ea_list);
				D_FREE(ec_agg->ea_server_ephs);
				D_FREE(ec_agg);
				continue;
			}

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

			if (min_eph == ec_agg->ea_current_eph)
				continue;

			/**
			 * NB: during extending or reintegration, the new
			 * server might cause the minimum epoch is less than
			 * ea_current_eph.
			 */
			D_DEBUG(DB_MD, DF_CONT" minimum "DF_U64" current "
				DF_U64"\n",
				DP_CONT(svc->cs_pool_uuid,
					ec_agg->ea_cont_uuid),
				min_eph, ec_agg->ea_current_eph);
			rc = cont_iv_ec_agg_eph_refresh(pool->sp_iv_ns,
							ec_agg->ea_cont_uuid,
							min_eph);
			if (rc) {
				D_CDEBUG(rc == -DER_NONEXIST,
					 DLOG_INFO, DLOG_ERR,
					 DF_CONT": refresh failed: "DF_RC"\n",
					 DP_CONT(svc->cs_pool_uuid,
						 ec_agg->ea_cont_uuid),
					DP_RC(rc));
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

out:
	D_DEBUG(DF_DSMS, DF_UUID": stop eph ult: rc %d\n",
		DP_UUID(svc->cs_pool_uuid), rc);

	d_list_for_each_entry_safe(ec_agg, tmp, &svc->cs_ec_agg_list, ea_list) {
		d_list_del(&ec_agg->ea_list);
		D_FREE(ec_agg->ea_server_ephs);
		D_FREE(ec_agg);
	}
}

static int
cont_svc_ec_agg_leader_start(struct cont_svc *svc)
{
	struct sched_req_attr	attr;
	ABT_thread		ec_eph_leader_ult = ABT_THREAD_NULL;
	int			rc;

	D_INIT_LIST_HEAD(&svc->cs_ec_agg_list);
	if (unlikely(ec_agg_disabled))
		return 0;

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
		DABT_THREAD_FREE(&ec_eph_leader_ult);
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

static int
cont_lookup_bylabel(struct rdb_tx *tx, const struct cont_svc *svc,
		    const char *label, struct cont **cont)
{
	size_t		label_len;
	uuid_t		uuid;
	d_iov_t		key;
	d_iov_t		val;
	int		rc;

	label_len = strnlen(label, DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (!label || (label_len == 0) || (label_len > DAOS_PROP_LABEL_MAX_LEN))
		return -DER_INVAL;

	/* Look up container UUID by label */
	d_iov_set(&key, (void *)label, label_len);
	d_iov_set(&val, (void *)uuid, sizeof(uuid_t));
	rc = rdb_tx_lookup(tx, &svc->cs_uuids, &key, &val);
	D_DEBUG(DF_DSMS, DF_UUID":lookup (len %zu) label=%s -> cuuid="DF_UUID
		", rc=%d\n", DP_UUID(svc->cs_pool_uuid), key.iov_len, label,
		DP_UUID(uuid), rc);
	if (rc != 0)
		return rc;

	/* Look up container by UUID */
	rc = cont_lookup(tx, svc, uuid, cont);
	if (rc != 0)
		return rc;

	D_DEBUG(DF_DSMS, DF_CONT": successfully found container %s\n",
		DP_CONT(svc->cs_pool_uuid, (*cont)->c_uuid), label);
	return 0;
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

static bool
cont_status_is_healthy(daos_prop_t *prop, uint32_t *pm_ver)
{
	struct daos_prop_entry	*entry;
	struct daos_co_status	 stat = { 0 };

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	D_ASSERT(entry != NULL);

	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	if (pm_ver != NULL)
		*pm_ver = stat.dcs_pm_ver;
	return (stat.dcs_status == DAOS_PROP_CO_HEALTHY);
}

static void
cont_status_set_unclean(daos_prop_t *prop)
{
	struct daos_prop_entry	*pentry;
	struct daos_co_status	 stat;

	pentry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	D_ASSERT(pentry != NULL);
	daos_prop_val_2_co_status(pentry->dpe_val, &stat);
	stat.dcs_status = DAOS_PROP_CO_UNCLEAN;
	pentry->dpe_val = daos_prop_co_status_2_val(&stat);
}

static int
cont_open(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	  crt_rpc_t *rpc)
{
	struct cont_open_in    *in = crt_req_get(rpc);
	struct cont_open_out   *out = crt_reply_get(rpc);
	d_iov_t			key;
	d_iov_t			value;
	daos_prop_t	       *prop = NULL;
	struct container_hdl	chdl;
	char			zero = 0;
	int			rc;
	struct ownership	owner;
	struct daos_acl		*acl;
	bool			is_healthy;
	bool			cont_hdl_opened = false;
	uint32_t		stat_pm_ver = 0;
	uint64_t		sec_capas = 0;
	uint32_t		snap_count;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" flags="
		DF_X64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc,
		DP_UUID(in->coi_op.ci_hdl), in->coi_flags);

	/* See if this container handle already exists. */
	d_iov_set(&key, in->coi_op.ci_hdl, sizeof(uuid_t));
	d_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != -DER_NONEXIST) {
		D_DEBUG(DF_DSMS, DF_CONT"/"DF_UUID": "
				 "Container handle already open.\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid),
			DP_UUID(in->coi_op.ci_hdl));
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
	is_healthy = cont_status_is_healthy(prop, &stat_pm_ver);
	out->coo_op.co_map_version = stat_pm_ver;
	if (is_healthy) {
		int	rf;

		rf = daos_cont_prop2redunfac(prop);
		rc = ds_pool_rf_verify(pool_hdl->sph_pool, stat_pm_ver, rf);
		if (rc == -DER_RF) {
			is_healthy = false;
		} else if (rc) {
			daos_prop_free(prop);
			goto out;
		}
	}
	if (!is_healthy && !(in->coi_flags & DAOS_COO_FORCE)) {
		rc = -DER_RF;
		D_ERROR(DF_CONT": RF broken, set DAOS_COO_FORCE to force "
			"cont_open, or clear DAOS_PROP_CO_STATUS prop"DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid,
				cont->c_uuid), DP_RC(rc));
		daos_prop_free(prop);
		goto out;
	}

	/* query the container properties from RDB and update to IV */
	rc = cont_iv_prop_update(pool_hdl->sph_pool->sp_iv_ns,
				 cont->c_uuid, prop);
	daos_prop_free(prop);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_iv_prop_update failed %d.\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out, rc);
	}

	/* update container capa to IV */
	rc = cont_iv_capability_update(pool_hdl->sph_pool->sp_iv_ns,
				       in->coi_op.ci_hdl, cont->c_uuid,
				       in->coi_flags, sec_capas, stat_pm_ver);
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

	/* Get nsnapshots */
	d_iov_set(&value, &snap_count, sizeof(snap_count));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		goto out;
	}
	out->coo_snap_count = snap_count;
	D_DEBUG(DF_DSMS, DF_CONT": got nsnapshots=%u\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), snap_count);

	/* Get latest snapshot */
	if (snap_count > 0) {
		d_iov_t		key_out;

		rc = rdb_tx_query_key_max(tx, &cont->c_snaps, &key_out);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to query lsnapshot, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}
		out->coo_lsnapshot = *(uint64_t *)key_out.iov_buf;
		D_DEBUG(DF_DSMS, DF_CONT": got lsnapshot="DF_X64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), out->coo_lsnapshot);
	}

out:
	if (rc == 0) {
		/**
		 * Put requested properties in output.
		 * the allocated prop will be freed after rpc replied in
		 * ds_cont_op_handler.
		 */
		rc = cont_prop_read(tx, cont, in->coi_prop_bits, &prop);
		out->coo_prop = prop;
	}
	if (rc != 0 && cont_hdl_opened)
		cont_iv_capability_invalidate(pool_hdl->sph_pool->sp_iv_ns,
					      in->coi_op.ci_hdl,
					      CRT_IV_SYNC_EAGER);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc,
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
		if (rc == -DER_SHUTDOWN) {
			/* If one of rank is being stopped, it may
			 * return -DER_SHUTDOWN, which can be ignored
			 * during capability invalidate.
			 */
			D_DEBUG(DF_DSMS, DF_CONT"/"DF_UUID" fail %d",
				DP_CONT(svc->cs_pool_uuid, NULL),
				DP_UUID(recs[i].tcr_hdl), rc);
			rc = 0;
		}
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
	struct rdb_tx	tx;
	int		i;
	int		rc;

	D_ASSERTF(nrecs > 0, "%d\n", nrecs);
	D_DEBUG(DF_DSMS, DF_CONT": closing %d recs: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64"\n", DP_CONT(svc->cs_pool_uuid, NULL),
		nrecs, DP_UUID(recs[0].tcr_hdl), recs[0].tcr_hce);

	rc = cont_close_recs(ctx, svc, recs, nrecs);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		goto out;

	for (i = 0; i < nrecs; i++) {
		rc = cont_close_one_hdl(&tx, svc, ctx, recs[i].tcr_hdl);
		if (rc != 0)
			goto out_tx;

		/*
		 * Yield frequently, in order to cope with the slow
		 * vos_obj_punch operations invoked by rdb_tx_commit for
		 * deleting the handles. (If there is no other RDB replica, the
		 * TX operations will not yield, and this loop would occupy the
		 * xstream for too long.)
		 */
		if ((i + 1) % 32 == 0) {
			rc = rdb_tx_commit(&tx);
			if (rc != 0)
				goto out_tx;
			rdb_tx_end(&tx);
			ABT_thread_yield();
			rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
			if (rc != 0)
				goto out;
		}
	}

	rc = rdb_tx_commit(&tx);

out_tx:
	rdb_tx_end(&tx);
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

/* TODO: decide if tqo_hae is needed at all; and query for more information.
 * Currently this does not do much and is not used. Kept for future expansion.
 */
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
		/* cqo_hae removed: query_out->cqo_hae = out->tqo_hae; */
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
	if (bits & DAOS_CO_QUERY_PROP_ROOTS) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_roots,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_ROOTS;
		D_ALLOC(prop->dpp_entries[idx].dpe_val_ptr, value.iov_len);
		if (prop->dpp_entries[idx].dpe_val_ptr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf,
		       value.iov_len);
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_CO_STATUS) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_co_status,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_STATUS;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_EC_CELL_SZ) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_ec_cell_sz,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);

		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_ALLOCED_OID) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_alloced_oid,
				   &value);
		if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_ALLOCED_OID;
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
cont_status_check(struct rdb_tx *tx, struct ds_pool *pool, struct cont *cont,
		  struct cont_query_in *in, daos_prop_t *prop,
		  uint32_t last_ver)
{
	struct daos_prop_entry	*entry;
	int			 rf;
	int			 rc;

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
	D_ASSERT(entry != NULL);
	rf = daos_cont_prop2redunfac(prop);
	rc = ds_pool_rf_verify(pool, last_ver, rf);
	if (rc == -DER_RF) {
		rc = 0;
		cont_status_set_unclean(prop);
	}

	return rc;
}

static int
cont_query(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_query_in   *in  = crt_req_get(rpc);
	struct cont_query_out  *out = crt_reply_get(rpc);
	daos_prop_t	       *prop = NULL;
	uint32_t		last_ver = 0;
	d_iov_t			value;
	int			snap_count;
	int			rc = 0;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cqi_op.ci_uuid), rpc,
		DP_UUID(in->cqi_op.ci_hdl));

	if (!hdl_has_query_access(hdl, cont, in->cqi_bits))
		return -DER_NO_PERM;

	/* Get nsnapshots */
	d_iov_set(&value, &snap_count, sizeof(snap_count));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		return rc;
	}
	out->cqo_snap_count = snap_count;

	/* Get latest snapshot */
	if (snap_count > 0) {
		d_iov_t		key_out;

		rc = rdb_tx_query_key_max(tx, &cont->c_snaps, &key_out);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to query lsnapshot, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}
		out->cqo_lsnapshot = *(uint64_t *)key_out.iov_buf;
		D_DEBUG(DF_DSMS, DF_CONT": got lsnapshot="DF_X64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), out->cqo_lsnapshot);
	}

	/* need RF to process co_status */
	if (in->cqi_bits & DAOS_CO_QUERY_PROP_CO_STATUS)
		in->cqi_bits |= DAOS_CO_QUERY_PROP_REDUN_FAC;

	/* Currently DAOS_CO_QUERY_TGT not used; code kept for future expansion. */
	if (in->cqi_bits & DAOS_CO_QUERY_TGT) {
		/* need RF if user query cont_info */
		in->cqi_bits |= DAOS_CO_QUERY_PROP_REDUN_FAC;
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
	if (rc) {
		D_ERROR(DF_CONT": cont_prop_read failed "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cqi_op.ci_uuid), DP_RC(rc));
		goto out;
	}

	/* if user queries co_status and UNCLEAN flag not set before, check
	 * the cont rf an pool map to verify co_status.
	 */
	if ((in->cqi_bits & DAOS_CO_QUERY_PROP_CO_STATUS) &&
	    cont_status_is_healthy(prop, &last_ver)) {
		D_ASSERT(in->cqi_bits & DAOS_CO_QUERY_PROP_REDUN_FAC);
		rc = cont_status_check(tx, pool_hdl->sph_pool, cont, in, prop,
				       last_ver);
		if (rc) {
			D_ERROR(DF_CONT": cont_status_verify failed "DF_RC"\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid,
					in->cqi_op.ci_uuid), DP_RC(rc));
			goto out;
		}
	}

	if (DAOS_FAIL_CHECK(DAOS_FORCE_PROP_VERIFY)) {
		daos_prop_t		*iv_prop = NULL;
		struct daos_prop_entry	*entry, *iv_entry;
		int			 i;

		D_ALLOC_PTR(iv_prop);
		if (iv_prop == NULL)
			return -DER_NOMEM;

		rc = cont_iv_prop_fetch(pool_hdl->sph_pool->sp_uuid,
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
			case DAOS_PROP_CO_STATUS:
			case DAOS_PROP_CO_EC_CELL_SZ:
			case DAOS_PROP_CO_ALLOCED_OID:
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
			case DAOS_PROP_CO_ROOTS:
				if (memcmp(entry->dpe_val_ptr,
					   iv_entry->dpe_val_ptr,
					   sizeof(struct daos_prop_co_roots)))
					rc = -DER_IO;
				break;

			default:
				D_ASSERTF(0, "bad dpe_type %d\n",
					  entry->dpe_type);
				break;
			};
		}
		daos_prop_free(iv_prop);
	}

out:
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

/* pre-processing for DAOS_PROP_CO_STATUS, set the pool map version */
static bool
set_prop_co_status_pre_process(struct ds_pool *pool, struct cont *cont,
			       daos_prop_t *prop_in)
{
	struct daos_prop_entry	*entry;
	struct daos_co_status	 co_status = { 0 };
	bool			 clear_stat;

	entry = daos_prop_entry_get(prop_in, DAOS_PROP_CO_STATUS);
	if (entry == NULL)
		return false;

	daos_prop_val_2_co_status(entry->dpe_val, &co_status);
	D_ASSERT(co_status.dcs_status == DAOS_PROP_CO_HEALTHY ||
		 co_status.dcs_status == DAOS_PROP_CO_UNCLEAN);
	clear_stat = (co_status.dcs_status == DAOS_PROP_CO_HEALTHY);
	co_status.dcs_pm_ver = ds_pool_get_version(pool);
	co_status.dcs_flags = 0;
	entry->dpe_val = daos_prop_co_status_2_val(&co_status);
	D_DEBUG(DF_DSMS, DF_CONT" updating co_status - status %s, pm_ver %d.\n",
		DP_CONT(pool->sp_uuid, cont->c_uuid),
		co_status.dcs_status == DAOS_PROP_CO_HEALTHY ?
		"DAOS_PROP_CO_HEALTHY" : "DAOS_PROP_CO_UNCLEAN",
		co_status.dcs_pm_ver);

	return clear_stat;
}

/* Sanity check set-prop label, and update cs_uuids KVS */
static int
check_set_prop_label(struct rdb_tx *tx, struct ds_pool *pool, struct cont *cont,
		     daos_prop_t *prop_in, daos_prop_t *prop_old)
{
	struct daos_prop_entry	*in_ent;
	char			*in_lbl;
	struct daos_prop_entry	*def_ent;
	char			*def_lbl;
	struct daos_prop_entry	*old_ent;
	char			*old_lbl;
	d_iov_t			 key;
	d_iov_t			 val;
	uuid_t			 match_cuuid;
	int			 rc;

	/* If label property not in the request, nothing more to do */
	in_ent = daos_prop_entry_get(prop_in, DAOS_PROP_CO_LABEL);
	if (in_ent == NULL)
		return 0;

	/* Verify request label conforms to rules */
	in_lbl = in_ent->dpe_str;
	if (!daos_label_is_valid(in_lbl)) {
		D_ERROR(DF_UUID": invalid label: %s\n", DP_UUID(cont->c_uuid),
			in_lbl);
		return -DER_INVAL;
	}

	/* Verify request label is not default. */
	def_ent = daos_prop_entry_get(&cont_prop_default, DAOS_PROP_CO_LABEL);
	D_ASSERT(def_ent != NULL);
	def_lbl = def_ent->dpe_str;
	if (strncmp(def_lbl, in_lbl, DAOS_PROP_LABEL_MAX_LEN) == 0) {
		D_ERROR(DF_UUID": invalid label matches default: %s\n",
			DP_UUID(cont->c_uuid), in_lbl);
		return -DER_INVAL;
	}

	/* If specified label matches existing label, nothing more to do */
	old_ent = daos_prop_entry_get(prop_old, DAOS_PROP_CO_LABEL);
	D_ASSERT(old_ent != NULL);
	old_lbl = old_ent->dpe_str;
	if (strncmp(old_lbl, in_lbl, DAOS_PROP_LABEL_MAX_LEN) == 0)
		return 0;

	/* Insert new label into cs_uuids KVS, fail if already in use */
	d_iov_set(&key, in_lbl, strnlen(in_lbl, DAOS_PROP_MAX_LABEL_BUF_LEN));
	d_iov_set(&val, match_cuuid, sizeof(uuid_t));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_uuids, &key, &val);
	if (rc != -DER_NONEXIST) {
		if (rc != 0) {
			D_ERROR(DF_UUID": lookup label (%s) failed: "DF_RC"\n",
				DP_UUID(cont->c_uuid), in_lbl, DP_RC(rc));
			return rc;	/* other lookup failure */
		}
		D_ERROR(DF_UUID": non-unique label (%s) matches different "
			"container "DF_UUID"\n", DP_UUID(cont->c_uuid),
			in_lbl, DP_UUID(match_cuuid));
		return -DER_EXIST;
	}

	d_iov_set(&val, cont->c_uuid, sizeof(uuid_t));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_uuids, &key, &val);
	if (rc != 0) {
		D_ERROR(DF_UUID": update cs_uuids failed: "DF_RC"\n",
			DP_UUID(cont->c_uuid), DP_RC(rc));
		return rc;
	}
	D_DEBUG(DB_MD, DF_UUID": inserted new label in cs_uuids KVS: %s\n",
		DP_UUID(cont->c_uuid), in_lbl);

	/* Remove old label from cs_uuids KVS, if applicable */
	d_iov_set(&key, old_lbl, strnlen(old_lbl, DAOS_PROP_MAX_LABEL_BUF_LEN));
	d_iov_set(&val, match_cuuid, sizeof(uuid_t));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_uuids, &key, &val);
	if (rc != -DER_NONEXIST) {
		if (rc != 0) {
			D_ERROR(DF_UUID": lookup label (%s) failed: "DF_RC"\n",
				DP_UUID(cont->c_uuid), old_lbl, DP_RC(rc));
			return rc;
		}
		d_iov_set(&val, NULL, 0);
		rc = rdb_tx_delete(tx, &cont->c_svc->cs_uuids, &key);
		if (rc != 0) {
			D_ERROR(DF_UUID": delete label (%s) failed: "DF_RC"\n",
				DP_UUID(cont->c_uuid), old_lbl, DP_RC(rc));
			return rc;
		}
		D_DEBUG(DB_MD, DF_UUID": deleted original label in cs_uuids KVS: %s\n",
			DP_UUID(cont->c_uuid), old_lbl);
	}

	return 0;
}

static int
set_prop(struct rdb_tx *tx, struct ds_pool *pool,
	 struct cont *cont, uint64_t sec_capas, uuid_t hdl_uuid,
	 daos_prop_t *prop_in)
{
	int			 rc;
	daos_prop_t		*prop_old = NULL;
	daos_prop_t		*prop_iv = NULL;
	bool			 clear_stat;

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
	clear_stat = set_prop_co_status_pre_process(pool, cont, prop_in);
	prop_iv = daos_prop_merge(prop_old, prop_in);
	if (prop_iv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* If label property given, run sanity checks & update cs_uuids */
	rc = check_set_prop_label(tx, pool, cont, prop_in, prop_old);
	if (rc != 0)
		goto out;

	rc = cont_prop_write(tx, &cont->c_prop, prop_in, false);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Update prop IV with merged prop */
	rc = cont_iv_prop_update(pool->sp_iv_ns, cont->c_uuid, prop_iv);
	if (rc) {
		D_ERROR(DF_UUID": failed to update prop IV for cont, "
			DF_RC"\n", DP_UUID(cont->c_uuid), DP_RC(rc));
		goto out;
	}

	if (clear_stat) {
		/* to notify each tgt server to do ds_cont_rf_check() */
		rc = ds_pool_iv_map_update(pool, NULL, 0);
		if (rc)
			D_ERROR(DF_UUID": ds_pool_iv_map_update failed, "
				DF_RC"\n", DP_UUID(cont->c_uuid), DP_RC(rc));
	}

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
	if (prop == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ACL;
	prop->dpp_entries[0].dpe_val_ptr = daos_acl_dup(acl);
	if (prop->dpp_entries[0].dpe_val_ptr == NULL)
		D_GOTO(out_prop, rc = -DER_NOMEM);

	rc = set_prop(tx, pool_hdl->sph_pool, cont, hdl->ch_sec_capas,
		      hdl_uuid, prop);

out_prop:
	daos_prop_free(prop);
out:
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
	struct daos_acl			*acl = NULL;
	int				 rc = 0;

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
	struct rdb_tx  *cia_tx;
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

	if (buf->rb_nrecs % RECS_BUF_RECS_PER_YIELD == 0) {
		ABT_thread_yield();
		rc = rdb_tx_revalidate(arg->cia_tx);
		if (rc != 0) {
			D_WARN("revalidate RDB TX: "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}
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

	arg.cia_tx = &tx;
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
	uuid_t				 pool_uuid;

	/* Number of containers in pool and conts[] index while counting */
	uint64_t			 ncont;

	/* conts[]: capacity*/
	uint64_t			 conts_len;
	struct daos_pool_cont_info	*conts;
	struct cont_svc			*svc;
	struct rdb_tx			*tx;
};

/* callback function for list containers iteration. */
static int
enum_cont_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct list_cont_iter_args	*ap = varg;
	struct daos_pool_cont_info	*cinfo;
	uuid_t				 cont_uuid;
	struct cont			*cont;
	daos_prop_t			*prop = NULL;
	int				 rc;
	(void)val;

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

		D_REALLOC_ARRAY(ptr, ap->conts, ap->conts_len, realloc_elems);
		if (ptr == NULL)
			return -DER_NOMEM;
		ap->conts = ptr;
		ap->conts_len = realloc_elems;
	}

	cinfo = &ap->conts[ap->ncont];
	ap->ncont++;
	uuid_copy(cinfo->pci_uuid, cont_uuid);

	/* Get the label property. FIXME: cont_lookup no need to search
	 * in cs_conts, since we're iterating that KVS already.
	 * Isn't val the container properties KVS? Can it be used directly?
	 */
	rc = cont_lookup(ap->tx, ap->svc, cont_uuid, &cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": lookup cont failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		return rc;
	}
	rc = cont_prop_read(ap->tx, cont, DAOS_CO_QUERY_PROP_LABEL, &prop);
	cont_put(cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_prop_read() failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		return rc;
	}
	strncpy(cinfo->pci_label, prop->dpp_entries[0].dpe_str,
		DAOS_PROP_LABEL_MAX_LEN);
	cinfo->pci_label[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	daos_prop_free(prop);
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
	args.svc = svc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	args.tx = &tx;
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
	struct cont_op_in		*in = crt_req_get(rpc);
	d_iov_t				 key;
	d_iov_t				 value;
	struct container_hdl		 hdl;
	struct cont_pool_metrics	*metrics;
	int				 rc;

	metrics = pool_hdl->sph_pool->sp_metrics[DAOS_CONT_MODULE];

	switch (opc_get(rpc->cr_opc)) {
	case CONT_OPEN:
	case CONT_OPEN_BYLABEL:
		d_tm_inc_counter(metrics->cpm_open_count, 1);
		d_tm_inc_gauge(metrics->cpm_open_cont_gauge, 1);
		rc = cont_open(tx, pool_hdl, cont, rpc);
		break;
	case CONT_CLOSE:
		d_tm_inc_counter(metrics->cpm_close_count, 1);
		d_tm_dec_gauge(metrics->cpm_open_cont_gauge, 1);
		rc = cont_close(tx, pool_hdl, cont, rpc);
		break;
	case CONT_DESTROY:
	case CONT_DESTROY_BYLABEL:
		d_tm_inc_counter(metrics->cpm_destroy_count, 1);
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
	struct cont_op_in		*in = crt_req_get(rpc);
	struct cont_open_bylabel_in	*olbl_in = NULL;
	struct cont_open_bylabel_out	*olbl_out = NULL;
	struct cont_destroy_bylabel_in	*dlbl_in = NULL;
	struct rdb_tx			 tx;
	crt_opcode_t			 opc = opc_get(rpc->cr_opc);
	struct cont			*cont = NULL;
	int				 rc;

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
	case CONT_OPEN_BYLABEL:
		olbl_in = crt_req_get(rpc);
		olbl_out = crt_reply_get(rpc);
		rc = cont_lookup_bylabel(&tx, svc, olbl_in->coli_label, &cont);
		if (rc != 0)
			D_GOTO(out_lock, rc);
		/* NB: call common cont_op_with_cont() same as CONT_OPEN case */
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc);
		uuid_copy(olbl_out->colo_uuid, cont->c_uuid);
		cont_put(cont);
		break;
	case CONT_DESTROY_BYLABEL:
		dlbl_in = crt_req_get(rpc);
		rc = cont_lookup_bylabel(&tx, svc, dlbl_in->cdli_label, &cont);
		if (rc != 0)
			D_GOTO(out_lock, rc);
		/* NB: call common cont_op_with_cont() same as CONT_DESTROY */
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc);
		cont_put(cont);
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
	struct cont_op_in		*in = crt_req_get(rpc);
	struct cont_op_out		*out = crt_reply_get(rpc);
	struct ds_pool_hdl		*pool_hdl;
	crt_opcode_t			 opc = opc_get(rpc->cr_opc);
	daos_prop_t			*prop = NULL;
	struct cont_svc			*svc;
	int				 rc;

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
	if (opc == CONT_OPEN_BYLABEL) {
		struct cont_open_bylabel_in	*lin = crt_req_get(rpc);
		struct cont_open_bylabel_out	*lout = crt_reply_get(rpc);

		D_DEBUG(DF_DSMS, DF_CONT":%s: replying rpc %p: hdl="DF_UUID
			" opc=%u rc=%d\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, lout->colo_uuid),
				lin->coli_label, rpc, DP_UUID(in->ci_hdl),
				opc, rc);
	} else if (opc == CONT_DESTROY_BYLABEL) {
		struct cont_destroy_bylabel_in	*lin = crt_req_get(rpc);

		D_DEBUG(DF_DSMS, DF_UUID":%s: replying rpc %p: opc=%u, rc=%d\n",
			DP_UUID(pool_hdl->sph_pool->sp_uuid), lin->cdli_label,
			rpc, opc, rc);
	} else {
			D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: hdl="DF_UUID
			" opc=%u rc=%d\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
			DP_UUID(in->ci_hdl), opc, rc);
	}
	ds_pool_hdl_put(pool_hdl);
out:
	/* cleanup the properties for cont_query */
	if (opc == CONT_QUERY) {
		struct cont_query_out  *cqo = crt_reply_get(rpc);

		prop = cqo->cqo_prop;
	} else if ((opc == CONT_OPEN) || (opc == CONT_OPEN_BYLABEL)) {
		struct cont_open_out *coo = crt_reply_get(rpc);

		prop = coo->coo_prop;
	}
	out->co_rc = rc;
	crt_reply_send(rpc);
	daos_prop_free(prop);
}

int
ds_cont_oid_fetch_add(uuid_t po_uuid, uuid_t co_uuid, uint64_t num_oids, uint64_t *oid)
{
	struct cont_svc		*svc;
	struct rdb_tx		tx;
	struct cont		*cont = NULL;
	d_iov_t			value;
	uint64_t		alloced_oid;
	int			rc;

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup_leader(po_uuid, 0, &svc, NULL);
	if (rc != 0)
		return rc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->cs_lock);

	rc = cont_lookup(&tx, svc, co_uuid, &cont);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	/* Read the max OID from the container metadata */
	d_iov_set(&value, &alloced_oid, sizeof(alloced_oid));
	rc = rdb_tx_lookup(&tx, &cont->c_prop, &ds_cont_prop_alloced_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup alloced_oid: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out_cont, rc);
	}

	/** Set the oid for the caller */
	*oid = alloced_oid;
	/** Increment the alloced_oid by how many oids user requested */
	alloced_oid += num_oids;

	/* Update the max OID */
	rc = rdb_tx_update(&tx, &cont->c_prop, &ds_cont_prop_alloced_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to update alloced_oid: %d\n",
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

	if (rc != 0)
		D_GOTO(out_lock, rc);

	D_ASSERT(prop != NULL);
	D_ASSERT(prop->dpp_nr == CONT_PROP_NUM);

	*prop_out = prop;

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_put:
	cont_svc_put_leader(svc);
	return rc;
}
