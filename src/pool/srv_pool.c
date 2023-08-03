/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include <daos_api.h> /* for daos_prop_alloc/_free() */
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos/rsvc.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rdb.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/security.h>
#include <cart/api.h>
#include <cart/iv.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"
#include "srv_pool_map.h"

#define DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED 1

/* Pool service crt event */
struct pool_svc_event {
	d_list_t		psv_link;
	d_rank_t		psv_rank;
	uint64_t		psv_incarnation;
	enum crt_event_source	psv_src;
	enum crt_event_type	psv_type;
};

#define DF_PS_EVENT	"rank=%u inc="DF_U64" src=%d type=%d"
#define DP_PS_EVENT(e)	e->psv_rank, e->psv_incarnation, e->psv_src, e->psv_type

#define RECHOOSE_SLEEP_MS 250

/* Pool service crt-event-handling state */
struct pool_svc_events {
	ABT_mutex		pse_mutex;
	ABT_cond		pse_cv;
	d_list_t		pse_queue;
	ABT_thread		pse_handler;
	bool			pse_stop;
};

/* Pool service schedule state */
struct pool_svc_sched {
	int		psc_svc_rf;
	bool		psc_force_notify;	/* for pool_svc_step_up_cb */
	ABT_mutex	psc_mutex;		/* only for psc_cv */
	ABT_cond	psc_cv;
	bool		psc_in_progress;
	bool		psc_canceled;
};

static int
sched_init(struct pool_svc_sched *sched)
{
	int rc;

	rc = ABT_mutex_create(&sched->psc_mutex);
	if (rc != ABT_SUCCESS) {
		return dss_abterr2der(rc);
	}

	rc = ABT_cond_create(&sched->psc_cv);
	if (rc != ABT_SUCCESS) {
		ABT_mutex_free(&sched->psc_mutex);
		return dss_abterr2der(rc);
	}

	sched->psc_svc_rf = -1;
	sched->psc_force_notify = false;
	sched->psc_in_progress = false;
	sched->psc_canceled = false;
	return 0;
}

static void
sched_fini(struct pool_svc_sched *sched)
{
	ABT_cond_free(&sched->psc_cv);
	ABT_mutex_free(&sched->psc_mutex);
}

static void
sched_begin(struct pool_svc_sched *sched)
{
	sched->psc_in_progress = true;
	sched->psc_canceled = false;
}

static void
sched_end(struct pool_svc_sched *sched)
{
	sched->psc_in_progress = false;
	sched->psc_canceled = false;
}

static void
sched_cancel_and_wait(struct pool_svc_sched *sched)
{
	/*
	 * The CV requires a mutex. We don't otherwise need it for ULTs within
	 * the same xstream.
	 */
	ABT_mutex_lock(sched->psc_mutex);
	if (sched->psc_in_progress)
		sched->psc_canceled = true;
	while (sched->psc_in_progress)
		ABT_cond_wait(sched->psc_cv, sched->psc_mutex);
	ABT_mutex_unlock(sched->psc_mutex);
}

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
	struct pool_svc_events	ps_events;
	uint32_t		ps_global_version;
	struct pool_svc_sched	ps_reconf_sched;

	/* Check all containers RF for the pool */
	struct pool_svc_sched	ps_rfcheck_sched;
	/* The global pool map version on all pool targets */
	uint32_t		ps_global_map_version;
};

/* Pool service failed to start */
struct pool_svc_failed {
	uuid_t			psf_uuid;	/* pool UUID */
	int			psf_error;	/* error number */
	d_list_t		psf_link;	/* link to global list */
};

/** serialize operations on pool_svc_failed_list */
static pthread_rwlock_t		psfl_rwlock = PTHREAD_RWLOCK_INITIALIZER;
/* tracking failed pool service */
D_LIST_HEAD(pool_svc_failed_list);

static bool pool_disable_exclude;
static int pool_prop_read(struct rdb_tx *tx, const struct pool_svc *svc,
			  uint64_t bits, daos_prop_t **prop_out);
static int pool_space_query_bcast(crt_context_t ctx, struct pool_svc *svc,
				  uuid_t pool_hdl, struct daos_pool_space *ps);
static int ds_pool_upgrade_if_needed(uuid_t pool_uuid, struct rsvc_hint *po_hint,
				     struct pool_svc *svc, crt_rpc_t *rpc);
static int
find_hdls_to_evict(struct rdb_tx *tx, struct pool_svc *svc, uuid_t **hdl_uuids,
		   size_t *hdl_uuids_size, int *n_hdl_uuids, char *machine);

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

	D_DEBUG(DB_MD, "version=%u ntargets=%u ndomains=%u\n", version,
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
	D_DEBUG(DB_MD, "version=%u ntargets=%u ndomains=%u\n", *version,
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

/* copy \a prop to \a prop_def (duplicated default prop) */
static int
pool_prop_default_copy(daos_prop_t *prop_def, daos_prop_t *prop)
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
		case DAOS_PROP_PO_EC_CELL_SZ:
		case DAOS_PROP_PO_REDUN_FAC:
		case DAOS_PROP_PO_EC_PDA:
		case DAOS_PROP_PO_RP_PDA:
		case DAOS_PROP_PO_SVC_REDUN_FAC:
		case DAOS_PROP_PO_PERF_DOMAIN:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_PO_POLICY:
			D_FREE(entry_def->dpe_str);
			D_STRNDUP(entry_def->dpe_str, entry->dpe_str,
				  DAOS_PROP_POLICYSTR_MAX_LEN);
			if (entry_def->dpe_str == NULL)
				return -DER_NOMEM;
			break;
		case DAOS_PROP_PO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl = entry->dpe_val_ptr;

				D_FREE(entry_def->dpe_val_ptr);
				rc = daos_prop_entry_dup_ptr(entry_def, entry,
							     daos_acl_get_size(acl));
				if (rc)
					return rc;
			}
			break;
		case DAOS_PROP_PO_SCRUB_MODE:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_PO_SCRUB_FREQ:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_PO_SCRUB_THRESH:
			entry_def->dpe_val = entry->dpe_val;
			break;
		case DAOS_PROP_PO_GLOBAL_VERSION:
		case DAOS_PROP_PO_UPGRADE_STATUS:
		case DAOS_PROP_PO_OBJ_VERSION:
			D_ERROR("pool property %u could be not set\n", entry->dpe_type);
			return -DER_INVAL;
		default:
			D_ERROR("ignore bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}

	/* Validate the result */
	if (!daos_prop_valid(prop_def, true /* pool */, true /* input */)) {
		D_ERROR("properties validation check failed\n");
		return -DER_INVAL;
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
	uint32_t		 val32;
	uint32_t		 global_ver;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return 0;

	/*
	 * Determine the global version. In some cases, such as
	 * init_pool_metadata, the global version shall be found in prop, not
	 * in the RDB.
	 */
	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_GLOBAL_VERSION);
	if (entry == NULL || !daos_prop_is_set(entry)) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, kvs, &ds_pool_prop_global_version, &value);
		if (rc && rc != -DER_NONEXIST)
			return rc;
		else if (rc == -DER_NONEXIST)
			global_ver = 0;
		else
			global_ver = val32;
	} else {
		global_ver = entry->dpe_val;
	}
	D_DEBUG(DB_MD, "global version: %u\n", global_ver);

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
			break;
		case DAOS_PROP_PO_OWNER:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_owner,
					   &value);
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_owner_group,
					   &value);
			break;
		case DAOS_PROP_PO_ACL:
			if (entry->dpe_val_ptr != NULL) {
				struct daos_acl *acl;

				acl = entry->dpe_val_ptr;
				d_iov_set(&value, acl, daos_acl_get_size(acl));
				rc = rdb_tx_update(tx, kvs, &ds_pool_prop_acl,
						   &value);
			}
			break;
		case DAOS_PROP_PO_SPACE_RB:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_space_rb,
					   &value);
			break;
		case DAOS_PROP_PO_SELF_HEAL:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_self_heal,
					   &value);
			break;
		case DAOS_PROP_PO_RECLAIM:
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_reclaim,
					   &value);
			break;
		case DAOS_PROP_PO_EC_CELL_SZ:
			if (!daos_ec_cs_valid(entry->dpe_val)) {
				D_ERROR("DAOS_PROP_PO_EC_CELL_SZ property value"
					" "DF_U64" should within rage of "
					"["DF_U64", "DF_U64"] and multiplier of "DF_U64"\n",
					entry->dpe_val,
					DAOS_PROP_PO_EC_CELL_SZ_MIN,
					DAOS_PROP_PO_EC_CELL_SZ_MAX,
					DAOS_PROP_PO_EC_CELL_SZ_MIN);
				rc = -DER_INVAL;
				break;
			}
			d_iov_set(&value, &entry->dpe_val,
				     sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_ec_cell_sz,
					   &value);
			break;
		case DAOS_PROP_PO_REDUN_FAC:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_redun_fac,
					   &value);
			break;
		case DAOS_PROP_PO_POLICY:
			if (entry->dpe_str == NULL ||
			    strlen(entry->dpe_str) == 0) {
				entry = daos_prop_entry_get(&pool_prop_default,
							    entry->dpe_type);
				D_ASSERT(entry != NULL);
			}
			d_iov_set(&value, entry->dpe_str,
				     strlen(entry->dpe_str));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_policy,
					   &value);
			break;
		case DAOS_PROP_PO_SVC_LIST:
			break;
		case DAOS_PROP_PO_EC_PDA:
			if (!daos_ec_pda_valid(entry->dpe_val)) {
				rc = -DER_INVAL;
				break;
			}
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_ec_pda,
					   &value);
			break;
		case DAOS_PROP_PO_RP_PDA:
			if (!daos_rp_pda_valid(entry->dpe_val)) {
				rc = -DER_INVAL;
				break;
			}
			d_iov_set(&value, &entry->dpe_val,
				   sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_rp_pda,
					   &value);
			break;
		case DAOS_PROP_PO_SCRUB_MODE:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_scrub_mode,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_SCRUB_FREQ:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_scrub_freq,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_SCRUB_THRESH:
			d_iov_set(&value, &entry->dpe_val,
				  sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_scrub_thresh,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_GLOBAL_VERSION:
			if (entry->dpe_val > DAOS_POOL_GLOBAL_VERSION) {
				rc = -DER_INVAL;
				break;
			}
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_global_version,
					   &value);
			break;
		case DAOS_PROP_PO_UPGRADE_STATUS:
			if (entry->dpe_val > DAOS_UPGRADE_STATUS_COMPLETED) {
				rc = -DER_INVAL;
				break;
			}
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_upgrade_status,
					   &value);
			break;
		case DAOS_PROP_PO_PERF_DOMAIN:
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_perf_domain,
					   &value);
			break;
		case DAOS_PROP_PO_SVC_REDUN_FAC:
			if (global_ver < 2) {
				D_DEBUG(DB_MD, "skip writing svc_redun_fac for global version %u\n",
					global_ver);
				rc = 0;
				break;
			}
			d_iov_set(&value, &entry->dpe_val, sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_redun_fac, &value);
			break;
		case DAOS_PROP_PO_OBJ_VERSION:
			if (entry->dpe_val > DS_POOL_OBJ_VERSION) {
				rc = -DER_INVAL;
				break;
			}
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_obj_version, &value);
			break;
		case DAOS_PROP_PO_CHECKPOINT_MODE:
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_checkpoint_mode, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_CHECKPOINT_FREQ:
			val32 = entry->dpe_val;
			if (val32 > DAOS_PROP_PO_CHECKPOINT_FREQ_MAX)
				val32 = DAOS_PROP_PO_CHECKPOINT_FREQ_MAX;
			else if (val32 < DAOS_PROP_PO_CHECKPOINT_FREQ_MIN)
				val32 = DAOS_PROP_PO_CHECKPOINT_FREQ_MIN;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_checkpoint_freq, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_CHECKPOINT_THRESH:
			val32 = entry->dpe_val;
			if (val32 > DAOS_PROP_PO_CHECKPOINT_THRESH_MAX)
				val32 = DAOS_PROP_PO_CHECKPOINT_THRESH_MAX;
			else if (val32 < DAOS_PROP_PO_CHECKPOINT_THRESH_MIN)
				val32 = DAOS_PROP_PO_CHECKPOINT_THRESH_MIN;

			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_checkpoint_thresh, &value);
			if (rc)
				return rc;
			break;
		default:
			D_ERROR("bad dpe_type %d.\n", entry->dpe_type);
			return -DER_INVAL;
		}
		if (rc) {
			D_ERROR("Failed to update entry type=%d, rc="DF_RC"\n",
				entry->dpe_type, DP_RC(rc));
			break;
		}
	}
	return rc;
}

static int
init_pool_metadata(struct rdb_tx *tx, const rdb_path_t *kvs, uint32_t nnodes, const char *group,
		   const d_rank_list_t *ranks, daos_prop_t *prop, uint32_t ndomains,
		   const uint32_t *domains)
{
	struct pool_buf	       *map_buf;
	uint32_t		map_version = 1;
	uint32_t		connectable;
	uint32_t		nhandles = 0;
	d_iov_t			value;
	struct rdb_kvs_attr	attr;
	int			ntargets = nnodes * dss_tgt_nr;
	uint32_t		upgrade_global_version = DAOS_POOL_GLOBAL_VERSION;
	int			rc;
	struct daos_prop_entry *entry;

	rc = gen_pool_buf(NULL /* map */, &map_buf, map_version, ndomains, nnodes, ntargets,
			  domains, dss_tgt_nr);
	if (rc != 0) {
		D_ERROR("failed to generate pool buf, "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_REDUN_FAC);
	if (entry) {
		if (entry->dpe_val + 1 > map_buf->pb_domain_nr) {
			D_ERROR("ndomains(%u) could not meet redunc factor(%lu)\n",
				map_buf->pb_domain_nr, entry->dpe_val);
			D_GOTO(out_map_buf, rc = -DER_INVAL);
		}
	}

	/* Initialize the pool map properties. */
	rc = write_map_buf(tx, kvs, map_buf, map_version);
	if (rc != 0) {
		D_ERROR("failed to write map properties, "DF_RC"\n", DP_RC(rc));
		goto out_map_buf;
	}

	rc = pool_prop_write(tx, kvs, prop);
	if (rc != 0) {
		D_ERROR("failed to write props, "DF_RC"\n", DP_RC(rc));
		goto out_map_buf;
	}

	/* Write connectable property */
	connectable = 1;
	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_connectable, &value);
	if (rc != 0) {
		D_ERROR("failed to write connectable prop, "DF_RC"\n",
			DP_RC(rc));
		goto out_map_buf;
	}

	/**
	 * Firstly write upgrading global version, so resuming could figure
	 * out what is target global version of upgrading, use this to reject
	 * resuming pool upgrading if DAOS software upgraded again.
	 */
	d_iov_set(&value, &upgrade_global_version, sizeof(upgrade_global_version));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_upgrade_global_version, &value);
	if (rc != 0) {
		D_ERROR("failed to write upgrade global version prop, "DF_RC"\n",
			DP_RC(rc));
		goto out_map_buf;
	}

	/* Write the handle properties. */
	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_nhandles, &value);
	if (rc != 0) {
		D_ERROR("failed to update handle props, "DF_RC"\n", DP_RC(rc));
		goto out_map_buf;
	}
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_pool_prop_handles, &attr);
	if (rc != 0) {
		D_ERROR("failed to create handle prop KVS, "DF_RC"\n",
			DP_RC(rc));
		goto out_map_buf;
	}

	/* Create pool user attributes KVS */
	rc = rdb_tx_create_kvs(tx, kvs, &ds_pool_attr_user, &attr);
	if (rc != 0)
		D_ERROR("failed to create user attr KVS, "DF_RC"\n", DP_RC(rc));

out_map_buf:
	pool_buf_free(map_buf);
out:
	return rc;
}

/*
 * The svc_rf parameter inputs the pool service redundancy factor, while
 * ranks->rl_nr outputs how many replicas are actually selected, which may be
 * less than the number of replicas required to achieve the pool service
 * redundancy factor. If the return value is 0, callers are responsible for
 * calling d_rank_list_free(*ranksp).
 */
static int
select_svc_ranks(int svc_rf, const d_rank_list_t *target_addrs, int ndomains,
		 const uint32_t *domains, d_rank_list_t **ranksp)
{
	int			nreplicas = ds_pool_svc_rf_to_nreplicas(svc_rf);
	int			selectable;
	d_rank_list_t		*rnd_tgts;
	d_rank_list_t		*ranks;
	int			i;
	int			j;
	int			rc;

	rc = d_rank_list_dup(&rnd_tgts, target_addrs);
	if (rc != 0)
		return rc;

	/* Shuffle the target ranks to avoid overloading any particular ranks. */
	/*
	 * DAOS-9177: Temporarily disable shuffle to give us more time to stabilize tests.
	 */
	/*daos_rank_list_shuffle(rnd_tgts);*/

	/* Determine the number of selectable targets. */
	selectable = rnd_tgts->rl_nr;

	if (nreplicas > selectable)
		nreplicas = selectable;
	ranks = daos_rank_list_alloc(nreplicas);
	if (ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* TODO: Choose ranks according to failure domains. */
	j = 0;
	for (i = 0; i < rnd_tgts->rl_nr; i++) {
		if (j == ranks->rl_nr)
			break;
		D_DEBUG(DB_MD, "ranks[%d]: %u\n", j, rnd_tgts->rl_ranks[i]);
		ranks->rl_ranks[j] = rnd_tgts->rl_ranks[i];
		j++;
	}
	D_ASSERTF(j == ranks->rl_nr, "%d == %u\n", j, ranks->rl_nr);

	*ranksp = ranks;
	rc = 0;

out:
	d_rank_list_free(rnd_tgts);
	return rc;
}

/* TODO: replace all rsvc_complete_rpc() calls in this file with pool_rsvc_complete_rpc() */

/*
 * Returns:
 *
 *   RSVC_CLIENT_RECHOOSE	Instructs caller to retry RPC starting from rsvc_client_choose()
 *   RSVC_CLIENT_PROCEED	OK; proceed to process the reply
 */
static int
pool_rsvc_client_complete_rpc(struct rsvc_client *client, const crt_endpoint_t *ep,
			      int rc_crt, struct pool_op_out *out)
{
	int rc;

	rc = rsvc_client_complete_rpc(client, ep, rc_crt, out->po_rc, &out->po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->po_rc))) {
		return RSVC_CLIENT_RECHOOSE;
	}
	return RSVC_CLIENT_PROCEED;
}

/**
 * Create a (combined) pool(/container) service. This method shall be called on
 * a single storage node in the pool. If the return value is 0, the caller is
 * responsible for freeing \a svc_addrs with d_rank_list_free.
 *
 * Note that if the return value is nonzero, the caller is responsible for
 * stopping and destroying any PS replicas that may have been created. This
 * behavior is tailored for ds_mgmt_create_pool, who will clean up all pool
 * resources upon errors.
 *
 * \param[in]		pool_uuid	pool UUID
 * \param[in]		ntargets	number of targets in the pool
 * \param[in]		group		crt group ID (unused now)
 * \param[in]		target_addrs	list of \a ntargets target ranks
 * \param[in]		ndomains	number of domains the pool spans over
 * \param[in]		domains		serialized domain tree
 * \param[in]		prop		pool properties (must include a valid
 *					pool service redundancy factor)
 * \param[out]		svc_addrs	returns the list of pool service
 *					replica ranks
 */
