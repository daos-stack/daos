/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include <daos/common.h>
#include <daos/object.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

#define VPOOL_16M	(16ULL << 20)
#define VPOOL_1G	(1ULL << 30)
#define VPOOL_2G	(2ULL << 30)
#define VPOOL_10G	(10ULL << 30)

#define VPOOL_SIZE	VPOOL_2G

#define VPOOL_NAME	"/mnt/daos/vpool"
#define	VP_OPS 10

extern int gc;

enum vts_ops_type {
	CREAT,
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
int dts_ctx_init(struct dts_context *tsc);
/**
 * Finalize I/O test context:
 * - close and destroy the test container
 * - disconnect and destroy the test pool
 */
void dts_ctx_fini(struct dts_context *tsc);

/**
 * Try to obtain a free credit from the I/O context.
 */
struct dts_io_credit *dts_credit_take(struct dts_context *tsc);

/**
 * VOS test suite run tests
 */
int run_pool_test(void);
int run_co_test(void);
int run_discard_tests(void);
int run_aggregate_tests(bool slow);
int run_dtx_tests(void);
int run_gc_tests(void);
int run_pm_tests(void);
int run_io_test(daos_ofeat_t feats, int keys, bool nest_iterators);

int run_ilog_tests(void);

int run_csum_extent_tests(void);

#endif
