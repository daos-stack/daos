/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos
 *
 * vos/tests/vts_common.h
 */
#ifndef __VTS_COMMON_H__
#define __VTS_COMMON_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos/common.h>
#include <daos/object.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

#if FAULT_INJECTION
#define FAULT_INJECTION_REQUIRED() do { } while (0)
#else
#define FAULT_INJECTION_REQUIRED() \
	do { \
		print_message("Fault injection required for test, skipping...\n"); \
		skip();\
	} while (0)
#endif /* FAULT_INJECTION */

#define VPOOL_256M	(256ULL << 20)
#define VPOOL_1G	(1ULL << 30)
#define VPOOL_2G	(2ULL << 30)
#define VPOOL_3G	(3ULL << 30)

#define VPOOL_SIZE	VPOOL_3G

#define	VP_OPS 10

#define STORAGE_PATH_LEN 96
extern char	vos_path[STORAGE_PATH_LEN+1];
extern int	gc;
extern bool	g_force_checksum;
extern bool	g_force_no_zero_copy;

/**
 * The last key stored in vos/tests/vts_io.c
 * It can be used for punching or overwrite
 */
extern char	last_dkey[];
extern char	last_akey[];

enum vts_ops_type {
	CREAT,
	CREAT_OPEN,
	OPEN,
	CLOSE,
	DESTROY,
	QUERY
};

struct vos_test_ctx {
	char			*tc_po_name;
	uuid_t			tc_po_uuid;
	uuid_t			tc_co_uuid;
	daos_handle_t		tc_po_hdl;
	daos_handle_t		tc_co_hdl;
	int			tc_step;
};


/**
 * Internal test  functions
 */
bool
vts_file_exists(const char *filename);

int
vts_alloc_gen_fname(char **fname);

int
vts_pool_fallocate(char **fname);

/**
 * Init and Fini context, Sets up
 * test context for I/O tests
 */
int
vts_ctx_init(struct vos_test_ctx *tcx,
	     size_t pool_size);

void
vts_ctx_fini(struct vos_test_ctx *tcx);

/**
 * Initialize I/O test context:
 * - create and connect to pool based on the input pool uuid and size
 * - create and open container based on the input container uuid
 */
int dts_ctx_init(struct credit_context *tsc);
/**
 * Finalize I/O test context:
 * - close and destroy the test container
 * - disconnect and destroy the test pool
 */
void dts_ctx_fini(struct credit_context *tsc);

/**
 * Try to obtain a free credit from the I/O context.
 */
struct io_credit *dts_credit_take(struct credit_context *tsc);

/** return the IO credit to the I/O context */
void dts_credit_return(struct credit_context *tsc, struct io_credit *cred);

/**
 * VOS test suite run tests
 */
int run_pool_test(const char *cfg);
int run_co_test(const char *cfg);
int run_discard_tests(const char *cfg);
int run_aggregate_tests(bool slow, const char *cfg);
int run_dtx_tests(const char *cfg);
int run_gc_tests(const char *cfg);
int run_pm_tests(const char *cfg);
int
    run_io_test(int *types, int num_types, int keys, const char *cfg);
int run_ts_tests(const char *cfg);

int run_ilog_tests(const char *cfg);
int run_csum_extent_tests(const char *cfg);
int run_mvcc_tests(const char *cfg);
int run_wal_tests(const char *cfg);
int
run_vos_command(const char *arg0, const char *cmd);

void
vts_dtx_begin(const daos_unit_oid_t *oid, daos_handle_t coh, daos_epoch_t epoch,
	      uint64_t dkey_hash, struct dtx_handle **dthp);
static inline void
vts_dtx_begin_ex(const daos_unit_oid_t *oid, daos_handle_t coh,
		 daos_epoch_t epoch, daos_epoch_t epoch_bound,
		 uint64_t dkey_hash, uint32_t nmods, struct dtx_handle **dthp)
{
	struct dtx_handle	*dth;

	vts_dtx_begin(oid, coh, epoch, dkey_hash, dthp);

	dth = *dthp;

	if (epoch_bound <= epoch)
		dth->dth_epoch_bound = epoch;
	else
		dth->dth_epoch_bound = epoch_bound;

	dth->dth_modification_cnt = nmods;

	/** first call in vts_dtx_begin will have set this to inline */
	assert_rc_equal(vos_dtx_rsrvd_init(dth), 0);
}

void
vts_dtx_end(struct dtx_handle *dth);

#endif