int
ds_pool_svc_dist_create(const uuid_t pool_uuid, int ntargets, const char *group,
			const d_rank_list_t *target_addrs, int ndomains, const uint32_t *domains,
			daos_prop_t *prop, d_rank_list_t **svc_addrs)
{
	struct daos_prop_entry *svc_rf_entry;
	d_rank_list_t	       *ranks;
	d_iov_t			psid;
	struct rsvc_client	client;
	struct dss_module_info *info = dss_get_module_info();
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct daos_prop_entry *lbl_ent;
	struct daos_prop_entry *def_lbl_ent;
	struct pool_create_in  *in;
	struct pool_create_out *out;
	struct d_backoff_seq	backoff_seq;
	int			rc;

	/* Check for default label supplied via property. */
	def_lbl_ent = daos_prop_entry_get(&pool_prop_default, DAOS_PROP_PO_LABEL);
	D_ASSERT(def_lbl_ent != NULL);
	lbl_ent = daos_prop_entry_get(prop, DAOS_PROP_PO_LABEL);
	if (lbl_ent != NULL) {
		if (strncmp(def_lbl_ent->dpe_str, lbl_ent->dpe_str,
			    DAOS_PROP_LABEL_MAX_LEN) == 0) {
			D_ERROR(DF_UUID": label is the same as default label\n",
				DP_UUID(pool_uuid));
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	D_ASSERTF(ntargets == target_addrs->rl_nr, "ntargets=%d num=%u\n",
		  ntargets, target_addrs->rl_nr);

	svc_rf_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_REDUN_FAC);
	D_ASSERT(svc_rf_entry != NULL && !(svc_rf_entry->dpe_flags & DAOS_PROP_ENTRY_NOT_SET));
	D_ASSERTF(daos_svc_rf_is_valid(svc_rf_entry->dpe_val), DF_U64"\n", svc_rf_entry->dpe_val);

	D_DEBUG(DB_MD, DF_UUID": creating PS: ntargets=%d ndomains=%d svc_rf="DF_U64"\n",
		DP_UUID(pool_uuid), ntargets, ndomains, svc_rf_entry->dpe_val);

	rc = select_svc_ranks(svc_rf_entry->dpe_val, target_addrs, ndomains, domains, &ranks);
	if (rc != 0)
		D_GOTO(out, rc);

	d_iov_set(&psid, (void *)pool_uuid, sizeof(uuid_t));
	rc = ds_rsvc_dist_start(DS_RSVC_CLASS_POOL, &psid, pool_uuid, ranks, RDB_NIL_TERM,
				true /* create */, true /* bootstrap */, ds_rsvc_get_md_cap());
	if (rc != 0)
		D_GOTO(out_ranks, rc);

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		D_GOTO(out_ranks, rc);

	rc = d_backoff_seq_init(&backoff_seq, 0 /* nzeros */, 16 /* factor */,
				8 /* next (ms) */, 1 << 10 /* max (ms) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

rechoose:
	/* Create a POOL_CREATE request. */
	ep.ep_grp = NULL;
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_backoff_seq;
	}
	rc = pool_req_create(info->dmi_ctx, &ep, POOL_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create POOL_CREATE RPC: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_backoff_seq;
	}
	in = crt_req_get(rpc);
	uuid_copy(in->pri_op.pi_uuid, pool_uuid);
	uuid_clear(in->pri_op.pi_hdl);
	in->pri_ntgts = ntargets;
	in->pri_tgt_ranks = (d_rank_list_t *)target_addrs;
	in->pri_prop = prop;
	in->pri_ndomains = ndomains;
	in->pri_domains.ca_count = ndomains;
	in->pri_domains.ca_arrays = (uint32_t *)domains;

	/* Send the POOL_CREATE request. */
	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);
	rc = rsvc_client_complete_rpc(&client, &ep, rc,
				      rc == 0 ? out->pro_op.po_rc : -DER_IO,
				      rc == 0 ? &out->pro_op.po_hint : NULL);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->pro_op.po_rc))) {
		crt_req_decref(rpc);
		dss_sleep(d_backoff_seq_next(&backoff_seq));
		D_GOTO(rechoose, rc);
	}
	rc = out->pro_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		D_GOTO(out_rpc, rc);
	}

	rc = d_rank_list_dup(svc_addrs, ranks);

out_rpc:
	crt_req_decref(rpc);
out_backoff_seq:
	d_backoff_seq_fini(&backoff_seq);
	rsvc_client_fini(&client);
	/*
	 * Intentionally skip cleaning up the PS replicas. See the function
	 * documentation above.
	 */
out_ranks:
	d_rank_list_free(ranks);
out:
	return rc;
}

/** Stop any local PS replica for \a pool_uuid. */
int
ds_pool_svc_stop(uuid_t pool_uuid)
{
	d_iov_t	id;
	int	rc;

	d_iov_set(&id, pool_uuid, sizeof(uuid_t));

	rc = ds_rsvc_stop(DS_RSVC_CLASS_POOL, &id, RDB_NIL_TERM, false /* destroy */);
	if (rc == -DER_ALREADY) {
		D_DEBUG(DB_MD, DF_UUID": ds_rsvc_stop: "DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
		rc = 0;
	}

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
	D_INIT_LIST_HEAD(&svc->ps_events.pse_queue);
	svc->ps_events.pse_handler = ABT_THREAD_NULL;

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

	rc = ABT_mutex_create(&svc->ps_events.pse_mutex);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto err_user;
	}

	rc = ABT_cond_create(&svc->ps_events.pse_cv);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto err_events_mutex;
	}

	rc = sched_init(&svc->ps_reconf_sched);
	if (rc != 0)
		goto err_events_cv;

	rc = sched_init(&svc->ps_rfcheck_sched);
	if (rc != 0)
		goto err_sched;

	rc = ds_cont_svc_init(&svc->ps_cont_svc, svc->ps_uuid, 0 /* id */,
			      &svc->ps_rsvc);
	if (rc != 0)
		goto err_cont_rf_sched;

	*rsvc = &svc->ps_rsvc;
	return 0;
err_cont_rf_sched:
	sched_fini(&svc->ps_rfcheck_sched);
err_sched:
	sched_fini(&svc->ps_reconf_sched);
err_events_cv:
	ABT_cond_free(&svc->ps_events.pse_cv);
err_events_mutex:
	ABT_mutex_free(&svc->ps_events.pse_mutex);
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
pool_svc_put(struct pool_svc *svc)
{
	ds_rsvc_put(&svc->ps_rsvc);
}

/* Disable all pools exclusion */
void
ds_pool_disable_exclude(void)
{
	pool_disable_exclude = true;
}

void
ds_pool_enable_exclude(void)
{
	pool_disable_exclude = false;
}

static int
queue_event(struct pool_svc *svc, d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
	    enum crt_event_type type)
{
	struct pool_svc_events *events = &svc->ps_events;
	struct pool_svc_event  *event;

	D_ALLOC_PTR(event);
	if (event == NULL)
		return -DER_NOMEM;

	event->psv_rank = rank;
	event->psv_incarnation = incarnation;
	event->psv_src = src;
	event->psv_type = type;

	D_DEBUG(DB_MD, DF_UUID": queuing event: "DF_PS_EVENT"\n", DP_UUID(svc->ps_uuid),
		DP_PS_EVENT(event));

	ABT_mutex_lock(events->pse_mutex);
	d_list_add_tail(&event->psv_link, &events->pse_queue);
	ABT_cond_broadcast(events->pse_cv);
	ABT_mutex_unlock(events->pse_mutex);
	return 0;
}

static void
discard_events(d_list_t *queue)
{
	struct pool_svc_event  *event;
	struct pool_svc_event  *tmp;

	d_list_for_each_entry_safe(event, tmp, queue, psv_link) {
		D_DEBUG(DB_MD, "discard event: "DF_PS_EVENT"\n", DP_PS_EVENT(event));
		d_list_del_init(&event->psv_link);
		D_FREE(event);
	}
}

static int pool_svc_exclude_rank(struct pool_svc *svc, d_rank_t rank);

static void
handle_event(struct pool_svc *svc, struct pool_svc_event *event)
{
	int rc;

	if ((event->psv_src != CRT_EVS_GRPMOD && event->psv_src != CRT_EVS_SWIM) ||
	    (event->psv_type == CRT_EVT_DEAD && pool_disable_exclude)) {
		D_DEBUG(DB_MD, "ignore event: "DF_PS_EVENT" exclude=%d\n", DP_PS_EVENT(event),
			pool_disable_exclude);
		goto out;
	}

	if (event->psv_rank == dss_self_rank() && event->psv_src == CRT_EVS_GRPMOD &&
	    event->psv_type == CRT_EVT_DEAD) {
		D_DEBUG(DB_MGMT, "ignore exclusion of self\n");
		goto out;
	}

	D_DEBUG(DB_MD, DF_UUID": handling event: "DF_PS_EVENT"\n", DP_UUID(svc->ps_uuid),
		DP_PS_EVENT(event));

	if (event->psv_src == CRT_EVS_SWIM && event->psv_type == CRT_EVT_ALIVE) {
		/*
		 * Check if the rank is up in the pool map. If in the future we
		 * add automatic reintegration below, for instance, we may need
		 * to not only take svc->ps_lock, but also employ an RDB TX by
		 * the book.
		 */
		ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
		rc = ds_pool_map_rank_up(svc->ps_pool->sp_map, event->psv_rank);
		ABT_rwlock_unlock(svc->ps_pool->sp_lock);
		if (!rc)
			goto out;

		/*
		 * The rank is up in the pool map. Request a pool map
		 * distribution just in case the rank has recently restarted
		 * and does not have a copy of the pool map.
		 */
		ds_rsvc_request_map_dist(&svc->ps_rsvc);
		D_DEBUG(DB_MD, DF_UUID": requested map dist for rank %u\n", DP_UUID(svc->ps_uuid),
			event->psv_rank);
	} else if (event->psv_type == CRT_EVT_DEAD) {
		daos_prop_t		prop = {0};
		struct daos_prop_entry *entry;

		rc = ds_pool_iv_prop_fetch(svc->ps_pool, &prop);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to fetch properties: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
			goto out;
		}

		entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SELF_HEAL);
		D_ASSERT(entry != NULL);
		if (!(entry->dpe_val & DAOS_SELF_HEAL_AUTO_EXCLUDE)) {
			D_DEBUG(DB_MD, DF_UUID": self healing is disabled\n",
				DP_UUID(svc->ps_uuid));
			goto out_prop;
		}

		rc = pool_svc_exclude_rank(svc, event->psv_rank);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to exclude rank %u: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), event->psv_rank, DP_RC(rc));
			goto out_prop;
		}

		D_DEBUG(DB_MD, DF_UUID": excluded rank %u\n", DP_UUID(svc->ps_uuid),
			event->psv_rank);
out_prop:
		daos_prop_fini(&prop);
	}

out:
	return;
}

static void
events_handler(void *arg)
{
	struct pool_svc	       *svc = arg;
	struct pool_svc_events *events = &svc->ps_events;

	D_DEBUG(DB_MD, DF_UUID": starting\n", DP_UUID(svc->ps_uuid));

	for (;;) {
		struct pool_svc_event  *event = NULL;
		bool			stop;

		ABT_mutex_lock(events->pse_mutex);
		for (;;) {
			stop = events->pse_stop;
			if (stop) {
				discard_events(&events->pse_queue);
				break;
			}
			if (!d_list_empty(&events->pse_queue)) {
				event = d_list_entry(events->pse_queue.next, struct pool_svc_event,
						     psv_link);
				d_list_del_init(&event->psv_link);
				break;
			}
			sched_cond_wait(events->pse_cv, events->pse_mutex);
		}
		ABT_mutex_unlock(events->pse_mutex);
		if (stop)
			break;

		handle_event(svc, event);

		D_FREE(event);
		ABT_thread_yield();
	}

	D_DEBUG(DB_MD, DF_UUID": stopping\n", DP_UUID(svc->ps_uuid));
}

static bool
events_pending(struct pool_svc *svc)
{
	return !d_list_empty(&svc->ps_events.pse_queue);
}

static void
ds_pool_crt_event_cb(d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
		     enum crt_event_type type, void *arg)
{
	struct pool_svc	       *svc = arg;
	int			rc;

	rc = queue_event(svc, rank, incarnation, src, type);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to queue event: "DF_PS_EVENT": "DF_RC"\n",
			DP_UUID(svc->ps_uuid), rank, incarnation, src, type, DP_RC(rc));
}

static int pool_svc_check_node_status(struct pool_svc *svc);

static int
init_events(struct pool_svc *svc)
{
	struct pool_svc_events *events = &svc->ps_events;
	int			rc;

	D_ASSERT(d_list_empty(&events->pse_queue));
	D_ASSERT(events->pse_handler == ABT_THREAD_NULL);
	D_ASSERT(events->pse_stop == false);

	rc = crt_register_event_cb(ds_pool_crt_event_cb, svc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to register event callback: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto err;
	}

	/*
	 * Note that events happened during the status-based recovery may
	 * appear twice in the event queue: one queued by the event callback,
	 * and one queued by the recovery.
	 */
	rc = pool_svc_check_node_status(svc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create event handler: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto err_cb;
	}

	rc = dss_ult_create(events_handler, svc, DSS_XS_SELF, 0, 0, &events->pse_handler);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create event handler: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto err_cb;
	}

	return 0;

err_cb:
	crt_unregister_event_cb(ds_pool_crt_event_cb, svc);
	discard_events(&events->pse_queue);
err:
	return rc;
}

static void
fini_events(struct pool_svc *svc)
{
	struct pool_svc_events *events = &svc->ps_events;
	int			rc;

	D_ASSERT(events->pse_handler != ABT_THREAD_NULL);

	crt_unregister_event_cb(ds_pool_crt_event_cb, svc);

	ABT_mutex_lock(events->pse_mutex);
	events->pse_stop = true;
	ABT_cond_broadcast(events->pse_cv);
	ABT_mutex_unlock(events->pse_mutex);

	rc = ABT_thread_join(events->pse_handler);
	D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	ABT_thread_free(&events->pse_handler);
	events->pse_handler = ABT_THREAD_NULL;
	events->pse_stop = false;
}

static void
pool_svc_free_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc *svc = pool_svc_obj(rsvc);

	ds_cont_svc_fini(&svc->ps_cont_svc);
	sched_fini(&svc->ps_reconf_sched);
	sched_fini(&svc->ps_rfcheck_sched);
	ABT_cond_free(&svc->ps_events.pse_cv);
	ABT_mutex_free(&svc->ps_events.pse_mutex);
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
	      uint32_t map_version, uint64_t term)
{
	struct ds_pool *pool;
	int		rc;

	rc = ds_pool_lookup(svc->ps_uuid, &pool);
	if (rc) {
		D_ERROR(DF_UUID": failed to get ds_pool: %d\n",
			DP_UUID(svc->ps_uuid), rc);
		return rc;
	}
	rc = ds_pool_tgt_map_update(pool, map_buf, map_version);
	if (rc != 0) {
		ds_pool_put(pool);
		return rc;
	}
	ds_pool_iv_ns_update(pool, dss_self_rank(), term);

	D_ASSERT(svc->ps_pool == NULL);
	svc->ps_pool = pool;
	return 0;
}

/* Finalize svc->ps_pool. */
static void
fini_svc_pool(struct pool_svc *svc)
{
	D_ASSERT(svc->ps_pool != NULL);
	ds_pool_put(svc->ps_pool);
	svc->ps_pool = NULL;
}

/* Is the primary group initialized (i.e., version > 0)? */
static bool
primary_group_initialized(void)
{
	uint32_t	version;
	int		rc;

	rc = crt_group_version(NULL /* grp */, &version);
	D_ASSERTF(rc == 0, "crt_group_version: "DF_RC"\n", DP_RC(rc));
	return (version > 0);
}

/*
 * Read the DB for map_buf, map_version, and prop. Callers are responsible for
 * freeing *map_buf and *prop.
 */
static int
read_db_for_stepping_up(struct pool_svc *svc, struct pool_buf **map_buf,
			uint32_t *map_version, daos_prop_t **prop)
{
	struct rdb_tx		tx;
	d_iov_t			value;
	bool			version_exists = false;
	struct daos_prop_entry *svc_rf_entry;
	int			rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_rdlock(svc->ps_lock);

	/* Check the layout version. */
	d_iov_set(&value, &svc->ps_global_version, sizeof(svc->ps_global_version));
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_global_version, &value);
	if (rc == -DER_NONEXIST) {
		/*
		 * This DB may be new or incompatible. Check the existence of
		 * the pool map to find out which is the case. (See the
		 * references to version_exists below.)
		 */
		D_DEBUG(DB_MD, DF_UUID": no layout version\n",
			DP_UUID(svc->ps_uuid));
		goto check_map;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to look up layout version: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out_lock;
	}
	version_exists = true;

	/**
	 * downgrading the DAOS software of an upgraded pool report
	 * a proper RAS error.
	 */
	if (svc->ps_global_version > DAOS_POOL_GLOBAL_VERSION) {
		ds_notify_ras_eventf(RAS_POOL_DF_INCOMPAT, RAS_TYPE_INFO,
				     RAS_SEV_ERROR, NULL /* hwid */,
				     NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */,
				     &svc->ps_uuid, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */,
				     NULL /* data */,
				     "incompatible layout version: %u larger than "
				     "%u", svc->ps_global_version,
				     DAOS_POOL_GLOBAL_VERSION);
		rc = -DER_DF_INCOMPT;
		goto out_lock;
	}

check_map:
	rc = read_map_buf(&tx, &svc->ps_root, map_buf, map_version);
	if (rc != 0) {
		if (rc == -DER_NONEXIST && !version_exists) {
			/*
			 * This DB is new. Note that if the layout version
			 * exists, then the pool map must also exist;
			 * otherwise, it is an error.
			 */
			D_DEBUG(DB_MD, DF_UUID": new db\n",
				DP_UUID(svc->ps_uuid));
			rc = + DER_UNINIT;
		} else {
			D_ERROR(DF_UUID": failed to read pool map buffer: "DF_RC
				"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
		}
		goto out_lock;
	}

	if (!version_exists)
		/* This could also be a 1.x pool, which we assume nobody cares. */
		D_DEBUG(DB_MD, DF_UUID": assuming 2.0\n", DP_UUID(svc->ps_uuid));

	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ALL, prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot get properties: "DF_RC"\n", DP_UUID(svc->ps_uuid),
			DP_RC(rc));
		goto out_lock;
	}

	svc_rf_entry = daos_prop_entry_get(*prop, DAOS_PROP_PO_SVC_REDUN_FAC);
	D_ASSERT(svc_rf_entry != NULL);
	if (daos_prop_is_set(svc_rf_entry))
		svc->ps_reconf_sched.psc_svc_rf = svc_rf_entry->dpe_val;
	else
		svc->ps_reconf_sched.psc_svc_rf = -1;

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

int
ds_pool_svc_rf_to_nreplicas(int svc_rf)
{
	D_ASSERTF(daos_svc_rf_is_valid(svc_rf), "%d out of range\n", svc_rf);
	return svc_rf * 2 + 1;
}

int
ds_pool_svc_rf_from_nreplicas(int nreplicas)
{
	int svc_rf;

	D_ASSERTF(nreplicas > 0, "%d out of range\n", nreplicas);
	if (nreplicas % 2 == 0)
		svc_rf = (nreplicas - 1) / 2;
	else
		svc_rf = nreplicas / 2;
	if (svc_rf > DAOS_PROP_PO_SVC_REDUN_FAC_MAX)
		svc_rf = DAOS_PROP_PO_SVC_REDUN_FAC_MAX;
	return svc_rf;
}

/*
 * There might be some rank status inconsistency, let's check and
 * fix it.
 */
