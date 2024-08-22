/*
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(st)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <math.h>

#include "self_test_lib.h"
#include "crt_utils.h"
#include <daos_errno.h>

#include <daos/agent.h>
#include <daos/mgmt.h>

/* Global shutdown flag, used to terminate the progress thread */
static int  g_shutdown_flag;
static bool g_group_inited;
static bool g_context_created;
static bool g_cart_inited;

static void *
progress_fn(void *arg)
{
	int            ret;
	crt_context_t *crt_ctx = NULL;

	crt_ctx = (crt_context_t *)arg;
	D_ASSERT(crt_ctx != NULL);
	D_ASSERT(*crt_ctx != NULL);

	while (!g_shutdown_flag) {
		ret = crt_progress(*crt_ctx, 1);
		if (ret != 0 && ret != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed; ret = %d\n", ret);
			break;
		}
	};

	pthread_exit(NULL);
}

static int
self_test_init(char *dest_name, crt_context_t *crt_ctx, crt_group_t **srv_grp, pthread_t *tid,
	       char *attach_info_path, bool listen, bool use_agent, bool no_sync)
{
	uint32_t            init_flags = 0;
	uint32_t            grp_size;
	d_rank_list_t      *rank_list      = NULL;
	int                 attach_retries = 40;
	int                 i;
	d_rank_t            max_rank = 0;
	int                 ret;
	crt_init_options_t  opt = {0};
	crt_init_options_t *init_opt;

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, attach_retries, false, false);

	if (use_agent) {
		ret = dc_agent_init();
		if (ret != 0) {
			fprintf(stderr, "dc_agent_init() failed. ret: %d\n", ret);
			return ret;
		}
		ret = crtu_dc_mgmt_net_cfg_setenv(dest_name, &opt);
		if (ret != 0) {
			D_ERROR("crtu_dc_mgmt_net_cfg_setenv() failed; ret = %d\n", ret);
			return ret;
		}

		init_opt = &opt;
	} else {
		init_opt = NULL;
	}

	if (listen)
		init_flags |= (CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);

	ret = crt_init_opt(CRT_SELF_TEST_GROUP_NAME, init_flags, init_opt);
	if (ret != 0)
		return ret;

	D_FREE(opt.cio_provider);
	D_FREE(opt.cio_interface);
	D_FREE(opt.cio_domain);
	g_cart_inited = true;

	if (attach_info_path) {
		ret = crt_group_config_path_set(attach_info_path);
		D_ASSERTF(ret == 0, "crt_group_config_path_set failed, ret = %d\n", ret);
	}

	ret = crt_context_create(crt_ctx);
	if (ret != 0) {
		D_ERROR("crt_context_create failed; ret = %d\n", ret);
		return ret;
	}
	g_context_created = true;

	if (use_agent) {
		ret = crt_group_view_create(dest_name, srv_grp);
		if (!*srv_grp || ret != 0) {
			D_ERROR("Failed to create group view; ret=%d\n", ret);
			assert(0);
		}

		ret = crtu_dc_mgmt_net_cfg_rank_add(dest_name, *srv_grp, *crt_ctx);
		if (ret != 0) {
			fprintf(stderr, "crtu_dc_mgmt_net_cfg_rank_add() failed. ret: %d\n", ret);
			return ret;
		}
	} else {
		/* DAOS-8839: Do not limit retries, instead rely on global test timeout */
		while (1) {
			ret = crt_group_attach(dest_name, srv_grp);
			if (ret == 0)
				break;
			sleep(1);
		}
	}

	if (ret != 0) {
		D_ERROR("crt_group_attach failed; ret = %d\n", ret);
		return ret;
	}

	g_group_inited = true;

	D_ASSERTF(*srv_grp != NULL, "crt_group_attach succeeded but returned group is NULL\n");

	g_shutdown_flag = 0;

	ret = pthread_create(tid, NULL, progress_fn, crt_ctx);
	if (ret != 0) {
		D_ERROR("failed to create progress thread: %s\n", strerror(errno));
		return -DER_MISC;
	}

	ret = crt_group_size(*srv_grp, &grp_size);
	D_ASSERTF(ret == 0, "crt_group_size() failed; rc=%d\n", ret);

	ret = crt_group_ranks_get(*srv_grp, &rank_list);
	D_ASSERTF(ret == 0, "crt_group_ranks_get() failed; rc=%d\n", ret);

	D_ASSERTF(rank_list != NULL, "Rank list is NULL\n");

	D_ASSERTF(rank_list->rl_nr == grp_size, "rank_list differs in size. expected %d got %d\n",
		  grp_size, rank_list->rl_nr);

	ret = crt_group_psr_set(*srv_grp, rank_list->rl_ranks[0]);
	D_ASSERTF(ret == 0, "crt_group_psr_set() failed; rc=%d\n", ret);

	/* waiting to sync with the following parameters
	 * 0 - tag 0
	 * 1 - total ctx
	 * 60 - ping timeout
	 * 120 - total timeout
	 */
	/* Only ping ranks if not using agent, and user didn't ask for no-sync  */
	if (!use_agent && !no_sync) {
		ret = crtu_wait_for_ranks(*crt_ctx, *srv_grp, rank_list, 0, 1, 60, 120);
		D_ASSERTF(ret == 0, "wait_for_ranks() failed; ret=%d\n", ret);
	}

	max_rank = rank_list->rl_ranks[0];
	for (i = 1; i < rank_list->rl_nr; i++) {
		if (rank_list->rl_ranks[i] > max_rank)
			max_rank = rank_list->rl_ranks[i];
	}

	d_rank_list_free(rank_list);

	ret = crt_rank_self_set(max_rank + 1, 1 /* group_version_min */);
	if (ret != 0) {
		D_ERROR("crt_rank_self_set failed; ret = %d\n", ret);
		return ret;
	}

	return 0;
}

