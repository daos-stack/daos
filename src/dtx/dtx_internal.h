/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: dtx internal.h
 *
 */
#ifndef __DTX_INTERNAL_H__
#define __DTX_INTERNAL_H__

#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/btree.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_DTX_VERSION	3

/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define DTX_PROTO_SRV_RPC_LIST						\
	X(DTX_COMMIT, 0, &CQF_dtx, dtx_handler, NULL, "dtx_commit")	\
	X(DTX_ABORT, 0, &CQF_dtx, dtx_handler, NULL, "dtx_abort")	\
	X(DTX_CHECK, 0, &CQF_dtx, dtx_handler, NULL, "dtx_check")	\
	X(DTX_REFRESH, 0, &CQF_dtx, dtx_handler, NULL, "dtx_refresh")

#define X(a, b, c, d, e, f) a,
enum dtx_operation {
	DTX_PROTO_SRV_RPC_LIST
	DTX_PROTO_SRV_RPC_COUNT,
};
#undef X

/* DTX RPC input fields */
#define DAOS_ISEQ_DTX							\
	((uuid_t)		(di_po_uuid)		CRT_VAR)	\
	((uuid_t)		(di_co_uuid)		CRT_VAR)	\
	((uint64_t)		(di_epoch)		CRT_VAR)	\
	((struct dtx_id)	(di_dtx_array)		CRT_ARRAY)	\
	((uint32_t)		(di_flags)		CRT_ARRAY)

/* DTX RPC output fields */
#define DAOS_OSEQ_DTX							\
	((int32_t)		(do_status)		CRT_VAR)	\
	((int32_t)		(do_misc)		CRT_VAR)	\
	((int32_t)		(do_sub_rets)		CRT_ARRAY)

CRT_RPC_DECLARE(dtx, DAOS_ISEQ_DTX, DAOS_OSEQ_DTX);

#define DTX_YIELD_CYCLE		(DTX_THRESHOLD_COUNT >> 3)

/* The time threshold for triggering DTX cleanup of stale entries.
 * If the oldest active DTX exceeds such threshold, it will trigger
 * DTX cleanup locally.
 */
#define DTX_CLEANUP_THD_AGE_UP	90

/* If DTX cleanup for stale entries is triggered, then the DTXs with
 * older ages than this threshold will be cleanup.
 */
#define DTX_CLEANUP_THD_AGE_LO	75

/* The count threshold (per pool) for triggering DTX aggregation. */
#define DTX_AGG_THD_CNT_MAX	(1 << 24)
#define DTX_AGG_THD_CNT_MIN	(1 << 20)
#define DTX_AGG_THD_CNT_DEF	((1 << 19) * 7)

/* If the total committed DTX entries count for the pool exceeds
 * such threshold, it will trigger DTX aggregation locally.
 *
 * XXX: It is controlled via the environment "DTX_AGG_THD_CNT".
 *	This threshold should consider the real SCM size. But
 *	it cannot be too small; otherwise, handing resent RPC
 *	make hit uncertain case and got failure -DER_EP_OLD.
 */
extern uint32_t dtx_agg_thd_cnt_up;

/* If DTX aggregation is triggered, then current DTX aggregation
 * will not stop until the committed DTX entries count for the
 * pool down to such threshold.
 */
extern uint32_t dtx_agg_thd_cnt_lo;

/* The age unit is second. */
#define DTX_AGG_THD_AGE_MAX	1830
#define DTX_AGG_THD_AGE_MIN	210
#define DTX_AGG_THD_AGE_DEF	630

/* There is race between DTX aggregation and DTX refresh. Consider the following scenario:
 *
 * The DTX leader triggers DTX commit for some DTX entry, then related DTX participants
 * (including the leader itself) will commit the DTX entry on each own target in parallel.
 * It is possible that the leader has already committed locally but DTX aggregation removed
 * the committed DTX very shortly after the commit. On the other hand, on some non-leader
 * before the local commit, someone triggers DTX refresh for such DTX on such non-leader.
 * Unfortunately the DTX entry has already gone on the leader. Then the non-leader will
 * get -DER_TX_UNCERTAIN, that will cause related application to fail unexpectedly.
 *
 * So even if the system has DRAM pressure, we still need to keep some very recent committed
 * DTX entries to handle above race.
 */
#define DTX_AGG_AGE_PRESERVE	3

/* The threshold for yield CPU when handle DTX RPC. */
#define DTX_RPC_YIELD_THD	32

/* The time threshold for triggering DTX aggregation. If the oldest
 * DTX in the DTX table exceeds such threshold, it will trigger DTX
 * aggregation locally.
 *
 * XXX: It is controlled via the environment "DTX_AGG_THD_AGE".
 *	It cannot be too small; otherwise, handing resent RPC
 *	make hit uncertain case and got failure -DER_EP_OLD.
 */
