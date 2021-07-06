/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/rpc.h>
#include <daos/event.h>

static void
daos_rpc_cb(const struct crt_cb_info *cb_info)
{
	tse_task_t	*task = cb_info->cci_arg;
	int		rc = cb_info->cci_rc;

	if (cb_info->cci_rc == -DER_TIMEDOUT)
		/** TODO */
		;

	tse_task_complete(task, rc);
}

int
daos_rpc_complete(crt_rpc_t *rpc, tse_task_t *task)
{
	struct crt_cb_info cbinfo;

	cbinfo.cci_arg = task;
	cbinfo.cci_rc  = 0;
	daos_rpc_cb(&cbinfo);
	crt_req_decref(rpc);
	return 0;
}

int
daos_rpc_send(crt_rpc_t *rpc, tse_task_t *task)
{
	int rc;

	rc = crt_req_send(rpc, daos_rpc_cb, task);
	if (rc != 0) {
		/** task will be completed in CB above */
		rc = 0;
	}

	return rc;
}

struct daos_rpc_status {
	int completed;
	int status;
};

static void
daos_rpc_wait(crt_context_t *ctx, struct daos_rpc_status *status)
{
	/* Wait on the event to complete */
	while (!status->completed) {
		int rc = 0;

		rc = crt_progress(ctx, 0);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("failed to progress CART context: %d\n", rc);
			break;
		}
	}
}

static void
daos_rpc_wait_cb(const struct crt_cb_info *cb_info)
{
	struct daos_rpc_status *status = cb_info->cci_arg;

	status->completed = 1;
	if (status->status == 0)
		status->status = cb_info->cci_rc;
}

int
daos_rpc_send_wait(crt_rpc_t *rpc)
{
	struct daos_rpc_status status = { 0 };
	int rc;

	rc = crt_req_send(rpc, daos_rpc_wait_cb, &status);
	if (rc != 0)
		return rc;

	daos_rpc_wait(rpc->cr_ctx, &status);
	rc = status.status;

	return rc;
}
