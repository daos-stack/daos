/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TELEMETRY_PRODUCER_H__
#define __TELEMETRY_PRODUCER_H__

/* Developer facing server API to write data */
int d_tm_increment_counter(struct d_tm_node_t **metric, uint64_t value,
			   const char *fmt, ...);
int d_tm_record_timestamp(struct d_tm_node_t **metric, const char *fmt, ...);
int d_tm_take_timer_snapshot(struct d_tm_node_t **metric, int clk_id,
			     const char *fmt, ...);
int d_tm_mark_duration_start(struct d_tm_node_t **metric, int clk_id,
			     const char *fmt, ...);
int d_tm_mark_duration_end(struct d_tm_node_t **metric, int err,
			   const char *fmt, ...);
int d_tm_set_gauge(struct d_tm_node_t **metric, uint64_t value,
		   const char *fmt, ...);
int d_tm_increment_gauge(struct d_tm_node_t **metric, uint64_t value,
			 const char *fmt, ...);
int d_tm_decrement_gauge(struct d_tm_node_t **metric, uint64_t value,
			 const char *fmt, ...);

/* Other server functions */
int d_tm_init(int id, uint64_t mem_size, int flags);
int d_tm_init_histogram(struct d_tm_node_t *node, char *path, int num_buckets,
			int initial_width, int multiplier);
int d_tm_add_metric(struct d_tm_node_t **node, int metric_type, char *desc,
		    char *units, const char *fmt, ...);
void d_tm_fini(void);
#endif /* __TELEMETRY_PRODUCER_H__ */
