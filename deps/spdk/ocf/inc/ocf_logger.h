/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_LOGGER_H__
#define __OCF_LOGGER_H__

/**
 * @file
 * @brief Logger API
 */

#include <ocf/ocf_types.h>
#include <stdarg.h>

/**
 * @brief Verbosity levels of context log
 */
typedef enum {
	log_emerg,
	log_alert,
	log_crit,
	log_err,
	log_warn,
	log_notice,
	log_info,
	log_debug,
} ocf_logger_lvl_t;

struct ocf_logger_ops {
	int (*open)(ocf_logger_t logger);
	void (*close)(ocf_logger_t logger);
	int (*print)(ocf_logger_t logger, ocf_logger_lvl_t lvl,
			const char *fmt, va_list args);
	int (*print_rl)(ocf_logger_t logger, const char *func_name);
	int (*dump_stack)(ocf_logger_t logger);
};

void ocf_logger_set_priv(ocf_logger_t logger, void *priv);

void *ocf_logger_get_priv(ocf_logger_t logger);

#endif /* __OCF_LOGGER_H__ */
