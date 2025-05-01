/*
 * (C) Copyright 2015-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_cont: Container Server API
 */

#ifndef __DAOS_SRV_CONTAINER_H__
#define __DAOS_SRV_CONTAINER_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/pool.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/evtree.h>
#include <daos/container.h>
#include <daos/cont_props.h>

void ds_cont_wrlock_metadata(struct cont_svc *svc);
void ds_cont_rdlock_metadata(struct cont_svc *svc);
void ds_cont_unlock_metadata(struct cont_svc *svc);
int
     ds_cont_init_metadata(struct rdb_tx *tx, const rdb_path_t *kvs, const uuid_t pool_uuid);
int ds_cont_svc_init(struct cont_svc **svcp, const uuid_t pool_uuid,
		     uint64_t id, struct ds_rsvc *rsvc);
void ds_cont_svc_fini(struct cont_svc **svcp);
int ds_cont_svc_step_up(struct cont_svc *svc);
void ds_cont_svc_step_down(struct cont_svc *svc);
int
    ds_cont_svc_set_prop(uuid_t pool_uuid, const char *cont_id, d_rank_list_t *ranks,
			 daos_prop_t *prop);
int ds_cont_list(uuid_t pool_uuid, struct daos_pool_cont_info **conts, uint64_t *ncont);
int ds_cont_filter(uuid_t pool_uuid, daos_pool_cont_filter_t *filt,
		   struct daos_pool_cont_info2 **conts, uint64_t *ncont);
int ds_cont_upgrade(uuid_t pool_uuid, struct cont_svc *svc);
int ds_cont_tgt_close(uuid_t pool_uuid, uuid_t hdl_uuid);
int ds_cont_tgt_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		     uuid_t cont_uuid, uint64_t flags, uint64_t sec_capas,
		     uint32_t status_pm_ver);
/*
 * Per-thread container (memory) object
 *
 * Stores per-thread, per-container information, such as the vos container
 * handle. N.B. sc_uuid and sc_pool_uuid must be contiguous in memory,
 * used as a 256 bit key in tls dt_cont_cache.
 */
struct ds_cont_child {
	struct daos_llink	 sc_list;
	daos_handle_t		 sc_hdl;	/* vos_container handle */
	uuid_t			 sc_uuid;	/* container UUID */
	uuid_t			 sc_pool_uuid;	/* pool UUID */
	struct ds_pool_child	*sc_pool;
	d_list_t		 sc_link;	/* link to spc_cont_list */
	d_list_t		 sc_open_hdls;	/* the list of ds_cont_hdl. */
	struct daos_csummer	*sc_csummer;
	struct cont_props	 sc_props;

	ABT_mutex		 sc_mutex;
	ABT_cond		 sc_dtx_resync_cond;
	ABT_cond		 sc_scrub_cond;
	ABT_cond		 sc_rebuild_cond;
	ABT_cond		 sc_fini_cond;
	uint32_t                 sc_dtx_resyncing : 1, sc_dtx_reindex : 1, sc_dtx_reindex_abort : 1,
	    sc_dtx_delay_reset : 1, sc_dtx_registered : 1, sc_props_fetched : 1, sc_stopping : 1,
	    sc_destroying : 1, sc_vos_agg_active : 1, sc_ec_agg_active : 1,
	    /* flag of CONT_CAPA_READ_DATA/_WRITE_DATA disabled */
	    sc_rw_disabled : 1, sc_scrubbing : 1, sc_rebuilding : 1, sc_open_initializing : 1;
	/* Tracks the schedule request for aggregation ULT */
	struct sched_request	*sc_agg_req;

	/* Tracks the schedule request for EC aggregation ULT */
	struct sched_request	*sc_ec_agg_req;
	/*
	 * Snapshot delete HLC (0 means no change), which is used
	 * to compare with the aggregation HLC, so it knows whether the
	 * aggregation needs to be restart from 0.
	 */
	uint64_t		sc_snapshot_delete_hlc;

	/* Upper bound of aggregation epoch, it can be:
	 *
	 * 0			: When snapshot list isn't retrieved yet
	 * DAOS_EPOCH_MAX	: When snapshot list is retrieved
	 * snapshot epoch	: When the snapshot creation is in-progress
	 */
	uint64_t		 sc_aggregation_max;

	uint64_t		*sc_snapshots;
	uint32_t		 sc_snapshots_nr;
	uint32_t		 sc_open;

