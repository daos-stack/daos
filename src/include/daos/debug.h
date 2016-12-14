/**
 * (C) Copyright 2015, 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __DAOS_DEBUG_H__
#define __DAOS_DEBUG_H__

#include <stdio.h>
#include <assert.h>
#include <crt_util/clog.h>

/** priority level */
#define DP_INFO			CLOG_INFO
#define DP_NOTE			CLOG_NOTE
#define DP_WARN			CLOG_WARN
#define DP_ERR			CLOG_ERR
#define DP_CRIT			CLOG_CRIT
#define DP_FATAL		CLOG_EMERG

/* default debug log file */
#define DD_LOG_ENV		"DD_LOG"
#define DD_LOG_DEFAULT		"/tmp/daos.log"

#define	DD_SEP			','
/* The environment variable for enabled debug facilities (subsystems) */
#define DD_FAC_ENV		"DD_SUBSYS"
#define DD_FAC_DEFAULT		"all"

/* The environment variable for the default debug bit-mask */
#define DD_MASK_ENV		"DD_MASK"
#define DD_MASK_DEFAULT		"all"

/* The environment variable for setting debug level being output to stderr.
 * Options: "info", "note", "warn", "err", "crit", "emerg".
 * Default: "crit", which is used by D_FATAL, D_ASSERT and D_ASSERTF
 */
#define DD_STDERR_ENV		"DD_STDERR"

/** predefined debug facilities (subsystems/modules) */
extern unsigned int dd_fac_null;
extern unsigned int dd_fac_misc;
extern unsigned int dd_fac_common;
extern unsigned int dd_fac_tree;
extern unsigned int dd_fac_vos;
extern unsigned int dd_fac_client;
extern unsigned int dd_fac_server;
extern unsigned int dd_fac_pool;
extern unsigned int dd_fac_container;
extern unsigned int dd_fac_object;
extern unsigned int dd_fac_placement;
extern unsigned int dd_fac_rebuild;
extern unsigned int dd_fac_tier;
extern unsigned int dd_fac_mgmt;
extern unsigned int dd_fac_utils;
extern unsigned int dd_fac_tests;

/**
 * Debug bits for logic path, now we can only have up to 16 different bits,
 * needs to change CaRT and make it support more bits.
 */
#define DB_ANY		(1 << (CLOG_DPRISHIFT + 0)) /* wildcard for anything */
#define DB_MEM		(1 << (CLOG_DPRISHIFT + 1)) /* memory operation */
#define DB_NET		(1 << (CLOG_DPRISHIFT + 2)) /* network operation */
#define DB_IO		(1 << (CLOG_DPRISHIFT + 3)) /* object I/O */
#define DB_MD		(1 << (CLOG_DPRISHIFT + 4)) /* metadata operation */
#define DB_PL		(1 << (CLOG_DPRISHIFT + 5)) /* placement */
#define DB_MGMT		(1 << (CLOG_DPRISHIFT + 6)) /* management */
#define DB_EPC		(1 << (CLOG_DPRISHIFT + 7)) /* epoch etc */

/* should be replaced by more reasonable mask, e.g. (DB_IO | DB_MD | DB_PL) */
#define DB_DEFAULT	DB_ANY
#define DB_NULL		0

/** XXX Temporary things, should be replaced by debug bits above */
#define DF_MEM		DB_MEM
#define DF_CL		DB_ANY
#define DF_CL2		DB_ANY
#define DF_CL3		DB_ANY
#define DF_PL		DB_ANY
#define DF_PL2		DB_ANY
#define DF_PL3		DB_ANY
#define DF_TP		DB_ANY
#define DF_VOS1		DB_ANY
#define DF_VOS2		DB_ANY
#define DF_VOS3		DB_ANY
#define DF_SERVER	DB_ANY
#define DF_MGMT		DB_ANY
#define DF_DSMC		DB_ANY
#define DF_DSMS		DB_ANY
#define DF_SR		DB_ANY
#define DF_SRC		DB_ANY
#define DF_TIER		DB_ANY
#define DF_TIERC	DB_ANY
#define DF_TIERS	DB_ANY
#define DF_MISC		DB_ANY

#define DD_FAC(name)	dd_fac_##name

#define D_DEBUG(mask, fmt, ...)						\
do {									\
	if (((mask) < DP_INFO) && DD_SUBSYS == dd_fac_null)		\
		break;							\
	crt_log((mask) | DD_SUBSYS,					\
		"%s:%d %s() " fmt, __FILE__, __LINE__, __func__,	\
		##__VA_ARGS__);						\
} while (0)

/** macros to output logs which are more important than D_DEBUG */
#define D_INFO(fmt, ...)	D_DEBUG(DP_INFO, fmt, ## __VA_ARGS__)
#define D_NOTE(fmt, ...)	D_DEBUG(DP_NOTE, fmt, ## __VA_ARGS__)
#define D_WARN(fmt, ...)	D_DEBUG(DP_WARN, fmt, ## __VA_ARGS__)
#define D_ERROR(fmt, ...)	D_DEBUG(DP_ERR, fmt, ## __VA_ARGS__)
#define D_CRIT(fmt, ...)	D_DEBUG(DP_CRIT, fmt, ## __VA_ARGS__)
#define D_FATAL(fmt, ...)	D_DEBUG(DP_FATAL, fmt, ## __VA_ARGS__)

#define D_ASSERTF(cond, fmt, ...)					\
do {									\
	if (!(cond)) {							\
		D_CRIT(fmt, ## __VA_ARGS__);				\
		fflush(stderr);						\
	}								\
	assert(cond);							\
} while (0)

#define D_ASSERT(cond)		D_ASSERTF(cond, "assertion failure\n")

#define D_CASSERT(cond)                                                 \
do {switch (1) {case (cond): case 0: break; } } while (0)

#define D_PRINT(fmt, ...)                                               \
do {                                                                    \
	fprintf(stdout, fmt, ## __VA_ARGS__);				\
	fflush(stdout);							\
} while (0)

/** initialize the debug system */
int  daos_debug_init(char *logfile);
/** finalize the debug system */
void daos_debug_fini(void);

#endif /* __DAOS_DEBUG_H__ */
