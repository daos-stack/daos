/**
 * (C) Copyright 2017-2022 Intel Corporation.
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
#include <daos/common.h>
#include <daos/credit.h>
#include <daos/tests_lib.h>
#include "suite/daos_test.h"
#include <daos/dts.h>
#include <daos/dpar.h>

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
engine_pool_init(struct credit_context *tsc)
{
	daos_handle_t	poh = DAOS_HDL_INVAL;
	int		rc = 0;

	if (tsc->tsc_mpi_rank == 0) {
		d_rank_list_t	*svc = &tsc->tsc_svc;
		char		str[37];

		if (tsc_create_pool(tsc)) {
			rc = dmg_pool_create(dmg_config_file, geteuid(),
					     getegid(), NULL, NULL,
					     tsc->tsc_scm_size,
					     tsc->tsc_nvme_size,
					     NULL, svc, tsc->tsc_pool_uuid);
			if (rc)
				goto bcast;
		}

		uuid_unparse(tsc->tsc_pool_uuid, str);
		rc = daos_pool_connect(str, NULL, DAOS_PC_EX, &poh, NULL, NULL);
		if (rc)
			goto bcast;
	}
	tsc->tsc_poh = poh;
bcast:
	if (tsc->tsc_mpi_size <= 1)
		return rc; /* don't need to share handle */

	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	if (rc)
		return rc; /* open failed */

	handle_share(&tsc->tsc_poh, HANDLE_POOL, tsc->tsc_mpi_rank,
		     tsc->tsc_poh, 0);
	return rc;
}

static void
engine_pool_fini(struct credit_context *tsc)
{
	int	rc;

	rc = daos_pool_disconnect(tsc->tsc_poh, NULL);
	D_ASSERTF(rc == 0 || rc == -DER_NO_HDL, "rc="DF_RC"\n", DP_RC(rc));
	par_barrier(PAR_COMM_WORLD);

	if (tsc->tsc_mpi_rank == 0 && tsc_create_pool(tsc)) {
		rc = dmg_pool_destroy(dmg_config_file, tsc->tsc_pool_uuid,
				      NULL, true);
		D_ASSERTF(rc == 0 || rc == -DER_NONEXIST || rc == -DER_MISC ||
			  rc == -DER_TIMEDOUT, "rc="DF_RC"\n", DP_RC(rc));
	}
}

static int
engine_cont_init(struct credit_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc = 0;

	if (tsc->tsc_mpi_rank == 0) {
		char str[37];

		if (tsc_create_cont(tsc)) {
			daos_prop_t *cont_prop;

			cont_prop = daos_prop_alloc(1);
			if (cont_prop == NULL) {
				rc = -DER_NOMEM;
				goto bcast;
			}
			cont_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
			cont_prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RANK;
			rc = daos_cont_create(tsc->tsc_poh, &tsc->tsc_cont_uuid,
					      cont_prop, NULL);
			daos_prop_free(cont_prop);
			if (rc != 0)
				goto bcast;
		}
		uuid_unparse(tsc->tsc_cont_uuid, str);
		rc = daos_cont_open(tsc->tsc_poh, str,
				    DAOS_COO_RW, &coh, NULL, NULL);
		if (rc != 0)
			goto bcast;
	}
	tsc->tsc_coh = coh;
bcast:
	if (tsc->tsc_mpi_size <= 1)
		return rc; /* don't need to share handle */

	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	if (rc)
		return rc; /* open failed */

	handle_share(&tsc->tsc_coh, HANDLE_CO, tsc->tsc_mpi_rank,
		     tsc->tsc_poh, 0);
	return rc;
}

static void
engine_cont_fini(struct credit_context *tsc)
{
	daos_cont_close(tsc->tsc_coh, NULL);
	/* NB: no container destroy at here, it will be destroyed by pool
	 * destroy later. This is because container destroy could be too
	 * expensive after performance tests.
	 */
}

static void
engine_fini(struct credit_context *tsc)
{
	daos_fini();
}

static int
engine_init(struct credit_context *tsc)
{
	return daos_init();
}

struct io_engine daos_engine = {
	.ie_name	= "DAOS",
	.ie_init	= engine_init,
	.ie_fini	= engine_fini,
	.ie_pool_init	= engine_pool_init,
	.ie_pool_fini	= engine_pool_fini,
	.ie_cont_init	= engine_cont_init,
	.ie_cont_fini	= engine_cont_fini,
};

bool
dts_is_async(struct credit_context *tsc)
{
	return daos_handle_is_valid(tsc->tsc_eqh);
}

/* see comments in daos/dts.h */
int
dts_ctx_init(struct credit_context *tsc, struct io_engine *engine)
{
	int	rc;

	tsc->tsc_init = DTS_INIT_NONE;
	/* Use default 'DAOS' engine when no engine specified */
	tsc->tsc_engine = (engine == NULL) ? &daos_engine : engine;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_DEBUG;

	D_ASSERT(tsc->tsc_engine->ie_init != NULL);
	rc = tsc->tsc_engine->ie_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_MODULE;

	if (tsc->tsc_scm_size == 0)
		tsc->tsc_scm_size = (1ULL << 30);
	if (tsc->tsc_dmg_conf)
		dmg_config_file = tsc->tsc_dmg_conf;

	D_ASSERT(tsc->tsc_engine->ie_pool_init != NULL);
	rc = tsc->tsc_engine->ie_pool_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_POOL;

	D_ASSERT(tsc->tsc_engine->ie_cont_init != NULL);
	rc = tsc->tsc_engine->ie_cont_init(tsc);
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
		D_ASSERT(tsc->tsc_engine->ie_cont_fini != NULL);
		tsc->tsc_engine->ie_cont_fini(tsc);
		/* fall through */
	case DTS_INIT_POOL:	/* close and destroy pool */
		D_ASSERT(tsc->tsc_engine->ie_pool_fini != NULL);
		tsc->tsc_engine->ie_pool_fini(tsc);
		/* fall through */
	case DTS_INIT_MODULE:	/* finalize module */
		D_ASSERT(tsc->tsc_engine->ie_fini != NULL);
		tsc->tsc_engine->ie_fini(tsc);
		/* fall through */
	case DTS_INIT_DEBUG:	/* finalize debug system */
		daos_debug_fini();
	}
}
