/**
 * (C) Copyright 2017-2020 Intel Corporation.
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

/**
 * examines if there is available credit freed by completed I/O, it will wait
 * until all credits are freed if @drain is true.
 */
static int
credit_poll(struct dts_context *tsc, bool drain)
{
	daos_event_t	*evs[DTS_CRED_MAX];
	int		 i;
	int		 rc;

	if (tsc->tsc_cred_inuse == 0)
		return 0; /* nothing inflight (sync mode never set inuse) */

	while (1) {
		rc = daos_eq_poll(tsc->tsc_eqh, 0, DAOS_EQ_WAIT, DTS_CRED_MAX,
				  evs);
		if (rc < 0) {
			fprintf(stderr, "failed to pool event: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		for (i = 0; i < rc; i++) {
			int err = evs[i]->ev_error;

			if (err != 0) {
				fprintf(stderr, "failed op: %d\n", err);
				return err;
			}
			tsc->tsc_credits[tsc->tsc_cred_avail] =
			   container_of(evs[i], struct dts_io_credit, tc_ev);

			tsc->tsc_cred_inuse--;
			tsc->tsc_cred_avail++;
		}

		if (tsc->tsc_cred_avail == 0)
			continue; /* still no available event */

		/* if caller wants to drain, is there any event inflight? */
		if (tsc->tsc_cred_inuse != 0 && drain)
			continue;

		return 0;
	}
}

/** try to obtain a free credit */
struct dts_io_credit *
dts_credit_take(struct dts_context *tsc)
{
	int	 rc;

	if (tsc->tsc_cred_avail < 0) /* synchronous mode */
		return &tsc->tsc_cred_buf[0];

	while (1) {
		if (tsc->tsc_cred_avail > 0) { /* yes there is free credit */
			tsc->tsc_cred_avail--;
			tsc->tsc_cred_inuse++;
			return tsc->tsc_credits[tsc->tsc_cred_avail];
		}

		rc = credit_poll(tsc, false);
		if (rc)
			return NULL;
	}
}

/** drain all the inflight credits */
int
dts_credit_drain(struct dts_context *tsc)
{
	return credit_poll(tsc, true);
}

static int
credits_init(struct dts_context *tsc)
{
	int	i;
	int	rc;

	if (tsc->tsc_cred_nr > 0) {
		rc = daos_eq_create(&tsc->tsc_eqh);
		if (rc)
			return rc;

		if (tsc->tsc_cred_nr > DTS_CRED_MAX)
			tsc->tsc_cred_avail = tsc->tsc_cred_nr = DTS_CRED_MAX;
		else
			tsc->tsc_cred_avail = tsc->tsc_cred_nr;
	} else { /* synchronous mode */
		tsc->tsc_eqh		= DAOS_HDL_INVAL;
		tsc->tsc_cred_nr	= 1;  /* take one slot in the buffer */
		tsc->tsc_cred_avail	= -1; /* always available */
	}

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		struct dts_io_credit *cred = &tsc->tsc_cred_buf[i];

		memset(cred, 0, sizeof(*cred));
		D_ALLOC(cred->tc_vbuf, tsc->tsc_cred_vsize);
		if (!cred->tc_vbuf) {
			fprintf(stderr, "Cannt allocate buffer size=%d\n",
				tsc->tsc_cred_vsize);
			return -1;
		}

		if (!daos_handle_is_inval(tsc->tsc_eqh)) {
			rc = daos_event_init(&cred->tc_ev, tsc->tsc_eqh, NULL);
			D_ASSERTF(!rc, "rc="DF_RC"\n", DP_RC(rc));
			cred->tc_evp = &cred->tc_ev;
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

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		if (!daos_handle_is_inval(tsc->tsc_eqh))
			daos_event_fini(&tsc->tsc_cred_buf[i].tc_ev);

		D_FREE(tsc->tsc_cred_buf[i].tc_vbuf);
	}

	if (!daos_handle_is_inval(tsc->tsc_eqh))
		daos_eq_destroy(tsc->tsc_eqh, DAOS_EQ_DESTROY_FORCE);
}

static int
pool_init(struct dts_context *tsc)
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

		/* Use pool size as blob size for this moment. */
		rc = vos_pool_create(pmem_file, tsc->tsc_pool_uuid, 0,
				     tsc->tsc_nvme_size);
		if (rc)
			goto out;

		rc = vos_pool_open(pmem_file, tsc->tsc_pool_uuid, false, &poh);
		if (rc)
			goto out;

	} else if (tsc->tsc_mpi_rank == 0) { /* DAOS mode and rank zero */
		d_rank_list_t	*svc = &tsc->tsc_svc;

		rc = dmg_pool_create(dmg_config_file, geteuid(), getegid(),
				     NULL, NULL,
				     tsc->tsc_scm_size, tsc->tsc_nvme_size,
				     NULL, svc, tsc->tsc_pool_uuid);
		if (rc)
			goto bcast;

		rc = daos_pool_connect(tsc->tsc_pool_uuid, NULL, svc,
				       DAOS_PC_EX, &poh, NULL, NULL);
		if (rc)
			goto bcast;
	}
	tsc->tsc_poh = poh;
 bcast:
	if (tsc->tsc_mpi_size <= 1)
		goto out; /* don't need to share handle */

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		goto out; /* open failed */

	handle_share(&tsc->tsc_poh, HANDLE_POOL, tsc->tsc_mpi_rank,
		     tsc->tsc_poh, 0);
 out:
	return rc;
}

