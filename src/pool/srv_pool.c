/*
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 * (C) Copyright 2025 Google LLC
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

#define DAOS_POOL_GLOBAL_VERSION_WITH_HDL_CRED    1
#define DAOS_POOL_GLOBAL_VERSION_WITH_SVC_OPS_KVS 3
#define DAOS_POOL_GLOBAL_VERSION_WITH_DATA_THRESH 3
#define DAOS_POOL_GLOBAL_VERSION_WITH_SRV_HDLS    4

#define PS_OPS_PER_SEC                            4096

/*
 * Return the corresponding VOS DF version or 0 if pool_global_version is not
 * supported.
 */
uint32_t
ds_pool_get_vos_df_version(uint32_t pool_global_version)
{
	if (pool_global_version == 4)
		return VOS_POOL_DF_2_8;
	if (pool_global_version == 3)
		return VOS_POOL_DF_2_6;
	else if (pool_global_version == 2)
		return VOS_POOL_DF_2_4;
	return 0;
}

/** Return the VOS DF version for the default pool global version. */
uint32_t
ds_pool_get_vos_df_version_default(void)
{
	uint32_t v = ds_pool_get_vos_df_version(DAOS_POOL_GLOBAL_VERSION);

	D_ASSERT(v != 0);
	return v;
}

#define DUP_OP_MIN_RDB_SIZE                       (1 << 30)

/* Pool service crt event */
struct pool_svc_event {
	d_rank_t		psv_rank;
	uint64_t		psv_incarnation;
	enum crt_event_source	psv_src;
	enum crt_event_type	psv_type;
};

#define DF_PS_EVENT	"rank=%u inc="DF_U64" src=%d type=%d"
#define DP_PS_EVENT(e)	e->psv_rank, e->psv_incarnation, e->psv_src, e->psv_type

/*
 * Pool service crt event set
 *
 * This stores an unordered array of pool_svc_event objects. For all different
 * i and j, we have pss_buf[i].psv_rank != pss_buf[j].psv_rank.
 *
 * An event set facilitates the merging of a sequence of events. For instance,
 * sequence (in the format <rank, type>)
 *   <3, D>, <5, D>, <1, D>, <5, A>, <1, A>, <1, D>
 * will merge into set
 *   <3, D>, <5, A>, <1, D>
 * (that is, during the merge, an event overrides a previuos event of the same
 * rank in the set).
 */
struct pool_svc_event_set {
	struct pool_svc_event *pss_buf;
	uint32_t               pss_len;
	uint32_t               pss_cap;
};

#define DF_PS_EVENT_SET    "len=%u"
#define DP_PS_EVENT_SET(s) s->pss_len

/* Pool service crt-event-handling state */
struct pool_svc_events {
	ABT_mutex                  pse_mutex;
	ABT_cond                   pse_cv;
	struct pool_svc_event_set *pse_pending;
	uint64_t                   pse_timeout; /* s */
	uint64_t                   pse_time;    /* s */
	struct sched_request      *pse_timer;
	ABT_thread                 pse_handler;
	bool                       pse_stop;
	bool                       pse_paused;
};

/* Pool service schedule state */
struct pool_svc_sched {
	ABT_mutex	psc_mutex;		/* only for psc_cv */
	ABT_cond	psc_cv;
	bool		psc_in_progress;
	bool		psc_canceled;
	void	       *psc_arg;
	int		psc_rc;
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

	sched->psc_in_progress = false;
	sched->psc_canceled = false;
	sched->psc_arg = NULL;
	sched->psc_rc = 0;
	return 0;
}

static void
sched_fini(struct pool_svc_sched *sched)
{
	ABT_cond_free(&sched->psc_cv);
	ABT_mutex_free(&sched->psc_mutex);
}

static void
sched_begin(struct pool_svc_sched *sched, void *arg)
{
	sched->psc_in_progress = true;
	sched->psc_canceled = false;
	sched->psc_arg = arg;
	sched->psc_rc = 0;
}

static void
sched_end(struct pool_svc_sched *sched)
{
	sched->psc_in_progress = false;
	sched->psc_canceled = false;
}

static void
sched_cancel(struct pool_svc_sched *sched)
{
	if (sched->psc_in_progress)
		sched->psc_canceled = true;
}

static void
sched_wait(struct pool_svc_sched *sched)
{
	/*
	 * The CV requires a mutex. We don't otherwise need it for ULTs within
	 * the same xstream.
	 */
	ABT_mutex_lock(sched->psc_mutex);
	while (sched->psc_in_progress)
		ABT_cond_wait(sched->psc_cv, sched->psc_mutex);
	ABT_mutex_unlock(sched->psc_mutex);
}

static void
sched_cancel_and_wait(struct pool_svc_sched *sched)
{
	sched_cancel(sched);
	sched_wait(sched);
}

struct pool_space_cache {
	struct daos_pool_space psc_space;
	uint64_t               psc_memfile_bytes;
	uint64_t               psc_timestamp;
	ABT_mutex              psc_lock;
};

/* Pool service */
struct pool_svc {
	struct ds_rsvc		ps_rsvc;
	uuid_t                  ps_uuid;     /* pool UUID */
	struct ds_pool	       *ps_pool;
	struct cont_svc        *ps_cont_svc; /* one combined svc for now */
	ABT_rwlock              ps_lock;     /* for DB data */
	rdb_path_t              ps_root;     /* root KVS */
	rdb_path_t              ps_handles;  /* pool handle KVS */
	rdb_path_t              ps_user;     /* pool user attributes KVS */
	rdb_path_t              ps_ops;      /* metadata ops KVS */
	int                     ps_error;    /* in DB data (see pool_svc_lookup_leader) */
	struct pool_svc_events	ps_events;
	struct pool_space_cache ps_space_cache;
	uint32_t		ps_global_version;
	int			ps_svc_rf;
	bool                    ps_force_notify; /* MS of PS membership */
	struct pool_svc_sched	ps_reconf_sched;
	struct pool_svc_sched   ps_rfcheck_sched;      /* Check all containers RF for the pool */
	uint32_t                ps_ops_enabled;        /* cached ds_pool_prop_svc_ops_enabled */
	uint32_t                ps_ops_max;            /* cached ds_pool_prop_svc_ops_max */
	uint32_t                ps_ops_age;            /* cached ds_pool_prop_svc_ops_age */
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
static int
	   pool_space_query_bcast(crt_context_t ctx, struct pool_svc *svc, uuid_t pool_hdl,
				  struct daos_pool_space *ps, uint64_t *mem_file_bytes);
static int
ds_pool_upgrade_if_needed(uuid_t pool_uuid, struct rsvc_hint *po_hint, struct pool_svc *svc,
			  crt_rpc_t *rpc, uuid_t srv_pool_hdl, uuid_t srv_cont_hdl);
static int
find_hdls_to_evict(struct rdb_tx *tx, struct pool_svc *svc, uuid_t **hdl_uuids,
		   size_t *hdl_uuids_size, int *n_hdl_uuids, char *machine);

static inline struct pool_svc *
pool_ds2svc(struct ds_pool_svc *ds_svc)
{
	return (struct pool_svc *)ds_svc;
}

static inline struct ds_pool_svc *
pool_svc2ds(struct pool_svc *svc)
{
	return (struct ds_pool_svc *)svc;
}

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
char *
ds_pool_svc_rdb_path(const uuid_t pool_uuid)
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
		case DAOS_PROP_PO_SVC_OPS_ENABLED:
		case DAOS_PROP_PO_SVC_OPS_ENTRY_AGE:
		case DAOS_PROP_PO_DATA_THRESH:
		case DAOS_PROP_PO_CHECKPOINT_MODE:
		case DAOS_PROP_PO_CHECKPOINT_THRESH:
		case DAOS_PROP_PO_CHECKPOINT_FREQ:
		case DAOS_PROP_PO_REINT_MODE:
			entry_def->dpe_val = entry->dpe_val;
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
		case DAOS_PROP_PO_DATA_THRESH:
			if (!daos_data_thresh_valid(entry->dpe_val)) {
				rc = -DER_INVAL;
				break;
			}
			d_iov_set(&value, &entry->dpe_val, sizeof(entry->dpe_val));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_data_thresh, &value);
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
		case DAOS_PROP_PO_REINT_MODE:
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_reint_mode,
					   &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_SVC_OPS_ENABLED:
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_ops_enabled, &value);
			if (rc)
				return rc;
			break;
		case DAOS_PROP_PO_SVC_OPS_ENTRY_AGE:
			val32 = entry->dpe_val;
			d_iov_set(&value, &val32, sizeof(val32));
			rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_ops_age, &value);
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
	uint32_t                svc_ops_enabled        = 1;
	/* max number of entries in svc_ops KVS: equivalent of max age (sec) x PS_OPS_PER_SEC */
	uint32_t                svc_ops_age = DAOS_PROP_PO_SVC_OPS_ENTRY_AGE_DEFAULT;
	uint32_t                svc_ops_max;
	uint32_t                svc_ops_num;
	uint64_t                rdb_size;
	int			rc;
	struct daos_prop_entry *entry;
	uuid_t                  uuid;

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
	if (rc != 0) {
		D_ERROR("failed to create user attr KVS, "DF_RC"\n", DP_RC(rc));
		goto out_map_buf;
	}

	/* Create pool service operations KVS */
	attr.dsa_class = RDB_KVS_LEXICAL;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_pool_prop_svc_ops, &attr);
	if (rc != 0) {
		D_ERROR("failed to create service ops KVS, " DF_RC "\n", DP_RC(rc));
		goto out_map_buf;
	}

	/* Determine if duplicate service operations detection will be enabled */
	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_OPS_ENABLED);
	if (entry)
		svc_ops_enabled = entry->dpe_val;
	if (svc_ops_enabled) {
		rc = rdb_get_size(tx->dt_db, &rdb_size);
		if (rc != 0)
			goto out_map_buf;
		if (rdb_size < DUP_OP_MIN_RDB_SIZE) {
			svc_ops_enabled = 0;
			D_WARN("pool duplicate ops detection disabled due to rdb size %zu < %u\n",
			       rdb_size, DUP_OP_MIN_RDB_SIZE);
		}
	}
	d_iov_set(&value, &svc_ops_enabled, sizeof(svc_ops_enabled));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_ops_enabled, &value);
	if (rc != 0) {
		DL_ERROR(rc, "failed to set svc_ops_enabled");
		goto out_map_buf;
	}

	/* Maximum number of RPCs that may be kept in svc_ops, from SVC_OPS_ENTRY_AGE property.
	 * Default: PS_OPS_PER_SEC x DEFAULT_SVC_OPS_ENTRY_AGE_SEC.
	 */
	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_OPS_ENTRY_AGE);
	if (entry)
		svc_ops_age = entry->dpe_val;
	svc_ops_max = PS_OPS_PER_SEC * svc_ops_age;
	svc_ops_num = 0;
	d_iov_set(&value, &svc_ops_age, sizeof(svc_ops_age));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_ops_age, &value);
	if (rc != 0) {
		DL_ERROR(rc, "failed to set svc_ops_age");
		goto out_map_buf;
	}
	d_iov_set(&value, &svc_ops_max, sizeof(svc_ops_max));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_ops_max, &value);
	if (rc != 0) {
		DL_ERROR(rc, "failed to set svc_ops_max");
		goto out_map_buf;
	}
	d_iov_set(&value, &svc_ops_num, sizeof(svc_ops_num));
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_svc_ops_num, &value);
	if (rc != 0) {
		DL_ERROR(rc, "failed to set svc_ops_num");
		goto out_map_buf;
	}

	d_iov_set(&value, uuid, sizeof(uuid_t));
	uuid_generate(uuid);
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_srv_handle, &value);
	if (rc != 0) {
		DL_ERROR(rc, "failed to write server pool handle");
		goto out_map_buf;
	}
	uuid_generate(uuid);
	rc = rdb_tx_update(tx, kvs, &ds_pool_prop_srv_cont_handle, &value);
	if (rc != 0)
		DL_ERROR(rc, "failed to write server container handle");

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
select_svc_ranks(int svc_rf, struct pool_buf *map_buf, uint32_t map_version,
		 d_rank_list_t **ranksp)
{
	struct pool_map *map;
	d_rank_list_t    replicas = {0};
	d_rank_list_t   *to_add;
	d_rank_list_t   *to_remove;
	int              rc;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0)
		return rc;

	rc = ds_pool_plan_svc_reconfs(svc_rf, map, &replicas, CRT_NO_RANK /* self */,
				      false /* filter_only */, &to_add, &to_remove);
	pool_map_decref(map);
	if (rc != 0)
		return rc;
	D_ASSERTF(to_remove->rl_nr == 0, "to_remove=%u\n", to_remove->rl_nr);
	d_rank_list_free(to_remove);

	d_rank_list_sort(to_add);

	*ranksp = to_add;
	return 0;
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
			d_rank_list_t *target_addrs, int ndomains, uint32_t *domains,
			daos_prop_t *prop, d_rank_list_t **svc_addrs)
{
	struct daos_prop_entry *svc_rf_entry;
	struct pool_buf	       *map_buf;
	uint32_t		map_version = 1;
	d_rank_list_t	       *ranks;
	d_iov_t			psid;
	struct rsvc_client	client;
	struct dss_module_info *info = dss_get_module_info();
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct daos_prop_entry *lbl_ent;
	struct daos_prop_entry *def_lbl_ent;
	struct pool_create_out *out;
	struct d_backoff_seq	backoff_seq;
	uuid_t                  pi_hdl_uuid;
	uint64_t                req_time   = 0;
	int			n_attempts = 0;
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

	rc = gen_pool_buf(NULL /* map */, &map_buf, map_version, ndomains, target_addrs->rl_nr,
			  target_addrs->rl_nr * dss_tgt_nr, domains, dss_tgt_nr);
	if (rc != 0)
		goto out;

	svc_rf_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_REDUN_FAC);
	D_ASSERT(svc_rf_entry != NULL && !(svc_rf_entry->dpe_flags & DAOS_PROP_ENTRY_NOT_SET));
	D_ASSERTF(daos_svc_rf_is_valid(svc_rf_entry->dpe_val), DF_U64"\n", svc_rf_entry->dpe_val);

	D_DEBUG(DB_MD, DF_UUID": creating PS: ntargets=%d ndomains=%d svc_rf="DF_U64"\n",
		DP_UUID(pool_uuid), ntargets, ndomains, svc_rf_entry->dpe_val);

	rc = select_svc_ranks(svc_rf_entry->dpe_val, map_buf, map_version, &ranks);
	if (rc != 0)
		goto out_map_buf;

	d_iov_set(&psid, (void *)pool_uuid, sizeof(uuid_t));
	rc = ds_rsvc_dist_start(DS_RSVC_CLASS_POOL, &psid, pool_uuid, ranks, RDB_NIL_TERM,
				DS_RSVC_CREATE, true /* bootstrap */, ds_rsvc_get_md_cap(),
				ds_pool_get_vos_df_version_default());
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
	if (n_attempts == 0)
		/*
		 * This is our first attempt. Use a non-null pi_hdl to ask the
		 * chosen PS replica to campaign.
		 */
		uuid_generate(pi_hdl_uuid);
	else
		uuid_clear(pi_hdl_uuid);

	rc = pool_req_create(info->dmi_ctx, &ep, POOL_CREATE, pool_uuid, pi_hdl_uuid, &req_time,
			     &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create POOL_CREATE RPC", DP_UUID(pool_uuid));
		goto out_backoff_seq;
	}
	/* We could send map_buf to simplify things. */
	pool_create_in_set_data(rpc, target_addrs, prop, ndomains, ntargets, domains);

	/* Send the POOL_CREATE request. */
	rc = dss_rpc_send(rpc);
	n_attempts++;
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
out_map_buf:
	D_FREE(map_buf);
out:
	return rc;
}

/** Start any local PS replica for \a uuid. */
int
ds_pool_svc_start(uuid_t uuid)
{
	char       *path;
	struct stat st;
	d_iov_t     id;
	int         rc;

	/*
	 * Check if an RDB file exists, to avoid unnecessary error messages
	 * from the ds_rsvc_start() call.
	 */
	path = ds_pool_svc_rdb_path(uuid);
	if (path == NULL) {
		D_ERROR(DF_UUID ": failed to allocate pool service path\n", DP_UUID(uuid));
		return -DER_NOMEM;
	}
	rc = stat(path, &st);
	D_FREE(path);
	if (rc != 0) {
		rc = errno;
		if (rc == ENOENT) {
			D_DEBUG(DB_MD, DF_UUID ": no pool service file\n", DP_UUID(uuid));
			return 0;
		}
		D_ERROR(DF_UUID ": failed to stat pool service file: %d\n", DP_UUID(uuid), rc);
		return daos_errno2der(rc);
	}

	d_iov_set(&id, uuid, sizeof(uuid_t));
	rc = ds_rsvc_start(DS_RSVC_CLASS_POOL, &id, uuid, RDB_NIL_TERM, DS_RSVC_START, 0 /* size */,
			   0 /* vos_df_version */, NULL /* replicas */, NULL /* arg */);
	if (rc == -DER_ALREADY) {
		D_DEBUG(DB_MD, DF_UUID": pool service already started\n", DP_UUID(uuid));
		return 0;
	} else if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to start pool service", DP_UUID(uuid));
		return rc;
	}

	return 0;
}

/** Stop any local PS replica for \a pool_uuid. */
int
ds_pool_svc_stop(uuid_t pool_uuid)
{
	d_iov_t	id;

	d_iov_set(&id, pool_uuid, sizeof(uuid_t));
	return ds_rsvc_stop(DS_RSVC_CLASS_POOL, &id, RDB_NIL_TERM, false /* destroy */);
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
	s = ds_pool_svc_rdb_path(id->iov_buf);
	if (s == NULL)
		return -DER_NOMEM;
	*path = s;
	return 0;
}