static int
pool_svc_check_node_status(struct pool_svc *svc)
{
	struct pool_domain     *doms;
	int			doms_cnt;
	int			i;
	int			rc = 0;

	if (pool_disable_exclude) {
		D_DEBUG(DB_MD, DF_UUID": skip: exclusion disabled\n", DP_UUID(svc->ps_uuid));
		return 0;
	}

	D_DEBUG(DB_MD, DF_UUID": checking node status\n", DP_UUID(svc->ps_uuid));
	ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
	doms_cnt = pool_map_find_nodes(svc->ps_pool->sp_map, PO_COMP_ID_ALL,
				       &doms);
	D_ASSERT(doms_cnt >= 0);
	for (i = 0; i < doms_cnt; i++) {
		struct swim_member_state state;

		/* Only check if UPIN server is excluded or dead for now */
		if (!(doms[i].do_comp.co_status & PO_COMP_ST_UPIN))
			continue;

		rc = crt_rank_state_get(crt_group_lookup(NULL),
					doms[i].do_comp.co_rank, &state);
		if (rc != 0 && rc != -DER_NONEXIST) {
			D_ERROR("failed to get status of rank %u: %d\n",
				doms[i].do_comp.co_rank, rc);
			break;
		}

		/* Since there is a big chance the INACTIVE node will become
		 * ACTIVE soon, let's only evict the DEAD node rank for the
		 * moment.
		 */
		D_DEBUG(DB_REBUILD, "rank/state %d/%d\n",
			doms[i].do_comp.co_rank,
			rc == -DER_NONEXIST ? -1 : state.sms_status);
		if (rc == -DER_NONEXIST || state.sms_status == SWIM_MEMBER_DEAD) {
			rc = queue_event(svc, doms[i].do_comp.co_rank, 0 /* incarnation */,
					 rc == -DER_NONEXIST ? CRT_EVS_GRPMOD : CRT_EVS_SWIM,
					 CRT_EVT_DEAD);
			if (rc) {
				D_ERROR("failed to exclude rank %u: %d\n",
					doms[i].do_comp.co_rank, rc);
				break;
			}
		}
	}
	ABT_rwlock_unlock(svc->ps_pool->sp_lock);
	return rc;
}

/*
 * Log a NOTE of as well as print a message. Arguments may be evaluated more
 * than once.
 */
#define DS_POOL_NOTE_PRINT(fmt, ...) do {							\
	D_NOTE(fmt, ## __VA_ARGS__);								\
	D_PRINT(fmt, ## __VA_ARGS__);								\
} while (0)

static void pool_svc_schedule(struct pool_svc *svc, struct pool_svc_sched *sched,
			      void (*func)(void *));
static void pool_svc_reconf_ult(void *arg);
static void pool_svc_rfcheck_ult(void *arg);

static int
pool_svc_step_up_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc	       *svc = pool_svc_obj(rsvc);
	struct pool_buf	       *map_buf = NULL;
	uint32_t		map_version;
	uuid_t			pool_hdl_uuid;
	uuid_t			cont_hdl_uuid;
	daos_prop_t	       *prop = NULL;
	bool			cont_svc_up = false;
	bool			events_initialized = false;
	bool			svc_scheduled = false;
	d_rank_t		rank = dss_self_rank();
	int			rc;

	/*
	 * If this is the only voting replica, it may have become the leader
	 * without doing any RPC. The primary group may have yet to be
	 * initialized by the MS. Proceeding with such a primary group may
	 * result in unnecessary rank exclusions (see the
	 * pool_svc_check_node_status call below). Wait for the primary group
	 * initialization by retrying the leader election (rate-limited by
	 * rdb_timerd). (If there's at least one other voting replica, at least
	 * one RPC must have been done, so the primary group must have been
	 * initialized at this point.)
	 */
	if (!primary_group_initialized())
		return -DER_GRPVER;

	rc = read_db_for_stepping_up(svc, &map_buf, &map_version, &prop);
	if (rc != 0)
		goto out;

	rc = init_svc_pool(svc, map_buf, map_version, svc->ps_rsvc.s_term);
	if (rc != 0)
		goto out;

	/*
	 * Just in case the previous leader didn't complete distributing the
	 * latest pool map. This doesn't need to be undone if we encounter an
	 * error below.
	 */
	ds_rsvc_request_map_dist(&svc->ps_rsvc);

	rc = ds_cont_svc_step_up(svc->ps_cont_svc);
	if (rc != 0)
		goto out;
	cont_svc_up = true;

	rc = init_events(svc);
	if (rc != 0)
		goto out;
	events_initialized = true;

	/*
	 * Just in case the previous leader didn't finish the last series of
	 * reconfigurations or the last MS notification.
	 */
	svc->ps_reconf_sched.psc_force_notify = true;
	pool_svc_schedule(svc, &svc->ps_reconf_sched, pool_svc_reconf_ult);
	pool_svc_schedule(svc, &svc->ps_rfcheck_sched, pool_svc_rfcheck_ult);
	svc_scheduled = true;

	rc = ds_pool_iv_prop_update(svc->ps_pool, prop);
	if (rc) {
		D_ERROR("ds_pool_iv_prop_update failed: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

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
		D_ERROR("ds_pool_iv_srv_hdl_update failed: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* resume pool upgrade if needed */
	rc = ds_pool_upgrade_if_needed(svc->ps_uuid, NULL, svc, NULL);
	if (rc != 0)
		goto out;

	rc = ds_rebuild_regenerate_task(svc->ps_pool, prop);
	if (rc != 0)
		goto out;

	DS_POOL_NOTE_PRINT(DF_UUID": rank %u became pool service leader "DF_U64": srv_pool_hdl="
			   DF_UUID" srv_cont_hdl="DF_UUID"\n", DP_UUID(svc->ps_uuid), rank,
			   svc->ps_rsvc.s_term, DP_UUID(pool_hdl_uuid), DP_UUID(cont_hdl_uuid));
out:
	if (rc != 0) {
		if (events_initialized)
			fini_events(svc);
		if (svc_scheduled) {
			sched_cancel_and_wait(&svc->ps_reconf_sched);
			sched_cancel_and_wait(&svc->ps_rfcheck_sched);
		}

		if (cont_svc_up)
			ds_cont_svc_step_down(svc->ps_cont_svc);
		if (svc->ps_pool != NULL)
			fini_svc_pool(svc);
	}
	if (map_buf != NULL)
		D_FREE(map_buf);
	if (prop != NULL)
		daos_prop_free(prop);
	if (rc < 0)
		ds_pool_failed_add(svc->ps_uuid, rc);
	else if (rc == 0)
		ds_pool_failed_remove(svc->ps_uuid);
	return rc;
}

static void
pool_svc_step_down_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc	       *svc = pool_svc_obj(rsvc);
	d_rank_t		rank = dss_self_rank();

	fini_events(svc);
	sched_cancel_and_wait(&svc->ps_reconf_sched);
	sched_cancel_and_wait(&svc->ps_rfcheck_sched);
	ds_cont_svc_step_down(svc->ps_cont_svc);
	fini_svc_pool(svc);

	DS_POOL_NOTE_PRINT(DF_UUID": rank %u no longer pool service leader "DF_U64"\n",
			   DP_UUID(svc->ps_uuid), rank, svc->ps_rsvc.s_term);
}

static void
pool_svc_drain_cb(struct ds_rsvc *rsvc)
{
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
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to distribute pool map %u: %d\n",
			DP_UUID(svc->ps_uuid), map_version, rc);
		D_GOTO(out, rc);
	}
	svc->ps_global_map_version = max(svc->ps_global_map_version, map_version);
out:
	if (map_buf != NULL)
		D_FREE(map_buf);
	return rc;
}

static struct ds_rsvc_class pool_svc_rsvc_class = {
	.sc_name	= pool_svc_name_cb,
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

	rc = ds_pool_failed_lookup(uuid);
	if (rc) {
		D_ERROR(DF_UUID": failed to start: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		return -DER_NO_SERVICE;
	}

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

int ds_pool_failed_add(uuid_t uuid, int rc)
{
	struct pool_svc_failed	*psf;
	int ret = 0;

	if (rc == 0)
		return 0;

	D_RWLOCK_WRLOCK(&psfl_rwlock);
	d_list_for_each_entry(psf, &pool_svc_failed_list, psf_link) {
		if (uuid_compare(psf->psf_uuid, uuid) == 0) {
			ret = 0;
			goto out;
		}
	}

	D_ALLOC_PTR(psf);
	if (psf == NULL) {
		ret = -DER_NOMEM;
		goto out;
	}

	uuid_copy(psf->psf_uuid, uuid);
	psf->psf_error = rc;
	d_list_add_tail(&psf->psf_link, &pool_svc_failed_list);
out:
	D_RWLOCK_UNLOCK(&psfl_rwlock);
	return ret;
}

void ds_pool_failed_remove(uuid_t uuid)
{
	struct pool_svc_failed	*psf;
	struct pool_svc_failed	*tmp;

	D_RWLOCK_WRLOCK(&psfl_rwlock);
	d_list_for_each_entry_safe(psf, tmp, &pool_svc_failed_list, psf_link) {
		if (uuid_compare(psf->psf_uuid, uuid) == 0) {
			d_list_del_init(&psf->psf_link);
			D_FREE(psf);
			break;
		}
	}
	D_RWLOCK_UNLOCK(&psfl_rwlock);
}

/* return error if failed pool found, otherwise 0 is returned */
int ds_pool_failed_lookup(uuid_t uuid)
{
	struct pool_svc_failed	*psf;

	D_RWLOCK_RDLOCK(&psfl_rwlock);
	d_list_for_each_entry(psf, &pool_svc_failed_list, psf_link) {
		if (uuid_compare(psf->psf_uuid, uuid) == 0) {
			D_RWLOCK_UNLOCK(&psfl_rwlock);
			return psf->psf_error;
		}
	}
	D_RWLOCK_UNLOCK(&psfl_rwlock);

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
		ds_pool_failed_add(uuid, rc);
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
	ds_rsvc_start(DS_RSVC_CLASS_POOL, &id, uuid, RDB_NIL_TERM, false /* create */, 0 /* size */,
		      NULL /* replicas */, NULL /* arg */);
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

static int
stop_one(uuid_t uuid, void *varg)
{
	D_DEBUG(DB_MD, DF_UUID": stopping pool\n", DP_UUID(uuid));
	ds_pool_stop(uuid);
	return 0;
}

static void
pool_stop_all(void *arg)
{
	int	rc;

	rc = ds_rsvc_stop_all(DS_RSVC_CLASS_POOL);
	if (rc != 0)
		D_ERROR("failed to stop all pool svcs: "DF_RC"\n", DP_RC(rc));

	ds_pool_hdl_delete_all();

	rc = ds_mgmt_tgt_pool_iterate(stop_one, NULL /* arg */);
	if (rc != 0)
		D_ERROR("failed to stop all pools: "DF_RC"\n", DP_RC(rc));
}

/*
 * Note that this function is currently called from the main xstream to save
 * one ULT creation.
 */
int
ds_pool_stop_all(void)
{
	ABT_thread	thread;
	int		rc;

	/* Create a ULT to stop pools, since it requires TLS */
	rc = dss_ult_create(pool_stop_all, NULL /* arg */, DSS_XS_SYS,
			    0 /* tgt_idx */, 0 /* stack_size */, &thread);
	if (rc != 0) {
		D_ERROR("failed to create pool stop ULT: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}
	ABT_thread_free(&thread);

	return 0;
}

static int
bcast_create(crt_context_t ctx, struct pool_svc *svc, crt_opcode_t opcode,
	     crt_bulk_t bulk_hdl, crt_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->ps_pool, DAOS_POOL_MODULE, opcode,
				    DAOS_POOL_VERSION, rpc, bulk_hdl, NULL);
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
	d_iov_t		 value;
	uint64_t	 val;
	uint64_t	 bit;
	uint32_t	 idx = 0, nr = 0, val32 = 0, global_ver;
	int		 rc;

	for (bit = DAOS_PO_QUERY_PROP_BIT_START;
	     bit <= DAOS_PO_QUERY_PROP_BIT_END; bit++) {
		if (bits & (1L << bit))
			nr++;
	}
	if (nr == 0)
		return 0;

	/* get pool global version */
	d_iov_set(&value, &val32, sizeof(val32));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_global_version,
			   &value);
	if (rc && rc != -DER_NONEXIST)
		return rc;
	else if (rc == -DER_NONEXIST)
		global_ver = 0;
	else
		global_ver = val32;

	prop = daos_prop_alloc(nr);
	if (prop == NULL)
		return -DER_NOMEM;
	if (bits & DAOS_PO_QUERY_PROP_LABEL) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_label,
				   &value);
		if (rc != 0)
			D_GOTO(out_prop, rc);
		if (value.iov_len > DAOS_PROP_LABEL_MAX_LEN) {
			D_ERROR("bad label length %zu (> %d).\n", value.iov_len,
				DAOS_PROP_LABEL_MAX_LEN);
			D_GOTO(out_prop, rc = -DER_IO);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_LABEL;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SPACE_RB) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_space_rb,
				   &value);
		if (rc != 0)
			D_GOTO(out_prop, rc);
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
			D_GOTO(out_prop, rc);
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
			D_GOTO(out_prop, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_RECLAIM;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_EC_CELL_SZ) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_ec_cell_sz,
				   &value);
		if (rc != 0)
			D_GOTO(out_prop, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_EC_CELL_SZ;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_REDUN_FAC) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_redun_fac,
				   &value);
		/**
		 * For upgrading, redunc fac might not exist, use
		 * default(0) for this case.
		 */
		if (rc == -DER_NONEXIST && global_ver < 1) {
			rc = 0;
			val = DAOS_PROP_PO_REDUN_FAC_DEFAULT;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_REDUN_FAC;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_ACL) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_acl,
				   &value);
		if (rc != 0)
			D_GOTO(out_prop, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_ACL;
		D_ALLOC(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf_len);
		if (prop->dpp_entries[idx].dpe_val_ptr == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		memcpy(prop->dpp_entries[idx].dpe_val_ptr, value.iov_buf,
		       value.iov_buf_len);
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_OWNER) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_owner,
				   &value);
		if (rc != 0)
			D_GOTO(out_prop, rc);
		if (value.iov_len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
			D_ERROR("bad owner length %zu (> %d).\n", value.iov_len,
				DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_GOTO(out_prop, rc = -DER_IO);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OWNER;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_OWNER_GROUP) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_owner_group,
				   &value);
		if (rc != 0)
			D_GOTO(out_prop, rc);
		if (value.iov_len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
			D_ERROR("bad owner group length %zu (> %d).\n",
				value.iov_len,
				DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_GOTO(out_prop, rc = -DER_IO);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OWNER_GROUP;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SVC_LIST) {
		d_rank_list_t	*svc_list = NULL;

		d_iov_set(&value, NULL, 0);
		rc = rdb_get_ranks(svc->ps_rsvc.s_db, &svc_list);
		if (rc) {
			D_ERROR("get svc list failed: rc " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_prop, rc);
		}
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SVC_LIST;
		prop->dpp_entries[idx].dpe_val_ptr = svc_list;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_EC_PDA) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_ec_pda,
				   &value);
		D_ASSERT(idx < nr);
		if (rc == -DER_NONEXIST && global_ver < 1)
			val = DAOS_PROP_PO_EC_PDA_DEFAULT;
		else  if (rc != 0)
			D_GOTO(out_prop, rc);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_EC_PDA;
		prop->dpp_entries[idx].dpe_val = val;
		if (rc == -DER_NONEXIST) {
			rc = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		}
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_RP_PDA) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_rp_pda,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 1)
			val = DAOS_PROP_PO_RP_PDA_DEFAULT;
		else  if (rc != 0)
			D_GOTO(out_prop, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_RP_PDA;
		prop->dpp_entries[idx].dpe_val = val;
		if (rc == -DER_NONEXIST) {
			rc = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		}
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_POLICY) {
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_policy,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 1) {
			value.iov_buf = DAOS_PROP_POLICYSTR_DEFAULT;
			value.iov_len = strlen(DAOS_PROP_POLICYSTR_DEFAULT);
		} else  if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		if (value.iov_len > DAOS_PROP_POLICYSTR_MAX_LEN) {
			D_ERROR("bad policy string length %zu (> %d).\n",
				value.iov_len, DAOS_PROP_POLICYSTR_MAX_LEN);
			D_GOTO(out_prop, rc = -DER_IO);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_POLICY;
		D_STRNDUP(prop->dpp_entries[idx].dpe_str, value.iov_buf,
			  value.iov_len);
		if (prop->dpp_entries[idx].dpe_str == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		}
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_GLOBAL_VERSION) {
		D_ASSERT(idx < nr);
		if (global_ver < 1)
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_GLOBAL_VERSION;
		prop->dpp_entries[idx].dpe_val = global_ver;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_OBJ_VERSION) {
		uint32_t obj_ver;

		D_ASSERT(idx < nr);
		/* get pool global version */
		d_iov_set(&value, &obj_ver, sizeof(obj_ver));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_obj_version,
				   &value);
		if (rc == -DER_NONEXIST && global_ver <= 1) {
			obj_ver = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}

		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OBJ_VERSION;
		prop->dpp_entries[idx].dpe_val = obj_ver;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_UPGRADE_STATUS) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_upgrade_status,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 1)
			val32 = DAOS_UPGRADE_STATUS_NOT_STARTED;
		else  if (rc != 0)
			D_GOTO(out_prop, rc);

		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_UPGRADE_STATUS;
		prop->dpp_entries[idx].dpe_val = val32;
		if (rc == -DER_NONEXIST) {
			rc = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		}
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_PERF_DOMAIN) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_perf_domain,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 2)
			val32 = DAOS_PROP_PO_PERF_DOMAIN_DEFAULT;
		else if (rc != 0)
			D_GOTO(out_prop, rc);

		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_PERF_DOMAIN;
		prop->dpp_entries[idx].dpe_val = val32;
		if (rc == -DER_NONEXIST) {
			rc = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		}
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_SCRUB_MODE) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_scrub_mode,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 2) { /* needs to be upgraded */
			rc = 0;
			val = DAOS_PROP_PO_SCRUB_MODE_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SCRUB_MODE;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SCRUB_FREQ) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_scrub_freq,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 2) { /* needs to be upgraded */
			rc = 0;
			val = DAOS_PROP_PO_SCRUB_FREQ_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SCRUB_FREQ;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_SCRUB_THRESH) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_scrub_thresh,
				   &value);
		if (rc == -DER_NONEXIST && global_ver < 2) { /* needs to be upgraded */
			rc = 0;
			val = DAOS_PROP_PO_SCRUB_THRESH_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SCRUB_THRESH;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_SVC_REDUN_FAC) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_redun_fac, &value);
		if (rc == -DER_NONEXIST && global_ver < 2) {
			rc = 0;
			val = DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SVC_REDUN_FAC;
		prop->dpp_entries[idx].dpe_val = val;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_CHECKPOINT_MODE) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_checkpoint_mode, &value);
		if (rc == -DER_NONEXIST && global_ver < 2) { /* needs to be upgraded */
			rc  = 0;
			val32 = DAOS_PROP_PO_CHECKPOINT_MODE_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_CHECKPOINT_MODE;
		prop->dpp_entries[idx].dpe_val  = val32;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_CHECKPOINT_FREQ) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_checkpoint_freq, &value);
		if (rc == -DER_NONEXIST && global_ver < 2) { /* needs to be upgraded */
			rc  = 0;
			val32 = DAOS_PROP_PO_CHECKPOINT_FREQ_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_CHECKPOINT_FREQ;
		prop->dpp_entries[idx].dpe_val  = val32;
		idx++;
	}
	if (bits & DAOS_PO_QUERY_PROP_CHECKPOINT_THRESH) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_checkpoint_thresh, &value);
		if (rc == -DER_NONEXIST && global_ver < 2) { /* needs to be upgraded */
			rc  = 0;
			val32 = DAOS_PROP_PO_CHECKPOINT_THRESH_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_CHECKPOINT_THRESH;
		prop->dpp_entries[idx].dpe_val  = val32;
		idx++;
	}

	*prop_out = prop;
	return 0;