int
st_compare_endpts(const void *a_in, const void *b_in)
{
	struct st_endpoint *a = (struct st_endpoint *)a_in;
	struct st_endpoint *b = (struct st_endpoint *)b_in;

	if (a->rank != b->rank)
		return a->rank > b->rank;
	return a->tag > b->tag;
}

int
st_compare_latencies_by_vals(const void *a_in, const void *b_in)
{
	struct st_latency *a = (struct st_latency *)a_in;
	struct st_latency *b = (struct st_latency *)b_in;

	if (a->val != b->val)
		return a->val > b->val;
	return a->cci_rc > b->cci_rc;
}

int
st_compare_latencies_by_ranks(const void *a_in, const void *b_in)
{
	struct st_latency *a = (struct st_latency *)a_in;
	struct st_latency *b = (struct st_latency *)b_in;

	if (a->rank != b->rank)
		return a->rank > b->rank;
	if (a->tag != b->tag)
		return a->tag > b->tag;
	if (a->val != b->val)
		return a->val > b->val;
	return a->cci_rc > b->cci_rc;
}

static void
start_test_cb(const struct crt_cb_info *cb_info)
{
	/* Result returned to main thread */
	int32_t *return_status = cb_info->cci_arg;

	/* Status retrieved from the RPC result payload */
	int32_t *reply_status;

	/* Check the status of the RPC transport itself */
	if (cb_info->cci_rc != 0) {
		*return_status = cb_info->cci_rc;
		return;
	}

	/* Get the status from the payload */
	reply_status = (int32_t *)crt_reply_get(cb_info->cci_rpc);
	D_ASSERT(reply_status != NULL);

	/* Return whatever result we got to the main thread */
	*return_status = *reply_status;
}

static void
status_req_cb(const struct crt_cb_info *cb_info)
{
	/* Result returned to main thread */
	struct crt_st_status_req_out *return_status = cb_info->cci_arg;

	/* Status retrieved from the RPC result payload */
	struct crt_st_status_req_out *reply_status;

	/* Check the status of the RPC transport itself */
	if (cb_info->cci_rc != 0) {
		return_status->status = cb_info->cci_rc;
		return;
	}

	/* Get the status from the payload */
	reply_status = crt_reply_get(cb_info->cci_rpc);
	D_ASSERT(reply_status != NULL);

	/*
	 * Return whatever result we got to the main thread
	 *
	 * Write these in specific order so we can avoid locking
	 * TODO: This assumes int32 writes are atomic
	 *   (they are on x86 if 4-byte aligned)
	 */
	return_status->test_duration_ns = reply_status->test_duration_ns;
	return_status->num_remaining    = reply_status->num_remaining;
	return_status->status           = reply_status->status;
}