static unsigned int
get_crt_event_delay(void)
{
	unsigned int t = 10 /* s */;

	d_getenv_uint("CRT_EVENT_DELAY", &t);
	return t;
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
	svc->ps_events.pse_timeout = get_crt_event_delay();
	svc->ps_events.pse_handler = ABT_THREAD_NULL;
	svc->ps_svc_rf = -1;
	svc->ps_force_notify = false;

	rc = ds_pool_lookup(svc->ps_uuid, &svc->ps_pool);
	if (rc != 0) {
		DL_INFO(rc, DF_UUID ": look up pool", DP_UUID(svc->ps_uuid));
		goto err_svc;
	}

	rc = ABT_rwlock_create(&svc->ps_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ps_lock: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_pool;
	}

	rc = ABT_mutex_create(&svc->ps_space_cache.psc_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create psc_lock: %d\n", rc);
		rc = dss_abterr2der(rc);
		goto err_lock;
	}

	rc = rdb_path_init(&svc->ps_root);
	if (rc != 0)
		goto err_psc_lock;
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
	rc = rdb_path_clone(&svc->ps_root, &svc->ps_ops);
	if (rc != 0)
		goto err_user;
	rc = rdb_path_push(&svc->ps_ops, &ds_pool_prop_svc_ops);
	if (rc != 0)
		goto err_svcops;

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
err_svcops:
	rdb_path_fini(&svc->ps_ops);
err_user:
	rdb_path_fini(&svc->ps_user);
err_handles:
	rdb_path_fini(&svc->ps_handles);
err_root:
	rdb_path_fini(&svc->ps_root);
err_psc_lock:
	ABT_mutex_free(&svc->ps_space_cache.psc_lock);
err_lock:
	ABT_rwlock_free(&svc->ps_lock);
err_pool:
	ds_pool_put(svc->ps_pool);
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
alloc_event_set(struct pool_svc_event_set **event_set)
{
	D_ALLOC_PTR(*event_set);
	if (*event_set == NULL)
		return -DER_NOMEM;
	return 0;
}

static void
free_event_set(struct pool_svc_event_set **event_set)
{
	D_FREE((*event_set)->pss_buf);
	D_FREE(*event_set);
}

static int
add_to_event_set(struct pool_svc_event_set *event_set, d_rank_t rank, uint64_t incarnation,
		 enum crt_event_source src, enum crt_event_type type)
{
	int i;

	/* Find rank in event_set. */
	for (i = 0; i < event_set->pss_len; i++)
		if (event_set->pss_buf[i].psv_rank == rank)
			break;

	/* If not found, prepare to add a new event. */
	if (i == event_set->pss_len) {
		if (event_set->pss_len == event_set->pss_cap) {
			uint32_t               cap;
			struct pool_svc_event *buf;

			if (event_set->pss_cap == 0)
				cap = 1;
			else
				cap = 2 * event_set->pss_cap;
			D_REALLOC_ARRAY(buf, event_set->pss_buf, event_set->pss_cap, cap);
			if (buf == NULL)
				return -DER_NOMEM;
			event_set->pss_buf = buf;
			event_set->pss_cap = cap;
		}
		event_set->pss_len++;
	}

	event_set->pss_buf[i].psv_rank        = rank;
	event_set->pss_buf[i].psv_incarnation = incarnation;
	event_set->pss_buf[i].psv_src         = src;
	event_set->pss_buf[i].psv_type        = type;
	return 0;
}

/* Merge next into prev. */
static int
merge_event_sets(struct pool_svc_event_set *prev, struct pool_svc_event_set *next)
{
	int i;

	for (i = 0; i < next->pss_len; i++) {
		struct pool_svc_event *event = &next->pss_buf[i];
		int                    rc;

		rc = add_to_event_set(prev, event->psv_rank, event->psv_incarnation, event->psv_src,
				      event->psv_type);
		if (rc != 0)
			return rc;
	}
	return 0;
}

static int
queue_event(struct pool_svc *svc, d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
	    enum crt_event_type type)
{
	struct pool_svc_events *events = &svc->ps_events;
	int                     rc;
	bool                    allocated = false;

	D_DEBUG(DB_MD, DF_UUID ": queuing event: " DF_PS_EVENT "\n", DP_UUID(svc->ps_uuid), rank,
		incarnation, src, type);

	ABT_mutex_lock(events->pse_mutex);

	if (events->pse_pending == NULL) {
		rc = alloc_event_set(&events->pse_pending);
		if (rc != 0)
			goto out;
		allocated = true;
	}

	rc = add_to_event_set(events->pse_pending, rank, incarnation, src, type);
	if (rc != 0)
		goto out;

	events->pse_time = daos_gettime_coarse();

	if (events->pse_paused) {
		D_DEBUG(DB_MD, DF_UUID ": resuming event handling\n", DP_UUID(svc->ps_uuid));
		events->pse_paused = false;
	}

	ABT_cond_broadcast(events->pse_cv);

out:
	if (rc != 0 && allocated)
		free_event_set(&events->pse_pending);
	ABT_mutex_unlock(events->pse_mutex);
	return rc;
}

static void
resume_event_handling(struct pool_svc *svc)
{
	struct pool_svc_events *events = &svc->ps_events;

	ABT_mutex_lock(events->pse_mutex);
	if (events->pse_paused) {
		D_DEBUG(DB_MD, DF_UUID ": resuming event handling\n", DP_UUID(svc->ps_uuid));
		events->pse_paused = false;
		ABT_cond_broadcast(events->pse_cv);
	}
	ABT_mutex_unlock(events->pse_mutex);
}

/*
 * Restart rebuild if the rank is UPIN in pool map and is in rebuilding.
 *
 * This function only used when PS leader gets CRT_EVT_ALIVE event of engine \a rank,
 * if that rank is UPIN in pool map and with unfinished rebuilding should be massive
 * failure case -
 * 1. some engines down and triggered rebuild.
 * 2. the engine \a rank participated the rebuild, not finished yet, it became down again,
 *    the #failures exceeds pool RF and will not change pool map.
 * 3. That engine restarted by administrator.
 *
 * In that case should recover the rebuild task on engine \a rank, to simplify it now just
 * abort and retry the global rebuild task.
 */
static void
pool_restart_rebuild_if_rank_wip(struct ds_pool *pool, d_rank_t rank)
{
	struct pool_domain	*dom;

	dom = pool_map_find_dom_by_rank(pool->sp_map, rank);
	if (dom == NULL) {
		D_DEBUG(DB_MD, DF_UUID": rank %d non-exist on pool map.\n",
			DP_UUID(pool->sp_uuid), rank);
		return;
	}

	if (dom->do_comp.co_status != PO_COMP_ST_UPIN) {
		D_INFO(DF_UUID": rank %d status %d in pool map, got CRT_EVT_ALIVE.\n",
		       DP_UUID(pool->sp_uuid), rank, dom->do_comp.co_status);
		return;
	}

	ds_rebuild_restart_if_rank_wip(pool->sp_uuid, rank);

	return;
}

static int pool_svc_exclude_ranks(struct pool_svc *svc, struct pool_svc_event_set *event_set);

static int
handle_event(struct pool_svc *svc, struct pool_svc_event_set *event_set)
{
	int i;
	int rc;

	D_INFO(DF_UUID ": handling event set: " DF_PS_EVENT_SET "\n", DP_UUID(svc->ps_uuid),
	       DP_PS_EVENT_SET(event_set));

	if (!pool_disable_exclude) {
		rc = pool_svc_exclude_ranks(svc, event_set);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to exclude ranks", DP_UUID(svc->ps_uuid));
			return rc;
		}
	}

	/*
	 * Check if the alive ranks are up in the pool map. If in the future we
	 * add automatic reintegration below, for instance, we may need
	 * to not only take svc->ps_lock, but also employ an RDB TX by
	 * the book.
	 */
	ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
	for (i = 0; i < event_set->pss_len; i++) {
		struct pool_svc_event *event = &event_set->pss_buf[i];

		if (event->psv_type != CRT_EVT_ALIVE)
			continue;

		D_DEBUG(DB_MD, DF_UUID ": got CRT_EVT_ALIVE event, psv_src %d, psv_rank %d\n",
		       DP_UUID(svc->ps_uuid), event->psv_src, event->psv_rank);
		pool_restart_rebuild_if_rank_wip(svc->ps_pool, event->psv_rank);

		if (ds_pool_map_rank_up(svc->ps_pool->sp_map, event->psv_rank)) {
			/*
			 * The rank is up in the pool map. Request a pool map
			 * distribution just in case the rank has recently
			 * restarted and does not have a copy of the pool map.
			 */
			ds_rsvc_request_map_dist(&svc->ps_rsvc);
			D_DEBUG(DB_MD, DF_UUID ": requested map dist for rank %u\n",
				DP_UUID(svc->ps_uuid), event->psv_rank);
			break;
		}
	}
	ABT_rwlock_unlock(svc->ps_pool->sp_lock);

	return 0;
}

struct event_timer_arg {
	struct pool_svc_events *eta_events;
	uint64_t                eta_deadline;
};

static void
event_timer(void *varg)
{
	struct event_timer_arg *arg = varg;
	struct pool_svc_events *events = arg->eta_events;
	int64_t                 time_left = arg->eta_deadline - daos_gettime_coarse();

	if (time_left > 0)
		sched_req_sleep(events->pse_timer, time_left * 1000);
	ABT_cond_broadcast(events->pse_cv);
}

static int
start_event_timer(struct event_timer_arg *arg)
{
	struct pool_svc_events *events = arg->eta_events;
	uuid_t                  uuid;
	struct sched_req_attr   attr;

	D_ASSERT(events->pse_timer == NULL);
	uuid_clear(uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &uuid);
	events->pse_timer = sched_create_ult(&attr, event_timer, arg, 0);
	if (events->pse_timer == NULL)
		return -DER_NOMEM;
	return 0;
}

static void
stop_event_timer(struct event_timer_arg *arg)
{
	struct pool_svc_events *events = arg->eta_events;

	D_ASSERT(events->pse_timer != NULL);
	sched_req_wait(events->pse_timer, true /* abort */);
	sched_req_put(events->pse_timer);
	events->pse_timer = NULL;
}

static void
events_handler(void *arg)
{
	struct pool_svc	       *svc = arg;
	struct pool_svc_events *events = &svc->ps_events;

	D_DEBUG(DB_MD, DF_UUID": starting\n", DP_UUID(svc->ps_uuid));

	for (;;) {
		struct pool_svc_event_set *event_set = NULL;
		bool                       stop;
		int                        rc;

		ABT_mutex_lock(events->pse_mutex);
		for (;;) {
			struct event_timer_arg timer_arg;
			int64_t                time_left;

			stop = events->pse_stop;
			if (stop) {
				events->pse_paused = false;
				if (events->pse_pending != NULL)
					free_event_set(&events->pse_pending);
				break;
			}

			timer_arg.eta_events   = events;
			timer_arg.eta_deadline = events->pse_time + events->pse_timeout;

			time_left = timer_arg.eta_deadline - daos_gettime_coarse();
			if (events->pse_pending != NULL && !events->pse_paused && time_left <= 0) {
				event_set = events->pse_pending;
				events->pse_pending = NULL;
				break;
			}

			/* A simple timed cond_wait without polling. */
			if (time_left > 0) {
				rc = start_event_timer(&timer_arg);
				if (rc != 0) {
					/* No delay then. */
					DL_ERROR(rc, DF_UUID ": failed to start event timer",
						 DP_UUID(svc->ps_uuid));
					events->pse_time = 0;
					continue;
				}
			}
			sched_cond_wait(events->pse_cv, events->pse_mutex);
			if (time_left > 0)
				stop_event_timer(&timer_arg);
		}
		ABT_mutex_unlock(events->pse_mutex);
		if (stop)
			break;

		rc = handle_event(svc, event_set);
		if (rc != 0) {
			/* Put event_set back to events->pse_pending. */
			D_DEBUG(DB_MD, DF_UUID ": returning event set\n", DP_UUID(svc->ps_uuid));
			ABT_mutex_lock(events->pse_mutex);
			if (events->pse_pending == NULL) {
				/*
				 * No pending events; pause the handling until
				 * next event or pool map change.
				 */
				D_DEBUG(DB_MD, DF_UUID ": pausing event handling\n",
					DP_UUID(svc->ps_uuid));
				events->pse_paused = true;
			} else {
				/*
				 * There are pending events; do not pause the
				 * handling.
				 */
				rc = merge_event_sets(event_set, events->pse_pending);
				if (rc != 0)
					DL_ERROR(rc, DF_UUID ": failed to merge events",
						 DP_UUID(svc->ps_uuid));
				free_event_set(&events->pse_pending);
			}
			events->pse_pending = event_set;
			event_set = NULL;
			ABT_mutex_unlock(events->pse_mutex);
		}

		if (event_set != NULL)
			free_event_set(&event_set);
		ABT_thread_yield();
	}

	D_DEBUG(DB_MD, DF_UUID": stopping\n", DP_UUID(svc->ps_uuid));
}

static bool
events_pending(struct pool_svc *svc)
{
	return svc->ps_events.pse_pending != NULL;
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

	D_ASSERT(events->pse_pending == NULL);
	D_ASSERT(events->pse_timer == NULL);
	D_ASSERT(events->pse_handler == ABT_THREAD_NULL);
	D_ASSERT(!events->pse_stop);
	D_ASSERT(!events->pse_paused);

	if (!ds_pool_restricted(svc->ps_pool, false)) {
		rc = crt_register_event_cb(ds_pool_crt_event_cb, svc);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to register event callback: "DF_RC"\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
			goto err;
		}
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
	if (!ds_pool_restricted(svc->ps_pool, false))
		crt_unregister_event_cb(ds_pool_crt_event_cb, svc);
	if (events->pse_pending != NULL)
		free_event_set(&events->pse_pending);
err:
	return rc;
}

static void
fini_events(struct pool_svc *svc)
{
	struct pool_svc_events *events = &svc->ps_events;

	D_ASSERT(events->pse_handler != ABT_THREAD_NULL);

	if (!ds_pool_restricted(svc->ps_pool, false))
		crt_unregister_event_cb(ds_pool_crt_event_cb, svc);

	ABT_mutex_lock(events->pse_mutex);
	events->pse_stop = true;
	ABT_cond_broadcast(events->pse_cv);
	ABT_mutex_unlock(events->pse_mutex);

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
	rdb_path_fini(&svc->ps_ops);
	rdb_path_fini(&svc->ps_user);
	rdb_path_fini(&svc->ps_handles);
	rdb_path_fini(&svc->ps_root);
	ABT_rwlock_free(&svc->ps_lock);
	ds_pool_put(svc->ps_pool);
	D_FREE(svc);
}

/*
 * Update svc->ps_pool with map_buf and map_version. This ensures that
 * svc->ps_pool matches the latest pool map.
 */
static int
update_svc_pool(struct pool_svc *svc, struct pool_buf *map_buf, uint32_t map_version, uint64_t term)
{
	int rc;

	rc = ds_pool_tgt_map_update(svc->ps_pool, map_buf, map_version);
	if (rc != 0)
		return rc;
	ds_pool_iv_ns_update(svc->ps_pool, dss_self_rank(), term);
	return 0;
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
 * Check the layout versions and read the pool map. If the DB is empty, return
 * positive error number DER_UNINIT. If the return value is 0, the caller is
 * responsible for freeing *map_buf_out with D_FREE eventually.
 */
int
ds_pool_svc_load(struct rdb_tx *tx, uuid_t uuid, rdb_path_t *root, uint32_t *global_version_out,
		 struct pool_buf **map_buf_out, uint32_t *map_version_out)
{
	uuid_t			uuid_tmp;
	d_iov_t			value;
	uint32_t		global_version;
	struct pool_buf	       *map_buf;
	uint32_t		map_version;
	bool                    version_exists  = false;
	int			rc;

	/*
	 * For the ds_notify_ras_eventf calls below, use a copy to avoid
	 * casting the uuid pointer.
	 */
	uuid_copy(uuid_tmp, uuid);

	/* Check the layout version. */
	d_iov_set(&value, &global_version, sizeof(global_version));
	rc = rdb_tx_lookup(tx, root, &ds_pool_prop_global_version, &value);
	if (rc == -DER_NONEXIST) {
		/*
		 * This DB may be new or incompatible. Check the existence of
		 * the pool map to find out which is the case. (See the
		 * references to version_exists below.)
		 */
		D_DEBUG(DB_MD, DF_UUID": no layout version\n", DP_UUID(uuid));
		goto check_map;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to look up layout version: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto out;
	}
	D_INFO(DF_UUID ": layout version %u\n", DP_UUID(uuid), global_version);
	version_exists = true;

	/**
	 * downgrading the DAOS software of an upgraded pool report
	 * a proper RAS error.
	 */
	if (global_version > DAOS_POOL_GLOBAL_VERSION) {
		ds_notify_ras_eventf(RAS_POOL_DF_INCOMPAT, RAS_TYPE_INFO,
				     RAS_SEV_ERROR, NULL /* hwid */,
				     NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */,
				     &uuid_tmp, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */,
				     NULL /* data */,
				     "incompatible layout version: %u larger than "
				     "%u", global_version,
				     DAOS_POOL_GLOBAL_VERSION);
		rc = -DER_DF_INCOMPT;
		goto out;
	}

check_map:
	rc = read_map_buf(tx, root, &map_buf, &map_version);
	if (rc != 0) {
		if (rc == -DER_NONEXIST && !version_exists) {
			/*
			 * This DB is new. Note that if the layout version
			 * exists, then the pool map must also exist;
			 * otherwise, it is an error.
			 */
			D_DEBUG(DB_MD, DF_UUID": new db\n", DP_UUID(uuid));
			rc = DER_UNINIT; /* positive error number */
		} else {
			D_ERROR(DF_UUID": failed to read pool map buffer: "DF_RC"\n",
				DP_UUID(uuid), DP_RC(rc));
		}
		goto out;
	}

	if (!version_exists)
		/* This could also be a 1.x pool, which we assume nobody cares. */
		D_DEBUG(DB_MD, DF_UUID": assuming 2.0\n", DP_UUID(uuid));

	D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	*global_version_out = global_version;
	*map_buf_out = map_buf;
	*map_version_out = map_version;
out:
	return rc;
}

/*
 * Read the DB for map_buf, map_version, and prop. If the return value is 0,
 * the caller is responsible for freeing *map_buf_out and *prop_out eventually.
 */
static int
read_db_for_stepping_up(struct pool_svc *svc, struct pool_buf **map_buf_out,
			uint32_t *map_version_out, daos_prop_t **prop_out, uuid_t srv_pool_hdl,
			uuid_t srv_cont_hdl)
{
	struct rdb_tx		tx;
	d_iov_t			value;
	struct pool_buf	       *map_buf;
	struct daos_prop_entry *svc_rf_entry;
	daos_prop_t	       *prop = NULL;
	uint32_t                map_version;
	int                     rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_rdlock(svc->ps_lock);

	rc = ds_pool_svc_load(&tx, svc->ps_uuid, &svc->ps_root, &svc->ps_global_version, &map_buf,
			      &map_version);
	if (rc != 0) {
		if (rc == -DER_DF_INCOMPT)
			svc->ps_error = rc;
		goto out_lock;
	}

	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ALL, &prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read pool properties: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		daos_prop_free(prop);
		goto out_map_buf;
	}

	svc_rf_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_REDUN_FAC);
	D_ASSERT(svc_rf_entry != NULL);
	if (daos_prop_is_set(svc_rf_entry))
		svc->ps_svc_rf = svc_rf_entry->dpe_val;
	else
		svc->ps_svc_rf = -1;

	if (svc->ps_global_version >= DAOS_POOL_GLOBAL_VERSION_WITH_SVC_OPS_KVS) {
		uint64_t rdb_size;
		bool     rdb_size_ok;

		/* Check if duplicate operations detection is enabled, for informative debug log */
		rc = rdb_get_size(svc->ps_rsvc.s_db, &rdb_size);
		if (rc != 0)
			goto out_map_buf;
		rdb_size_ok = (rdb_size >= DUP_OP_MIN_RDB_SIZE);

		d_iov_set(&value, &svc->ps_ops_enabled, sizeof(svc->ps_ops_enabled));
		rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_svc_ops_enabled, &value);
		if (rc != 0) {
			D_ERROR(DF_UUID ": failed to lookup svc_ops_enabled: " DF_RC "\n",
				DP_UUID(svc->ps_uuid), DP_RC(rc));
			goto out_map_buf;
		}

		d_iov_set(&value, &svc->ps_ops_age, sizeof(svc->ps_ops_age));
		rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_svc_ops_age, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to lookup svc_ops_age",
				 DP_UUID(svc->ps_uuid));
			goto out_map_buf;
		}

		d_iov_set(&value, &svc->ps_ops_max, sizeof(svc->ps_ops_max));
		rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_svc_ops_max, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to lookup svc_ops_max",
				 DP_UUID(svc->ps_uuid));
			goto out_map_buf;
		}

		D_DEBUG(DB_MD,
			DF_UUID ": duplicate ops detection %s (rdb size " DF_U64 " %s %u minimum), "
				"max entries %u, max entry age %u sec\n",
			DP_UUID(svc->ps_uuid), svc->ps_ops_enabled ? "enabled" : "disabled",
			rdb_size, rdb_size_ok ? ">=" : "<", DUP_OP_MIN_RDB_SIZE, svc->ps_ops_max,
			svc->ps_ops_age);
	} else {
		svc->ps_ops_enabled = 0;
		svc->ps_ops_age     = 0;
		svc->ps_ops_max     = 0;
		D_DEBUG(DB_MD, DF_UUID ": duplicate ops detection unavailable\n",
			DP_UUID(svc->ps_uuid));
	}

	if (svc->ps_global_version >= DAOS_POOL_GLOBAL_VERSION_WITH_SRV_HDLS) {
		d_iov_set(&value, srv_pool_hdl, sizeof(uuid_t));
		rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_srv_handle, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to look up server pool handle",
				 DP_UUID(svc->ps_uuid));
			goto out_map_buf;
		}
		if (uuid_is_null(srv_pool_hdl)) {
			D_ERROR(DF_UUID ": null server pool handle\n", DP_UUID(svc->ps_uuid));
			rc = -DER_IO;
			goto out_map_buf;
		}
		d_iov_set(&value, srv_cont_hdl, sizeof(uuid_t));
		rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_srv_cont_handle, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to look up server container handle",
				 DP_UUID(svc->ps_uuid));
			goto out_map_buf;
		}
		if (uuid_is_null(srv_cont_hdl)) {
			D_ERROR(DF_UUID ": null server container handle\n", DP_UUID(svc->ps_uuid));
			rc = -DER_IO;
			goto out_map_buf;
		}
	} else {
		uuid_clear(srv_pool_hdl);
		uuid_clear(srv_cont_hdl);
	}

	D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
	*map_buf_out = map_buf;
	*map_version_out = map_version;
	*prop_out = prop;

