/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <daos_srv/daos_engine.h>
#include "bio_internal.h"

/**
 * BIO thread-local storage (TLS) private to each target
 */

static void
bio_tls_fini(void *data)
{
	struct bio_tls *tls = data;

	D_FREE(tls);
}

static void *
bio_tls_init(int xs_id, int tgt_id)
{
	struct bio_tls *tls;
	int		rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	if (tgt_id < 0)
		return tls;

	rc = d_tm_add_metric(&tls->btl_dma_buf, D_TM_GAUGE,
			     "Amount of (R)DMA buffers allocated", "bytes",
			     "io/%u/dma_buf", tgt_id);
	if (rc)
		D_WARN("Failed to create dma_buf sensor: "DF_RC"\n",
		       DP_RC(rc));

	return tls;
}

struct dss_module_key bio_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = bio_tls_init,
	.dmk_fini = bio_tls_fini,
};

static int
bio_mod_init(void)
{
	return 0;
}

static int
bio_mod_fini(void)
{
	return 0;
}

struct dss_module bio_srv_module =  {
	.sm_name	= "bio_srv",
	.sm_mod_id	= DAOS_BIO_MODULE,
	.sm_ver		= 1,
	.sm_init	= bio_mod_init,
	.sm_fini	= bio_mod_fini,
	.sm_key		= &bio_module_key,
};
