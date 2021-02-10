/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TELEMETRY_PRODUCER_H__
#define __TELEMETRY_PRODUCER_H__

/* Developer facing server API to write data */
int d_tm_increment_counter(struct d_tm_node_t **metric, char *item, ...);
int d_tm_record_timestamp(struct d_tm_node_t **metric, char *item, ...);
int d_tm_take_timer_snapshot(struct d_tm_node_t **metric, int clk_id,
			     char *item, ...);
int d_tm_mark_duration_start(struct d_tm_node_t **metric, int clk_id,
			     char *item, ...);
int d_tm_mark_duration_end(struct d_tm_node_t **metric, char *item, ...);
int d_tm_set_gauge(struct d_tm_node_t **metric, uint64_t value,
		   char *item, ...);
int d_tm_increment_gauge(struct d_tm_node_t **metric, uint64_t value,
			 char *item, ...);
int d_tm_decrement_gauge(struct d_tm_node_t **metric, uint64_t value,
			 char *item, ...);

/* Other server functions */
int d_tm_init(int id, uint64_t mem_size);
int d_tm_init_client(int client_id, uint64_t mem_size);
int d_tm_add_metric(struct d_tm_node_t **node, char *metric, int metric_type,
		    char *sh_desc, char *lng_desc);
void d_tm_fini(void);
#endif /* __TELEMETRY_PRODUCER_H__ */