out_prop:
	daos_prop_free(prop);
	return rc;
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
	d_iov_t			value;
	struct rdb_kvs_attr	attr;
	daos_prop_t	       *prop_dup = NULL;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pri_op.pi_uuid), rpc);

	if (in->pri_ntgts != in->pri_tgt_ranks->rl_nr)
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
			D_DEBUG(DB_MD, DF_UUID": db already initialized\n",
				DP_UUID(svc->ps_uuid));
		else
			D_ERROR(DF_UUID": failed to look up pool map: "
				DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_tx, rc);
	}

	/* duplicate the default properties, overwrite it with pool create
	 * parameter and then write to pool meta data.
	 */
	prop_dup = daos_prop_dup(&pool_prop_default, true /* pool */,
				 false /* input */);
	if (prop_dup == NULL) {
		D_ERROR("daos_prop_dup failed.\n");
		D_GOTO(out_tx, rc = -DER_NOMEM);
	}

	if (DAOS_FAIL_CHECK(DAOS_FAIL_POOL_CREATE_VERSION)) {
		uint64_t fail_val = daos_fail_value_get();
		struct daos_prop_entry *entry;

		entry = daos_prop_entry_get(prop_dup, DAOS_PROP_PO_OBJ_VERSION);
		D_ASSERT(entry != NULL);
		entry->dpe_val = (uint32_t)fail_val;
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
	rc = init_pool_metadata(&tx, &svc->ps_root, in->pri_ntgts, NULL /* group */,
				in->pri_tgt_ranks, prop_dup, in->pri_ndomains,
				in->pri_domains.ca_arrays);
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
		D_GOTO(out_mutex, rc);

	if (ds_rsvc_get_state(&svc->ps_rsvc) == DS_RSVC_UP_EMPTY) {
		/*
		 * The DB is no longer empty. Since the previous
		 * pool_svc_step_up_cb() call didn't finish stepping up due to
		 * an empty DB, and there hasn't been a pool_svc_step_down_cb()
		 * call yet, we should call pool_svc_step_up() to finish
		 * stepping up.
		 */
		D_DEBUG(DB_MD, DF_UUID": trying to finish stepping up\n",
			DP_UUID(in->pri_op.pi_uuid));
		rc = pool_svc_step_up_cb(&svc->ps_rsvc);
		if (rc != 0) {
			D_ASSERT(rc != DER_UNINIT);
			rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
			D_GOTO(out_mutex, rc);
		}
		ds_rsvc_set_state(&svc->ps_rsvc, DS_RSVC_UP);
	}

out_mutex:
	ABT_mutex_unlock(svc->ps_rsvc.s_mutex);
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pro_op.po_hint);
	pool_svc_put(svc);
out:
	out->pro_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pri_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

static int
pool_connect_iv_dist(struct pool_svc *svc, uuid_t pool_hdl,
		     uint64_t flags, uint64_t sec_capas, d_iov_t *cred,
		     uint32_t global_ver, uint32_t layout_ver)
{
	d_rank_t rank;
	int	 rc;

	D_DEBUG(DB_MD, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	rc = crt_group_rank(svc->ps_pool->sp_group, &rank);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_pool_iv_conn_hdl_update(svc->ps_pool, pool_hdl, flags,
					sec_capas, cred, global_ver, layout_ver);
	if (rc) {
		if (rc == -DER_SHUTDOWN) {
			D_DEBUG(DB_MD, DF_UUID":"DF_UUID" some ranks stop.\n",
				DP_UUID(svc->ps_uuid), DP_UUID(pool_hdl));
			rc = 0;
		}
		D_GOTO(out, rc);
	}
out:
	D_DEBUG(DB_MD, DF_UUID": bcasted: "DF_RC"\n", DP_UUID(svc->ps_uuid),
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

/* Currently we only maintain compatibility between 2 versions */
#define NUM_POOL_VERSIONS	2

static void
ds_pool_connect_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_connect_v4_in      *in = crt_req_get(rpc);
	struct pool_connect_v5_out     *out = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	struct pool_buf		       *map_buf = NULL;
	uint32_t			map_version;
	uint32_t			connectable;
	uint32_t			global_ver;
	uint32_t			obj_layout_ver;
	struct rdb_tx			tx;
	d_iov_t				key;
	d_iov_t				value;
	struct pool_hdl		       *hdl = NULL;
	uint32_t			nhandles;
	int				skip_update = 0;
	int				rc;
	daos_prop_t		       *prop = NULL;
	uint64_t			prop_bits;
	struct daos_prop_entry	       *acl_entry;
	struct d_ownership		owner;
	struct daos_prop_entry	       *owner_entry, *global_ver_entry;
	struct daos_prop_entry	       *owner_grp_entry;
	struct daos_prop_entry	       *obj_ver_entry;
	uint64_t			sec_capas = 0;
	struct pool_metrics	       *metrics;
	char			       *machine = NULL;
	bool				transfer_map = false;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
	D_DEBUG(DB_MD, DF_UUID": connectable=%u\n",
		DP_UUID(in->pci_op.pi_uuid), connectable);
	if (!connectable) {
		D_ERROR(DF_UUID": being destroyed, not accepting connections\n",
			DP_UUID(in->pci_op.pi_uuid));
		D_GOTO(out_lock, rc = -DER_BUSY);
	}

	/* Check existing pool handles. */
	d_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
	if (rc == 0) {
		/* found it */
		if (((struct pool_hdl *)value.iov_buf)->ph_flags == in->pci_flags) {
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

	global_ver_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_GLOBAL_VERSION);
	D_ASSERT(global_ver_entry != NULL);
	global_ver = global_ver_entry->dpe_val;
	/*
	 * Reject pool connection if old clients try to connect new format pool.
	 */
	if (handler_version >= 5) {
		struct pool_connect_v5_in *in1 = (struct pool_connect_v5_in *)in;
		int diff = DAOS_POOL_GLOBAL_VERSION - in1->pci_pool_version;

		if (in1->pci_pool_version <= DAOS_POOL_GLOBAL_VERSION) {
			if (diff >= NUM_POOL_VERSIONS) {
				D_ERROR(DF_UUID": cannot connect, client supported pool "
					"layout version (%u) is more than %u versions smaller "
					"than server supported pool layout version(%u), "
					"try to upgrade client firstly.\n",
					DP_UUID(in->pci_op.pi_uuid), in1->pci_pool_version,
					NUM_POOL_VERSIONS - 1, DAOS_POOL_GLOBAL_VERSION);
				D_GOTO(out_map_version, rc = -DER_NOTSUPPORTED);
			}

			if (global_ver > in1->pci_pool_version) {
				D_ERROR(DF_UUID": cannot connect, pool layout version(%u) > "
					"max client supported pool layout version(%u), "
					"try to upgrade client firstly.\n",
					DP_UUID(in->pci_op.pi_uuid), global_ver,
					in1->pci_pool_version);
				D_GOTO(out_map_version, rc = -DER_NOTSUPPORTED);
			}
		} else {
			diff = -diff;
			if (diff >= NUM_POOL_VERSIONS) {
				D_ERROR(DF_UUID": cannot connect, client supported pool "
					"layout version (%u) is more than %u versions "
					"larger than server supported pool layout version(%u), "
					"try to upgrade server firstly.\n",
					DP_UUID(in->pci_op.pi_uuid), in1->pci_pool_version,
					NUM_POOL_VERSIONS - 1, DAOS_POOL_GLOBAL_VERSION);
				D_GOTO(out_map_version, rc = -DER_NOTSUPPORTED);
			}
			/* New clients should be able to access old pools without problem */
		}
	} else {
		/* Assuming 2.0 client */
		if (global_ver > 1) {
			D_ERROR(DF_UUID": cannot connect, 2.0 clients unable to access pool format "
				"version > 1, try to upgrade client firstly.\n",
				DP_UUID(in->pci_op.pi_uuid));
			D_GOTO(out_map_version, rc = -DER_NOTSUPPORTED);
		}
	}

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

	obj_ver_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OBJ_VERSION);
	D_ASSERT(obj_ver_entry != NULL);
	obj_layout_ver = obj_ver_entry->dpe_val;

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

	rc = ds_sec_cred_get_origin(&in->pci_cred, &machine);

	if (rc != 0) {
		D_ERROR(DF_UUID": unable to retrieve origin error: "DF_RC"\n",
			DP_UUID(in->pci_op.pi_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}

	if (!ds_sec_pool_can_connect(sec_capas)) {
		D_ERROR(DF_UUID": permission denied for connect attempt for "
			DF_X64"\n", DP_UUID(in->pci_op.pi_uuid),
			in->pci_flags);
		D_GOTO(out_map_version, rc = -DER_NO_PERM);
	}

	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool map: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}
	transfer_map = true;
	if (skip_update)
		D_GOTO(out_map_version, rc = 0);

	d_iov_set(&value, &nhandles, sizeof(nhandles));
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_nhandles, &value);
	if (rc != 0)
		D_GOTO(out_map_version, rc);

	/* Take care of exclusive handles. */
	if (nhandles != 0) {
		if (in->pci_flags & DAOS_PC_EX) {
			D_DEBUG(DB_MD, DF_UUID": others already connected\n",
				DP_UUID(in->pci_op.pi_uuid));
			D_GOTO(out_map_version, rc = -DER_BUSY);
		} else {
			/*
			 * If there is a non-exclusive handle, then all handles
			 * are non-exclusive.
			 */
			d_iov_set(&value, NULL, 0);
			rc = rdb_tx_fetch(&tx, &svc->ps_handles,
					  RDB_PROBE_FIRST, NULL /* key_in */,
					  NULL /* key_out */, &value);
			if (rc != 0)
				D_GOTO(out_map_version, rc);
			if (((struct pool_hdl *)value.iov_buf)->ph_flags & DAOS_PC_EX)
				D_GOTO(out_map_version, rc = -DER_BUSY);
		}
	}

	rc = pool_connect_iv_dist(svc, in->pci_op.pi_hdl, in->pci_flags,
				  sec_capas, &in->pci_cred, global_ver, obj_layout_ver);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_CONNECT_FAIL_CORPC)) {
		D_DEBUG(DB_MD, DF_UUID": fault injected: DAOS_POOL_CONNECT_FAIL_CORPC\n",
			DP_UUID(in->pci_op.pi_uuid));
		rc = -DER_TIMEDOUT;
	}
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to connect to targets: "DF_RC"\n",
			DP_UUID(in->pci_op.pi_uuid), DP_RC(rc));
		D_GOTO(out_map_version, rc);
	}

	/* handle did not exist so create it */
	/* XXX may be can check pool version to avoid allocating too much ? */
	D_ALLOC(hdl, sizeof(*hdl) + in->pci_cred.iov_len);
	if (hdl == NULL)
		D_GOTO(out_map_version, rc = -DER_NOMEM);

	hdl->ph_flags = in->pci_flags;
	hdl->ph_sec_capas = sec_capas;
	/* XXX may be can check pool version to avoid initializing 3 following hdl fields ? */
	strncpy(hdl->ph_machine, machine, MAXHOSTNAMELEN);
	hdl->ph_cred_len = in->pci_cred.iov_len;
	memcpy(&hdl->ph_cred[0], in->pci_cred.iov_buf, in->pci_cred.iov_len);

	nhandles++;
	d_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, hdl,
		  svc->ps_global_version >= DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED ?
		  sizeof(struct pool_hdl) + hdl->ph_cred_len : sizeof(struct pool_hdl_v0));
	D_DEBUG(DB_MD, "writing a pool connect handle in db, size %zu, pool version %u\n",
		value.iov_len, svc->ps_global_version);
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

	/** update metric */
	metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
	d_tm_inc_counter(metrics->connect_total, 1);

	if (in->pci_query_bits & DAOS_PO_QUERY_SPACE)
		rc = pool_space_query_bcast(rpc->cr_ctx, svc, in->pci_op.pi_hdl,
					    &out->pco_space);
out_map_version:
	out->pco_op.po_map_version = ds_pool_get_version(svc->ps_pool);
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc == 0 && transfer_map) {
		rc = ds_pool_transfer_map_buf(map_buf, map_version, rpc, in->pci_map_bulk,
					      &out->pco_map_buf_size);
		/** TODO: roll back tx if transfer fails? */
	}
	D_FREE(map_buf);
	D_FREE(hdl);
	D_FREE(machine);
	if (prop)
		daos_prop_free(prop);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pco_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pco_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pci_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_connect_handler_v4(crt_rpc_t *rpc)
{
	ds_pool_connect_handler(rpc, 4);
}

void
ds_pool_connect_handler_v5(crt_rpc_t *rpc)
{
	ds_pool_connect_handler(rpc, 5);
}

static int
pool_disconnect_bcast(crt_context_t ctx, struct pool_svc *svc,
		      uuid_t *pool_hdls, int n_pool_hdls)
{
	struct pool_tgt_disconnect_in  *in;
	struct pool_tgt_disconnect_out *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DB_MD, DF_UUID": bcasting\n", DP_UUID(svc->ps_uuid));

	rc = bcast_create(ctx, svc, POOL_TGT_DISCONNECT, NULL, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tdi_uuid, svc->ps_uuid);
	in->tdi_hdls.ca_arrays = pool_hdls;
	in->tdi_hdls.ca_count = n_pool_hdls;
	rc = dss_rpc_send(rpc);
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_DISCONNECT_FAIL_CORPC)) {
		D_DEBUG(DB_MD, DF_UUID": fault injected: DAOS_POOL_DISCONNECT_FAIL_CORPC\n",
			DP_UUID(svc->ps_uuid));
		rc = -DER_TIMEDOUT;
	}
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to disconnect from targets: " DF_RC "\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DB_MD, DF_UUID": bcasted: "DF_RC"\n", DP_UUID(svc->ps_uuid),
		DP_RC(rc));
	return rc;
}

