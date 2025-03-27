/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifndef __OCF_CFG_H__
#define __OCF_CFG_H__

/**
 * @file
 * @brief OCF configuration file
 */

/**
 * Configure maximum numbers of cores in cache instance
 */
#ifndef OCF_CONFIG_MAX_CORES
#define OCF_CONFIG_MAX_CORES 4096
#endif

/** Maximum number of IO classes that can be configured */
#ifndef OCF_CONFIG_MAX_IO_CLASSES
#define OCF_CONFIG_MAX_IO_CLASSES 33
#endif

#if OCF_CONFIG_MAX_IO_CLASSES > 256
#error "Limit of maximum number of IO classes exceeded"
#endif

/** Enabling debug statistics */
#ifndef OCF_CONFIG_DEBUG_STATS
#define OCF_CONFIG_DEBUG_STATS 0
#endif

#endif /* __OCF_CFG_H__ */
