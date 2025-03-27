/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_env.h"
#include "ocf/ocf_logger.h"
#include "ocf_logger_priv.h"
#include "ocf_priv.h"

/*
 *
 */
__attribute__((format(printf, 3, 4)))
int ocf_log_raw(ocf_logger_t logger, ocf_logger_lvl_t lvl,
		const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (!logger->ops->print)
		return -ENOTSUP;

	va_start(args, fmt);
	ret = logger->ops->print(logger, lvl, fmt, args);
	va_end(args);

	return ret;
}

int ocf_log_raw_rl(ocf_logger_t logger, const char *func_name)
{
	if (!logger->ops->print_rl)
		return -ENOTSUP;

	return logger->ops->print_rl(logger, func_name);
}

/*
 *
 */
int ocf_log_stack_trace_raw(ocf_logger_t logger)
{
	if (!logger->ops->dump_stack)
		return -ENOTSUP;

	return logger->ops->dump_stack(logger);
}

void ocf_logger_init(ocf_logger_t logger,
		const struct ocf_logger_ops *ops, void *priv)
{
	logger->ops = ops;
	logger->priv = priv;
}

int ocf_logger_open(ocf_logger_t logger)
{
	if (!logger->ops->open)
		return 0;

	return logger->ops->open(logger);
}

void ocf_logger_close(ocf_logger_t logger)
{
	if (!logger->ops->close)
		return;

	logger->ops->close(logger);
}

void ocf_logger_set_priv(ocf_logger_t logger, void *priv)
{
	OCF_CHECK_NULL(logger);

	logger->priv = priv;
}

void *ocf_logger_get_priv(ocf_logger_t logger)
{
	OCF_CHECK_NULL(logger);

	return logger->priv;
}