static int
pool_disconnect_hdls(struct rdb_tx *tx, struct pool_svc *svc, uuid_t *hdl_uuids,
		     int n_hdl_uuids, crt_context_t ctx)
{
	d_iov_t			 value;
	uint32_t		 nhandles;
	int			 i;
	int			 rc;

	D_ASSERTF(n_hdl_uuids > 0, "%d\n", n_hdl_uuids);

	D_DEBUG(DB_MD, DF_UUID": disconnecting %d hdls: hdl_uuids[0]="DF_UUID
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
	if (rc == 0)
		D_INFO(DF_UUID": success\n", DP_UUID(svc->ps_uuid));
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
	int				rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
	d_iov_set(&value, NULL, 0);
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

	if (rc == 0) {
		struct pool_metrics *metrics;

		/** update metric */
		metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
		d_tm_inc_counter(metrics->disconnect_total, 1);
	}

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &pdo->pdo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	pdo->pdo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(pdi->pdi_op.pi_uuid), rpc,
		DP_RC(rc));
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
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_POOL_QUERY_FAIL_CORPC)) {
		D_DEBUG(DB_MD, DF_UUID": fault injected: DAOS_POOL_QUERY_FAIL_CORPC\n",
			DP_UUID(svc->ps_uuid));
		rc = -DER_TIMEDOUT;
	}
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	rc = out->tqo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to query from targets: " DF_RC "\n",
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
transfer_cont_buf(void *cont_buf, size_t cont_buf_size, struct pool_svc *svc,
		  crt_rpc_t *rpc, crt_bulk_t remote_bulk)
{
	daos_size_t			 remote_bulk_size;
	d_iov_t				 cont_iov;
	d_sg_list_t			 cont_sgl;
	crt_bulk_t			 bulk = CRT_BULK_NULL;
	struct crt_bulk_desc		 bulk_desc;
	crt_bulk_opid_t			 bulk_opid;
	ABT_eventual			 eventual;
	int				*status;
	int				 rc;

	D_ASSERT(cont_buf_size > 0);

	/* Check if the client bulk buffer is large enough. */
	rc = crt_bulk_get_len(remote_bulk, &remote_bulk_size);
	if (rc != 0)
		D_GOTO(out, rc);
	if (remote_bulk_size < cont_buf_size) {
		D_ERROR(DF_UUID": remote container buffer("DF_U64") < required (%zu)\n",
			DP_UUID(svc->ps_uuid), remote_bulk_size, cont_buf_size);
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
				   resp_cont, in->plci_ncont * sizeof(struct daos_pool_cont_info));
	if (rc != 0)
		D_GOTO(out_resp_buf, rc);

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->plco_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		/* To simplify logic, destroy bulk hdl and buffer each time */
		list_cont_bulk_destroy(in->plci_cont_bulk);
		D_FREE(resp_cont);
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
		D_GOTO(rechoose, rc);
	}

	rc = out->plco_op.po_rc;
	if (rc == -DER_TRUNC) {
		/* resp_ncont too small - realloc with server-provided ncont */
		resp_ncont = out->plco_ncont;
		list_cont_bulk_destroy(in->plci_cont_bulk);
		D_FREE(resp_cont);
		crt_req_decref(rpc);
		D_GOTO(realloc_resp, rc);
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to get container list for pool: %d\n",
			DP_UUID(uuid), rc);
	} else {
		*ncontainers = out->plco_ncont;
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

static bool is_pool_from_srv(uuid_t pool_uuid, uuid_t poh_uuid);

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
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
			d_iov_set(&value, NULL, 0);
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
		D_DEBUG(DB_MD, DF_UUID": hdl="DF_UUID": has %"PRIu64
				 " containers (more than client: %"PRIu64")\n",
				 DP_UUID(in->plci_op.pi_uuid),
				 DP_UUID(in->plci_op.pi_hdl),
				 ncont, in->plci_ncont);
		D_GOTO(out_free_cont_buf, rc = -DER_TRUNC);
	} else {
		size_t nbytes = ncont * sizeof(struct daos_pool_cont_info);

		D_DEBUG(DB_MD, DF_UUID": hdl="DF_UUID": has %"PRIu64 "containers\n",
			DP_UUID(in->plci_op.pi_uuid), DP_UUID(in->plci_op.pi_hdl), ncont);

		/* Send any results only if client provided a handle */
		if (cont_buf && (in->plci_ncont > 0) && (in->plci_cont_bulk != CRT_BULK_NULL))
			rc = transfer_cont_buf(cont_buf, nbytes, svc, rpc, in->plci_cont_bulk);
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->plci_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

/* TODO: consider moving to common function for client and engine use */
static bool
pool_cont_filter_is_valid(uuid_t pool_uuid, daos_pool_cont_filter_t *filt)
{
	uint32_t	i;

	/* TODO: decide if filt == NULL is ok especially on client side */
	D_ASSERT(filt != NULL);

	D_DEBUG(DB_MD, DF_UUID": filter with %u parts, combine with logical %s\n",
		DP_UUID(pool_uuid), filt->pcf_nparts, (filt->pcf_combine_func == 0) ? "AND" : "OR");
	if ((filt->pcf_nparts > 0) && (filt->pcf_parts == NULL)) {
		D_ERROR(DF_UUID": filter has %u parts but pcf_parts is NULL\n", DP_UUID(pool_uuid),
			filt->pcf_nparts);
		return false;
	}
	for (i = 0; i < filt->pcf_nparts; i++) {
		daos_pool_cont_filter_part_t *part = filt->pcf_parts[i];

		if (part->pcfp_key >= PCF_KEY_MAX) {
			D_ERROR(DF_UUID": filter part key %u is outside of valid range %u..%u\n",
				DP_UUID(pool_uuid), part->pcfp_key, 0, (PCF_KEY_MAX - 1));
			return false;
		}
		if (part->pcfp_func >= PCF_FUNC_MAX) {
			D_ERROR(DF_UUID": filter part func %u is outside of valid range %u..%u\n",
				DP_UUID(pool_uuid), part->pcfp_key, 0, (PCF_FUNC_MAX - 1));
			return false;
		}
		D_DEBUG(DB_MD, DF_UUID": filter part %u: key(%s) %s "DF_U64"\n",
			DP_UUID(pool_uuid), i,
			daos_pool_cont_filter_key_str(part->pcfp_key),
			daos_pool_cont_filter_func_str(part->pcfp_func),
			part->pcfp_val64);
	}

	return true;
}

/* CaRT RPC handler for pool container filtering
 * Requires a pool handle.
 */
void
ds_pool_filter_cont_handler(crt_rpc_t *rpc)
{
	struct pool_filter_cont_in	*in = crt_req_get(rpc);
	struct pool_filter_cont_out	*out = crt_reply_get(rpc);
	struct daos_pool_cont_info2	*cont_buf = NULL;
	uint64_t			 ncont = 0;
	struct pool_svc			*svc;
	struct rdb_tx			 tx;
	d_iov_t				 key;
	d_iov_t				 value;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pfci_op.pi_uuid), rpc, DP_UUID(in->pfci_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pfci_op.pi_uuid, &svc, &out->pfco_op.po_hint);
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
		if (!is_pool_from_srv(in->pfci_op.pi_uuid,
				      in->pfci_op.pi_hdl)) {
			d_iov_set(&key, in->pfci_op.pi_hdl, sizeof(uuid_t));
			d_iov_set(&value, NULL, 0);
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

	/* Debug log and check filter specification */
	if (!pool_cont_filter_is_valid(in->pfci_op.pi_uuid, &in->pfci_filt)) {
		D_ERROR(DF_UUID": filter input failed\n", DP_UUID(in->pfci_op.pi_uuid));
		D_GOTO(out_svc, rc = -DER_INVAL);
	}

	/* Call container service to get the filtered list of containers */
	rc = ds_cont_filter(in->pfci_op.pi_uuid, &in->pfci_filt, &cont_buf, &ncont);
	if (rc != 0) {
		D_GOTO(out_svc, rc);
	} else if ((in->pfci_ncont > 0) && (ncont > in->pfci_ncont)) {
		/* Got a list, but client buffer not supplied or too small */
		D_DEBUG(DB_MD, DF_UUID": hdl="DF_UUID": %"PRIu64" matching containers "
			"(more than client: %"PRIu64")\n",DP_UUID(in->pfci_op.pi_uuid),
			DP_UUID(in->pfci_op.pi_hdl), ncont, in->pfci_ncont);
		D_GOTO(out_free_cont_buf, rc = -DER_TRUNC);
	} else {
		size_t nbytes = ncont * sizeof(struct daos_pool_cont_info2);

		D_DEBUG(DB_MD, DF_UUID": hdl="DF_UUID": %"PRIu64" matching containers\n",
			DP_UUID(in->pfci_op.pi_uuid), DP_UUID(in->pfci_op.pi_hdl), ncont);

		/* Send any results only if client provided a handle */
		if (cont_buf && (in->pfci_ncont > 0) && (in->pfci_cont_bulk != CRT_BULK_NULL))
			rc = transfer_cont_buf(cont_buf, nbytes, svc, rpc, in->pfci_cont_bulk);
	}
out_free_cont_buf:
	if (cont_buf) {
		D_FREE(cont_buf);
		cont_buf = NULL;
	}
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pfco_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pfco_op.po_rc = rc;
	out->pfco_ncont = ncont;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->pfci_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

static void
ds_pool_query_handler(crt_rpc_t *rpc, int version)
{
	struct pool_query_v5_in	 *in = crt_req_get(rpc);
	struct pool_query_v5_out *out = crt_reply_get(rpc);
	daos_prop_t		 *prop = NULL;
	struct pool_buf		 *map_buf;
	uint32_t		  map_version = 0;
	struct pool_svc		 *svc;
	struct pool_metrics	 *metrics;
	struct rdb_tx		  tx;
	d_iov_t			  key;
	d_iov_t			  value;
	int			  rc;
	struct daos_prop_entry	 *entry;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
		if (rc != 0) {
			if (rc == -DER_NONEXIST)
				rc = -DER_NO_HDL;
			D_GOTO(out_lock, rc);
		}
	}

	if (version >= 5) {
		rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_GLOBAL_VERSION, &prop);
		if (rc != 0)
			D_GOTO(out_lock, rc);

		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_GLOBAL_VERSION);
		D_ASSERT(entry != NULL);
		out->pqo_pool_layout_ver = entry->dpe_val;
		out->pqo_upgrade_layout_ver = DAOS_POOL_GLOBAL_VERSION;
		daos_prop_free(prop);
		prop = NULL;
	}

	/* read optional properties */
	rc = pool_prop_read(&tx, svc, in->pqi_query_bits, &prop);
	if (rc != 0)
		D_GOTO(out_lock, rc);
	out->pqo_prop = prop;

	if (unlikely(DAOS_FAIL_CHECK(DAOS_FORCE_PROP_VERIFY) && prop != NULL)) {
		daos_prop_t		*iv_prop = NULL;
		struct daos_prop_entry	*iv_entry;
		int			i;

		D_ALLOC_PTR(iv_prop);
		if (iv_prop == NULL)
			D_GOTO(out_lock, rc = -DER_NOMEM);

		rc = ds_pool_iv_prop_fetch(svc->ps_pool, iv_prop);
		if (rc) {
			D_ERROR("ds_pool_iv_prop_fetch failed "DF_RC"\n",
				DP_RC(rc));
			daos_prop_free(iv_prop);
			D_GOTO(out_lock, rc);
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
			case DAOS_PROP_PO_EC_CELL_SZ:
			case DAOS_PROP_PO_REDUN_FAC:
			case DAOS_PROP_PO_EC_PDA:
			case DAOS_PROP_PO_RP_PDA:
			case DAOS_PROP_PO_GLOBAL_VERSION:
			case DAOS_PROP_PO_UPGRADE_STATUS:
			case DAOS_PROP_PO_SCRUB_MODE:
			case DAOS_PROP_PO_SCRUB_FREQ:
			case DAOS_PROP_PO_SCRUB_THRESH:
			case DAOS_PROP_PO_SVC_REDUN_FAC:
			case DAOS_PROP_PO_OBJ_VERSION:
			case DAOS_PROP_PO_PERF_DOMAIN:
				if (entry->dpe_val != iv_entry->dpe_val) {
					D_ERROR("type %d mismatch "DF_U64" - "
						DF_U64".\n", entry->dpe_type,
						entry->dpe_val,
						iv_entry->dpe_val);
					rc = -DER_IO;
					}
				break;
			case DAOS_PROP_PO_POLICY:
				D_ASSERT(strnlen(entry->dpe_str,
						 DAOS_PROP_POLICYSTR_MAX_LEN) <=
					 DAOS_PROP_POLICYSTR_MAX_LEN);
				if (strncmp(entry->dpe_str, iv_entry->dpe_str,
					    DAOS_PROP_POLICYSTR_MAX_LEN) != 0) {
					D_ERROR("mismatch %s - %s.\n",
						entry->dpe_str,
						iv_entry->dpe_str);
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
			D_GOTO(out_lock, rc);
		}
	}

	rc = read_map_buf(&tx, &svc->ps_root, &map_buf, &map_version);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to read pool map: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out_svc;

	rc = ds_pool_transfer_map_buf(map_buf, map_version, rpc,
				      in->pqi_map_bulk, &out->pqo_map_buf_size);
	D_FREE(map_buf);
	if (rc != 0)
		goto out_svc;

	metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];

	/* See comment above, rebuild doesn't connect the pool */
	if ((in->pqi_query_bits & DAOS_PO_QUERY_SPACE) &&
	    !is_pool_from_srv(in->pqi_op.pi_uuid, in->pqi_op.pi_hdl)) {
		rc = pool_space_query_bcast(rpc->cr_ctx, svc, in->pqi_op.pi_hdl,
					    &out->pqo_space);
		if (unlikely(rc))
			goto out_svc;

		d_tm_inc_counter(metrics->query_space_total, 1);
	}
	d_tm_inc_counter(metrics->query_total, 1);

out_svc:
	if (map_version == 0)
		out->pqo_op.po_map_version = ds_pool_get_version(svc->ps_pool);
	else
		out->pqo_op.po_map_version = map_version;
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pqo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pqo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pqi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
	if (prop)
		daos_prop_free(prop);
}

void
ds_pool_query_handler_v4(crt_rpc_t *rpc)
{
	ds_pool_query_handler(rpc, 4);
}

void
ds_pool_query_handler_v5(crt_rpc_t *rpc)
{
	ds_pool_query_handler(rpc, 5);
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

static int
pool_query_tgt_space(crt_context_t ctx, struct pool_svc *svc, uuid_t pool_hdl,
		     d_rank_t rank, uint32_t tgt_idx, struct daos_space *ds)
{
	struct pool_tgt_query_in	*in;
	struct pool_tgt_query_out	*out;
	crt_rpc_t			*rpc;
	crt_endpoint_t			 tgt_ep = { 0 };
	crt_opcode_t			 opcode;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID": query target for rank:%u tgt:%u\n",
		DP_UUID(svc->ps_uuid), rank, tgt_idx);

	tgt_ep.ep_rank = rank;
	tgt_ep.ep_tag = daos_rpc_tag(DAOS_REQ_TGT, tgt_idx);
	opcode = DAOS_RPC_OPCODE(POOL_TGT_QUERY, DAOS_POOL_MODULE,
				 DAOS_POOL_VERSION);
	rc = crt_req_create(ctx, &tgt_ep, opcode, &rpc);
	if (rc) {
		D_ERROR("crt_req_create failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	in = crt_req_get(rpc);
	uuid_copy(in->tqi_op.pi_uuid, svc->ps_uuid);
	uuid_copy(in->tqi_op.pi_hdl, pool_hdl);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	rc = out->tqo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to query rank:%u, tgt:%u, "DF_RC"\n",
			DP_UUID(svc->ps_uuid), rank, tgt_idx, DP_RC(rc));
	} else {
		D_ASSERT(ds != NULL);
		*ds = out->tqo_space.ps_space;
	}

out_rpc:
	crt_req_decref(rpc);
	return rc;
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

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
		ABT_rwlock_unlock(svc->ps_pool->sp_lock);
		D_GOTO(out_svc, rc = -DER_NONEXIST);
	} else {
		rc = 0;
	}

	D_ASSERT(target != NULL);

	tgt_state = target->ta_comp.co_status;
	out->pqio_state = enum_pool_comp_state_to_tgt_state(tgt_state);
	out->pqio_op.po_map_version =
			pool_map_get_version(svc->ps_pool->sp_map);

	ABT_rwlock_unlock(svc->ps_pool->sp_lock);

	if (tgt_state == PO_COMP_ST_UPIN) {
		rc = pool_query_tgt_space(rpc->cr_ctx, svc, in->pqii_op.pi_hdl,
					  in->pqii_rank, in->pqii_tgt,
					  &out->pqio_space);
		if (rc)
			D_ERROR(DF_UUID": Failed to query rank:%u, tgt:%d, "
				""DF_RC"\n", DP_UUID(in->pqii_op.pi_uuid),
				in->pqii_rank, in->pqii_tgt, DP_RC(rc));
	} else {
		memset(&out->pqio_space, 0, sizeof(out->pqio_space));
	}
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pqio_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pqio_op.po_rc = rc;
	out->pqio_rank = in->pqii_rank;
	out->pqio_tgt = in->pqii_tgt;

	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pqii_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

/**
 * Query pool target information without holding a pool handle.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ps_ranks	Ranks of pool svc replicas
 * \param[in]	rank		Pool storage engine rank
 * \param[in]	tgt_idx		Target index within the pool storage engine
 * \param[out]	ti		Target information (state, storage capacity and usage)
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		Negative value	Other error
 */
int
ds_pool_svc_query_target(uuid_t pool_uuid, d_rank_list_t *ps_ranks, d_rank_t rank,
			 uint32_t tgt_idx, daos_target_info_t *ti)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	int				i;
	struct pool_query_info_in	*in;
	struct pool_query_info_out	*out;

	if (ti == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DB_MGMT, DF_UUID": Querying pool target %u\n", DP_UUID(pool_uuid), tgt_idx);

	rc = rsvc_client_init(&client, ps_ranks);
	if (rc != 0)
		goto out;

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_QUERY_INFO, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool query target rpc, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pqii_op.pi_uuid, pool_uuid);
	uuid_clear(in->pqii_op.pi_hdl);
	in->pqii_rank = rank;
	in->pqii_tgt = tgt_idx;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->pqio_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
		goto rechoose;
	}

	rc = out->pqio_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to query pool rank %u target %u "DF_RC"\n",
			DP_UUID(pool_uuid), rank, tgt_idx, DP_RC(rc));
		goto out_rpc;
	}

	D_DEBUG(DB_MGMT, DF_UUID": Successfully queried pool rank %u target %u\n",
		DP_UUID(pool_uuid), rank, tgt_idx);

	ti->ta_type = DAOS_TP_UNKNOWN;
	ti->ta_state = out->pqio_state;
	for (i = 0; i < DAOS_MEDIA_MAX; i++) {
		ti->ta_space.s_total[i] = out->pqio_space.s_total[i];
		ti->ta_space.s_free[i] = out->pqio_space.s_free[i];
	}

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

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pgi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
	if (prop)
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

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->pgo_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
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
ds_pool_extend(uuid_t pool_uuid, int ntargets, const d_rank_list_t *rank_list, int ndomains,
	       const uint32_t *domains, d_rank_list_t *svc_ranks)
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
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

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
	in->pei_tgt_ranks = (d_rank_list_t *)rank_list;
	in->pei_domains.ca_count = ndomains;
	in->pei_domains.ca_arrays = (uint32_t *)domains;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->peo_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
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
			    struct pool_target_addr_list *target_addrs,
			    pool_comp_state_t state)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_add_in		*in;
	struct pool_add_out		*out;
	crt_opcode_t			opcode;

	rc = rsvc_client_init(&client, ranks);
	if (rc != 0)
		return rc;

rechoose:
	ep.ep_grp = NULL; /* primary group */
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out_client;
	}

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

	in->pti_addr_list.ca_arrays = target_addrs->pta_addrs;
	in->pti_addr_list.ca_count = (size_t)target_addrs->pta_number;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->pto_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
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

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->psi_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->psi_op.pi_uuid, &svc,
				    &out->pso_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	if (!daos_prop_valid(in->psi_prop, true /* pool */, true /* input */)) {
		D_ERROR(DF_UUID": invalid properties input\n",
			DP_UUID(in->psi_op.pi_uuid));
		D_GOTO(out_svc, rc = -DER_INVAL);
	}

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
		daos_prop_free(prop);
	}
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pso_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pso_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->psi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

static int
pool_upgrade_one_prop(struct rdb_tx *tx, struct pool_svc *svc, bool *need_commit, d_iov_t *prop_iov,
		      d_iov_t *value)
{
	int			rc;

	rc = rdb_tx_lookup(tx, &svc->ps_root, prop_iov, value);
	if (rc && rc != -DER_NONEXIST) {
		return rc;
	} else if (rc == -DER_NONEXIST) {
		rc = rdb_tx_update(tx, &svc->ps_root, prop_iov, value);
		if (rc)
			return rc;
		*need_commit = true;
	}
	return 0;
}

static int
pool_upgrade_one_prop_int64(struct rdb_tx *tx, struct pool_svc *svc, uuid_t uuid, bool *need_commit,
			    const char *friendly_name, d_iov_t *prop_iov, uint64_t default_value)
{
	d_iov_t  value;
	uint64_t val;
	int      rc;

	val = default_value;
	d_iov_set(&value, &val, sizeof(default_value));
	rc = pool_upgrade_one_prop(tx, svc, need_commit, prop_iov, &value);
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to upgrade '%s' of pool: %d.\n", DP_UUID(uuid),
			friendly_name, rc);
	}
	return rc;
}

static int
pool_upgrade_one_prop_int32(struct rdb_tx *tx, struct pool_svc *svc, uuid_t uuid, bool *need_commit,
			    const char *friendly_name, d_iov_t *prop_iov, uint32_t default_value)
{
	d_iov_t  value;
	uint32_t val;
	int      rc;

	val = default_value;
	d_iov_set(&value, &val, sizeof(default_value));
	rc = pool_upgrade_one_prop(tx, svc, need_commit, prop_iov, &value);
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to upgrade '%s' of pool: %d.\n", DP_UUID(uuid),
			friendly_name, rc);
	}
	return rc;
}