static int
test_msg_size(crt_context_t crt_ctx, struct st_master_endpt *ms_endpts, uint32_t num_ms_endpts,
	      struct crt_st_start_params *test_params, struct st_latency **latencies,
	      crt_bulk_t *latencies_bulk_hdl)
{
	int                         ret;
	int                         done;
	uint32_t                    failed_count;
	uint32_t                    complete_count;
	crt_rpc_t                  *new_rpc;
	struct crt_st_start_params *start_args;
	uint32_t                    m_idx;

	/*
	 * Launch self-test 1:many sessions on each master endpoint
	 * as simultaneously as possible (don't wait for acknowledgment)
	 */
	for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
		crt_endpoint_t *endpt = &ms_endpts[m_idx].endpt;

		/* Create and send a new RPC starting the test */
		ret = crt_req_create(crt_ctx, endpt, CRT_OPC_SELF_TEST_START, &new_rpc);
		if (ret != 0) {
			D_ERROR("Creating start RPC failed to endpoint"
				" %u:%u; ret = %d\n",
				endpt->ep_rank, endpt->ep_tag, ret);
			ms_endpts[m_idx].test_failed    = 1;
			ms_endpts[m_idx].test_completed = 1;
			continue;
		}

		start_args = (struct crt_st_start_params *)crt_req_get(new_rpc);
		D_ASSERTF(start_args != NULL, "crt_req_get returned NULL\n");
		memcpy(start_args, test_params, sizeof(*test_params));
		start_args->srv_grp = test_params->srv_grp;

		/* Set the launch status to a known impossible value */
		ms_endpts[m_idx].reply.status = INT32_MAX;

		ret = crt_req_send(new_rpc, start_test_cb, &ms_endpts[m_idx].reply.status);
		if (ret != 0) {
			D_ERROR("Failed to send start RPC to endpoint %u:%u; "
				"ret = %d\n",
				endpt->ep_rank, endpt->ep_tag, ret);
			ms_endpts[m_idx].test_failed    = 1;
			ms_endpts[m_idx].test_completed = 1;
			continue;
		}
	}

	/*
	 * Wait for each node to report whether or not the test launched
	 * successfully
	 */
	do {
		/* Flag indicating all test launches have returned a status */
		done = 1;

		/* Wait a bit for tests to finish launching */
		sched_yield();

		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (ms_endpts[m_idx].reply.status == INT32_MAX) {
				/* No response yet... */
				done = 0;
				break;
			}
	} while (done != 1);

	/* Print a warning for any 1:many sessions that failed to launch */
	failed_count = 0;
	for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
		if (ms_endpts[m_idx].reply.status != 0) {
			D_ERROR("Failed to launch self-test 1:many session on"
				" %u:%u; ret = %d\n",
				ms_endpts[m_idx].endpt.ep_rank, ms_endpts[m_idx].endpt.ep_tag,
				ms_endpts[m_idx].reply.status);
			ms_endpts[m_idx].test_failed    = 1;
			ms_endpts[m_idx].test_completed = 1;
			failed_count++;
		} else if (ms_endpts[m_idx].test_failed != 0) {
			ms_endpts[m_idx].test_failed    = 1;
			ms_endpts[m_idx].test_completed = 1;
			failed_count++;
		} else {
			ms_endpts[m_idx].test_failed    = 0;
			ms_endpts[m_idx].test_completed = 0;
		}

	/* Check to make sure that at least one 1:many session was started */
	if (failed_count >= num_ms_endpts) {
		D_ERROR("Failed to launch any 1:many test sessions\n");
		return ms_endpts[0].reply.status;
	}

	/*
	 * Poll the master nodes until all tests complete
	 *   (either successfully or by returning an error)
	 */
	do {
		/* Wait a small amount of time for tests to progress */
		sleep(1);

		/* Send status requests to every non-finished node */
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
			/* Skip endpoints that have finished */
			if (ms_endpts[m_idx].test_completed != 0)
				continue;

			/* Set result status to impossible guard value */
			ms_endpts[m_idx].reply.status = INT32_MAX;

			/* Create a new RPC to check the status */
			ret = crt_req_create(crt_ctx, &ms_endpts[m_idx].endpt,
					     CRT_OPC_SELF_TEST_STATUS_REQ, &new_rpc);
			if (ret != 0) {
				D_ERROR("Creating status request RPC to"
					" endpoint %u:%u; ret = %d\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag, ret);
				ms_endpts[m_idx].test_failed    = 1;
				ms_endpts[m_idx].test_completed = 1;
				continue;
			}

			/*
			 * Sent data is the bulk handle where results should
			 * be written
			 */
			*((crt_bulk_t *)crt_req_get(new_rpc)) = latencies_bulk_hdl[m_idx];

			/* Send the status request */
			ret = crt_req_send(new_rpc, status_req_cb, &ms_endpts[m_idx].reply);
			if (ret != 0) {
				D_ERROR("Failed to send status RPC to endpoint"
					" %u:%u; ret = %d\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag, ret);
				ms_endpts[m_idx].test_failed    = 1;
				ms_endpts[m_idx].test_completed = 1;
				continue;
			}
		}

		/* Wait for all status request results to come back */
		do {
			/* Flag indicating all status requests have returned */
			done = 1;

			/* Wait a bit for status requests to be handled */
			sched_yield();

			for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
				if (ms_endpts[m_idx].reply.status == INT32_MAX &&
				    ms_endpts[m_idx].test_completed == 0) {
					/* No response yet... */
					done = 0;
					break;
				}
		} while (done != 1);

		complete_count = 0;
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
			/* Skip endpoints that have already finished */
			if (ms_endpts[m_idx].test_completed != 0) {
				complete_count++;
				continue;
			}

			switch (ms_endpts[m_idx].reply.status) {
			case CRT_ST_STATUS_TEST_IN_PROGRESS:
				D_DEBUG(DB_TEST,
					"Test still processing on "
					"%u:%u - # RPCs remaining: %u\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag,
					ms_endpts[m_idx].reply.num_remaining);
				break;
			case CRT_ST_STATUS_TEST_COMPLETE:
				ms_endpts[m_idx].test_completed = 1;
				break;
			default:
				D_ERROR("Detected test failure on %u:%u -"
					" ret = %d\n",
					ms_endpts[m_idx].endpt.ep_rank,
					ms_endpts[m_idx].endpt.ep_tag,
					ms_endpts[m_idx].reply.status);
				ms_endpts[m_idx].test_failed    = 1;
				ms_endpts[m_idx].test_completed = 1;
				complete_count++;
			}
		}
	} while (complete_count < num_ms_endpts);

	return 0;
}