out_map_buf:
	if (rc != 0)
		D_FREE(map_buf);
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
	doms_cnt = pool_map_find_ranks(svc->ps_pool->sp_map, PO_COMP_ID_ALL,
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
 * Log as well as print a message. Arguments may be evaluated more
 * than once.
 */
#define DS_POOL_LOG_PRINT(log, fmt, ...) do {							\
	D_##log(fmt, ## __VA_ARGS__);								\
	D_PRINT(fmt, ## __VA_ARGS__);								\
} while (0)

static void
pool_svc_update_map_metrics(uuid_t uuid, struct pool_map *map, struct pool_metrics *metrics)
{
	unsigned int   num_total    = 0;
	unsigned int   num_enabled  = 0;
	unsigned int   num_draining = 0;
	unsigned int   num_disabled = 0;
	d_rank_list_t *ranks;
	int            rc;

	D_ASSERT(map != NULL && metrics != NULL);

	rc = pool_map_find_failed_tgts(map, NULL, &num_disabled);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get failed targets", DP_UUID(uuid));
		return;
	}
	d_tm_set_gauge(metrics->disabled_targets, num_disabled);

	rc = pool_map_find_tgts_by_state(map, PO_COMP_ST_DRAIN, NULL, &num_draining);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get draining targets", DP_UUID(uuid));
		return;
	}
	d_tm_set_gauge(metrics->draining_targets, num_draining);

	rc = pool_map_find_tgts_by_state(map, -1, NULL, &num_total);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get total targets", DP_UUID(uuid));
		return;
	}
	d_tm_set_gauge(metrics->total_targets, num_total);

	rc = pool_map_get_ranks(uuid, map, false, &ranks);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get degraded ranks", DP_UUID(uuid));
		return;
	}
	num_disabled = ranks->rl_nr;
	d_tm_set_gauge(metrics->degraded_ranks, num_disabled);
	d_rank_list_free(ranks);

	rc = pool_map_get_ranks(uuid, map, true, &ranks);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get enabled ranks", DP_UUID(uuid));
		return;
	}
	num_enabled = ranks->rl_nr;
	d_tm_set_gauge(metrics->total_ranks, num_enabled + num_disabled);
	d_rank_list_free(ranks);
}

static int
count_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *varg)
{
	uint64_t *counter = varg;

	if (counter == NULL)
		return -DER_INVAL;
	*counter = *counter + 1;

	return 0;
}

static int
pool_svc_step_up_metrics(struct pool_svc *svc, d_rank_t leader, uint32_t map_version,
			 struct pool_buf *map_buf)
{
	struct pool_map     *map;
	struct pool_metrics *metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
	struct rdb_tx        tx;
	uint64_t             handle_count = 0;
	int                  rc;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create pool map", DP_UUID(svc->ps_uuid));
		D_GOTO(out, rc);
	}

	d_tm_set_gauge(metrics->service_leader, leader);
	d_tm_set_counter(metrics->map_version, map_version);

	pool_svc_update_map_metrics(svc->ps_uuid, map, metrics);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get rdb transaction", DP_UUID(svc->ps_uuid));
		D_GOTO(out_map, rc);
	}

	rc = rdb_tx_iterate(&tx, &svc->ps_handles, false, count_iter_cb, &handle_count);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to count open pool handles", DP_UUID(svc->ps_uuid));
		D_GOTO(out_tx, rc);
	}
	d_tm_set_gauge(metrics->open_handles, handle_count);

out_tx:
	rdb_tx_end(&tx);
out_map:
	pool_map_decref(map);
out:
	return rc;
}

static void
pool_svc_step_down_metrics(struct pool_svc *svc)
{
	struct pool_metrics *metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];

	/* NB: zero these out to indicate that this rank is not leader */
	d_tm_set_gauge(metrics->service_leader, 0);
	d_tm_set_counter(metrics->map_version, 0);
	d_tm_set_gauge(metrics->open_handles, 0);
	d_tm_set_gauge(metrics->draining_targets, 0);
	d_tm_set_gauge(metrics->disabled_targets, 0);
	d_tm_set_gauge(metrics->total_targets, 0);
	d_tm_set_gauge(metrics->degraded_ranks, 0);
	d_tm_set_gauge(metrics->total_ranks, 0);
}

static int pool_svc_schedule(struct pool_svc *svc, struct pool_svc_sched *sched,
			     void (*func)(void *), void *arg);
static int pool_svc_schedule_reconf(struct pool_svc *svc, struct pool_map *map,
				    uint32_t map_version_for, bool sync_remove);
static void pool_svc_rfcheck_ult(void *arg);

static int
pool_svc_step_up_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc	       *svc = pool_svc_obj(rsvc);
	struct pool_buf	       *map_buf = NULL;
	uint32_t		map_version = 0;
	uuid_t                  srv_pool_hdl;
	uuid_t                  srv_cont_hdl;
	daos_prop_t	       *prop = NULL;
	bool			cont_svc_up = false;
	bool			events_initialized = false;
	d_rank_t		rank = dss_self_rank();
	int			rc;

	D_ASSERTF(svc->ps_error == 0, "ps_error: " DF_RC "\n", DP_RC(svc->ps_error));

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

	rc =
	    read_db_for_stepping_up(svc, &map_buf, &map_version, &prop, srv_pool_hdl, srv_cont_hdl);
	if (rc != 0)
		goto out;

	rc = update_svc_pool(svc, map_buf, map_version, svc->ps_rsvc.s_term);
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
	svc->ps_force_notify = true;
	rc = pool_svc_schedule_reconf(svc, NULL /* map */, map_version, false /* sync_remove */);
	if (rc == -DER_OP_CANCELED) {
		DL_INFO(rc, DF_UUID": not scheduling pool service reconfiguration",
			DP_UUID(svc->ps_uuid));
		rc = 0;
	} else if (rc != 0) {
		DL_ERROR(rc, DF_UUID": failed to schedule pool service reconfiguration",
			 DP_UUID(svc->ps_uuid));
		goto out;
	}

	rc = pool_svc_schedule(svc, &svc->ps_rfcheck_sched, pool_svc_rfcheck_ult, NULL);
	if (rc == -DER_OP_CANCELED) {
		DL_INFO(rc, DF_UUID ": not scheduling RF check", DP_UUID(svc->ps_uuid));
		rc = 0;
	} else if (rc != 0) {
		DL_ERROR(rc, DF_UUID": failed to schedule RF check", DP_UUID(svc->ps_uuid));
		goto out;
	}

	rc = ds_pool_iv_prop_update(svc->ps_pool, prop);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": ds_pool_iv_prop_update failed", DP_UUID(svc->ps_uuid));
		goto out;
	}

	if (svc->ps_global_version >= DAOS_POOL_GLOBAL_VERSION_WITH_SRV_HDLS) {
		/* See the is_pool_from_srv comment in the "else" branch. */
		if (uuid_is_null(svc->ps_pool->sp_srv_pool_hdl))
			uuid_copy(svc->ps_pool->sp_srv_pool_hdl, srv_pool_hdl);
	} else {
		if (!uuid_is_null(svc->ps_pool->sp_srv_cont_hdl)) {
			uuid_copy(srv_pool_hdl, svc->ps_pool->sp_srv_pool_hdl);
			uuid_copy(srv_cont_hdl, svc->ps_pool->sp_srv_cont_hdl);
		} else {
			uuid_generate(srv_pool_hdl);
			uuid_generate(srv_cont_hdl);
			/* Only copy server handle to make is_pool_from_srv() check correctly, and
			 * container server handle will not be copied here, otherwise
			 * ds_pool_iv_refresh_hdl will not open the server container handle.
			 */
			uuid_copy(svc->ps_pool->sp_srv_pool_hdl, srv_pool_hdl);
		}
	}

	rc = ds_pool_iv_srv_hdl_update(svc->ps_pool, srv_pool_hdl, srv_cont_hdl);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": ds_pool_iv_srv_hdl_update failed", DP_UUID(svc->ps_uuid));
		goto out;
	}

	/* resume pool upgrade if needed */
	rc = ds_pool_upgrade_if_needed(svc->ps_uuid, NULL, svc, NULL, srv_pool_hdl, srv_cont_hdl);
	if (rc != 0)
		goto out;

	rc = ds_rebuild_regenerate_task(svc->ps_pool, prop);
	if (rc != 0)
		goto out;

	rc = pool_svc_step_up_metrics(svc, rank, map_version, map_buf);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to initialize pool service metrics",
			 DP_UUID(svc->ps_uuid));
		D_GOTO(out, rc);
	}

	DS_POOL_LOG_PRINT(NOTE,
			  DF_UUID ": rank %u became pool service leader " DF_U64
				  ": srv_pool_hdl=" DF_UUID " srv_cont_hdl=" DF_UUID "\n",
			  DP_UUID(svc->ps_uuid), rank, svc->ps_rsvc.s_term, DP_UUID(srv_pool_hdl),
			  DP_UUID(srv_cont_hdl));
out:
	if (rc != 0) {
		if (events_initialized)
			fini_events(svc);
		sched_cancel_and_wait(&svc->ps_rfcheck_sched);
		sched_cancel_and_wait(&svc->ps_reconf_sched);
		if (cont_svc_up)
			ds_cont_svc_step_down(svc->ps_cont_svc);
	}
	if (map_buf != NULL)
		D_FREE(map_buf);
	if (prop != NULL)
		daos_prop_free(prop);
	if (svc->ps_error != 0) {
		/*
		 * Step up with the error anyway, so that RPCs to the PS
		 * receive an error instead of timeouts.
		 */
		DS_POOL_LOG_PRINT(NOTE, DF_UUID": rank %u became pool service leader "DF_U64
				  " with error: "DF_RC"\n", DP_UUID(svc->ps_uuid), rank,
				  svc->ps_rsvc.s_term, DP_RC(svc->ps_error));
		rc = 0;
	}
	return rc;
}

static void
pool_svc_step_down_cb(struct ds_rsvc *rsvc)
{
	struct pool_svc *svc  = pool_svc_obj(rsvc);
	d_rank_t         rank = dss_self_rank();

	if (svc->ps_error == 0) {
		pool_svc_step_down_metrics(svc);
		fini_events(svc);
		sched_cancel_and_wait(&svc->ps_reconf_sched);
		sched_cancel_and_wait(&svc->ps_rfcheck_sched);
		ds_cont_svc_step_down(svc->ps_cont_svc);
		DS_POOL_LOG_PRINT(NOTE, DF_UUID": rank %u no longer pool service leader "DF_U64"\n",
				  DP_UUID(svc->ps_uuid), rank, svc->ps_rsvc.s_term);
	} else {
		DS_POOL_LOG_PRINT(NOTE, DF_UUID": rank %u no longer pool service leader "DF_U64
				  " with error: "DF_RC"\n", DP_UUID(svc->ps_uuid), rank,
				  svc->ps_rsvc.s_term, DP_RC(svc->ps_error));
		svc->ps_error = 0;
	}
}

static void
pool_svc_drain_cb(struct ds_rsvc *rsvc)
{
}

static int
pool_svc_map_dist_cb(struct ds_rsvc *rsvc, uint32_t *version)
{
	struct pool_svc        *svc = pool_svc_obj(rsvc);
	struct pool_metrics    *metrics;
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
		DL_ERROR(rc, DF_UUID ": failed to read pool map buffer", DP_UUID(svc->ps_uuid));
		goto out;
	}

	rc = ds_pool_iv_map_update(svc->ps_pool, map_buf, map_version);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to distribute pool map %u", DP_UUID(svc->ps_uuid),
			 map_version);
		D_GOTO(out, rc);
	}

	*version = map_version;

	metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
	d_tm_set_counter(metrics->map_version, map_version);
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

/* Use pool_svc_lookup_leader instead. */
static int
pool_svc_lookup(uuid_t uuid, struct pool_svc **svcp)
{
	struct ds_rsvc *rsvc;
	d_iov_t         id;
	int             rc;

	d_iov_set(&id, uuid, sizeof(uuid_t));
	rc = ds_rsvc_lookup(DS_RSVC_CLASS_POOL, &id, &rsvc);
	if (rc != 0)
		return rc;
	*svcp = pool_svc_obj(rsvc);
	return 0;
}

static int
pool_svc_lookup_leader(uuid_t uuid, struct pool_svc **svcp, struct rsvc_hint *hint)
{
	struct ds_rsvc  *rsvc;
	d_iov_t          id;
	struct pool_svc *svc;
	int              rc;

	rc = ds_pool_failed_lookup(uuid);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID ": failed: " DF_RC "\n", DP_UUID(uuid), DP_RC(rc));
		return -DER_NO_SERVICE;
	}

	d_iov_set(&id, uuid, sizeof(uuid_t));
	rc = ds_rsvc_lookup_leader(DS_RSVC_CLASS_POOL, &id, &rsvc, hint);
	if (rc != 0)
		return rc;

	/*
	 * The svc->ps_error field stores a persistent error, usually in the DB
	 * data, if any. For instance, "the layout of the DB data is
	 * incompatible with the software version". This mustn't be a replica
	 * error, because there may be a majorty of replicas working. We let the
	 * PS step up with this error so that it can serve all requests by
	 * returning the error. PS clients therefore get a quick error response
	 * instead of a timeout.
	 *
	 * Checking svc->ps_error here without confirming our leadership via
	 * rdb_raft_verify_leadership may cause some requests to get
	 * unnecessary errors, if there is a newer leader whose svc->ps_error
	 * is zero and is able to serve those requests. Such a state won't last
	 * much longer than an election timeout though, because we will step
	 * down due to inability to maintain a majority lease.
	 */
	svc = pool_svc_obj(rsvc);
	if (svc->ps_error != 0) {
		rc = svc->ps_error;
		ds_rsvc_put_leader(rsvc);
		return rc;
	}

	*svcp = svc;
	return 0;
}

static void
pool_svc_put_leader(struct pool_svc *svc)
{
	ds_rsvc_put_leader(&svc->ps_rsvc);
}

int
ds_pool_svc_lookup_leader(uuid_t uuid, struct ds_pool_svc **ds_svcp, struct rsvc_hint *hint)
{
	struct pool_svc	*svc = NULL;
	int		 rc;

	rc = pool_svc_lookup_leader(uuid, &svc, hint);
	if (rc == 0)
		*ds_svcp = pool_svc2ds(svc);

	return rc;
}

void
ds_pool_svc_put_leader(struct ds_pool_svc *ds_svc)
{
	struct pool_svc	*svc = pool_ds2svc(ds_svc);

	if (svc != NULL)
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
	DL_ERROR(rc, DF_UUID ": added to list of failed pools", DP_UUID(uuid));
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
			DL_INFO(psf->psf_error, DF_UUID ": removed from list of failed pools",
				DP_UUID(uuid));
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

struct pool_start_args {
	bool	psa_aft_chk;
	bool	psa_immutable;
};

/*
 * Try to start the pool. Continue the iteration upon errors as other pools may
 * still be able to work.
 */
static int
start_one(uuid_t uuid, void *varg)
{
	struct pool_start_args	*psa = varg;
	bool			 aft_chk;
	bool			 immutable;
	int			 rc;

	if (psa != NULL) {
		aft_chk = psa->psa_aft_chk;
		immutable = psa->psa_immutable;
	} else {
		aft_chk = false;
		immutable = false;
	}

	D_DEBUG(DB_MD, DF_UUID ": starting pool, aft_chk %s, immutable %s\n",
		DP_UUID(uuid), aft_chk ? "yes" : "no", immutable ? "yes" : "no");

	rc = ds_pool_start(uuid, aft_chk, immutable);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to start pool, aft_chk %s, immutable %s",
			 DP_UUID(uuid), aft_chk ? "yes" : "no", immutable ? "yes" : "no");
		ds_pool_failed_add(uuid, rc);
	}

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

bool
ds_pool_restricted(struct ds_pool *pool, bool immutable)
{
	if (ds_pool_skip_for_check(pool))
		return true;

	if (pool->sp_immutable && !immutable)
		return true;

	return false;
}

int
ds_pool_start_after_check(uuid_t uuid, bool immutable)
{
	struct pool_start_args	psa;

	psa.psa_aft_chk = true;
	psa.psa_immutable = immutable;

	return start_one(uuid, &psa);
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
	ABT_thread_free(&thread);
	return 0;
}

static void
stop_one(void *arg)
{
	unsigned char *uuid = arg;

	D_DEBUG(DB_MD, DF_UUID ": stopping pool\n", DP_UUID(uuid));
	ds_pool_stop(uuid);
}

struct stop_ult {
	d_list_t   su_entry;
	ABT_thread su_thread;
	uuid_t     su_uuid;
};

struct stop_all_arg {
	d_list_t saa_list; /* of stop_ult objects */
};

static int
stop_all_cb(unsigned char *uuid, void *varg)
{
	struct stop_all_arg *arg = varg;
	struct stop_ult     *ult;
	int                  rc;

	D_ALLOC_PTR(ult);
	if (ult == NULL)
		return -DER_NOMEM;

	uuid_copy(ult->su_uuid, uuid);

	rc = dss_ult_create(stop_one, ult->su_uuid, DSS_XS_SYS, 0, 0, &ult->su_thread);
	if (rc != 0) {
		D_FREE(ult);
		return rc;
	}

	d_list_add(&ult->su_entry, &arg->saa_list);
	return 0;
}

static void
pool_stop_all(void *varg)
{
	struct stop_all_arg arg;
	struct stop_ult    *ult;
	struct stop_ult    *ult_tmp;
	int                 rc;

	D_INIT_LIST_HEAD(&arg.saa_list);

	rc = ds_mgmt_tgt_pool_iterate(stop_all_cb, &arg);

	/* Wait for the stopper ULTs to return. */
	d_list_for_each_entry_safe(ult, ult_tmp, &arg.saa_list, su_entry) {
		d_list_del_init(&ult->su_entry);
		ABT_thread_free(&ult->su_thread);
		D_FREE(ult);
	}

	if (rc != 0)
		DL_ERROR(rc, "failed to stop all pools");
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
				    DAOS_POOL_VERSION, rpc, bulk_hdl, NULL, NULL);
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

	if (bits & DAOS_PO_QUERY_PROP_DATA_THRESH) {
		d_iov_set(&value, &val, sizeof(val));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_data_thresh, &value);
		if (rc == -DER_NONEXIST && global_ver < DAOS_POOL_GLOBAL_VERSION_WITH_DATA_THRESH) {
			/* needs to be upgraded */
			rc  = 0;
			val = DAOS_PROP_PO_DATA_THRESH_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": DAOS_PO_QUERY_PROP_DATA_THRESH lookup failed",
				 DP_UUID(svc->ps_uuid));
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_DATA_THRESH;
		prop->dpp_entries[idx].dpe_val = val;
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

	if (bits & DAOS_PO_QUERY_PROP_REINT_MODE) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_reint_mode, &value);
		/* NB: would test global_ver < 2, but on master branch, code added after v3 bump. */
		if (rc == -DER_NONEXIST && global_ver < 3) { /* needs to be upgraded */
			rc  = 0;
			val32 = DAOS_PROP_PO_REINT_MODE_DEFAULT;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			D_ERROR(DF_UUID ": DAOS_PROP_PO_REINT_MODE missing from the pool\n",
				DP_UUID(svc->ps_uuid));
			D_GOTO(out_prop, rc);
		}
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_REINT_MODE;
		prop->dpp_entries[idx].dpe_val  = val32;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_SVC_OPS_ENABLED) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops_enabled, &value);
		if (rc == -DER_NONEXIST && global_ver < DAOS_POOL_GLOBAL_VERSION_WITH_SVC_OPS_KVS) {
			/* needs to be upgraded */
			rc    = 0;
			val32 = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": DAOS_PROP_PO_SVC_OPS_ENABLED missing from the pool",
				 DP_UUID(svc->ps_uuid));
			D_GOTO(out_prop, rc);
		}
		if (rc != 0)
			D_GOTO(out_prop, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SVC_OPS_ENABLED;
		prop->dpp_entries[idx].dpe_val  = val32;
		idx++;
	}

	if (bits & DAOS_PO_QUERY_PROP_SVC_OPS_ENTRY_AGE) {
		d_iov_set(&value, &val32, sizeof(val32));
		rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops_age, &value);
		if (rc == -DER_NONEXIST && global_ver < DAOS_POOL_GLOBAL_VERSION_WITH_SVC_OPS_KVS) {
			/* needs to be upgraded */
			rc    = 0;
			val32 = 0;
			prop->dpp_entries[idx].dpe_flags |= DAOS_PROP_ENTRY_NOT_SET;
		} else if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": DAOS_PROP_PO_SVC_OPS_ENTRY_AGE missing from pool",
				 DP_UUID(svc->ps_uuid));
			D_GOTO(out_prop, rc);
		}
		if (rc != 0)
			D_GOTO(out_prop, rc);
		D_ASSERT(idx < nr);
		prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SVC_OPS_ENTRY_AGE;
		prop->dpp_entries[idx].dpe_val  = val32;
		idx++;
	}

	*prop_out = prop;
	return 0;

