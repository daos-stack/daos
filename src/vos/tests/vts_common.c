/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * vos/tests/vts_common.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <daos_srv/vos.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <errno.h>
#include <vos_internal.h>
#include "vts_common.h"
#include <cmocka.h>

enum {
	TCX_NONE,
	TCX_PO_CREATE,
	TCX_PO_OPEN,
	TCX_CO_CREATE,
	TCX_CO_OPEN,
	TCX_READY,
};

int gc, oid_cnt;

bool
vts_file_exists(const char *filename)
{
	if (access(filename, F_OK) != -1)
		return true;
	else
		return false;
}

int
vts_alloc_gen_fname(char **fname)
{
	char *file_name = NULL;
	int n;

	file_name = malloc(25);
	if (!file_name)
		return -ENOMEM;
	n = snprintf(file_name, 25, VPOOL_NAME);
	snprintf(file_name+n, 25-n, ".%d", gc++);
	*fname = file_name;

	return 0;
}

int
vts_pool_fallocate(char **fname)
{
	int ret = 0, fd;

	ret = vts_alloc_gen_fname(fname);
	if (ret)
		return ret;

	fd = open(*fname, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (fd < 0) {
		ret = -ENOMEM;
		goto exit;
	}
	ret = fallocate(fd, 0, 0, VPOOL_16M);
exit:
	return ret;
}

int
vts_ctx_init(struct vos_test_ctx *tcx, size_t psize)
{
	int	 rc;

	memset(tcx, 0, sizeof(*tcx));
	oid_cnt = 0;
	rc = vts_alloc_gen_fname(&tcx->tc_po_name);
	assert_int_equal(rc, 0);

	if (vts_file_exists(tcx->tc_po_name)) {
		rc = remove(tcx->tc_po_name);
		assert_int_equal(rc, 0);
	}

	uuid_generate_time_safe(tcx->tc_po_uuid);
	uuid_generate_time_safe(tcx->tc_co_uuid);

	/* specify @psize as both NVMe size and SCM size */
	rc = vos_pool_create(tcx->tc_po_name, tcx->tc_po_uuid, psize, psize);
	if (rc) {
		print_error("vpool create %s failed with error : %d\n",
			    tcx->tc_po_name, rc);
		goto failed;
	}
	tcx->tc_step = TCX_PO_CREATE;

	rc = vos_pool_open(tcx->tc_po_name, tcx->tc_po_uuid, &tcx->tc_po_hdl);
	if (rc) {
		print_error("vos pool open %s error: %d\n",
			    tcx->tc_po_name, rc);
		goto failed;
	}
	tcx->tc_step = TCX_PO_OPEN;

	rc = vos_cont_create(tcx->tc_po_hdl, tcx->tc_co_uuid);
	if (rc) {
		print_error("vos container creation error: %d\n", rc);
		goto failed;
	}
	tcx->tc_step = TCX_CO_CREATE;

	rc = vos_cont_open(tcx->tc_po_hdl, tcx->tc_co_uuid,
			   &tcx->tc_co_hdl);
	if (rc) {
		print_error("vos container open error: %d\n", rc);
		goto failed;
	}
	tcx->tc_step = TCX_CO_OPEN;
	tcx->tc_step = TCX_READY;
	return 0;

 failed:
	vts_ctx_fini(tcx);
	return rc;
}

void
vts_ctx_fini(struct vos_test_ctx *tcx)
{
	int	rc;

	switch (tcx->tc_step) {
	default:
	case TCX_NONE:
		break;

	case TCX_READY:
	case TCX_CO_OPEN:
		rc = vos_cont_close(tcx->tc_co_hdl);
		assert_int_equal(rc, 0);
		/* fallthrough */
	case TCX_CO_CREATE:
		rc = vos_cont_destroy(tcx->tc_po_hdl, tcx->tc_co_uuid);
		assert_int_equal(rc, 0);
		/* fallthrough */
	case TCX_PO_OPEN:
		rc = vos_pool_close(tcx->tc_po_hdl);
		assert_int_equal(rc, 0);
	case TCX_PO_CREATE:
		rc = vos_pool_destroy(tcx->tc_po_name, tcx->tc_po_uuid);
		assert_int_equal(rc, 0);
		/* fallthrough */
	}
	memset(tcx, 0, sizeof(*tcx));
}
