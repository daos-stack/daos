/**
 * (C) Copyright 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TELEMETRY_PRODUCER_H__
#define __TELEMETRY_PRODUCER_H__

#include <sys/types.h>
#include <gurt/telemetry_common.h>

/* Developer facing server API to write data */
void d_tm_set_counter(struct d_tm_node_t *metric, uint64_t value);
void d_tm_inc_counter(struct d_tm_node_t *metric, uint64_t value);
void d_tm_record_timestamp(struct d_tm_node_t *metric);
void d_tm_record_meminfo(struct d_tm_node_t *metric);
void d_tm_take_timer_snapshot(struct d_tm_node_t *metric, int clk_id);
void d_tm_mark_duration_start(struct d_tm_node_t *metric, int clk_id);
void d_tm_mark_duration_end(struct d_tm_node_t *metric);
void d_tm_set_gauge(struct d_tm_node_t *metric, uint64_t value);
void d_tm_inc_gauge(struct d_tm_node_t *metric, uint64_t value);
void d_tm_dec_gauge(struct d_tm_node_t *metric, uint64_t value);

/* Other server functions */
int d_tm_init(int id, uint64_t mem_size, int flags);
int
    d_tm_init_with_name(int id, uint64_t mem_size, int flags, const char *root_name);
int
    d_tm_init_histogram(struct d_tm_node_t *node, char *path, int num_buckets, int initial_width,
			int multiplier, const char *unit);
int d_tm_add_metric(struct d_tm_node_t **node, int metric_type, char *desc,
		    char *units, const char *fmt, ...);
int d_tm_add_ephemeral_dir(struct d_tm_node_t **node, size_t size_bytes,
			   const char *fmt, ...);
int
    d_tm_attach_path_segment(key_t key, const char *fmt, ...);
int d_tm_del_ephemeral_dir(const char *fmt, ...);
int
     d_tm_try_del_ephemeral_dir(const char *fmt, ...);
void d_tm_fini(void);

#endif /* __TELEMETRY_PRODUCER_H__ */
