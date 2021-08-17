/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_metrics: Client metrics API
 */

#ifndef __DC_METRICS_H__
#define __DC_METRICS_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>
#include <daos_metrics.h>
#include <daos/rpc.h>

typedef struct {
	d_list_t			list;
	daos_metrics_stat_t		update_stat;
	daos_metrics_stat_t		fetch_stat;
	daos_metrics_iodist_bsz_t	ids;
	daos_metrics_iodist_bpt_t	idp;
} dc_metrics_tls_data_t;

static inline void
dc_metrics_clr_cntr(daos_metrics_cntr_t *cntr) {
	__atomic_store_n(&cntr->mc_inflight, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&cntr->mc_failure, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&cntr->mc_success, 0, __ATOMIC_RELAXED);
}

static inline int
dc_metrics_incr_inflightcntr(daos_metrics_cntr_t *cntr) {
	return __atomic_add_fetch(&cntr->mc_inflight, 1, __ATOMIC_RELAXED);
}

static inline int
dc_metrics_incr_completecntr(daos_metrics_cntr_t *cntr, int rc) {
	return __atomic_add_fetch(rc ? &cntr->mc_failure : &cntr->mc_success, 1, __ATOMIC_RELAXED);
}

static inline void
dc_metrics_cntr_copy(daos_metrics_cntr_t *dst, daos_metrics_cntr_t *src) {
	unsigned long inflight = src->mc_inflight;
	unsigned long success = src->mc_success;
	unsigned long failure = src->mc_failure;

	if ((success + failure) > inflight)
		dst->mc_inflight =  0;
	else
		dst->mc_inflight = inflight - (success + failure);
	dst->mc_success = success;
	dst->mc_failure = failure;
}

static inline void
dc_metrics_cntr_merge(daos_metrics_cntr_t *dst, daos_metrics_cntr_t *src) {
	unsigned long inflight = src->mc_inflight;
	unsigned long success = src->mc_success;
	unsigned long failure = src->mc_failure;

	if ((success + failure) > inflight)
		dst->mc_inflight +=  0;
	else
		dst->mc_inflight += inflight - (success + failure);
	dst->mc_success += success;
	dst->mc_failure += failure;
}

int dc_metrics_init(void);
void dc_metrics_fini(void);
int dc_metrics_update_iostats(int is_update, size_t size);
int dc_metrics_update_iodist(int is_update, size_t size, struct daos_oclass_attr *ptype,
		int is_full_stripe);

#endif
