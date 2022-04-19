/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos/rpc.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/daos_engine.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

static bool
ds_chk_need_rejoin(void) {
	return true;
}

static void
ds_chk_start_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_stop_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_query_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_act_hdlr(crt_rpc_t *rpc)
{
}

static int
ds_chk_init(void)
{
	return 0;
}

static int
ds_chk_fini(void)
{
	return 0;
}

static int
ds_chk_setup(void)
{
	int	rc;

	/* TBD: open log. */

	if (ds_chk_need_rejoin()) {
		rc = chk_rejoin();
		/* Rejoin check may fail, that is normal. Because former check instance may has
		 * already been stopped or exited, or current rank may miss some critical phase
		 * or has been excluded. Just some warning message without stopping the engine.
		 */
		if (rc != 0)
			D_WARN("Cannot rejoin CHECK: "DF_RC"\n", DP_RC(rc));
	}

	return 0;
}

static int
ds_chk_cleanup(void)
{
	chk_pause();

	/* TBD: close log. */

	return 0;
}

#define X(a, b, c, d, e, f)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
},

static struct daos_rpc_handler chk_handlers[] = {
	CHK_PROTO_SRV_RPC_LIST
};

#undef X

struct dss_module chk_module = {
	.sm_name		= "chk",
	.sm_mod_id		= DAOS_CHK_MODULE,
	.sm_ver			= DAOS_CHK_VERSION,
	.sm_init		= ds_chk_init,
	.sm_fini		= ds_chk_fini,
	.sm_setup		= ds_chk_setup,
	.sm_cleanup		= ds_chk_cleanup,
	.sm_cli_count		= 0,
	.sm_handlers		= chk_handlers,
};
