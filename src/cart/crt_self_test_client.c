/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(st)

#include <pthread.h>
#include "crt_internal.h"

#define ISBULK(type) ((type) == CRT_SELF_TEST_MSG_TYPE_BULK_GET || \
		      (type) == CRT_SELF_TEST_MSG_TYPE_BULK_PUT)

#define ST_SET_NUM_INFLIGHT(newval)					\
	do {								\
		D_SPIN_LOCK(&g_data->ctr_lock);				\
		D_ASSERT(g_data->num_inflight == 0);			\
		g_data->num_inflight = (newval);			\
		D_SPIN_UNLOCK(&g_data->ctr_lock);			\
	} while (0)

#define ST_DEC_NUM_INFLIGHT()						\
	do {								\
		D_SPIN_LOCK(&g_data->ctr_lock);				\
		D_ASSERT(g_data->num_inflight > 0);			\
		g_data->num_inflight--;					\
		D_SPIN_UNLOCK(&g_data->ctr_lock);			\
	} while (0)

struct st_test_endpt {
	uint32_t rank;
	uint32_t tag;

	/* Session ID to use when sending messages to this endpoint */
	int64_t session_id;

	/*
	 * If this endpoint is detected as evicted, no more messages should be
	 * sent to it
	 */
	uint8_t evicted;
};

/*
 * "public" data that is the same for all ongoing test messages.
 *
 * A client can only manage one active test at any one time.
 *   (This is a single 1:many test instance)
 * However, many:many can be performed by running multiple 1:many tests
 *   simultaneously (using different nodes as the 1:many instances)
 */
struct st_g_data {
	crt_context_t			  crt_ctx;
	crt_group_t			 *srv_grp;

	/* Test parameters */
	uint32_t			  rep_count;
	uint32_t			  max_inflight;
	uint32_t			  send_size;
	uint32_t			  reply_size;
	int16_t				  buf_alignment;
	enum crt_st_msg_type		  send_type;
	enum crt_st_msg_type		  reply_type;

	/* Private arguments data for all RPC callback functions */
	struct st_cb_args		**cb_args_ptrs;

	/* Used to measure individual RPC latencies */
	struct st_latency		 *rep_latencies;

	/* Bulk descriptor used to transfer the above latencies */
	d_iov_t				  rep_latencies_iov;
	d_sg_list_t			  rep_latencies_sg_list;
	crt_bulk_t			  rep_latencies_bulk_hdl;

	/* List of endpoints to test against */
	struct st_test_endpt		 *endpts;

	/* Number of endpoints in the endpts array */
	uint32_t			  num_endpts;

	/* Start / stop times for this test run */
	struct timespec			  time_start;
	struct timespec			  time_stop;

	/* Used to protect the following counters across threads */
	pthread_spinlock_t		  ctr_lock;

	/* Set to nonzero only after the entire test cycle has completed */
	uint32_t			  test_complete;

	/*
	 * Used to track how many RPCs have been sent so far
	 * NOTE: Read/Write-protected by ctr_lock
	 */
	uint32_t			  rep_sent_count;

	/*
	 * Used to track how many RPCs have been completed so far
	 * NOTE: Write-protected by ctr_lock
	 */
	uint32_t			  rep_completed_count;

	/*
	 * Last used endpoint index
	 * NOTE: Write-protected by ctr_lock
	 */
	uint32_t			  next_endpt_idx;

	/*
	 * Used to track how many RPCs are currently in-flight
	 * NOTE: Write-protected by ctr_lock
	 */
	uint32_t			  num_inflight;
};

/*
 * An instance of this structure exists per in-flight RPC to serve as the
 * "private" data for each repetition
 */
struct st_cb_args {
	int			 rep_idx;
	struct timespec		 sent_time;
	struct st_test_endpt	*endpt;

	crt_bulk_t		 bulk_hdl;
	d_sg_list_t		 sg_list;
	d_iov_t			 sg_iov;

	/* Length of the buf array */
	size_t			 buf_len;

	/*
	 * Extra space used for the payload of this repetition
	 *
	 * Size is determined by whether the reply uses BULK or not:
	 * if reply is bulk
	 *   size = max(g_data->send_size, g_data->reply_size)
	 * else
	 *   size = g_data->send_size;
	 */
	char			*buf;
};

/********************* Forward Declarations *********************/
static void test_rpc_cb(const struct crt_cb_info *cb_info);

/********************* Global data *********************/
/* Data structure with all information about an ongoing test from this client */
static struct st_g_data *g_data;
/*
 * Lock used to protect the g_data pointer
 *
 * Locking g_data_lock is only necessary in start() and status() (when free'ing)
 * The rest of the functions that use it are only reachable when g_data is valid
 */
static pthread_mutex_t g_data_lock;

void crt_self_test_client_init(void)
{
	D_MUTEX_INIT(&g_data_lock, NULL);
}

void crt_self_test_client_fini(void)
{
	D_MUTEX_DESTROY(&g_data_lock);
}