static int
pool_upgrade_props(struct rdb_tx *tx, struct pool_svc *svc,
			      uuid_t pool_uuid, crt_rpc_t *rpc)
{
	d_iov_t			value;
	uint64_t		val;
	uint32_t		val32;
	int			rc;
	bool			need_commit = false;
	uuid_t		       *hdl_uuids = NULL;
	size_t			hdl_uuids_size;
	int			n_hdl_uuids = 0;
	uint32_t		connectable;

	if (rpc) {
		rc = find_hdls_to_evict(tx, svc, &hdl_uuids, &hdl_uuids_size,
					&n_hdl_uuids, NULL);
		if (rc)
			return rc;
		D_DEBUG(DB_MD, "number of handles found was: %d\n", n_hdl_uuids);
	}

	if (n_hdl_uuids > 0) {
		rc = pool_disconnect_hdls(tx, svc, hdl_uuids, n_hdl_uuids,
					  rpc->cr_ctx);
		if (rc != 0)
			D_GOTO(out_free, rc);
		need_commit = true;
	}

	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_connectable,
			   &value);
	if (rc)
		D_GOTO(out_free, rc);

	/*
	 * Write connectable property to 0 to reject any new connections
	 * while upgrading in progress.
	 */
	if (connectable > 0) {
		connectable = 0;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_connectable,
				   &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to set connectable of pool "
				"%d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_policy,
			   &value);
	if (rc && rc != -DER_NONEXIST) {
		return rc;
	} else if (rc == -DER_NONEXIST) {
		value.iov_buf = DAOS_PROP_POLICYSTR_DEFAULT;
		value.iov_len = strlen(DAOS_PROP_POLICYSTR_DEFAULT);
		value.iov_buf_len = value.iov_len;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_policy,
				   &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade pool policy of pool "
				"%d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	d_iov_set(&value, &val, sizeof(val));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_redun_fac,
			   &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		val = DAOS_PROP_PO_REDUN_FAC_DEFAULT;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_redun_fac, &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade redundancy factor of pool, "
				"%d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_ec_pda, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		val = DAOS_PROP_PO_EC_PDA_DEFAULT;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_ec_pda, &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade EC performance domain "
				"affinity of pool, %d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_rp_pda, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		val = DAOS_PROP_PO_RP_PDA_DEFAULT;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_rp_pda, &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade RP performance domain "
				"affinity of pool, %d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_redun_fac, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		d_rank_list_t *replicas;

		rc = rdb_get_ranks(svc->ps_rsvc.s_db, &replicas);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to get service replica ranks: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
			D_GOTO(out_free, rc);
		}
		val = ds_pool_svc_rf_from_nreplicas(replicas->rl_nr);
		if (val < DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT)
			val = DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT;
		d_rank_list_free(replicas);
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_svc_redun_fac, &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade service redundancy factor "
				"of pool, %d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	/* Upgrade to have scrubbing properties */
	rc = pool_upgrade_one_prop_int64(tx, svc, pool_uuid, &need_commit, "scrub mode",
					 &ds_pool_prop_scrub_mode, DAOS_PROP_PO_SCRUB_MODE_DEFAULT);
	if (rc != 0)
		D_GOTO(out_free, rc);

	rc = pool_upgrade_one_prop_int64(tx, svc, pool_uuid, &need_commit, "scrub freq",
					 &ds_pool_prop_scrub_freq, DAOS_PROP_PO_SCRUB_FREQ_DEFAULT);
	if (rc != 0)
		D_GOTO(out_free, rc);

	rc = pool_upgrade_one_prop_int64(tx, svc, pool_uuid, &need_commit, "scrub thresh",
					 &ds_pool_prop_scrub_thresh,
					 DAOS_PROP_PO_SCRUB_THRESH_DEFAULT);
	if (rc != 0)
		D_GOTO(out_free, rc);

	/** WAL Checkpointing properties */
	rc = pool_upgrade_one_prop_int32(tx, svc, pool_uuid, &need_commit, "checkpoint mode",
					 &ds_pool_prop_checkpoint_mode,
					 DAOS_PROP_PO_CHECKPOINT_MODE_DEFAULT);
	if (rc != 0)
		D_GOTO(out_free, rc);

	rc = pool_upgrade_one_prop_int32(tx, svc, pool_uuid, &need_commit, "checkpoint freq",
					 &ds_pool_prop_checkpoint_freq,
					 DAOS_PROP_PO_CHECKPOINT_FREQ_DEFAULT);
	if (rc != 0)
		D_GOTO(out_free, rc);

	rc = pool_upgrade_one_prop_int32(tx, svc, pool_uuid, &need_commit, "checkpoint thresh",
					 &ds_pool_prop_checkpoint_thresh,
					 DAOS_PROP_PO_CHECKPOINT_THRESH_DEFAULT);
	if (rc != 0)
		D_GOTO(out_free, rc);

	d_iov_set(&value, &val32, sizeof(val32));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_upgrade_status, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST || val32 != DAOS_UPGRADE_STATUS_IN_PROGRESS) {
		val32 = DAOS_UPGRADE_STATUS_IN_PROGRESS;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_upgrade_status, &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade 'upgrade status' "
				"of pool, %d.\n", DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	d_iov_set(&value, &val32, sizeof(val32));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_perf_domain, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		val32 = DAOS_PROP_PO_PERF_DOMAIN_DEFAULT;
		rc = rdb_tx_update(tx, &svc->ps_root,
				   &ds_pool_prop_perf_domain, &value);
		if (rc != 0) {
			D_ERROR("failed to write pool performain domain prop, "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_upgrade_global_version,
			   &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST || val32 != DAOS_POOL_GLOBAL_VERSION) {
		val32 = DAOS_POOL_GLOBAL_VERSION;
		rc = rdb_tx_update(tx, &svc->ps_root,
				   &ds_pool_prop_upgrade_global_version, &value);
		if (rc != 0) {
			D_ERROR("failed to write upgrade global version prop, "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	if (need_commit) {
		daos_prop_t *prop = NULL;

		rc = rdb_tx_commit(tx);
		if (rc)
			D_GOTO(out_free, rc);
		rc = pool_prop_read(tx, svc, DAOS_PO_QUERY_PROP_ALL, &prop);
		if (rc)
			D_GOTO(out_free, rc);
		rc = ds_pool_iv_prop_update(svc->ps_pool, prop);
		daos_prop_free(prop);
	}

out_free:
	D_FREE(hdl_uuids);
	return rc;
}

static int
__ds_pool_mark_upgrade_completed(uuid_t pool_uuid, struct pool_svc *svc, int rc)
{
	struct rdb_tx			tx;
	d_iov_t				value;
	uint32_t			upgrade_status;
	uint32_t			global_version;
	uint32_t			obj_version;
	uint32_t			connectable;
	int				rc1;
	daos_prop_t			*prop = NULL;

	rc1 = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc1 != 0)
		D_GOTO(out, rc1);

	ABT_rwlock_wrlock(svc->ps_lock);
	if (rc == 0)
		upgrade_status = DAOS_UPGRADE_STATUS_COMPLETED;
	else
		upgrade_status = DAOS_UPGRADE_STATUS_FAILED;

	d_iov_set(&value, &upgrade_status, sizeof(upgrade_status));
	rc1 = rdb_tx_update(&tx, &svc->ps_root, &ds_pool_prop_upgrade_status,
			   &value);
	if (rc1)
		D_GOTO(out_tx, rc1);

	/*
	 * only bump global version and connectable properties
	 * if upgrade succeed.
	 */
	if (rc == 0) {
		global_version = DAOS_POOL_GLOBAL_VERSION;
		d_iov_set(&value, &global_version, sizeof(global_version));
		rc1 = rdb_tx_update(&tx, &svc->ps_root,
				    &ds_pool_prop_global_version, &value);
		if (rc1) {
			D_ERROR(DF_UUID": failed to upgrade global version "
				"of pool, %d.\n", DP_UUID(pool_uuid), rc1);
			D_GOTO(out_tx, rc1);
		}

		if (DAOS_FAIL_CHECK(DAOS_FAIL_POOL_CREATE_VERSION)) {
			uint64_t fail_val = daos_fail_value_get();

			obj_version = (uint32_t)fail_val;
		} else {
			obj_version = DS_POOL_OBJ_VERSION;
		}

		d_iov_set(&value, &obj_version, sizeof(obj_version));
		rc1 = rdb_tx_update(&tx, &svc->ps_root,
				    &ds_pool_prop_obj_version, &value);
		if (rc1) {
			D_ERROR(DF_UUID": failed to upgrade global version "
				"of pool, %d.\n", DP_UUID(pool_uuid), rc1);
			D_GOTO(out_tx, rc1);
		}

		connectable = 1;
		d_iov_set(&value, &connectable, sizeof(connectable));
		rc1 = rdb_tx_update(&tx, &svc->ps_root, &ds_pool_prop_connectable,
				    &value);
		if (rc1) {
			D_ERROR(DF_UUID": failed to set connectable of pool "
				"%d.\n", DP_UUID(pool_uuid), rc1);
			D_GOTO(out_tx, rc1);
		}
	}

	rc1 = rdb_tx_commit(&tx);
	if (rc1)
		D_GOTO(out_tx, rc1);

	if (rc == 0)
		/* also bump cached version */
		svc->ps_global_version = DAOS_POOL_GLOBAL_VERSION;

	rc1 = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ALL, &prop);
	if (rc1)
		D_GOTO(out_tx, rc1);
	rc1 = ds_pool_iv_prop_update(svc->ps_pool, prop);
	daos_prop_free(prop);
	if (rc1)
		D_GOTO(out_tx, rc1);

out_tx:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	D_DEBUG(DB_MD, DF_UUID "mark upgrade complete.: %d/%d\n", DP_UUID(pool_uuid), rc1, rc);
	return rc1;
}

/* check and upgrade the object layout if needed. */
static int
pool_check_upgrade_object_layout(struct rdb_tx *tx, struct pool_svc *svc,
				 bool *scheduled_layout_upgrade)
{
	daos_epoch_t	upgrade_eph = d_hlc_get();
	d_iov_t		value;
	uint32_t	current_layout_ver = 0;
	int		rc = 0;

	d_iov_set(&value, &current_layout_ver, sizeof(current_layout_ver));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_obj_version, &value);
	if (rc && rc != -DER_NONEXIST)
		return rc;
	else if (rc == -DER_NONEXIST)
		current_layout_ver = 0;

	if (current_layout_ver < DS_POOL_OBJ_VERSION) {
		rc = ds_rebuild_schedule(svc->ps_pool, svc->ps_pool->sp_map_version,
					 upgrade_eph, DS_POOL_OBJ_VERSION, NULL,
					 RB_OP_UPGRADE, 0);
		if (rc == 0)
			*scheduled_layout_upgrade = true;
	}
	return rc;
}

static int
ds_pool_mark_upgrade_completed_internal(struct pool_svc *svc, int ret)
{
	int rc;

	if (ret == 0)
		ret = ds_cont_upgrade(svc->ps_uuid, svc->ps_cont_svc);

	rc = __ds_pool_mark_upgrade_completed(svc->ps_uuid, svc, ret);
	if (rc == 0 && ret)
		rc = ret;

	return rc;
}

int
ds_pool_mark_upgrade_completed(uuid_t pool_uuid, int ret)
{
	struct pool_svc	*svc;
	int		rc;

	/* XXX check if the whole upgrade progress is really completed */
	rc = pool_svc_lookup_leader(pool_uuid, &svc, NULL);
	if (rc != 0)
		return rc;

	rc = ds_pool_mark_upgrade_completed_internal(svc, ret);

	pool_svc_put_leader(svc);

	return rc;
}

static int
ds_pool_upgrade_if_needed(uuid_t pool_uuid, struct rsvc_hint *po_hint,
			  struct pool_svc *svc, crt_rpc_t *rpc)
{
	struct rdb_tx			tx;
	d_iov_t				value;
	uint32_t			upgrade_status;
	uint32_t			upgrade_global_ver;
	int				rc;
	bool				scheduled_layout_upgrade = false;
	bool				dmg_upgrade_cmd = false;
	bool				request_schedule_upgrade = false;

	if (!svc) {
		rc = pool_svc_lookup_leader(pool_uuid, &svc, po_hint);
		if (rc != 0)
			return rc;
		dmg_upgrade_cmd = true;
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_put_leader, rc);

	/**
	 * Four kinds of pool upgrading states:
	 *
	 * 1. pool upgrade not started:
	 * upgrade_state: not started
	 * upgrade_global_version: v1
	 * global_version: v1
	 *
	 * 2. pool upgrade in progress:
	 * upgrade_state: in progress
	 * upgrade_global_version: v2
	 * global_version: v1
	 *
	 * 3. pool upgrade completed:
	 * upgrade_state: completed
	 * upgrade_global_version: v2
	 * global_version: v2
	 *
	 * 4. pool upgrade failed:
	 * upgrade_state: failed
	 * upgrade_global_version: v2
	 * global_version: v1
	 */
	ABT_rwlock_wrlock(svc->ps_lock);
	d_iov_set(&value, &upgrade_global_ver, sizeof(upgrade_global_ver));
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_upgrade_global_version,
			   &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_tx, rc);
	} else if (rc == -DER_NONEXIST) {
		if (!dmg_upgrade_cmd)
			D_GOTO(out_tx, rc = 0);
		D_GOTO(out_upgrade, rc);
	} else {
		d_iov_set(&value, &upgrade_status, sizeof(upgrade_status));
		rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_upgrade_status,
				   &value);
		if (rc)
			D_GOTO(out_tx, rc);

		if (upgrade_global_ver > DAOS_POOL_GLOBAL_VERSION) {
			D_ERROR(DF_UUID": downgrading pool is unsupported: %u -> %u\n",
				DP_UUID(svc->ps_uuid), upgrade_global_ver,
				DAOS_POOL_GLOBAL_VERSION);
			D_GOTO(out_tx, rc = -DER_INVAL);
		}
		D_DEBUG(DB_TRACE, "upgrade ver %u status %u\n", upgrade_global_ver, upgrade_status);
		switch (upgrade_status) {
		case DAOS_UPGRADE_STATUS_NOT_STARTED:
		case DAOS_UPGRADE_STATUS_COMPLETED:
			if ((upgrade_global_ver < DAOS_POOL_GLOBAL_VERSION &&
			     dmg_upgrade_cmd) || DAOS_FAIL_CHECK(DAOS_FORCE_OBJ_UPGRADE))
				D_GOTO(out_upgrade, rc = 0);
			else
				D_GOTO(out_tx, rc = 0);
			break;
		case DAOS_UPGRADE_STATUS_FAILED:
			if (upgrade_global_ver < DAOS_POOL_GLOBAL_VERSION) {
				D_ERROR(DF_UUID": upgrading pool %u -> %u\n is unsupported"
					" because pool upgraded to %u last time failed\n",
					DP_UUID(svc->ps_uuid), upgrade_global_ver,
					DAOS_POOL_GLOBAL_VERSION, upgrade_global_ver);
				D_GOTO(out_tx, rc = -DER_NOTSUPPORTED);
			}
			/* try again as users requested. */
			if (dmg_upgrade_cmd)
				D_GOTO(out_upgrade, rc = 0);
			else
				D_GOTO(out_tx, rc = 0);
			break;
		case DAOS_UPGRADE_STATUS_IN_PROGRESS:
			if (upgrade_global_ver < DAOS_POOL_GLOBAL_VERSION) {
				D_ERROR(DF_UUID": upgrading pool %u -> %u\n is unsupported"
					" because pool upgraded to %u not finished yet\n",
					DP_UUID(svc->ps_uuid), upgrade_global_ver,
					DAOS_POOL_GLOBAL_VERSION, upgrade_global_ver);
				D_GOTO(out_tx, rc = -DER_NOTSUPPORTED);
			} else if (dmg_upgrade_cmd) { /* not from resume */
				D_GOTO(out_tx, rc = -DER_INPROGRESS);
			} else {
				D_GOTO(out_upgrade, rc = 0);
			}
			break;
		default:
			D_ERROR("unknown upgrade pool status: %u\n", upgrade_status);
			D_GOTO(out_upgrade, rc = -DER_INVAL);
			break;
		}
	}
out_upgrade:
	request_schedule_upgrade = true;
	/**
	 * Todo: make sure no rebuild/reint/expand are in progress
	 */
	rc = pool_upgrade_props(&tx, svc, pool_uuid, rpc);
	if (rc)
		D_GOTO(out_tx, rc);

	rc = pool_check_upgrade_object_layout(&tx, svc, &scheduled_layout_upgrade);
	if (rc < 0)
		D_GOTO(out_tx, rc);

out_tx:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);

	if (request_schedule_upgrade && !scheduled_layout_upgrade) {
		int rc1;

		if (rc == 0 && dmg_upgrade_cmd &&
		    DAOS_FAIL_CHECK(DAOS_POOL_UPGRADE_CONT_ABORT))
			D_GOTO(out_put_leader, rc = -DER_AGAIN);
		rc1 = ds_pool_mark_upgrade_completed_internal(svc, rc);
		if (rc == 0 && rc1)
			rc = rc1;
	}
out_put_leader:
	if (dmg_upgrade_cmd) {
		ds_rsvc_set_hint(&svc->ps_rsvc, po_hint);
		pool_svc_put_leader(svc);
	}

	return rc;
}

/**
 * Set a pool's properties without having a handle for the pool
 */
void
ds_pool_upgrade_handler(crt_rpc_t *rpc)
{
	struct pool_upgrade_in		*in = crt_req_get(rpc);
	struct pool_upgrade_out		*out = crt_reply_get(rpc);
	int				rc;

	rc = ds_pool_upgrade_if_needed(in->poi_op.pi_uuid,
				       &out->poo_op.po_hint, NULL, rpc);
	out->poo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->poi_op.pi_uuid), rpc, rc);
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

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_PERF_DOMAIN)) {
		D_ERROR("Can't set perf_domain on existing pool.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_REDUN_FAC)) {
		D_ERROR("Can't set set redundancy factor on existing pool.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_EC_PDA)) {
		D_ERROR("Can't set EC performance domain affinity on existing pool\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_RP_PDA)) {
		D_ERROR("Can't set RP performance domain affinity on existing pool\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_GLOBAL_VERSION)) {
		D_ERROR("Can't set pool global version if pool is created.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_UPGRADE_STATUS)) {
		D_ERROR("Can't set pool upgrade status if pool is created.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	/* Disallow to begin with; will support in the future. */
	if (daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_REDUN_FAC)) {
		D_ERROR(DF_UUID ": cannot set pool service redundancy factor on existing pool\n",
			DP_UUID(pool_uuid));
		rc = -DER_NO_PERM;
		goto out;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_OBJ_VERSION)) {
		D_ERROR("Can't set pool obj version if pool is created.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

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

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->pso_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
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

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->pui_op.pi_uuid), rpc, rc);
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

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->puo_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
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

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->pdi_op.pi_uuid), rpc, rc);
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

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->pdo_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
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

static void
pool_svc_reconf_ult(void *arg)
{
	struct pool_svc		*svc = arg;
	struct pool_svc_sched	*reconf = &svc->ps_reconf_sched;
	d_rank_list_t		*current;
	d_rank_list_t		*to_add;
	d_rank_list_t		*to_remove;
	d_rank_list_t		*new;
	int			 rc;

	D_DEBUG(DB_MD, DF_UUID": begin\n", DP_UUID(svc->ps_uuid));

	if (reconf->psc_canceled)
		goto out;

	/* When there are pending events, the pool map may be unstable. */
	while (events_pending(svc)) {
		dss_sleep(3000 /* ms */);
		if (reconf->psc_canceled)
			goto out;
	}

	rc = rdb_get_ranks(svc->ps_rsvc.s_db, &current);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to get pool service replica ranks: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out;
	}

	ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
	rc = ds_pool_plan_svc_reconfs(reconf->psc_svc_rf, svc->ps_pool->sp_map, current,
				      dss_self_rank(), &to_add, &to_remove);
	ABT_rwlock_unlock(svc->ps_pool->sp_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot plan pool service reconfigurations: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out_cur;
	}

	D_DEBUG(DB_MD, DF_UUID": svc_rf=%d current=%u to_add=%u to_remove=%u\n",
		DP_UUID(svc->ps_uuid), reconf->psc_svc_rf, current->rl_nr, to_add->rl_nr,
		to_remove->rl_nr);

	/*
	 * Ignore the return values from the "add" and "remove" calls here. If
	 * the "add" calls returns an error, to_add contains the N ranks that
	 * have not been added. We delete N ranks from to_remove to account for
	 * the failed additions, and continue to make the "remove" call. If any
	 * of the two calls returns an error, we still need to report any
	 * membership changes to the MS.
	 */
	if (to_add->rl_nr > 0)
		ds_rsvc_add_replicas_s(&svc->ps_rsvc, to_add, ds_rsvc_get_md_cap());
	if (reconf->psc_canceled)
		goto out_to_add_remove;
	if (to_add->rl_nr > to_remove->rl_nr)
		to_remove->rl_nr = 0;
	else
		to_remove->rl_nr -= to_add->rl_nr;
	if (to_remove->rl_nr > 0) {
		d_rank_list_t *tmp;

		/*
		 * Since the ds_rsvc_dist_stop part is likely to hit RPC
		 * timeouts, after removing the replicas from the membership,
		 * we notify the MS first, and then come back to
		 * ds_rsvc_dist_stop.
		 */
		rc = d_rank_list_dup(&tmp, to_remove);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to duplicate to_remove: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
			goto out_to_add_remove;
		}
		rc = rdb_remove_replicas(svc->ps_rsvc.s_db, tmp);
		if (rc != 0)
			D_ERROR(DF_UUID": failed to remove replicas: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
		/* Delete from to_remove ranks that are not removed. */
		d_rank_list_filter(tmp, to_remove, true /* exclude */);
		d_rank_list_free(tmp);
	}

	if (rdb_get_ranks(svc->ps_rsvc.s_db, &new) == 0) {
		d_rank_list_sort(current);
		d_rank_list_sort(new);

		if (reconf->psc_force_notify || !d_rank_list_identical(new, current)) {
			/*
			 * Send RAS event to control-plane over dRPC to indicate
			 * change in pool service replicas.
			 */
			rc = ds_notify_pool_svc_update(&svc->ps_uuid, new, svc->ps_rsvc.s_term);
			if (rc == 0)
				reconf->psc_force_notify = false;
			else
				D_ERROR(DF_UUID": replica update notify failure: "DF_RC"\n",
					DP_UUID(svc->ps_uuid), DP_RC(rc));
		}

		d_rank_list_free(new);
	}
	if (reconf->psc_canceled)
		goto out_to_add_remove;

	/* Ignore the return value of this ds_rsvc_dist_stop call. */
	if (to_remove->rl_nr > 0)
		ds_rsvc_dist_stop(svc->ps_rsvc.s_class, &svc->ps_rsvc.s_id, to_remove,
				  NULL /* excluded */, svc->ps_rsvc.s_term, true /* destroy */);

out_to_add_remove:
	d_rank_list_free(to_remove);
	d_rank_list_free(to_add);
out_cur:
	d_rank_list_free(current);
out:
	sched_end(reconf);
	ABT_cond_broadcast(reconf->psc_cv);
	D_DEBUG(DB_MD, DF_UUID": end\n", DP_UUID(svc->ps_uuid));
}

static void
pool_svc_schedule(struct pool_svc *svc, struct pool_svc_sched *sched,
		  void (*func)(void *))
{
	enum ds_rsvc_state	state;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": begin\n", DP_UUID(svc->ps_uuid));

	/*
	 * Avoid scheduling when the PS is stepping down
	 * and has already called sched_cancel_and_wait.
	 */
	state = ds_rsvc_get_state(&svc->ps_rsvc);
	if (state == DS_RSVC_DRAINING) {
		D_DEBUG(DB_MD, DF_UUID": end: service %s\n", DP_UUID(svc->ps_uuid),
			ds_rsvc_state_str(state));
		return;
	}

	D_ASSERT(&svc->ps_reconf_sched == sched || &svc->ps_rfcheck_sched == sched);
	sched_cancel_and_wait(sched);

	sched_begin(sched);

	/*
	 * An extra svc leader reference is not required, because
	 * pool_svc_step_down_cb waits for this ULT to terminate.
	 *
	 * ULT tracking is achieved through sched, not a ULT handle.
	 */
	rc = dss_ult_create(func, svc, DSS_XS_SELF, 0, 0, NULL /* ult */);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create ULT: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		sched_end(sched);
	}

	D_DEBUG(DB_MD, DF_UUID": end: "DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
}

static int pool_find_all_targets_by_addr(struct pool_map *map,
					 struct pool_target_addr_list *list,
					 struct pool_target_id_list *tgt_list,
					 struct pool_target_addr_list *inval);

static int
cont_rf_check_cb(uuid_t pool_uuid, uuid_t cont_uuid, struct rdb_tx *tx, void *arg)
{
	struct pool_svc_sched *sched = arg;
	int rc;

	/* If anything happened during rf check, let's continue to check the next container
	 * for the moment.
	 */
	rc = ds_cont_rf_check(pool_uuid, cont_uuid, tx);
	if (rc)
		D_CDEBUG(rc == -DER_RF, DB_MD, DLOG_ERR, DF_CONT" check_rf: "DF_RC"\n",
			 DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));

	if (sched->psc_canceled) {
		D_DEBUG(DB_MD, DF_CONT" is canceled.\n", DP_CONT(pool_uuid, cont_uuid));
		return 1;
	}

	return 0;
}