out_prop:
	daos_prop_free(prop);
	return rc;
}

/* Test if pool opcode is a pool service operation, and is a metadata "write" operation. */
static bool
pool_op_is_write(crt_opcode_t opc)
{
	bool is_write;

	switch (opc) {
	/* opcodes handled by pool service that just read the metadata */
	case POOL_QUERY:
	case POOL_QUERY_INFO:
	case POOL_ATTR_LIST:
	case POOL_ATTR_GET:
	case POOL_LIST_CONT:
	case POOL_FILTER_CONT:
	case POOL_PROP_GET:
	case POOL_RANKS_GET:
	/* opcodes not handled by pool service */
	case POOL_TGT_QUERY_MAP:
	case POOL_TGT_DISCONNECT:
	case POOL_TGT_QUERY:
	case POOL_ADD_TGT:
	case POOL_TGT_DISCARD:
		is_write = false;
		break;
	default:
		is_write = true;
		break;
	}
	return is_write;
}

#if 0
/* DEBUG */
static int
pool_op_iter_cb(daos_handle_t ih, d_iov_t *key_enc, d_iov_t *val, void *arg)
{
	struct ds_pool_svc_op_key  op_key;
	struct ds_pool_svc_op_val *op_val = val->iov_buf;

	ds_pool_svc_op_key_decode(key_enc, &op_key);

	D_DEBUG(DB_MD, "key: time=" DF_X64 ", cli=" DF_UUID ", rc=%d\n",
		op_key.ok_client_time, DP_UUID(op_key.ok_client_id), op_val->ov_rc);

	return 0;
}
#endif

static int
pool_op_check_delete_oldest(struct rdb_tx *tx, struct pool_svc *svc, bool dup_op,
			    uint32_t *svc_ops_num)
{
	int                       rc;
	d_iov_t                   key1_enc;
	struct ds_pool_svc_op_key k1;
	uint64_t                  t1_sec;
	uint64_t                  t2_sec;
	uint64_t                  age_sec;

	if (svc->ps_ops_enabled == 0)
		return 0;

	d_iov_set(&key1_enc, NULL, 0);
	rc = rdb_tx_fetch(tx, &svc->ps_ops, RDB_PROBE_FIRST, NULL /* key_in */, &key1_enc,
			  NULL /* value */);
	if (rc == -DER_NONEXIST)
		return 0;
	else if (rc != 0) {
		DL_ERROR(rc, "failed to probe first ps_ops entry");
		return rc;
	}

	rc = ds_pool_svc_op_key_decode(&key1_enc, &k1);
	if (rc != 0) {
		DL_ERROR(rc, "key decode failed");
		return rc;
	}

	/* If number of RPCs is at the limit, or the oldest is more than ps_ops_age old,
	 * delete the oldest entry. TODO: evict many/all such entries (during periodic cleanup?).
	 */
	t1_sec  = d_hlc2sec(k1.ok_client_time);
	t2_sec  = d_hlc2sec(d_hlc_get());
	age_sec = t2_sec - t1_sec;

	if ((*svc_ops_num < svc->ps_ops_max) && (age_sec <= svc->ps_ops_age))
		return 0;

	D_DEBUG(DB_MD, DF_UUID ": will delete oldest entry, svc_ops_num=%u, age=%zu sec\n",
		DP_UUID(svc->ps_uuid), *svc_ops_num, age_sec);
	rc = rdb_tx_delete(tx, &svc->ps_ops, &key1_enc);
	if (rc != 0) {
		DL_ERROR(rc, "failed to delete oldest entry in ps_ops");
		return rc;
	}

	*svc_ops_num -= 1;
	return 0;
}

/* Check if this is a duplicate/retry operation that was already done, and if so the stored result.
 * Return the answer in is_dup (when rc == 0). Further when is_dup is true, assign value into valp.
 * Common function called by pool and container service RPC op lookup functions,
 */
int
ds_pool_svc_ops_lookup(struct rdb_tx *tx, void *pool_svc, uuid_t pool_uuid, uuid_t *cli_uuidp,
		       uint64_t cli_time, bool *is_dup, struct ds_pool_svc_op_val *valp)
{
	struct pool_svc          *svc          = pool_svc;
	bool                      need_put_svc = false;
	struct ds_pool_svc_op_key op_key;
	d_iov_t                   op_key_enc = {.iov_buf = NULL};
	struct ds_pool_svc_op_val op_val;
	d_iov_t                   val;
	bool                      duplicate = false;
	int                       rc  = 0;

	if (!svc) {
		rc = pool_svc_lookup_leader(pool_uuid, &svc, NULL /* hint */);
		if (rc != 0) {
			DL_ERROR(rc, "pool_svc lookup failed");
			goto out;
		}
		need_put_svc = true;
	}

	if (!svc->ps_ops_enabled)
		goto out_svc;

#if 0
	/* DEBUG */
	rc = rdb_tx_iterate(tx, &svc->ps_ops, false /* backward */, pool_op_iter_cb,
			    NULL /* arg */);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to iterate ps_ops KVS", DP_UUID(pool_uuid));
		goto out_svc;
	}
#endif

	/* Construct (encoded) client ID key, look for it (duplicate RPC) in ps_ops */
	d_iov_set(&val, &op_val, sizeof(op_val));
	uuid_copy(op_key.ok_client_id, *cli_uuidp);
	op_key.ok_client_time = cli_time;
	rc                    = ds_pool_svc_op_key_encode(&op_key, &op_key_enc);
	if (rc != 0)
		goto out_svc;
	rc = rdb_tx_lookup(tx, &svc->ps_ops, &op_key_enc, &val);
	if (rc == 0) {
		/* found - this is a retry/duplicate RPC being handled */
		D_DEBUG(DB_MD,
			DF_UUID ": retry RPC detected client=" DF_UUID " time=%016lx op_rc=%d\n",
			DP_UUID(pool_uuid), DP_UUID(*cli_uuidp), cli_time, op_val.ov_rc);
		duplicate = true;
	} else if (rc == -DER_NONEXIST) {
		/* not found - new, unique RPC being handled */
		rc = 0;
	} else {
		DL_ERROR(rc, DF_UUID ": failed to lookup RPC client=" DF_UUID " time=%016lx",
			 DP_UUID(pool_uuid), DP_UUID(*cli_uuidp), cli_time);
		goto out_enc;
	}

out_enc:
	D_FREE(op_key_enc.iov_buf);
out_svc:
	if (need_put_svc)
		pool_svc_put_leader(svc);
out:
	if (rc == 0) {
		*is_dup = duplicate;
		if (duplicate)
			*valp = op_val;
	}
	return rc;
}

/* Check if this is a duplicate/retry operation that was already done, and if so the stored result.
 * Return the answer in is_dup (when rc == 0). Further when is_dup is true, assign value into valp.
 */
static int
pool_op_lookup(struct rdb_tx *tx, struct pool_svc *svc, crt_rpc_t *rpc, int pool_proto_ver,
	       bool *is_dup, struct ds_pool_svc_op_val *valp)
{
	struct pool_op_in    *in  = crt_req_get(rpc);
	crt_opcode_t          opc = opc_get(rpc->cr_opc);
	int                   rc  = 0;

	D_ASSERT(pool_proto_ver >= POOL_PROTO_VER_WITH_SVC_OP_KEY);
	/* If the operation is not a write, skip (read-only ops not tracked for duplicates) */
	if (!pool_op_is_write(opc))
		goto out;

	rc = ds_pool_svc_ops_lookup(tx, svc, svc->ps_uuid, &in->pi_cli_id, in->pi_time, is_dup,
				    valp);

out:
	return rc;
}

int
ds_pool_svc_ops_save(struct rdb_tx *tx, void *pool_svc, uuid_t pool_uuid, uuid_t *cli_uuidp,
		     uint64_t cli_time, bool dup_op, int rc_in, struct ds_pool_svc_op_val *op_valp)
{
	struct pool_svc          *svc          = pool_svc;
	bool                      need_put_svc = false;
	d_iov_t                   val;
	struct ds_pool_svc_op_key op_key;
	d_iov_t                   op_key_enc = {.iov_buf = NULL};
	uint32_t                  svc_ops_num;
	uint32_t                  new_svc_ops_num;
	int                       rc = 0;

	if (!svc) {
		rc = pool_svc_lookup_leader(pool_uuid, &svc, NULL /* hint */);
		if (rc != 0) {
			DL_ERROR(rc, "pool_svc lookup failed");
			goto out;
		}
		need_put_svc = true;
	}

	if (!svc->ps_ops_enabled)
		goto out_svc;

	/* Get number of entries in the KVS for incrementing/decrementing as applicable below */
	d_iov_set(&val, &svc_ops_num, sizeof(svc_ops_num));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops_num, &val);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to lookup svc_ops_num", DP_UUID(pool_uuid));
		goto out_svc;
	}
	new_svc_ops_num = svc_ops_num;

	if (!dup_op && !daos_rpc_retryable_rc(op_valp->ov_rc)) {
		/* If the write operation failed, discard its (unwanted) updates first. */
		if (op_valp->ov_rc != 0)
			rdb_tx_discard(tx);

		/* Construct (encoded) client ID key, insert an entry into ps_ops */
		d_iov_set(&val, op_valp, sizeof(*op_valp));
		uuid_copy(op_key.ok_client_id, *cli_uuidp);
		op_key.ok_client_time = cli_time;
		rc                    = ds_pool_svc_op_key_encode(&op_key, &op_key_enc);
		if (rc != 0)
			goto out_svc;
		rc = rdb_tx_update(tx, &svc->ps_ops, &op_key_enc, &val);
		if (rc != 0) {
			DL_ERROR(rc,
				 DF_UUID ": svc_ops update failed: client=" DF_UUID " time=%016lx",
				 DP_UUID(pool_uuid), DP_UUID(*cli_uuidp), cli_time);
			goto out_enc;
		}
		new_svc_ops_num++;
	}

	rc = pool_op_check_delete_oldest(tx, svc, dup_op, &new_svc_ops_num);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed pool_op_check_delete_oldest()", DP_UUID(pool_uuid));
		goto out_enc;
	}

	/* update the number of entries in the KVS */
	if (new_svc_ops_num != svc_ops_num) {
		svc_ops_num = new_svc_ops_num;
		d_iov_set(&val, &svc_ops_num, sizeof(svc_ops_num));
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_svc_ops_num, &val);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to update svc_ops_num", DP_UUID(pool_uuid));
			goto out_enc;
		}
	}
out_enc:
	D_FREE(op_key_enc.iov_buf);
out_svc:
	if (need_put_svc)
		pool_svc_put_leader(svc);
out:
	return rc;
}

/* Save results of the (new, not duplicate) operation in svc_ops KVS, if applicable.
 * And delete oldest entry if KVS has reached maximum number, or oldest exceeds age limit.
 */
static int
pool_op_save(struct rdb_tx *tx, struct pool_svc *svc, crt_rpc_t *rpc, int pool_proto_ver,
	     bool dup_op, int rc_in, struct ds_pool_svc_op_val *op_valp)
{
	struct pool_op_in    *in  = crt_req_get(rpc);
	crt_opcode_t          opc = opc_get(rpc->cr_opc);
	int                   rc  = 0;

	if (!dup_op)
		op_valp->ov_rc = rc_in;

	D_ASSERT(pool_proto_ver >= POOL_PROTO_VER_WITH_SVC_OP_KEY);
	/* If the operation is not a write, skip (read-only ops not tracked for duplicates) */
	if (!pool_op_is_write(opc))
		goto out;

	rc = ds_pool_svc_ops_save(tx, svc, svc->ps_uuid, &in->pi_cli_id, in->pi_time, dup_op, rc_in,
				  op_valp);

out:
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
	daos_prop_t            *prop      = NULL;
	d_rank_list_t          *tgt_ranks = NULL;
	uint32_t                ndomains;
	uint32_t                ntgts;
	uint32_t               *domains;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pri_op.pi_uuid), rpc);

	pool_create_in_get_data(rpc, &tgt_ranks, &prop, &ndomains, &ntgts, &domains);

	if (ntgts != tgt_ranks->rl_nr)
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

	if (!uuid_is_null(in->pri_op.pi_hdl)) {
		/*
		 * Try starting a campaign without waiting for the election
		 * timeout. Since this is a performance optimization, ignore
		 * errors.
		 */
		rc = rdb_campaign(svc->ps_rsvc.s_db);
		D_DEBUG(DB_MD, DF_UUID": campaign: "DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, RDB_NIL_TERM, &tx);
	if (rc != 0)
		D_GOTO(out_mutex, rc);
	ABT_rwlock_wrlock(svc->ps_lock);
	ds_cont_wrlock_metadata(svc->ps_cont_svc);

	if (svc->ps_error != 0) {
		DL_ERROR(svc->ps_error, DF_UUID ": encountered pool service leader with error",
			 DP_UUID(svc->ps_uuid));
		rc = svc->ps_error;
		goto out_tx;
	}

	/* See if the DB has already been initialized. */
	d_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(&tx, &svc->ps_root, &ds_pool_prop_map_buffer,
			   &value);
	if (rc != -DER_NONEXIST) {
		if (rc == 0)
			D_DEBUG(DB_MD, DF_UUID": db already initialized\n",
				DP_UUID(svc->ps_uuid));
		else
			DL_ERROR(rc, DF_UUID ": failed to look up pool map", DP_UUID(svc->ps_uuid));
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

	rc = pool_prop_default_copy(prop_dup, prop);
	if (rc) {
		DL_ERROR(rc, "daos_prop_default_copy() failed");
		D_GOTO(out_tx, rc);
	}

	/* Initialize the DB and the metadata for this pool. */
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 8;
	rc = rdb_tx_create_root(&tx, &attr);
	if (rc != 0)
		D_GOTO(out_tx, rc);
	rc = init_pool_metadata(&tx, &svc->ps_root, ntgts, NULL /* group */, tgt_ranks, prop_dup,
				ndomains, domains);
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
		if (DAOS_FAIL_CHECK(DAOS_POOL_CREATE_FAIL_STEP_UP))
			rc = -DER_GRPVER;
		else
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

/* Currently we only maintain compatibility between 2 metadata layout versions */
#define NUM_POOL_VERSIONS	2

static void
pool_connect_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_connect_in         *in  = crt_req_get(rpc);
	struct pool_connect_out        *out = crt_reply_get(rpc);
	struct pool_svc                *svc;
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
	d_iov_t                        *credp;
	uint64_t                        flags;
	uint64_t                        query_bits;
	crt_bulk_t                      bulk;
	uint32_t                        cli_pool_version;
	bool                            dup_op = false;
	struct ds_pool_svc_op_val       op_val;
	bool                            transfer_map    = false;
	bool                            fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                            fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);
	bool                            fi_pass_nl_noreply;
	bool                            fi_fail_nl_noreply;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pci_op.pi_uuid), rpc, DP_UUID(in->pci_op.pi_hdl));

	fi_pass_nl_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY_NEWLDR);
	fi_fail_nl_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY_NEWLDR);

	rc = pool_svc_lookup_leader(in->pci_op.pi_uuid, &svc, &out->pco_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	pool_connect_in_get_cred(rpc, &credp);
	pool_connect_in_get_data(rpc, &flags, &query_bits, &bulk, &cli_pool_version);

	if (query_bits & DAOS_PO_QUERY_REBUILD_STATUS) {
		rc = ds_rebuild_query(in->pci_op.pi_uuid, &out->pco_rebuild_st);
		if (rc != 0)
			D_GOTO(out_svc, rc);
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_op_lookup(&tx, svc, rpc, handler_version, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op)
		skip_update = true;
	if (fi_fail_noreply || fi_fail_nl_noreply)
		goto out_map_version;

	/* Check if pool is being destroyed and not accepting connections */
	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_lookup(&tx, &svc->ps_root,
			   &ds_pool_prop_connectable, &value);
	if (rc != 0)
		goto out_lock;
	if (!connectable) {
		D_ERROR(DF_UUID": being destroyed, not accepting connections\n",
			DP_UUID(in->pci_op.pi_uuid));
		D_GOTO(out_lock, rc = -DER_BUSY);
	}

	/*
	 * NOTE: Under check mode, there is a small race window between ds_pool_mark_connectable()
	 *	 and PS restart with full service. If some client tries to connect the pool during
	 *	 such internal, it will get -DER_BUSY temporarily.
	 */
	if (unlikely(ds_pool_skip_for_check(svc->ps_pool))) {
		rc = -DER_BUSY;
		D_ERROR(DF_UUID " is not ready for full pool service: " DF_RC "\n",
			DP_UUID(in->pci_op.pi_uuid), DP_RC(rc));
		goto out_lock;
	}

	if (svc->ps_pool->sp_immutable && flags != DAOS_PC_RO) {
		rc = -DER_NO_PERM;
		D_ERROR(DF_UUID " failed to connect immutable pool, flags " DF_X64 ": " DF_RC "\n",
			DP_UUID(in->pci_op.pi_uuid), flags, DP_RC(rc));
		goto out_lock;
	}

	/* Check existing pool handles. */
	d_iov_set(&key, in->pci_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
	if (rc == 0) {
		/* found it */
		if (((struct pool_hdl *)value.iov_buf)->ph_flags == flags) {
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
		goto out_lock;
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
	int diff = DAOS_POOL_GLOBAL_VERSION - cli_pool_version;
	if (cli_pool_version <= DAOS_POOL_GLOBAL_VERSION) {
		if (diff >= NUM_POOL_VERSIONS) {
			rc = -DER_NOTSUPPORTED;
			DL_ERROR(rc,
				 DF_UUID ": cannot connect, client supported pool "
					 "layout version (%u) is more than %u versions smaller "
					 "than server supported pool layout version(%u), "
					 "try to upgrade client firstly",
				 DP_UUID(in->pci_op.pi_uuid), cli_pool_version,
				 NUM_POOL_VERSIONS - 1, DAOS_POOL_GLOBAL_VERSION);
			goto out_map_version;
		}

		if (global_ver > cli_pool_version) {
			rc = -DER_NOTSUPPORTED;
			DL_ERROR(rc,
				 DF_UUID ": cannot connect, pool layout version(%u) > "
					 "max client supported pool layout version(%u), "
					 "try to upgrade client firstly",
				 DP_UUID(in->pci_op.pi_uuid), global_ver, cli_pool_version);
			goto out_map_version;
		}
	} else {
		diff = -diff;
		if (diff >= NUM_POOL_VERSIONS) {
			rc = -DER_NOTSUPPORTED;
			DL_ERROR(rc,
				 DF_UUID ": cannot connect, client supported pool "
					 "layout version (%u) is more than %u versions "
					 "larger than server supported pool layout version(%u), "
					 "try to upgrade server firstly",
				 DP_UUID(in->pci_op.pi_uuid), cli_pool_version,
				 NUM_POOL_VERSIONS - 1, DAOS_POOL_GLOBAL_VERSION);
			goto out_map_version;
		}
		/* New clients should be able to access old pools without problem */
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
	rc = ds_sec_pool_get_capabilities(flags, credp, &owner, acl_entry->dpe_val_ptr, &sec_capas);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": refusing connect attempt for " DF_X64,
			 DP_UUID(in->pci_op.pi_uuid), flags);
		D_GOTO(out_map_version, rc);
	}

	rc = ds_sec_cred_get_origin(credp, &machine);

	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": unable to retrieve origin", DP_UUID(in->pci_op.pi_uuid));
		D_GOTO(out_map_version, rc);
	}

	if (!ds_sec_pool_can_connect(sec_capas)) {
		rc = -DER_NO_PERM;
		DL_ERROR(rc, DF_UUID ": permission denied for connect attempt for " DF_X64,
			 DP_UUID(in->pci_op.pi_uuid), flags);
		goto out_map_version;
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
		if (flags & DAOS_PC_EX) {
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

	D_DEBUG(DB_MD, DF_UUID "/" DF_UUID ": connecting to %s pool with flags "
		DF_X64", sec_capas " DF_X64 "\n",
		DP_UUID(in->pci_op.pi_uuid), DP_UUID(in->pci_op.pi_hdl),
		svc->ps_pool->sp_immutable ? "immutable" : "regular", flags, sec_capas);

	rc = pool_connect_iv_dist(svc, in->pci_op.pi_hdl, flags, sec_capas, credp, global_ver,
				  obj_layout_ver);
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
	D_ALLOC(hdl, sizeof(*hdl) + credp->iov_len);
	if (hdl == NULL)
		D_GOTO(out_map_version, rc = -DER_NOMEM);

	hdl->ph_flags     = flags;
	hdl->ph_sec_capas = sec_capas;
	/* XXX may be can check pool version to avoid initializing 3 following hdl fields ? */
	strncpy(hdl->ph_machine, machine, MAXHOSTNAMELEN);
	hdl->ph_cred_len = credp->iov_len;
	memcpy(&hdl->ph_cred[0], credp->iov_buf, credp->iov_len);

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

out_map_version:
	out->pco_op.po_map_version = ds_pool_get_version(svc->ps_pool);

	D_DEBUG(DB_MD, DF_UUID ": rc=%d, dup_op=%d\n", DP_UUID(in->pci_op.pi_uuid), rc, dup_op);
	/* If meets criteria (not dup, write op, definitive rc, etc.), store result in ps_ops KVS */
	if ((rc == 0) && !dup_op && (fi_fail_noreply || fi_fail_nl_noreply))
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, handler_version, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;
	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;

	rc = op_val.ov_rc;
	if ((rc == 0) && !dup_op) {
		/** update metric */
		metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
		d_tm_inc_counter(metrics->connect_total, 1);
		d_tm_inc_gauge(metrics->open_handles, 1);
	}

	if ((rc == 0) && (query_bits & DAOS_PO_QUERY_SPACE))
		rc = pool_space_query_bcast(rpc->cr_ctx, svc, in->pci_op.pi_hdl, &out->pco_space,
					    NULL);

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc == 0 && transfer_map) {
		struct ds_pool_map_bc *map_bc;
		uint32_t               map_version;

		rc = ds_pool_lookup_map_bc(svc->ps_pool, rpc->cr_ctx, &map_bc, &map_version);
		if (rc == 0) {
			rc = ds_pool_transfer_map_buf(map_bc, rpc, bulk, &out->pco_map_buf_size);
			ds_pool_put_map_bc(map_bc);
			/* Ensure the map version matches the map buffer. */
			out->pco_op.po_map_version = map_version;
		}
		/** TODO: roll back tx if transfer fails? Perhaps rdb_tx_discard()? */
	}
	if (rc == 0)
		rc = op_val.ov_rc;
	D_FREE(hdl);
	D_FREE(machine);
	if (prop)
		daos_prop_free(prop);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pco_op.po_hint);
	pool_svc_put_leader(svc);
out:
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->pci_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->pci_op.pi_uuid));
	}
	if ((rc == 0) && !dup_op && fi_pass_nl_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY_NEWLDR\n",
			DP_UUID(in->pci_op.pi_uuid));
		rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_nl_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY_NEWLDR\n",
			DP_UUID(in->pci_op.pi_uuid));
		rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
	}

	out->pco_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pci_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_connect_handler(crt_rpc_t *rpc)
{
	pool_connect_handler(rpc, DAOS_POOL_VERSION);
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
	struct pool_metrics     *metrics;
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

	metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
	d_tm_dec_gauge(metrics->open_handles, n_hdl_uuids);
out:
	if (rc == 0)
		D_INFO(DF_UUID": success\n", DP_UUID(svc->ps_uuid));
	return rc;
}

