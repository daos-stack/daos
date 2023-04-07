/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __BIO_UT_H__
#define __BIO_UT_H__

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos/sys_db.h>
#include <daos_srv/bio.h>

#if FAULT_INJECTION
#define FAULT_INJECTION_REQUIRED() do { } while (0)
#else
#define FAULT_INJECTION_REQUIRED() \
	do { \
		print_message("Fault injection required for test, skipping...\n"); \
		skip();\
	} while (0)
#endif /* FAULT_INJECTION */

struct bio_ut_args {
	struct bio_xs_context	*bua_xs_ctxt;
	struct bio_meta_context	*bua_mc;
	uuid_t			 bua_pool_id;
	unsigned int		 bua_seed;
};

/* bio_ut.c */
extern struct bio_ut_args	ut_args;
void ut_fini(struct bio_ut_args *args);
int ut_init(struct bio_ut_args *args);

/* wal_ut.c */
int run_wal_tests(void);

#endif /* __BIO_UT_H__ */
