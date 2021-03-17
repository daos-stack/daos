/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TELEMETRY_COMMON_H__
#define __TELEMETRY_COMMON_H__

#include <gurt/common.h>

#define D_TM_VERSION			1
#define D_TM_MAX_NAME_LEN		256
#define D_TM_MAX_SHORT_LEN		64
#define D_TM_MAX_LONG_LEN		1024
#define D_TM_TIME_BUFF_LEN		26

#define D_TM_SHARED_MEMORY_KEY		0x10242048
#define D_TM_SHARED_MEMORY_SIZE		(1024 * 1024)

/** d_tm_metric_types */
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
	D_TM_ALL_NODES			= (D_TM_DIRECTORY | \
					   D_TM_COUNTER | \
					   D_TM_TIMESTAMP | \
					   D_TM_TIMER_SNAPSHOT | \
					   D_TM_DURATION | \
					   D_TM_GAUGE)
};

enum {
	D_TM_SERVER_PROCESS		= 0x000,
	D_TM_SERIALIZATION		= 0x001,
	D_TM_RETAIN_SHMEM		= 0x002,
};

/** Output formats */
enum {
	D_TM_STANDARD			= 0x001,
	D_TM_CSV			= 0x002,
};

/** Optional CSV field descriptors */
enum {
	D_TM_INCLUDE_TIMESTAMP		= 0x001,
	D_TM_INCLUDE_METADATA		= 0x002,
};

/**
 * @brief Statistics for gauge and duration metrics
 *
 * Stores the computed min, max, sum, standard deviation, mean, sum of squares
 * and sample size.
 */
struct d_tm_stats_t {
	uint64_t dtm_min;
	uint64_t dtm_max;
	uint64_t dtm_sum;
	double std_dev;
	double mean;
	double sum_of_squares;
	uint64_t sample_size;
};

struct d_tm_bucket_t {
	uint64_t dtb_min;
	uint64_t dtb_max;
	struct d_tm_node_t *dtb_bucket;
};

struct d_tm_histogram_t {
	struct d_tm_bucket_t *dth_buckets;
	int dth_num_buckets;
	int dth_initial_width;
	int dth_value_multiplier;
};

struct d_tm_metric_t {
	union data {
		uint64_t value;
		struct timespec tms[2];
	} dtm_data;
	struct d_tm_stats_t *dtm_stats;
	struct d_tm_histogram_t *dtm_histogram;
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
	bool dtn_protect;
};

struct d_tm_nodeList_t {
	struct d_tm_node_t *dtnl_node;
	struct d_tm_nodeList_t *dtnl_next;
};

void *d_tm_shmalloc(int length);
uint64_t *d_tm_allocate_shared_memory(int srv_idx, size_t mem_size);
int d_tm_clock_id(int clk_id);
bool d_tm_validate_shmem_ptr(uint64_t *shmem_root, void *ptr);
int d_tm_add_node(struct d_tm_node_t *src, struct d_tm_nodeList_t **nodelist);
void d_tm_list_free(struct d_tm_nodeList_t *nodeList);
void d_tm_free_node(uint64_t *shmem_root, struct d_tm_node_t *node);
struct d_tm_node_t *d_tm_find_child(uint64_t *shmem_root,
				    struct d_tm_node_t *parent, char *name);
int d_tm_alloc_node(struct d_tm_node_t **newnode, char *name);
int d_tm_add_child(struct d_tm_node_t **newnode, struct d_tm_node_t *parent,
		   char *name);
int d_tm_get_version(void);
void d_tm_compute_stats(struct d_tm_node_t *node, uint64_t value);
double d_tm_compute_standard_dev(double sum_of_squares, uint64_t sample_size,
				 double mean);
int d_tm_compute_histogram(struct d_tm_node_t *node, uint64_t value);
void d_tm_print_stats(FILE *stream, struct d_tm_stats_t *stats, int format);
#endif /* __TELEMETRY_COMMON_H__ */
