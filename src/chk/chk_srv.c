/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos/rpc.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/daos_engine.h>

#include "chk_internal.h"

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
ds_chk_mark_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_act_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_report_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_rejoin_hdlr(crt_rpc_t *rpc)
{
}

static int
ds_chk_init(void)
{
	int	rc;

	rc = chk_iv_init();

	return rc;
}

static int
ds_chk_fini(void)
{
	return chk_iv_fini();
}

static int
ds_chk_setup(void)
{
	/* Do NOT move chk_vos_init into ds_chk_init, because sys_db is not ready at that time. */
	chk_vos_init();

	return 0;
}

static int
ds_chk_cleanup(void)
{
	chk_vos_fini();

	return 0;
}

#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler chk_handlers[] = {
	CHK_PROTO_SRV_RPC_LIST,
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
	.sm_proto_count		= 1,
	.sm_proto_fmt		= &chk_proto_fmt,
	.sm_cli_count		= 0,
	.sm_handlers		= chk_handlers,
};
