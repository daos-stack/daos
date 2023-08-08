/*
 * (C) Copyright 2016-2023 Intel Corporation.
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
#include <daos/pool.h>
#include <daos_srv/pool.h>
#include <daos_srv/rdb.h>
#include <daos_srv/security.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"

#define DAOS_POOL_GLOBAL_VERSION_WITH_CONT_MDTIMES 2
#define DAOS_POOL_GLOBAL_VERSION_WITH_CONT_NHANDLES 2
#define DAOS_POOL_GLOBAL_VERSION_WITH_CONT_EX_EVICT 2

static int
cont_prop_read(struct rdb_tx *tx, struct cont *cont, uint64_t bits,
	       daos_prop_t **prop_out, bool ignore_not_set);

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

	D_ERROR("Domain contains %d failed components, allows at most %d\n", num_failed,
		num_allowed_failures);
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
	int rc;

	D_ASSERT(svc->cs_pool == NULL);
	rc = ds_pool_lookup(svc->cs_pool_uuid, &svc->cs_pool);
	if (rc != 0)  {
		D_ERROR(DF_UUID": pool lookup failed: "DF_RC"\n",
			DP_UUID(svc->cs_pool_uuid), DP_RC(rc));
		return rc;
	}
	D_ASSERT(svc->cs_pool != NULL);

	rc = cont_svc_ec_agg_leader_start(svc);
	if (rc != 0)
		D_ERROR(DF_UUID": start ec agg leader failed: "DF_RC"\n",
			DP_UUID(svc->cs_pool_uuid), DP_RC(rc));

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
	struct rdb_kvs_attr	attr;
	int			rc;

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


/* Update container open and metadata modify times, if the co_md_times key exists in rdb */
static int
get_metadata_times(struct rdb_tx *tx, struct cont *cont, bool update_otime, bool update_mtime,
		   struct co_md_times *times)
{
	struct co_md_times	mdtimes = {0};
	d_iov_t			value;
	bool			do_update = (update_otime || update_mtime);
	int			rc;

	if ((cont == NULL) || (!do_update && (times == NULL)))
		return 0;

	/* Lookup most recent metadata times (may need to keep the mtime in the update below) */
	d_iov_set(&value, &mdtimes, sizeof(mdtimes));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_co_md_times, &value);
	if (rc == -DER_NONEXIST) {
		rc = 0;			/* pool/container has old layout without metadata times */
		goto out;
	} else if (rc != 0) {
		D_ERROR(DF_CONT": rdb_tx_lookup co_md_times failed, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		goto out;
	}

	if (do_update) {
		uint64_t		cur_hlc;

		cur_hlc = d_hlc_get();
		mdtimes.otime = update_otime ? cur_hlc : mdtimes.otime;
		mdtimes.mtime = update_mtime ? cur_hlc : mdtimes.mtime;

		rc = rdb_tx_update(tx, &cont->c_prop, &ds_cont_prop_co_md_times, &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update metadata times, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}
	}

	D_DEBUG(DB_MD, DF_CONT": metadata times: open(%s)="DF_X64", modify(%s)="DF_X64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		update_otime ? "updated" : "unchanged", mdtimes.otime,
		update_mtime ? "updated" : "unchanged", mdtimes.mtime);

out:
	if ((rc == 0) && (times != NULL))
		*times = mdtimes;

	return rc;
}

enum nhandles_op {
	NHANDLES_GET = 0,
	NHANDLES_PRE_INCREMENT,
	NHANDLES_PRE_DECREMENT
};

/** Number of handles hash table record - cache rdb num_handles within one tx closing a batch. */
struct nhandles_ht_rec {
	uuid_t		nhr_cuuid;
	uint32_t	nhr_nhandles;
	uint32_t	nhr_ref;
	d_list_t	nhr_hlink;
};

static inline struct nhandles_ht_rec *
nhandles_rec_obj(d_list_t *rlink)
{
	return container_of(rlink, struct nhandles_ht_rec, nhr_hlink);
}

static void
nhandles_ht_rec_addref(struct d_hash_table *ht, d_list_t *link)
{
	struct nhandles_ht_rec *rec = nhandles_rec_obj(link);

	rec->nhr_ref++;
	D_DEBUG(DB_TRACE, "rec=%p, incremented nhr_ref to %u\n", rec, rec->nhr_ref);
}

static bool
nhandles_ht_rec_decref(struct d_hash_table *ht, d_list_t *link)
{
	struct nhandles_ht_rec *rec = nhandles_rec_obj(link);

	rec->nhr_ref--;
	D_DEBUG(DB_TRACE, "rec=%p, decremented nhr_ref to %u\n", rec, rec->nhr_ref);
	return (rec->nhr_ref == 0);
}

static bool
nhandles_ht_cmp_keys(struct d_hash_table *ht, d_list_t *rlink, const void *key, unsigned int ksize)
{
	struct nhandles_ht_rec *rec = nhandles_rec_obj(rlink);

	return uuid_compare(key, rec->nhr_cuuid) == 0;
}

static void
nhandles_ht_rec_free(struct d_hash_table *htable, d_list_t *link)
{
	struct nhandles_ht_rec *rec = nhandles_rec_obj(link);

	D_ASSERT(d_hash_rec_unlinked(&rec->nhr_hlink));
	D_DEBUG(DB_MD, "Free rec=%p\n", rec);
	D_FREE(rec);
}

static d_hash_table_ops_t nhandles_hops = {
	.hop_key_cmp = nhandles_ht_cmp_keys,
	.hop_rec_addref = nhandles_ht_rec_addref,
	.hop_rec_decref = nhandles_ht_rec_decref,
	.hop_rec_free = nhandles_ht_rec_free,
};

static int
nhandles_ht_create(struct d_hash_table *nht)
{
	return d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 6 /* bits */, NULL /* priv */,
					   &nhandles_hops, nht);
}

static int
nhandles_ht_destroy(struct d_hash_table *nht)
{
	int rc;

	rc = d_hash_table_destroy_inplace(nht, true);
	if (rc)
		D_ERROR("d_hash_table_destroy_inplace() failed, "DF_RC"\n", DP_RC(rc));
	return rc;
}

