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
/*
 * ds_mgmt: Pool Methods
 */
#define DD_SUBSYS	DD_FAC(mgmt)

#include "srv_internal.h"

#include <daos_srv/pool.h>

struct pc_inprogress {
	/* uuid as the unique ID of the pool-creating */
	uuid_t			 pc_pool_uuid;
	/* pool_create rpc request */
	crt_rpc_t		*pc_rpc_req;
	/* list of tgt_create RPC */
	daos_list_t		 pc_tc_list;
	/* number of tgt_create sent */
	unsigned int		 pc_tc_num;
	/* number of ACKs received of tgt_create */
	unsigned int		 pc_tc_ack_num;
	/* number of failed tgt_create */
	unsigned int		 pc_tc_fail_num;
	/* failure of the tgt_create */
	int			 pc_tc_fail;
	/* list of tgt_destroy RPC */
	daos_list_t		 pc_td_list;
	/* number of tgt_destroy sent */
	unsigned int		 pc_td_num;
	/* number of ACKs received of tgt_destroy */
	unsigned int		 pc_td_ack_num;
	/* number of failed tgt_destroy */
	unsigned int		 pc_td_fail_num;
	/* mutex to protect pc_tc_list, pc_td_list */
	pthread_mutex_t		 pc_req_mutex;
	/* array of target UUID, actual size is pc_tc_num */
	uuid_t			*pc_tgt_uuids;
	/* eventual for tgt_create/tgt_destroy completion */
	ABT_eventual		 pc_completion;
};

struct pc_tgt_create {
	/* link to pc_inprogress::pc_tc_list */
	daos_list_t		 ptc_link;
	/* tgt_create RPC */
	crt_rpc_t		*ptc_rpc_req;
};

struct pc_tgt_destroy {
	/* link to pc_inprogress::pc_td_list */
	daos_list_t		 ptd_link;
	/* tgt_destroy RPC */
	crt_rpc_t		*ptd_rpc_req;
};

static inline int
pc_add_req_to_inprog(struct pc_inprogress *pc_inprog, crt_rpc_t *pc_req)
{
	int	rc;

	D_ASSERT(pc_inprog != NULL && pc_req != NULL);

	rc = crt_req_addref(pc_req);
	D_ASSERT(rc == 0);
	pc_inprog->pc_rpc_req = pc_req;

	return 0;
}

static inline int
tc_add_req_to_inprog(struct pc_inprogress *pc_inprog, crt_rpc_t *tc_req)
{
	struct pc_tgt_create	*tc_req_item;
	int	rc;

	D_ASSERT(pc_inprog != NULL && tc_req != NULL);

	D_ALLOC_PTR(tc_req_item);
	if (tc_req_item == NULL)
		return -DER_NOMEM;

	/* init the pc_req item */
	DAOS_INIT_LIST_HEAD(&tc_req_item->ptc_link);
	rc = crt_req_addref(tc_req);
	D_ASSERT(rc == 0);
	tc_req_item->ptc_rpc_req = tc_req;

	/* insert the tc_req item to pc_inprogress::pc_tc_list */
	pthread_mutex_lock(&pc_inprog->pc_req_mutex);
	daos_list_add_tail(&tc_req_item->ptc_link,
			   &pc_inprog->pc_tc_list);
	pthread_mutex_unlock(&pc_inprog->pc_req_mutex);

	return 0;
}

static inline int
td_add_req_to_pc_inprog(struct pc_inprogress *pc_inprog, crt_rpc_t *td_req)
{
	struct pc_tgt_destroy	*td_req_item;
	int			 rc;

	D_ASSERT(pc_inprog != NULL && td_req != NULL);

	D_ALLOC_PTR(td_req_item);
	if (td_req_item == NULL)
		return -DER_NOMEM;

	/* init the pc_req item */
	DAOS_INIT_LIST_HEAD(&td_req_item->ptd_link);
	rc = crt_req_addref(td_req);
	D_ASSERT(rc == 0);
	td_req_item->ptd_rpc_req = td_req;

	/* insert the td_req item to pc_inprogress::pd_td_list */
	pthread_mutex_lock(&pc_inprog->pc_req_mutex);
	daos_list_add_tail(&td_req_item->ptd_link,
			   &pc_inprog->pc_td_list);
	pthread_mutex_unlock(&pc_inprog->pc_req_mutex);

	return 0;
}

