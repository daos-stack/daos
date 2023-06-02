/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC       DD_FAC(tests)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos_test.h>
#include <daos/dts.h>
#include <daos_srv/vos.h>

static int
engine_pool_init(struct credit_context *tsc)
{
	daos_handle_t	poh = DAOS_HDL_INVAL;
	char		*pmem_file = tsc->tsc_pmem_file;
	int		rc, fd;

	if (!daos_file_is_dax(pmem_file)) {
		rc = open(pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (rc < 0)
			return rc;

		fd = rc;
		rc = fallocate(fd, 0, 0, tsc->tsc_scm_size);
		if (rc)
			return rc;
	}

	if (tsc_create_pool(tsc)) {
		/* Use pool size as blob size for this moment. */
		rc = vos_pool_create(pmem_file, tsc->tsc_pool_uuid, 0,
				     tsc->tsc_nvme_size, 0, &poh);
		if (rc)
			return rc;
	} else {
		rc = vos_pool_open(pmem_file, tsc->tsc_pool_uuid, 0, &poh);
		if (rc)
			return rc;
	}
	tsc->tsc_poh = poh;

	return rc;
}

static void
engine_pool_fini(struct credit_context *tsc)
{
	int	rc;

	vos_pool_close(tsc->tsc_poh);
	if (tsc_create_pool(tsc)) {
		rc = vos_pool_destroy(tsc->tsc_pmem_file,
				      tsc->tsc_pool_uuid);
		D_ASSERTF(rc == 0 || rc == -DER_NONEXIST,
			  "rc: "DF_RC"\n", DP_RC(rc));
	}
}

static int
engine_cont_init(struct credit_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;

	if (tsc_create_cont(tsc)) {
		rc = vos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid);
		if (rc)
			return rc;
	}

	rc = vos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid, &coh);
	if (rc)
		return rc;

	tsc->tsc_coh = coh;
	return rc;
}

static void
engine_cont_fini(struct credit_context *tsc)
{
	vos_cont_close(tsc->tsc_coh);
	/* NB: no container destroy at here, it will be destroyed by pool
	 * destroy later. This is because container destroy could be too
	 * expensive after performance tests.
	 */
}

static void
engine_fini(struct credit_context *tsc)
{
	vos_self_fini();
}

static int
engine_init(struct credit_context *tsc)
{
	return vos_self_init(tsc->tsc_pmem_path, false, BIO_STANDALONE_TGT_ID);
}

struct io_engine vos_engine = {
	.ie_name	= "VOS",
	.ie_init	= engine_init,
	.ie_fini	= engine_fini,
	.ie_pool_init	= engine_pool_init,
	.ie_pool_fini	= engine_pool_fini,
	.ie_cont_init	= engine_cont_init,
	.ie_cont_fini	= engine_cont_fini,
};
