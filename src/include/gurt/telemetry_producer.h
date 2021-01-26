/**
 * (C) Copyright 2020-2021 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
int d_tm_init(int srv_idx, uint64_t mem_size);
int d_tm_add_metric(struct d_tm_node_t **node, char *metric, int metric_type,
		    char *sh_desc, char *lng_desc);
void d_tm_fini(void);
#endif /* __TELEMETRY_PRODUCER_H__ */
