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
#ifndef __TELEMETRY_COMMON_H__
#define __TELEMETRY_COMMON_H__

#define D_TM_VERSION			1
#define D_TM_MAX_NAME_LEN		256
#define D_TM_MAX_SHORT_LEN		64
#define D_TM_MAX_LONG_LEN		1024
#define D_TM_TIME_BUFF_LEN		26
#define D_TM_SUCCESS			0

#define D_TM_SHARED_MEMORY_KEY		0x10242048
#define D_TM_SHARED_MEMORY_SIZE		(1024 * 1024)

enum {
	D_TM_DIRECTORY			= 0x001,
	D_TM_COUNTER			= 0x002,
	D_TM_TIMESTAMP			= 0x004,
	D_TM_TIMER_SNAPSHOT		= 0x008,
	D_TM_DURATION			= 0x010,
	D_TM_GAUGE			= 0x020,
	D_TM_CLOCK_REALTIME		= 0x040,
	D_TM_CLOCK_PROCESS_CPUTIME	= 0x080,
	D_TM_CLOCK_THREAD_CPUTIME	= 0x100,
};

struct d_tm_metric_t {
	union data {
		uint64_t value;
		struct timespec tms[2];
	} dtm_data;
	char *dtm_sh_desc;
	char *dtm_lng_desc;
};

struct d_tm_node_t {
	struct d_tm_node_t *dtn_child;
	struct d_tm_node_t *dtn_sibling;
	char *dtn_name;
	int dtn_type;
	pthread_mutex_t dtn_lock;
	struct d_tm_metric_t *dtn_metric;
};

struct d_tm_nodeList_t {
	struct d_tm_node_t *dtnl_node;
	struct d_tm_nodeList_t *dtnl_next;
};

void *d_tm_shmalloc(int length);
uint64_t *d_tm_allocate_shared_memory(int rank, size_t mem_size);
int d_tm_clock_id(int clk_id);
bool d_tm_validate_shmem_ptr(uint64_t *shmem_root, void *ptr);
struct d_tm_nodeList_t *d_tm_add_node(struct d_tm_node_t *src,
				      struct d_tm_nodeList_t *nodelist);
void d_tm_list_free(struct d_tm_nodeList_t *nodeList);
void d_tm_free_node(uint64_t *shmem_root, struct d_tm_node_t *node);
struct d_tm_node_t *d_tm_find_child(uint64_t *shmem_root,
				    struct d_tm_node_t *parent, char *name);
int d_tm_build_path(char **path, char *item, va_list args);
int d_tm_alloc_node(struct d_tm_node_t **newnode, char *name);
int d_tm_add_child(struct d_tm_node_t **newnode, struct d_tm_node_t *parent,
		   char *name);
int d_tm_get_version(void);
#endif /* __TELEMETRY_COMMON_H__ */