static void
pool_disconnect_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_disconnect_in      *pdi = crt_req_get(rpc);
	struct pool_disconnect_out     *pdo = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	struct rdb_tx			tx;
	d_iov_t                         key;
	d_iov_t                         value;
	bool                            dup_op = false;
	struct ds_pool_svc_op_val       op_val;
	bool                            fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                            fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);
	int				rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(pdi->pdi_op.pi_uuid), rpc, DP_UUID(pdi->pdi_op.pi_hdl));

	D_ASSERT(handler_version >= POOL_PROTO_VER_WITH_SVC_OP_KEY);
	D_DEBUG(DB_MD, DF_UUID ": client= " DF_UUID ", time=" DF_X64 "\n",
		DP_UUID(pdi->pdi_op.pi_uuid), DP_UUID(pdi->pdi_op.pi_cli_id), pdi->pdi_op.pi_time);

	rc = pool_svc_lookup_leader(pdi->pdi_op.pi_uuid, &svc,
				    &pdo->pdo_op.po_hint);
	if (rc != 0)
		goto out;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_op_lookup(&tx, svc, rpc, handler_version, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;

	d_iov_set(&key, pdi->pdi_op.pi_hdl, sizeof(uuid_t));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(&tx, &svc->ps_handles, &key, &value);
	if (rc != 0) {
		/* TODO: consider should this test be removed, now that dup ops are detectable?
		 * consider evict use case though.
		 */
		if (rc == -DER_NONEXIST)
			rc = op_val.ov_rc = 0;
		D_GOTO(out_commit, rc);
	}

	rc = pool_disconnect_hdls(&tx, svc, &pdi->pdi_op.pi_hdl,
				  1 /* n_hdl_uuids */, rpc->cr_ctx);
	if (rc != 0)
		goto out_commit;

out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, handler_version, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;
	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	/* No need to set pdo->pdo_op.po_map_version. */

	rc = op_val.ov_rc;
	if ((rc == 0) && !dup_op) {
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
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(pdi->pdi_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(pdi->pdi_op.pi_uuid));
	}

	pdo->pdo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(pdi->pdi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_disconnect_handler(crt_rpc_t *rpc)
{
	pool_disconnect_handler(rpc, DAOS_POOL_VERSION);
}

static int
pool_space_query_bcast(crt_context_t ctx, struct pool_svc *svc, uuid_t pool_hdl,
		       struct daos_pool_space *ps, uint64_t *mem_file_bytes)
{
	struct pool_tgt_query_in	*in;
	struct pool_tgt_query_out	*out;
	crt_rpc_t			*rpc;
	struct pool_space_cache         *cache    = &svc->ps_space_cache;
	uint64_t                         cur_time = 0;
	bool                             unlock   = false;
	int				 rc;

	if (ps_cache_intvl > 0) {
		ABT_mutex_lock(cache->psc_lock);

		cur_time = daos_gettime_coarse();
		if (cur_time < cache->psc_timestamp + ps_cache_intvl) {
			*ps = cache->psc_space;
			if (mem_file_bytes != NULL)
				*mem_file_bytes = cache->psc_memfile_bytes;
			ABT_mutex_unlock(cache->psc_lock);
			return 0;
		}
		unlock = true;
	}

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
	D_ASSERT(ps != NULL);
	if (rc == 0) {
		*ps = out->tqo_space;
		if (mem_file_bytes != NULL)
			*mem_file_bytes = out->tqo_mem_file_bytes;

		if (ps_cache_intvl > 0 && cur_time > cache->psc_timestamp) {
			cache->psc_timestamp = cur_time;
			cache->psc_space     = *ps;
			if (mem_file_bytes != NULL)
				cache->psc_memfile_bytes = *mem_file_bytes;
		}
	} else {
		D_ERROR(DF_UUID ": failed to query from targets: " DF_RC "\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		rc = -DER_IO;
	}
out_rpc:
	crt_req_decref(rpc);
out:
	if (unlock)
		ABT_mutex_unlock(cache->psc_lock);

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
	uint64_t                         ncont;
	crt_bulk_t                       bulk;
	uuid_t                           no_uuid;
	uint64_t                         req_time = 0;

	D_DEBUG(DB_MGMT, DF_UUID": Getting container list\n", DP_UUID(uuid));
	uuid_clear(no_uuid);
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
	rc = pool_req_create(info->dmi_ctx, &ep, POOL_LIST_CONT, uuid, no_uuid, &req_time, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create pool list cont rpc", DP_UUID(uuid));
		goto out_client;
	}

	/* Allocate response buffer */
	D_ALLOC_ARRAY(resp_cont, resp_ncont);
	if (resp_cont == NULL)
		D_GOTO(out_rpc, rc = -DER_NOMEM);

	in = crt_req_get(rpc);
	uuid_copy(in->plci_op.pi_uuid, uuid);
	uuid_clear(in->plci_op.pi_hdl);
	ncont = resp_ncont;
	rc    = list_cont_bulk_create(info->dmi_ctx, &bulk, resp_cont,
				      ncont * sizeof(struct daos_pool_cont_info));
	if (rc != 0)
		D_GOTO(out_resp_buf, rc);

	pool_list_cont_in_set_data(rpc, bulk, ncont);

	rc = dss_rpc_send(rpc);
	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	rc = pool_rsvc_client_complete_rpc(&client, &ep, rc, &out->plco_op);
	if (rc == RSVC_CLIENT_RECHOOSE) {
		/* To simplify logic, destroy bulk hdl and buffer each time */
		list_cont_bulk_destroy(bulk);
		D_FREE(resp_cont);
		crt_req_decref(rpc);
		dss_sleep(250);
		D_GOTO(rechoose, rc);
	}

	rc = out->plco_op.po_rc;
	if (rc == -DER_TRUNC) {
		/* resp_ncont too small - realloc with server-provided ncont */
		resp_ncont = out->plco_ncont;
		list_cont_bulk_destroy(bulk);
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

	list_cont_bulk_destroy(bulk);
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
static void
pool_list_cont_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_list_cont_in	*in = crt_req_get(rpc);
	struct pool_list_cont_out	*out = crt_reply_get(rpc);
	struct daos_pool_cont_info	*cont_buf = NULL;
	uint64_t			 ncont = 0;
	struct pool_svc			*svc;
	uint64_t                         ncont_in;
	crt_bulk_t                       bulk;
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

	pool_list_cont_in_get_data(rpc, &bulk, &ncont_in);

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
	} else if ((ncont_in > 0) && (ncont > ncont_in)) {
		/* Got a list, but client buffer not supplied or too small */
		D_DEBUG(DB_MD,
			DF_UUID ": hdl=" DF_UUID ": has %" PRIu64 "containers (more than client"
				": %" PRIu64 ")\n",
			DP_UUID(in->plci_op.pi_uuid), DP_UUID(in->plci_op.pi_hdl), ncont, ncont_in);
		D_GOTO(out_free_cont_buf, rc = -DER_TRUNC);
	} else {
		size_t nbytes = ncont * sizeof(struct daos_pool_cont_info);

		D_DEBUG(DB_MD, DF_UUID": hdl="DF_UUID": has %"PRIu64 "containers\n",
			DP_UUID(in->plci_op.pi_uuid), DP_UUID(in->plci_op.pi_hdl), ncont);

		/* Send any results only if client provided a handle */
		if (cont_buf && (ncont_in > 0) && (bulk != CRT_BULK_NULL))
			rc = transfer_cont_buf(cont_buf, nbytes, svc, rpc, bulk);
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

void
ds_pool_list_cont_handler(crt_rpc_t *rpc)
{
	pool_list_cont_handler(rpc, DAOS_POOL_VERSION);
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
static void
pool_filter_cont_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_filter_cont_in	*in = crt_req_get(rpc);
	struct pool_filter_cont_out	*out = crt_reply_get(rpc);
	struct daos_pool_cont_info2	*cont_buf = NULL;
	uint64_t			 ncont = 0;
	struct pool_svc			*svc;
	uint64_t                         ncont_in;
	crt_bulk_t                       bulk;
	daos_pool_cont_filter_t         *filt_in;
	struct rdb_tx			 tx;
	d_iov_t				 key;
	d_iov_t				 value;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pfci_op.pi_uuid), rpc, DP_UUID(in->pfci_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pfci_op.pi_uuid, &svc, &out->pfco_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	pool_filter_cont_in_get_data(rpc, &bulk, &ncont_in, &filt_in);

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
	if (!pool_cont_filter_is_valid(in->pfci_op.pi_uuid, filt_in)) {
		rc = -DER_INVAL;
		DL_ERROR(rc, DF_UUID ": filter input failed", DP_UUID(in->pfci_op.pi_uuid));
		goto out_svc;
	}

	/* Call container service to get the filtered list of containers */
	rc = ds_cont_filter(in->pfci_op.pi_uuid, filt_in, &cont_buf, &ncont);
	if (rc != 0) {
		D_GOTO(out_svc, rc);
	} else if ((ncont_in > 0) && (ncont > ncont_in)) {
		/* Got a list, but client buffer not supplied or too small */
		D_DEBUG(DB_MD,
			DF_UUID ": hdl=" DF_UUID ": %" PRIu64 " matching containers "
				"(more than client: %" PRIu64 ")\n",
			DP_UUID(in->pfci_op.pi_uuid), DP_UUID(in->pfci_op.pi_hdl), ncont, ncont_in);
		D_GOTO(out_free_cont_buf, rc = -DER_TRUNC);
	} else {
		size_t nbytes = ncont * sizeof(struct daos_pool_cont_info2);

		D_DEBUG(DB_MD, DF_UUID": hdl="DF_UUID": %"PRIu64" matching containers\n",
			DP_UUID(in->pfci_op.pi_uuid), DP_UUID(in->pfci_op.pi_hdl), ncont);

		/* Send any results only if client provided a handle */
		if (cont_buf && (ncont_in > 0) && (bulk != CRT_BULK_NULL))
			rc = transfer_cont_buf(cont_buf, nbytes, svc, rpc, bulk);
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

void
ds_pool_filter_cont_handler(crt_rpc_t *rpc)
{
	pool_filter_cont_handler(rpc, DAOS_POOL_VERSION);
}

static void
pool_query_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_query_in     *in   = crt_req_get(rpc);
	struct pool_query_out    *out  = crt_reply_get(rpc);
	daos_prop_t		 *prop = NULL;
	struct ds_pool_map_bc    *map_bc;
	uint32_t		  map_version = 0;
	struct pool_svc		 *svc;
	struct pool_metrics	 *metrics;
	struct rdb_tx		  tx;
	d_iov_t			  key;
	d_iov_t			  value;
	crt_bulk_t                bulk;
	uint64_t                  query_bits;
	int			  rc;
	struct daos_prop_entry	 *entry;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pqi_op.pi_uuid), rpc, DP_UUID(in->pqi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pqi_op.pi_uuid, &svc,
				    &out->pqo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	pool_query_in_get_data(rpc, &bulk, &query_bits);

	if (query_bits & DAOS_PO_QUERY_REBUILD_STATUS) {
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

	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_GLOBAL_VERSION, &prop);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_GLOBAL_VERSION);
	D_ASSERT(entry != NULL);
	out->pqo_pool_layout_ver    = entry->dpe_val;
	out->pqo_upgrade_layout_ver = DAOS_POOL_GLOBAL_VERSION;
	daos_prop_free(prop);
	prop = NULL;

	/* read optional properties */
	rc = pool_prop_read(&tx, svc, query_bits, &prop);
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
			case DAOS_PROP_PO_CHECKPOINT_MODE:
			case DAOS_PROP_PO_CHECKPOINT_FREQ:
			case DAOS_PROP_PO_CHECKPOINT_THRESH:
			case DAOS_PROP_PO_REINT_MODE:
			case DAOS_PROP_PO_SVC_OPS_ENABLED:
			case DAOS_PROP_PO_SVC_OPS_ENTRY_AGE:
			case DAOS_PROP_PO_DATA_THRESH:
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
			D_GOTO(out_lock, rc);
		}
	}

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out_svc;

	rc = ds_pool_lookup_map_bc(svc->ps_pool, rpc->cr_ctx, &map_bc, &map_version);
	if (rc != 0)
		goto out_svc;
	rc = ds_pool_transfer_map_buf(map_bc, rpc, bulk, &out->pqo_map_buf_size);
	ds_pool_put_map_bc(map_bc);
	if (rc != 0)
		goto out_svc;

	metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];

	/* See comment above, rebuild doesn't connect the pool */
	if (query_bits & DAOS_PO_QUERY_SPACE) {
		uint64_t *mem_file_bytes = handler_version >= 7 ? &out->pqo_mem_file_bytes : NULL;

		rc = pool_space_query_bcast(rpc->cr_ctx, svc, in->pqi_op.pi_hdl, &out->pqo_space,
					    mem_file_bytes);
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
ds_pool_query_handler_v6(crt_rpc_t *rpc)
{
	pool_query_handler(rpc, 6);
}

void
ds_pool_query_handler(crt_rpc_t *rpc)
{
	pool_query_handler(rpc, DAOS_POOL_VERSION);
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
pool_query_tgt_space(crt_context_t ctx, struct pool_svc *svc, uuid_t pool_hdl, d_rank_t rank,
		     uint32_t tgt_idx, struct daos_space *ds, uint64_t *mem_file_bytes)
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

	D_ASSERT(ds != NULL);
	out = crt_reply_get(rpc);
	rc  = out->tqo_rc;
	if (rc == 0) {
		*ds = out->tqo_space.ps_space;
		if (mem_file_bytes != NULL)
			*mem_file_bytes = out->tqo_mem_file_bytes;
	} else {
		D_ERROR(DF_UUID ": failed to query rank:%u, tgt:%u, " DF_RC "\n",
			DP_UUID(svc->ps_uuid), rank, tgt_idx, DP_RC(rc));
	}
out_rpc:
	crt_req_decref(rpc);
	return rc;
}

static void
pool_query_info_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_query_info_in	*in = crt_req_get(rpc);
	struct pool_query_info_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct pool_target		*target = NULL;
	int				 tgt_state;
	uint32_t                         rank;
	uint32_t                         tgt;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pqii_op.pi_uuid), rpc, DP_UUID(in->pqii_op.pi_hdl));

	pool_query_info_in_get_data(rpc, &rank, &tgt);

	rc = pool_svc_lookup_leader(in->pqii_op.pi_uuid, &svc,
				    &out->pqio_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	/* get the target state from pool map */
	ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
	rc = pool_map_find_target_by_rank_idx(svc->ps_pool->sp_map, rank, tgt, &target);
	if (rc != 1) {
		D_ERROR(DF_UUID ": Failed to get rank:%u, idx:%d\n, rc:%d",
			DP_UUID(in->pqii_op.pi_uuid), rank, tgt, rc);
		ABT_rwlock_unlock(svc->ps_pool->sp_lock);
		D_GOTO(out_svc, rc = -DER_NONEXIST);
	} else {
		rc = 0;
	}

	D_ASSERT(target != NULL);

	tgt_state = target->ta_comp.co_status;
	out->pqio_state = enum_pool_comp_state_to_tgt_state(tgt_state);
	out->pqio_op.po_map_version = pool_map_get_version(svc->ps_pool->sp_map);

	ABT_rwlock_unlock(svc->ps_pool->sp_lock);

	if (tgt_state == PO_COMP_ST_UPIN) {
		uint64_t *mem_file_bytes = handler_version >= 7 ? &out->pqio_mem_file_bytes : NULL;

		rc = pool_query_tgt_space(rpc->cr_ctx, svc, in->pqii_op.pi_hdl, rank, tgt,
					  &out->pqio_space, mem_file_bytes);
		if (rc)
			DL_ERROR(rc, DF_UUID ": Failed to query rank:%u, tgt:%d",
				 DP_UUID(in->pqii_op.pi_uuid), rank, tgt);
	} else {
		memset(&out->pqio_space, 0, sizeof(out->pqio_space));
	}
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pqio_op.po_hint);
	pool_svc_put_leader(svc);
out:
	out->pqio_op.po_rc = rc;
	out->pqio_rank     = rank;
	out->pqio_tgt      = tgt;

	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pqii_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_query_info_handler_v6(crt_rpc_t *rpc)
{
	pool_query_info_handler(rpc, 6);
}

void
ds_pool_query_info_handler(crt_rpc_t *rpc)
{
	pool_query_info_handler(rpc, DAOS_POOL_VERSION);
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
	uint64_t                         query_bits;
	int				rc;
	daos_prop_t			*prop = NULL;

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pgi_op.pi_uuid), rpc);

	pool_prop_get_in_get_data(rpc, &query_bits);

	rc = pool_svc_lookup_leader(in->pgi_op.pi_uuid, &svc,
				    &out->pgo_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_rdlock(svc->ps_lock);

	rc = pool_prop_read(&tx, svc, query_bits, &prop);
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
 * Set a pool's properties without having a handle for the pool
 */
void
ds_pool_prop_set_handler(crt_rpc_t *rpc)
{
	struct pool_prop_set_in		*in = crt_req_get(rpc);
	struct pool_prop_set_out	*out = crt_reply_get(rpc);
	struct pool_svc			*svc;
	struct rdb_tx			tx;
	daos_prop_t                     *prop_in = NULL;
	daos_prop_t			*prop = NULL;
	bool                             dup_op  = false;
	struct ds_pool_svc_op_val        op_val;
	bool                             fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                             fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);
	int				rc;

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->psi_op.pi_uuid), rpc);

	pool_prop_set_in_get_data(rpc, &prop_in);

	rc = pool_svc_lookup_leader(in->psi_op.pi_uuid, &svc, &out->pso_op.po_hint);
	if (rc != 0)
		D_GOTO(out, rc);

	if (!daos_prop_valid(prop_in, true /* pool */, true /* input */)) {
		D_ERROR(DF_UUID": invalid properties input\n",
			DP_UUID(in->psi_op.pi_uuid));
		D_GOTO(out_svc, rc = -DER_INVAL);
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_op_lookup(&tx, svc, rpc, DAOS_POOL_VERSION, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;

	rc = pool_prop_write(&tx, &svc->ps_root, prop_in);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to write prop for pool: %d\n",
			DP_UUID(in->psi_op.pi_uuid), rc);
		D_GOTO(out_commit, rc);
	}