static inline int
pc_inprog_create(struct pc_inprogress **pc_inprog, crt_rpc_t *rpc_req)
{
	struct mgmt_pool_create_in	*pc_in;
	struct pc_inprogress		*pc_inp_item;
	int				 rc = 0;

	D_ASSERT(pc_inprog != NULL && rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	D_ALLOC_PTR(pc_inp_item);
	if (pc_inp_item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* init the pc_inprogress item */
	uuid_copy(pc_inp_item->pc_pool_uuid, pc_in->pc_pool_uuid);
	DAOS_INIT_LIST_HEAD(&pc_inp_item->pc_tc_list);
	if (pc_in->pc_tgts == NULL) {
		rc = crt_group_size(NULL, &pc_inp_item->pc_tc_num);
		D_ASSERT(rc == 0);
	} else {
		pc_inp_item->pc_tc_num = pc_in->pc_tgts->rl_nr.num;
	}

	D_ALLOC(pc_inp_item->pc_tgt_uuids,
		sizeof(uuid_t) * pc_inp_item->pc_tc_num);
	if (pc_inp_item->pc_tgt_uuids == NULL) {
		D_FREE_PTR(pc_inp_item);
		D_GOTO(out, rc);
	}

	rc = ABT_eventual_create(0 /* nbytes */, &pc_inp_item->pc_completion);
	if (rc != ABT_SUCCESS) {
		D_FREE(pc_inp_item->pc_tgt_uuids,
		       sizeof(uuid_t) * pc_inp_item->pc_tc_num);
		D_FREE_PTR(pc_inp_item);
		D_GOTO(out, rc = dss_abterr2der(rc));
	}

	pc_inp_item->pc_tc_ack_num = 0;
	pc_inp_item->pc_tc_fail_num = 0;
	DAOS_INIT_LIST_HEAD(&pc_inp_item->pc_td_list);
	pc_inp_item->pc_td_num = 0;
	pc_inp_item->pc_td_ack_num = 0;
	pc_inp_item->pc_td_fail_num = 0;
	pthread_mutex_init(&pc_inp_item->pc_req_mutex, NULL);

	rc = pc_add_req_to_inprog(pc_inp_item, rpc_req);
	if (rc != 0) {
		D_ERROR("pc_add_req_to_inprog failed, rc: %d.\n", rc);
		ABT_eventual_free(&pc_inp_item->pc_completion);
		D_FREE(pc_inp_item->pc_tgt_uuids,
		       sizeof(uuid_t) * pc_inp_item->pc_tc_num);
		pthread_mutex_destroy(&pc_inp_item->pc_req_mutex);
		D_FREE_PTR(pc_inp_item);
		D_GOTO(out, rc);
	}

	*pc_inprog = pc_inp_item;

out:
	return rc;
}

static inline void
pc_inprog_destroy(struct pc_inprogress *pc_inprog)
{
	struct pc_tgt_create	*tc, *tc_next;
	struct pc_tgt_destroy	*td, *td_next;
	int			 rc;

	D_ASSERT(pc_inprog != NULL);

	/* decref corresponds to the addref in pc_add_req_to_inprog */
	rc = crt_req_decref(pc_inprog->pc_rpc_req);
	D_ASSERT(rc == 0);

	/* cleanup tgt-create req list */
	daos_list_for_each_entry_safe(tc, tc_next, &pc_inprog->pc_tc_list,
				      ptc_link) {
		daos_list_del_init(&tc->ptc_link);
		/* decref corresponds to the addref in tc_add_req_to_inprog */
		rc = crt_req_decref(tc->ptc_rpc_req);
		D_ASSERT(rc == 0);
		D_FREE_PTR(tc);
	}

	/* cleanup tgt-destroy req list */
	daos_list_for_each_entry_safe(td, td_next, &pc_inprog->pc_td_list,
				      ptd_link) {
		daos_list_del_init(&td->ptd_link);
		/* decref corresponds to addref in td_add_req_to_pc_inprog */
		rc = crt_req_decref(td->ptd_rpc_req);
		D_ASSERT(rc == 0);
		D_FREE_PTR(td);
	}

	pthread_mutex_destroy(&pc_inprog->pc_req_mutex);

	ABT_eventual_free(&pc_inprog->pc_completion);

	D_FREE(pc_inprog->pc_tgt_uuids, sizeof(uuid_t) * pc_inprog->pc_tc_num);
	D_FREE_PTR(pc_inprog);
}

/*
 * Compare the two pool_create input parameters.
 * Return true if all parameters are same, false otherwise.
 */
static inline bool
pc_input_identical(struct mgmt_pool_create_in *pc_in1,
		 struct mgmt_pool_create_in *pc_in2)
{
	D_ASSERT(pc_in1 != NULL && pc_in2 != NULL);

	if (uuid_compare(pc_in1->pc_pool_uuid, pc_in2->pc_pool_uuid) != 0)
		return false;
	if (pc_in1->pc_mode != pc_in2->pc_mode)
		return false;
	if (strcmp(pc_in1->pc_grp, pc_in2->pc_grp) != 0)
		return false;
	if (!daos_rank_list_identical(pc_in1->pc_tgts, pc_in2->pc_tgts, true))
		return false;
	if (strcmp(pc_in1->pc_tgt_dev, pc_in2->pc_tgt_dev) != 0)
		return false;
	if (pc_in1->pc_tgt_size != pc_in2->pc_tgt_size)
		return false;

	return true;
}

static int
pc_tgt_destroy_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*td_req;
	struct mgmt_tgt_destroy_in	*td_in;
	struct mgmt_tgt_destroy_out	*td_out;
	struct pc_inprogress		*pc_inprog;
	struct pc_tgt_destroy		*td, *td_next;
	bool				 td_done = false;
	int				 rc = 0;

	td_req = cb_info->cci_rpc;
	td_in = crt_req_get(td_req);
	td_out = crt_reply_get(td_req);
	rc = cb_info->cci_rc;
	pc_inprog = (struct pc_inprogress *)cb_info->cci_arg;
	D_ASSERT(pc_inprog != NULL && td_in != NULL && td_out != NULL);
	D_ASSERT(pc_inprog->pc_tc_ack_num == pc_inprog->pc_tc_num);

	pthread_mutex_lock(&pc_inprog->pc_req_mutex);
	pc_inprog->pc_td_ack_num++;
	if (rc != 0 || td_out->td_rc != 0) {
		pc_inprog->pc_td_fail_num++;
		D_ERROR("MGMT_TGT_DESTROY(to rank: %d) failed, "
			"cb_info->cci_rc: %d, td_out->td_rc: %d. "
			"total failed num: %d.\n", td_req->cr_ep.ep_rank, rc,
			td_out->td_rc, pc_inprog->pc_td_fail_num);
	}
	D_ASSERT(pc_inprog->pc_td_ack_num <= pc_inprog->pc_td_num);
	D_ASSERT(pc_inprog->pc_td_fail_num <= pc_inprog->pc_td_num);
	daos_list_for_each_entry_safe(td, td_next, &pc_inprog->pc_td_list,
				      ptd_link) {
		if (td->ptd_rpc_req == td_req) {
			daos_list_del_init(&td->ptd_link);
			/* decref corresponds to the addref in
			 * td_add_req_to_pc_inprog */
			rc = crt_req_decref(td_req);
			D_ASSERT(rc == 0);
			D_FREE_PTR(td);
			break;
		}
	}
	if (pc_inprog->pc_td_ack_num == pc_inprog->pc_td_num)
		td_done = true;
	pthread_mutex_unlock(&pc_inprog->pc_req_mutex);

	if (td_done == false)
		D_GOTO(out, rc = 0);

	ABT_eventual_set(pc_inprog->pc_completion, NULL /* value */,
			 0 /* nbytes */);

out:
	return rc;
}

static int
tgt_create_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*tc_req;
	struct mgmt_tgt_create_in	*tc_in;
	struct mgmt_tgt_create_out	*tc_out;
	struct pc_inprogress		*pc_inprog;
	struct pc_tgt_create		*tc, *tc_next;
	crt_rpc_t			*pc_req;
	struct mgmt_pool_create_in	*pc_in;
	bool				 tc_done = false;
	int				 rc = 0;

	tc_req = cb_info->cci_rpc;
	tc_in = crt_req_get(tc_req);
	tc_out = crt_reply_get(tc_req);
	rc = cb_info->cci_rc;
	pc_inprog = (struct pc_inprogress *)cb_info->cci_arg;
	D_ASSERT(pc_inprog != NULL && tc_in != NULL && tc_out != NULL);

	pc_req = pc_inprog->pc_rpc_req;
	pc_in = crt_req_get(pc_req);

	pthread_mutex_lock(&pc_inprog->pc_req_mutex);
	pc_inprog->pc_tc_ack_num++;

	if (rc)
		D_ERROR(DF_UUID": RPC error while creating tgt on rank %d: "
			"%d\n", DP_UUID(pc_inprog->pc_pool_uuid),
			tc_req->cr_ep.ep_rank, rc);
	if (tc_out->tc_rc)
		D_ERROR(DF_UUID": failed to create tgt on rank %d: %d\n",
			DP_UUID(pc_inprog->pc_pool_uuid),
			tc_req->cr_ep.ep_rank, tc_out->tc_rc);

	if (rc != 0 || tc_out->tc_rc != 0) {
		pc_inprog->pc_tc_fail_num++;
		if (pc_inprog->pc_tc_fail == 0)
			pc_inprog->pc_tc_fail = rc != 0 ? rc : tc_out->tc_rc;

		/* Remove failed tgt-create req from tgt-create req list,
		 * the succeed req remains there as if some other req failed
		 * need to do the tgt-destroy for error handling. */
		daos_list_for_each_entry_safe(tc, tc_next,
					      &pc_inprog->pc_tc_list,
					      ptc_link) {
			if (tc->ptc_rpc_req == tc_req) {
				daos_list_del_init(&tc->ptc_link);
				/* decref corresponds to the addref in
				 * tc_add_req_to_inprog */
				rc = crt_req_decref(tc_req);
				D_ASSERT(rc == 0);
				D_FREE_PTR(tc);
				break;
			}
		}
	} else {
		int idx;

		D_DEBUG(DB_MGMT, DF_UUID": tgt "DF_UUID" created on rank %d\n",
			DP_UUID(pc_inprog->pc_pool_uuid),
			DP_UUID(tc_out->tc_tgt_uuid), tc_req->cr_ep.ep_rank);

		if (pc_in->pc_tgts == NULL) {
			idx = tc_req->cr_ep.ep_rank;
		} else {
			bool found;

			found = daos_rank_list_find(pc_in->pc_tgts,
						   tc_req->cr_ep.ep_rank,
						   &idx);
			D_ASSERT(found);
		}
		/** copy returned target UUID */
		uuid_copy(pc_inprog->pc_tgt_uuids[idx], tc_out->tc_tgt_uuid);
	}

	D_ASSERT(pc_inprog->pc_tc_ack_num <= pc_inprog->pc_tc_num);
	D_ASSERT(pc_inprog->pc_tc_fail_num <= pc_inprog->pc_tc_num);
	if (pc_inprog->pc_tc_ack_num == pc_inprog->pc_tc_num)
		tc_done = true;
	pthread_mutex_unlock(&pc_inprog->pc_req_mutex);

	if (!tc_done)
		D_GOTO(out, rc);

	ABT_eventual_set(pc_inprog->pc_completion, NULL /* value */,
			 0 /* nbytes */);

out:
	return rc;
}