	uint32_t		 sc_dtx_committable_count;
	uint32_t		 sc_dtx_committable_coll_count;

	/* Last timestamp when EC aggregation reports -DER_INPROGRESS. */
	uint64_t		 sc_ec_agg_busy_ts;


	/* The global minimum stable epoch. All data @lower epoch should has been globally
	 * stable (committed or aborted). Used as the start epoch for incremental reintegration.
	 */
	uint64_t		sc_global_stable_eph;

	/* The global minimum EC aggregation epoch, which will be upper
	 * limit for VOS aggregation, i.e. EC object VOS aggregation can
	 * not cross this limit. For simplification purpose, all objects
	 * VOS aggregation will use this boundary. We will optimize it later.
	 */
	uint64_t		sc_ec_agg_eph_boundary;
	/* The current EC aggregate epoch for this xstream */
	uint64_t		sc_ec_agg_eph;
	/* Used by ds_cont_track_eph_query_ult to query the minimum ec_agg_eph and stable_eph
	 * from all local VOS.
	 */
	uint64_t		*sc_query_ec_agg_eph;
	uint64_t		*sc_query_stable_eph;
	/**
	 * Timestamp of last EC update, which is used by aggregation to check
	 * if it needs to do EC aggregate.
	 */
	uint64_t		sc_ec_update_timestamp;

	/* The objects with committable DTXs in DRAM. */
	daos_handle_t		 sc_dtx_cos_hdl;
	/* The DTX COS-btree. */
	struct btr_root		 sc_dtx_cos_btr;
	/* The global list for committable non-collective DTXs. */
	d_list_t		 sc_dtx_cos_list;
	/* The global list for committable collective DTXs. */
	d_list_t		 sc_dtx_coll_list;
	/* The list for current DTX batched commit. */
	d_list_t		 sc_dtx_batched_list;
	/* the pool map version of updating DAOS_PROP_CO_STATUS prop */
	uint32_t		 sc_status_pm_ver;
};

struct agg_param {
	void			*ap_data;
	struct ds_cont_child	*ap_cont;
	daos_epoch_t		ap_full_scan_hlc;
	bool			ap_vos_agg;
};

typedef int (*cont_aggregate_cb_t)(struct ds_cont_child *cont,
				   daos_epoch_range_t *epr, uint32_t flags,
				   struct agg_param *param);
void
cont_aggregate_interval(struct ds_cont_child *cont, cont_aggregate_cb_t cb,
			struct agg_param *param);

/*
 * Yield function regularly called by EC and VOS aggregation ULTs.
 *
 * \param[in] arg	Aggregation parameter
 *
 * \retval		-1:	Inform aggregation to abort current round;
 *			 0:	Inform aggregation to run in tight mode; (less yield)
 *			 1:	Inform aggregation to run in slack mode; (yield more often)
 */
int agg_rate_ctl(void *arg);

/*
 * Per-thread container handle (memory) object
 *
 * Stores per-thread, per-handle information, such as the container
 * capabilities. References the ds_cont and the ds_pool_child objects.
 */
struct ds_cont_hdl {
	d_list_t		sch_entry;
	/* link to ds_cont_child::sc_open_hdls if sch_cont is not NULL. */
	d_list_t		sch_link;
	uuid_t			sch_uuid;	/* of the container handle */
	uint64_t		sch_flags;	/* user-supplied flags */
	uint64_t		sch_sec_capas;	/* access control capas */
	struct ds_cont_child	*sch_cont;
	int32_t                  sch_ref;
};

struct ds_cont_hdl *ds_cont_hdl_lookup(const uuid_t uuid);
void ds_cont_hdl_put(struct ds_cont_hdl *hdl);
void ds_cont_hdl_get(struct ds_cont_hdl *hdl);

int ds_cont_close_by_pool_hdls(uuid_t pool_uuid, uuid_t *pool_hdls,
			       int n_pool_hdls, crt_context_t ctx);
int ds_cont_local_close(uuid_t cont_hdl_uuid);

int ds_cont_child_start_all(struct ds_pool_child *pool_child);
void ds_cont_child_stop_all(struct ds_pool_child *pool_child);

int ds_cont_child_lookup(uuid_t pool_uuid, uuid_t cont_uuid,
			 struct ds_cont_child **ds_cont);