out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, DAOS_POOL_VERSION, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	if (op_val.ov_rc != 0)
		D_GOTO(out_lock, rc = op_val.ov_rc);

	/* Read all props & update prop IV */
	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ALL, &prop);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to read prop for pool, rc=%d\n",
			DP_UUID(in->psi_op.pi_uuid), rc);
		D_GOTO(out_lock, rc);
	}
	D_ASSERT(prop != NULL);

	rc = op_val.ov_rc;
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
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->psi_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->psi_op.pi_uuid));
	}

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
pool_upgrade_props(struct rdb_tx *tx, struct pool_svc *svc, uuid_t pool_uuid, crt_rpc_t *rpc,
		   uuid_t srv_pool_hdl, uuid_t srv_cont_hdl)
{
	d_iov_t			value;
	uint64_t		val;
	uint32_t		val32;
	uuid_t                  valuuid;
	int			rc;
	bool			need_commit = false;
	uuid_t		       *hdl_uuids = NULL;
	size_t			hdl_uuids_size;
	int			n_hdl_uuids = 0;
	uint32_t		connectable;
	uint32_t                svc_ops_enabled = 0;
	uint32_t		svc_ops_age;
	uint32_t                svc_ops_max;

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

	d_iov_set(&value, &val, sizeof(val));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_data_thresh, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		val = DAOS_PROP_PO_DATA_THRESH_DEFAULT;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_data_thresh, &value);
		if (rc) {
			D_ERROR(DF_UUID": failed to upgrade 'data threshold' "
				"of pool, %d.\n", DP_UUID(pool_uuid), rc);
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

	d_iov_set(&value, &val32, sizeof(val32));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_reint_mode, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		val32 = DAOS_PROP_PO_REINT_MODE_DEFAULT;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_reint_mode, &value);
		if (rc != 0) {
			D_ERROR("failed to write pool reintegration mode prop, "DF_RC"\n",
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

	/* Upgrade for the pool/container service operations KVS */
	D_DEBUG(DB_MD, DF_UUID ": check ds_pool_prop_svc_ops\n", DP_UUID(pool_uuid));
	d_iov_set(&value, NULL, 0);
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR(DF_UUID ": failed to lookup service ops KVS: %d\n", DP_UUID(pool_uuid), rc);
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		struct rdb_kvs_attr attr;
		uint32_t            svc_ops_num;

		D_DEBUG(DB_MD, DF_UUID ": creating service ops KVS\n", DP_UUID(pool_uuid));
		attr.dsa_class = RDB_KVS_LEXICAL;
		attr.dsa_order = 16;
		rc             = rdb_tx_create_kvs(tx, &svc->ps_root, &ds_pool_prop_svc_ops, &attr);
		if (rc != 0) {
			D_ERROR(DF_UUID ": failed to create service ops KVS: %d\n",
				DP_UUID(pool_uuid), rc);
			D_GOTO(out_free, rc);
		}
		svc_ops_num = 0;
		d_iov_set(&value, &svc_ops_num, sizeof(svc_ops_num));
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_svc_ops_num, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to write upgrade svc_ops_num",
				 DP_UUID(pool_uuid));
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	/* And enable the new service operations KVS only if rdb is large enough */
	D_DEBUG(DB_MD, DF_UUID ": check ds_pool_prop_svc_ops_enabled\n", DP_UUID(pool_uuid));
	d_iov_set(&value, &svc_ops_enabled, sizeof(svc_ops_enabled));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops_enabled, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR(DF_UUID ": failed to lookup service ops enabled boolean: %d\n",
			DP_UUID(pool_uuid), rc);
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		uint64_t rdb_nbytes;

		D_DEBUG(DB_MD, DF_UUID ": creating service ops enabled boolean\n",
			DP_UUID(pool_uuid));

		rc = rdb_get_size(tx->dt_db, &rdb_nbytes);
		if (rc != 0)
			D_GOTO(out_free, rc);
		if (rdb_nbytes >= DUP_OP_MIN_RDB_SIZE)
			svc_ops_enabled = 1;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_svc_ops_enabled, &value);
		if (rc != 0) {
			D_ERROR(DF_UUID ": set svc_ops_enabled=%d failed, " DF_RC "\n",
				DP_UUID(pool_uuid), svc_ops_enabled, DP_RC(rc));
			D_GOTO(out_free, rc);
		}
		D_DEBUG(DB_MD,
			DF_UUID ": duplicate RPC detection %s (rdb size: " DF_U64 " %s %u)\n",
			DP_UUID(pool_uuid), svc_ops_enabled ? "enabled" : "disabled", rdb_nbytes,
			svc_ops_enabled ? ">=" : "<", DUP_OP_MIN_RDB_SIZE);
		need_commit = true;
	}

	D_DEBUG(DB_MD, DF_UUID ": check ds_pool_prop_svc_ops_age\n", DP_UUID(pool_uuid));
	d_iov_set(&value, &svc_ops_age, sizeof(svc_ops_age));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops_age, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		svc_ops_age = DAOS_PROP_PO_SVC_OPS_ENTRY_AGE_DEFAULT;
		rc    = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_svc_ops_age, &value);
		if (rc != 0) {
			DL_ERROR(rc, "failed to write upgrade svc_ops_age");
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	D_DEBUG(DB_MD, DF_UUID ": check ds_pool_prop_svc_ops_max\n", DP_UUID(pool_uuid));
	d_iov_set(&value, &svc_ops_max, sizeof(svc_ops_max));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_svc_ops_max, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		svc_ops_max = PS_OPS_PER_SEC * svc_ops_age;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_svc_ops_max, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to write upgrade svc_ops_max",
				 DP_UUID(pool_uuid));
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	/*
	 * Initialize server pool and container handles in the DB. To be conservative, we require
	 * the old server pool and container handles to be initialized already in memory, and use
	 * their existing values instead of generating new UUIDs.
	 */
	d_iov_set(&value, valuuid, sizeof(uuid_t));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_srv_handle, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		if (srv_pool_hdl != NULL && !uuid_is_null(srv_pool_hdl)) {
			uuid_copy(valuuid, srv_pool_hdl);
		} else if (!uuid_is_null(svc->ps_pool->sp_srv_pool_hdl)) {
			uuid_copy(valuuid, svc->ps_pool->sp_srv_pool_hdl);
		} else {
			D_ERROR(DF_UUID ": server pool handle unavailable\n", DP_UUID(pool_uuid));
			D_GOTO(out_free, rc);
		}
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_srv_handle, &value);
		if (rc) {
			DL_ERROR(rc, DF_UUID ": failed to upgrade server pool handle",
				 DP_UUID(pool_uuid));
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_srv_cont_handle, &value);
	if (rc && rc != -DER_NONEXIST) {
		D_GOTO(out_free, rc);
	} else if (rc == -DER_NONEXIST) {
		if (srv_cont_hdl != NULL && !uuid_is_null(srv_cont_hdl)) {
			uuid_copy(valuuid, srv_cont_hdl);
		} else if (!uuid_is_null(svc->ps_pool->sp_srv_cont_hdl)) {
			uuid_copy(valuuid, svc->ps_pool->sp_srv_cont_hdl);
		} else {
			D_ERROR(DF_UUID ": server container handle unavailable\n",
				DP_UUID(pool_uuid));
			D_GOTO(out_free, rc);
		}
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_srv_cont_handle, &value);
		if (rc) {
			DL_ERROR(rc, DF_UUID ": failed to upgrade server container handle",
				 DP_UUID(pool_uuid));
			D_GOTO(out_free, rc);
		}
		need_commit = true;
	}

	D_DEBUG(DB_MD, DF_UUID ": need_commit=%s\n", DP_UUID(pool_uuid),
		need_commit ? "true" : "false");
	if (need_commit) {
		daos_prop_t *prop = NULL;

		rc = rdb_tx_commit(tx);
		if (rc)
			D_GOTO(out_free, rc);

		svc->ps_ops_enabled = svc_ops_enabled;
		svc->ps_ops_age     = svc_ops_age;
		svc->ps_ops_max     = svc_ops_max;

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
ds_pool_mark_connectable_internal(struct rdb_tx *tx, struct pool_svc *svc)
{
	d_iov_t		value;
	uint32_t	connectable = 0;
	int		rc;

	d_iov_set(&value, &connectable, sizeof(connectable));
	rc = rdb_tx_lookup(tx, &svc->ps_root, &ds_pool_prop_connectable, &value);
	if ((rc == 0 && connectable == 0) || rc == -DER_NONEXIST) {
		connectable = 1;
		rc = rdb_tx_update(tx, &svc->ps_root, &ds_pool_prop_connectable, &value);
		if (rc == 0)
			rc = 1;
	}

	if (rc < 0)
		D_ERROR("Failed to mark connectable of pool "DF_UUIDF": "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));

	return rc;
}

static int
__ds_pool_mark_upgrade_completed(uuid_t pool_uuid, struct pool_svc *svc, int rc)
{
	struct rdb_tx			tx;
	d_iov_t				value;
	uint32_t			upgrade_status;
	uint32_t			global_version = DAOS_POOL_GLOBAL_VERSION;
	uint32_t			obj_version;
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

	if (rc != 0) {
		/*
		 * Currently, the upgrade global version may have not been updated yet, if
		 * pool_upgrade_props has encountered an error.
		 */
		d_iov_set(&value, &global_version, sizeof(global_version));
		rc1 = rdb_tx_update(&tx, &svc->ps_root, &ds_pool_prop_upgrade_global_version,
				    &value);
		if (rc1) {
			DL_ERROR(rc1, "failed to write upgrade global version prop");
			D_GOTO(out_tx, rc1);
		}
	}

	/*
	 * only bump global version and connectable properties
	 * if upgrade succeed.
	 */
	if (rc == 0) {
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

		rc1 = ds_pool_mark_connectable_internal(&tx, svc);
		if (rc1 < 0) {
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
ds_pool_upgrade_if_needed(uuid_t pool_uuid, struct rsvc_hint *po_hint, struct pool_svc *svc,
			  crt_rpc_t *rpc, uuid_t srv_pool_hdl, uuid_t srv_cont_hdl)
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
			if (DAOS_FAIL_CHECK(DAOS_FORCE_OBJ_UPGRADE)) {
				D_GOTO(out_upgrade, rc = 0);
			} else if (upgrade_global_ver < DAOS_POOL_GLOBAL_VERSION &&
				   dmg_upgrade_cmd) {
				if (DAOS_POOL_GLOBAL_VERSION - upgrade_global_ver == 1)
					D_GOTO(out_upgrade, rc = 0);
				D_ERROR(DF_UUID ": upgrading pool %u -> %u\n is unsupported"
						" please upgrade pool to %u firstly\n",
					DP_UUID(svc->ps_uuid), upgrade_global_ver,
					DAOS_POOL_GLOBAL_VERSION, upgrade_global_ver + 1);
				D_GOTO(out_tx, rc = -DER_NOTSUPPORTED);
			} else {
				D_GOTO(out_tx, rc = 0);
			}
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
	rc = pool_upgrade_props(&tx, svc, pool_uuid, rpc, srv_pool_hdl, srv_cont_hdl);
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

	rc = ds_pool_upgrade_if_needed(in->poi_op.pi_uuid, &out->poo_op.po_hint, NULL, rpc, NULL,
				       NULL);
	out->poo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->poi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
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
	struct daos_acl                 *acl_in = NULL;
	daos_prop_t			*prop = NULL;
	struct daos_prop_entry		*entry = NULL;
	bool                             dup_op = false;
	struct ds_pool_svc_op_val        op_val;
	bool                             fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                             fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);

	D_DEBUG(DB_MD, DF_UUID": processing rpc %p\n",
		DP_UUID(in->pui_op.pi_uuid), rpc);

	pool_acl_update_in_get_data(rpc, &acl_in);

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

	rc = pool_op_lookup(&tx, svc, rpc, DAOS_POOL_VERSION, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;

	rc = pool_prop_read(&tx, svc, DAOS_PO_QUERY_PROP_ACL, &prop);
	if (rc != 0)
		D_GOTO(out_prop, rc);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
	if (entry == NULL) {
		D_ERROR(DF_UUID": No ACL prop entry for pool\n",
			DP_UUID(in->pui_op.pi_uuid));
		D_GOTO(out_prop, rc);
	}

	rc = merge_acl((struct daos_acl **)&entry->dpe_val_ptr, acl_in);
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

out_prop:
	if (prop != NULL)
		daos_prop_free(prop);
out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, DAOS_POOL_VERSION, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	rc = op_val.ov_rc;
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->puo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->pui_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->pui_op.pi_uuid));
	}

	out->puo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->pui_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
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
	bool                             dup_op = false;
	struct ds_pool_svc_op_val        op_val;
	bool                             fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                             fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);

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

	rc = pool_op_lookup(&tx, svc, rpc, DAOS_POOL_VERSION, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;

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

out_prop:
	if (prop != NULL)
		daos_prop_free(prop);
out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, DAOS_POOL_VERSION, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	rc = op_val.ov_rc;
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pdo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->pdi_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->pdi_op.pi_uuid));
	}

	out->pdo_op.po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d\n", DP_UUID(in->pdi_op.pi_uuid), rpc, rc);
	crt_reply_send(rpc);
}

struct pool_svc_reconf_arg {
	struct pool_map	       *sca_map;
	uint32_t		sca_map_version_for;
	bool			sca_sync_remove;
};

/* Must be used with pool_svc.ps_reconf_sched (see container_of below). */
static void
pool_svc_reconf_ult(void *varg)
{
	struct pool_svc_sched      *reconf = varg;
	struct pool_svc_reconf_arg *arg    = reconf->psc_arg;
	struct pool_svc            *svc;
	struct pool_map            *map;
	d_rank_list_t              *pre;
	d_rank_list_t              *to_add;
	d_rank_list_t              *to_remove;
	d_rank_list_t              *post;
	uint64_t                    rdb_nbytes = 0;
	int                         rc;

	svc = container_of(reconf, struct pool_svc, ps_reconf_sched);

	if (arg->sca_map == NULL)
		map = svc->ps_pool->sp_map;
	else
		map = arg->sca_map;

	D_DEBUG(DB_MD, DF_UUID": begin\n", DP_UUID(svc->ps_uuid));

	if (reconf->psc_canceled) {
		rc = -DER_OP_CANCELED;
		goto out;
	}

	/* When there are pending events, the pool map may be unstable. */
	while (!arg->sca_sync_remove && events_pending(svc)) {
		dss_sleep(3000 /* ms */);
		if (reconf->psc_canceled) {
			rc = -DER_OP_CANCELED;
			goto out;
		}
	}

	rc = rdb_get_ranks(svc->ps_rsvc.s_db, &pre);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to get pool service replica ranks: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out;
	}

	/* If adding replicas, get the correct rdb size (do not trust DAOS_MD_CAP). */
	rc = rdb_get_size(svc->ps_rsvc.s_db, &rdb_nbytes);
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to get rdb size: " DF_RC "\n", DP_UUID(svc->ps_uuid),
			DP_RC(rc));
		goto out_cur;
	}

	if (arg->sca_map == NULL)
		ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
	rc = ds_pool_plan_svc_reconfs(svc->ps_svc_rf, map, pre, dss_self_rank(),
				      arg->sca_sync_remove /* filter_only */, &to_add, &to_remove);
	if (arg->sca_map == NULL)
		ABT_rwlock_unlock(svc->ps_pool->sp_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot plan pool service reconfigurations: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out_cur;
	}

	D_DEBUG(DB_MD, DF_UUID ": svc_rf=%d pre=%u to_add=%u to_remove=%u\n", DP_UUID(svc->ps_uuid),
		svc->ps_svc_rf, pre->rl_nr, to_add->rl_nr, to_remove->rl_nr);

	/*
	 * Ignore the return values from the "add" and "remove" calls here. If
	 * the "add" calls returns an error, to_add contains the N ranks that
	 * have not been added. We delete N ranks from to_remove to account for
	 * the failed additions, and continue to make the "remove" call. If any
	 * of the two calls returns an error, we still need to report any
	 * membership changes to the MS.
	 */
	if (!arg->sca_sync_remove && to_add->rl_nr > 0) {
		uint32_t vos_df_version;

		vos_df_version = ds_pool_get_vos_df_version(svc->ps_global_version);
		D_ASSERTF(vos_df_version != 0, DF_UUID ": vos_df_version=0 global_version=%u\n",
			  DP_UUID(svc->ps_uuid), svc->ps_global_version);
		ds_rsvc_add_replicas_s(&svc->ps_rsvc, to_add, rdb_nbytes, vos_df_version);
		if (reconf->psc_canceled) {
			rc = -DER_OP_CANCELED;
			goto out_to_add_remove;
		}
		if (to_add->rl_nr > to_remove->rl_nr)
			to_remove->rl_nr = 0;
		else
			to_remove->rl_nr -= to_add->rl_nr;
	}
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

	if (rdb_get_ranks(svc->ps_rsvc.s_db, &post) == 0) {
		if (svc->ps_force_notify || !d_rank_list_identical(post, pre)) {
			int rc_tmp;

			/*
			 * Send RAS event to control-plane over dRPC to indicate
			 * change in pool service replicas.
			 */
			rc_tmp = ds_notify_pool_svc_update(&svc->ps_uuid, post,
							   svc->ps_rsvc.s_term);
			if (rc_tmp == 0)
				svc->ps_force_notify = false;
			else
				DL_ERROR(rc_tmp, DF_UUID": replica update notify failure",
					 DP_UUID(svc->ps_uuid));
		}

		d_rank_list_free(post);
	}
	if (reconf->psc_canceled) {
		rc = -DER_OP_CANCELED;
		goto out_to_add_remove;
	}

	/*
	 * Don't attempt to destroy any removed replicas in the "synchronous
	 * remove" mode, so that we don't delay pool_svc_update_map_internal
	 * for too long. Ignore the return value of this ds_rsvc_dist_stop
	 * call.
	 */
	if (!arg->sca_sync_remove && to_remove->rl_nr > 0)
		ds_rsvc_dist_stop(svc->ps_rsvc.s_class, &svc->ps_rsvc.s_id, to_remove,
				  NULL /* excluded */, svc->ps_rsvc.s_term, true /* destroy */);

out_to_add_remove:
	d_rank_list_free(to_remove);
	d_rank_list_free(to_add);
out_cur:
	d_rank_list_free(pre);
out:
	/* Do not yield between the D_FREE and the sched_end. */
	D_FREE(reconf->psc_arg);
	reconf->psc_rc = rc;
	sched_end(reconf);
	ABT_cond_broadcast(reconf->psc_cv);
	D_DEBUG(DB_MD, DF_UUID": end: "DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
}

