/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_LOGGER_PRIV_H__
#define __OCF_LOGGER_PRIV_H__

#include "ocf/ocf_logger.h"

struct ocf_logger {
	const struct ocf_logger_ops *ops;
	void *priv;
};

__attribute__((format(printf, 3, 4)))
int ocf_log_raw(ocf_logger_t logger, ocf_logger_lvl_t lvl,
		const char *fmt, ...);

int ocf_log_raw_rl(ocf_logger_t logger, const char *func_name);

int ocf_log_stack_trace_raw(ocf_logger_t logger);

void ocf_logger_init(ocf_logger_t logger,
		const struct ocf_logger_ops *ops, void *priv);

int ocf_logger_open(ocf_logger_t logger);

void ocf_logger_close(ocf_logger_t logger);

#endif /* __OCF_LOGGER_PRIV_H__ */
