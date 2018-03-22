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
#include <gurt/dlog.h>

/** predefined debug facilities (subsystems/modules) */
extern unsigned int dd_fac_null;
extern unsigned int dd_fac_misc;
extern unsigned int dd_fac_common;
extern unsigned int dd_fac_tree;
extern unsigned int dd_fac_vos;
extern unsigned int dd_fac_client;
extern unsigned int dd_fac_server;
extern unsigned int dd_fac_rdb;
extern unsigned int dd_fac_pool;
extern unsigned int dd_fac_container;
extern unsigned int dd_fac_object;
extern unsigned int dd_fac_placement;
extern unsigned int dd_fac_rebuild;
extern unsigned int dd_fac_tier;
extern unsigned int dd_fac_mgmt;
extern unsigned int dd_fac_utils;
extern unsigned int dd_fac_tests;

/** other debug tunables */
#define DD_TUNE_ALLOC		"DD_ALLOC"
extern bool dd_tune_alloc;

/* DAOS-specific debug bits OPT1-10 available */
#define DB_MD		DB_OPT1	/* metadata operation */
#define	DB_PL		DB_OPT2	/* placement */
#define DB_MGMT		DB_OPT3	/* pool management */
#define DB_EPC		DB_OPT4	/* epoch system */
#define DB_DF		DB_OPT5	/* durable format */
#define DB_REBUILD	DB_OPT6	/* rebuild process */

#define DB_DEFAULT	(DB_IO | DB_MD | DB_PL | DB_REBUILD)
#define DB_NULL		0
/** XXX Temporary things, should be replaced by debug bits above */
#define DF_DSMC		DB_ANY
#define DF_DSMS		DB_ANY
#define DF_TIER		DB_ANY
#define DF_TIERC	DB_ANY
#define DF_TIERS	DB_ANY
#define DF_MISC		DB_ANY

#define DDFAC(name)	dd_fac_##name

#define D__ASSERTF(cond, fmt, ...)					\
do {									\
	if (!(cond)) {							\
		D_CRIT(fmt, ## __VA_ARGS__);				\
		fflush(stderr);						\
	}								\
	assert(cond);							\
} while (0)

#define D__ASSERT(cond)		D__ASSERTF(cond, "assertion failure\n")

#define D_CASSERT(cond)                                                 \
do {switch (1) {case (cond): case 0: break; } } while (0)

#define DAOS_API_ARG_ASSERT(args, name)					\
do {									\
	int __opc = DAOS_OPC_##name;					\
	D__ASSERTF(sizeof(args) == dc_funcs[__opc].arg_size,		\
		  "Argument size %zu != predefiened arg size %zu\n",	\
		  sizeof(args), dc_funcs[__opc].arg_size);		\
} while (0)

#define D__GOTO(label, rc)						\
do {									\
	typeof(rc) __rc = (rc);						\
	D_DEBUG(DB_TRACE, "goto %s: %ld\n", #label, (long)__rc);	\
	goto label;							\
} while (0)

#define D__RETURN(rc)							\
do {									\
	typeof(rc) __rc = (rc);						\
	D_DEBUG(DB_TRACE, "return: %ld\n", (long)__rc);			\
	return __rc;							\
} while (0)

#define D__PRINT(fmt, ...)                                               \
do {                                                                    \
	fprintf(stdout, fmt, ## __VA_ARGS__);				\
	fflush(stdout);							\
} while (0)

/** initialize the debug system */
int  daos_debug_init(char *logfile);
/** finalize the debug system */
void daos_debug_fini(void);

#endif /* __DAOS_DEBUG_H__ */
