/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
int d_tm_get_gauge(uint64_t *val, struct d_tm_stats_t *stats,
		   uint64_t *shmem_root, struct d_tm_node_t *node,
		   char *metric);
int d_tm_get_duration(struct timespec *tms, struct d_tm_stats_t *stats,
		      uint64_t *shmem_root, struct d_tm_node_t *node,
		      char *metric);
int d_tm_get_metadata(char **sh_desc, char **lng_desc, uint64_t *shmem_root,
		      struct d_tm_node_t *node, char *metric);
int d_tm_get_num_buckets(struct d_tm_histogram_t *histogram,
			 uint64_t *shmem_root, struct d_tm_node_t *node);
int d_tm_get_bucket_range(struct d_tm_bucket_t *bucket, int bucket_id,
			  uint64_t *shmem_root, struct d_tm_node_t *node);

/* Developer facing client API to discover topology and manage results */
uint64_t *d_tm_get_shared_memory(int srv_idx);
void *d_tm_conv_ptr(uint64_t *shmem_root, void *ptr);
struct d_tm_node_t *d_tm_get_root(uint64_t *shmem);
struct d_tm_node_t *d_tm_find_metric(uint64_t *shmem_root, char *path);
uint64_t d_tm_count_metrics(uint64_t *shmem_root, struct d_tm_node_t *node,
			    int d_tm_type);
int d_tm_list(struct d_tm_nodeList_t **head, uint64_t *shmem_root,
	      struct d_tm_node_t *node, int d_tm_type);
void d_tm_print_my_children(uint64_t *shmem_root, struct d_tm_node_t *node,
			    int level, int filter, char *path, int format,
			    bool show_meta, bool show_timestamp, FILE *stream);
void d_tm_print_node(uint64_t *shmem_root, struct d_tm_node_t *node, int level,
		     char *name, int format, bool show_meta,
		     bool show_timestamp, FILE *stream);
void d_tm_print_field_descriptors(int extra_fields, FILE *stream);
void d_tm_print_counter(uint64_t val, char *name, int format, FILE *stream);
void d_tm_print_timestamp(time_t *clk, char *name, int format, FILE *stream);
void d_tm_print_timer_snapshot(struct timespec *tms, char *name, int tm_type,
			       int format, FILE *stream);
void d_tm_print_duration(struct timespec *tms, struct d_tm_stats_t *stats,
			 char *name, int tm_type, int format, FILE *stream);
void d_tm_print_gauge(uint64_t val, struct d_tm_stats_t *stats, char *name,
		      int format, FILE *stream);
void d_tm_print_metadata(char *short_desc, char *long_desc, int format,
			 FILE *stream);
#endif /* __TELEMETRY_CONSUMER_H__ */