static void
pool_svc_rfcheck_ult(void *arg)
{
	struct pool_svc	*svc = arg;
	int rc;

	do {
		/* retry until some one stop the pool svc(rc == 1) or succeed */
		rc = ds_cont_rdb_iterate(svc->ps_cont_svc, cont_rf_check_cb,
					 &svc->ps_rfcheck_sched);
		if (rc >= 0)
			break;

		D_DEBUG(DB_MD, DF_UUID" check rf with %d and retry\n",
			DP_UUID(svc->ps_uuid), rc);

		dss_sleep(0);
	} while (1);

	sched_end(&svc->ps_rfcheck_sched);
	D_INFO("RF check finished for "DF_UUID"\n", DP_UUID(svc->ps_uuid));
	ABT_cond_broadcast(svc->ps_rfcheck_sched.psc_cv);
}

/*
 * Perform an update to the pool map of \a svc.
 *
 * \param[in]	svc		pool service
 * \param[in]	opc		update operation (e.g., POOL_EXCLUDE)
 * \param[in]	exclude_rank	for excluding ranks (rather than targets)
 * \param[in]	extend_rank_list ranks list to be extended.
 * \param[in]	extend_domains_nr number of extend domains.
 * \param[in]	extend_domains	domains to be extended.
 * \param[in,out]
 *		tgts		target IDs (if empty, must specify tgt_addrs)
 * \param[in]	tgt_addrs	optional target addresses (ignored if \a tgts is
 *				nonempty; requires inval_tgt_addrs)
 * \param[out]	hint		optional leadership hint
 * \param[out]	p_updated	optional info on if pool map has been updated
 * \param[out]	map_version_p	pool map version
 * \param[out]	tgt_map_ver	pool map version for the last target change
 *				(instead of a node change, for example) made by
 *				this update, or 0 if none has been made (see
 *				ds_pool_map_tgts_update)
 * \param[out]	inval_tgt_addrs	optional invalid target addresses (ignored if
 *				\a tgts is nonempty; if specified, must be
 *				initialized to empty and freed by the caller)
 */
static int
pool_svc_update_map_internal(struct pool_svc *svc, unsigned int opc,
			     bool exclude_rank, d_rank_list_t *extend_rank_list,
			     uint32_t extend_domains_nr, uint32_t *extend_domains,
			     struct pool_target_id_list *tgts,
			     struct pool_target_addr_list *tgt_addrs,
			     struct rsvc_hint *hint, bool *p_updated,
			     uint32_t *map_version_p, uint32_t *tgt_map_ver,
			     struct pool_target_addr_list *inval_tgt_addrs)
{
	struct rdb_tx		tx;
	struct pool_map	       *map;
	uint32_t		map_version_before;
	uint32_t		map_version;
	struct pool_buf	       *map_buf = NULL;
	bool			updated = false;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": opc=%u exclude_rank=%d ntgts=%d ntgt_addrs=%d\n",
		DP_UUID(svc->ps_uuid), opc, exclude_rank, tgts->pti_number,
		tgt_addrs == NULL ? 0 : tgt_addrs->pta_number);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_wrlock(svc->ps_lock);

	/* Create a temporary pool map based on the last committed version. */
	rc = read_map(&tx, &svc->ps_root, &map);
	if (rc != 0)
		goto out_lock;

	if (opc == POOL_EXTEND) {
		D_ASSERT(extend_rank_list != NULL);
		map_version = pool_map_get_version(map) + 1;
		rc = gen_pool_buf(map, &map_buf, map_version, extend_domains_nr,
				  extend_rank_list->rl_nr, extend_rank_list->rl_nr * dss_tgt_nr,
				  extend_domains, dss_tgt_nr);
		if (rc != 0)
			D_GOTO(out_map, rc);

		if (map_buf != NULL) {
			/* Extend the current pool map */
			rc = pool_map_extend(map, map_version, map_buf);
			pool_buf_free(map_buf);
			map_buf = NULL;
			if (rc != 0)
				D_GOTO(out_map, rc);
		}

		/* Get a list of all the targets being added */
		rc = pool_map_find_targets_on_ranks(map, extend_rank_list, tgts);
		if (rc <= 0) {
			D_ERROR(DF_UUID" failed to find targets rc: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
			D_GOTO(out_map, rc);
		}
	} else {
		/*
		 * If an empty target ID list is provided, convert from target
		 * addresses.
		 */
		if (tgts->pti_number == 0) {
			D_ASSERT(tgts->pti_ids == NULL);
			D_ASSERT(tgt_addrs != NULL);
			D_ASSERT(inval_tgt_addrs != NULL);
			rc = pool_find_all_targets_by_addr(map, tgt_addrs, tgts,
							   inval_tgt_addrs);
			if (rc != 0)
				goto out_map;
			if (inval_tgt_addrs->pta_number > 0) {
				/*
				 * If any invalid ranks/targets were specified here,
				 * abort the entire request. This will mean the
				 * operator needs to resubmit the request with
				 * corrected arguments, which will be easier without
				 * trying to figure out which arguments were accepted &
				 * started processing already.
				 */
				rc = -DER_NONEXIST;
				goto out_map;
			}
		}
	}
	/*
	 * Attempt to modify the temporary pool map and save its versions
	 * before and after. If the version hasn't changed, we are done.
	 */
	map_version_before = pool_map_get_version(map);
	rc = ds_pool_map_tgts_update(map, tgts, opc, exclude_rank, tgt_map_ver,
				     true);
	if (rc != 0)
		D_GOTO(out_map, rc);
	map_version = pool_map_get_version(map);
	D_DEBUG(DB_MD, DF_UUID": version=%u->%u\n",
		DP_UUID(svc->ps_uuid), map_version_before, map_version);
	if (map_version == map_version_before)
		D_GOTO(out_map, rc = 0);

	/* Write the new pool map. */
	rc = pool_buf_extract(map, &map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);
	rc = write_map_buf(&tx, &svc->ps_root, map_buf, map_version);
	if (rc != 0)
		goto out_map_buf;

	rc = rdb_tx_commit(&tx);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": failed to commit: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out_map_buf;
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
		goto out_map_buf;
	}

	ds_rsvc_request_map_dist(&svc->ps_rsvc);

	pool_svc_schedule(svc, &svc->ps_reconf_sched, pool_svc_reconf_ult);
	if (opc == POOL_EXCLUDE)
		pool_svc_schedule(svc, &svc->ps_rfcheck_sched, pool_svc_rfcheck_ult);

out_map_buf:
	pool_buf_free(map_buf);
out_map:
	pool_map_decref(map);
out_lock:
	if (map_version_p != NULL)
		*map_version_p = ds_pool_get_version(svc->ps_pool);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	if (hint != NULL)
		ds_rsvc_set_hint(&svc->ps_rsvc, hint);
	if (p_updated)
		*p_updated = updated;
	return rc;
}

static int
pool_find_all_targets_by_addr(struct pool_map *map,
			      struct pool_target_addr_list *list,
			      struct pool_target_id_list *tgt_list,
			      struct pool_target_addr_list *inval_list_out)
{
	int	i;
	int	rc = 0;

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
			D_DEBUG(DB_MD, "cannot find rank %u target %u\n",
				list->pta_addrs[i].pta_rank,
				list->pta_addrs[i].pta_target);
			ret = pool_target_addr_list_append(inval_list_out,
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

/* See pool_svc_update_map_internal documentation. */
static int
pool_update_map_internal(uuid_t pool_uuid, unsigned int opc, bool exclude_rank,
			 struct pool_target_id_list *tgts,
			 struct pool_target_addr_list *tgt_addrs,
			 struct rsvc_hint *hint, bool *p_updated,
			 uint32_t *map_version_p, uint32_t *tgt_map_ver,
			 struct pool_target_addr_list *inval_tgt_addrs)
{
	struct pool_svc	       *svc;
	int			rc;

	rc = pool_svc_lookup_leader(pool_uuid, &svc, hint);
	if (rc != 0)
		return rc;

	rc = pool_svc_update_map_internal(svc, opc, exclude_rank, NULL, 0,
					  NULL, tgts, tgt_addrs, hint, p_updated,
					  map_version_p, tgt_map_ver,
					  inval_tgt_addrs);

	pool_svc_put_leader(svc);
	return rc;
}

int
ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, POOL_EXCLUDE_OUT, false,
					list, NULL, NULL, NULL, NULL, NULL,
					NULL);
}

int
ds_pool_tgt_exclude(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, POOL_EXCLUDE, false, list,
					NULL, NULL, NULL, NULL, NULL, NULL);
}

int
ds_pool_tgt_add_in(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, POOL_ADD_IN, false, list,
					NULL, NULL, NULL, NULL, NULL, NULL);
}

/*
 * Perform a pool map update indicated by opc. If successful, the new pool map
 * version is reported via map_version. Upon -DER_NOTLEADER, a pool service
 * leader hint, if available, is reported via hint (if not NULL).
 */
