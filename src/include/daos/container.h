/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_cont: Container Client API
 */

#ifndef __DD_CONT_H__
#define __DD_CONT_H__

#include <daos/common.h>
#include <daos/pool_map.h>
#include <daos/tse.h>
#include <daos_types.h>
#include "checksum.h"
#include <daos/metrics.h>

extern daos_metrics_cntr_t   *cont_rpc_cntrs;

static inline int
dc_cont_metrics_incr_inflightcntr(int opc) {
	if (cont_rpc_cntrs)
		return dc_metrics_incr_inflightcntr(&cont_rpc_cntrs[opc_get(opc)]);
	return 0;
}

static inline int
dc_cont_metrics_incr_completecntr(int opc, int rc) {
	if (cont_rpc_cntrs)
		return dc_metrics_incr_completecntr(&cont_rpc_cntrs[opc_get(opc)], rc);
	return 0;
}

int dc_cont_init(void);
void dc_cont_fini(void);

int dc_cont_tgt_idx2ptr(daos_handle_t coh, uint32_t tgt_idx,
			struct pool_target **tgt);
int dc_cont_node_id2ptr(daos_handle_t coh, uint32_t node_id,
			struct pool_domain **dom);
int dc_cont_hdl2uuid(daos_handle_t coh, uuid_t *hdl_uuid, uuid_t *con_uuid);
daos_handle_t dc_cont_hdl2pool_hdl(daos_handle_t coh);
struct daos_csummer *dc_cont_hdl2csummer(daos_handle_t coh);
struct cont_props dc_cont_hdl2props(daos_handle_t coh);
int dc_cont_hdl2redunfac(daos_handle_t coh);

int dc_cont_local2global(daos_handle_t coh, d_iov_t *glob);
int dc_cont_global2local(daos_handle_t poh, d_iov_t glob,
			 daos_handle_t *coh);

int dc_cont_create(tse_task_t *task);
int dc_cont_open(tse_task_t *task);
int dc_cont_close(tse_task_t *task);
int dc_cont_destroy(tse_task_t *task);
int dc_cont_query(tse_task_t *task);
int dc_cont_set_prop(tse_task_t *task);
int dc_cont_update_acl(tse_task_t *task);
int dc_cont_delete_acl(tse_task_t *task);
int dc_cont_aggregate(tse_task_t *task);
int dc_cont_rollback(tse_task_t *task);
int dc_cont_subscribe(tse_task_t *task);
int dc_cont_list_attr(tse_task_t *task);
int dc_cont_get_attr(tse_task_t *task);
int dc_cont_set_attr(tse_task_t *task);
int dc_cont_del_attr(tse_task_t *task);
int dc_cont_alloc_oids(tse_task_t *task);
int dc_cont_list_snap(tse_task_t *task);
int dc_cont_create_snap(tse_task_t *task);
int dc_cont_destroy_snap(tse_task_t *task);

int dc_cont_metrics_init(void);
void dc_cont_metrics_fini(void);
int dc_cont_metrics_get_rpccntrs(daos_metrics_cont_rpc_cntrs_t *cntrs);
int dc_cont_metrics_reset(void);
#endif /* __DD_CONT_H__ */
