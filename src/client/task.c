/**
 * (C) Copyright 2017 Intel Corporation.
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
/*
 * This file is part of client DAOS library.
 *
 * client/task.c
 */

#define DDSUBSYS	DDFAC(client)

#include <daos/common.h>
#include "task_internal.h"

int
daos_task_create(daos_opc_t opc, tse_sched_t *sched, void *op_args,
		 unsigned int num_deps, tse_task_t *dep_tasks[],
		 tse_task_t **taskp)
{
	struct daos_task_args args;
	int rc;

	if (DAOS_OPC_INVALID >= opc || DAOS_OPC_MAX <= opc)
		return -DER_NOSYS;

	args.opc	= opc;
	args.priv	= NULL;
	if (op_args)
		memcpy(&args.op_args, op_args, dc_funcs[opc].arg_size);

	D__ASSERT(sizeof(struct daos_task_args) >=
		 (dc_funcs[opc].arg_size + sizeof(daos_opc_t)));

	rc = tse_task_init(dc_funcs[opc].task_func, &args,
			   sizeof(struct daos_task_args), sched, taskp);

	if (rc != 0)
		return rc;

	if (num_deps && *taskp) {
		rc = tse_task_register_deps(*taskp, num_deps, dep_tasks);
		if (rc != 0)
			return rc;
	}

	return rc;
}

void *
daos_task_get_args(daos_opc_t opc, tse_task_t *task)
{
	struct daos_task_args *task_arg;

	task_arg = tse_task_buf_get(task, sizeof(*task_arg));

	if (task_arg->opc != opc) {
		D__DEBUG(DB_ANY, "OPC does not match task's OPC\n");
		return NULL;
	}

	return &task_arg->op_args;
}

void *
daos_task_get_priv(tse_task_t *task)
{
	struct daos_task_args *task_arg;

	task_arg = tse_task_buf_get(task, sizeof(*task_arg));
	return task_arg->priv;
}

void
daos_task_set_priv(tse_task_t *task, void *priv)
{
	struct daos_task_args *task_arg;

	task_arg = tse_task_buf_get(task, sizeof(*task_arg));
	task_arg->priv = priv;
}

struct daos_progress_args_t {
	tse_sched_t	*sched;
	bool		*is_empty;
};

static int
sched_progress_cb(void *data)
{
	struct daos_progress_args_t *args = (struct daos_progress_args_t *)data;

	if (tse_sched_check_complete(args->sched)) {
		*args->is_empty = true;
		return 1;
	}

	tse_sched_progress(args->sched);
	return 0;
}

int
daos_progress(tse_sched_t *sched, int64_t timeout, bool *is_empty)
{
	struct daos_progress_args_t args;
	int rc;

	*is_empty = false;
	tse_sched_progress(sched);

	args.sched = sched;
	args.is_empty = is_empty;

	rc = crt_progress((crt_context_t *)sched->ds_udata, timeout,
			  sched_progress_cb, &args);
	if (rc != 0 && rc != -DER_TIMEDOUT)
		D__ERROR("crt progress failed with %d\n", rc);

	return rc;
}
