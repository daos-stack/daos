/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DFUSE_COMMON_H__
#define __DFUSE_COMMON_H__

#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(dfuse)
#endif

#include "dfuse_log.h"

typedef struct {
	int shm_region_size;
	int pool_info_size;
	int cont_info_size;
	int dfs_info_size;
} DFS_INFO_SIZE_HEAD;

#endif /* __DFUSE_COMMON_H__ */
