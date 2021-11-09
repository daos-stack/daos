/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * server: RPC Utilities
 */
#define D_LOGFAC       DD_FAC(server)

#include <daos_srv/daos_engine.h>

static void
rpc_cb(const struct crt_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->cci_arg;

	DABT_EVENTUAL_SET(*eventual, (void *)&cb_info->cci_rc, sizeof(cb_info->cci_rc));
}

/**
 * Send \a rpc and wait for the reply. Does not consume any references to \a
 * rpc.
 *
 * \param[in] rpc	RPC to be sent
 * \return		error code
 */
int
dss_rpc_send(crt_rpc_t *rpc)
{
	ABT_eventual	eventual;
	int	       *status;
	int		rc;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	crt_req_addref(rpc);

	rc = crt_req_send(rpc, rpc_cb, &eventual);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	DABT_EVENTUAL_WAIT(eventual, (void **)&status);

	rc = *status;

out_eventual:
	DABT_EVENTUAL_FREE(&eventual);
out:
	return rc;
}

/**
 * Server send reply or drop reply by fail_loc.
 *
 * \param[in] rpc	rpc to be replied.
 * \param[in] fail_loc	Fail_loc to check if it needs to drop reply.
 *
 * \return		0 if success, negative errno if failed.
 */
int
dss_rpc_reply(crt_rpc_t *rpc, unsigned int fail_loc)
{
	int rc;

	if (DAOS_FAIL_CHECK(fail_loc))
		return 0;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));

	return rc;
}
