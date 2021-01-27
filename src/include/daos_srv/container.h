/*
 * (C) Copyright 2015-2020 Intel Corporation.
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
 * ds_cont: Container Server API
 */

#ifndef __DAOS_SRV_CONTAINER_H__
#define __DAOS_SRV_CONTAINER_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/evtree.h>
#include <daos/container.h>
#include <daos/cont_props.h>

void ds_cont_wrlock_metadata(struct cont_svc *svc);
void ds_cont_rdlock_metadata(struct cont_svc *svc);
void ds_cont_unlock_metadata(struct cont_svc *svc);
int ds_cont_init_metadata(struct rdb_tx *tx, const rdb_path_t *kvs,
			  const uuid_t pool_uuid);
int ds_cont_svc_init(struct cont_svc **svcp, const uuid_t pool_uuid,
		     uint64_t id, struct ds_rsvc *rsvc);
void ds_cont_svc_fini(struct cont_svc **svcp);
void ds_cont_svc_step_up(struct cont_svc *svc);
void ds_cont_svc_step_down(struct cont_svc *svc);

int ds_cont_svc_set_prop(uuid_t pool_uuid, uuid_t cont_uuid,
			      d_rank_list_t *ranks, daos_prop_t *prop);

int ds_cont_list(uuid_t pool_uuid, struct daos_pool_cont_info **conts,
		 uint64_t *ncont);

int ds_cont_tgt_close(uuid_t hdl_uuid);
int ds_cont_tgt_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		     uuid_t cont_uuid, uint64_t flags, uint64_t sec_capas);
/*
 * Per-thread container (memory) object
 *
 * Stores per-thread, per-container information, such as the vos container
 * handle.
 */
struct ds_cont_child {
	struct daos_llink	 sc_list;
	daos_handle_t		 sc_hdl;	/* vos_container handle */
	uuid_t			 sc_uuid;	/* container UUID */
	struct ds_pool_child	*sc_pool;
	d_list_t		 sc_link;	/* link to spc_cont_list */
	struct daos_csummer	*sc_csummer;
	struct cont_props	 sc_props;

	ABT_mutex		 sc_mutex;
	ABT_cond		 sc_dtx_resync_cond;
	uint32_t		 sc_dtx_resyncing:1,
				 sc_dtx_aggregating:1,
				 sc_dtx_reindex:1,
				 sc_dtx_reindex_abort:1,
				 sc_closing:1,
				 sc_vos_aggregating:1,
				 sc_abort_vos_aggregating:1,
				 sc_props_fetched:1,
				 sc_stopping:1,
				 sc_cos_shutdown:1;
	/* Tracks the schedule request for aggregation ULT */
	struct sched_request	*sc_agg_req;

	/*
	 * Snapshot delete HLC (0 means no change), which is used
	 * to compare with the aggregation HLC, so it knows whether the
	 * aggregation needs to be restart from 0.
	 */
	uint64_t		sc_snapshot_delete_hlc;

	/* HLC when the full scan aggregation start, if it is smaller than
	 * snapshot_delete_hlc(or rebuild), then aggregation needs to restart
	 * from 0.
	 */
	uint64_t		sc_aggregation_full_scan_hlc;

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

	uint64_t		 sc_dtx_committable_count;

	/* The global minimum EC aggregation epoch, which will be upper
	 * limit for VOS aggregation, i.e. EC object VOS aggregation can
	 * not cross this limit. For simplification purpose, all objects
	 * VOS aggregation will use this boundary. We will optimize it later.
	 */
	uint64_t		sc_ec_agg_eph_boundry;
	/* The current EC aggregate epoch for this xstream */
	uint64_t		sc_ec_agg_eph;
	/* The objects with committable DTXs in DRAM. */
	daos_handle_t		 sc_dtx_cos_hdl;
	/* The DTX COS-btree. */
	struct btr_root		 sc_dtx_cos_btr;
	/* The global list for committable DTXs. */
	d_list_t		 sc_dtx_cos_list;
	/* The pool map version for the latest DTX resync on the container. */
	uint32_t		 sc_dtx_resync_ver;
};

/*
 * Per-thread container handle (memory) object
 *
 * Stores per-thread, per-handle information, such as the container
 * capabilities. References the ds_cont and the ds_pool_child objects.
 */