static void
pool_fini(struct dts_context *tsc)
{
	int	rc;

	if (tsc->tsc_pmem_file) { /* VOS mode */
		vos_pool_close(tsc->tsc_poh);
		rc = vos_pool_destroy(tsc->tsc_pmem_file, tsc->tsc_pool_uuid);
		D_ASSERTF(rc == 0 || rc == -DER_NONEXIST, "rc="DF_RC"\n",
			DP_RC(rc));

	} else { /* DAOS mode */
		daos_pool_disconnect(tsc->tsc_poh, NULL);
		MPI_Barrier(MPI_COMM_WORLD);
		if (tsc->tsc_mpi_rank == 0) {
			rc = dmg_pool_destroy(dmg_config_file,
					      tsc->tsc_pool_uuid, NULL, true);
			D_ASSERTF(rc == 0 || rc == -DER_NONEXIST ||
				  rc == -DER_TIMEDOUT, "rc="DF_RC"\n",
				  DP_RC(rc));
		}
	}
}

static int
cont_init(struct dts_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;

	if (tsc->tsc_pmem_file) { /* VOS mode */
		rc = vos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid);
		if (rc)
			goto out;

		rc = vos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid, &coh);
		if (rc)
			goto out;

	} else if (tsc->tsc_mpi_rank == 0) { /* DAOS mode and rank zero */
		rc = daos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid, NULL,
				      NULL);
		if (rc != 0)
			goto bcast;

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

	handle_share(&tsc->tsc_coh, HANDLE_CO, tsc->tsc_mpi_rank,
		     tsc->tsc_poh, 0);
 out:
	return rc;
}

static void
cont_fini(struct dts_context *tsc)
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
dts_is_async(struct dts_context *tsc)
{
	return !daos_handle_is_inval(tsc->tsc_eqh);
}

/* see comments in daos/dts.h */
int
dts_ctx_init(struct dts_context *tsc)
{
	int	rc;

	tsc->tsc_init = DTS_INIT_NONE;
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_DEBUG;

	if (tsc->tsc_pmem_file) /* VOS mode */
		rc = vos_init();
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
		if (tsc->tsc_pmem_file)
			vos_fini();
		else
			daos_fini();
		/* fall through */
	case DTS_INIT_DEBUG:	/* finalize debug system */
		daos_debug_fini();
	}
}
