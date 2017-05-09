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

#define DD_SUBSYS	DD_FAC(client)

#include <daos/common.h>
#include "task_internal.h"

int
daos_task_create(daos_opc_t opc, struct daos_sched *sched, void *op_args,
		 unsigned int num_deps, struct daos_task *dep_tasks[],
		 struct daos_task *task)
{
	struct daos_task_args args;
	int rc;

	if (DAOS_OPC_INVALID >= opc || DAOS_OPC_MAX <= opc)
		return -DER_NOSYS;

	args.opc	= opc;
	args.priv	= NULL;
	if (op_args)
		memcpy(&args.op_args, op_args, dc_funcs[opc].arg_size);

	D_ASSERT(sizeof(struct daos_task_args) >=
		 (dc_funcs[opc].arg_size + sizeof(daos_opc_t)));

	rc = daos_task_init(task, dc_funcs[opc].task_func, &args,
			    sizeof(struct daos_task_args), sched);
	if (rc != 0)
		return rc;

	if (num_deps) {
		rc = daos_task_register_deps(task, num_deps, dep_tasks);
		if (rc != 0)
			return rc;
	}

	return rc;
}

void *
daos_task_get_args(daos_opc_t opc, struct daos_task *task)
{
	struct daos_task_args *task_arg;

	task_arg = daos_task_buf_get(task, sizeof(*task_arg));

	if (task_arg->opc != opc) {
		D_DEBUG(DB_ANY, "OPC does not match task's OPC\n");
		return NULL;
	}

	return &task_arg->op_args;
}

void *
daos_task_get_priv(struct daos_task *task)
{
	struct daos_task_args *task_arg;

	task_arg = daos_task_buf_get(task, sizeof(*task_arg));
	return task_arg->priv;
}

void
daos_task_set_priv(struct daos_task *task, void *priv)
{
	struct daos_task_args *task_arg;

	task_arg = daos_task_buf_get(task, sizeof(*task_arg));
	task_arg->priv = priv;
}

struct daos_progress_args_t {
	struct daos_sched	*sched;
	bool			*is_empty;
};

static int
sched_progress_cb(void *data)
{
	struct daos_progress_args_t *args = (struct daos_progress_args_t *)data;

	if (daos_sched_check_complete(args->sched)) {
		*args->is_empty = true;
		return 1;
	}

	daos_sched_progress(args->sched);
	return 0;
}

int
daos_progress(struct daos_sched *sched, int64_t timeout, bool *is_empty)
{
	struct daos_progress_args_t args;
	int rc;

	*is_empty = false;
	daos_sched_progress(sched);

	args.sched = sched;
	args.is_empty = is_empty;

	rc = crt_progress((crt_context_t *)sched->ds_udata, timeout,
			  sched_progress_cb, &args);
	if (rc != 0 && rc != -DER_TIMEDOUT)
		D_ERROR("crt progress failed with %d\n", rc);

	return rc;
}
