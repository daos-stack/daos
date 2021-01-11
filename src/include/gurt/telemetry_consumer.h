/**
 * (C) Copyright 2020 Intel Corporation.
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
#ifndef __TELEMETRY_CONSUMER_H__
#define __TELEMETRY_CONSUMER_H__

/* Developer facing client API to read data */
int d_tm_get_counter(uint64_t *val, uint64_t *shmem_root,
		     struct d_tm_node_t *node, char *metric);
int d_tm_get_timestamp(time_t *val, uint64_t *shmem_root,
		       struct d_tm_node_t *node, char *metric);
int d_tm_get_timer_snapshot(struct timespec *tms, uint64_t *shmem_root,
			    struct d_tm_node_t *node, char *metric);
int d_tm_get_duration(struct timespec *tms, uint64_t *shmem_root,
		      struct d_tm_node_t *node, char *metric);
int d_tm_get_gauge(uint64_t *val, uint64_t *shmem_root,
		   struct d_tm_node_t *node, char *metric);
int d_tm_get_metadata(char **sh_desc, char **lng_desc, uint64_t *shmem_root,
		      struct d_tm_node_t *node, char *metric);

/* Developer facing client API to discover topology and manage results */
uint64_t *d_tm_get_shared_memory(int rank);
void *d_tm_conv_ptr(uint64_t *shmem_root, void *ptr);
struct d_tm_node_t *d_tm_get_root(uint64_t *shmem);
struct d_tm_node_t *d_tm_find_metric(uint64_t *shmem_root, char *path);
uint64_t d_tm_get_num_objects(uint64_t *shmem_root, char *path,
			      int dtn_type);
uint64_t d_tm_count_metrics(uint64_t *shmem_root, struct d_tm_node_t *node);
int d_tm_list(struct d_tm_nodeList_t **nodelist, uint64_t *shmem_root,
	      char *path, int dtn_type);
void d_tm_print_my_children(uint64_t *shmem_root, struct d_tm_node_t *node,
			    int level, FILE *stream);
void d_tm_print_node(uint64_t *shmem_root, struct d_tm_node_t *node, int level,
		     FILE *stream);
void d_tm_print_counter(uint64_t val, char *name, FILE *stream);
void d_tm_print_timestamp(time_t *clk, char *name, FILE *stream);
void d_tm_print_timer_snapshot(struct timespec *tms, char *name, int tm_type,
			       FILE *stream);
void d_tm_print_duration(struct timespec *tms, char *name, int tm_type,
			 FILE *stream);
void d_tm_print_gauge(uint64_t val, char *name, FILE *stream);
#endif /* __TELEMETRY_CONSUMER_H__ */
