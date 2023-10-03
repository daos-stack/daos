/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <linux/limits.h>

enum {
	TCX_NONE,
	TCX_PO_CREATE_OPEN,
	TCX_CO_CREATE,
	TCX_CO_OPEN,
	TCX_READY,
};

int	gc, oid_cnt;
char	vos_path[STORAGE_PATH_LEN+1];

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
	int rc;

	rc = asprintf(fname, "%s/vpool.%d", vos_path, gc++);
	if (rc < 0) {
		*fname = NULL;
		print_error("Failed to allocate memory for fname: rc = %d\n", rc);
		return rc;
	}

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
		free(*fname);
		*fname = NULL;
		goto exit;
	}
	ret = fallocate(fd, 0, 0, VPOOL_256M);

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
	rc = vos_pool_create(tcx->tc_po_name, tcx->tc_po_uuid, psize, psize, 0,
			     &tcx->tc_po_hdl);
	if (rc) {
		print_error("vpool create %s failed with error : %d\n",
			    tcx->tc_po_name, rc);
		goto failed;
	}
	tcx->tc_step = TCX_PO_CREATE_OPEN;

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
		assert_rc_equal(rc, 0);
		/* fallthrough */
	case TCX_CO_CREATE:
		rc = vos_cont_destroy(tcx->tc_po_hdl, tcx->tc_co_uuid);
		assert_rc_equal(rc, 0);
		/* fallthrough */
	case TCX_PO_CREATE_OPEN:
		rc = vos_pool_close(tcx->tc_po_hdl);
		assert_rc_equal(rc, 0);
		rc = vos_pool_destroy(tcx->tc_po_name, tcx->tc_po_uuid);
		assert_rc_equal(rc, 0);
		free(tcx->tc_po_name);
		/* fallthrough */
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
struct io_credit *
dts_credit_take(struct credit_context *tsc)
{
	int	i;

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		if (tsc->tsc_credits[i] == NULL) {
			D_ASSERT(tsc->tsc_cred_avail > 0);
			tsc->tsc_cred_avail--;
			tsc->tsc_credits[i] = &tsc->tsc_cred_buf[i];
			return tsc->tsc_credits[i];
		}
	}
	D_ASSERT(tsc->tsc_cred_avail == 0);
	return NULL;
}

void
dts_credit_return(struct credit_context *tsc, struct io_credit *cred)
{
	int	i;

	D_ASSERT(tsc->tsc_cred_avail < tsc->tsc_cred_nr);

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		if (tsc->tsc_credits[i] == cred) {
			tsc->tsc_credits[i] = NULL;
			tsc->tsc_cred_avail++;
			return;
		}
	}
	D_ASSERT(0);
}

static int
vts_credits_init(struct credit_context *tsc)
{
	int	i;

	tsc->tsc_eqh		= DAOS_HDL_INVAL;
	tsc->tsc_cred_avail	= tsc->tsc_cred_nr;

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		struct io_credit *cred = &tsc->tsc_cred_buf[i];

		memset(cred, 0, sizeof(*cred));
		D_ALLOC(cred->tc_vbuf, tsc->tsc_cred_vsize);
		if (!cred->tc_vbuf) {
			fprintf(stderr, "Cannot allocate buffer size=%d\n", tsc->tsc_cred_vsize);
			return -1;
		}
	}
	return 0;
}

static void
vts_credits_fini(struct credit_context *tsc)
{
	int	i;

	D_ASSERT(!tsc->tsc_cred_inuse);

	for (i = 0; i < tsc->tsc_cred_nr; i++)
		D_FREE(tsc->tsc_cred_buf[i].tc_vbuf);
}

static int
pool_init(struct credit_context *tsc)
{
	char		*pmem_file = tsc->tsc_pmem_file;
	daos_handle_t	 poh = DAOS_HDL_INVAL;
	int		 fd;
	int		 rc;

	if (tsc->tsc_scm_size == 0)
		tsc->tsc_scm_size = (1ULL << 30);

	D_ASSERT(!daos_file_is_dax(pmem_file));
	rc = open(pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (rc < 0)
		goto out;

	fd = rc;
	rc = fallocate(fd, 0, 0, tsc->tsc_scm_size);
	if (rc)
		goto out;

	/* Use pool size as blob size for this moment. */
	if (tsc_create_pool(tsc)) {
		rc = vos_pool_create(pmem_file, tsc->tsc_pool_uuid, 0,
				     tsc->tsc_nvme_size, 0, &poh);
		if (rc)
			goto out;
	} else {
		rc = vos_pool_open(pmem_file, tsc->tsc_pool_uuid, 0, &poh);
		if (rc)
			goto out;
	}

	tsc->tsc_poh = poh;
 out:
	return rc;
}

static void
pool_fini(struct credit_context *tsc)
{
	int	rc;

	vos_pool_close(tsc->tsc_poh);
	if (tsc_create_pool(tsc)) {
		rc = vos_pool_destroy(tsc->tsc_pmem_file, tsc->tsc_pool_uuid);
		D_ASSERTF(rc == 0 || rc == -DER_NONEXIST, "rc="DF_RC"\n",
			  DP_RC(rc));
	}
}

static int
cont_init(struct credit_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;

	if (tsc_create_cont(tsc)) {
		rc = vos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid);
		if (rc)
			goto out;
	}

	rc = vos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid, &coh);
	if (rc)
		goto out;

	tsc->tsc_coh = coh;
 out:
	return rc;
}

static void
cont_fini(struct credit_context *tsc)
{
	if (tsc->tsc_pmem_file) /* VOS mode */
		vos_cont_close(tsc->tsc_coh);
}

/* see comments in dts_common.h */
int
dts_ctx_init(struct credit_context *tsc)
{
	int	rc;

	tsc->tsc_init = DTS_INIT_NONE;
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_DEBUG;

	rc = vos_self_init(vos_path, false, BIO_STANDALONE_TGT_ID);
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
	rc = vts_credits_init(tsc);
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
dts_ctx_fini(struct credit_context *tsc)
{
	switch (tsc->tsc_init) {
	case DTS_INIT_CREDITS:	/* finalize credits */
		vts_credits_fini(tsc);
		/* fall through */
	case DTS_INIT_CONT:	/* close and destroy container */
		cont_fini(tsc);
		/* fall through */
	case DTS_INIT_POOL:	/* close and destroy pool */
		pool_fini(tsc);
		/* fall through */
	case DTS_INIT_MODULE:	/* finalize module */
		vos_self_fini();
		/* fall through */
	case DTS_INIT_DEBUG:	/* finalize debug system */
		daos_debug_fini();
	}
}