int
ds_mgmt_hdlr_pool_create(crt_rpc_t *rpc_req)
{
	struct mgmt_pool_create_in	*pc_in;
	crt_endpoint_t			 svr_ep;
	struct mgmt_pool_create_out	*pc_out;
	struct pc_inprogress		*pc_inprog = NULL;
	crt_rpc_t			*tc_req;
	crt_opcode_t			 opc;
	struct mgmt_tgt_create_in	*tc_in;
	struct pc_tgt_create		*tc, *tc_next;
	bool				 tc_req_sent = false;
	bool				 td_req_sent = false;
	bool				 pc_inprog_alloc = false;
	int				 i, rc = 0;

	pc_in = crt_req_get(rpc_req);
	pc_out = crt_reply_get(rpc_req);
	D_ASSERT(pc_in != NULL && pc_out != NULL);
	pc_out->pc_svc = NULL;
	if (pc_in->pc_tgts)
		daos_rank_list_sort(pc_in->pc_tgts);

	/* TODO check metadata about the pool's existence? */

	rc = pc_inprog_create(&pc_inprog, rpc_req);
	if (rc == 0) {
		D_ASSERT(pc_inprog != NULL);
		pc_inprog_alloc = true;
	} else {
		D_ERROR("pc_inprog_create failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	/** new request handling */

	/** allocate service rank list */
	D_ALLOC_PTR(pc_out->pc_svc);
	if (pc_out->pc_svc == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(pc_out->pc_svc->rl_ranks,
		pc_in->pc_svc_nr * sizeof(daos_rank_t));
	if (pc_out->pc_svc->rl_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	pc_out->pc_svc->rl_nr.num = pc_in->pc_svc_nr;

	/** send MGMT_TGT_CREATE RPC to tgts */
	svr_ep.ep_grp = NULL;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(MGMT_TGT_CREATE, DAOS_MGMT_MODULE, 1);
	for (i = 0; i < pc_inprog->pc_tc_num; i++) {
		if (pc_in->pc_tgts == NULL)
			svr_ep.ep_rank = i;
		else
			svr_ep.ep_rank = pc_in->pc_tgts->rl_ranks[i];

		rc = crt_req_create(dss_get_module_info()->dmi_ctx, svr_ep,
				    opc, &tc_req);
		if (rc != 0) {
			D_ERROR("crt_req_create(MGMT_TGT_CREATE) failed, "
				"rc: %d.\n", rc);
			pc_inprog->pc_tc_ack_num = pc_inprog->pc_tc_num - i;
			pc_inprog->pc_tc_fail_num = pc_inprog->pc_tc_num - i;
			D_GOTO(out, rc);
		}
		tc_in = crt_req_get(tc_req);
		D_ASSERT(tc_in != NULL);
		uuid_copy(tc_in->tc_pool_uuid, pc_in->pc_pool_uuid);
		/* the pc_in->pc_tgt_dev will be freed when the MGMT_POOL_CREATE
		 * finishes, it is after TGT_CREATE RPC handling so it is safe
		 * to directly use it here. */
		tc_in->tc_tgt_dev = pc_in->pc_tgt_dev;
		tc_in->tc_tgt_size = pc_in->pc_tgt_size;

		rc = crt_req_send(tc_req, tgt_create_cb, pc_inprog);
		if (rc != 0) {
			D_ERROR("crt_req_send(MGMT_TGT_CREATE) failed, "
				"rc: %d.\n", rc);
			pc_inprog->pc_tc_ack_num = pc_inprog->pc_tc_num - i;
			pc_inprog->pc_tc_fail_num = pc_inprog->pc_tc_num - i;
			D_GOTO(out, rc);
		}

		tc_req_sent =  true;
		rc = tc_add_req_to_inprog(pc_inprog, tc_req);
		D_ASSERT(rc == 0);
	}

	if (tc_req_sent == false)
		D_GOTO(out, rc);

	ABT_eventual_wait(pc_inprog->pc_completion, NULL /* value */);

	/* all tgt_create finished */
	if (pc_inprog->pc_tc_fail_num == 0) {
		daos_rank_list_t	ranks;
		int			doms[pc_inprog->pc_tc_num];

		D_DEBUG(DB_MGMT, DF_UUID": all tgts created, setting up pool "
			"svc\n", DP_UUID(pc_inprog->pc_pool_uuid));

		for (i = 0; i < pc_inprog->pc_tc_num; i++)
			doms[i] = 1;

		if (pc_in->pc_tgts == NULL) {
			ranks.rl_nr.num = pc_inprog->pc_tc_num;
			D_ALLOC(ranks.rl_ranks,
				sizeof(*ranks.rl_ranks) * ranks.rl_nr.num);
			if (ranks.rl_ranks == NULL)
				D_GOTO(svc_create_fail, rc = -DER_NOMEM);
			for (i = 0; i < ranks.rl_nr.num; i++)
				ranks.rl_ranks[i] = i;
		}

		/**
		 * TODO: fetch domain list from external source
		 * Report 1 domain per target for now
		 */
		rc = ds_pool_svc_create(pc_inprog->pc_pool_uuid, pc_in->pc_uid,
					pc_in->pc_gid, pc_in->pc_mode,
					pc_inprog->pc_tc_num,
					pc_inprog->pc_tgt_uuids, pc_in->pc_grp,
					pc_in->pc_tgts ?: &ranks,
					ARRAY_SIZE(doms), doms, pc_out->pc_svc);

		if (pc_in->pc_tgts == NULL)
			D_FREE(ranks.rl_ranks,
			       sizeof(*ranks.rl_ranks) * ranks.rl_nr.num);

		if (rc == 0)
			D_GOTO(out, rc = 0);

svc_create_fail:
		D_ERROR(DF_UUID": pool svc setup failed with %d\n",
			DP_UUID(pc_inprog->pc_pool_uuid), rc);
	}

	/* do error handling, send tgt_destroy for succeed tgt_create */
	ABT_eventual_reset(pc_inprog->pc_completion);
	svr_ep.ep_grp = NULL;
	svr_ep.ep_tag = 0;
	daos_list_for_each_entry_safe(tc, tc_next, &pc_inprog->pc_tc_list,
				      ptc_link) {
		crt_rpc_t			*td_req;
		struct mgmt_tgt_destroy_in	*td_in;
		int				ret;

		daos_list_del_init(&tc->ptc_link);

		tc_req = tc->ptc_rpc_req;
		tc_in = crt_req_get(tc_req);
		svr_ep.ep_rank = tc_req->cr_ep.ep_rank;

		D_FREE_PTR(tc);

		pc_inprog->pc_td_num++;
		opc = DAOS_RPC_OPCODE(MGMT_TGT_DESTROY, DAOS_MGMT_MODULE, 1);
		ret = crt_req_create(dss_get_module_info()->dmi_ctx, svr_ep,
				    opc, &td_req);
		if (ret != 0) {
			D_ERROR("crt_req_create(MGMT_TGT_DESTROY) failed, "
				"rc: %d.\n", ret);
			pc_inprog->pc_td_ack_num++;
			pc_inprog->pc_td_fail_num++;
			/* decref corresponds to the addref in
			 * tc_add_req_to_inprog */
			ret = crt_req_decref(tc_req);
			D_ASSERT(ret == 0);
			continue;
		}

		td_in = crt_req_get(td_req);
		D_ASSERT(td_in != NULL);
		uuid_copy(td_in->td_pool_uuid, tc_in->tc_pool_uuid);

		/* decref corresponds to the addref in tc_add_req_to_inprog */
		ret = crt_req_decref(tc_req);
		D_ASSERT(ret == 0);

		ret = crt_req_send(td_req, pc_tgt_destroy_cb, pc_inprog);
		if (ret != 0) {
			D_ERROR("crt_req_send(MGMT_TGT_DESTROY) failed, "
				"rc: %d.\n", ret);
			pc_inprog->pc_td_ack_num++;
			pc_inprog->pc_td_fail_num++;
			continue;
		}

		td_req_sent = true;
		ret = td_add_req_to_pc_inprog(pc_inprog, td_req);
		D_ASSERT(ret == 0);
	}

	if (td_req_sent == false)
		D_GOTO(out, rc);

	ABT_eventual_wait(pc_inprog->pc_completion, NULL /* value */);

out:
	if (rc == 0 && pc_inprog != NULL)
		rc = pc_inprog->pc_tc_fail;
	pc_out->pc_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d "
			"(pc_tgt_dev: %s).\n", rc, pc_in->pc_tgt_dev);
	if (pc_inprog_alloc == true)
		pc_inprog_destroy(pc_inprog);
	return rc;
}

struct pd_inprogress {
	uuid_t			 pd_pool_uuid;
	/* pool_destroy rpc request */
	crt_rpc_t		*pd_rpc_req;
	/* list of tgt_destroy RPC */
	daos_list_t		 pd_td_list;
	/* number of tgt_destroy sent */
	unsigned int		 pd_td_num;
	/* number of ACKs received of tgt_destroy */
	unsigned int		 pd_td_ack_num;
	/* number of failed tgt_destroy */
	unsigned int		 pd_td_fail_num;
	/* pool_destroy return code */
	int			 pd_rc;
	/* mutex to protect pd_td_list */
	pthread_mutex_t		 pd_req_mutex;
	/* eventual for tgt_destroy completion */
	ABT_eventual		 pd_completion;
};

struct pd_tgt_destroy {
	/* link to pd_inprogress::pd_td_list */
	daos_list_t		 ptd_link;
	/* tgt_destroy RPC */
	crt_rpc_t		*ptd_rpc_req;
};

static inline int
pd_add_req_to_inprog(struct pd_inprogress *pd_inprog, crt_rpc_t *pd_req)
{
	int		rc;

	D_ASSERT(pd_inprog != NULL && pd_req != NULL);

	rc = crt_req_addref(pd_req);
	D_ASSERT(rc == 0);
	pd_inprog->pd_rpc_req = pd_req;

	return 0;
}

static inline int
td_add_req_to_pd_inprog(struct pd_inprogress *pd_inprog, crt_rpc_t *td_req)
{
	struct pd_tgt_destroy	*td_req_item;
	int	rc;

	D_ASSERT(pd_inprog != NULL && td_req != NULL);

	D_ALLOC_PTR(td_req_item);
	if (td_req_item == NULL)
		return -DER_NOMEM;

	/* init the pc_req item */
	DAOS_INIT_LIST_HEAD(&td_req_item->ptd_link);
	rc = crt_req_addref(td_req);
	D_ASSERT(rc == 0);
	td_req_item->ptd_rpc_req = td_req;

	/* insert the pc_req item to pd_inprogress::pd_td_list */
	pthread_mutex_lock(&pd_inprog->pd_req_mutex);
	daos_list_add_tail(&td_req_item->ptd_link,
			   &pd_inprog->pd_td_list);
	pthread_mutex_unlock(&pd_inprog->pd_req_mutex);

	return 0;
}

static inline int
pd_inprog_create(struct pd_inprogress **pd_inprog, crt_rpc_t *rpc_req)
{
	struct mgmt_pool_destroy_in	*pd_in;
	struct pd_inprogress		*pd_inp_item;
	int				 rc = 0;

	D_ASSERT(pd_inprog != NULL && rpc_req != NULL);
	pd_in = crt_req_get(rpc_req);
	D_ASSERT(pd_in != NULL);

	D_ALLOC_PTR(pd_inp_item);
	if (pd_inp_item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ABT_eventual_create(0 /* nbytes */, &pd_inp_item->pd_completion);
	if (rc != ABT_SUCCESS) {
		D_FREE_PTR(pd_inp_item);
		D_GOTO(out, rc = dss_abterr2der(rc));
	}

	/* init the pd_inprogress item */
	uuid_copy(pd_inp_item->pd_pool_uuid, pd_in->pd_pool_uuid);
	DAOS_INIT_LIST_HEAD(&pd_inp_item->pd_td_list);
	/* TODO query metadata about the tgt list of the pool? */
	rc = crt_group_size(NULL, &pd_inp_item->pd_td_num);
	D_ASSERT(rc == 0);
	pd_inp_item->pd_td_ack_num = 0;
	pd_inp_item->pd_td_fail_num = 0;
	pd_inp_item->pd_rc = 0;
	pthread_mutex_init(&pd_inp_item->pd_req_mutex, NULL);

	rc = pd_add_req_to_inprog(pd_inp_item, rpc_req);
	if (rc != 0) {
		D_ERROR("pd_add_req_to_inprog failed, rc: %d.\n", rc);
		pthread_mutex_destroy(&pd_inp_item->pd_req_mutex);
		D_FREE_PTR(pd_inp_item);
		D_GOTO(out, rc);
	}

	*pd_inprog = pd_inp_item;

out:
	return rc;
}

static inline void
pd_inprog_destroy(struct pd_inprogress *pd_inprog)
{
	struct pd_tgt_destroy	*td, *td_next;
	int			rc;

	D_ASSERT(pd_inprog != NULL);

	/* decref corresponds to the addref in pd_add_req_to_inprog */
	rc = crt_req_decref(pd_inprog->pd_rpc_req);
	D_ASSERT(rc == 0);

	/* cleanup tgt-destroy req list */
	daos_list_for_each_entry_safe(td, td_next, &pd_inprog->pd_td_list,
				      ptd_link) {
		daos_list_del_init(&td->ptd_link);
		/* decref corresponds to addref in td_add_req_to_pd_inprog */
		rc = crt_req_decref(td->ptd_rpc_req);
		D_ASSERT(rc == 0);
		D_FREE_PTR(td);
	}

	pthread_mutex_destroy(&pd_inprog->pd_req_mutex);
	D_FREE_PTR(pd_inprog);
}

static int
pd_tgt_destroy_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*td_req;
	struct mgmt_tgt_destroy_in	*td_in;
	struct mgmt_tgt_destroy_out	*td_out;
	struct pd_inprogress		*pd_inprog;
	struct pc_tgt_destroy		*td, *td_next;
	bool				 td_done = false;
	int				 rc = 0;

	td_req = cb_info->cci_rpc;
	td_in = crt_req_get(td_req);
	td_out = crt_reply_get(td_req);
	rc = cb_info->cci_rc;
	pd_inprog = (struct pd_inprogress *)cb_info->cci_arg;
	D_ASSERT(pd_inprog != NULL && td_in != NULL && td_out != NULL);

	pthread_mutex_lock(&pd_inprog->pd_req_mutex);
	pd_inprog->pd_td_ack_num++;
	if (rc != 0 || td_out->td_rc != 0) {
		pd_inprog->pd_td_fail_num++;
		D_ERROR("MGMT_TGT_DESTROY(to rank: %d) failed, "
			"cb_info->cci_rc: %d, td_out->td_rc: %d. "
			"total failed num: %d.\n", td_req->cr_ep.ep_rank, rc,
			td_out->td_rc, pd_inprog->pd_td_fail_num);
		if (rc == 0)
			rc = td_out->td_rc;
		pd_inprog->pd_rc = rc;
	}
	D_ASSERT(pd_inprog->pd_td_ack_num <= pd_inprog->pd_td_num);
	D_ASSERT(pd_inprog->pd_td_fail_num <= pd_inprog->pd_td_num);
	daos_list_for_each_entry_safe(td, td_next, &pd_inprog->pd_td_list,
				      ptd_link) {
		if (td->ptd_rpc_req == td_req) {
			daos_list_del_init(&td->ptd_link);
			/* decref corresponds to the addref in
			 * td_add_req_to_pd_inprog */
			rc = crt_req_decref(td_req);
			D_ASSERT(rc == 0);
			D_FREE_PTR(td);
			break;
		}
	}
	if (pd_inprog->pd_td_ack_num == pd_inprog->pd_td_num)
		td_done = true;
	pthread_mutex_unlock(&pd_inprog->pd_req_mutex);

	if (td_done == false)
		D_GOTO(out, rc = 0);

	ABT_eventual_set(pd_inprog->pd_completion, NULL /* value */,
			 0 /* nbytes */);

out:
	return rc;
}

int
ds_mgmt_hdlr_pool_destroy(crt_rpc_t *rpc_req)
{
	struct mgmt_pool_destroy_in	*pd_in;
	crt_endpoint_t			 svr_ep;
	struct mgmt_pool_destroy_out	*pd_out;
	struct pd_inprogress		*pd_inprog = NULL;
	crt_rpc_t			*td_req;
	crt_opcode_t			 opc;
	struct mgmt_tgt_destroy_in	*td_in;
	bool				 td_req_sent = false;
	bool				 pd_inprog_alloc = false;
	int				 i, rc = 0;

	pd_in = crt_req_get(rpc_req);
	pd_out = crt_reply_get(rpc_req);
	D_ASSERT(pd_in != NULL && pd_out != NULL);

	/* TODO check metadata about the pool's existence?
	 *      and check active pool connection for "force"
	 */

	rc = pd_inprog_create(&pd_inprog, rpc_req);
	if (rc == 0) {
		D_ASSERT(pd_inprog != NULL);
		pd_inprog_alloc = true;
	} else {
		D_ERROR("pd_inprog_create failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID"\n",
		DP_UUID(pd_in->pd_pool_uuid));

	/** send MGMT_TGT_DESTROY RPC to tgts */
	/* TODO query metadata the tgt list of the pool */
	svr_ep.ep_grp = NULL;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(MGMT_TGT_DESTROY, DAOS_MGMT_MODULE, 1);
	for (i = 0; i < pd_inprog->pd_td_num; i++) {
		svr_ep.ep_rank = i;
		rc = crt_req_create(dss_get_module_info()->dmi_ctx, svr_ep,
				    opc, &td_req);
		if (rc != 0) {
			D_ERROR("crt_req_create(MGMT_TGT_DESTROY) failed, "
				"rc: %d.\n", rc);
			pd_inprog->pd_td_ack_num = pd_inprog->pd_td_num - i;
			pd_inprog->pd_td_fail_num = pd_inprog->pd_td_num - i;
			D_GOTO(out, rc);
		}

		td_in = crt_req_get(td_req);
		D_ASSERT(td_in != NULL);
		uuid_copy(td_in->td_pool_uuid, pd_in->pd_pool_uuid);
		rc = crt_req_send(td_req, pd_tgt_destroy_cb, pd_inprog);
		if (rc != 0) {
			D_ERROR("crt_req_send(MGMT_TGT_DESTROY) failed, "
				"rc: %d.\n", rc);
			pd_inprog->pd_td_ack_num = pd_inprog->pd_td_num - i;
			pd_inprog->pd_td_fail_num = pd_inprog->pd_td_num - i;
			D_GOTO(out, rc);
		}

		td_req_sent =  true;
		rc = td_add_req_to_pd_inprog(pd_inprog, td_req);
		D_ASSERT(rc == 0);
	}

	if (td_req_sent == false)
		D_GOTO(out, rc);

	ABT_eventual_wait(pd_inprog->pd_completion, NULL /* value */);

	if (pd_out->pd_rc == 0)
		D_DEBUG(DB_MGMT, "Destroying pool "DF_UUID" succeed.\n",
			DP_UUID(pd_in->pd_pool_uuid));
	else
		D_ERROR("Destroying pool "DF_UUID"failed, rc: %d.\n",
			DP_UUID(pd_in->pd_pool_uuid), pd_out->pd_rc);

	rc = ds_pool_svc_destroy(pd_in->pd_pool_uuid);
	if (rc != 0)
		D_ERROR("Failed to destroy pool service "DF_UUID": %d\n",
			DP_UUID(pd_in->pd_pool_uuid), rc);

	if (pd_inprog->pd_rc != 0)
		rc = pd_inprog->pd_rc;
out:
	pd_out->pd_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d.\n", rc);
	if (pd_inprog_alloc == true)
		pd_inprog_destroy(pd_inprog);
	return rc;
}
