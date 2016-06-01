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

#define VPOOL_SIZE 16777216ULL
#define VPOOL_NAME "/mnt/daos/vpool"
#define	VP_OPS 10

extern int gc;

enum ops_type {
	CREAT,
	OPEN,
	CLOSE,
	DESTROY,
	QUERY
};

/**
 * Internal test  functions
 */
bool
file_exists(const char *filename);

int
alloc_gen_fname(char **fname);

int
pool_fallocate(char **fname);

void
io_set_oid(daos_unit_oid_t *oid);

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
