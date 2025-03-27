
/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <ocf/ocf_types.h>
#include <ocf/ocf_logger.h>
#include <stdarg.h>
#include "ocf_env.h"

#define LOG_BUFFER_SIZE 4096

struct pyocf_logger_priv {
	int (*pyocf_log)(void *pyocf_logger, ocf_logger_lvl_t lvl, char *msg);
};

int pyocf_printf_helper(ocf_logger_t logger, ocf_logger_lvl_t lvl,
		const char *fmt, va_list args)
{
	char *buffer = env_zalloc(LOG_BUFFER_SIZE, ENV_MEM_NORMAL);
	struct pyocf_logger_priv *priv = ocf_logger_get_priv(logger);
	int ret;

	if (!buffer) {
		ret = -ENOMEM;
		goto out;
	}

	ret = vsnprintf(buffer, LOG_BUFFER_SIZE, fmt, args);
	if (ret < 0) {
		env_free(buffer);
		goto out;
	}

	ret = priv->pyocf_log(logger, lvl, buffer);

	env_free(buffer);

out:
	return ret;
}
