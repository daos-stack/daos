/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

	rc = vos_pool_open(tcx->tc_po_name, tcx->tc_po_uuid, false,
			   &tcx->tc_po_hdl);
	if (rc) {
		print_error("vos pool open %s "DF_UUIDF" error: %d\n",
			    tcx->tc_po_name, DP_UUID(tcx->tc_po_uuid), rc);
		goto failed;
	}
	tcx->tc_step = TCX_PO_OPEN;

	rc = vos_cont_create(tcx->tc_po_hdl, tcx->tc_co_uuid);
	if (rc) {
		print_error("vos container creation error: "DF_RC"\n",
			    DP_RC(rc));
		goto failed;
	}
	tcx->tc_step = TCX_CO_CREATE;

	rc = vos_cont_open(tcx->tc_po_hdl, tcx->tc_co_uuid,
			   &tcx->tc_co_hdl);
	if (rc) {
		print_error("vos container open error: "DF_RC"\n", DP_RC(rc));
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
		free(tcx->tc_po_name);
	}
	memset(tcx, 0, sizeof(*tcx));
}

enum {
	DTS_INIT_NONE,		/* nothing has been initialized */
	DTS_INIT_DEBUG,		/* debug system has been initialized */
	DTS_INIT_MODULE,	/* modules have been loaded */
	DTS_INIT_POOL,		/* pool has been created */
	DTS_INIT_CONT,		/* container has been created */
	DTS_INIT_CREDITS,	/* I/O credits have been initialized */
};

/** try to obtain a free credit */
struct dts_io_credit *
dts_credit_take(struct dts_context *tsc)
{
	return &tsc->tsc_cred_buf[0];
}

static int
credits_init(struct dts_context *tsc)
{
	int	i;

	tsc->tsc_eqh		= DAOS_HDL_INVAL;
	tsc->tsc_cred_nr	= 1;  /* take one slot in the buffer */
	tsc->tsc_cred_avail	= -1; /* always available */

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		struct dts_io_credit *cred = &tsc->tsc_cred_buf[i];

		memset(cred, 0, sizeof(*cred));
		D_ALLOC(cred->tc_vbuf, tsc->tsc_cred_vsize);
		if (!cred->tc_vbuf) {
			fprintf(stderr, "Cannt allocate buffer size=%d\n",
				tsc->tsc_cred_vsize);
			return -1;
		}
		tsc->tsc_credits[i] = cred;
	}
	return 0;
}

static void
credits_fini(struct dts_context *tsc)
{
	int	i;

	D_ASSERT(!tsc->tsc_cred_inuse);

	for (i = 0; i < tsc->tsc_cred_nr; i++)
		D_FREE(tsc->tsc_cred_buf[i].tc_vbuf);
}

static int
pool_init(struct dts_context *tsc)
{
	char		*pmem_file = tsc->tsc_pmem_file;
	daos_handle_t	 poh = DAOS_HDL_INVAL;
	int		 fd;
	int		 rc;

	if (tsc->tsc_scm_size == 0)
		tsc->tsc_scm_size = (1ULL << 30);

	if (!daos_file_is_dax(pmem_file)) {
		rc = open(pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (rc < 0)
			goto out;

		fd = rc;
		rc = fallocate(fd, 0, 0, tsc->tsc_scm_size);
		if (rc)
			goto out;
	}

	/* Use pool size as blob size for this moment. */
	rc = vos_pool_create(pmem_file, tsc->tsc_pool_uuid, 0,
			     tsc->tsc_nvme_size);
	if (rc)
		goto out;

	rc = vos_pool_open(pmem_file, tsc->tsc_pool_uuid, false, &poh);
	if (rc)
		goto out;

	tsc->tsc_poh = poh;
 out:
	return rc;
}

static void
pool_fini(struct dts_context *tsc)
{
	int	rc;

	vos_pool_close(tsc->tsc_poh);
	rc = vos_pool_destroy(tsc->tsc_pmem_file, tsc->tsc_pool_uuid);
	D_ASSERTF(rc == 0 || rc == -DER_NONEXIST, "rc="DF_RC"\n", DP_RC(rc));
}

static int
cont_init(struct dts_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;

	rc = vos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid);
	if (rc)
		goto out;

	rc = vos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid, &coh);
	if (rc)
		goto out;

	tsc->tsc_coh = coh;
 out:
	return rc;
}

static void
cont_fini(struct dts_context *tsc)
{
	if (tsc->tsc_pmem_file) /* VOS mode */
		vos_cont_close(tsc->tsc_coh);
}

/* see comments in dts_common.h */
int
dts_ctx_init(struct dts_context *tsc)
{
	int	rc;

	tsc->tsc_init = DTS_INIT_NONE;
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_DEBUG;

	rc = vos_init();
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_MODULE;

	rc = pool_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_POOL;

	rc = cont_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_CONT;

	/* initialize I/O credits, which include EQ, event, I/O buffers... */
	rc = credits_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_CREDITS;

	return 0;
 out:
	fprintf(stderr, "Failed to initialize step=%d, rc=%d\n",
		tsc->tsc_init, rc);
	dts_ctx_fini(tsc);
	return rc;
}

/* see comments in dts_common.h */
void
dts_ctx_fini(struct dts_context *tsc)
{
	switch (tsc->tsc_init) {
	case DTS_INIT_CREDITS:	/* finalize credits */
		credits_fini(tsc);
		/* fall through */
	case DTS_INIT_CONT:	/* close and destroy container */
		cont_fini(tsc);
		/* fall through */
	case DTS_INIT_POOL:	/* close and destroy pool */
		pool_fini(tsc);
		/* fall through */
	case DTS_INIT_MODULE:	/* finalize module */
		vos_fini();
		/* fall through */
	case DTS_INIT_DEBUG:	/* finalize debug system */
		daos_debug_fini();
	}
}
