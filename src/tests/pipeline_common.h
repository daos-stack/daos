/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SIMPLE_PIPE_COMMON_H__
#define __DAOS_SIMPLE_PIPE_COMMON_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_pipeline.h>

#define ASSERT(cond, ...)                                                                          \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			fprintf(stderr, __VA_ARGS__);                                              \
			exit(1);                                                                   \
		}                                                                                  \
	} while (0)

int
free_pipeline(daos_pipeline_t *pipe);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_SIMPLE_PIPE_COMMON_H__ */
