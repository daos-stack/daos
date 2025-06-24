/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/include/daos/metrics.h
 */

#ifndef __DAOS_METRICS_H__
#define __DAOS_METRICS_H__

#include <daos/common.h>
#include <daos/tls.h>
#include <daos_types.h>
#include <gurt/telemetry_common.h>

#define DC_TM_JOB_ROOT_ID             256
/* For now TLS is only enabled if metrics are enabled */
#define DAOS_CLIENT_METRICS_DUMP_DIR  "D_CLIENT_METRICS_DUMP_DIR"
#define DAOS_CLIENT_METRICS_ENABLE    "D_CLIENT_METRICS_ENABLE"
#define DAOS_CLIENT_METRICS_RETAIN    "D_CLIENT_METRICS_RETAIN"
extern bool daos_client_metric;
extern bool daos_client_metric_retain;

struct daos_module_metrics {
	/* Indicate where the keys should be instantiated */
	enum daos_module_tag dmm_tags;

	/**
	 * allocate metrics with path to ephemeral shmem for to the
	 * newly-created pool
	 */
	void *(*dmm_init)(const char *path, int tgt_id);
	void (*dmm_fini)(void *data);

	/**
	 * Get the number of metrics allocated by this module in total (including all targets).
	 */
	int (*dmm_nr_metrics)(void);
};

/* Estimate of bytes per typical metric node */
#define NODE_BYTES                                                                                 \
	(sizeof(struct d_tm_node_t) + sizeof(struct d_tm_metric_t) + 64 /* buffer for metadata */)
/* Estimate of bytes per histogram bucket */
#define BUCKET_BYTES (sizeof(struct d_tm_bucket_t) + NODE_BYTES)
/*
   Estimate of bytes per metric.
   This is a generous high-water mark assuming most metrics are not using
   histograms. May need adjustment if the balance of metrics changes.
*/
#define PER_METRIC_BYTES                                                                           \
	(NODE_BYTES + sizeof(struct d_tm_stats_t) + sizeof(struct d_tm_histogram_t) + BUCKET_BYTES)

int
daos_metrics_init(enum daos_module_tag tag, uint32_t id, struct daos_module_metrics *metrics);
void
daos_metrics_fini(void);
int
daos_module_init_metrics(enum dss_module_tag tag, void **metrics, const char *path, int tgt_id);
void
daos_module_fini_metrics(enum dss_module_tag tag, void **metrics);

int
daos_module_nr_pool_metrics(void);

/**
 *  Called during library initialization to init metrics.
 */
int
dc_tm_init(crt_init_options_t *crt_info);

/**
 *  Called during library finalization to free metrics resources
 */
void
dc_tm_fini(void);

#endif /*__DAOS_METRICS_H__*/
