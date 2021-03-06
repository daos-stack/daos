/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

/**
 * Test suite helper functions.
 */
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
#include <mpi.h>
#include <daos/common.h>
#include <daos/credit.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include <daos/dts.h>

/* path to dmg config file */
const char *dmg_config_file;

enum {
	DTS_INIT_NONE,		/* nothing has been initialized */
	DTS_INIT_DEBUG,		/* debug system has been initialized */
	DTS_INIT_MODULE,	/* modules have been loaded */
	DTS_INIT_POOL,		/* pool has been created */
	DTS_INIT_CONT,		/* container has been created */
	DTS_INIT_CREDITS,	/* I/O credits have been initialized */
};

static int
pool_init(struct credit_context *tsc)
{
	daos_handle_t	poh = DAOS_HDL_INVAL;
	int		rc;

	if (tsc->tsc_scm_size == 0)
		tsc->tsc_scm_size = (1ULL << 30);

	if (tsc->tsc_pmem_file) { /* VOS mode */
		char	*pmem_file = tsc->tsc_pmem_file;
		int	 fd;

		if (!daos_file_is_dax(pmem_file)) {
			rc = open(pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
			if (rc < 0)
				goto out;

			fd = rc;
			rc = fallocate(fd, 0, 0, tsc->tsc_scm_size);
			if (rc)
				goto out;
		}
		if (tsc_create_pool(tsc)) {
			/* Use pool size as blob size for this moment. */
			rc = vos_pool_create(pmem_file, tsc->tsc_pool_uuid, 0,
					     tsc->tsc_nvme_size, 0, &poh);
			if (rc)
				goto out;
		} else {
			rc = vos_pool_open(pmem_file, tsc->tsc_pool_uuid, 0,
					   &poh);
			if (rc)
				goto out;
		}

	} else if (tsc->tsc_mpi_rank == 0) { /* DAOS mode and rank zero */
		d_rank_list_t	*svc = &tsc->tsc_svc;

		if (tsc->tsc_dmg_conf)
			dmg_config_file = tsc->tsc_dmg_conf;

		if (tsc_create_pool(tsc)) {
			rc = dmg_pool_create(dmg_config_file, geteuid(),
					     getegid(), NULL, NULL,
					     tsc->tsc_scm_size,
					     tsc->tsc_nvme_size,
					     NULL, svc, tsc->tsc_pool_uuid);
			if (rc)
				goto bcast;
		}

		rc = daos_pool_connect(tsc->tsc_pool_uuid, NULL,
				       DAOS_PC_EX, &poh, NULL, NULL);
		if (rc)
			goto bcast;
	}
	tsc->tsc_poh = poh;
 bcast:
	if (tsc->tsc_mpi_size <= 1)
		goto out; /* don't need to share handle */

	if (!tsc->tsc_pmem_file)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		goto out; /* open failed */

	if (!tsc->tsc_pmem_file)
		handle_share(&tsc->tsc_poh, HANDLE_POOL, tsc->tsc_mpi_rank,
			     tsc->tsc_poh, 0);
 out:
	return rc;
}

static void
pool_fini(struct credit_context *tsc)
{
	int	rc;

	if (tsc->tsc_pmem_file) { /* VOS mode */
		vos_pool_close(tsc->tsc_poh);
		if (tsc_create_pool(tsc)) {
			rc = vos_pool_destroy(tsc->tsc_pmem_file,
					      tsc->tsc_pool_uuid);
			D_ASSERTF(rc == 0 || rc == -DER_NONEXIST,
				  "rc: "DF_RC"\n", DP_RC(rc));
		}
	} else { /* DAOS mode */
		rc = daos_pool_disconnect(tsc->tsc_poh, NULL);
		D_ASSERTF(rc == 0 || rc == -DER_NO_HDL, "rc="DF_RC"\n",
			  DP_RC(rc));
		MPI_Barrier(MPI_COMM_WORLD);
		if (tsc->tsc_mpi_rank == 0 && tsc_create_pool(tsc)) {
			rc = dmg_pool_destroy(dmg_config_file,
					      tsc->tsc_pool_uuid, NULL, true);
			D_ASSERTF(rc == 0 || rc == -DER_NONEXIST ||
				  rc == -DER_TIMEDOUT, "rc="DF_RC"\n",
				  DP_RC(rc));
		}
	}
}

static int
cont_init(struct credit_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;

	if (tsc->tsc_pmem_file) { /* VOS mode */
		if (tsc_create_cont(tsc)) {
			rc = vos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid);
			if (rc)
				goto out;
		}

		rc = vos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid, &coh);
		if (rc)
			goto out;

	} else if (tsc->tsc_mpi_rank == 0) { /* DAOS mode and rank zero */
		if (tsc_create_cont(tsc)) {
			rc = daos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid,
					      NULL, NULL);
			if (rc != 0)
				goto bcast;
		}
		rc = daos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid,
				    DAOS_COO_RW, &coh, NULL, NULL);
		if (rc != 0)
			goto bcast;
	}
	tsc->tsc_coh = coh;
 bcast:
	if (tsc->tsc_mpi_size <= 1)
		goto out; /* don't need to share handle */

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		goto out; /* open failed */

	if (!tsc->tsc_pmem_file)
		handle_share(&tsc->tsc_coh, HANDLE_CO, tsc->tsc_mpi_rank,
			     tsc->tsc_poh, 0);
 out:
	return rc;
}

static void
cont_fini(struct credit_context *tsc)
{
	if (tsc->tsc_pmem_file) /* VOS mode */
		vos_cont_close(tsc->tsc_coh);
	else /* DAOS mode */
		daos_cont_close(tsc->tsc_coh, NULL);

	/* NB: no container destroy at here, it will be destroyed by pool
	 * destroy later. This is because container destroy could be too
	 * expensive after performance tests.
	 */
}

bool
dts_is_async(struct credit_context *tsc)
{
	return daos_handle_is_valid(tsc->tsc_eqh);
}

/* see comments in daos/dts.h */
int
dts_ctx_init(struct credit_context *tsc)
{
	int	rc;

	tsc->tsc_init = DTS_INIT_NONE;
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_DEBUG;

	if (tsc->tsc_pmem_file) /* VOS mode */
		rc = vos_self_init("/mnt/daos");
	else
		rc = daos_init();
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

/* see comments in daos/dts.h */
void
dts_ctx_fini(struct credit_context *tsc)
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
		if (tsc->tsc_pmem_file)
			vos_self_fini();
		else
			daos_fini();
		/* fall through */
	case DTS_INIT_DEBUG:	/* finalize debug system */
		daos_debug_fini();
	}
}