int
ds_cont_child_destroy(uuid_t pool_uuid, uuid_t cont_uuid);

void
ds_cont_child_reset_ec_agg_eph_all(struct ds_pool_child *pool_child);
/** initialize a csummer based on container properties. Will retrieve the
 * checksum related properties from IV
 */
int ds_cont_csummer_init(struct ds_cont_child *cont);
int ds_cont_get_props(struct cont_props *cont_props, uuid_t pool_uuid,
		      uuid_t cont_uuid);

void ds_cont_child_put(struct ds_cont_child *cont);
void ds_cont_child_get(struct ds_cont_child *cont);

int ds_cont_child_open_create(uuid_t pool_uuid, uuid_t cont_uuid,
			      struct ds_cont_child **cont);

typedef int (*cont_iter_cb_t)(uuid_t co_uuid, vos_iter_entry_t *ent, void *arg);
int ds_cont_iter(daos_handle_t ph, uuid_t co_uuid, cont_iter_cb_t callback,
		 void *arg, uint32_t type, uint32_t flags);

/**
 * Query container properties.
 *
 * \param[in]	po_uuid	pool uuid
 * \param[in]	co_uuid	container uuid
 * \param[out]	cont_prop
 *			returned container properties
 *			If it is NULL, return -DER_INVAL;
 *			If cont_prop is non-NULL but its dpp_entries is NULL,
 *			will query all pool properties, DAOS internally
 *			allocates the needed buffers and assign pointer to
 *			dpp_entries.
 *			If cont_prop's dpp_nr > 0 and dpp_entries is non-NULL,
 *			will query the properties for specific dpe_type(s), DAOS
 *			internally allocates the needed buffer for dpe_str or
 *			dpe_val_ptr, if the dpe_type with immediate value then
 *			will directly assign it to dpe_val.
 *			User can free the associated buffer by calling
 *			daos_prop_free().
 *
 * \return		0 if Success, negative if failed.
 */
int ds_cont_fetch_prop(uuid_t po_uuid, uuid_t co_uuid,
		       daos_prop_t *cont_prop);

/** get all snapshots of the container from IV */
int ds_cont_fetch_snaps(struct ds_iv_ns *ns, uuid_t cont_uuid,
			uint64_t **snapshots, int *snap_count);

/** revoke all cached snapshot epochs */
int ds_cont_revoke_snaps(struct ds_iv_ns *ns, uuid_t cont_uuid,
			 unsigned int shortcut, unsigned int sync_mode);

/** find the container open handle from its uuid */
int ds_cont_find_hdl(uuid_t po_uuid, uuid_t coh_uuid,
		     struct ds_cont_hdl **coh_p);

int dsc_cont_open(daos_handle_t poh, uuid_t cont_uuid, uuid_t cont_hdl_uuid,
		  unsigned int flags, daos_handle_t *coh);
int dsc_cont_close(daos_handle_t poh, daos_handle_t coh);
struct daos_csummer *dsc_cont2csummer(daos_handle_t coh);
int dsc_cont_get_props(daos_handle_t coh, struct cont_props *props);

void ds_cont_track_eph_query_ult(void *data);
void ds_cont_track_eph_free(struct ds_pool *pool);

void ds_cont_ec_timestamp_update(struct ds_cont_child *cont);

typedef int(*cont_rdb_iter_cb_t)(uuid_t pool_uuid, uuid_t cont_uuid, struct rdb_tx *tx, void *arg);
int ds_cont_rdb_iterate(struct cont_svc *svc, cont_rdb_iter_cb_t iter_cb, void *cb_arg);
int ds_cont_rf_check(uuid_t pool_uuid, uuid_t cont_uuid, struct rdb_tx *tx);

int ds_cont_existence_check(struct cont_svc *svc, uuid_t uuid, daos_prop_t **prop);

int ds_cont_destroy_orphan(struct cont_svc *svc, uuid_t uuid);

int ds_cont_iterate_labels(struct cont_svc *svc, rdb_iterate_cb_t cb, void *arg);

int ds_cont_set_label(struct cont_svc *svc, uuid_t uuid, daos_prop_t *prop_in,
		      daos_prop_t *prop_old, bool for_svc);

int ds_cont_fetch_ec_agg_boundary(void *ns, uuid_t cont_uuid);

#endif /* ___DAOS_SRV_CONTAINER_H_ */