extern uint32_t dtx_agg_thd_age_up;

/* If DTX aggregation is triggered, then the DTXs with older ages than
 * this threshold will be aggregated.
 */
extern uint32_t dtx_agg_thd_age_lo;

/* The default count of DTX batched commit ULTs. */
#define DTX_BATCHED_ULT_DEF	32

/*
 * Ideally, dedicated DXT batched commit ULT for each opened container is the most simple model.
 * But it may be burden for the engine if opened containers become more and more on the target.
 * So it is necessary to restrict the count of DTX batched commit ULTs on the target. It can be
 * adjusted via the environment "DAOS_DTX_BATCHED_ULT_MAX" when load the module.
 *
 * Zero:		disable DTX batched commit, all DTX will be committed synchronously.
 * Others:		the max count of DXT batched commit ULTs.
 */
extern uint32_t dtx_batched_ult_max;

/*
 * If the size of dtx_memberships exceeds DTX_INLINE_MBS_SIZE, then load it (DTX mbs)
 * dynamically when use it to avoid holding a lot of DRAM resource for long time that
 * may happen on some very large system.
 */
#define DTX_INLINE_MBS_SIZE		512

struct dtx_pool_metrics {
	struct d_tm_node_t	*dpm_batched_degree;
	struct d_tm_node_t	*dpm_batched_total;
	struct d_tm_node_t	*dpm_total[DTX_PROTO_SRV_RPC_COUNT];
};

/*
 * DTX TLS
 */
struct dtx_tls {
	struct d_tm_node_t	*dt_committable;
	uint64_t		 dt_agg_gen;
	uint32_t		 dt_batched_ult_cnt;
};

extern struct dss_module_key dtx_module_key;

static inline struct dtx_tls *
dtx_tls_get(void)
{
	return dss_module_key_get(dss_tls_get(), &dtx_module_key);
}

static inline bool
dtx_cont_opened(struct ds_cont_child *cont)
{
	return cont->sc_open > 0;
}

static inline uint32_t
dtx_cont2ver(struct ds_cont_child *cont)
{
	return cont->sc_pool->spc_pool->sp_map_version;
}

extern struct crt_proto_format dtx_proto_fmt;
extern btr_ops_t dbtree_dtx_cf_ops;
extern btr_ops_t dtx_btr_cos_ops;

/* dtx_common.c */
int dtx_handle_reinit(struct dtx_handle *dth);
void dtx_batched_commit(void *arg);
void dtx_aggregation_main(void *arg);
int start_dtx_reindex_ult(struct ds_cont_child *cont);
void stop_dtx_reindex_ult(struct ds_cont_child *cont);

/* dtx_cos.c */
int dtx_fetch_committable(struct ds_cont_child *cont, uint32_t max_cnt,
			  daos_unit_oid_t *oid, daos_epoch_t epoch,
			  struct dtx_entry ***dtes, struct dtx_cos_key **dcks);
int dtx_add_cos(struct ds_cont_child *cont, struct dtx_entry *dte,
		daos_unit_oid_t *oid, uint64_t dkey_hash,
		daos_epoch_t epoch, uint32_t flags);
int dtx_del_cos(struct ds_cont_child *cont, struct dtx_id *xid,
		daos_unit_oid_t *oid, uint64_t dkey_hash);
uint64_t dtx_cos_oldest(struct ds_cont_child *cont);

/* dtx_rpc.c */
int dtx_commit(struct ds_cont_child *cont, struct dtx_entry **dtes,
	       struct dtx_cos_key *dcks, int count);
int dtx_check(struct ds_cont_child *cont, struct dtx_entry *dte,
	      daos_epoch_t epoch);

int dtx_refresh_internal(struct ds_cont_child *cont, int *check_count, d_list_t *check_list,
			 d_list_t *cmt_list, d_list_t *abt_list, d_list_t *act_list, bool for_io);
int dtx_status_handle_one(struct ds_cont_child *cont, struct dtx_entry *dte,
			  daos_epoch_t epoch, int *tgt_array, int *err);

int dtx_leader_get(struct ds_pool *pool, struct dtx_memberships *mbs,
		   struct pool_target **p_tgt);

enum dtx_status_handle_result {
	DSHR_NEED_COMMIT	= 1,
	DSHR_NEED_RETRY		= 2,
	DSHR_IGNORE		= 3,
	DSHR_ABORT_FAILED	= 4,
	DSHR_CORRUPT		= 5,
};

enum dtx_rpc_flags {
	DRF_INITIAL_LEADER	= (1 << 0),
};

#endif /* __DTX_INTERNAL_H__ */