static void
close_session_cb(const struct crt_cb_info *cb_info)
{
	struct st_test_endpt	*endpt = cb_info->cci_arg;

	D_ASSERT(endpt != NULL);
	D_ASSERT(g_data != NULL);

	if (cb_info->cci_rc != 0)
		D_WARN("Close session failed for endpoint=%u:%u\n",
		       endpt->rank, endpt->tag);

	/* Decrement the number of in-flight RPCs now that this one is done */
	ST_DEC_NUM_INFLIGHT();

	if (g_data->num_inflight == 0)
		g_data->test_complete = 1;
}

static void close_sessions(void)
{
	uint32_t	num_close_sent = 0;
	uint32_t	i;
	int		ret;

	/* Serious bug if we get here with no g_data or some outstanding RPCs */
	D_ASSERT(g_data != NULL);
	D_ASSERT(g_data->num_inflight == 0);
	D_ASSERT(g_data->num_endpts > 0);
	D_ASSERT(g_data->endpts != NULL);

	/* Planning to send RPCs equal to the number of endpoints */
	ST_SET_NUM_INFLIGHT(g_data->num_endpts);

	/*
	 * Dispatch a close to every specified endpoint
	 * If at any point sending to an endpoint fails, mark it as evicted
	 */
	for (i = 0; i < g_data->num_endpts; i++) {
		crt_endpoint_t	 local_endpt = {0};
		crt_rpc_t	*new_rpc;
		int64_t		*args;

		/* Don't bother to close sessions for nodes where open failed */
		if (g_data->endpts[i].session_id < 0) {
			/* No actual send - decrement the in-flight counter */
			ST_DEC_NUM_INFLIGHT();

			continue;
		}

		local_endpt.ep_grp = g_data->srv_grp;
		local_endpt.ep_rank = g_data->endpts[i].rank;
		local_endpt.ep_tag = g_data->endpts[i].tag;

		/* Start a new RPC request */
		ret = crt_req_create(g_data->crt_ctx, &local_endpt,
				     CRT_OPC_SELF_TEST_CLOSE_SESSION,
				     &new_rpc);
		if (ret != 0) {
			D_WARN("Failed to close session %ld on endpoint=%u:%u;"
			       " crt_req_created failed with ret = %d\n",
			       g_data->endpts[i].session_id,
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			/* Mark the node as evicted (likely already done) */
			g_data->endpts[i].evicted = 1;

			/* Sending failed - decrement the in-flight counter */
			ST_DEC_NUM_INFLIGHT();

			continue;
		}
		D_ASSERTF(new_rpc != NULL,
			  "crt_req_create succeeded but RPC is NULL\n");

		args = crt_req_get(new_rpc);
		D_ASSERTF(args != NULL, "crt_req_get returned NULL\n");

		*args = g_data->endpts[i].session_id;

		/* Send the RPC */
		ret = crt_req_send(new_rpc, close_session_cb,
				   &g_data->endpts[i]);
		if (ret != 0) {
			D_WARN("crt_req_send failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			g_data->endpts[i].session_id = -1;
			g_data->endpts[i].evicted = 1;

			/* Sending failed - decrement the in-flight counter */
			ST_DEC_NUM_INFLIGHT();

			continue;
		}

		/* Successfully sent this close request - increment counter */
		num_close_sent++;
	}

	if (num_close_sent == 0)
		g_data->test_complete = 1;
}

/*
 * This function sends an RPC to the next available endpoint.
 *
 * If sending fails for any reason, the endpoint is marked as evicted and the
 * function attempts to send to the next endpoint in the list until none remain.
 * This function will only return a non-zero value if there are no remaining
 * endpoints that it is possible to send a message to, or if d_gettime()
 * fails.
 *
 * skip_inc_complete is a flag that, when set to anything but zero, skips
 * incrementing the rep_completed_count - this is useful when generating the
 * initial RPCs
 */
static void send_next_rpc(struct st_cb_args *cb_args, int skip_inc_complete)
{
	crt_rpc_t		*new_rpc;
	void			*args = NULL;
	crt_endpoint_t		 local_endpt = {0};
	struct st_test_endpt	*endpt_ptr;
	crt_opcode_t		 opcode;
	uint32_t		 failed_endpts;

	uint32_t		 local_rep;
	int			 ret;

	D_ASSERT(cb_args != NULL);
	D_ASSERT(g_data != NULL);
	D_ASSERT(g_data->num_endpts > 0);
	D_ASSERT(g_data->endpts != NULL);

	/******************** LOCK: ctr_lock ********************/
	D_SPIN_LOCK(&g_data->ctr_lock);

	/* Only mark completion of an RPC if requested */
	if (skip_inc_complete == 0)
		g_data->rep_completed_count += 1;

	/* Get an index for a message that still needs to be sent */
	local_rep = g_data->rep_sent_count;
	if (g_data->rep_sent_count < g_data->rep_count)
		g_data->rep_sent_count += 1;

	D_SPIN_UNLOCK(&g_data->ctr_lock);
	/******************* UNLOCK: ctr_lock *******************/

	/* Only send another message if one is left to send */
	if (local_rep >= g_data->rep_count)
		D_GOTO(abort, ret = 0);

	/*
	 * Loop until either:
	 * - Detect that no more RPCs need to be sent
	 * - A new RPC message is sent successfully
	 * - All endpoints are marked as evicted and it is impossible to send
	 *   another message
	 * - d_gettime() fails (which shouldn't happen)
	 *
	 * In each of these cases the relevant code will return without needing
	 * to break the loop
	 */
	while (1) {
		/******************** LOCK: ctr_lock ********************/
		D_SPIN_LOCK(&g_data->ctr_lock);

		/* Get the next non-evicted endpoint to send a message to */
		failed_endpts = 0;
		do {
			if (failed_endpts >= g_data->num_endpts) {
				D_ERROR("No non-evicted endpoints remaining\n");
				D_SPIN_UNLOCK(&g_data->ctr_lock);
				/************** UNLOCK: ctr_lock **************/
				D_GOTO(abort, ret = -DER_UNREACH);
			}
			failed_endpts++;

			endpt_ptr = &g_data->endpts[g_data->next_endpt_idx];
			g_data->next_endpt_idx++;
			if (g_data->next_endpt_idx >= g_data->num_endpts)
				g_data->next_endpt_idx = 0;
		} while (endpt_ptr->evicted != 0);

		D_SPIN_UNLOCK(&g_data->ctr_lock);
		/******************* UNLOCK: ctr_lock *******************/

		local_endpt.ep_grp = g_data->srv_grp;
		local_endpt.ep_rank = endpt_ptr->rank;
		local_endpt.ep_tag = endpt_ptr->tag;

		/* Re-use payload data memory, set arguments */
		cb_args->rep_idx = local_rep;

		/*
		 * For the repetition we are just now generating, set which
		 * rank/tag this upcoming latency measurement will be for
		 */
		g_data->rep_latencies[cb_args->rep_idx].rank =
			local_endpt.ep_rank;
		g_data->rep_latencies[cb_args->rep_idx].tag =
			local_endpt.ep_tag;

		/*
		 * Determine which opcode (and thus underlying structures)
		 * should be used for this test message
		 */
		opcode = crt_st_compute_opcode(g_data->send_type,
					       g_data->reply_type);

		/* Start a new RPC request */
		ret = crt_req_create(g_data->crt_ctx, &local_endpt,
				     opcode, &new_rpc);
		if (ret != 0) {
			D_WARN("crt_req_create failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			goto try_again;
		}

		D_ASSERTF(new_rpc != NULL,
			  "crt_req_create succeeded but RPC is NULL\n");

		/* No arguments to assemble for BOTH_EMPTY RPCs */
		if (opcode == CRT_OPC_SELF_TEST_BOTH_EMPTY)
			goto send_rpc;

		/* Get the arguments handle */
		args = crt_req_get(new_rpc);
		D_ASSERTF(args != NULL, "crt_req_get returned NULL\n");

		/* Session ID is always the first field */
		*((int64_t *)args) = endpt_ptr->session_id;

		switch (opcode) {
		case CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY:
		case CRT_OPC_SELF_TEST_BOTH_IOV:
			{
				struct crt_st_send_id_iov *typed_args =
					(struct crt_st_send_id_iov *)args;

				D_ASSERT(cb_args->buf_len >= g_data->send_size);
				d_iov_set(&typed_args->buf,
					 crt_st_get_aligned_ptr(cb_args->buf,
						g_data->buf_alignment),
					 g_data->send_size);
			}
			break;
		case CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK:
			{
				struct crt_st_send_id_iov_bulk *typed_args =
					(struct crt_st_send_id_iov_bulk *)args;

				D_ASSERT(cb_args->buf_len >= g_data->send_size);
				d_iov_set(&typed_args->buf,
					 crt_st_get_aligned_ptr(cb_args->buf,
						g_data->buf_alignment),
					 g_data->send_size);
				typed_args->bulk_hdl = cb_args->bulk_hdl;
				D_ASSERT(typed_args->bulk_hdl != CRT_BULK_NULL);
			}
			break;
		case CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV:
		case CRT_OPC_SELF_TEST_BOTH_BULK:
			{
				struct crt_st_send_id_bulk *typed_args =
					(struct crt_st_send_id_bulk *)args;

				typed_args->bulk_hdl = cb_args->bulk_hdl;
				D_ASSERT(typed_args->bulk_hdl != CRT_BULK_NULL);
			}
			break;
		}

/* Good - This in-flight RPC perpetuates itself */
send_rpc:
		/* Give the callback a pointer to this endpoint entry */
		cb_args->endpt = endpt_ptr;

		ret = d_gettime(&cb_args->sent_time);
		if (ret != 0) {
			D_ERROR("d_gettime failed; ret = %d\n", ret);

			/* Free the RPC request that was created but not sent */
			RPC_PUB_DECREF(new_rpc);
			D_GOTO(abort, ret = -DER_MISC);
		}

		/* Send the RPC */
		ret = crt_req_send(new_rpc, test_rpc_cb, cb_args);
		if (ret != 0) {
			D_WARN("crt_req_send failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			goto try_again;
		}

		/* RPC sent successfully */
		return;

/* Still have a local_rep that needs sending; have to try again */
try_again:
		/*
		 * Something must be wrong with this endpoint
		 * Mark it as evicted and try a different one instead
		 */
		D_WARN("Marking endpoint endpoint=%u:%u as evicted\n",
		       local_endpt.ep_rank, local_endpt.ep_tag);

		/*
		 * No need to lock g_data->ctr_lock here
		 *
		 * Lock or no lock the worst that can happen is that
		 * send_next_rpc() attempts to send another RPC to this endpoint
		 * and crt_req_send fails and the endpoint gets re-marked as
		 * evicted
		 */
		endpt_ptr->evicted = 1;
	}

/*
 * Either there are no more RPCs that need sending or something fatal happened
 * and another RPC cannot be sent.
 */
abort:
	/*
	 * Since it is impossible to send another RPC, there is now one less
	 * RPC in-flight
	 */
	ST_DEC_NUM_INFLIGHT();

	if (g_data->num_inflight == 0) {
		/* Record the time right when we finished this size */
		ret = d_gettime(&g_data->time_stop);
		if (ret != 0)
			D_ERROR("d_gettime failed; ret = %d\n", ret);

		close_sessions();
	}
}

/* A note about how arguments are passed to this callback:
 *
 * The main test function allocates an arguments array with one slot for each
 * of the max_inflight_rpcs. The main loop then instantiates
 * max_inflight_rpcs, passing into the callback data pointer for each one its
 * own private pointer to the slot it can use in the arguments array. Each
 * time the callback is called (and needs to generate another RPC), it can
 * re-use the previous slot allocated to it as callback data for the RPC it is
 * just now creating.
 */
static void
test_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct st_cb_args	*cb_args = cb_info->cci_arg;
	struct timespec		 now;
	int			 ret;

	D_ASSERT(cb_args != NULL);
	D_ASSERT(g_data != NULL);

	/* Record latency of this RPC */
	ret = d_gettime(&now);
	if (ret != 0) {
		D_ERROR("d_gettime failed; ret = %d\n", ret);
		return;
	}

	g_data->rep_latencies[cb_args->rep_idx].val =
		d_timediff_ns(&cb_args->sent_time, &now);

	/* Record return code */
	g_data->rep_latencies[cb_args->rep_idx].cci_rc = cb_info->cci_rc;

	/* If this endpoint was evicted during the RPC, mark it as so */
	if (cb_info->cci_rc == -DER_OOG) {
		D_WARN("Test RPC failed with -DER_OOG for endpoint=%u:%u;"
		       " marking it as evicted\n",
		       cb_args->endpt->rank, cb_args->endpt->tag);
		/*
		 * No need to lock g_data->ctr_lock here
		 *
		 * Lock or no lock the worst that can happen is that
		 * send_next_rpc() attempts to send another RPC to this endpoint
		 * and crt_req_send fails and the endpoint gets re-marked as
		 * evicted
		 */
		cb_args->endpt->evicted = 1;
	}

	send_next_rpc(cb_args, 0);
}

static void launch_test_rpcs(void)
{
	uint32_t	inflight_idx;
	int		ret;

	/* Serious bug if we get here with no g_data or some outstanding RPCs */
	D_ASSERT(g_data != NULL);
	D_ASSERT(g_data->num_inflight == 0);
	D_ASSERT(g_data->max_inflight > 0);

	/* Record the time right when we start processing this size */
	ret = d_gettime(&g_data->time_start);
	if (ret != 0) {
		D_ERROR("d_gettime failed; ret = %d\n", ret);
		/* No point in continuing if time is broken */
		close_sessions();
	}

	/* Attempt to send the requested number of in-flight RPCs */
	ST_SET_NUM_INFLIGHT(g_data->max_inflight);

	/* Launch max_inflight separate RPCs to get the test started */
	for (inflight_idx = 0; inflight_idx < g_data->max_inflight;
	     inflight_idx++)
		send_next_rpc(g_data->cb_args_ptrs[inflight_idx], 1);
}

static void
open_session_cb(const struct crt_cb_info *cb_info)
{
	struct st_test_endpt	*endpt = cb_info->cci_arg;
	int64_t			*session_id;

	D_ASSERT(endpt != NULL);
	D_ASSERT(g_data != NULL);

	/* Get the session ID from the response message */
	session_id = crt_reply_get(cb_info->cci_rpc);
	D_ASSERT(session_id != NULL);

	/* If this endpoint returned any kind of error, mark it is evicted */
	if (cb_info->cci_rc != 0) {
		D_WARN("Got cci_rc = %d while opening session with endpoint"
		       " %u:%u - removing it from the list of endpoints\n",
		       cb_info->cci_rc, endpt->rank, endpt->tag);
		/* Nodes with evicted=1 are skipped for the rest of the test */
		endpt->evicted = 1;
		endpt->session_id = -1;
	} else if (*session_id < 0) {
		D_WARN("Got invalid session id = %ld from endpoint %u:%u -\n"
		       " removing it from the list of endpoints\n",
		       *session_id, endpt->rank, endpt->tag);
		endpt->evicted = 1;
		endpt->session_id = -1;
	} else {
		/* Got a valid session_id - associate it with this endpoint */
		endpt->session_id = *session_id;
	}

	/* Decrement the number of in-flight RPCs now that this one is done */
	ST_DEC_NUM_INFLIGHT();

	if (g_data->num_inflight == 0)
		launch_test_rpcs();
}

static void open_sessions(void)
{
	uint32_t	num_open_sent = 0;
	uint32_t	i;
	int		ret;

	/* Serious bug if we get here with no g_data or some outstanding RPCs */
	D_ASSERT(g_data != NULL);
	D_ASSERT(g_data->num_inflight == 0);
	D_ASSERT(g_data->num_endpts > 0);
	D_ASSERT(g_data->endpts != NULL);

	/* Sessions are not required for (EMPTY EMPTY) */
	if (g_data->send_type == CRT_SELF_TEST_MSG_TYPE_EMPTY &&
	    g_data->reply_type == CRT_SELF_TEST_MSG_TYPE_EMPTY) {
		for (i = 0; i < g_data->num_endpts; i++)
			g_data->endpts[i].session_id = -1;
		launch_test_rpcs();
		return;
	}

	/* Planning to send RPCs equal to the number of endpoints */
	ST_SET_NUM_INFLIGHT(g_data->num_endpts);

	/*
	 * Dispatch an open to every specified endpoint
	 * If at any point sending to an endpoint fails, mark it as evicted
	 */
	for (i = 0; i < g_data->num_endpts; i++) {
		crt_endpoint_t			 local_endpt = {0};
		crt_rpc_t			*new_rpc;
		struct crt_st_session_params	*args;

		local_endpt.ep_grp = g_data->srv_grp;
		local_endpt.ep_rank = g_data->endpts[i].rank;
		local_endpt.ep_tag = g_data->endpts[i].tag;

		/* Start a new RPC request */
		ret = crt_req_create(g_data->crt_ctx, &local_endpt,
				     CRT_OPC_SELF_TEST_OPEN_SESSION,
				     &new_rpc);
		if (ret != 0) {
			D_WARN("crt_req_create failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			g_data->endpts[i].session_id = -1;
			g_data->endpts[i].evicted = 1;

			/* Sending failed - decrement the in-flight counter */
			ST_DEC_NUM_INFLIGHT();
		}
		D_ASSERTF(new_rpc != NULL,
			  "crt_req_create succeeded but RPC is NULL\n");

		args = crt_req_get(new_rpc);
		D_ASSERTF(args != NULL, "crt_req_get returned NULL\n");

		/* Copy test parameters */
		args->send_size = g_data->send_size;
		args->reply_size = g_data->reply_size;
		args->send_type = g_data->send_type;
		args->reply_type = g_data->reply_type;
		args->buf_alignment = g_data->buf_alignment;

		/*
		 * Set the number of buffers that the service should allocate.
		 * This is the maximum number of RPCs that the service should
		 * expect to see at any one time.
		 *
		 * TODO: Note this may have to change when randomizing endpoints
		 */
		args->num_buffers =
			max(1, min(g_data->max_inflight / g_data->num_endpts,
				   g_data->rep_count));

		/* Send the RPC */
		ret = crt_req_send(new_rpc, open_session_cb,
				   &g_data->endpts[i]);
		if (ret != 0) {
			D_WARN("crt_req_send failed for endpoint=%u:%u;"
			       " ret = %d\n",
			       local_endpt.ep_rank, local_endpt.ep_tag, ret);

			g_data->endpts[i].session_id = -1;
			g_data->endpts[i].evicted = 1;

			/* Sending failed - decrement the in-flight counter */
			ST_DEC_NUM_INFLIGHT();
		}

		/* Successfully sent this close request - increment counter */
		num_open_sent++;

		continue;
	}

	if (num_open_sent == 0)
		launch_test_rpcs();
}

/*
 * Frees the global data structure used to manage test sessions
 * Caller MUST be holding the g_data_lock
 */
static void free_g_data(void)
{
	uint32_t alloc_idx;

	if (g_data == NULL)
		return;

	if (g_data->cb_args_ptrs != NULL) {
		for (alloc_idx = 0; alloc_idx < g_data->max_inflight;
		     alloc_idx++) {
			struct st_cb_args *cb_args =
				g_data->cb_args_ptrs[alloc_idx];

			if (cb_args != NULL) {
				if (cb_args->bulk_hdl != CRT_BULK_NULL) {
					crt_bulk_free(cb_args->bulk_hdl);
					cb_args->bulk_hdl = NULL;
				}
				D_FREE(cb_args->buf);

				/*
				 * Free and zero the actual pointer, not
				 * the local copy
				 */
				D_FREE(g_data->cb_args_ptrs[alloc_idx]);
			}
		}

		D_FREE(g_data->cb_args_ptrs);
	}
	if (g_data->rep_latencies_bulk_hdl != CRT_BULK_NULL) {
		crt_bulk_free(g_data->rep_latencies_bulk_hdl);
		g_data->rep_latencies_bulk_hdl = CRT_BULK_NULL;
	}
	D_FREE(g_data->rep_latencies);
	D_FREE(g_data->endpts);
	D_FREE(g_data);
}

void
crt_self_test_start_handler(crt_rpc_t *rpc_req)
{
	struct crt_st_start_params	*args;
	int32_t				*reply_status;
	uint32_t			 alloc_idx;
	int				 ret;
	size_t				 alloc_buf_len;
	size_t				 test_buf_len;
	uint32_t			 local_rep;
	uint32_t			 endpt_idx;

	/* Get pointers to the arguments and response buffers */
	args = crt_req_get(rpc_req);
	D_ASSERT(args != NULL);

	reply_status = crt_reply_get(rpc_req);
	D_ASSERT(reply_status != NULL);

	/******************** LOCK: g_data_lock ********************/
	D_MUTEX_LOCK(&g_data_lock);

	/* Validate the input */
	if (((args->endpts.iov_buf_len & 0x7) != 0) ||
	    (args->endpts.iov_buf_len == 0)) {
		D_ERROR("Invalid IOV length - must be a multiple of 8 bytes\n");
		D_GOTO(send_reply, ret = -DER_INVAL);
	}
	if (args->send_type == CRT_SELF_TEST_MSG_TYPE_BULK_PUT ||
	    args->reply_type == CRT_SELF_TEST_MSG_TYPE_BULK_GET) {
		D_ERROR("Invalid self-test bulk type;"
			" only send/get reply/put are supported\n");
		D_GOTO(send_reply, ret = -DER_INVAL);
	}
	if (args->max_inflight == 0) {
		D_ERROR("Max in-flight must be greater than zero\n");
		D_GOTO(send_reply, ret = -DER_INVAL);
	}
	if (args->rep_count == 0) {
		D_ERROR("Rep count must be greater than zero\n");
		D_GOTO(send_reply, ret = -DER_INVAL);
	}
	if ((args->buf_alignment < CRT_ST_BUF_ALIGN_MIN ||
	     args->buf_alignment > CRT_ST_BUF_ALIGN_MAX) &&
	     args->buf_alignment != CRT_ST_BUF_ALIGN_DEFAULT) {
		D_ERROR("Buf alignment must be in the range [%d:%d]\n",
			CRT_ST_BUF_ALIGN_MIN, CRT_ST_BUF_ALIGN_MAX);
		D_GOTO(send_reply, ret = -DER_INVAL);
	}

	/*
	 * Allocate a new global tracking structure that is the same for all
	 *   callbacks.
	 * If a previously requested test session is still running, fail.
	 * If a previous session completed but the results were never
	 *   collected, free those results and start a new session.
	 */
	if (g_data != NULL && g_data->test_complete == 0) {
		D_ERROR("Failed to start a new test run - one still exists\n");
		D_GOTO(send_reply, ret = -DER_BUSY);
	} else {
		free_g_data();
	}

	D_ALLOC_PTR(g_data);
	if (g_data == NULL)
		D_GOTO(fail_cleanup, ret = -DER_NOMEM);

	/* Initialize the global callback data */
	g_data->crt_ctx = rpc_req->cr_ctx;
	g_data->srv_grp = crt_group_lookup(args->srv_grp);
	g_data->rep_count = args->rep_count;
	g_data->max_inflight = args->max_inflight;
	g_data->send_size = args->send_size;
	g_data->reply_size = args->reply_size;
	g_data->send_type = args->send_type;
	g_data->buf_alignment = args->buf_alignment;
	g_data->reply_type = args->reply_type;
	g_data->num_endpts = args->endpts.iov_buf_len / 8;
	ret = D_SPIN_INIT(&g_data->ctr_lock, PTHREAD_PROCESS_PRIVATE);
	if (ret != 0)
		D_GOTO(fail_cleanup, ret);

	/* Allocate a buffer for the list of endpoints */
	D_ALLOC_ARRAY(g_data->endpts, g_data->num_endpts);
	if (g_data->endpts == NULL)
		D_GOTO(fail_cleanup, ret = -DER_NOMEM);

	/* Copy the endpoint data from the caller */
	for (endpt_idx = 0; endpt_idx < g_data->num_endpts; endpt_idx++) {
		g_data->endpts[endpt_idx].rank =
			((uint32_t *)(args->endpts.iov_buf))[endpt_idx * 2];
		g_data->endpts[endpt_idx].tag =
			((uint32_t *)(args->endpts.iov_buf))[endpt_idx * 2 + 1];
	}

	/* Allocate a buffer for latency measurements */
	D_ALLOC_ARRAY(g_data->rep_latencies, g_data->rep_count);
	if (g_data->rep_latencies == NULL)
		D_GOTO(fail_cleanup, ret = -DER_NOMEM);

	/*
	 * Set up a bulk descriptor to use later to send the latencies back
	 * to the self-test requester
	 */
	d_iov_set(&g_data->rep_latencies_iov, g_data->rep_latencies,
		 g_data->rep_count * sizeof(g_data->rep_latencies[0]));
	g_data->rep_latencies_sg_list.sg_iovs = &g_data->rep_latencies_iov;
	g_data->rep_latencies_sg_list.sg_nr = 1;
	ret = crt_bulk_create(g_data->crt_ctx, &g_data->rep_latencies_sg_list,
			      CRT_BULK_RO, &g_data->rep_latencies_bulk_hdl);
	if (ret != 0) {
		D_ERROR("Failed to allocate latencies bulk handle; ret = %d\n",
			ret);
		D_GOTO(fail_cleanup, ret);
	}
	D_ASSERT(g_data->rep_latencies_bulk_hdl != CRT_BULK_NULL);

	/*
	 * Allocate an array of pointers to keep track of private
	 * per-inflight-rpc buffers
	 */
	D_ALLOC_ARRAY(g_data->cb_args_ptrs, g_data->max_inflight);
	if (g_data->cb_args_ptrs == NULL)
		D_GOTO(fail_cleanup, ret = -DER_NOMEM);

	/*
	 * Compute the amount of space needed for this test run
	 * Note that if bulk is used for the reply, need to make sure
	 * this is big enough for the bulk reply to be written to
	 */
	if (ISBULK(g_data->reply_type))
		test_buf_len = max(g_data->send_size, g_data->reply_size);
	else
		test_buf_len = g_data->send_size;

	/*
	 * If the user requested that messages be aligned, add additional
	 * space so that a requested aligned value will always be present
	 *
	 * Note that CRT_ST_BUF_ALIGN_MAX is required to be one less than a
	 * power of two
	 */
	if (g_data->buf_alignment != CRT_ST_BUF_ALIGN_DEFAULT)
		alloc_buf_len = test_buf_len + CRT_ST_BUF_ALIGN_MAX;
	else
		alloc_buf_len = test_buf_len;

	/* Allocate "private" buffers for each in-flight RPC */
	for (alloc_idx = 0; alloc_idx < g_data->max_inflight; alloc_idx++) {
		struct st_cb_args *cb_args;

		D_ALLOC_PTR(g_data->cb_args_ptrs[alloc_idx]);
		if (g_data->cb_args_ptrs[alloc_idx] == NULL)
			D_GOTO(fail_cleanup, ret = -DER_NOMEM);

		/*
		 * Now that this pointer can't change, get a shorter
		 * alias for it
		 */
		cb_args = g_data->cb_args_ptrs[alloc_idx];

		/*
		 * Free the buffer attached to this RPC instance if
		 * there is one from the previous size
		 */
		D_FREE(cb_args->buf);

		/* No buffer needed if there is no payload */
		if (test_buf_len == 0)
			continue;

		/* Allocate a new data buffer for this in-flight RPC */
		D_ALLOC(cb_args->buf, alloc_buf_len);
		if (cb_args->buf == NULL)
			D_GOTO(fail_cleanup, ret = -DER_NOMEM);

		/* Fill the buffer with an arbitrary data pattern */
		memset(cb_args->buf, 0xC5, alloc_buf_len);

		/* Track how big the buffer is for bookkeeping */
		cb_args->buf_len = alloc_buf_len;

		/*
		 * Link the sg_list, iov's, and cb_args entries
		 *
		 * Note that here the length is the length of the actual
		 * buffer; this will probably need to be changed when it
		 * comes time to actually do a bulk transfer
		 */
		cb_args->sg_list.sg_iovs = &cb_args->sg_iov;
		cb_args->sg_list.sg_nr = 1;
		d_iov_set(&cb_args->sg_iov,
			 crt_st_get_aligned_ptr(cb_args->buf,
						g_data->buf_alignment),
			 test_buf_len);

		/* Create bulk handle if required */
		if (ISBULK(g_data->send_type) || ISBULK(g_data->reply_type)) {
			crt_bulk_perm_t perms =
				ISBULK(g_data->reply_type) ?
					CRT_BULK_RW : CRT_BULK_RO;

			ret = crt_bulk_create(g_data->crt_ctx,
					      &cb_args->sg_list,
					      perms, &cb_args->bulk_hdl);
			if (ret != 0) {
				D_ERROR("crt_bulk_create failed; ret = %d\n",
					ret);
				D_GOTO(fail_cleanup, ret);
			}
			D_ASSERT(cb_args->bulk_hdl != CRT_BULK_NULL);
		}
	}

	/* Initialize the latencies to -1 to indicate invalid data */
	for (local_rep = 0; local_rep < g_data->rep_count; local_rep++)
		g_data->rep_latencies[local_rep].val = -1;

	/* Next phase - open sessions with every endpoint */
	open_sessions();

	/*
	 * If we got this far, a test session is in progress - indicate to the
	 * caller that launching the test was successful
	 */
	D_GOTO(send_reply, ret = 0);

fail_cleanup:
	free_g_data();

send_reply:
	*reply_status = ret;

	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		D_ERROR("crt_reply_send failed; ret = %d\n", ret);

	D_MUTEX_UNLOCK(&g_data_lock);
	/******************* UNLOCK: g_data_lock *******************/
}

static int status_req_bulk_put_cb(const struct crt_bulk_cb_info *cb_info)
{
	char hostname[1024] = {0};
	struct crt_st_status_req_out	*res = cb_info->bci_arg;
	int				 ret;

	res->num_remaining = 0;
	res->test_duration_ns =
		d_timediff_ns(&g_data->time_start, &g_data->time_stop);
	gethostname(hostname, 1024);
	res->status = CRT_ST_STATUS_TEST_COMPLETE;

	if (cb_info->bci_rc != 0) {
		D_ERROR("BULK_PUT of latency results failed; bci_rc=%d\n",
			cb_info->bci_rc);
		res->status = cb_info->bci_rc;
	}

	ret = crt_reply_send(cb_info->bci_bulk_desc->bd_rpc);
	if (ret != 0)
		D_ERROR("crt_reply_send failed; ret = %d\n", ret);

	RPC_PUB_DECREF(cb_info->bci_bulk_desc->bd_rpc);

	D_MUTEX_UNLOCK(&g_data_lock);
	/******************* UNLOCK: g_data_lock *******************/

	/* Reply sent successfully - don't need to hang onto results anymore */
	free_g_data();

	return 0;
}

void
crt_self_test_status_req_handler(crt_rpc_t *rpc_req)
{
	crt_bulk_t			*bulk_hdl_in;
	struct crt_st_status_req_out	*res;
	int				 ret;

	/*
	 * Increment the reference counter for this RPC
	 * It is decremented after the reply is sent
	 */
	RPC_PUB_ADDREF(rpc_req);

	bulk_hdl_in = crt_req_get(rpc_req);
	D_ASSERT(bulk_hdl_in != NULL);
	D_ASSERT(*bulk_hdl_in != CRT_BULK_NULL);

	res = crt_reply_get(rpc_req);
	D_ASSERT(res != NULL);

	/* Default response values if no test data is available */
	res->test_duration_ns = -1;
	res->num_remaining = UINT32_MAX;
	res->status = CRT_ST_STATUS_INVAL;

	/******************** LOCK: g_data_lock ********************/
	D_MUTEX_LOCK(&g_data_lock);

	/*
	 * If this thread acquired the lock and g_data is not null, it must have
	 * completed the entire start function and either be complete or busy
	 * working on the test
	 */
	if (g_data != NULL && g_data->test_complete == 1) {
		size_t			bulk_in_len;
		struct crt_bulk_desc	bulk_desc;

		/*
		 * Test finished! Need to transfer the results
		 *
		 * The callback will take care of unlocking g_data_lock - it
		 * needs to be held until the reply is sent, and it can't be
		 * sent until the bulk transfer is complete and the local
		 * buffers are released
		 */

		ret = crt_bulk_get_len(*bulk_hdl_in, &bulk_in_len);
		if (ret != 0) {
			D_ERROR("Failed to get bulk handle length; ret = %d\n",
				ret);
			res->status = ret;
			D_GOTO(send_rpc, res->status = ret);
		}

		/* Validate the bulk handle length from the caller */
		if (bulk_in_len != (g_data->rep_count *
				    sizeof(g_data->rep_latencies[0]))) {
			D_ERROR("Bulk handle length mismatch (%zu != %zu)\n",
				bulk_in_len,
				(g_data->rep_count *
				 sizeof(g_data->rep_latencies[0])));
			D_GOTO(send_rpc, res->status = ret);
		}

		bulk_desc.bd_rpc = rpc_req;
		bulk_desc.bd_bulk_op = CRT_BULK_PUT;
		bulk_desc.bd_remote_hdl = *bulk_hdl_in;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = g_data->rep_latencies_bulk_hdl;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = bulk_in_len;

		ret = crt_bulk_transfer(&bulk_desc, status_req_bulk_put_cb,
					res, NULL);
		if (ret != 0) {
			D_ERROR("bulk transfer of latencies failed; ret = %d\n",
				ret);
			D_GOTO(send_rpc, res->status = ret);
		}

		return;
	} else if (g_data != NULL) {
		/*
		 * Test still going - try to return some status info
		 * Note that num_remaining may be zero if the test is close
		 *   to completion but is still closing sessions, etc.
		 */
		res->status = CRT_ST_STATUS_TEST_IN_PROGRESS;
		res->num_remaining =
			g_data->rep_count - g_data->rep_completed_count;
	}

send_rpc:
	ret = crt_reply_send(rpc_req);
	if (ret != 0)
		D_ERROR("crt_reply_send failed; ret = %d\n", ret);

	RPC_PUB_DECREF(rpc_req);

	D_MUTEX_UNLOCK(&g_data_lock);
	/******************* UNLOCK: g_data_lock *******************/
}
