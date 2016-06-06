/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
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

#define VPOOL_16M 16777216ULL
#define VPOOL_1G  1073741824ULL
#define VPOOL_10G 10737418240ULL
#define VPOOL_NAME "/mnt/daos/vpool"
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
	char		*tc_po_name;
	uuid_t		 tc_po_uuid;
	uuid_t		 tc_co_uuid;
	daos_handle_t	 tc_po_hdl;
	daos_handle_t	 tc_co_hdl;
	int		 tc_step;
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

void
vts_io_set_oid(daos_unit_oid_t *oid);

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
 * VOS test suite run tests
 */
int
run_pool_test(void);

int
run_co_test(void);

int
run_chtable_tests(void);

int
run_io_test(void);


#endif
