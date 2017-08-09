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

#define DD_SUBSYS	DD_FAC(tier)

#include <daos/tier.h>
#include <daos/debug.h>
#include "rpc.h"
#include "cli_internal.h"

/* CB args*/
struct tier_reg_cold_arg {
	crt_rpc_t	*rpc;
};

struct tier_conn_arg {
	crt_rpc_t	*rpc;
};

/*Completion callback for Cross-Connection*/
static int
dc_tier_conn_cb(tse_task_t *task, void *data)
{
	struct tier_conn_arg		*tc_arg = (struct tier_conn_arg *)data;
	struct tier_cross_conn_out	*cco_out = crt_reply_get(tc_arg->rpc);
	int				rc = task->dt_result;

	/*Check for task error*/
	if (rc) {
		D_ERROR("Task error in Cross-conn: %d\n", rc);
		D_GOTO(out, rc);
	}

	/*Check return status of the RPC itself (i.e. what did the server say)*/
	rc = cco_out->cco_ret;
	if (rc) {
		D_ERROR("Cross-Conn error: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* Info as its a onetime per run call*/
	D_INFO("Warm-Cold Connection Complete!\n");

out:
	crt_req_decref(tc_arg->rpc);
	return rc;
}

/*Completion callback for cold tier registry*/
static int
dc_tier_register_cold_cb(tse_task_t *task, void *data)
{
	struct tier_reg_cold_arg	*trc_arg =
					(struct tier_reg_cold_arg *)data;
	struct tier_upstream_out	*uo_out = crt_reply_get(trc_arg->rpc);
	int				rc = task->dt_result;

	/*Check for task  error*/
	if (rc) {
		D_ERROR("Task error from dc_tier_register_cold: %d\n", rc);
		D_GOTO(out, rc);
	}

	/*Check return status of the RPC itself (i.e. what did the server say)*/
	rc = uo_out->uo_ret;
	if (rc) {
		D_ERROR("Tier register cold error: %d\n", rc);
		D_GOTO(out, rc);
	}

	/*info as its a onetime per run call*/
	D_INFO("Tier Register Cold CB Complete!!\n");

out:
	crt_req_decref(trc_arg->rpc);
	return rc;
}

int
dc_tier_connect(const uuid_t warm_id, const char *warm_grp,
		tse_task_t *task)
{
	int				rc;
	crt_endpoint_t			warm_tgt_ep = {0};
	crt_rpc_t			*rpc_req;
	struct tier_cross_conn_in	*cci_in = NULL;
	struct tier_conn_arg		*tc_arg = NULL;
	int				alen;
	char				*warm_grp_cpy;


	/*NOTE hardcoding to rank 0 is temp measure.
	* Tag is hack to prevent deadlock with shared contexts
	*/
	warm_tgt_ep.ep_rank = 0;
	warm_tgt_ep.ep_tag = 1;

	rc = tier_req_create(daos_task2ctx(task), &warm_tgt_ep, TIER_CROSS_CONN,
			     &rpc_req);

	if (rc != 0) {
		D_ERROR("crt_req_create(TIER_CROSS_CONN) failed, rc: %d.\n",
			rc);
		D_GOTO(out_final, rc);
	}

	/*Verifying Request is there.*/
	D_ASSERT(rpc_req != NULL);
	cci_in = crt_req_get(rpc_req);
	D_ASSERT(cci_in != NULL);

	/*Set up arg info affiliated with task*
	* The alloc is non-std because we need to copy the warm_grp string
	* note this should be freed via callback infrastructure automatically
	*/
	alen = sizeof(*tc_arg) + strlen(warm_grp) + 1;
	D_ALLOC(tc_arg, alen);
	warm_grp_cpy = (char *)(&tc_arg[1]);
	strcpy(warm_grp_cpy, warm_grp);

	/*Load up the RPC inputs*/
	uuid_copy(cci_in->cci_warm_id, warm_id);
	cci_in->cci_warm_grp = (crt_string_t)warm_grp_cpy;

	crt_req_addref(rpc_req); /*Added for the arg*/
	tc_arg->rpc = rpc_req;

	/*Register CB*/
	rc = tse_task_register_comp_cb(task, dc_tier_conn_cb, tc_arg,
				       sizeof(struct tier_conn_arg));
	if (rc) {
		D_ERROR("Failed to register task callback.\n");
		D_GOTO(out_decref, rc);
	}
	/*Send the RPC*/
	rc = daos_rpc_send(rpc_req, task);

	return rc;

out_decref:
	/*Decrement ref count since callback never triggers if we got here*/
	crt_req_decref(rpc_req);
	/*Free since completion callback will never be triggered*/
	D_FREE(tc_arg, alen);
	return rc;
out_final:
	return rc;
}

int
dc_tier_register_cold(const uuid_t colder_id, const char *colder_grp,
		      char *tgt_grp_id, tse_task_t *task){

	int				rc;
	crt_endpoint_t			tgt;
	crt_rpc_t			*rpc_req = NULL;
	struct tier_register_cold_in	*rc_in = NULL;
	struct tier_reg_cold_arg	*trc_arg = NULL;

	tgt.ep_grp = tier_crt_group_lookup(tgt_grp_id);
	tgt.ep_rank = 0;
	tgt.ep_tag = 0;

	D_DEBUG(DF_TIERS, "tgt.ep_grp = %p\n", tgt.ep_grp);

	rc = tier_req_create(daos_task2ctx(task), &tgt, TIER_REGISTER_COLD,
			     &rpc_req);

	D_DEBUG(DF_TIERS, "entering...\n");
	if (rc != 0)
		D_GOTO(out, rc);

	/*Verifying Request is, you know, there.*/
	D_ASSERT(rpc_req != NULL);
	rc_in = crt_req_get(rpc_req);
	D_ASSERT(rc_in != NULL);

	/*Load up the RPC inputs*/
	uuid_copy(rc_in->rci_colder_id, colder_id);
	rc_in->rci_colder_grp = (crt_string_t)colder_grp;

	/*Set log up arg for task CB, this should be automatically freed
	* by the callback infrastrucure after it completes
	**/
	D_ALLOC_PTR(trc_arg);
	crt_req_addref(rpc_req);
	trc_arg->rpc = rpc_req;

	/*Register CB*/
	rc = tse_task_register_comp_cb(task, dc_tier_register_cold_cb, trc_arg,
				       sizeof(struct tier_reg_cold_arg));
	if (rc)
		D_GOTO(out, rc);

	/*Send the RPC*/
	rc = daos_rpc_send(rpc_req, task);

	return rc;
out:
	/*Decrement ref count since callback never triggers if we got here*/
	crt_req_decref(rpc_req);
	return rc;
}