static int
pool_svc_update_map(struct pool_svc *svc, crt_opcode_t opc, bool exclude_rank,
		    d_rank_list_t *extend_rank_list, uint32_t *extend_domains,
		    uint32_t extend_domains_nr, struct pool_target_addr_list *list,
		    struct pool_target_addr_list *inval_list_out,
		    uint32_t *map_version, struct rsvc_hint *hint)
{
	daos_rebuild_opc_t		op;
	struct pool_target_id_list	target_list = { 0 };
	daos_prop_t			prop = { 0 };
	uint32_t			tgt_map_ver = 0;
	struct daos_prop_entry		*entry;
	bool				updated;
	int				rc;
	char				*env;
	daos_epoch_t			rebuild_eph = d_hlc_get();
	uint64_t			delay = 2;

	rc = pool_svc_update_map_internal(svc, opc, exclude_rank, extend_rank_list,
					  extend_domains_nr, extend_domains,
					  &target_list, list, hint, &updated,
					  map_version, &tgt_map_ver, inval_list_out);
	if (rc)
		D_GOTO(out, rc);

	if (!updated)
		D_GOTO(out, rc);

	switch (opc) {
	case POOL_EXCLUDE:
		op = RB_OP_EXCLUDE;
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

	rc = ds_pool_iv_prop_fetch(svc->ps_pool, &prop);
	if (rc)
		D_GOTO(out, rc);

	entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SELF_HEAL);
	D_ASSERT(entry != NULL);
	if (!(entry->dpe_val & DAOS_SELF_HEAL_AUTO_REBUILD)) {
		D_DEBUG(DB_MD, "self healing is disabled\n");
		D_GOTO(out, rc);
	}

	if (daos_fail_check(DAOS_REBUILD_DELAY))
		delay = 5;

	D_DEBUG(DB_MD, "map ver %u/%u\n", map_version ? *map_version : -1,
		tgt_map_ver);
	if (tgt_map_ver != 0) {
		rc = ds_rebuild_schedule(svc->ps_pool, tgt_map_ver, rebuild_eph,
					 0, &target_list, op, delay);
		if (rc != 0) {
			D_ERROR("rebuild fails rc: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

out:
	daos_prop_fini(&prop);
	pool_target_id_list_free(&target_list);
	return rc;
}

void
ds_pool_extend_handler(crt_rpc_t *rpc)
{
	struct pool_extend_in	*in = crt_req_get(rpc);
	struct pool_extend_out	*out = crt_reply_get(rpc);
	struct pool_svc		*svc;
	uuid_t			pool_uuid;
	d_rank_list_t		rank_list;
	uint32_t		ndomains;
	uint32_t		*domains;
	uint32_t		rebuild_ver = 0;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n", DP_UUID(in->pei_op.pi_uuid), rpc);

	uuid_copy(pool_uuid, in->pei_op.pi_uuid);
	rank_list.rl_nr = in->pei_tgt_ranks->rl_nr;
	rank_list.rl_ranks = in->pei_tgt_ranks->rl_ranks;
	ndomains = in->pei_ndomains;
	domains = in->pei_domains.ca_arrays;

	rc = pool_svc_lookup_leader(in->pei_op.pi_uuid, &svc, &out->peo_op.po_hint);
	if (rc != 0)
		goto out;

	/**
	 * Check if there are ongoing rebuild jobs for extend/reint/drain, to avoid
	 * updating the pool map during rebuild, which might screw the object layout.
	 */
	ds_rebuild_running_query(svc->ps_uuid, &rebuild_ver, NULL, NULL);
	if (rebuild_ver != 0) {
		D_ERROR(DF_UUID": other rebuild job rebuild ver %u is ongoing,"
			"extending can not be done: %d\n",
			DP_UUID(svc->ps_uuid), rebuild_ver, -DER_BUSY);
		D_GOTO(put, rc = -DER_BUSY);
	}

	rc = pool_svc_update_map(svc, opc_get(rpc->cr_opc),
				 false /* exclude_rank */,
				 &rank_list, domains, ndomains,
				 NULL, NULL, &out->peo_op.po_map_version,
				 &out->peo_op.po_hint);
put:
	pool_svc_put_leader(svc);
out:
	out->peo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pei_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

static int
pool_discard(crt_context_t ctx, struct pool_svc *svc, struct pool_target_addr_list *list)
{
	struct pool_tgt_discard_in	*ptdi_in;
	struct pool_tgt_discard_out	*ptdi_out;
	crt_rpc_t			*rpc;
	d_rank_list_t			*rank_list = NULL;
	crt_opcode_t			opc;
	int				i;
	int				rc;

	rank_list = d_rank_list_alloc(list->pta_number);
	if (rank_list == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rank_list->rl_nr = 0;
	/* remove the duplicate ranks from list, see reintegrate target case */
	for (i = 0; i < list->pta_number; i++) {
		if (daos_rank_in_rank_list(rank_list, list->pta_addrs[i].pta_rank))
			continue;

		rank_list->rl_ranks[rank_list->rl_nr++] = list->pta_addrs[i].pta_rank;
		D_DEBUG(DB_MD, DF_UUID": discard rank %u\n",
			DP_UUID(svc->ps_pool->sp_uuid), list->pta_addrs[i].pta_rank);
	}

	if (rank_list->rl_nr == 0) {
		D_DEBUG(DB_MD, DF_UUID" discard 0 rank.\n", DP_UUID(svc->ps_pool->sp_uuid));
		D_GOTO(out, rc = 0);
	}

	opc = DAOS_RPC_OPCODE(POOL_TGT_DISCARD, DAOS_POOL_MODULE, DAOS_POOL_VERSION);
	rc = crt_corpc_req_create(ctx, NULL, rank_list, opc, NULL,
				  NULL, CRT_RPC_FLAG_FILTER_INVERT,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 32), &rpc);
	if (rc)
		D_GOTO(out, rc);

	ptdi_in = crt_req_get(rpc);
	ptdi_in->ptdi_addrs.ca_arrays = list->pta_addrs;
	ptdi_in->ptdi_addrs.ca_count = list->pta_number;
	uuid_copy(ptdi_in->ptdi_uuid, svc->ps_pool->sp_uuid);
	rc = dss_rpc_send(rpc);

	ptdi_out = crt_reply_get(rpc);
	D_ASSERT(ptdi_out != NULL);
	rc = ptdi_out->ptdo_rc;
	if (rc != 0)
		D_ERROR(DF_UUID": pool discard failed: rc: %d\n",
			DP_UUID(svc->ps_pool->sp_uuid), rc);

	crt_req_decref(rpc);

out:
	if (rank_list)
		d_rank_list_free(rank_list);
	return rc;
}

void
ds_pool_update_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_update_in      *in = crt_req_get(rpc);
	struct pool_tgt_update_out     *out = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	uint32_t			rebuild_ver = 0;
	struct pool_target_addr_list	list = { 0 };
	struct pool_target_addr_list	inval_list_out = { 0 };
	int				rc;

	if (in->pti_addr_list.ca_arrays == NULL ||
	    in->pti_addr_list.ca_count == 0)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p ntargets=%zu\n", DP_UUID(in->pti_op.pi_uuid),
		rpc, in->pti_addr_list.ca_count);

	rc = pool_svc_lookup_leader(in->pti_op.pi_uuid, &svc,
				    &out->pto_op.po_hint);
	if (rc != 0)
		goto out;

	if (opc_get(rpc->cr_opc) == POOL_REINT || opc_get(rpc->cr_opc) == POOL_DRAIN) {
		/**
		 * Check if there are ongoing rebuild jobs for extend/reint/drain, to avoid
		 * updating the pool map during rebuild, which might screw the object layout.
		 */
		ds_rebuild_running_query(svc->ps_uuid, &rebuild_ver, NULL, NULL);
		if (rebuild_ver != 0) {
			D_ERROR(DF_UUID": other rebuild job rebuild ver %u is ongoing,"
				" so current opc %d can not be done: %d\n",
				DP_UUID(svc->ps_uuid), rebuild_ver, opc_get(rpc->cr_opc),
				-DER_BUSY);
			D_GOTO(out_svc, rc = -DER_BUSY);
		}
	}

	list.pta_number = in->pti_addr_list.ca_count;
	list.pta_addrs = in->pti_addr_list.ca_arrays;

	if (opc_get(rpc->cr_opc) == POOL_REINT) {
		rc = pool_discard(rpc->cr_ctx, svc, &list);
		if (rc)
			goto out_svc;
	}

	rc = pool_svc_update_map(svc, opc_get(rpc->cr_opc),
				 false /* exclude_rank */, NULL, NULL, 0, &list,
				 &inval_list_out, &out->pto_op.po_map_version,
				 &out->pto_op.po_hint);
	if (rc != 0)
		goto out_svc;

	out->pto_addr_list.ca_arrays = inval_list_out.pta_addrs;
	out->pto_addr_list.ca_count = inval_list_out.pta_number;

out_svc:
	pool_svc_put_leader(svc);
out:
	out->pto_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pti_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
	pool_target_addr_list_free(&inval_list_out);
}

static int
pool_svc_exclude_rank(struct pool_svc *svc, d_rank_t rank)
{
	struct pool_target_addr_list	list;
	struct pool_target_addr_list	inval_list_out = { 0 };
	struct pool_target_addr		tgt_rank;
	uint32_t			map_version = 0;
	int				rc;

	tgt_rank.pta_rank = rank;
	tgt_rank.pta_target = -1;
	list.pta_number = 1;
	list.pta_addrs = &tgt_rank;

	rc = pool_svc_update_map(svc, POOL_EXCLUDE, true /* exclude_rank */,
				 NULL, NULL, 0, &list, &inval_list_out, &map_version,
				 NULL /* hint */);

	D_DEBUG(DB_MD, "Exclude pool "DF_UUID"/%u rank %u: rc %d\n",
		DP_UUID(svc->ps_uuid), map_version, rank, rc);

	pool_target_addr_list_free(&inval_list_out);

	return rc;
}

struct evict_iter_arg {
	uuid_t *eia_hdl_uuids;
	size_t	eia_hdl_uuids_size;
	int	eia_n_hdl_uuids;
	char	*eia_machine;
	struct pool_svc *eia_pool_svc;
};

static int
evict_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	struct evict_iter_arg  *arg = varg;

	D_ASSERT(arg->eia_hdl_uuids != NULL);
	D_ASSERT(arg->eia_hdl_uuids_size > sizeof(uuid_t));

	if (key->iov_len != sizeof(uuid_t)) {
		D_ERROR("invalid key size: "DF_U64"\n", key->iov_len);
		return -DER_IO;
	}
	if (val->iov_len == sizeof(struct pool_hdl_v0)) {
		/* old/2.0 pool handle format ? */
		if (arg->eia_pool_svc->ps_global_version < DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED) {
			D_DEBUG(DB_MD, "2.0 pool handle format detected\n");
			/* if looking for a specific machine, do not select this handle */
			if (arg->eia_machine)
				return 0;
		} else {
			D_ERROR("invalid value size: "DF_U64" for pool version %u\n", val->iov_len,
				arg->eia_pool_svc->ps_global_version);
			return -DER_IO;
		}
	} else {
		struct pool_hdl *hdl = val->iov_buf;

		if (val->iov_len != sizeof(struct pool_hdl) + hdl->ph_cred_len ||
		    arg->eia_pool_svc->ps_global_version < DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED) {
			D_ERROR("invalid value size: "DF_U64" for pool version %u, expected %zu\n",
				val->iov_len, arg->eia_pool_svc->ps_global_version,
				arg->eia_pool_svc->ps_global_version <
				DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED ?
				sizeof(struct pool_hdl_v0) :
				sizeof(struct pool_hdl) + hdl->ph_cred_len);
			return -DER_IO;
		}
	}

	/* If we specified a machine name as a filter check before we do the realloc */
	if (arg->eia_machine) {
		struct pool_hdl	*hdl = val->iov_buf;

		if (strncmp(arg->eia_machine, hdl->ph_machine, sizeof(hdl->ph_machine)) != 0) {
			return 0;
		}
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
		   size_t *hdl_uuids_size, int *n_hdl_uuids, char *machine)
{
	struct evict_iter_arg	arg = {0};
	int			rc;

	arg.eia_hdl_uuids_size = sizeof(uuid_t) * 4;
	D_ALLOC(arg.eia_hdl_uuids, arg.eia_hdl_uuids_size);
	if (arg.eia_hdl_uuids == NULL)
		return -DER_NOMEM;
	arg.eia_n_hdl_uuids = 0;
	if (machine)
		arg.eia_machine = machine;
	arg.eia_pool_svc = svc;

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

	if (hdl_list == NULL || n_hdl_list == 0) {
		return -DER_INVAL;
	}

	/* Assume the entire list is valid */
	D_ALLOC(valid_list, sizeof(uuid_t) * n_hdl_list);
	if (valid_list == NULL)
		return -DER_NOMEM;

	for (i = 0; i < n_hdl_list; i++) {
		d_iov_set(&key, hdl_list[i], sizeof(uuid_t));
		d_iov_set(&value, NULL, 0);
		rc = rdb_tx_lookup(tx, &svc->ps_handles, &key, &value);

		if (rc == 0) {
			uuid_copy(valid_list[n_valid_list], hdl_list[i]);
			n_valid_list++;
		} else if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_MD, "Skipping invalid handle" DF_UUID "\n",
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

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
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
					&n_hdl_uuids, in->pvi_machine);
	}

	if (rc != 0)
		D_GOTO(out_lock, rc);
	D_DEBUG(DB_MD, "number of handles found was: %d\n", n_hdl_uuids);

	if (n_hdl_uuids > 0) {
		/* If pool destroy but not forcibly, error: the pool is busy */

		if (in->pvi_pool_destroy && !in->pvi_pool_destroy_force) {
			D_DEBUG(DB_MD, DF_UUID": busy, %u open handles\n",
				DP_UUID(in->pvi_op.pi_uuid), n_hdl_uuids);
			D_GOTO(out_free, rc = -DER_BUSY);
		} else {
			/* Pool evict, or pool destroy with force=true */
			rc = pool_disconnect_hdls(&tx, svc, hdl_uuids,
						  n_hdl_uuids, rpc->cr_ctx);
			if (rc != 0) {
				D_GOTO(out_free, rc);
			} else {
				struct pool_metrics *metrics;

				/** update metric */
				metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
				d_tm_inc_counter(metrics->evict_total, n_hdl_uuids);
			}
		}
	}

	/* If pool destroy and not error case, disable new connections */
	if (in->pvi_pool_destroy) {
		uint32_t	connectable = 0;
		d_iov_t		value;

		d_iov_set(&value, &connectable, sizeof(connectable));
		rc = rdb_tx_update_critical(&tx, &svc->ps_root, &ds_pool_prop_connectable, &value);
		if (rc != 0)
			D_GOTO(out_free, rc);

		ds_pool_iv_srv_hdl_invalidate(svc->ps_pool);
		ds_iv_ns_leader_stop(svc->ps_pool->sp_iv_ns);
		D_DEBUG(DB_MD, DF_UUID": pool destroy/evict: mark pool for "
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
	out->pvo_n_hdls_evicted = n_hdl_uuids;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pvi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

/**
 * Send a CaRT message to the pool svc to test and
 * (if applicable based on destroy and force option) evict all open handles
 * on a pool.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	handles		List of handles to selectively evict
 * \param[in]	n_handles	Number of items in handles
 * \param[in]	destroy		If true the evict request is a destroy request
 * \param[in]	force		If true and destroy is true request all handles
 *				be forcibly evicted
 * \param[in]   machine		Hostname to use as filter for evicting handles
 * \param[out]	count		Number of handles evicted
 *
 * \return	0		Success
 *		-DER_BUSY	Open pool handles exist and no force requested
 *
 */
int
ds_pool_svc_check_evict(uuid_t pool_uuid, d_rank_list_t *ranks,
			uuid_t *handles, size_t n_handles,
			uint32_t destroy, uint32_t force,
			char *machine, uint32_t *count)
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
	in->pvi_machine = machine;

	/* Pool destroy (force=false): assert no open handles / do not evict.
	 * Pool destroy (force=true): evict any/all open handles on the pool.
	 */
	in->pvi_pool_destroy = destroy;
	in->pvi_pool_destroy_force = force;

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->pvo_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
		D_GOTO(rechoose, rc);
	}

	rc = out->pvo_op.po_rc;
	if (rc != 0)
		D_ERROR(DF_UUID ": pool destroy failed to evict handles, rc: " DF_RC "\n",
			DP_UUID(pool_uuid), DP_RC(rc));
	if (count)
		*count = out->pvo_n_hdls_evicted;

	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&client);
out:
	return rc;
}

/*
 * Transfer list of pool ranks to "remote_bulk". If the remote bulk buffer
 * is too small, then return -DER_TRUNC. RPC response will contain the number
 * of ranks in the pool that the client can use to resize its buffer
 * for another RPC request.
 */
static int
transfer_ranks_buf(d_rank_t *ranks_buf, size_t nranks,
		   struct pool_svc *svc, crt_rpc_t *rpc, crt_bulk_t remote_bulk)
{
	size_t				 ranks_buf_size;
	daos_size_t			 remote_bulk_size;
	d_iov_t				 ranks_iov;
	d_sg_list_t			 ranks_sgl;
	crt_bulk_t			 bulk = CRT_BULK_NULL;
	struct crt_bulk_desc		 bulk_desc;
	crt_bulk_opid_t			 bulk_opid;
	ABT_eventual			 eventual;
	int				*status;
	int				 rc;

	D_ASSERT(nranks > 0);
	ranks_buf_size = nranks * sizeof(d_rank_t);

	/* Check if the client bulk buffer is large enough. */
	rc = crt_bulk_get_len(remote_bulk, &remote_bulk_size);
	if (rc != 0)
		D_GOTO(out, rc);
	if (remote_bulk_size < ranks_buf_size) {
		D_ERROR(DF_UUID ": remote ranks buffer(" DF_U64 ")"
			" < required (%lu)\n", DP_UUID(svc->ps_uuid),
			remote_bulk_size, ranks_buf_size);
		D_GOTO(out, rc = -DER_TRUNC);
	}

	d_iov_set(&ranks_iov, ranks_buf, ranks_buf_size);
	ranks_sgl.sg_nr = 1;
	ranks_sgl.sg_nr_out = 0;
	ranks_sgl.sg_iovs = &ranks_iov;

	rc = crt_bulk_create(rpc->cr_ctx, &ranks_sgl, CRT_BULK_RO, &bulk);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Prepare for crt_bulk_transfer(). */
	bulk_desc.bd_rpc = rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_PUT;
	bulk_desc.bd_remote_hdl = remote_bulk;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = bulk;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = ranks_iov.iov_len;

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

/* CaRT RPC handler run in PS leader to return pool storage ranks
 */
void
ds_pool_ranks_get_handler(crt_rpc_t *rpc)
{
	struct pool_ranks_get_in	*in = crt_req_get(rpc);
	struct pool_ranks_get_out	*out = crt_reply_get(rpc);
	uint32_t			 nranks = 0;
	d_rank_list_t			out_ranks = {0};
	struct pool_svc			*svc;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p\n", DP_UUID(in->prgi_op.pi_uuid), rpc);

	rc = pool_svc_lookup_leader(in->prgi_op.pi_uuid, &svc,
				    &out->prgo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	/* This is a server to server RPC only */
	if (daos_rpc_from_client(rpc))
		D_GOTO(out, rc = -DER_INVAL);

	/* Get available ranks */
	rc = ds_pool_get_ranks(in->prgi_op.pi_uuid, POOL_GROUP_MAP_STATUS, &out_ranks);
	if (rc != 0) {
		D_ERROR(DF_UUID ": get ranks failed, " DF_RC "\n",
			DP_UUID(in->prgi_op.pi_uuid), DP_RC(rc));
		D_GOTO(out_svc, rc);
	} else if ((in->prgi_nranks > 0) &&
		   (out_ranks.rl_nr > in->prgi_nranks)) {
		D_DEBUG(DB_MD, DF_UUID ": %u ranks (more than client: %u)\n",
			DP_UUID(in->prgi_op.pi_uuid), out_ranks.rl_nr,
			in->prgi_nranks);
		D_GOTO(out_free, rc = -DER_TRUNC);
	} else {
		D_DEBUG(DB_MD, DF_UUID ": %u ranks\n",
			DP_UUID(in->prgi_op.pi_uuid), out_ranks.rl_nr);
		if ((out_ranks.rl_nr > 0) && (in->prgi_nranks > 0) &&
		    (in->prgi_ranks_bulk != CRT_BULK_NULL))
			rc = transfer_ranks_buf(out_ranks.rl_ranks,
						out_ranks.rl_nr, svc, rpc,
						in->prgi_ranks_bulk);
	}

out_free:
	nranks = out_ranks.rl_nr;
	map_ranks_fini(&out_ranks);

out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->prgo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->prgo_op.po_rc = rc;
	out->prgo_nranks = nranks;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->prgi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

/* This RPC could be implemented by ds_rsvc. */
void
ds_pool_svc_stop_handler(crt_rpc_t *rpc)
{
	struct pool_svc_stop_in	       *in = crt_req_get(rpc);
	struct pool_svc_stop_out       *out = crt_reply_get(rpc);
	d_iov_t				id;
	int				rc;

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->psi_op.pi_uuid), rpc);

	d_iov_set(&id, in->psi_op.pi_uuid, sizeof(uuid_t));
	rc = ds_rsvc_stop_leader(DS_RSVC_CLASS_POOL, &id, &out->pso_op.po_hint);

	out->pso_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->psi_op.pi_uuid), rpc,
		DP_RC(rc));
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
ds_pool_iv_ns_update(struct ds_pool *pool, unsigned int master_rank,
		     uint64_t term)
{
	ds_iv_ns_update(pool->sp_iv_ns, master_rank, term);
}

int
ds_pool_svc_global_map_version_get(uuid_t uuid, uint32_t *version)
{
	struct pool_svc	*svc;
	int		rc;

	rc = pool_svc_lookup_leader(uuid, &svc, NULL /* hint */);
	if (rc != 0)
		return rc;

	*version = svc->ps_global_map_version;

	pool_svc_put_leader(svc);
	return 0;
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

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pasi_op.pi_uuid), rpc,
		DP_RC(rc));
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

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->padi_op.pi_uuid), rpc,
		DP_RC(rc));
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

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pagi_op.pi_uuid), rpc,
		DP_RC(rc));
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

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
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
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pali_op.pi_uuid), rpc,
		DP_RC(rc));
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
		rc = ds_rsvc_remove_replicas(DS_RSVC_CLASS_POOL, &id, ranks, &out->pmo_hint);
		break;

	default:
		D_ASSERT(0);
	}

	out->pmo_failed = ranks;
out:
	out->pmo_rc = rc;
	crt_reply_send(rpc);
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

/**
 * Is \a hdl a "server handle" for \a pool?
 *
 * \param[in]	pool	pool
 * \param[in]	hdl	pool handle UUID
 *
 * \return	1	yes
 *		0	no
 *		<0	error from the IV fetch
 */
int
ds_pool_hdl_is_from_srv(struct ds_pool *pool, uuid_t hdl)
{
	uuid_t	srv_hdl;
	int	rc;

	/*
	 * Use the cached value if available. (Not sure if this cache could be
	 * stale...)
	 */
	if (!uuid_is_null(pool->sp_srv_pool_hdl))
		return uuid_compare(pool->sp_srv_pool_hdl, hdl) == 0;

	rc = ds_pool_iv_srv_hdl_fetch(pool, &srv_hdl, NULL);
	if (rc != 0)
		return rc;

	return uuid_compare(srv_hdl, hdl) == 0;
}

static bool
is_pool_from_srv(uuid_t pool_uuid, uuid_t poh_uuid)
{
	struct ds_pool	*pool;
	int		rc;

	rc = ds_pool_lookup(pool_uuid, &pool);
	if (rc) {
		D_ERROR(DF_UUID": failed to get ds_pool: %d\n",
			DP_UUID(pool_uuid), rc);
		return false;
	}

	rc = ds_pool_hdl_is_from_srv(pool, poh_uuid);
	ds_pool_put(pool);
	if (rc < 0) {
		D_ERROR(DF_UUID" fetch srv hdl: %d\n", DP_UUID(pool_uuid), rc);
		return false;
	}

	return rc ? true : false;
}

int ds_pool_svc_upgrade(uuid_t pool_uuid, d_rank_list_t *ranks)
{
	int				rc;
	struct rsvc_client		client;
	crt_endpoint_t			ep;
	struct dss_module_info		*info = dss_get_module_info();
	crt_rpc_t			*rpc;
	struct pool_upgrade_in		*in;
	struct pool_upgrade_out		*out;

	D_DEBUG(DB_MGMT, DF_UUID": Upgrading pool prop\n", DP_UUID(pool_uuid));

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

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_UPGRADE, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool upgrade rpc: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->poi_op.pi_uuid, pool_uuid);
	uuid_clear(in->poi_op.pi_hdl);

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->poo_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		crt_req_decref(rpc);
		dss_sleep(RECHOOSE_SLEEP_MS);
		D_GOTO(rechoose, rc);
	}

	rc = out->poo_op.po_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to upgrade pool: %d\n",
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

/* Check if the target(by id) matched the status */
int
ds_pool_target_status_check(struct ds_pool *pool, uint32_t id, uint8_t matched_status,
			    struct pool_target **p_tgt)
{
	struct pool_target *target;
	int		   rc;

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = pool_map_find_target(pool->sp_map, id, &target);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc <= 0)
		return rc == 0 ? -DER_NONEXIST : rc;

	if (p_tgt)
		*p_tgt = target;

	return target->ta_comp.co_status == matched_status ? 1 : 0;
}

/**
 * A hack (since we don't take svc->ps_lock to avoid lock order issues with
 * cont_svc->cs_lock) for cont_svc to look up the credential of a pool handle
 * in the DB. If the return value is zero, the caller is responsible for
 * freeing \a cred->iov_buf with D_FREE.
 */
int
ds_pool_lookup_hdl_cred(struct rdb_tx *tx, uuid_t pool_uuid, uuid_t pool_hdl_uuid, d_iov_t *cred)
{
	struct pool_svc	       *svc;
	d_iov_t			key;
	d_iov_t			value;
	struct pool_hdl	       *hdl;
	void		       *buf;
	int			rc;

	rc = pool_svc_lookup_leader(pool_uuid, &svc, NULL /* hint */);
	if (rc != 0)
		goto out;

	if (svc->ps_global_version < DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED) {
		D_ERROR(DF_UUID": no credential in pool global version %u\n", DP_UUID(svc->ps_uuid),
			svc->ps_global_version);
		rc = -DER_NOTSUPPORTED;
		goto out_svc;
	}

	d_iov_set(&key, pool_hdl_uuid, sizeof(uuid_t));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(tx, &svc->ps_handles, &key, &value);
	if (rc != 0)
		goto out_svc;
	hdl = value.iov_buf;

	D_ALLOC(buf, hdl->ph_cred_len);
	if (buf == NULL) {
		rc = -DER_NOMEM;
		goto out_svc;
	}
	memcpy(buf, hdl->ph_cred, hdl->ph_cred_len);

	cred->iov_buf = buf;
	cred->iov_len = hdl->ph_cred_len;
	cred->iov_buf_len = hdl->ph_cred_len;

out_svc:
	pool_svc_put_leader(svc);
out:
	return rc;
}