/* If returning 0, this function must have scheduled func(arg). */
static int
pool_svc_schedule(struct pool_svc *svc, struct pool_svc_sched *sched, void (*func)(void *),
		  void *arg)
{
	enum ds_rsvc_state	state;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": begin\n", DP_UUID(svc->ps_uuid));

	if (ds_pool_restricted(svc->ps_pool, false)) {
		D_DEBUG(DB_MD, DF_UUID": end: skip in check mode\n", DP_UUID(svc->ps_uuid));
		return -DER_OP_CANCELED;
	}

	/*
	 * Avoid scheduling when the PS is stepping down
	 * and has already called sched_cancel_and_wait.
	 */
	state = ds_rsvc_get_state(&svc->ps_rsvc);
	if (state == DS_RSVC_DRAINING) {
		D_DEBUG(DB_MD, DF_UUID": end: service %s\n", DP_UUID(svc->ps_uuid),
			ds_rsvc_state_str(state));
		return -DER_OP_CANCELED;
	}

	D_ASSERT(&svc->ps_reconf_sched == sched || &svc->ps_rfcheck_sched == sched);
	sched_cancel_and_wait(sched);

	sched_begin(sched, arg);

	/*
	 * An extra svc leader reference is not required, because
	 * pool_svc_step_down_cb waits for this ULT to terminate.
	 *
	 * ULT tracking is achieved through sched, not a ULT handle.
	 */
	rc = dss_ult_create(func, sched, DSS_XS_SELF, 0, 0, NULL /* ult */);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create ULT: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		sched_end(sched);
		return rc;
	}

	D_DEBUG(DB_MD, DF_UUID": end: "DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
	return 0;
}

/**
 * Schedule PS reconfigurations (if necessary). This is currently for the chk
 * module only.
 */
int
ds_pool_svc_schedule_reconf(struct ds_pool_svc *svc)
{
	struct pool_svc *s = pool_ds2svc(svc);
	int              rc;

	/*
	 * Pass 1 as map_version_for, since there shall be no other
	 * reconfiguration in progress.
	 */
	s->ps_pool->sp_cr_checked = 1;
	rc = pool_svc_schedule_reconf(s, NULL /* map */, 1 /* map_version_for */,
				      true /* sync_remove */);
	if (rc != 0)
		DL_ERROR(rc, DF_UUID": failed to schedule pool service reconfiguration",
			 DP_UUID(s->ps_uuid));
	return rc;
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
		DL_CDEBUG(rc == -DER_RF, DB_MD, DLOG_ERR, rc, DF_CONT " check_rf",
			  DP_CONT(pool_uuid, cont_uuid));

	if (sched->psc_canceled) {
		D_DEBUG(DB_MD, DF_CONT" is canceled.\n", DP_CONT(pool_uuid, cont_uuid));
		return 1;
	}

	return 0;
}

/* Must be used with pool_svc.ps_rfcheck_sched (see container_of below). */
static void
pool_svc_rfcheck_ult(void *arg)
{
	struct pool_svc_sched *sched = arg;
	struct pool_svc       *svc   = container_of(sched, struct pool_svc, ps_rfcheck_sched);
	int                    rc;

	do {
		/* retry until some one stop the pool svc(rc == 1) or succeed */
		if (DAOS_FAIL_CHECK(DAOS_POOL_RFCHECK_FAIL))
			rc = -DER_NOMEM;
		else
			rc = ds_cont_rdb_iterate(svc->ps_cont_svc, cont_rf_check_cb,
						 &svc->ps_rfcheck_sched);
		if (rc >= 0)
			break;

		if (sched->psc_canceled) {
			D_DEBUG(DB_MD, DF_UUID ": canceled\n", DP_UUID(svc->ps_uuid));
			break;
		}

		D_DEBUG(DB_MD, DF_UUID" check rf with %d and retry\n",
			DP_UUID(svc->ps_uuid), rc);

		dss_sleep(1000 /* ms */);
	} while (1);

	sched_end(&svc->ps_rfcheck_sched);
	D_INFO("RF check finished for "DF_UUID"\n", DP_UUID(svc->ps_uuid));
	ABT_cond_broadcast(svc->ps_rfcheck_sched.psc_cv);
}

/*
 * If map is NULL, map_version_for must be provided, and svc->ps_pool->sp_map
 * will be used during reconfiguration; otherwise, map_version_for is ignored.
 */
static int
pool_svc_schedule_reconf(struct pool_svc *svc, struct pool_map *map, uint32_t map_version_for,
			 bool sync_remove)
{
	struct pool_svc_reconf_arg     *reconf_arg;
	uint32_t			v;
	int				rc;

	if (map == NULL)
		v = map_version_for;
	else
		v = pool_map_get_version(map);

	if (svc->ps_reconf_sched.psc_in_progress) {
		uint32_t v_in_progress;

		/* Safe to access psc_arg as long as we don't yield. */
		reconf_arg = svc->ps_reconf_sched.psc_arg;
		if (reconf_arg->sca_map == NULL)
			v_in_progress = reconf_arg->sca_map_version_for;
		else
			v_in_progress = pool_map_get_version(reconf_arg->sca_map);
		if (v_in_progress >= v) {
			D_DEBUG(DB_MD, DF_UUID": stale request: v_in_progress=%u v=%u\n",
				DP_UUID(svc->ps_uuid), v_in_progress, v);
			return -DER_OP_CANCELED;
		}
	}

	D_ALLOC_PTR(reconf_arg);
	if (reconf_arg == NULL)
		return -DER_NOMEM;
	reconf_arg->sca_map = map;
	reconf_arg->sca_map_version_for = v;
	reconf_arg->sca_sync_remove = sync_remove;

	/*
	 * If successful, this call passes the ownership of reconf_arg to
	 * pool_svc_reconf_ult.
	 */
	rc = pool_svc_schedule(svc, &svc->ps_reconf_sched, pool_svc_reconf_ult, reconf_arg);
	if (rc != 0) {
		D_FREE(reconf_arg);
		return rc;
	}

	if (sync_remove) {
		sched_wait(&svc->ps_reconf_sched);

		rc = svc->ps_reconf_sched.psc_rc;
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID": pool service reconfigurator", DP_UUID(svc->ps_uuid));
			return rc;
		}
	}

	return 0;
}

static int
pool_map_crit_prompt(struct pool_svc *svc, struct pool_map *map)
{
	crt_group_t		*primary_grp;
	struct pool_domain	*doms;
	int			 doms_cnt;
	int			 i;
	int			 rc = 0;

	D_DEBUG(DB_MD, DF_UUID": checking node status\n", DP_UUID(svc->ps_uuid));
	doms_cnt = pool_map_find_ranks(map, PO_COMP_ID_ALL, &doms);
	D_ASSERT(doms_cnt >= 0);
	primary_grp = crt_group_lookup(NULL);
	D_ASSERT(primary_grp != NULL);

	D_CRIT("!!! Please try to recover these engines in top priority -\n");
	D_CRIT("!!! Please refer \"Pool-Wise Redundancy Factor\" section in pool_operations.md\n");
	for (i = 0; i < doms_cnt; i++) {
		struct swim_member_state state;

		if (!(doms[i].do_comp.co_status & PO_COMP_ST_UPIN))
			continue;

		rc = crt_rank_state_get(primary_grp, doms[i].do_comp.co_rank, &state);
		if (rc != 0 && rc != -DER_NONEXIST) {
			D_ERROR("failed to get status of rank %u: %d\n",
				doms[i].do_comp.co_rank, rc);
			break;
		}

		D_DEBUG(DB_MD, "rank/state %d/%d\n", doms[i].do_comp.co_rank,
			rc == -DER_NONEXIST ? -1 : state.sms_status);
		if (rc == -DER_NONEXIST || state.sms_status == SWIM_MEMBER_DEAD)
			D_CRIT("!!! pool "DF_UUID" : intolerable unavailability: engine rank %u\n",
			       DP_UUID(svc->ps_uuid), doms[i].do_comp.co_rank);
	}

	return rc;
}

/*
 * Perform an update to the pool map of \a svc.
 *
 * \param[in]	svc		pool service
 * \param[in]	opc		update operation (e.g., POOL_EXCLUDE)
 * \param[in]	exclude_rank	for excluding ranks (rather than targets)
 * \param[in]	extend_rank_list
 *				ranks list to be extended.
 * \param[in]	extend_domains_nr
 *				number of extend domains.
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
 * \param[in]	src		source of the map update
 * \param[in]	skip_rf_check	skip the RF check
 */
static int
pool_svc_update_map_internal(struct pool_svc *svc, unsigned int opc, bool exclude_rank,
			     d_rank_list_t *extend_rank_list, uint32_t extend_domains_nr,
			     uint32_t *extend_domains, struct pool_target_id_list *tgts,
			     struct pool_target_addr_list *tgt_addrs, struct rsvc_hint *hint,
			     bool *p_updated, uint32_t *map_version_p, uint32_t *tgt_map_ver,
			     struct pool_target_addr_list *inval_tgt_addrs,
			     enum map_update_source src, bool skip_rf_check)
{
	struct rdb_tx		tx;
	struct pool_map	       *map;
	uint32_t		map_version_before;
	uint32_t		map_version;
	struct pool_buf	       *map_buf = NULL;
	struct pool_domain     *node;
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

	if (opc == MAP_EXTEND) {
		D_ASSERT(extend_rank_list != NULL);
		map_version = pool_map_get_version(map) + 1;
		rc          = gen_pool_buf(map, &map_buf, map_version, extend_domains_nr,
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
			if (src == MUS_DMG && inval_tgt_addrs->pta_number > 0) {
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
	rc = ds_pool_map_tgts_update(svc->ps_uuid, map, tgts, opc, exclude_rank, tgt_map_ver, true);
	if (rc != 0)
		D_GOTO(out_map, rc);
	map_version = pool_map_get_version(map);
	D_DEBUG(DB_MD, DF_UUID": version=%u->%u\n",
		DP_UUID(svc->ps_uuid), map_version_before, map_version);
	if (map_version == map_version_before)
		D_GOTO(out_map, rc = 0);

	/*
	 * If the map modification affects myself, leave it to a new PS leader
	 * if there's another PS replica, or reject it.
	 */
	node = pool_map_find_dom_by_rank(map, dss_self_rank());
	if (node == NULL || !(node->do_comp.co_status & DC_POOL_SVC_MAP_STATES)) {
		d_rank_list_t *replicas;

		rc = rdb_get_ranks(svc->ps_rsvc.s_db, &replicas);
		if (replicas->rl_nr == 1) {
			D_ERROR(DF_UUID": rejecting rank exclusion: self removal requested\n",
				DP_UUID(svc->ps_uuid));
			rc = -DER_INVAL;
		} else {
			/*
			 * The handling is unreliable, for we may become a new
			 * PS leader again; a more reliable implementation
			 * requires the currently unavailable Raft leadership
			 * transfer support.
			 */
			D_INFO(DF_UUID": resigning PS leadership: self removal requested\n",
			       DP_UUID(svc->ps_uuid));
			rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
			rc = -DER_NOTLEADER;
		}
		d_rank_list_free(replicas);
		goto out_map;
	}

	/*
	 * For SWIM exclude:
	 * Do not change pool map if the pw_rf is broken or is going to be broken,
	 * With CRIT log message to ask administrator to bring back the engine.
	 *
	 * For DMG exclude:
	 * Do not change the pool map if the `pw_rf` is broken or is about to break,
	 * unless the force option is given.
	 */
	if (!skip_rf_check && opc == MAP_EXCLUDE) {
		int failed_cnt;

		rc = pool_map_update_failed_cnt(map);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID": pool_map_update_failed_cnt failed.",
				 DP_UUID(svc->ps_uuid));
			goto out_map;
		}

		/* TODO DAOS-6353: Update to FAULT when supported */
		failed_cnt = pool_map_get_failed_cnt(map, PO_COMP_TP_NODE);
		D_INFO(DF_UUID ": Exclude %d ranks, failed NODE %d\n", DP_UUID(svc->ps_uuid),
		       tgt_addrs->pta_number, failed_cnt);
		if (failed_cnt > pw_rf) {
			D_CRIT(DF_UUID": exclude %d ranks will break pool RF %d, failed_cnt %d\n",
			       DP_UUID(svc->ps_uuid), tgt_addrs->pta_number, pw_rf, failed_cnt);
			ABT_rwlock_rdlock(svc->ps_pool->sp_lock);
			rc = pool_map_crit_prompt(svc, svc->ps_pool->sp_map);
			ABT_rwlock_unlock(svc->ps_pool->sp_lock);
			if (rc != 0)
				DL_ERROR(rc, DF_UUID ": failed to log prompt",
					 DP_UUID(svc->ps_uuid));
			rc = -DER_RF;
			goto out_map;
		}
	}

	/* Write the new pool map. */
	rc = pool_buf_extract(map, &map_buf);
	if (rc != 0)
		D_GOTO(out_map, rc);
	rc = write_map_buf(&tx, &svc->ps_root, map_buf, map_version);
	if (rc != 0)
		goto out_map_buf;

	/*
	 * Remove all undesired PS replicas (if any) before committing map, so
	 * that the set of PS replicas remains a subset of the pool groups.
	 */
	rc = pool_svc_schedule_reconf(svc, map, 0 /* map_version_for */, true /* sync_remove */);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID": failed to remove undesired pool service replicas",
			 DP_UUID(svc->ps_uuid));
		goto out_map;
	}

	rc = rdb_tx_commit(&tx);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": failed to commit: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		goto out_map_buf;
	}

	DS_POOL_LOG_PRINT(INFO, DF_UUID ": committed pool map: version=%u->%u map=%p\n",
			  DP_UUID(svc->ps_uuid), map_version_before, map_version, map);
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

	/* See events_handler. */
	resume_event_handling(svc);

	rc = pool_svc_schedule_reconf(svc, NULL /* map */, map_version, false /* sync_remove */);
	if (rc != 0) {
		DL_INFO(rc, DF_UUID": failed to schedule pool service reconfiguration",
			DP_UUID(svc->ps_uuid));
		rc = 0;
	}

	if (opc == MAP_EXCLUDE) {
		rc = pool_svc_schedule(svc, &svc->ps_rfcheck_sched, pool_svc_rfcheck_ult, NULL);
		if (rc != 0)
			DL_INFO(rc, DF_UUID": failed to schedule RF check", DP_UUID(svc->ps_uuid));
	}

	pool_svc_update_map_metrics(svc->ps_uuid, map, svc->ps_pool->sp_metrics[DAOS_POOL_MODULE]);

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

	rc = pool_svc_update_map_internal(svc, opc, exclude_rank, NULL, 0, NULL, tgts, tgt_addrs,
					  hint, p_updated, map_version_p, tgt_map_ver,
					  inval_tgt_addrs, MUS_DMG, true);

	pool_svc_put_leader(svc);
	return rc;
}

int
ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, pool_opc_2map_opc(POOL_EXCLUDE_OUT), false,
					list, NULL, NULL, NULL, NULL, NULL, NULL);
}

int
ds_pool_tgt_add_in(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, pool_opc_2map_opc(POOL_ADD_IN), false, list,
					NULL, NULL, NULL, NULL, NULL, NULL);
}

int
ds_pool_tgt_finish_rebuild(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, MAP_FINISH_REBUILD, false, list,
					NULL, NULL, NULL, NULL, NULL, NULL);
}

int
ds_pool_tgt_revert_rebuild(uuid_t pool_uuid, struct pool_target_id_list *list)
{
	return pool_update_map_internal(pool_uuid, MAP_REVERT_REBUILD, false, list,
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
		    struct pool_target_addr_list *inval_list_out, uint32_t *map_version,
		    struct rsvc_hint *hint, enum map_update_source src, bool skip_rf_check)
{
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
					  extend_domains_nr, extend_domains, &target_list, list,
					  hint, &updated, map_version, &tgt_map_ver, inval_list_out,
					  src, skip_rf_check);
	if (rc)
		D_GOTO(out, rc);

	if (!updated)
		D_GOTO(out, rc);

	d_agetenv_str(&env, REBUILD_ENV);
	if ((env && !strcasecmp(env, REBUILD_ENV_DISABLED)) ||
	     daos_fail_check(DAOS_REBUILD_DISABLE)) {
		D_DEBUG(DB_REBUILD, DF_UUID ": Rebuild is disabled for all pools\n",
			DP_UUID(svc->ps_pool->sp_uuid));
		d_freeenv_str(&env);
		D_GOTO(out, rc = 0);
	}
	d_freeenv_str(&env);

	rc = ds_pool_iv_prop_fetch(svc->ps_pool, &prop);
	if (rc)
		D_GOTO(out, rc);

	entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SELF_HEAL);
	D_ASSERT(entry != NULL);
	if (!(entry->dpe_val & (DAOS_SELF_HEAL_AUTO_REBUILD | DAOS_SELF_HEAL_DELAY_REBUILD))) {
		D_DEBUG(DB_MD, "self healing is disabled\n");
		D_GOTO(out, rc);
	}

	if (svc->ps_pool->sp_reint_mode == DAOS_REINT_MODE_NO_DATA_SYNC) {
		D_DEBUG(DB_MD, "self healing is disabled for no_data_sync reintegration mode.\n");
		if (opc == MAP_EXCLUDE || opc == MAP_DRAIN) {
			rc = ds_pool_tgt_exclude_out(svc->ps_pool->sp_uuid, &target_list);
			if (rc)
				D_INFO("mark failed target %d of "DF_UUID " as DOWNOUT: "DF_RC"\n",
					target_list.pti_ids[0].pti_id,
					DP_UUID(svc->ps_pool->sp_uuid), DP_RC(rc));
		}
		D_GOTO(out, rc);
	}

	if ((entry->dpe_val & DAOS_SELF_HEAL_DELAY_REBUILD) && (opc == MAP_EXCLUDE))
		delay = -1;
	else if (daos_fail_check(DAOS_REBUILD_DELAY))
		delay = 5;

	D_DEBUG(DB_MD, "map ver %u/%u\n", map_version ? *map_version : -1,
		tgt_map_ver);

	if (tgt_map_ver != 0) {
		rc = ds_rebuild_schedule(svc->ps_pool, tgt_map_ver, rebuild_eph,
					 0, &target_list, RB_OP_REBUILD, delay);
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

	rc =
	    pool_svc_update_map(svc, pool_opc_2map_opc(opc_get(rpc->cr_opc)),
				false /* exclude_rank */, &rank_list, domains, ndomains, NULL, NULL,
				&out->peo_op.po_map_version, &out->peo_op.po_hint, MUS_DMG, true);

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

	D_ASSERTF(svc->ps_pool->sp_incr_reint == 0, "incremental reint should not get here\n");

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

static void
pool_update_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_tgt_update_in      *in = crt_req_get(rpc);
	struct pool_tgt_update_out     *out = crt_reply_get(rpc);
	struct pool_svc		       *svc;
	struct pool_target_addr_list	list = { 0 };
	struct pool_target_addr_list	inval_list_out = { 0 };
	int				rc;
	uint32_t                        flags;

	pool_tgt_update_in_get_data(rpc, &list.pta_addrs, &list.pta_number, &flags);

	if (list.pta_addrs == NULL || list.pta_number == 0)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p ntargets=%zu\n", DP_UUID(in->pti_op.pi_uuid),
		rpc, (size_t)list.pta_number);

	rc = pool_svc_lookup_leader(in->pti_op.pi_uuid, &svc,
				    &out->pto_op.po_hint);
	if (rc != 0)
		goto out;

	if (opc_get(rpc->cr_opc) == POOL_REINT &&
	    svc->ps_pool->sp_reint_mode == DAOS_REINT_MODE_DATA_SYNC) {
		rc = pool_discard(rpc->cr_ctx, svc, &list);
		if (rc)
			goto out_svc;
	}

	rc = pool_svc_update_map(svc, pool_opc_2map_opc(opc_get(rpc->cr_opc)),
				 false /* exclude_rank */, NULL, NULL, 0, &list, &inval_list_out,
				 &out->pto_op.po_map_version, &out->pto_op.po_hint, MUS_DMG,
				 flags & POOL_TGT_UPDATE_SKIP_RF_CHECK);
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

void
ds_pool_update_handler(crt_rpc_t *rpc)
{
	pool_update_handler(rpc, DAOS_POOL_VERSION);
}