void
randomize_endpoints(struct st_endpoint *endpts, uint32_t num_endpts)
{
	struct st_endpoint tmp;
	int                r_index;
	int                i;
	int                k;

	srand(time(NULL));

	printf("Randomizing order of endpoints\n");
	/* Shuffle endpoints few times */

	for (k = 0; k < 10; k++)
		for (i = 0; i < num_endpts; i++) {
			r_index = rand() % num_endpts;

			tmp             = endpts[i];
			endpts[i]       = endpts[r_index];
			endpts[r_index] = tmp;
		}

	printf("New order:\n");
	for (i = 0; i < num_endpts; i++) {
		printf("%d:%d ", endpts[i].rank, endpts[i].tag);
	}
	printf("\n");
}

void
free_size_latencies(struct st_latency ***size_latencies, uint32_t num_msg_sizes,
		    uint32_t num_ms_endpts)
{
	int size_idx;
	int m_idx;

	if (size_latencies == NULL)
		return;

	for (size_idx = 0; size_idx < num_msg_sizes; size_idx++) {
		struct st_latency **sess_latencies = size_latencies[size_idx];

		if (sess_latencies == NULL)
			continue;

		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (sess_latencies[m_idx] != NULL)
				D_FREE(sess_latencies[m_idx]);

		D_FREE(size_latencies[size_idx]);
	}
	D_FREE(size_latencies);
}

