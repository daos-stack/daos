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
#define D_LOGFAC	DD_FAC(client)

#include <daos/rpc.h>
#include <daos/event.h>

struct daos_task_wrap {
	tse_task_t	*task;
	uint32_t	 gen;
};

static void
daos_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct daos_task_wrap	*arg = cb_info->cci_arg;
	tse_task_t		*task = arg->task;
	int			 rc = cb_info->cci_rc;

	if (rc == -DER_TIMEDOUT)
		/** TODO */
		;

	D_ASSERTF(task->dt_gen == arg->gen,
		  "FFFFFFFFFF: task is resued by others: %u/%u\n",
		  task->dt_gen, arg->gen);

	D_FREE_PTR(arg);
	task->dt_gen = 0;
	tse_task_complete(task, rc);
}

int
daos_rpc_complete(crt_rpc_t *rpc, tse_task_t *task)
{
	struct crt_cb_info      cbinfo;
	struct daos_task_wrap	arg;

	arg.task = task;
	arg.gen = task->dt_gen = rand();
	memset(&cbinfo, 0, sizeof(cbinfo));
	cbinfo.cci_arg = &arg;
	cbinfo.cci_rc  = 0;
	daos_rpc_cb(&cbinfo);
	crt_req_decref(rpc);
	return 0;
}

int
daos_rpc_send(crt_rpc_t *rpc, tse_task_t *task)
{
	struct daos_task_wrap	*arg;
	int			 rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -DER_NOMEM;

	arg->task = task;
	arg->gen = task->dt_gen = rand();
	rc = crt_req_send(rpc, daos_rpc_cb, arg);
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

static int
rpc_progress_cb(void *arg)
{
	struct daos_rpc_status *status = arg;

	return status->completed;
}

static void
daos_rpc_wait(crt_context_t *ctx, struct daos_rpc_status *status)
{
	/* Wait on the event to complete */
	while (!status->completed) {
		int rc = 0;

		rc = crt_progress(ctx, DAOS_EQ_WAIT,
				  rpc_progress_cb, status);
		if (rc)
			break;
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
