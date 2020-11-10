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
#ifndef __TELEMETRY_H__
#define __TELEMETRY_H__

#define D_TM_VERSION	1

#define D_TM_MAX_NAME_LEN	256
#define D_TM_MAX_SHORT_LEN	64
#define D_TM_MAX_LONG_LEN	1024
#define D_TM_TIME_BUFF_LEN	64

#define D_TM_SUCCESS 0

#define D_TM_SHARED_MEMORY_KEY 0x10242048
#define D_TM_SHARED_MEMORY_SIZE (1024 * 1024)

#include "time.h"
#include <stdarg.h>

enum d_tm_metric_types {
	D_TM_DIRECTORY = 0x1,
	D_TM_COUNTER = 0x2,
	D_TM_TIMESTAMP = 0x4,
	D_TM_HIGH_RES_TIMER = 0x8,
	D_TM_DURATION = 0x10,
	D_TM_GAUGE = 0x20,
	D_TM_CLOCK_REALTIME = 0x40,
	D_TM_CLOCK_PROCESS_CPUTIME = 0x80,
	D_TM_CLOCK_THREAD_CPUTIME = 0x100
};

struct d_tm_metric_t {
	union data {
		uint64_t value;
		struct timespec tms[2];
	} data;
	char *shortDesc;
	char *longDesc;
};

struct d_tm_node_t {
	struct d_tm_node_t *child;
	struct d_tm_node_t *sibling;
	char *name;
	int d_tm_type;
	pthread_mutex_t lock;
	struct d_tm_metric_t *metric;
};

struct d_tm_nodeList_t {
	struct d_tm_node_t *node;
	struct d_tm_nodeList_t *next;
};

int d_tm_init(int rank, uint64_t memSize);

int d_tm_add_metric(struct d_tm_node_t **node, char *metric, int metricType,
		    char *shortDesc, char *longDesc);

struct d_tm_node_t *d_tm_get_root(uint64_t *shmemRoot);
void d_tm_print_my_children(uint64_t *cshmemRoot, struct d_tm_node_t *node,
			    int level);

/* Developer facing server API to write data */
int d_tm_increment_counter(struct d_tm_node_t **metric, char *item, ...);
int d_tm_record_timestamp(struct d_tm_node_t **metric, char *item, ...);
int d_tm_record_high_res_timer(struct d_tm_node_t **metric, char *item, ...);
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
struct d_tm_nodeList_t *d_tm_add_node(struct d_tm_node_t *src,
				      struct d_tm_nodeList_t *nodelist);
uint8_t *d_tm_allocate_shared_memory(int rank, size_t mem_size);
void d_tm_fini(void);
void d_tm_free_node(uint64_t *cshmemRoot, struct d_tm_node_t *node);
void *d_tm_shmalloc(int length);
int d_tm_shmalloc_offset(int length);
int d_tm_clock_id(int clk_id);

struct d_tm_node_t *d_tm_find_child(uint64_t *cshmemRoot,
				    struct d_tm_node_t *parent, char *name);
struct d_tm_node_t *d_tm_find_metric(uint64_t *cshmemRoot, char *metric);
bool d_tm_validate_shmem_ptr(uint64_t *cshmemRoot, void *ptr);

/* Developer facing client API to read data */
int d_tm_get_counter(uint64_t *val, uint64_t *cshmemRoot,
		     struct d_tm_node_t *node, char *metric);
int d_tm_get_timestamp(time_t *val, uint64_t *cshmemRoot,
		       struct d_tm_node_t *node, char *metric);
int d_tm_get_highres_timer(struct timespec *tms, uint64_t *cshmemRoot,
			   struct d_tm_node_t *node, char *metric);
int d_tm_get_duration(struct timespec *tms, uint64_t *cshmemRoot,
		      struct d_tm_node_t *node, char *metric);
int d_tm_get_gauge(uint64_t *val, uint64_t *cshmemRoot,
		   struct d_tm_node_t *node, char *metric);

int d_tm_get_metadata(char **shortDesc, char **longDesc, uint64_t *cshmemRoot,
		      struct d_tm_node_t *node, char *metric);

/* Developer facing client API to discover topology and manage results */
int d_tm_list(struct d_tm_nodeList_t **nodelist, uint64_t *cshmemRoot,
	      char *path, int d_tm_type);
uint64_t d_tm_get_num_objects(uint64_t *cshmemRoot, char *path,
			      int d_tm_type);
uint64_t d_tm_count_metrics(uint64_t *cshmemRoot, struct d_tm_node_t *node);
void d_tm_list_free(struct d_tm_nodeList_t *nodeList);
int d_tm_get_version(void);
uint8_t *d_tm_get_shared_memory(int rank);

struct d_tm_node_t *d_tm_convert_node_ptr(uint64_t *cshmemRoot, void *ptr);
struct d_tm_metric_t *d_tm_convert_metric_ptr(uint64_t *cshmemRoot, void *ptr);
char *d_tm_convert_char_ptr(uint64_t *cshmemRoot, void *ptr);

#endif /* __TELEMETRY_H__ */
