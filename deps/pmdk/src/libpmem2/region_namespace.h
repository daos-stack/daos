/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2020, Intel Corporation */

/*
 * region_namespace.h -- internal definitions for libpmem2
 *                       common region related functions
 */

#ifndef PMDK_REGION_NAMESPACE_H
#define PMDK_REGION_NAMESPACE_H 1

#include "os.h"
#include "pmem2_utils.h"
#include "source.h"

#ifdef __cplusplus
extern "C" {
#endif

int pmem2_get_region_id(const struct pmem2_source *src, unsigned *region_id);

#ifdef __cplusplus
}
#endif

#endif /* PMDK_REGION_NAMESPACE_H */
