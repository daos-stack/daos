/*
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CRT_PERF_ERROR_H__
#define __CRT_PERF_ERROR_H__

#include <gurt/common.h>

/*****************/
/* Public Macros */
/*****************/

/* Check for D_ ret value and goto label */
#define CRT_PERF_CHECK_D_ERROR(label, rc, ...)                                                     \
	do {                                                                                       \
		if (unlikely(rc != 0)) {                                                           \
			DL_ERROR(rc, __VA_ARGS__);                                                 \
			goto label;                                                                \
		}                                                                                  \
	} while (0)

/* Check for cond, set ret to err_val and goto label */
#define CRT_PERF_CHECK_ERROR(cond, label, rc, err_val, ...)                                        \
	do {                                                                                       \
		if (unlikely(cond)) {                                                              \
			rc = err_val;                                                              \
			DL_ERROR(rc, __VA_ARGS__);                                                 \
			goto label;                                                                \
		}                                                                                  \
	} while (0)

#endif /* __CRT_PERF_ERROR_H__ */