int
run_self_test(struct st_size_params all_params[], int num_msg_sizes, int rep_count,
	      int max_inflight, char *dest_name, struct st_endpoint *ms_endpts_in,
	      uint32_t num_ms_endpts_in, struct st_endpoint *endpts, uint32_t num_endpts,
	      struct st_master_endpt **ms_endpts_out, uint32_t *num_ms_endpts_out,
	      struct st_latency ****size_latencies_out, int16_t buf_alignment,
	      char *attach_info_path, bool use_agent, bool no_sync)
{
	crt_context_t           crt_ctx;
	crt_group_t            *srv_grp;
	pthread_t               tid;

	int                     size_idx;
	uint32_t                m_idx;

	int                     ret;
	int                     cleanup_ret;
	struct st_master_endpt *ms_endpts     = NULL;
	uint32_t                num_ms_endpts = 0;

	struct st_latency    ***size_latencies     = NULL;
	d_iov_t                *latencies_iov      = NULL;
	d_sg_list_t            *latencies_sg_list  = NULL;
	crt_bulk_t             *latencies_bulk_hdl = CRT_BULK_NULL;
	bool                    listen             = false;

	crt_endpoint_t          self_endpt;

	/* Sanity checks that would indicate bugs */
	D_ASSERT(endpts != NULL && num_endpts > 0);
	D_ASSERT((ms_endpts_in == NULL && num_ms_endpts_in == 0) ||
		 (ms_endpts_in != NULL && num_ms_endpts_in > 0));
	D_ASSERT(ms_endpts_out != NULL && num_ms_endpts_out != NULL);
	D_ASSERT(size_latencies_out != NULL);

	/* will send TEST_START RPC to self, so listen for incoming requests */
	if (ms_endpts_in == NULL)
		listen = true;
	/* Initialize CART */
	ret = self_test_init(dest_name, &crt_ctx, &srv_grp, &tid, attach_info_path,
			     listen /* run as server */, use_agent, no_sync);
	if (ret != 0) {
		D_ERROR("self_test_init failed; ret = %d\n", ret);
		D_GOTO(cleanup_nothread, ret);
	}

	/* Get the group/rank/tag for this application (self_endpt) */
	ret = crt_group_rank(NULL, &self_endpt.ep_rank);
	if (ret != 0) {
		D_ERROR("crt_group_rank failed; ret = %d\n", ret);
		D_GOTO(cleanup, ret);
	}
	self_endpt.ep_grp = crt_group_lookup(CRT_SELF_TEST_GROUP_NAME);
	if (self_endpt.ep_grp == NULL) {
		D_ERROR("crt_group_lookup failed for group %s\n", CRT_SELF_TEST_GROUP_NAME);
		D_GOTO(cleanup, ret = -DER_NONEXIST);
	}
	self_endpt.ep_tag = 0;

	/*
	 * Allocate a new list of unique master endpoints, each with a
	 * crt_endpoint_t and additional metadata
	 */
	if (ms_endpts_in == NULL) {
		/*
		 * If no master endpoints were specified, allocate just one and
		 * set it to self_endpt
		 */
		num_ms_endpts = 1;
		D_ALLOC_PTR(ms_endpts);
		if (ms_endpts == NULL)
			D_GOTO(cleanup, ret = -DER_NOMEM);
		ms_endpts[0].endpt.ep_rank = self_endpt.ep_rank;
		ms_endpts[0].endpt.ep_tag  = self_endpt.ep_tag;
		ms_endpts[0].endpt.ep_grp  = self_endpt.ep_grp;
	} else {
		/*
		 * If master endpoints were specified, initially allocate enough
		 * space to hold all of them, but only unique master endpoints
		 * to the new list
		 */
		D_ALLOC_ARRAY(ms_endpts, num_ms_endpts_in);
		if (ms_endpts == NULL)
			D_GOTO(cleanup, ret = -DER_NOMEM);

		/*
		 * Sort the supplied endpoints to make it faster to identify
		 * duplicates
		 */
		qsort(ms_endpts_in, num_ms_endpts_in, sizeof(ms_endpts_in[0]), st_compare_endpts);

		/* Add the first element to the new list */
		ms_endpts[0].endpt.ep_rank = ms_endpts_in[0].rank;
		ms_endpts[0].endpt.ep_tag  = ms_endpts_in[0].tag;
		/*
		 * TODO: This isn't right - it should be self_endpt.ep_grp.
		 * However, this requires changes elsewhere - this is tracked
		 * by CART-187.
		 *
		 * As implemented here, rank 0 tag 0 in the client group will
		 * be used as the master endpoint by default
		 */
		ms_endpts[0].endpt.ep_grp = srv_grp;
		num_ms_endpts             = 1;

		/*
		 * Add unique elements to the new list
		 */
		for (m_idx = 1; m_idx < num_ms_endpts_in; m_idx++)
			if ((ms_endpts_in[m_idx].rank !=
			     ms_endpts[num_ms_endpts - 1].endpt.ep_rank) ||
			    (ms_endpts_in[m_idx].tag !=
			     ms_endpts[num_ms_endpts - 1].endpt.ep_tag)) {
				ms_endpts[num_ms_endpts].endpt.ep_rank = ms_endpts_in[m_idx].rank;
				ms_endpts[num_ms_endpts].endpt.ep_tag  = ms_endpts_in[m_idx].tag;
				ms_endpts[num_ms_endpts].endpt.ep_grp  = srv_grp;
				num_ms_endpts++;
			}

		/*
		 * If the counts don't match up, some were duplicates - resize
		 * the resulting smaller array which contains only unique
		 * entries
		 */
		if (num_ms_endpts != num_ms_endpts_in) {
			struct st_master_endpt *realloc_ptr;

			D_REALLOC_ARRAY(realloc_ptr, ms_endpts, num_ms_endpts_in, num_ms_endpts);
			if (realloc_ptr == NULL)
				D_GOTO(cleanup, ret = -DER_NOMEM);
			ms_endpts = realloc_ptr;
		}
	}

	/* Allocate latency lists for each size */
	D_ALLOC_ARRAY(size_latencies, num_msg_sizes);
	if (size_latencies == NULL)
		D_GOTO(cleanup, ret = -DER_NOMEM);

	/* Allocate latency lists for each 1:many session per size */
	for (size_idx = 0; size_idx < num_msg_sizes; size_idx++) {
		D_ALLOC_ARRAY(size_latencies[size_idx], num_ms_endpts);
		if (size_latencies[size_idx] == NULL)
			D_GOTO(cleanup, ret = -DER_NOMEM);
	}
	D_ALLOC_ARRAY(latencies_iov, num_ms_endpts);
	if (latencies_iov == NULL)
		D_GOTO(cleanup, ret = -DER_NOMEM);
	D_ALLOC_ARRAY(latencies_sg_list, num_ms_endpts);
	if (latencies_sg_list == NULL)
		D_GOTO(cleanup, ret = -DER_NOMEM);
	D_ALLOC_ARRAY(latencies_bulk_hdl, num_ms_endpts);
	if (latencies_bulk_hdl == NULL)
		D_GOTO(cleanup, ret = -DER_NOMEM);

	for (size_idx = 0; size_idx < num_msg_sizes; size_idx++) {
		struct crt_st_start_params test_params = {0};
		struct st_latency        **latencies   = size_latencies[size_idx];

		D_ASSERT(latencies != NULL);

		/*
		 * For each 1:many session, allocate an array for latency results.
		 * Map that array to an IOV, and create a bulk handle that will be used
		 * to transfer latency results back into that buffer
		 */
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++) {
			D_ALLOC_ARRAY(latencies[m_idx], rep_count);
			if (latencies[m_idx] == NULL)
				D_GOTO(cleanup, ret = -DER_NOMEM);
			d_iov_set(&latencies_iov[m_idx], latencies[m_idx],
				  rep_count * sizeof(**latencies));
			latencies_sg_list[m_idx].sg_iovs = &latencies_iov[m_idx];
			latencies_sg_list[m_idx].sg_nr   = 1;

			ret = crt_bulk_create(crt_ctx, &latencies_sg_list[m_idx], CRT_BULK_RW,
					      &latencies_bulk_hdl[m_idx]);
			if (ret != 0) {
				D_ERROR("Failed to allocate latencies bulk handle;"
					" ret = %d\n",
					ret);
				D_GOTO(cleanup, ret);
			}
			D_ASSERT(latencies_bulk_hdl != CRT_BULK_NULL);
		}

		/* Set test parameters to send to the test node */
		d_iov_set(&test_params.endpts, endpts, num_endpts * sizeof(*endpts));
		test_params.rep_count     = rep_count;
		test_params.max_inflight  = max_inflight;
		test_params.send_size     = all_params[size_idx].send_size;
		test_params.reply_size    = all_params[size_idx].reply_size;
		test_params.send_type     = all_params[size_idx].send_type;
		test_params.reply_type    = all_params[size_idx].reply_type;
		test_params.buf_alignment = buf_alignment;
		test_params.srv_grp       = dest_name;

		ret = test_msg_size(crt_ctx, ms_endpts, num_ms_endpts, &test_params, latencies,
				    latencies_bulk_hdl);
		if (ret != 0) {
			D_ERROR("Testing message size (%d-%s %d-%s) failed;"
				" ret = %d\n",
				test_params.send_size, crt_st_msg_type_str[test_params.send_type],
				test_params.reply_size, crt_st_msg_type_str[test_params.reply_type],
				ret);
			D_GOTO(cleanup, ret);
		}

		/* Clean up this size iteration's handles */
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (latencies_bulk_hdl[m_idx] != CRT_BULK_NULL)
				crt_bulk_free(latencies_bulk_hdl[m_idx]);
	}

