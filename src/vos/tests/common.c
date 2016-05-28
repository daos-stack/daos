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
 * Test for KV object creation and destroy.
 * vos/tests/common.c
 */

#include <stdio.h>
#include <fcntl.h>
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
#include <errno.h>
#include <vos_internal.h>
#include "common.h"

#define VOS_ENV_POOL	"VOS_POOL"
#define VOS_POOL	"/tmp/vos_pool"

enum {
	TCX_NONE,
	TCX_VOS_INIT,
	TCX_PO_CREATE,
	TCX_PO_OPEN,
	TCX_CO_CREATE,
	TCX_CO_OPEN,
	TCX_READY,
};

bool
vts_file_exists(const char *fname)
{
	struct stat sbuf;
	int	    rc;

	rc = stat(fname, &sbuf);
	return rc == 0 ? true : false;
}

int
vts_ctx_init(struct vos_test_ctx *tcx)
{
	int	 rc;

	memset(tcx, 0, sizeof(*tcx));

	tcx->tc_po_name = getenv(VOS_ENV_POOL);
	if (tcx->tc_po_name == NULL)
		tcx->tc_po_name = VOS_POOL;

	if (vts_file_exists(tcx->tc_po_name)) {
		rc = remove(tcx->tc_po_name);
		if (rc != 0) {
			D_ERROR("can't remove stale pool file: %d\n", errno);
			return rc;
		}
	}

	rc = vos_init();
	if (rc) {
		D_ERROR("VOS init error: %d\n", rc);
		goto failed;
	}
	tcx->tc_step = TCX_VOS_INIT;

	uuid_generate_time_safe(tcx->tc_po_uuid);
	uuid_generate_time_safe(tcx->tc_co_uuid);

	rc = vos_pool_create(tcx->tc_po_name, tcx->tc_po_uuid,
			     PMEMOBJ_MIN_POOL, &tcx->tc_po_hdl, NULL);
	if (rc) {
		D_ERROR("vpool create failed with error : %d", rc);
		goto failed;
	}
	D_PRINT("Success creating pool at %s\n", tcx->tc_po_name);
	tcx->tc_step = TCX_PO_CREATE;

	rc = vos_pool_close(tcx->tc_po_hdl, NULL);
	D_ASSERT(rc == 0);
	rc = vos_pool_open(tcx->tc_po_name, tcx->tc_po_uuid, &tcx->tc_po_hdl,
			   NULL);
	if (rc) {
		D_ERROR("vos pool open error: %d\n", rc);
		goto failed;
	}
	D_PRINT("Success openning pool at %s\n", tcx->tc_po_name);
	tcx->tc_step = TCX_PO_OPEN;

	rc = vos_co_create(tcx->tc_po_hdl, tcx->tc_co_uuid, NULL);
	if (rc) {
		D_ERROR("vos container creation error: %d\n", rc);
		goto failed;
	}
	D_PRINT("Success creating container in the pool\n");
	tcx->tc_step = TCX_CO_CREATE;

	rc = vos_co_open(tcx->tc_po_hdl, tcx->tc_co_uuid,
			 &tcx->tc_co_hdl, NULL);
	if (rc) {
		D_ERROR("vos container open error: %d\n", rc);
		goto failed;
	}
	D_PRINT("Success openning container\n");
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
		rc = vos_co_close(tcx->tc_co_hdl, NULL);
		if (rc != 0)
			D_ERROR("Container close failed: %d\n", rc);
		else
			D_PRINT("Success closing container\n");

		/* fallthrough */
	case TCX_CO_CREATE:
		rc = vos_co_destroy(tcx->tc_po_hdl, tcx->tc_co_uuid, NULL);
		if (rc != 0)
			D_ERROR("Container destroy failed: %d\n", rc);
		else
			D_PRINT("Success destroy container\n");

		/* fallthrough */
	case TCX_PO_OPEN:
	case TCX_PO_CREATE:
		rc = vos_pool_destroy(tcx->tc_po_hdl, NULL);
		if (rc != 0)
			D_ERROR("Pool destroy failed: %d\n", rc);
		else
			D_PRINT("Success destroying pool at %s\n",
				tcx->tc_po_name);

		/* fallthrough */
	case TCX_VOS_INIT:
		vos_fini();
	}
	memset(tcx, 0, sizeof(*tcx));
}
