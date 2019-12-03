/**
 * (C) Copyright 2016 Intel Corporation.
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
 * server: RPC Utilities
 */
#define D_LOGFAC       DD_FAC(server)

#include <daos_srv/daos_server.h>

static void
rpc_cb(const struct crt_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->cci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->cci_rc,
			 sizeof(cb_info->cci_rc));
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

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	rc = *status;

out_eventual:
	ABT_eventual_free(&eventual);
out:
	return rc;
}

static int
group_create_cb(crt_group_t *grp, void *priv, int status)
{
	ABT_eventual *eventual = priv;

	if (status != 0) {
		D_ERROR("failed to create group: %d\n", status);
		grp = NULL;
	}
	ABT_eventual_set(*eventual, &grp, sizeof(grp));
	return 0;
}

/**
 * Create a group synchronously with \a id and \a ranks. See
 * crt_group_create().
 *
 * \param[in]	id	group ID
 * \param[in]	ranks	list of ranks in new group
 * \param[out]	group	new group
 */
int
dss_group_create(crt_group_id_t id, d_rank_list_t *ranks, crt_group_t **group)
{
	ABT_eventual	eventual;
	crt_group_t   **g;
	int		rc;

	rc = ABT_eventual_create(sizeof(*g), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	/*
	 * Always wait for the eventual, as group_create_cb() will be called in
	 * all cases, possibly by another ULT. "!populate_now" is not
	 * implemented yet.
	 */
	crt_group_create(id, ranks, true /* populate_now */, group_create_cb,
			 &eventual);

	rc = ABT_eventual_wait(eventual, (void **)&g);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*g == NULL)
		D_GOTO(out_eventual, rc = -DER_IO);

	*group = *g;
	rc = 0;
out_eventual:
	ABT_eventual_free(&eventual);
out:
	return rc;
}

static int
group_destroy_cb(void *args, int status)
{
	ABT_eventual *eventual = args;

	ABT_eventual_set(*eventual, &status, sizeof(status));
	return 0;
}

/**
 * Destroy \a group synchronously.
 *
 * \param[in]	group	group to destroy
 */
int
dss_group_destroy(crt_group_t *group)
{
	ABT_eventual	eventual;
	int	       *status;
	int		rc;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	/*
	 * Always wait for the eventual, as group_destroy_cb() will be called
	 * in all cases, possibly by another ULT.
	 */
	crt_group_destroy(group, group_destroy_cb, &eventual);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	rc = *status;
out_eventual:
	ABT_eventual_free(&eventual);
out:
	return rc;
}

/**
 * Argobots-sleep \a ms milliseconds. If you'd like to sleep more than a
 * second, then you'd likely need to write your own loop and check a cancel
 * flag or something similar, so that the sleep can be canceled.
 *
 * \param[in]	ms	milliseconds to sleep for
 */
void
dss_sleep(int ms)
{
	double t = ABT_get_wtime() + ms / 1000.0;

	do {
		ABT_thread_yield();
	} while (ABT_get_wtime() < t);
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
