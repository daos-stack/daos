/**
 * (C) Copyright 2019-2021 Intel Corporation.
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
#define DAOS_DTX_VERSION	2

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
	((struct dtx_id)	(di_dtx_array)		CRT_ARRAY)

/* DTX RPC output fields */
#define DAOS_OSEQ_DTX							\
	((int32_t)		(do_status)		CRT_VAR)	\
	((int32_t)		(do_pad)		CRT_VAR)	\
	((int32_t)		(do_sub_rets)		CRT_ARRAY)

CRT_RPC_DECLARE(dtx, DAOS_ISEQ_DTX, DAOS_OSEQ_DTX);

/* The age unit is second. */

/* If the DTX entries are not more than this count threshold,
 * then no need DTX aggregation.
 *
 * XXX: This threshold should consider the real SCM size. But
 *	it cannot be too small; otherwise, handing resent RPC
 *	make hit uncertain case and got failure -DER_EP_OLD.
 */
#define DTX_AGG_THD_CNT_LO	((1 << 19) * 6)

/* The count threshold for triggerring DTX aggregation. */
#define DTX_AGG_THD_CNT_UP	((1 << 19) * 7)

/* The time threshold for triggerring DTX aggregation. If the oldest
 * DTX in the DTX table exceeds such threshold, it will trigger DTX
 * aggregation locally.
 */
#define DTX_AGG_THD_AGE_UP	210

/* If DTX aggregation is triggered, then the DTXs with older ages than
 * this threshold will be aggregated.
 *
 * XXX: It cannot be too small; otherwise, handing resent RPC
 *	make hit uncertain case and got failure -DER_EP_OLD.
 */
#define DTX_AGG_THD_AGE_LO	180

/* The time threshold for triggerring DTX cleanup of stale entries.
 * If the oldest active DTX exceeds such threshold, it will trigger
 * DTX cleanup locally.
 */
#define DTX_CLEANUP_THD_AGE_UP	60

/* If DTX cleanup for stale entries is triggered, then the DTXs with
 * older ages than this threshold will be cleanup.
 */
#define DTX_CLEANUP_THD_AGE_LO	45

struct dtx_pool_metrics {
	struct d_tm_node_t	*dpm_total[DTX_PROTO_SRV_RPC_COUNT];
};

/*
 * DTX TLS
 */
struct dtx_tls {
	struct d_tm_node_t	*dt_committable;
};

extern struct dss_module_key dtx_module_key;

static inline struct dtx_tls *
dtx_tls_get(void)
{
	return dss_module_key_get(dss_tls_get(), &dtx_module_key);
}

extern struct crt_proto_format dtx_proto_fmt;
extern btr_ops_t dbtree_dtx_cf_ops;
extern btr_ops_t dtx_btr_cos_ops;
extern uint64_t dtx_agg_gen;

/* dtx_common.c */
int dtx_handle_reinit(struct dtx_handle *dth);
void dtx_batched_commit(void *arg);

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

int dtx_refresh_internal(struct ds_cont_child *cont, int *check_count,
			 d_list_t *check_list, d_list_t *cmt_list,
			 d_list_t *abt_list, d_list_t *act_list, bool failout);
int dtx_status_handle_one(struct ds_cont_child *cont, struct dtx_entry *dte,
			  daos_epoch_t epoch, int *tgt_array, int *err);

enum dtx_status_handle_result {
	DSHR_NEED_COMMIT	= 1,
	DSHR_NEED_RETRY		= 2,
	DSHR_COMMITTED		= 3,
	DSHR_ABORTED		= 4,
	DSHR_ABORT_FAILED	= 5,
	DSHR_CORRUPT		= 6,
};

#endif /* __DTX_INTERNAL_H__ */