/* Get, optionally update container number of handles. Use "nhandles cache" (nhc) if provided. */
static int
get_nhandles(struct rdb_tx *tx, struct d_hash_table *nhc, struct cont *cont, enum nhandles_op op,
	     uint32_t *nhandles)
{
	uint32_t		result = 0;
	d_iov_t			value;
	uint32_t		lookup_val;		/* value from DRAM cache or rdb */
	struct nhandles_ht_rec *rec = NULL;
	bool			do_update = true;
	uint32_t		pool_global_version = cont->c_svc->cs_pool->sp_global_version;
	int			rc = 0;

	/* Test if pool/container has old layout without nhandles */
	if (pool_global_version < DAOS_POOL_GLOBAL_VERSION_WITH_CONT_NHANDLES)
		goto out;

	/* Caller performing multiple updates in the tx: insert into, or get value from HT cache. */
	if (nhc) {
		d_list_t *hlink = d_hash_rec_find(nhc, cont->c_uuid, sizeof(uuid_t));

		if (hlink) {
			rec = nhandles_rec_obj(hlink);
			lookup_val = rec->nhr_nhandles;
		}
	}

	/* If no cache, or miss, lookup value in rdb. */
	if (rec == NULL) {
		d_iov_set(&value, &lookup_val, sizeof(lookup_val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nhandles, &value);
		if (rc) {
			D_ERROR(DF_CONT": rdb_tx_lookup nhandles failed, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}

		/* ... and cache it, if applicable. */
		if (nhc) {
			D_ALLOC_PTR(rec);
			if (rec == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			rec->nhr_ref = 1;
			D_DEBUG(DB_MD, DF_CONT": alloc rec=%p\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rec);
			uuid_copy(rec->nhr_cuuid, cont->c_uuid);
			rec->nhr_nhandles = lookup_val;
			rc = d_hash_rec_insert(nhc, rec->nhr_cuuid, sizeof(uuid_t), &rec->nhr_hlink,
					       true);
			if (rc != 0) {
				D_ERROR(DF_CONT": error inserting into nhandles cache" DF_RC"\n",
					DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
					DP_RC(rc));
				D_FREE(rec);
				goto out;
			}
		}
	}

	switch (op) {
	case NHANDLES_GET:
		do_update = false;
		result = lookup_val;
		break;
	case NHANDLES_PRE_INCREMENT:
		result = lookup_val + 1;
		break;
	case NHANDLES_PRE_DECREMENT:
		result = lookup_val - 1;
		break;
	default:
		D_ASSERTF(0, "invalid op=%d\n", op);
		break;
	}

	/* Update persistent and, if applicable, cached nhandles value */
	if (do_update) {
		d_iov_set(&value, &result, sizeof(result));
		rc = rdb_tx_update(tx, &cont->c_prop, &ds_cont_prop_nhandles, &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": rdb_tx_update nhandles failed, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}

		if (rec)
			rec->nhr_nhandles = result;
	}

out:
	if (rec)
		d_hash_rec_decref(nhc, &rec->nhr_hlink);
	/* Note: if rc != 0, and rec was allocated/inserted, it will be freed in HT destroy. */
	if ((rc == 0) && (nhandles != NULL))
		*nhandles = result;
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
		D_DEBUG(DB_MD, DF_CONT": no label, lookup by UUID "DF_UUIDF
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
	bool			 inherit_redunc_fac = true;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		/* skip not set property entry */
		if (!daos_prop_is_set(entry))
			continue;
		entry_def = daos_prop_entry_get(prop_def, entry->dpe_type);
		if (entry_def == NULL) {
			D_ERROR("type: %d not supported in default prop.\n",
				entry->dpe_type);
			return -DER_INVAL;
		}
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
		case DAOS_PROP_CO_REDUN_LVL:
		case DAOS_PROP_CO_SNAPSHOT_MAX:
		case DAOS_PROP_CO_COMPRESS:
		case DAOS_PROP_CO_ENCRYPT:
		case DAOS_PROP_CO_DEDUP:
		case DAOS_PROP_CO_EC_CELL_SZ:
		case DAOS_PROP_CO_ALLOCED_OID:
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
		case DAOS_PROP_CO_EC_PDA:
		case DAOS_PROP_CO_RP_PDA:
		case DAOS_PROP_CO_PERF_DOMAIN:
		case DAOS_PROP_CO_SCRUBBER_DISABLED:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			inherit_redunc_fac = false;
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
		case DAOS_PROP_CO_GLOBAL_VERSION:
		case DAOS_PROP_CO_OBJ_VERSION:
			D_ERROR("container global/obj %u version could be not set\n",
				entry->dpe_type);
			return -DER_INVAL;
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

	if (pool_hdl->sph_global_ver > 0 && inherit_redunc_fac) {
		entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_REDUN_FAC);
		D_ASSERT(entry_def != NULL);
		/* No specified redun fac from container, inherit from pool */
		entry_def->dpe_val = pool_hdl->sph_pool->sp_redun_fac;
	}

	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_EC_PDA);
	if (pool_hdl->sph_global_ver > 0)
		D_ASSERT(entry_def != NULL);
	if (entry_def && entry_def->dpe_val == 0) {
		/* No specified ec pda from container, inherit from pool */
		D_ASSERT(pool_hdl->sph_pool->sp_ec_pda != 0);
		entry_def->dpe_val = pool_hdl->sph_pool->sp_ec_pda;
	}

	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_RP_PDA);
	if (pool_hdl->sph_global_ver > 0)
		D_ASSERT(entry_def != NULL);
	if (entry_def && entry_def->dpe_val == 0) {
		/* No specified ec pda from container, inherit from pool */
		D_ASSERT(pool_hdl->sph_pool->sp_rp_pda != 0);
		entry_def->dpe_val = pool_hdl->sph_pool->sp_rp_pda;
	}

	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_PERF_DOMAIN);
	if (pool_hdl->sph_global_ver > 2)
		D_ASSERT(entry_def != NULL);
	if (entry_def && entry_def->dpe_val == 0) {
		/* No specified perf_domain from container, inherit from pool */
		entry_def->dpe_val = pool_hdl->sph_pool->sp_perf_domain;
	}

	/* inherit global version from pool*/
	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_GLOBAL_VERSION);
	if (entry_def)
		entry_def->dpe_val = pool_hdl->sph_global_ver;

	/* inherit object version from pool*/
	entry_def = daos_prop_entry_get(prop_def, DAOS_PROP_CO_OBJ_VERSION);
	if (entry_def)
		entry_def->dpe_val = pool_hdl->sph_obj_ver;
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
		case DAOS_PROP_CO_EC_PDA:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_ec_pda,
					   &value);
			break;
		case DAOS_PROP_CO_RP_PDA:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_rp_pda,
					   &value);
			break;
		case DAOS_PROP_CO_PERF_DOMAIN:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_cont_prop_perf_domain,
					   &value);
			break;
		case DAOS_PROP_CO_GLOBAL_VERSION:
			/* type of the property in rdb is uint32_t */
			if (entry->dpe_val <= UINT_MAX) {
				uint32_t cont_ver = (uint32_t)entry->dpe_val;

				d_iov_set(&value, &cont_ver, sizeof(cont_ver));
				rc = rdb_tx_update(tx, kvs,
						   &ds_cont_prop_cont_global_version, &value);
				D_DEBUG(DB_MD, "wrote cont_global_version = %u\n", cont_ver);
			} else {
				rc = -DER_INVAL;
			}
			break;
		case DAOS_PROP_CO_OBJ_VERSION:
			if (entry->dpe_val <= UINT_MAX) {
				uint32_t obj_ver = (uint32_t)entry->dpe_val;

				d_iov_set(&value, &obj_ver, sizeof(obj_ver));
				rc = rdb_tx_update(tx, kvs, &ds_cont_prop_cont_obj_version,
						   &value);
				D_DEBUG(DB_MD, "update obj_version = %u\n", obj_ver);
			} else {
				rc = -DER_INVAL;
			}
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
		case DAOS_PROP_CO_SCRUBBER_DISABLED:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs,
					   &ds_cont_prop_scrubber_disabled,
					   &value);
			if (rc)
				return rc;
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

	D_DEBUG(DB_MD, DF_CONT": processing rpc %p\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc);

	/* Verify the pool handle capabilities. */
	if (!ds_sec_pool_can_create_cont(pool_hdl->sph_sec_capas)) {
		D_ERROR(DF_CONT": permission denied to create cont\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid));
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	/* Determine if the label property was supplied, and if so,
	 * verify that it is not the default unset label.
	 */
	def_lbl_ent = daos_prop_entry_get(&cont_prop_default, DAOS_PROP_CO_LABEL);
	D_ASSERT(def_lbl_ent != NULL);
	lbl_ent = daos_prop_entry_get(in->cci_prop, DAOS_PROP_CO_LABEL);
	if (lbl_ent != NULL && lbl_ent->dpe_str != NULL) {
		if (strncmp(def_lbl_ent->dpe_str, lbl_ent->dpe_str,
			    DAOS_PROP_LABEL_MAX_LEN) == 0) {
			D_ERROR(DF_CONT": label is the same as default label\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid));
			D_GOTO(out, rc = -DER_INVAL);
		}
		lbl = lbl_ent->dpe_str;
	}

	/* duplicate the default properties, overwrite it with cont create
	 * parameter (write to rdb below).
	 */
	if (pool_hdl->sph_global_ver < 1)
		prop_dup = daos_prop_dup(&cont_prop_default_v0, false, false);
	else
		prop_dup = daos_prop_dup(&cont_prop_default, false, false);
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

	/* Check if a container with this UUID and label already exists */
	rc = cont_existence_check(tx, svc, pool_hdl->sph_pool->sp_uuid,
				  in->cci_op.ci_uuid, lbl);
	if (rc != -DER_NONEXIST) {
		if (rc == 0)
			D_DEBUG(DB_MD, DF_CONT": container already exists\n",
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

	/* Create the container property KVS under the container KVS. */
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

	/* Create a path to the container property KVS. */
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

	/* Create the ALLOCED_OID property. */
	d_iov_set(&value, &alloced_oid, sizeof(alloced_oid));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_alloced_oid, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": create alloced_oid prop failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
	}

	/* Set initial container open and metadata modify times. */
	if (pool_hdl->sph_global_ver >= DAOS_POOL_GLOBAL_VERSION_WITH_CONT_MDTIMES) {
		struct co_md_times	mdtimes;

		mdtimes.otime = 0;
		mdtimes.mtime = d_hlc_get();
		d_iov_set(&value, &mdtimes, sizeof(mdtimes));
		rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_co_md_times, &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": create co_md_times failed: "DF_RC"\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid),
				DP_RC(rc));
			D_GOTO(out_kvs, rc);
		}
		D_DEBUG(DB_MD, DF_CONT": set metadata times: open="DF_X64", modify="DF_X64"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), mdtimes.otime,
			mdtimes.mtime);
	}

	/* Number of open handles */
	if (pool_hdl->sph_global_ver >= DAOS_POOL_GLOBAL_VERSION_WITH_CONT_NHANDLES) {
		uint32_t	nhandles = 0;

		d_iov_set(&value, &nhandles, sizeof(nhandles));
		rc = rdb_tx_update(tx, &kvs, &ds_cont_prop_nhandles, &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": create nhandles failed: "DF_RC"\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid),
				DP_RC(rc));
			goto out_kvs;
		}
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
		D_DEBUG(DB_MD, DF_CONT": creating container, label: %s\n",
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
		D_GOTO(out_kvs, rc);
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
		D_GOTO(out_kvs, rc);
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
		D_GOTO(out_kvs, rc);
	}

	/* Create the oit oids index KVS. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_prop_oit_oids, &attr);
	if (rc != 0) {
		D_ERROR(DF_CONT" failed to create container oit oids KVS: "
			""DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->cci_op.ci_uuid), DP_RC(rc));
		D_GOTO(out_kvs, rc);
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

	D_DEBUG(DB_MD, DF_CONT": bcasting\n",
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
	D_DEBUG(DB_MD, DF_CONT": bcasted: %d\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
	return rc;
}

/*
 * Doesn't allocate anything new, just passes back pointers to data inside the
 * prop.
 */
static void
get_cont_prop_access_info(daos_prop_t *prop, struct d_ownership *owner,
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
	struct cont	       *fha_cont;
	struct recs_buf		fha_buf;
	d_iov_t		       *fha_cred;
};

/*
 * Does the container handle represented by key belong to the user represented
 * by arg->fha_cred? This function may return
 *
 *   - a error,
 *   - zero if "doesn't belong to", or
 *   - a positive integer if "belongs to".
 */
static int
belongs_to_user(d_iov_t *key, struct find_hdls_by_cont_arg *arg)
{
	struct cont	       *cont = arg->fha_cont;
	struct container_hdl   *hdl;
	d_iov_t			value;
	d_iov_t			cred;
	struct ds_pool_hdl     *pool_hdl;
	int			rc;

	d_iov_set(&value, NULL, sizeof(struct container_hdl));
	rc = rdb_tx_lookup(arg->fha_tx, &cont->c_svc->cs_hdls, key, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": look up container handle "DF_UUIDF": "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_UUID(key->iov_buf),
			DP_RC(rc));
		return rc;
	}
	hdl = value.iov_buf;

	/* Usually we already have the pool handle in memory. */
	pool_hdl = ds_pool_hdl_lookup(hdl->ch_pool_hdl);
	if (pool_hdl == NULL) {
		/* Otherwise, look it up in the pool metadata via a hack. */
		rc = ds_pool_lookup_hdl_cred(arg->fha_tx, cont->c_svc->cs_pool_uuid,
					     hdl->ch_pool_hdl, &cred);
		if (rc != 0)
			return rc;
	} else {
		cred = pool_hdl->sph_cred;
	}

	rc = ds_sec_creds_are_same_user(&cred, arg->fha_cred);

	if (pool_hdl == NULL)
		D_FREE(cred.iov_buf);
	return rc;
}

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

	if (arg->fha_cred != NULL) {
		rc = belongs_to_user(key, arg);
		if (rc < 0)
			return rc;
		else if (!rc) /* doesn't belong to */
			return 0;
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
evict_hdls(struct rdb_tx *tx, struct cont *cont, bool force, struct ds_pool_hdl *pool_hdl,
	   crt_context_t ctx)
{
	struct find_hdls_by_cont_arg	arg;
	int				rc;

	arg.fha_tx = tx;
	arg.fha_cont = cont;
	rc = recs_buf_init(&arg.fha_buf);
	if (rc != 0)
		return rc;
	arg.fha_cred = (pool_hdl == NULL ? NULL : &pool_hdl->sph_cred);

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
	struct d_ownership		owner;
	struct daos_acl		       *acl;

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p force=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc, in->cdi_force);

	/* Fetch the container props to check access for delete */
	rc = cont_prop_read(tx, cont,
			    DAOS_CO_QUERY_PROP_ACL |
			    DAOS_CO_QUERY_PROP_OWNER |
			    DAOS_CO_QUERY_PROP_OWNER_GROUP |
			    DAOS_CO_QUERY_PROP_LABEL, &prop, true);
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

	rc = evict_hdls(tx, cont, in->cdi_force, NULL /* pool_hdl */, rpc->cr_ctx);
	if (rc != 0)
		goto out_prop;

	rc = cont_destroy_bcast(rpc->cr_ctx, cont->c_svc, cont->c_uuid);
	if (rc != 0)
		goto out_prop;

	cont_ec_agg_delete(cont->c_svc, cont->c_uuid);

	/* Destroy oit oids index KVS. */
	rc = rdb_tx_destroy_kvs(tx, &cont->c_prop, &ds_cont_prop_oit_oids);
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
	D_DEBUG(DB_MD, DF_CONT ": replying rpc: %p " DF_RC "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc, DP_RC(rc));
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

	D_DEBUG(DB_MD, DF_CONT": %s agg boundary eph "DF_X64"->"DF_X64"\n",
		DP_CONT(arg->pool_uuid, arg->cont_uuid),
		cont_child->sc_ec_agg_eph_boundary < arg->min_eph ? "update" : "ignore",
		cont_child->sc_ec_agg_eph_boundary, arg->min_eph);

	if (cont_child->sc_ec_agg_eph_boundary < arg->min_eph)
		cont_child->sc_ec_agg_eph_boundary = arg->min_eph;

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

		rc = map_ranks_init(pool->sp_map, PO_COMP_ST_DOWNOUT | PO_COMP_ST_DOWN,
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

				/* If there are network error or pool map inconsistency,
				 * let's skip the following eph sync, which will fail
				 * anyway.
				 */
				if (daos_crt_network_error(rc) || rc == -DER_GRPVER) {
					D_INFO(DF_UUID": skip refresh due to: "DF_RC"\n",
					       DP_UUID(svc->cs_pool_uuid), DP_RC(rc));
					break;
				}

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
	D_DEBUG(DB_MD, DF_UUID": stop eph ult: rc %d\n",
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
	uuid_t			anonym_uuid;

	D_INIT_LIST_HEAD(&svc->cs_ec_agg_list);
	if (unlikely(ec_agg_disabled))
		return 0;

	D_ASSERT(svc->cs_ec_leader_ephs_req == NULL);
	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
	svc->cs_ec_leader_ephs_req = sched_create_ult(&attr, cont_agg_eph_leader_ult, svc, 0);
	if (svc->cs_ec_leader_ephs_req == NULL) {
		D_ERROR(DF_UUID" Failed to create EC leader eph ULT.\n",
			DP_UUID(svc->cs_pool_uuid));
		return -DER_NOMEM;
	}

	return 0;
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
cont_lookup(struct rdb_tx *tx, const struct cont_svc *svc, const uuid_t uuid, struct cont **cont)
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

	/* c_oit_oids */
	rc = rdb_path_clone(&p->c_prop, &p->c_oit_oids);
	if (rc != 0)
		D_GOTO(err_hdls, rc);
	rc = rdb_path_push(&p->c_oit_oids, &ds_cont_prop_oit_oids);
	if (rc != 0)
		D_GOTO(err_oit_oids, rc);

	*cont = p;
	return 0;

err_oit_oids:
	rdb_path_fini(&p->c_oit_oids);
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
	D_DEBUG(DB_MD, DF_UUID":lookup (len %zu) label=%s -> cuuid="DF_UUID
		", rc=%d\n", DP_UUID(svc->cs_pool_uuid), key.iov_len, label,
		DP_UUID(uuid), rc);
	if (rc != 0)
		return rc;

	/* Look up container by UUID */
	rc = cont_lookup(tx, svc, uuid, cont);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_MD, DF_CONT": successfully found container %s\n",
		DP_CONT(svc->cs_pool_uuid, (*cont)->c_uuid), label);
	return 0;
}

void
cont_put(struct cont *cont)
{
	rdb_path_fini(&cont->c_oit_oids);
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
check_hdl_compatibility(struct rdb_tx *tx, struct cont *cont, uint64_t flags)
{
	d_iov_t	key;
	d_iov_t	value;
	int	rc;

	/* Is there any existing handle for the container? */
	d_iov_set(&key, NULL, 0);
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_fetch(tx, &cont->c_hdls, RDB_PROBE_FIRST, NULL /* key_in */, &key, &value);
	if (rc == -DER_NONEXIST) {
		return 0;
	} else if (rc != 0) {
		D_ERROR(DF_CONT": fetch first handle key: "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		return rc;
	}

	if (flags & DAOS_COO_EX) {
		/* An exclusive open is incompatible with any existing handle. */
		D_DEBUG(DB_MD, DF_CONT": found existing handle\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return -DER_BUSY;
	}

	/*
	 * A non-exclusive open is incompatible with an exclusive handle. We
	 * need to look up the flags of the existing handle we've just found.
	 */
	d_iov_set(&value, NULL, sizeof(struct container_hdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": look up first handle value: "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		return rc;
	}
	if (((struct container_hdl *)value.iov_buf)->ch_flags & DAOS_COO_EX) {
		D_DEBUG(DB_MD, DF_CONT": found existing exclusive handle\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		return -DER_BUSY;
	}

	return 0;
}

static int
cont_open(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	  crt_rpc_t *rpc, int cont_proto_ver)
{
	struct cont_open_in    *in = crt_req_get(rpc);
	struct cont_open_out   *out = crt_reply_get(rpc);
	d_iov_t			key;
	d_iov_t			value;
	daos_prop_t	       *prop = NULL;
	struct container_hdl	chdl;
	char			zero = 0;
	int			rc;
	struct d_ownership	owner;
	struct daos_acl	       *acl;
	bool			is_healthy;
	bool			cont_hdl_opened = false;
	uint32_t		stat_pm_ver = 0;
	uint64_t		sec_capas = 0;
	uint32_t		snap_count;
	uint32_t		nhandles;
	struct co_md_times	mdtimes;
	bool			mdtimes_in_reply = (cont_proto_ver >= CONT_PROTO_VER_WITH_MDTIMES);
	const uint64_t		NOSTAT = (DAOS_COO_RO | DAOS_COO_RO_MDSTATS);
	bool			update_otime = ((in->coi_flags & NOSTAT) == NOSTAT) ? false : true;
	uint32_t		pool_global_version = cont->c_svc->cs_pool->sp_global_version;

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID " flags=" DF_X64 "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc, DP_UUID(in->coi_op.ci_hdl),
		in->coi_flags);

	/* See if this container handle already exists. */
	d_iov_set(&key, in->coi_op.ci_hdl, sizeof(uuid_t));
	d_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != -DER_NONEXIST) {
		D_DEBUG(DB_MD, DF_CONT "/" DF_UUID ": Container handle already open.\n",
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

	if (pool_global_version < DAOS_POOL_GLOBAL_VERSION_WITH_CONT_EX_EVICT &&
	    (in->coi_flags & (DAOS_COO_EX | DAOS_COO_EVICT | DAOS_COO_EVICT_ALL))) {
		D_ERROR(DF_CONT": DAOS_COO_{EX,EVICT,EVICT_ALL} not supported in pool global "
			"version %u: pool global version >= %u required\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), pool_global_version,
			DAOS_POOL_GLOBAL_VERSION_WITH_CONT_EX_EVICT);
		rc = -DER_INVAL;
		goto out;
	}

	/*
	 * Need props to check for pool redundancy requirements and access
	 * control.
	 */
	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, false);
	if (rc != 0)
		D_GOTO(out, rc);
	D_ASSERT(prop != NULL);
	D_ASSERT(prop->dpp_nr <= CONT_PROP_NUM);

	get_cont_prop_access_info(prop, &owner, &acl);

	rc = ds_sec_cont_get_capabilities(in->coi_flags, &pool_hdl->sph_cred, &owner, acl,
					  &sec_capas);
	if (rc != 0) {
		D_ERROR(DF_CONT ": refusing attempt to open with flags " DF_X64 " error: " DF_RC
				"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), in->coi_flags, DP_RC(rc));
		daos_prop_free(prop);
		D_GOTO(out, rc);
	}

	if ((in->coi_flags & DAOS_COO_EVICT_ALL) && !ds_sec_cont_can_evict_all(sec_capas)) {
		D_ERROR(DF_CONT": permission denied evicting all handles\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		daos_prop_free(prop);
		rc = -DER_NO_PERM;
		goto out;
	}

	if ((in->coi_flags & DAOS_COO_EX) && !ds_sec_cont_can_open_ex(sec_capas)) {
		D_ERROR(DF_CONT": permission denied opening exclusively\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
		daos_prop_free(prop);
		rc = -DER_NO_PERM;
		goto out;
	}

	if (!ds_sec_cont_can_open(sec_capas)) {
		D_ERROR(DF_CONT": permission denied opening with flags "
			DF_X64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			in->coi_flags);
		daos_prop_free(prop);
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (in->coi_flags & (DAOS_COO_EVICT | DAOS_COO_EVICT_ALL)) {
		rc = evict_hdls(tx, cont, true /* force */,
				(in->coi_flags & DAOS_COO_EVICT_ALL) ? NULL : pool_hdl,
				rpc->cr_ctx);
		if (rc != 0) {
			daos_prop_free(prop);
			goto out;
		}
	}

	rc = check_hdl_compatibility(tx, cont, in->coi_flags);
	if (rc != 0) {
		daos_prop_free(prop);
		goto out;
	}

	/* Determine pool meets container redundancy factor requirements */
	is_healthy = cont_status_is_healthy(prop, &stat_pm_ver);
	out->coo_op.co_map_version = stat_pm_ver;
	if (is_healthy) {
		int	rf, rlvl;

		rlvl = daos_cont_prop2redunlvl(prop);
		rf = daos_cont_prop2redunfac(prop);
		rc = ds_pool_rf_verify(pool_hdl->sph_pool, stat_pm_ver, rlvl, rf);
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

	/* lookup metadata times (and pre-update open time to reflect this open operation.
	 * NB client/engine may have recent (protocol) versions, however the pool may not
	 * have been upgraded to new layout yet. mdtimes will be zeros in that case.
	 */
	rc = get_metadata_times(tx, cont, update_otime, false /* update_mtime */, &mdtimes);
	if (rc != 0)
		goto out;

	/* include metadata times in reply if client speaks the protocol */
	if (mdtimes_in_reply && (opc_get(rpc->cr_opc) == CONT_OPEN)) {
		struct cont_open_v7_out *out_v7 = crt_reply_get(rpc);

		out_v7->coo_md_otime = mdtimes.otime;
		out_v7->coo_md_mtime = mdtimes.mtime;
	}

	if (mdtimes_in_reply && (opc_get(rpc->cr_opc) == CONT_OPEN_BYLABEL)) {
		struct cont_open_bylabel_v7_out *out_v7 = crt_reply_get(rpc);

		out_v7->coo_md_otime = mdtimes.otime;
		out_v7->coo_md_mtime = mdtimes.mtime;
	}

	/* query the container properties from RDB and update to IV */
	rc = cont_iv_prop_update(pool_hdl->sph_pool->sp_iv_ns,
				 cont->c_uuid, prop, true);
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

	d_iov_set(&value, &chdl, sizeof(chdl));
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
	D_DEBUG(DB_MD, DF_CONT": got nsnapshots=%u\n",
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
		D_DEBUG(DB_MD, DF_CONT": got lsnapshot="DF_X64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), out->coo_lsnapshot);
	}

	/* Get number of open handles (pre-incremented to reflect the effects of this open) */
	if (cont_proto_ver >= CONT_PROTO_VER_WITH_NHANDLES) {
		rc = get_nhandles(tx, NULL /* nhc */, cont, NHANDLES_PRE_INCREMENT, &nhandles);
		if (rc != 0) {
			D_ERROR(DF_CONT": get_nhandles() failed: "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}
		out->coo_nhandles = nhandles;
	}

out:
	if (rc == 0) {
		/**
		 * Put requested properties in output.
		 * the allocated prop will be freed after rpc replied in
		 * ds_cont_op_handler.
		 */
		rc = cont_prop_read(tx, cont, in->coi_prop_bits, &prop, true);
		if (rc == -DER_SUCCESS)
			out->coo_prop = prop;
	}
	if (rc != 0 && cont_hdl_opened)
		cont_iv_capability_invalidate(pool_hdl->sph_pool->sp_iv_ns,
					      in->coi_op.ci_hdl,
					      CRT_IV_SYNC_EAGER);
	D_DEBUG(DB_MD, DF_CONT ": replying rpc: %p " DF_RC "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, cont->c_uuid), rpc, DP_RC(rc));
	return rc;
}

static int
cont_close_recs(crt_context_t ctx, struct cont_svc *svc,
		struct cont_tgt_close_rec recs[], int nrecs)
{
	int	i;
	int	rc = 0;

	D_DEBUG(DB_MD, DF_CONT": closing: recs[0].hdl="DF_UUID
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
			D_DEBUG(DB_MD, DF_CONT"/"DF_UUID" fail %d",
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
	D_DEBUG(DB_MD, DF_CONT": bcasted: hdls[0]="DF_UUID" nhdls=%d: %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), DP_UUID(recs[0].tcr_hdl),
		nrecs, rc);
	return rc;
}

static int
cont_close_one_hdl(struct rdb_tx *tx, struct d_hash_table *nhc, struct cont_svc *svc,
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

	/* Get, decrement/update number of open handles. Use nhandles cache if provided. */
	rc = get_nhandles(tx, nhc, cont, NHANDLES_PRE_DECREMENT, NULL /* nhandles */);
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
	struct rdb_tx		tx;
	struct d_hash_table	txs_nhc;	/* TX per-container number of handles cache (HT). */
	int			i;
	int			num_tx = 0;
	int			rc;
	int			rc1;

	D_ASSERTF(nrecs > 0, "%d\n", nrecs);
	D_DEBUG(DB_MD, DF_CONT": closing %d recs: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64"\n", DP_CONT(svc->cs_pool_uuid, NULL),
		nrecs, DP_UUID(recs[0].tcr_hdl), recs[0].tcr_hce);

	rc = cont_close_recs(ctx, svc, recs, nrecs);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to close %d recs: "DF_RC"\n",
			DP_CONT(svc->cs_pool_uuid, NULL), nrecs, DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		goto out;
	num_tx++;

	/* TX nhandles cache (for multiple close, tx cannot read back its uncommitted updates. */
	rc = nhandles_ht_create(&txs_nhc);
	if (rc) {
		D_ERROR("failed to create HT txs_nhc, tx %d"DF_RC"\n", num_tx, DP_RC(rc));
		goto out_tx;
	}
	D_DEBUG(DB_TRACE, DF_CONT": created txs_nhc HT, tx batch %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), num_tx);

	for (i = 0; i < nrecs; i++) {
		rc = cont_close_one_hdl(&tx, &txs_nhc, svc, ctx, recs[i].tcr_hdl);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to close handle: "DF_UUID", "DF_RC"\n",
				DP_CONT(svc->cs_pool_uuid, NULL), DP_UUID(recs[i].tcr_hdl),
				DP_RC(rc));
			goto out_ht;
		}

		/*
		 * Yield frequently, in order to cope with the slow
		 * vos_obj_punch operations invoked by rdb_tx_commit for
		 * deleting the handles. (If there is no other RDB replica, the
		 * TX operations will not yield, and this loop would occupy the
		 * xstream for too long.). Also, destroy/re-create TX-scoped nhandles HT.
		 */
		if ((i + 1) % 32 == 0) {
			rc = rdb_tx_commit(&tx);
			if (rc != 0)
				goto out_ht;
			rdb_tx_end(&tx);
			rc = nhandles_ht_destroy(&txs_nhc);
			if (rc) {
				D_ERROR(DF_CONT": failed to destroy HT txs_nhc, tx  %d, "DF_RC"\n",
					DP_CONT(svc->cs_pool_uuid, NULL), num_tx, DP_RC(rc));
				goto out;
			}
			D_DEBUG(DB_TRACE, DF_CONT": destroyed txs_nhc HT, tx %d\n",
				DP_CONT(svc->cs_pool_uuid, NULL), num_tx);
			ABT_thread_yield();
			rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
			if (rc != 0)
				goto out;
			num_tx++;
			rc = nhandles_ht_create(&txs_nhc);
			if (rc) {
				D_ERROR(DF_CONT": failed to create HT txs_nhc, tx %d, "DF_RC"\n",
					DP_CONT(svc->cs_pool_uuid, NULL), num_tx, DP_RC(rc));
				goto out_tx;
			}
			D_DEBUG(DB_TRACE, DF_CONT": created txs_nhc HT, tx %d\n",
				DP_CONT(svc->cs_pool_uuid, NULL), num_tx);
		}
	}

	rc = rdb_tx_commit(&tx);

out_ht:
	rc1 = nhandles_ht_destroy(&txs_nhc);
	if (rc1) {
		D_ERROR("failed to destroy HT: txs_nhc: "DF_RC"\n", DP_RC(rc1));
		if (rc == 0)
			rc = rc1;
	} else {
		D_DEBUG(DB_TRACE, DF_CONT": destroyed txs_nhc HT after tx %d and nrecs %d\n",
			DP_CONT(svc->cs_pool_uuid, NULL), num_tx, nrecs);
	}

out_tx:
	rdb_tx_end(&tx);
out:
	if (rc == 0)
		D_INFO(DF_CONT": closed %d recs\n", DP_CONT(svc->cs_pool_uuid, NULL), nrecs);
	return rc;
}

static int
cont_close(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   crt_rpc_t *rpc, bool *update_mtime)
{
	struct cont_close_in	       *in = crt_req_get(rpc);
	d_iov_t				key;
	d_iov_t				value;
	struct container_hdl		chdl;
	struct cont_tgt_close_rec	rec;
	bool				update_mtime_needed = false;
	int				rc;

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		DP_UUID(in->cci_op.ci_hdl));

	/* See if this container handle is already closed. */
	d_iov_set(&key, in->cci_op.ci_hdl, sizeof(uuid_t));
	d_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_MD, DF_CONT": already closed: "DF_UUID"\n",
				DP_CONT(cont->c_svc->cs_pool->sp_uuid,
					cont->c_uuid),
				DP_UUID(in->cci_op.ci_hdl));
			rc = 0;
		}
		D_GOTO(out, rc);
	}

	uuid_copy(rec.tcr_hdl, in->cci_op.ci_hdl);
	rec.tcr_hce = chdl.ch_hce;

	D_DEBUG(DB_MD, DF_CONT": closing: hdl="DF_UUID" hce="DF_U64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, in->cci_op.ci_uuid),
		DP_UUID(rec.tcr_hdl), rec.tcr_hce);

	rc = cont_close_recs(rpc->cr_ctx, cont->c_svc, &rec, 1 /* nrecs */);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_close_one_hdl(tx, NULL /* nhc */, cont->c_svc, rpc->cr_ctx, rec.tcr_hdl);

	/* On success update modify time (except if open specified read-only metadata stats) */
	if (rc == 0 && !(chdl.ch_flags & DAOS_COO_RO_MDSTATS))
		update_mtime_needed = true;

out:
	*update_mtime = update_mtime_needed;
	D_DEBUG(DB_MD, DF_CONT ": replying rpc: %p " DF_RC "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc, DP_RC(rc));
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

	D_DEBUG(DB_MD,
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
		D_DEBUG(DB_MD, DF_CONT": failed to query %d targets\n",
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
	       daos_prop_t **prop_out, bool ignore_not_set)
{
	daos_prop_t	*prop = NULL;
	d_iov_t		 value;
	uint64_t	 val, bitmap;
	uint32_t	 idx = 0, nr = 0;
	int		 rc = 0;
	int		 negative_nr = 0;

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
		/* sizeof(DAOS_PROP_CO_LABEL_DEFAULT) includes \0 at the end */
		if (value.iov_len == (sizeof(DAOS_PROP_CO_LABEL_DEFAULT) - 1) &&
		    strncmp(value.iov_buf, DAOS_PROP_CO_LABEL_DEFAULT,
			    sizeof(DAOS_PROP_CO_LABEL_DEFAULT) - 1) == 0 ) {
			prop->dpp_nr--;
		} else {
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
	if (bits & DAOS_CO_QUERY_PROP_SCRUB_DIS) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop,
				   &ds_cont_prop_scrubber_disabled, &value);
		if (rc == -DER_NONEXIST)
			val = 0;
		else if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_SCRUBBER_DISABLED;
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
	if (bits & DAOS_CO_QUERY_PROP_EC_PDA) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_ec_pda,
				   &value);
		if (rc == -DER_NONEXIST)
			val = DAOS_PROP_PO_EC_PDA_DEFAULT;
		else if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_EC_PDA;
		prop->dpp_entries[idx].dpe_val = val;
		if (rc == -DER_NONEXIST) {
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
			negative_nr++;
			rc = 0;
		}
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_PERF_DOMAIN) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_perf_domain, &value);
		if (rc == -DER_NONEXIST)
			val = DAOS_PROP_CO_PERF_DOMAIN_DEFAULT;
		else if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_PERF_DOMAIN;
		prop->dpp_entries[idx].dpe_val = val;
		if (rc == -DER_NONEXIST) {
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
			negative_nr++;
			rc = 0;
		}
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_RP_PDA) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_rp_pda,
				   &value);
		if (rc == -DER_NONEXIST)
			val = DAOS_PROP_PO_RP_PDA_DEFAULT;
		else if (rc != 0)
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_RP_PDA;
		prop->dpp_entries[idx].dpe_val = val;
		if (rc == -DER_NONEXIST) {
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
			negative_nr++;
			rc = 0;
		}
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_GLOBAL_VERSION) {
		/* type of the property in rdb is uint32_t */
		uint32_t cont_ver;

		d_iov_set(&value, &cont_ver, sizeof(cont_ver));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_cont_global_version, &value);
		if (rc == 0)
			val = cont_ver;
		else if (rc == -DER_NONEXIST)
			val = 0;
		else
			D_GOTO(out, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_GLOBAL_VERSION;
		prop->dpp_entries[idx].dpe_val = val;
		if (rc == -DER_NONEXIST) {
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
			negative_nr++;
			rc = 0;
		}
		idx++;
	}
	if (bits & DAOS_CO_QUERY_PROP_OBJ_VERSION) {
		uint32_t obj_ver;

		d_iov_set(&value, &obj_ver, sizeof(obj_ver));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_cont_obj_version,
				   &value);
		if (rc == -DER_NONEXIST)
			obj_ver = 0;
		else  if (rc != 0)
			D_GOTO(out, rc);

		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_CO_OBJ_VERSION;
		prop->dpp_entries[idx].dpe_val = obj_ver;
		if (rc == -DER_NONEXIST) {
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
			negative_nr++;
			rc = 0;
		}
		idx++;
	}

out:
	if (rc == 0) {
		if (negative_nr == nr) {
			daos_prop_free(prop);
			return 0;
		} else if (negative_nr > 0 && ignore_not_set) {
			*prop_out = daos_prop_dup(prop, false, false);
			daos_prop_free(prop);
			if (*prop_out == NULL)
				rc = -DER_NOMEM;
		} else {
			*prop_out = prop;
		}
	} else {
		daos_prop_free(prop);
	}
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
	int			 rf, rlvl;
	int			 rc;

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_LVL);
	D_ASSERT(entry != NULL);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
	D_ASSERT(entry != NULL);
	rlvl = daos_cont_prop2redunlvl(prop);
	rf = daos_cont_prop2redunfac(prop);
	rc = ds_pool_rf_verify(pool, last_ver, rlvl, rf);
	if (rc == -DER_RF) {
		D_ERROR(DF_CONT": RF broken, last_ver %d, rlvl %d, rf %d, "DF_RC"\n",
			DP_CONT(pool->sp_uuid, cont->c_uuid), last_ver, rlvl, rf, DP_RC(rc));
		rc = 0;
		cont_status_set_unclean(prop);
	}

	return rc;
}

static int
cont_info_read(struct rdb_tx *tx, struct cont *cont, int cont_proto_ver, daos_cont_info_t *cinfo) {
	d_iov_t			value;
	int			snap_count;
	daos_cont_info_t	out = {0};
	int			rc;

	uuid_copy(out.ci_uuid, cont->c_uuid);

	/* Get nsnapshots */
	d_iov_set(&value, &snap_count, sizeof(snap_count));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup nsnapshots, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		return rc;
	}
	out.ci_nsnapshots = snap_count;

	/* Get latest snapshot */
	if (snap_count > 0) {
		d_iov_t		key_out;

		rc = rdb_tx_query_key_max(tx, &cont->c_snaps, &key_out);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to query lsnapshot, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}
		out.ci_lsnapshot = *(uint64_t *)key_out.iov_buf;
		D_DEBUG(DB_MD, DF_CONT": got lsnapshot="DF_X64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), out.ci_lsnapshot);
	}

	/* lookup metadata times */
	D_DEBUG(DB_MD, DF_CONT": cont_proto_ver=%d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), cont_proto_ver);
	if (cont_proto_ver >= CONT_PROTO_VER_WITH_MDTIMES) {
		struct co_md_times		mdtimes;

		/* NB client/engine may have recent (protocol) versions, however the pool may not
		 * have been upgraded to new layout yet. mdtimes will be zeros in that case.
		 */
		rc = get_metadata_times(tx, cont, false /* update_otime */, false /* mtime */,
					&mdtimes);
		if (rc != 0)
			goto out;
		out.ci_md_otime = mdtimes.otime;
		out.ci_md_mtime = mdtimes.mtime;
	}

	/* Get number of open handles (without updating it) */
	if (cont_proto_ver >= CONT_PROTO_VER_WITH_NHANDLES) {
		rc = get_nhandles(tx, NULL /* nhc */, cont, NHANDLES_GET, &out.ci_nhandles);
		if (rc != 0) {
			D_ERROR(DF_CONT": get_nhandles failed, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		}
	}

out:
	if (rc == 0)
		*cinfo = out;
	return rc;

}

static int
cont_query(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   struct container_hdl *hdl, crt_rpc_t *rpc, int cont_proto_ver)
{
	struct cont_query_in   *in  = crt_req_get(rpc);
	struct cont_query_out  *out = crt_reply_get(rpc);
	daos_cont_info_t	cinfo;
	daos_prop_t	       *prop = NULL;
	uint32_t		last_ver = 0;
	int			rc = 0;

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cqi_op.ci_uuid), rpc,
		DP_UUID(in->cqi_op.ci_hdl));

	if (!hdl_has_query_access(hdl, cont, in->cqi_bits))
		return -DER_NO_PERM;

	/* Read container info */
	rc = cont_info_read(tx, cont, cont_proto_ver, &cinfo);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to read container info, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
		return rc;
	}
	out->cqo_snap_count = cinfo.ci_nsnapshots;
	if (cinfo.ci_nsnapshots > 0)
		out->cqo_lsnapshot = cinfo.ci_lsnapshot;
	if (cont_proto_ver >= CONT_PROTO_VER_WITH_MDTIMES) {
		struct cont_query_v7_out       *out_v7 = crt_reply_get(rpc);

		out_v7->cqo_md_otime = cinfo.ci_md_otime;
		out_v7->cqo_md_mtime = cinfo.ci_md_mtime;
	}

	if (cont_proto_ver >= CONT_PROTO_VER_WITH_NHANDLES)
		out->cqo_nhandles = cinfo.ci_nhandles;

	/* need RF to process co_status */
	if (in->cqi_bits & DAOS_CO_QUERY_PROP_CO_STATUS)
		in->cqi_bits |= (DAOS_CO_QUERY_PROP_REDUN_FAC | DAOS_CO_QUERY_PROP_REDUN_LVL);

	/* Currently DAOS_CO_QUERY_TGT not used; code kept for future expansion. */
	if (in->cqi_bits & DAOS_CO_QUERY_TGT) {
		/* need RF if user query cont_info */
		in->cqi_bits |= (DAOS_CO_QUERY_PROP_REDUN_FAC | DAOS_CO_QUERY_PROP_REDUN_LVL);
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
	rc = cont_prop_read(tx, cont, in->cqi_bits, &prop, true);
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
			case DAOS_PROP_CO_EC_PDA:
			case DAOS_PROP_CO_RP_PDA:
			case DAOS_PROP_CO_PERF_DOMAIN:
			case DAOS_PROP_CO_GLOBAL_VERSION:
			case DAOS_PROP_CO_SCRUBBER_DISABLED:
			case DAOS_PROP_CO_OBJ_VERSION:
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
	char			*old_lbl = NULL;
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
	if (old_ent) {
		old_lbl = old_ent->dpe_str;
		if (strncmp(old_lbl, in_lbl, DAOS_PROP_LABEL_MAX_LEN) == 0)
			return 0;
	}

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
	if (old_lbl == NULL)
		return 0;

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
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(prop_in, DAOS_PROP_CO_GLOBAL_VERSION);
	if (entry) {
		D_ERROR("container global version could be not set\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	entry = daos_prop_entry_get(prop_in, DAOS_PROP_CO_OBJ_VERSION);
	if (entry) {
		D_ERROR("container object version could be not set\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!daos_prop_valid(prop_in, false, true))
		D_GOTO(out, rc = -DER_INVAL);

	if (!capas_can_set_prop(cont, sec_capas, prop_in))
		D_GOTO(out, rc = -DER_NO_PERM);

	/* Read all props for prop IV update */
	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop_old, false);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read prop for cont, rc=%d\n",
			DP_UUID(cont->c_uuid), rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(prop_old != NULL);
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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
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

	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ACL, &acl_prop, true);
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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->caui_op.ci_uuid), rpc,
		DP_UUID(in->caui_op.ci_hdl));

	acl_in = in->caui_acl;
	if (daos_acl_validate(acl_in) != 0)
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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->casi_op.ci_uuid), rpc,
		DP_UUID(in->casi_op.ci_hdl));

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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cadi_op.ci_uuid), rpc,
		DP_UUID(in->cadi_op.ci_hdl));

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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cagi_op.ci_uuid), rpc,
		DP_UUID(in->cagi_op.ci_hdl));

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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cali_op.ci_uuid), rpc,
		DP_UUID(in->cali_op.ci_hdl));

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

	if (!shall_close(hdl->ch_pool_hdl, arg->cia_pool_hdls, arg->cia_n_pool_hdls))
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

	D_DEBUG(DB_MD, DF_CONT": closing by %d pool hdls: pool_hdls[0]="
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

	D_DEBUG(DB_MD, "pool/cont: "DF_CONTF"\n",
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
	rc = cont_prop_read(ap->tx, cont, DAOS_CO_QUERY_PROP_LABEL, &prop, true);
	cont_put(cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_prop_read() failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		return rc;
	}
	if (prop->dpp_entries[0].dpe_str) {
		strncpy(cinfo->pci_label, prop->dpp_entries[0].dpe_str,
			DAOS_PROP_LABEL_MAX_LEN);
		cinfo->pci_label[DAOS_PROP_LABEL_MAX_LEN] = '\0';
	} else {
		cinfo->pci_label[0] = '\0';
	}

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
	D_DEBUG(DB_MD, "iterate rc=%d, args.conts=%p, args.ncont="DF_U64"\n",
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
cont_filter_part_match(struct rdb_tx *tx, struct cont *cont, daos_pool_cont_filter_part_t *part,
		       bool *match)
{
	d_iov_t			value;
	uint64_t		val64 = 0;
	uint32_t		val32;
	bool			result = false;
	struct co_md_times	mdtimes;
	int			rc;

	/* Fetch the key's value from rdb */
	switch(part->pcfp_key) {
	case PCF_KEY_MD_OTIME:
	case PCF_KEY_MD_MTIME:
		rc = get_metadata_times(tx, cont, false /* update_otime */, false /* mtime */,
					&mdtimes);
		val64 = (part->pcfp_key == PCF_KEY_MD_OTIME) ? mdtimes.otime : mdtimes.mtime;
		break;
	case PCF_KEY_NUM_SNAPSHOTS:
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nsnapshots, &value);
		val64 = (uint64_t)val32;
		break;
	case PCF_KEY_NUM_HANDLES:
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_nhandles, &value);
		val64 = (uint64_t)val32;
		break;
	default:
		/* Should not be here so long as caller verifies. See pool_cont_filter_is_valid() */
		rc = -DER_INVAL;
		break;
	}

	if (rc != 0) {
		D_ERROR(DF_CONT": metadata lookup of %s failed, "DF_RC"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			daos_pool_cont_filter_key_str(part->pcfp_key), DP_RC(rc));
		goto out;
	}

	/* Apply the comparison function to the key */
	switch(part->pcfp_func) {
	case PCF_FUNC_EQ:
		result = (val64 == part->pcfp_val64);
		break;
	case PCF_FUNC_NE:
		result = (val64 != part->pcfp_val64);
		break;
	case PCF_FUNC_LT:
		result = (val64 < part->pcfp_val64);
		break;
	case PCF_FUNC_LE:
		result = (val64 <= part->pcfp_val64);
		break;
	case PCF_FUNC_GT:
		result = (val64 > part->pcfp_val64);
		break;
	case PCF_FUNC_GE:
		result = (val64 >= part->pcfp_val64);
		break;
	default:
		/* Should not be here so long as caller verifies. See pool_cont_filter_is_valid() */
		D_ERROR(DF_CONT": invalid comparison function (%u)\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), part->pcfp_func);
		rc = -DER_INVAL;
		break;	/* goto out */
	}

out:
	if (rc == 0) {
		D_DEBUG(DB_MD, DF_CONT": %s filter part: %s(value "DF_U64") %s "DF_U64")\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			result ? "matched" : "did not match",
			daos_pool_cont_filter_key_str(part->pcfp_key), val64,
			daos_pool_cont_filter_func_str(part->pcfp_func), part->pcfp_val64);
		*match = result;
	}
	return rc;
}

static int
cont_filter_match(struct rdb_tx *tx, struct cont *cont, daos_pool_cont_filter_t *filt, bool *match)
{
	int		i;
	bool		whole_match = true;
	uint32_t	combine_op = filt->pcf_combine_func;
	int		rc = 0;

	/* defensive, partially redundant with pool_cont_filter_is_valid() from top-level handler */
	if ((filt->pcf_nparts > 0) && (filt->pcf_parts == NULL)) {
		D_ERROR(DF_CONT": filter has %u parts but pcf_parts is NULL\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), filt->pcf_nparts);
		return -DER_INVAL;
	}

	/* logical OR combining: start with false result, transition to true on first match */
	if ((filt->pcf_parts != NULL) && (combine_op == PCF_COMBINE_LOGICAL_OR))
		whole_match = false;

	for (i = 0; i < filt->pcf_nparts; i++) {
		bool	part_match;

		rc = cont_filter_part_match(tx, cont, filt->pcf_parts[i], &part_match);
		if (rc != 0) {
			D_ERROR(DF_CONT": cont_filter_part_match() failed, "DF_RC"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), DP_RC(rc));
			goto out;
		} else {
			if (!part_match && (combine_op == PCF_COMBINE_LOGICAL_AND)) {
				D_DEBUG(DB_MD, DF_CONT": logical AND, done due to false compare\n",
					DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
				whole_match = false;
				break;
			} else if (part_match && (combine_op == PCF_COMBINE_LOGICAL_OR)) {
				D_DEBUG(DB_MD, DF_CONT": logical OR, done due to true compare\n",
					DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));
				whole_match = true;
				break;
			}
			/* otherwise, keep testing remaining filter parts */
		}
	}

out:
	if (rc == 0) {
		D_DEBUG(DB_MD, DF_CONT": whole filter %s\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			whole_match ? "matched" : "did not match");
		*match = whole_match;
	}

	return rc;
}

/* argument type for callback function to filter containers */
struct filter_cont_iter_args {
	uuid_t				 pool_uuid;

	/* Filter criteria specification */
	daos_pool_cont_filter_t		*filt;

	/* Number of containers in pool and conts[] index while counting */
	uint64_t			 ncont;

	/* conts[]: capacity*/
	uint64_t			 conts_len;
	struct daos_pool_cont_info2	*conts;
	struct cont_svc			*svc;
	struct rdb_tx			*tx;
};

/* callback function for filter containers iteration. */
static int
filter_cont_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct filter_cont_iter_args	*ap = varg;
	struct daos_pool_cont_info2	*pcinfo;
	uuid_t				 cont_uuid;
	struct cont			*cont;
	daos_prop_t			*prop = NULL;
	bool				 filt_match = false;
	int				 rc;
	(void)val;

	if (key->iov_len != sizeof(uuid_t)) {
		D_ERROR("invalid key size: key="DF_U64"\n", key->iov_len);
		return -DER_IO;
	}
	uuid_copy(cont_uuid, key->iov_buf);

	D_DEBUG(DB_MD, "pool/cont: "DF_CONTF": ncont=%zu, conts_len=%zu\n",
		DP_CONT(ap->pool_uuid, cont_uuid), ap->ncont, ap->conts_len);

	/* Lookup container, see if it matches filter specification before adding to ap->conts[] */
	rc = cont_lookup(ap->tx, ap->svc, cont_uuid, &cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": lookup cont failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		return rc;
	}

	rc = cont_filter_match(ap->tx, cont, ap->filt, &filt_match);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_match() failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		goto out_cont;
	} else if (filt_match == false) {
		goto out_cont;
	}

	/* The container matches the filter specification: save information */

	/* Realloc conts[] if needed (double each time starting with 1) */
	if (ap->ncont == ap->conts_len) {
		void	*ptr;
		size_t	realloc_elems = (ap->conts_len == 0) ? 1 : ap->conts_len * 2;

		D_REALLOC_ARRAY(ptr, ap->conts, ap->conts_len, realloc_elems);
		if (ptr == NULL)
			D_GOTO(out_cont, rc = -DER_NOMEM);

		ap->conts = ptr;
		ap->conts_len = realloc_elems;
	}

	pcinfo = &ap->conts[ap->ncont];
	ap->ncont++;
	uuid_copy(pcinfo->pci_id.pci_uuid, cont_uuid);

	/* TODO: Specify client cont_proto_version. This is invoked from a pool client RPC */
	rc = cont_info_read(ap->tx, cont, DAOS_CONT_VERSION /* engine protocol version */,
			    &pcinfo->pci_cinfo);
	if (rc != 0) {
		D_ERROR(DF_CONT": read container info failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		goto out_cont;
	}

	rc = cont_prop_read(ap->tx, cont, DAOS_CO_QUERY_PROP_LABEL, &prop, true);
	if (rc != 0) {
		D_ERROR(DF_CONT": cont_prop_read() failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		goto out_cont;
	}

	memset(pcinfo->pci_id.pci_label, 0, sizeof(pcinfo->pci_id.pci_label));
	if (prop->dpp_entries[0].dpe_str) {
		strncpy(pcinfo->pci_id.pci_label, prop->dpp_entries[0].dpe_str,
			DAOS_PROP_LABEL_MAX_LEN);
		pcinfo->pci_id.pci_label[DAOS_PROP_LABEL_MAX_LEN] = '\0';
	}

	daos_prop_free(prop);

out_cont:
	cont_put(cont);

	return rc;
}

/**
 * Select/filter from the containers in a pool.
 *
 * \param[in]	pool_uuid	Pool UUID.
 * \param[in]	filt		Filtering criteria specification.
 * \param[out]	conts		Array of container extended info structures
 *				to be allocated. Caller must free.
 * \param[out]	ncont		Number of containers in the pool
 *				(number of items populated in conts[]).
 */
int
ds_cont_filter(uuid_t pool_uuid, daos_pool_cont_filter_t *filt,
	       struct daos_pool_cont_info2 **conts, uint64_t *ncont)
{
	int				 rc;
	struct cont_svc			*svc;
	struct rdb_tx			 tx;
	struct filter_cont_iter_args	 args;

	*conts = NULL;
	*ncont = 0;

	uuid_copy(args.pool_uuid, pool_uuid);
	args.filt = filt;
	args.ncont = 0;			/* number of containers in the pool */
	args.conts_len = 0;		/* allocated length of conts[] */
	args.conts = NULL;

	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc, NULL /* hint **/);
	if (rc != 0)
		D_GOTO(out, rc);
	args.svc = svc;

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	args.tx = &tx;
	ABT_rwlock_rdlock(svc->cs_lock);

	rc = rdb_tx_iterate(&tx, &svc->cs_conts, false /* !backward */, filter_cont_cb, &args);

	/* read-only, so no rdb_tx_commit */
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);

out_svc:
	cont_svc_put_leader(svc);

out:
	D_DEBUG(DB_MD, "filter rc=%d, args.conts=%p, args.ncont="DF_U64"\n",
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

/* argument type for callback function to upgrade containers */
struct upgrade_cont_iter_args {
	uuid_t				 pool_uuid;

	struct cont_svc			*svc;
	struct rdb_tx			*tx;
	/* total number of containers */
	uint32_t			cont_nrs;
	/* number of upgraded containers */
	uint32_t			cont_upgraded_nrs;
	/* if tx commit is needed after iteration */
	bool				need_commit;
};

/* callback function for upgrading containers iteration. */
static int
upgrade_cont_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct upgrade_cont_iter_args	*ap = varg;
	uuid_t				 cont_uuid;
	struct cont			*cont;
	d_iov_t				 value;
	uint64_t			 pda;
	uint64_t			 perf_domain;
	int				 rc;
	bool				 upgraded = false;
	uint32_t			 global_ver = 0;
	uint32_t			 from_global_ver;
	uint32_t			 obj_ver = 0;
	uint32_t			 nhandles = 0;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry;
	struct co_md_times		 mdtimes;
	(void)val;

	if (key->iov_len != sizeof(uuid_t)) {
		D_ERROR("invalid key size: key="DF_U64"\n", key->iov_len);
		return -DER_IO;
	}

	uuid_copy(cont_uuid, key->iov_buf);
	D_DEBUG(DB_MD, "pool/cont: "DF_CONTF"\n",
		DP_CONT(ap->pool_uuid, cont_uuid));

	rc = cont_lookup(ap->tx, ap->svc, cont_uuid, &cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": lookup cont failed, "DF_RC"\n",
			DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
		return rc;
	}

	if (DAOS_FAIL_CHECK(DAOS_FORCE_OBJ_UPGRADE)) {
		obj_ver = DS_POOL_OBJ_VERSION;
		d_iov_set(&value, &obj_ver, sizeof(obj_ver));
		rc = rdb_tx_update(ap->tx, &cont->c_prop,
				   &ds_cont_prop_cont_obj_version, &value);
		if (rc) {
			D_ERROR("failed to upgrade container obj version pool/cont: "DF_CONTF"\n",
				DP_CONT(ap->pool_uuid, cont_uuid));
			goto out;
		}
		upgraded = true;
		/* Read all props for prop IV update */
		rc = cont_prop_read(ap->tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, false);
		if (rc) {
			D_ERROR(DF_CONTF" property fetch: %d\n", DP_CONT(ap->pool_uuid, cont_uuid),
				rc);
			goto out;
		}

		goto out;
	}

	d_iov_set(&value, &global_ver, sizeof(global_ver));
	rc = rdb_tx_lookup(ap->tx, &cont->c_prop,
			   &ds_cont_prop_cont_global_version, &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	/* latest container, nothing to update */
	if (rc == 0 && global_ver == DAOS_POOL_GLOBAL_VERSION) {
		D_DEBUG(DB_MD, DF_CONTF" ver %u do not need upgrade.\n",
			DP_CONT(ap->pool_uuid, cont_uuid), global_ver);
		goto out;
	}

	if (global_ver > DAOS_POOL_GLOBAL_VERSION) {
		D_ERROR("Downgrading pool/cont: "DF_CONTF" not supported\n",
			DP_CONT(ap->pool_uuid, cont_uuid));
		rc = -DER_NOTSUPPORTED;
		goto out;
	}

	/* TODO? make sure global_ver == (DAOS_POOL_GLOBAL_VERSION - 1)? */

	/* Read all props for prop IV update */
	rc = cont_prop_read(ap->tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, false);
	if (rc)
		goto out;

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_GLOBAL_VERSION);
	D_ASSERT(entry != NULL);
	if (global_ver == 0) {
		D_ASSERT(daos_prop_is_set(entry) == false);
		entry->dpe_flags &= ~DAOS_PROP_ENTRY_NOT_SET;
	}
	entry->dpe_val = DAOS_POOL_GLOBAL_VERSION;
	D_DEBUG(DB_MD, "pool/cont: "DF_CONTF" upgrading layout %d->%d\n",
		DP_CONT(ap->pool_uuid, cont_uuid), global_ver, DAOS_POOL_GLOBAL_VERSION);

	from_global_ver = global_ver;
	global_ver = DAOS_POOL_GLOBAL_VERSION;
	rc = rdb_tx_update(ap->tx, &cont->c_prop,
			   &ds_cont_prop_cont_global_version, &value);
	if (rc) {
		D_ERROR("failed to upgrade container global version pool/cont: "DF_CONTF"\n",
			DP_CONT(ap->pool_uuid, cont_uuid));
		goto out;
	}

	if (from_global_ver < 2) {
		struct rdb_kvs_attr	attr;

		/* Create the oit oids index KVS. */
		attr.dsa_class = RDB_KVS_GENERIC;
		attr.dsa_order = 16;
		rc = rdb_tx_create_kvs(ap->tx, &cont->c_prop, &ds_cont_prop_oit_oids, &attr);
		if (rc != 0) {
			D_ERROR(DF_CONT" failed to create container oit oids KVS: "
				""DF_RC"\n",
				DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
			goto out;
		}
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OBJ_VERSION);
	D_ASSERT(entry != NULL);
	entry->dpe_val = DS_POOL_OBJ_VERSION;
	entry->dpe_flags &= ~DAOS_PROP_ENTRY_NOT_SET;
	obj_ver = DS_POOL_OBJ_VERSION;
	d_iov_set(&value, &obj_ver, sizeof(obj_ver));
	rc = rdb_tx_update(ap->tx, &cont->c_prop,
			   &ds_cont_prop_cont_obj_version, &value);
	if (rc) {
		D_ERROR("failed to upgrade container obj version pool/cont: "DF_CONTF"\n",
			DP_CONT(ap->pool_uuid, cont_uuid));
		goto out;
	}

	upgraded = true;

	d_iov_set(&value, &pda, sizeof(pda));
	rc = rdb_tx_lookup(ap->tx, &cont->c_prop,
			   &ds_cont_prop_ec_pda, &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	if (rc == -DER_NONEXIST) {
		pda = DAOS_PROP_PO_EC_PDA_DEFAULT;
		rc = rdb_tx_update(ap->tx, &cont->c_prop,
				   &ds_cont_prop_ec_pda, &value);
		if (rc) {
			D_ERROR("failed to upgrade container ec_pda pool/cont: "DF_CONTF"\n",
				DP_CONT(ap->pool_uuid, cont_uuid));
			goto out;
		}
		upgraded = true;
		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_EC_PDA);
		D_ASSERT(entry != NULL);
		D_ASSERT(daos_prop_is_set(entry) == false);
		entry->dpe_flags &= ~DAOS_PROP_ENTRY_NOT_SET;
		entry->dpe_val = pda;
	}

	rc = rdb_tx_lookup(ap->tx, &cont->c_prop, &ds_cont_prop_rp_pda,
			   &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	if (rc == -DER_NONEXIST) {
		pda = DAOS_PROP_PO_RP_PDA_DEFAULT;
		rc = rdb_tx_update(ap->tx, &cont->c_prop, &ds_cont_prop_rp_pda,
				   &value);
		if (rc) {
			D_ERROR("failed to upgrade container rp_pda pool/cont: "DF_CONTF"\n",
				DP_CONT(ap->pool_uuid, cont_uuid));
			goto out;
		}
		upgraded = true;
		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_RP_PDA);
		D_ASSERT(entry != NULL);
		D_ASSERT(daos_prop_is_set(entry) == false);
		entry->dpe_flags &= ~DAOS_PROP_ENTRY_NOT_SET;
		entry->dpe_val = pda;
	}

	d_iov_set(&value, &perf_domain, sizeof(perf_domain));
	rc = rdb_tx_lookup(ap->tx, &cont->c_prop, &ds_cont_prop_perf_domain, &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	if (rc == -DER_NONEXIST) {
		perf_domain = DAOS_PROP_CO_PERF_DOMAIN_DEFAULT;
		rc = rdb_tx_update(ap->tx, &cont->c_prop, &ds_cont_prop_perf_domain, &value);
		if (rc) {
			D_ERROR("failed to upgrade container perf_domain pool/cont: "DF_CONTF"\n",
				DP_CONT(ap->pool_uuid, cont_uuid));
			goto out;
		}
		upgraded = true;
		entry = daos_prop_entry_get(prop, DAOS_PROP_CO_PERF_DOMAIN);
		D_ASSERT(entry != NULL);
		D_ASSERT(daos_prop_is_set(entry) == false);
		entry->dpe_flags &= ~DAOS_PROP_ENTRY_NOT_SET;
		entry->dpe_val = perf_domain;
	}

	rc = rdb_tx_lookup(ap->tx, &cont->c_prop, &ds_cont_prop_scrubber_disabled, &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	if (rc == -DER_NONEXIST) {
		pda = 0;
		rc = rdb_tx_update(ap->tx, &cont->c_prop, &ds_cont_prop_scrubber_disabled, &value);
		if (rc) {
			D_ERROR("failed to upgrade container scrubbing disabled prop: "DF_CONTF"\n",
				DP_CONT(ap->pool_uuid, cont_uuid));
			goto out;
		}
		upgraded = true;
	}

	/* Initialize number of open handles to zero */
	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(ap->tx, &cont->c_prop, &ds_cont_prop_nhandles, &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	if (rc == -DER_NONEXIST) {
		rc = rdb_tx_update(ap->tx, &cont->c_prop, &ds_cont_prop_nhandles, &value);
		if (rc) {
			D_ERROR("failed to upgrade container nhandles pool/cont: "DF_CONTF
				", "DF_RC"\n", DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
			goto out;
		}
		upgraded = true;
	}

	/* Initialize or update container open / metadata modify times.
	 * Update modify time even when the container already has co_md_times key in properties KVS.
	 */
	d_iov_set(&value, &mdtimes, sizeof(mdtimes));
	rc = rdb_tx_lookup(ap->tx, &cont->c_prop,
			   &ds_cont_prop_co_md_times, &value);
	if (rc && rc != -DER_NONEXIST)
		goto out;
	else if (rc == -DER_NONEXIST) {
		if (from_global_ver >= DAOS_POOL_GLOBAL_VERSION_WITH_CONT_MDTIMES) {
			D_ERROR(DF_CONT": version %u container metadata is missing co_md_times!\n",
				DP_CONT(ap->pool_uuid, cont_uuid), from_global_ver);
			goto out;
		}

		mdtimes.otime = 0;
	}

	mdtimes.mtime = d_hlc_get();
	rc = rdb_tx_update(ap->tx, &cont->c_prop, &ds_cont_prop_co_md_times, &value);
	if (rc) {
		D_ERROR("failed to upgrade container co_md_times/cont: "DF_CONTF"\n",
			DP_CONT(ap->pool_uuid, cont_uuid));
		goto out;
	}
	upgraded = true;
	D_DEBUG(DB_MD, DF_CONT": set metadata times: open="DF_X64", modify="DF_X64"\n",
		DP_CONT(ap->pool_uuid, cont_uuid), mdtimes.otime, mdtimes.mtime);

out:
	if (rc == 0) {
		ap->cont_nrs++;
		if (upgraded) {
			/* Yield in case there are too many containers. */
			if ((ap->cont_upgraded_nrs + 1) % 32 == 0) {
				rc = rdb_tx_commit(ap->tx);
				ap->need_commit = false;
				if (rc)
					goto out_free_prop;
				rdb_tx_end(ap->tx);
				ABT_thread_yield();
				rc = rdb_tx_begin(ap->svc->cs_rsvc->s_db,
						  ap->svc->cs_rsvc->s_term, ap->tx);
				if (rc)
					goto out_free_prop;
			} else {
				ap->need_commit = true;
			}
			ap->cont_upgraded_nrs++;
			rc = cont_iv_prop_update(ap->svc->cs_pool->sp_iv_ns,
						 cont_uuid, prop, true);
			if (rc)
				D_ERROR(DF_UUID": failed to update prop IV for cont, "
					DF_RC"\n", DP_UUID(cont_uuid), DP_RC(rc));
		}
	}
out_free_prop:
	D_DEBUG(DB_MD, "pool/cont: "DF_CONTF" upgrade: "DF_RC"\n",
		DP_CONT(ap->pool_uuid, cont_uuid), DP_RC(rc));
	if (prop)
		daos_prop_free(prop);
	cont_put(cont);
	return rc;
}

/**
 * upgrade all containers in a pool.
 *
 * \param[in]	pool_uuid	Pool UUID.
 */
int
ds_cont_upgrade(uuid_t pool_uuid, struct cont_svc *svc)
{
	int				rc;
	struct rdb_tx			tx;
	struct upgrade_cont_iter_args	args = { 0 };
	bool				need_put_leader = false;

	uuid_copy(args.pool_uuid, pool_uuid);

	if (!svc) {
		rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc,
					    NULL /* hint **/);
		if (rc != 0)
			D_GOTO(out_svc, rc);
		need_put_leader = true;
	}

	args.svc = svc;
	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);
	args.tx = &tx;
	args.need_commit = false;
	ABT_rwlock_wrlock(svc->cs_lock);

	rc = rdb_tx_iterate(&tx, &svc->cs_conts, false /* !backward */,
			    upgrade_cont_cb, &args);
	if (rc == 0 && args.need_commit)
		rc = rdb_tx_commit(&tx);

	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);

out_svc:
	D_DEBUG(DB_MD, DF_UUID" upgrade all container: rc %d\n", DP_UUID(pool_uuid), rc);
	if (need_put_leader)
		cont_svc_put_leader(svc);

	return rc;
}

int
ds_cont_rf_check(uuid_t pool_uuid, uuid_t cont_uuid, struct rdb_tx *tx)
{
	struct cont_svc		*svc = NULL;
	struct cont		*cont = NULL;
	struct ds_pool		*pool;
	daos_prop_t		*prop = NULL;
	daos_prop_t		*stat_prop = NULL;
	struct daos_prop_entry	*entry;
	struct daos_co_status	stat = { 0 };
	int			rc;

	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc, NULL /* hint **/);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_lookup(tx, svc, cont_uuid, &cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": lookup cont failed, "DF_RC"\n",
			DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	pool = svc->cs_pool;
	rc = cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, false);
	if (rc != 0) {
		D_ERROR(DF_CONT ": failed to read prop for cont: " DF_RC "\n",
			DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	D_ASSERT(entry != NULL);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	if (stat.dcs_status == DAOS_PROP_CO_UNCLEAN) {
		D_DEBUG(DB_MD, DF_CONT" %u status %u is unhealthy.\n",
			DP_CONT(pool_uuid, cont_uuid), stat.dcs_pm_ver, stat.dcs_status);
		D_GOTO(out, rc = -DER_RF);
	}

	rc = ds_pool_rf_verify(pool, stat.dcs_pm_ver, daos_cont_prop2redunlvl(prop),
			       daos_cont_prop2redunfac(prop));
	if (rc != -DER_RF) {
		D_CDEBUG(rc == 0, DB_MD, DLOG_ERR, DF_CONT", verify" DF_RC"\n",
			 DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_CONT" ver %u set unhealthy.\n",
		DP_CONT(pool_uuid, cont_uuid), ds_pool_get_version(pool));
	stat_prop = daos_prop_alloc(1);
	if (stat_prop == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	stat.dcs_pm_ver = ds_pool_get_version(pool);
	stat.dcs_status = DAOS_PROP_CO_UNCLEAN;

	/* Update healthy status RDB property */
	stat_prop->dpp_entries[0].dpe_val = daos_prop_co_status_2_val(&stat);
	stat_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_STATUS;
	rc = cont_prop_write(tx, &cont->c_prop, stat_prop, false);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Update prop IV with merged prop */
	entry->dpe_val = daos_prop_co_status_2_val(&stat);
	rc = cont_iv_prop_update(pool->sp_iv_ns, cont_uuid, prop, false);
	if (rc) {
		D_ERROR(DF_UUID": failed to update prop IV for cont, "
			DF_RC"\n", DP_UUID(cont_uuid), DP_RC(rc));
		goto out;
	}
out:
	if (cont != NULL)
		cont_put(cont);
	if (prop != NULL)
		daos_prop_free(prop);
	if (stat_prop != NULL)
		daos_prop_free(stat_prop);
	if (svc != NULL)
		cont_svc_put_leader(svc);

	D_DEBUG(DB_MD, "check pool/cont: "DF_CONT": "DF_RC"\n",
		DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));

	return rc;
}

struct cont_rdb_iter_arg {
	struct cont_svc	*svc;
	struct rdb_tx	*tx;
	cont_rdb_iter_cb_t iter_cb;
	void		*cb_arg;
};

static int
cont_rdb_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct cont_rdb_iter_arg *args = varg;
	uuid_t			 cont_uuid;
	int			 rc;

	uuid_copy(cont_uuid, key->iov_buf);

	rc = args->iter_cb(args->svc->cs_pool->sp_uuid, cont_uuid, args->tx, args->cb_arg);

	return rc;
}

int
ds_cont_rdb_iterate(struct cont_svc *svc, cont_rdb_iter_cb_t iter_cb, void *cb_arg)
{
	struct cont_rdb_iter_arg	args = { 0 };
	struct rdb_tx			tx;
	int				rc;

	args.svc = svc;
	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	args.tx = &tx;
	args.cb_arg = cb_arg;
	args.iter_cb = iter_cb;
	ABT_rwlock_wrlock(svc->cs_lock);
	rc = rdb_tx_iterate(&tx, &svc->cs_conts, false /* !backward */, cont_rdb_iter_cb, &args);
	ABT_rwlock_unlock(svc->cs_lock);
	if (rc < 0) {
		D_ERROR(DF_UUID" iterate error: %d\n", DP_UUID(svc->cs_pool_uuid), rc);
		D_GOTO(tx_end, rc);
	}

	rc = rdb_tx_commit(&tx);
	if (rc)
		D_ERROR("rdb tx commit error: %d\n", rc);
tx_end:
	rdb_tx_end(&tx);

out_svc:
	D_DEBUG(DB_MD, DF_UUID" container iter: rc %d\n", DP_UUID(svc->cs_pool_uuid), rc);

	return rc;
}

static int
cont_op_with_hdl(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
		 struct container_hdl *hdl, crt_rpc_t *rpc, int cont_proto_ver, bool *update_mtime)
{
	struct cont_pool_metrics *metrics;
	int			  rc;

	*update_mtime = false;

	switch (opc_get(rpc->cr_opc)) {
	case CONT_QUERY:
		rc = cont_query(tx, pool_hdl, cont, hdl, rpc, cont_proto_ver);
		if (likely(rc == 0)) {
			metrics = pool_hdl->sph_pool->sp_metrics[DAOS_CONT_MODULE];
			d_tm_inc_counter(metrics->query_total, 1);
		}
		return rc;
	case CONT_ATTR_LIST:
		return cont_attr_list(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_GET:
		return cont_attr_get(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_SET:
		*update_mtime = true;
		return cont_attr_set(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ATTR_DEL:
		*update_mtime = true;
		return cont_attr_del(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_AGGREGATE:
		*update_mtime = true;
		return ds_cont_epoch_aggregate(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_LIST:
		return ds_cont_snap_list(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_CREATE:
		*update_mtime = true;
		return ds_cont_snap_create(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_DESTROY:
		*update_mtime = true;
		return ds_cont_snap_destroy(tx, pool_hdl, cont, hdl, rpc);
	case CONT_PROP_SET:
		*update_mtime = true;
		return ds_cont_prop_set(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ACL_UPDATE:
		*update_mtime = true;
		return ds_cont_acl_update(tx, pool_hdl, cont, hdl, rpc);
	case CONT_ACL_DELETE:
		*update_mtime = true;
		return ds_cont_acl_delete(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_OIT_OID_GET:
		return ds_cont_snap_oit_oid_get(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_OIT_CREATE:
		*update_mtime = true;
		return ds_cont_snap_oit_create(tx, pool_hdl, cont, hdl, rpc);
	case CONT_SNAP_OIT_DESTROY:
		*update_mtime = true;
		return ds_cont_snap_oit_destroy(tx, pool_hdl, cont, hdl, rpc);
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
		  struct cont *cont, crt_rpc_t *rpc, bool *update_mtime, int cont_proto_ver)
{
	struct cont_op_in		*in = crt_req_get(rpc);
	d_iov_t				 key;
	d_iov_t				 value;
	struct container_hdl		 hdl;
	struct cont_pool_metrics	*metrics;
	bool				 update_mtime_needed = false;
	int				 rc;

	metrics = pool_hdl->sph_pool->sp_metrics[DAOS_CONT_MODULE];

	switch (opc_get(rpc->cr_opc)) {
	case CONT_OPEN:
	case CONT_OPEN_BYLABEL:
		rc = cont_open(tx, pool_hdl, cont, rpc, cont_proto_ver);
		if (likely(rc == 0))
			d_tm_inc_counter(metrics->open_total, 1);
		break;
	case CONT_CLOSE:
		rc = cont_close(tx, pool_hdl, cont, rpc, &update_mtime_needed);
		if (likely(rc == 0))
			d_tm_inc_counter(metrics->close_total, 1);
		break;
	case CONT_DESTROY:
	case CONT_DESTROY_BYLABEL:
		rc = cont_destroy(tx, pool_hdl, cont, rpc);
		if (likely(rc == 0))
			d_tm_inc_counter(metrics->destroy_total, 1);
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
			goto out;
		}
		rc = cont_op_with_hdl(tx, pool_hdl, cont, &hdl, rpc, cont_proto_ver,
				      &update_mtime_needed);
		if (rc != 0)
			goto out;
	}
out:
	if (rc == 0)
		*update_mtime = update_mtime_needed;

	return rc;
}

void
ds_cont_prop_iv_update(struct cont_svc *svc, uuid_t cont_uuid)
{
	struct rdb_tx	tx;
	struct cont	*cont = NULL;
	daos_prop_t	*prop = NULL;
	int		rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0) {
		D_ERROR(DF_UUID": Failed to start rdb tx: %d\n",
			DP_UUID(svc->cs_pool_uuid), rc);
		return;
	}

	ABT_rwlock_rdlock(svc->cs_lock);
	rc = cont_lookup(&tx, svc, cont_uuid, &cont);
	if (rc != 0) {
		D_ERROR(DF_CONT": Failed to look container: %d\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
		D_GOTO(out_lock, rc);
	}

	rc = cont_prop_read(&tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, false);
	if (rc)
		D_ERROR(DF_CONT": prop read failed:"DF_RC"\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), DP_RC(rc));
	cont_put(cont);

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
	if (rc == 0) {
		/* Update prop IV with merged prop */
		rc = cont_iv_prop_update(svc->cs_pool->sp_iv_ns, cont_uuid, prop, true);
		if (rc)
			D_ERROR(DF_CONT": failed to update prop IV for cont, "
				DF_RC"\n", DP_CONT(svc->cs_pool_uuid, cont_uuid),
				DP_RC(rc));
	}

	if (prop != NULL)
		daos_prop_free(prop);
}

/*
 * Look up the container, or if the RPC does not need this, call the final
 * handler.
 */
static int
cont_op_with_svc(struct ds_pool_hdl *pool_hdl, struct cont_svc *svc,
		 crt_rpc_t *rpc, int cont_proto_ver)
{
	struct cont_op_in		*in = crt_req_get(rpc);
	struct cont_open_bylabel_in	*olbl_in = NULL;
	struct cont_open_bylabel_out	*olbl_out = NULL;
	struct cont_destroy_bylabel_in	*dlbl_in = NULL;
	struct rdb_tx			 tx;
	crt_opcode_t			 opc = opc_get(rpc->cr_opc);
	struct cont			*cont = NULL;
	struct cont_pool_metrics	*metrics;
	bool				 update_mtime = false;
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
		if (likely(rc == 0)) {
			metrics = pool_hdl->sph_pool->sp_metrics[DAOS_CONT_MODULE];
			d_tm_inc_counter(metrics->create_total, 1);
		}
		break;
	case CONT_OPEN_BYLABEL:
		olbl_in = crt_req_get(rpc);
		olbl_out = crt_reply_get(rpc);
		rc = cont_lookup_bylabel(&tx, svc, olbl_in->coli_label, &cont);
		if (rc != 0)
			goto out_lock;
		/* NB: call common cont_op_with_cont() same as CONT_OPEN case */
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc, &update_mtime, cont_proto_ver);
		uuid_copy(olbl_out->colo_uuid, cont->c_uuid);
		break;
	case CONT_DESTROY_BYLABEL:
		dlbl_in = crt_req_get(rpc);
		rc = cont_lookup_bylabel(&tx, svc, dlbl_in->cdli_label, &cont);
		if (rc != 0)
			goto out_lock;
		/* NB: call common cont_op_with_cont() same as CONT_DESTROY */
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc, &update_mtime, cont_proto_ver);
		break;
	default:
		rc = cont_lookup(&tx, svc, in->ci_uuid, &cont);
		if (rc != 0)
			goto out_lock;
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc, &update_mtime, cont_proto_ver);
	}
	if (rc != 0)
		goto out_contref;

	/* Update container metadata modified times as applicable
	 * NB: this is a NOOP if the pool has not been upgraded to the layout containing mdtimes.
	 */
	rc = get_metadata_times(&tx, cont, false /* otime */, update_mtime, NULL /* times */);
	if (rc != 0)
		goto out_contref;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_ERROR(DF_CONT": rpc=%p opc=%u hdl="DF_UUID" rdb_tx_commit "
			"failed: "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid),
			rpc, opc, DP_UUID(in->ci_hdl), DP_RC(rc));

out_contref:
	if (cont)
		cont_put(cont);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out:
	/* Propagate new snapshot list by IV */
	if (rc == 0) {
		if (opc == CONT_SNAP_CREATE || opc == CONT_SNAP_DESTROY)
			ds_cont_update_snap_iv(svc, in->ci_uuid);
		else if (opc == CONT_PROP_SET)
			ds_cont_prop_iv_update(svc, in->ci_uuid);
	}

	D_DEBUG(DB_MD, DF_CONT": opc=%d returning, "DF_RC"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), opc, DP_RC(rc));

	return rc;
}

static char *
cont_cli_opc_name(crt_opcode_t opc)
{
	switch (opc) {
	case CONT_CREATE:		return "CREATE";
	case CONT_DESTROY:		return "DESTROY";
	case CONT_OPEN:			return "OPEN";
	case CONT_CLOSE:		return "CLOSE";
	case CONT_QUERY:		return "QUERY";
	case CONT_OID_ALLOC:		return "OID_ALLOC";
	case CONT_ATTR_LIST:		return "ATTR_LIST";
	case CONT_ATTR_GET:		return "ATTR_GET";
	case CONT_ATTR_SET:		return "ATTR_SET";
	case CONT_ATTR_DEL:		return "ATTR_DEL";
	case CONT_EPOCH_AGGREGATE:	return "EPOCH_AGGREGATE";
	case CONT_SNAP_LIST:		return "SNAP_LIST";
	case CONT_SNAP_CREATE:		return "SNAP_CREATE";
	case CONT_SNAP_DESTROY:		return "SNAP_DESTROY";
	case CONT_PROP_SET:		return "PROP_SET";
	case CONT_ACL_UPDATE:		return "ACL_UPDATE";
	case CONT_ACL_DELETE:		return "ACL_DELETE";
	case CONT_OPEN_BYLABEL:		return "OPEN_BYLABEL";
	case CONT_DESTROY_BYLABEL:	return "DESTROY_BYLABEL";
	case CONT_SNAP_OIT_OID_GET:	return "SNAP_OIT_OID_GET";
	case CONT_SNAP_OIT_CREATE:	return "SNAP_OIT_CREATE";
	case CONT_SNAP_OIT_DESTROY:	return "SNAP_OIT_DESTROY";
	default:			return "?";
	}
}

/* Look up the pool handle and the matching container service. */
static void
ds_cont_op_handler(crt_rpc_t *rpc, int cont_proto_ver)
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

	D_DEBUG(DB_MD, DF_CONT ": processing rpc: %p proto=%d hdl=" DF_UUID " opc=%u(%s)\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc, cont_proto_ver,
		DP_UUID(in->ci_hdl), opc, cont_cli_opc_name(opc));

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup_leader(pool_hdl->sph_pool->sp_uuid, 0 /* id */,
				    &svc, &out->co_hint);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_CONT": rpc: %p hdl=" DF_UUID " opc=%u(%s) find leader\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc, DP_UUID(in->ci_hdl),
			opc, cont_cli_opc_name(opc));
		D_GOTO(out_pool_hdl, rc);
	}

	rc = cont_op_with_svc(pool_hdl, svc, rpc, cont_proto_ver);

	ds_rsvc_set_hint(svc->cs_rsvc, &out->co_hint);
	cont_svc_put_leader(svc);
out_pool_hdl:
	if (opc == CONT_OPEN_BYLABEL) {
		struct cont_open_bylabel_in	*lin = crt_req_get(rpc);
		struct cont_open_bylabel_out	*lout = crt_reply_get(rpc);

		D_DEBUG(DB_MD, DF_CONT":%s: replying rpc: %p hdl=" DF_UUID " opc=%u(%s) "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, lout->colo_uuid), lin->coli_label, rpc,
			DP_UUID(in->ci_hdl), opc, cont_cli_opc_name(opc), DP_RC(rc));
	} else if (opc == CONT_DESTROY_BYLABEL) {
		struct cont_destroy_bylabel_in	*lin = crt_req_get(rpc);

		D_DEBUG(DB_MD, DF_UUID":%s: replying rpc: %p opc=%u(%s), "DF_RC"\n",
			DP_UUID(pool_hdl->sph_pool->sp_uuid), lin->cdli_label, rpc, opc,
			cont_cli_opc_name(opc), DP_RC(rc));
	} else {
		D_DEBUG(DB_MD, DF_CONT": replying rpc: %p hdl=" DF_UUID " opc=%u(%s) "DF_RC"\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc, DP_UUID(in->ci_hdl),
			opc, cont_cli_opc_name(opc), DP_RC(rc));
	}
	ds_pool_hdl_put(pool_hdl);
out:
	/* cleanup the properties for cont_query */
	if (opc == CONT_QUERY) {
		struct cont_query_out *cqo = crt_reply_get(rpc);

		prop = cqo->cqo_prop;
	} else if ((opc == CONT_OPEN) || (opc == CONT_OPEN_BYLABEL)) {
		struct cont_open_out *coo = crt_reply_get(rpc);

		prop = coo->coo_prop;
	}
	out->co_rc = rc;
	crt_reply_send(rpc);
	daos_prop_free(prop);
}

void
ds_cont_op_handler_v7(crt_rpc_t *rpc)
{
	return ds_cont_op_handler(rpc, 7);
}

void
ds_cont_op_handler_v6(crt_rpc_t *rpc)
{
	return ds_cont_op_handler(rpc, 6);
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
		ds_cont_op_handler(rpc, 7);
		return;
	}

	/*
	 * Server RPCs don't have pool or container handles. Just need the pool
	 * and container UUIDs.
	 */
	uuid_copy(pool_uuid, in->cpsi_pool_uuid);
	uuid_copy(cont_uuid, in->cpsi_op.ci_uuid);

	D_DEBUG(DB_MD, DF_CONT": processing cont set prop rpc %p\n",
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
	D_DEBUG(DB_MD, DF_CONT ": replying rpc: %p rc=%d\n",
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

	rc = cont_prop_read(&tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, true);
	cont_put(cont);

	if (rc != 0)
		D_GOTO(out_lock, rc);

	D_ASSERT(prop != NULL);
	D_ASSERT(prop->dpp_nr <= CONT_PROP_NUM);

	*prop_out = prop;

out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_put:
	cont_svc_put_leader(svc);
	return rc;
}

int
ds_cont_hdl_rdb_lookup(uuid_t pool_uuid, uuid_t cont_hdl_uuid, struct container_hdl *chdl)
{
	d_iov_t		key;
	d_iov_t		value;
	struct rdb_tx	tx;
	struct cont_svc *svc;
	int		rc;

	rc = cont_svc_lookup_leader(pool_uuid, 0 /* id */, &svc, NULL);
	if (rc != 0) {
		D_ERROR(DF_CONT": find leader: %d\n",
			DP_CONT(pool_uuid, cont_hdl_uuid), rc);
		return rc;
	}

	/* check if it is server container hdl */
	if (uuid_compare(cont_hdl_uuid, svc->cs_pool->sp_srv_cont_hdl) == 0)
		D_GOTO(put, rc);

	rc = rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx);
	if (rc != 0)
		D_GOTO(put, rc);

	ABT_rwlock_rdlock(svc->cs_lock);
	/* See if this container handle already exists. */
	d_iov_set(&key, cont_hdl_uuid, sizeof(uuid_t));
	d_iov_set(&value, chdl, sizeof(*chdl));
	rc = rdb_tx_lookup(&tx, &svc->cs_hdls, &key, &value);
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);

put:
	D_DEBUG(DB_MD, DF_CONT "lookup rc %d.\n", DP_CONT(pool_uuid, cont_hdl_uuid), rc);
	cont_svc_put_leader(svc);
	return rc;
}
