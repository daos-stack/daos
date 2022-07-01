/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TELEMETRY_CONSUMER_H__
#define __TELEMETRY_CONSUMER_H__

#include <gurt/telemetry_common.h>

/* Developer facing client API to read data */
char *
d_tm_get_name(struct d_tm_context *ctx, struct d_tm_node_t *node);
int
d_tm_get_counter(struct d_tm_context *ctx, uint64_t *val, struct d_tm_node_t *node);
int
d_tm_get_timestamp(struct d_tm_context *ctx, time_t *val, struct d_tm_node_t *node);
int
d_tm_get_timer_snapshot(struct d_tm_context *ctx, struct timespec *tms, struct d_tm_node_t *node);
int
d_tm_get_gauge(struct d_tm_context *ctx, uint64_t *val, struct d_tm_stats_t *stats,
	       struct d_tm_node_t *node);
int
d_tm_get_duration(struct d_tm_context *ctx, struct timespec *tms, struct d_tm_stats_t *stats,
		  struct d_tm_node_t *node);
int
d_tm_get_metadata(struct d_tm_context *ctx, char **desc, char **units, struct d_tm_node_t *node);
int
d_tm_get_num_buckets(struct d_tm_context *ctx, struct d_tm_histogram_t *histogram,
		     struct d_tm_node_t *node);
int
d_tm_get_bucket_range(struct d_tm_context *ctx, struct d_tm_bucket_t *bucket, int bucket_id,
		      struct d_tm_node_t *node);

/* Developer facing client API to discover topology and manage results */
struct d_tm_context *
d_tm_open(int id);
void
d_tm_close(struct d_tm_context **ctx);
void
d_tm_gc_ctx(struct d_tm_context *ctx);
void *
d_tm_conv_ptr(struct d_tm_context *ctx, struct d_tm_node_t *node, void *ptr);
struct d_tm_node_t *
d_tm_get_root(struct d_tm_context *ctx);
struct d_tm_node_t *
d_tm_get_child(struct d_tm_context *ctx, struct d_tm_node_t *node);
struct d_tm_node_t *
d_tm_get_sibling(struct d_tm_context *ctx, struct d_tm_node_t *node);
struct d_tm_node_t *
d_tm_find_metric(struct d_tm_context *ctx, char *path);
uint64_t
d_tm_count_metrics(struct d_tm_context *ctx, struct d_tm_node_t *node, int d_tm_type);
int
d_tm_list(struct d_tm_context *ctx, struct d_tm_nodeList_t **head, struct d_tm_node_t *node,
	  int d_tm_type);
int
d_tm_list_subdirs(struct d_tm_context *ctx, struct d_tm_nodeList_t **head, struct d_tm_node_t *node,
		  uint64_t *node_count, int max_depth);
void
d_tm_print_my_children(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, int filter,
		       char *path, int format, int opt_fields, FILE *stream);
void
d_tm_print_node(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, char *name,
		int format, int opt_fields, FILE *stream);
void
d_tm_print_field_descriptors(int opt_fields, FILE *stream);
void
d_tm_print_counter(uint64_t val, char *name, int format, char *units, int opt_fields, FILE *stream);
void
d_tm_print_timestamp(time_t *clk, char *name, int format, int opt_fields, FILE *stream);
void
d_tm_print_timer_snapshot(struct timespec *tms, char *name, int tm_type, int format, int opt_fields,
			  FILE *stream);
void
d_tm_print_duration(struct timespec *tms, struct d_tm_stats_t *stats, char *name, int tm_type,
		    int format, int opt_fields, FILE *stream);
void
d_tm_print_gauge(uint64_t val, struct d_tm_stats_t *stats, char *name, int format, char *units,
		 int opt_fields, FILE *stream);
void
d_tm_print_metadata(char *desc, char *units, int format, FILE *stream);
int
d_tm_clock_id(int clk_id);
char *
d_tm_clock_string(int clk_id);

#endif /* __TELEMETRY_CONSUMER_H__ */