static int
pool_svc_exclude_ranks(struct pool_svc *svc, struct pool_svc_event_set *event_set)
{
	struct pool_target_addr_list	list;
	struct pool_target_addr_list	inval_list_out = { 0 };
	struct pool_target_addr	       *addrs;
	d_rank_t			self_rank = dss_self_rank();
	uint32_t			map_version = 0;
	int				n = 0;
	int				i;
	int				rc;

	D_ALLOC_ARRAY(addrs, event_set->pss_len);
	if (addrs == NULL)
		return -DER_NOMEM;
	for (i = 0; i < event_set->pss_len; i++) {
		struct pool_svc_event *event = &event_set->pss_buf[i];

		if (event->psv_type != CRT_EVT_DEAD)
			continue;
		if (event->psv_src == CRT_EVS_GRPMOD && event->psv_rank == self_rank) {
			D_DEBUG(DB_MD, DF_UUID ": ignore exclusion of self\n",
				DP_UUID(svc->ps_uuid));
			continue;
		}
		addrs[n].pta_rank   = event->psv_rank;
		addrs[n].pta_target = -1;
		n++;
	}
	if (n == 0) {
		rc = 0;
		goto out;
	}
	list.pta_number = n;
	list.pta_addrs  = addrs;

	rc = pool_svc_update_map(svc, pool_opc_2map_opc(POOL_EXCLUDE), true /* exclude_rank */,
				 NULL, NULL, 0, &list, &inval_list_out, &map_version,
				 NULL /* hint */, MUS_SWIM, false);

	D_DEBUG(DB_MD, DF_UUID ": exclude %u ranks: map_version=%u: " DF_RC "\n",
		DP_UUID(svc->ps_uuid), n, rc == 0 ? map_version : 0, DP_RC(rc));
	for (i = 0; i < inval_list_out.pta_number; i++)
		D_DEBUG(DB_MD, DF_UUID ": skipped: rank=%u target=%u\n", DP_UUID(svc->ps_uuid),
			inval_list_out.pta_addrs[i].pta_rank,
			inval_list_out.pta_addrs[i].pta_target);

	pool_target_addr_list_free(&inval_list_out);
out:
	D_FREE(addrs);
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
	struct pool_evict_in     *in  = crt_req_get(rpc);
	struct pool_evict_out    *out = crt_reply_get(rpc);
	struct pool_svc          *svc;
	struct rdb_tx             tx;
	bool                      dup_op = false;
	struct ds_pool_svc_op_val op_val;
	uuid_t                   *hdl_uuids = NULL;
	size_t                    hdl_uuids_size;
	int                       n_hdl_uuids     = 0;
	bool                      fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                      fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);
	int                       rc;

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

	rc = pool_op_lookup(&tx, svc, rpc, DAOS_POOL_VERSION, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;
	/* TODO: (for dup op case) implement per-opcode result data retrieval from stored op_val,
	 *       (for new op case) implement per-opcode result data storage into op_val.
	 */

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
		goto out_commit;
	D_DEBUG(DB_MD, "number of handles found was: %d\n", n_hdl_uuids);

	if (n_hdl_uuids > 0) {
		/* If pool destroy but not forcibly, error: the pool is busy */

		if (in->pvi_pool_destroy && !in->pvi_pool_destroy_force) {
			D_DEBUG(DB_MD, DF_UUID": busy, %u open handles\n",
				DP_UUID(in->pvi_op.pi_uuid), n_hdl_uuids);
			D_GOTO(out_free, rc = -DER_BUSY);
		} else {
			/* Pool evict, or pool destroy with force=true */
			if (DAOS_FAIL_CHECK(DAOS_POOL_EVICT_FAIL))
				rc = 0; /* unrealistic */
			else
				rc = pool_disconnect_hdls(&tx, svc, hdl_uuids, n_hdl_uuids,
							  rpc->cr_ctx);
			if (rc != 0) {
				goto out_free;
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
			goto out_free;

		ds_pool_iv_srv_hdl_invalidate(svc->ps_pool);
		ds_iv_ns_leader_stop(svc->ps_pool->sp_iv_ns);
		D_DEBUG(DB_MD, DF_UUID": pool destroy/evict: mark pool for "
			"no new connections\n", DP_UUID(in->pvi_op.pi_uuid));
	}

out_free:
	D_FREE(hdl_uuids);
out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, DAOS_POOL_VERSION, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;
	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	/* No need to set out->pvo_op.po_map_version. */

	rc = op_val.ov_rc;
	if ((rc == 0) && !dup_op) {
		struct pool_metrics *metrics;

		/** update metric */
		metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
		d_tm_inc_counter(metrics->disconnect_total, 1);
	}
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->pvo_op.po_hint);
	pool_svc_put_leader(svc);
out:
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->pvi_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->pvi_op.pi_uuid));
	}

	out->pvo_op.po_rc = rc;
	out->pvo_n_hdls_evicted = n_hdl_uuids;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pvi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
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
	rc = ds_pool_get_ranks(in->prgi_op.pi_uuid, DC_POOL_GROUP_MAP_STATES, &out_ranks);
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
static void
pool_svc_stop_handler(crt_rpc_t *rpc, int handler_version)
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

void
ds_pool_svc_stop_handler(crt_rpc_t *rpc)
{
	pool_svc_stop_handler(rpc, DAOS_POOL_VERSION);
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
ds_pool_svc_query_map_dist(uuid_t uuid, uint32_t *version, bool *idle)
{
	struct pool_svc	*svc;
	int		rc;

	rc = pool_svc_lookup_leader(uuid, &svc, NULL /* hint */);
	if (rc != 0)
		return rc;

	ds_rsvc_query_map_dist(&svc->ps_rsvc, version, idle);

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

static void
pool_attr_set_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_attr_set_in  *in = crt_req_get(rpc);
	struct pool_op_out	 *out = crt_reply_get(rpc);
	struct pool_svc		 *svc;
	uint64_t                  count;
	crt_bulk_t                bulk;
	struct rdb_tx		  tx;
	bool                      dup_op = false;
	struct ds_pool_svc_op_val op_val;
	bool                      fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                      fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);
	int			  rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pasi_op.pi_uuid), rpc, DP_UUID(in->pasi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pasi_op.pi_uuid, &svc, &out->po_hint);
	if (rc != 0)
		goto out;

	pool_attr_set_in_get_data(rpc, &count, &bulk);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_op_lookup(&tx, svc, rpc, handler_version, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;

	rc = ds_rsvc_set_attr(&svc->ps_rsvc, &tx, &svc->ps_user, bulk, rpc, count);
	if (rc != 0)
		goto out_commit;

out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, handler_version, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	rc = op_val.ov_rc;

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->po_hint);
	pool_svc_put_leader(svc);
out:
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->pasi_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->pasi_op.pi_uuid));
	}

	out->po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->pasi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_attr_set_handler(crt_rpc_t *rpc)
{
	pool_attr_set_handler(rpc, DAOS_POOL_VERSION);
}

static void
pool_attr_del_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_attr_del_in  *in = crt_req_get(rpc);
	struct pool_op_out	 *out = crt_reply_get(rpc);
	struct pool_svc		 *svc;
	uint64_t                  count;
	crt_bulk_t                bulk;
	struct rdb_tx		  tx;
	bool                      dup_op = false;
	struct ds_pool_svc_op_val op_val;
	bool                      fi_pass_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_PASS_NOREPLY);
	bool                      fi_fail_noreply = DAOS_FAIL_CHECK(DAOS_MD_OP_FAIL_NOREPLY);
	int			  rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->padi_op.pi_uuid), rpc, DP_UUID(in->padi_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->padi_op.pi_uuid, &svc, &out->po_hint);
	if (rc != 0)
		goto out;

	pool_attr_del_in_get_data(rpc, &count, &bulk);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_op_lookup(&tx, svc, rpc, handler_version, &dup_op, &op_val);
	if (rc != 0)
		goto out_lock;
	else if (dup_op || fi_fail_noreply)
		goto out_commit;

	rc = ds_rsvc_del_attr(&svc->ps_rsvc, &tx, &svc->ps_user, bulk, rpc, count);
	if (rc != 0)
		goto out_commit;

out_commit:
	if ((rc == 0) && !dup_op && fi_fail_noreply)
		rc = -DER_MISC;
	rc = pool_op_save(&tx, svc, rpc, handler_version, dup_op, rc, &op_val);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		goto out_lock;
	rc = op_val.ov_rc;
out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out_svc:
	ds_rsvc_set_hint(&svc->ps_rsvc, &out->po_hint);
	pool_svc_put_leader(svc);
out:
	if ((rc == 0) && !dup_op && fi_pass_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_PASS_NOREPLY\n",
			DP_UUID(in->padi_op.pi_uuid));
	}
	if ((rc == -DER_MISC) && !dup_op && fi_fail_noreply) {
		rc = -DER_TIMEDOUT;
		D_DEBUG(DB_MD, DF_UUID ": fault injected: DAOS_MD_OP_FAIL_NOREPLY\n",
			DP_UUID(in->padi_op.pi_uuid));
	}

	out->po_rc = rc;
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->padi_op.pi_uuid), rpc,
		DP_RC(rc));
	crt_reply_send(rpc);
}

void
ds_pool_attr_del_handler(crt_rpc_t *rpc)
{
	pool_attr_del_handler(rpc, DAOS_POOL_VERSION);
}

static void
pool_attr_get_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_attr_get_in  *in = crt_req_get(rpc);
	struct pool_op_out	 *out = crt_reply_get(rpc);
	struct pool_svc		 *svc;
	uint64_t                  count;
	uint64_t                  key_length;
	crt_bulk_t                bulk;
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

	pool_attr_get_in_get_data(rpc, &count, &key_length, &bulk);

	ABT_rwlock_rdlock(svc->ps_lock);
	rc = ds_rsvc_get_attr(&svc->ps_rsvc, &tx, &svc->ps_user, bulk, rpc, count, key_length);
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
ds_pool_attr_get_handler(crt_rpc_t *rpc)
{
	pool_attr_get_handler(rpc, DAOS_POOL_VERSION);
}

static void
pool_attr_list_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_attr_list_in	*in	    = crt_req_get(rpc);
	struct pool_attr_list_out	*out	    = crt_reply_get(rpc);
	struct pool_svc			*svc;
	crt_bulk_t                       bulk;
	struct rdb_tx			 tx;
	int				 rc;

	D_DEBUG(DB_MD, DF_UUID ": processing rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->pali_op.pi_uuid), rpc, DP_UUID(in->pali_op.pi_hdl));

	rc = pool_svc_lookup_leader(in->pali_op.pi_uuid, &svc,
				    &out->palo_op.po_hint);
	if (rc != 0)
		goto out;

	pool_attr_list_in_get_data(rpc, &bulk);

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0)
		goto out_svc;

	ABT_rwlock_rdlock(svc->ps_lock);
	rc = ds_rsvc_list_attr(&svc->ps_rsvc, &tx, &svc->ps_user, bulk, rpc, &out->palo_size);
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
ds_pool_attr_list_handler(crt_rpc_t *rpc)
{
	pool_attr_list_handler(rpc, DAOS_POOL_VERSION);
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
		/*
		 * Before starting to use this unused RPC, we need to fix the
		 * arguments passed to ds_rsvc_add_replicas. The size argument
		 * might need to be retrieved from an existing replica; the
		 * vos_df_version argument needs to be determined somehow.
		 */
		D_ASSERTF(false, "code fixes required before use");
		rc = ds_rsvc_add_replicas(DS_RSVC_CLASS_POOL, &id, ranks, ds_rsvc_get_md_cap(),
					  0 /* vos_df_version */, &out->pmo_hint);
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

/* Query the target(by id)'s status */
int
ds_pool_target_status(struct ds_pool *pool, uint32_t id)
{
	struct pool_target *target;
	int		   rc;

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = pool_map_find_target(pool->sp_map, id, &target);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc <= 0)
		return rc == 0 ? -DER_NONEXIST : rc;

	return (int)target->ta_comp.co_status;
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

int
ds_pool_mark_connectable(struct ds_pool_svc *ds_svc)
{
	struct pool_svc		*svc = pool_ds2svc(ds_svc);
	struct rdb_tx		 tx;
	int			 rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc == 0) {
		ABT_rwlock_wrlock(svc->ps_lock);
		rc = ds_pool_mark_connectable_internal(&tx, svc);
		if (rc > 0)
			rc = rdb_tx_commit(&tx);
		ABT_rwlock_unlock(svc->ps_lock);
		rdb_tx_end(&tx);
	}

	return rc;
}

int
ds_pool_svc_load_map(struct ds_pool_svc *ds_svc, struct pool_map **map)
{
	struct pool_svc		*svc = pool_ds2svc(ds_svc);
	struct rdb_tx		 tx = { 0 };
	int			 rc;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc == 0) {
		ABT_rwlock_rdlock(svc->ps_lock);
		rc = read_map(&tx, &svc->ps_root, map);
		ABT_rwlock_unlock(svc->ps_lock);
		rdb_tx_end(&tx);
	}

	if (rc != 0)
		D_ERROR("Failed to load pool map for pool "DF_UUIDF": "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));

	return rc;
}

int
ds_pool_svc_flush_map(struct ds_pool_svc *ds_svc, struct pool_map *map)
{
	struct pool_svc		*svc = pool_ds2svc(ds_svc);
	struct pool_buf		*buf = NULL;
	struct rdb_tx		 tx = { 0 };
	uint32_t		 version;
	int			 rc = 0;
	bool			 locked = false;

	version = pool_map_get_version(map);
	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0) {
		D_ERROR("Failed to begin TX for flush pool "DF_UUIDF" map with version %u: "
			DF_RC"\n", DP_UUID(svc->ps_uuid), version, DP_RC(rc));
		goto out;
	}

	ABT_rwlock_wrlock(svc->ps_lock);
	locked = true;

	rc = pool_buf_extract(map, &buf);
	if (rc != 0) {
		D_ERROR("Failed to extract buf for flush pool "DF_UUIDF" map with version %u: "
			DF_RC"\n", DP_UUID(svc->ps_uuid), version, DP_RC(rc));
		goto out_lock;
	}

	rc = write_map_buf(&tx, &svc->ps_root, buf, version);
	if (rc != 0) {
		D_ERROR("Failed to write buf for flush pool "DF_UUIDF" map with version %u: "
			DF_RC"\n", DP_UUID(svc->ps_uuid), version, DP_RC(rc));
		goto out_buf;
	}

	rc = rdb_tx_commit(&tx);
	if (rc != 0) {
		D_ERROR("Failed to commit TX for flush pool "DF_UUIDF" map with version %u: "
			DF_RC"\n", DP_UUID(svc->ps_uuid), version, DP_RC(rc));
		goto out_buf;
	}

	/* Update svc->ps_pool to match the new pool map. */
	rc = ds_pool_tgt_map_update(svc->ps_pool, buf, version);
	if (rc != 0) {
		D_ERROR("Failed to refresh local pool "DF_UUIDF" map with version %u: "
			DF_RC"\n", DP_UUID(svc->ps_uuid), version, DP_RC(rc));
		/*
		 * Have to resign to avoid handling future requests with stale pool map cache.
		 * Continue to distribute the new pool map to other pool shards since the RDB
		 * has already been updated.
		 */
		rdb_resign(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term);
	} else {
		ds_rsvc_request_map_dist(&svc->ps_rsvc);
		ABT_rwlock_unlock(svc->ps_lock);
		locked = false;
		ds_rsvc_wait_map_dist(&svc->ps_rsvc);
	}

out_buf:
	pool_buf_free(buf);
out_lock:
	if (locked)
		ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

int
ds_pool_svc_update_label(struct ds_pool_svc *ds_svc, const char *label)
{
	struct pool_svc		*svc = pool_ds2svc(ds_svc);
	daos_prop_t		*prop = NULL;
	struct rdb_tx		 tx = { 0 };
	int			 rc = 0;

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_LABEL;
	if (label != NULL) {
		D_STRNDUP(prop->dpp_entries[0].dpe_str, label, strlen(label));
		if (prop->dpp_entries[0].dpe_str == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		prop->dpp_entries[0].dpe_flags = DAOS_PROP_ENTRY_NOT_SET;
		prop->dpp_entries[0].dpe_str = NULL;
	}

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0) {
		D_ERROR("Failed to begin TX for updating pool "DF_UUIDF" label %s: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), label != NULL ? label : "(null)", DP_RC(rc));
		D_GOTO(out, rc);
	}

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = pool_prop_write(&tx, &svc->ps_root, prop);
	if (rc != 0) {
		D_ERROR("Failed to updating pool "DF_UUIDF" label %s: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), label != NULL ? label : "(null)", DP_RC(rc));
		D_GOTO(out_lock, rc);
	}

	rc = rdb_tx_commit(&tx);
	if (rc != 0)
		D_ERROR("Failed to commit TX for updating pool "DF_UUIDF" label %s: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), label != NULL ? label : "(null)", DP_RC(rc));

out_lock:
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	daos_prop_free(prop);
	return rc;
}

int
ds_pool_svc_evict_all(struct ds_pool_svc *ds_svc)
{
	struct pool_svc		*svc = pool_ds2svc(ds_svc);
	struct pool_metrics	*metrics;
	uuid_t			*hdl_uuids = NULL;
	struct rdb_tx		 tx = { 0 };
	size_t			 hdl_uuids_size = 0;
	int			 n_hdl_uuids = 0;
	int			 rc = 0;

	rc = rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx);
	if (rc != 0) {
		D_ERROR("Failed to begin TX for evict pool "DF_UUIDF" connections: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	ABT_rwlock_wrlock(svc->ps_lock);

	rc = find_hdls_to_evict(&tx, svc, &hdl_uuids, &hdl_uuids_size, &n_hdl_uuids, NULL);
	if (rc != 0) {
		D_ERROR("Failed to find hdls for evict pool "DF_UUIDF" connections: "DF_RC"\n",
			DP_UUID(svc->ps_uuid), DP_RC(rc));
		D_GOTO(out_lock, rc);
	}

	if (n_hdl_uuids > 0) {
		rc = pool_disconnect_hdls(&tx, svc, hdl_uuids, n_hdl_uuids,
					  dss_get_module_info()->dmi_ctx);
		if (rc != 0)
			D_GOTO(out_lock, rc);

		metrics = svc->ps_pool->sp_metrics[DAOS_POOL_MODULE];
		d_tm_inc_counter(metrics->evict_total, n_hdl_uuids);
		rc = rdb_tx_commit(&tx);
		if (rc != 0)
			D_ERROR("Failed to commit TX for evict pool "DF_UUIDF" connections: "
				DF_RC"\n", DP_UUID(svc->ps_uuid), DP_RC(rc));
	}

out_lock:
	D_FREE(hdl_uuids);
	ABT_rwlock_unlock(svc->ps_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

struct ds_pool *
ds_pool_svc2pool(struct ds_pool_svc *ds_svc)
{
	return pool_ds2svc(ds_svc)->ps_pool;
}

struct cont_svc *
ds_pool_ps2cs(struct ds_pool_svc *ds_svc)
{
	return pool_ds2svc(ds_svc)->ps_cont_svc;
}

/* Upgrade the VOS pool of a pool service replica (if any). */
int
ds_pool_svc_upgrade_vos_pool(struct ds_pool *pool)
{
	d_iov_t         id;
	struct ds_rsvc *rsvc;
	uint32_t        df_version;
	int             rc;

	df_version = ds_pool_get_vos_df_version(pool->sp_global_version);
	if (df_version == 0) {
		rc = -DER_NO_PERM;
		DL_ERROR(rc, DF_UUID ": pool global version %u no longer supported",
			 DP_UUID(pool->sp_uuid), pool->sp_global_version);
		return rc;
	}

	d_iov_set(&id, pool->sp_uuid, sizeof(uuid_t));
	rc = ds_rsvc_lookup(DS_RSVC_CLASS_POOL, &id, &rsvc);
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID ": no applicable pool service replica: " DF_RC "\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
		return 0;
	}

	rc = rdb_upgrade_vos_pool(rsvc->s_db, df_version);
	if (rc == 0)
		D_DEBUG(DB_MD, DF_UUID ": upgraded to or already at %u\n", DP_UUID(pool->sp_uuid),
			df_version);
	else
		DL_ERROR(rc, DF_UUID ": failed to upgrade pool service to global version %u",
			 DP_UUID(pool->sp_uuid), pool->sp_global_version);

	ds_rsvc_put(rsvc);
	return rc;
}