cleanup:
	/* Tell the progress thread to abort and exit */
	g_shutdown_flag = 1;

	if (pthread_join(tid, NULL)) {
		D_ERROR("Could not join progress thread\n");
		ret = -1;
	}

cleanup_nothread:
	if (latencies_bulk_hdl != NULL) {
		for (m_idx = 0; m_idx < num_ms_endpts; m_idx++)
			if (latencies_bulk_hdl[m_idx] != CRT_BULK_NULL)
				crt_bulk_free(latencies_bulk_hdl[m_idx]);
		D_FREE(latencies_bulk_hdl);
	}
	if (latencies_sg_list != NULL)
		D_FREE(latencies_sg_list);
	if (latencies_iov != NULL)
		D_FREE(latencies_iov);
	if (ret != 0) {
		if (ms_endpts != NULL)
			D_FREE(ms_endpts);
		free_size_latencies(size_latencies, num_msg_sizes, num_ms_endpts);
	} else {
		*size_latencies_out = size_latencies;
		*ms_endpts_out      = ms_endpts;
		*num_ms_endpts_out  = num_ms_endpts;
	}

	if (srv_grp != NULL && g_group_inited) {
		cleanup_ret = crt_group_detach(srv_grp);
		if (cleanup_ret != 0)
			D_ERROR("crt_group_detach failed; ret = %d\n", cleanup_ret);
		/* Make sure first error is returned, if applicable */
		ret = ((ret == 0) ? cleanup_ret : ret);
	}

	cleanup_ret = 0;
	if (g_context_created) {
		cleanup_ret = crt_context_destroy(crt_ctx, 0);
		if (cleanup_ret != 0)
			D_ERROR("crt_context_destroy failed; ret = %d\n", cleanup_ret);
	}

	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);

	cleanup_ret = 0;
	if (g_cart_inited) {
		cleanup_ret = crt_finalize();
		if (cleanup_ret != 0)
			D_ERROR("crt_finalize failed; ret = %d\n", cleanup_ret);
	}
	/* Make sure first error is returned, if applicable */
	ret = ((ret == 0) ? cleanup_ret : ret);
	return ret;
}