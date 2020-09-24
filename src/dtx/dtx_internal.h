/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * dtx: dtx internal.h
 *
 */
#ifndef __DTX_INTERNAL_H__
#define __DTX_INTERNAL_H__

#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/btree.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_DTX_VERSION	1

/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define DTX_PROTO_SRV_RPC_LIST(X)				\
	X(DTX_COMMIT, 0, &CQF_dtx, dtx_handler, NULL),		\
	X(DTX_ABORT, 0, &CQF_dtx, dtx_handler, NULL),		\
	X(DTX_CHECK, 0, &CQF_dtx, dtx_handler, NULL)

#define X_OPC(a, b, c, d, e) a

enum dtx_operation {
	DTX_PROTO_SRV_RPC_LIST(X_OPC)
};

/* DTX RPC input fields */
#define DAOS_ISEQ_DTX							\
	((uuid_t)		(di_po_uuid)		CRT_VAR)	\
	((uuid_t)		(di_co_uuid)		CRT_VAR)	\
	((uint64_t)		(di_epoch)		CRT_VAR)	\
	((struct dtx_id)	(di_dtx_array)		CRT_ARRAY)

/* DTX RPC output fields */
#define DAOS_OSEQ_DTX							\
	((int32_t)		(do_status)		CRT_VAR)

CRT_RPC_DECLARE(dtx, DAOS_ISEQ_DTX, DAOS_OSEQ_DTX);

/* The age unit is second. */

/* The count threshould for triggerring DTX aggregation.
 * This threshould should consider the real SCM size.
 */
#define DTX_AGG_THRESHOLD_CNT_UPPER	(1 << 27)

/* If the DTX entries are not more than this count threshould,
 * then no need DTX aggregation.
 */
#define DTX_AGG_THRESHOLD_CNT_LOWER	(1 << 17)

/* The time threshould for triggerring DTX aggregation. If the oldest
 * DTX in the DTX table exceeds such threshould, it will trigger DTX
 * aggregation locally.
 */
#define DTX_AGG_THRESHOLD_AGE_UPPER	4800

/* If DTX aggregation is triggered, then he DTXs with older ages than
 * this threshold will be aggregated.
 */
#define DTX_AGG_THRESHOLD_AGE_LOWER	3600

extern struct crt_proto_format dtx_proto_fmt;
extern btr_ops_t dbtree_dtx_cf_ops;
extern btr_ops_t dtx_btr_cos_ops;

/* dtx_common.c */
void dtx_aggregate(void *arg);
void dtx_batched_commit(void *arg);

/* dtx_cos.c */
int dtx_fetch_committable(struct ds_cont_child *cont, uint32_t max_cnt,
			  daos_unit_oid_t *oid, daos_epoch_t epoch,
			  struct dtx_entry ***dtes);
int dtx_lookup_cos(struct ds_cont_child *cont, struct dtx_id *xid,
		   daos_unit_oid_t *oid, uint64_t dkey_hash);
int dtx_add_cos(struct ds_cont_child *cont, struct dtx_entry *dte,
		daos_unit_oid_t *oid, uint64_t dkey_hash,
		daos_epoch_t epoch, uint32_t flags);
int dtx_del_cos(struct ds_cont_child *cont, struct dtx_id *xid,
		daos_unit_oid_t *oid, uint64_t dkey_hash);
uint64_t dtx_cos_oldest(struct ds_cont_child *cont);

/* dtx_rpc.c */
int dtx_commit(uuid_t po_uuid, uuid_t co_uuid, struct dtx_entry **dtes,
	       int count, bool drop_cos);
int dtx_abort(uuid_t po_uuid, uuid_t co_uuid, daos_epoch_t epoch,
	      struct dtx_entry **dtes, int count);
int dtx_check(uuid_t po_uuid, uuid_t co_uuid, struct dtx_entry *dte);


#endif /* __DTX_INTERNAL_H__ */