struct ds_cont_hdl {
	d_list_t		sch_entry;
	uuid_t			sch_uuid;	/* of the container handle */
	uint64_t		sch_flags;	/* user-supplied flags */
	uint64_t		sch_sec_capas;	/* access control capas */
	struct ds_cont_child	*sch_cont;
	int			sch_ref;
};

struct ds_cont_hdl *ds_cont_hdl_lookup(const uuid_t uuid);
void ds_cont_hdl_put(struct ds_cont_hdl *hdl);
void ds_cont_hdl_get(struct ds_cont_hdl *hdl);

int ds_cont_close_by_pool_hdls(uuid_t pool_uuid, uuid_t *pool_hdls,
			       int n_pool_hdls, crt_context_t ctx);
int ds_cont_local_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		       uuid_t cont_uuid, uint64_t flags,
		       uint64_t sec_capas, struct ds_cont_hdl **cont_hdl);
int ds_cont_local_close(uuid_t cont_hdl_uuid);

int ds_cont_child_start_all(struct ds_pool_child *pool_child);
void ds_cont_child_stop_all(struct ds_pool_child *pool_child);

int ds_cont_child_lookup(uuid_t pool_uuid, uuid_t cont_uuid,
			 struct ds_cont_child **ds_cont);

/** initialize a csummer based on container properties. Will retrieve the
 * checksum related properties from IV
 */
int ds_cont_csummer_init(struct ds_cont_child *cont);
int ds_get_cont_props(struct cont_props *cont_props, struct ds_iv_ns *pool_ns,
		      uuid_t cont_uuid);

void ds_cont_child_put(struct ds_cont_child *cont);
void ds_cont_child_get(struct ds_cont_child *cont);

int ds_cont_child_open_create(uuid_t pool_uuid, uuid_t cont_uuid,
			      struct ds_cont_child **cont);

typedef int (*cont_iter_cb_t)(uuid_t co_uuid, vos_iter_entry_t *ent, void *arg);
int ds_cont_iter(daos_handle_t ph, uuid_t co_uuid, cont_iter_cb_t callback,
		 void *arg, uint32_t type);

/**
 * Query container properties.
 *
 * \param[in]	ns	pool IV namespace
 * \param[in]	co_uuid
 *			container uuid
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
int ds_cont_fetch_prop(struct ds_iv_ns *ns, uuid_t co_uuid,
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

struct csum_recalc {
	struct evt_extent	 cr_log_ext;
	struct evt_extent	*cr_phy_ext;
	struct agg_phy_ent	*cr_phy_ent; /* Incomplete ex vos_aggregate.c */
	struct dcs_csum_info	*cr_phy_csum;
	daos_off_t		 cr_phy_off;
	unsigned int		 cr_prefix_len;
	unsigned int		 cr_suffix_len;
};

struct csum_recalc_args {
	struct bio_sglist	*cra_bsgl;	/* read sgl */
	d_sg_list_t		*cra_sgl;	/* write sgl */
	struct evt_entry_in	*cra_ent_in;    /* coalesced entry */
	struct csum_recalc	*cra_recalcs;   /* recalc info */
	void			*cra_buf;	/* read buffer */
	struct bio_xs_context	*cra_bio_ctxt;	/* used to log error */
	daos_size_t		 cra_seg_size;  /* size of coalesced entry */
	unsigned int		 cra_seg_cnt;   /* # of read segments */
	unsigned int		 cra_buf_len;	/* length of read buffer */
	int			 cra_tgt_id;	/* VOS target ID */
	int			 cra_rc;	/* return code */
	ABT_eventual		 csum_eventual;
};

/* Callback function to pass to vos_aggregation */
void
ds_csum_recalc(void *args);

/* Used for VOS unit tests */
void
ds_csum_agg_recalc(void *args);

int dsc_cont_open(daos_handle_t poh, uuid_t cont_uuid, uuid_t cont_hdl_uuid,
		  unsigned int flags, daos_handle_t *coh);
int dsc_cont_close(daos_handle_t poh, daos_handle_t coh);


void ds_cont_tgt_ec_eph_query_ult(void *data);
#endif /* ___DAOS_SRV_CONTAINER_H_ */
