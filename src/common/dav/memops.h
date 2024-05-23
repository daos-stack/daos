/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2020, Intel Corporation */

/*
 * memops.h -- aggregated memory operations helper definitions
 */

#ifndef __DAOS_COMMON_MEMOPS_H
#define __DAOS_COMMON_MEMOPS_H 1

#include <stddef.h>
#include <stdint.h>

#include "vec.h"
#include "mo_wal.h"
#include "ulog.h"

enum operation_log_type {
	LOG_PERSISTENT, /* log of persistent modifications */
	LOG_TRANSIENT, /* log of transient memory modifications */

	MAX_OPERATION_LOG_TYPE
};

enum log_type {
	LOG_TYPE_UNDO,
	LOG_TYPE_REDO,

	MAX_LOG_TYPE,
};

struct user_buffer_def {
	void *addr;
	size_t size;
};

struct operation_context;

struct operation_context *
operation_new(struct ulog *redo, size_t ulog_base_nbytes,
	ulog_extend_fn extend, ulog_free_fn ulog_free,
	const struct mo_ops *p_ops, enum log_type type);

void operation_init(struct operation_context *ctx);
void operation_start(struct operation_context *ctx);

void operation_delete(struct operation_context *ctx);
void operation_free_logs(struct operation_context *ctx);

int operation_add_buffer(struct operation_context *ctx,
	void *dest, void *src, size_t size, ulog_operation_type type);

int operation_add_entry(struct operation_context *ctx,
	void *ptr, uint64_t value, ulog_operation_type type);
int operation_add_typed_entry(struct operation_context *ctx,
	void *ptr, uint64_t value,
	ulog_operation_type type, enum operation_log_type log_type);
void operation_set_auto_reserve(struct operation_context *ctx,
		int auto_reserve);

int operation_reserve(struct operation_context *ctx, size_t new_capacity);
void operation_process(struct operation_context *ctx);
void operation_finish(struct operation_context *ctx, unsigned flags);
void operation_cancel(struct operation_context *ctx);

#endif /* __DAOS_COMMON_MEMOPS_H */
